/*
 * Copyright (C) 2006, Jamie McCracken <jamiemcc@gnome.org>
 * Copyright (C) 2008, Nokia <ivan.frade@nokia.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 */

#include "config.h"

#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>

#include <libtracker-common/tracker-common.h>
#include <libtracker-sparql/tracker-sparql.h>

#include "tracker-class.h"
#include "tracker-data-manager.h"
#include "tracker-data-update.h"
#include "tracker-data-query.h"
#include "tracker-db-interface-sqlite.h"
#include "tracker-db-manager.h"
#include "tracker-db-journal.h"
#include "tracker-ontologies.h"
#include "tracker-property.h"
#include "tracker-sparql-query.h"

typedef struct _TrackerDataUpdateBuffer TrackerDataUpdateBuffer;
typedef struct _TrackerDataUpdateBufferResource TrackerDataUpdateBufferResource;
typedef struct _TrackerDataUpdateBufferPredicate TrackerDataUpdateBufferPredicate;
typedef struct _TrackerDataUpdateBufferProperty TrackerDataUpdateBufferProperty;
typedef struct _TrackerDataUpdateBufferTable TrackerDataUpdateBufferTable;
typedef struct _TrackerDataBlankBuffer TrackerDataBlankBuffer;
typedef struct _TrackerStatementDelegate TrackerStatementDelegate;
typedef struct _TrackerCommitDelegate TrackerCommitDelegate;

struct _TrackerDataUpdateBuffer {
	/* string -> integer */
	GHashTable *resource_cache;
	/* string -> TrackerDataUpdateBufferResource */
	GHashTable *resources;
	/* integer -> TrackerDataUpdateBufferResource */
	GHashTable *resources_by_id;

	/* the following two fields are valid per sqlite transaction, not just for same subject */
	/* TrackerClass -> integer */
	GHashTable *class_counts;

#if HAVE_TRACKER_FTS
	gboolean fts_ever_updated;
#endif
};

struct _TrackerDataUpdateBufferResource {
	const gchar *subject;
	gint id;
	gboolean create;
	gboolean modified;
	/* TrackerProperty -> GArray */
	GHashTable *predicates;
	/* string -> TrackerDataUpdateBufferTable */
	GHashTable *tables;
	/* TrackerClass */
	GPtrArray *types;

#if HAVE_TRACKER_FTS
	gboolean fts_updated;
#endif
};

struct _TrackerDataUpdateBufferProperty {
	const gchar *name;
	GValue value;
	gint graph;
	gboolean date_time : 1;

#if HAVE_TRACKER_FTS
	gboolean fts : 1;
#endif
};

struct _TrackerDataUpdateBufferTable {
	gboolean insert;
	gboolean delete_row;
	gboolean delete_value;
	gboolean multiple_values;
	TrackerClass *class;
	/* TrackerDataUpdateBufferProperty */
	GArray *properties;
};

/* buffer for anonymous blank nodes
 * that are not yet in the database */
struct _TrackerDataBlankBuffer {
	GHashTable *table;
	gchar *subject;
	/* string */
	GArray *predicates;
	/* string */
	GArray *objects;
	/* string */
	GArray *graphs;
};

struct _TrackerStatementDelegate {
	TrackerStatementCallback callback;
	gpointer user_data;
};

struct _TrackerCommitDelegate {
	TrackerCommitCallback callback;
	gpointer user_data;
};

typedef struct {
	gchar *graph;
	gchar *subject;
	gchar *predicate;
	gchar *object;
	gboolean is_uri;
} QueuedStatement;

struct _TrackerData {
	GObject parent_instance;

	TrackerDataManager *manager;

	gboolean in_transaction;
	gboolean in_ontology_transaction;
	gboolean in_journal_replay;
	TrackerDataUpdateBuffer update_buffer;

	/* current resource */
	TrackerDataUpdateBufferResource *resource_buffer;
	TrackerDataBlankBuffer blank_buffer;
	time_t resource_time;
	gint transaction_modseq;
	gboolean has_persistent;

	GPtrArray *insert_callbacks;
	GPtrArray *delete_callbacks;
	GPtrArray *commit_callbacks;
	GPtrArray *rollback_callbacks;
	gint max_service_id;
	gint max_ontology_id;

	TrackerDBJournal *journal_writer;
};

struct _TrackerDataClass {
	GObjectClass parent_class;
};

enum {
	PROP_0,
	PROP_MANAGER
};

G_DEFINE_TYPE (TrackerData, tracker_data, G_TYPE_OBJECT);

static gint         ensure_resource_id         (TrackerData      *data,
                                                const gchar      *uri,
                                                gboolean         *create);
static void         cache_insert_value         (TrackerData      *data,
                                                const gchar      *table_name,
                                                const gchar      *field_name,
                                                gboolean          transient,
                                                GValue           *value,
                                                gint              graph,
                                                gboolean          multiple_values,
                                                gboolean          fts,
                                                gboolean          date_time);
static GArray      *get_old_property_values    (TrackerData      *data,
                                                TrackerProperty  *property,
                                                GError          **error);
static gchar*       gvalue_to_string           (TrackerPropertyType  type,
                                                GValue           *gvalue);
static gboolean     delete_metadata_decomposed (TrackerData      *data,
                                                TrackerProperty  *property,
                                                const gchar      *value,
                                                gint              value_id,
                                                GError          **error);
static void         resource_buffer_switch     (TrackerData *data,
                                                const gchar *graph,
                                                const gchar *subject,
                                                gint         subject_id);

void
tracker_data_add_commit_statement_callback (TrackerData             *data,
                                            TrackerCommitCallback    callback,
                                            gpointer                 user_data)
{
	TrackerCommitDelegate *delegate = g_new0 (TrackerCommitDelegate, 1);

	if (!data->commit_callbacks) {
		data->commit_callbacks = g_ptr_array_new_with_free_func (g_free);
	}

	delegate->callback = callback;
	delegate->user_data = user_data;

	g_ptr_array_add (data->commit_callbacks, delegate);
}

void
tracker_data_remove_commit_statement_callback (TrackerData           *data,
                                               TrackerCommitCallback  callback,
                                               gpointer               user_data)
{
	TrackerCommitDelegate *delegate;
	guint i;

	if (!data->commit_callbacks) {
		return;
	}

	for (i = 0; i < data->commit_callbacks->len; i++) {
		delegate = g_ptr_array_index (data->commit_callbacks, i);
		if (delegate->callback == callback && delegate->user_data == user_data) {
			g_ptr_array_remove_index (data->commit_callbacks, i);
			return;
		}
	}
}

void
tracker_data_add_rollback_statement_callback (TrackerData             *data,
                                              TrackerCommitCallback    callback,
                                              gpointer                 user_data)
{
	TrackerCommitDelegate *delegate = g_new0 (TrackerCommitDelegate, 1);

	if (!data->rollback_callbacks) {
		data->rollback_callbacks = g_ptr_array_new_with_free_func (g_free);
	}

	delegate->callback = callback;
	delegate->user_data = user_data;

	g_ptr_array_add (data->rollback_callbacks, delegate);
}


void
tracker_data_remove_rollback_statement_callback (TrackerData          *data,
                                                 TrackerCommitCallback callback,
                                                 gpointer              user_data)
{
	TrackerCommitDelegate *delegate;
	guint i;

	if (!data->rollback_callbacks) {
		return;
	}

	for (i = 0; i < data->rollback_callbacks->len; i++) {
		delegate = g_ptr_array_index (data->rollback_callbacks, i);
		if (delegate->callback == callback && delegate->user_data == user_data) {
			g_ptr_array_remove_index (data->rollback_callbacks, i);
			return;
		}
	}
}

void
tracker_data_add_insert_statement_callback (TrackerData             *data,
                                            TrackerStatementCallback callback,
                                            gpointer                 user_data)
{
	TrackerStatementDelegate *delegate = g_new0 (TrackerStatementDelegate, 1);

	if (!data->insert_callbacks) {
		data->insert_callbacks = g_ptr_array_new_with_free_func (g_free);
	}

	delegate->callback = callback;
	delegate->user_data = user_data;

	g_ptr_array_add (data->insert_callbacks, delegate);
}

void
tracker_data_remove_insert_statement_callback (TrackerData             *data,
                                               TrackerStatementCallback callback,
                                               gpointer                 user_data)
{
	TrackerStatementDelegate *delegate;
	guint i;

	if (!data->insert_callbacks) {
		return;
	}

	for (i = 0; i < data->insert_callbacks->len; i++) {
		delegate = g_ptr_array_index (data->insert_callbacks, i);
		if (delegate->callback == callback && delegate->user_data == user_data) {
			g_ptr_array_remove_index (data->insert_callbacks, i);
			return;
		}
	}
}

void
tracker_data_add_delete_statement_callback (TrackerData             *data,
                                            TrackerStatementCallback callback,
                                            gpointer                 user_data)
{
	TrackerStatementDelegate *delegate = g_new0 (TrackerStatementDelegate, 1);

	if (!data->delete_callbacks) {
		data->delete_callbacks = g_ptr_array_new_with_free_func (g_free);
	}

	delegate->callback = callback;
	delegate->user_data = user_data;

	g_ptr_array_add (data->delete_callbacks, delegate);
}

void
tracker_data_remove_delete_statement_callback (TrackerData             *data,
                                               TrackerStatementCallback callback,
                                               gpointer                 user_data)
{
	TrackerStatementDelegate *delegate;
	guint i;

	if (!data->delete_callbacks) {
		return;
	}

	for (i = 0; i < data->delete_callbacks->len; i++) {
		delegate = g_ptr_array_index (data->delete_callbacks, i);
		if (delegate->callback == callback && delegate->user_data == user_data) {
			g_ptr_array_remove_index (data->delete_callbacks, i);
			return;
		}
	}
}

static gint
tracker_data_update_get_new_service_id (TrackerData *data)
{
	TrackerDBCursor    *cursor = NULL;
	TrackerDBInterface *iface;
	TrackerDBStatement *stmt;
	GError *error = NULL;

	if (data->in_ontology_transaction) {
		if (G_LIKELY (data->max_ontology_id != 0)) {
			return ++data->max_ontology_id;
		}

		iface = tracker_data_manager_get_writable_db_interface (data->manager);

		stmt = tracker_db_interface_create_statement (iface, TRACKER_DB_STATEMENT_CACHE_TYPE_SELECT, &error,
		                                              "SELECT MAX(ID) AS A FROM Resource WHERE ID <= %d", TRACKER_ONTOLOGIES_MAX_ID);

		if (stmt) {
			cursor = tracker_db_statement_start_cursor (stmt, &error);
			g_object_unref (stmt);
		}

		if (cursor) {
			if (tracker_db_cursor_iter_next (cursor, NULL, &error)) {
				data->max_ontology_id = MAX (tracker_db_cursor_get_int (cursor, 0), data->max_ontology_id);
			}

			g_object_unref (cursor);
		}

		if (G_UNLIKELY (error)) {
			g_warning ("Could not get new resource ID for ontology transaction: %s\n", error->message);
			g_error_free (error);
		}

		return ++data->max_ontology_id;
	} else {
		if (G_LIKELY (data->max_service_id != 0)) {
			return ++data->max_service_id;
		}

		data->max_service_id = TRACKER_ONTOLOGIES_MAX_ID;

		iface = tracker_data_manager_get_writable_db_interface (data->manager);

		stmt = tracker_db_interface_create_statement (iface, TRACKER_DB_STATEMENT_CACHE_TYPE_SELECT, &error,
		                                              "SELECT MAX(ID) AS A FROM Resource");

		if (stmt) {
			cursor = tracker_db_statement_start_cursor (stmt, &error);
			g_object_unref (stmt);
		}

		if (cursor) {
			if (tracker_db_cursor_iter_next (cursor, NULL, &error)) {
				data->max_service_id = MAX (tracker_db_cursor_get_int (cursor, 0), data->max_service_id);
			}

			g_object_unref (cursor);
		}

		if (G_UNLIKELY (error)) {
			g_warning ("Could not get new resource ID: %s\n", error->message);
			g_error_free (error);
		}

		return ++data->max_service_id;
	}
}

static gint
tracker_data_update_get_next_modseq (TrackerData *data)
{
	TrackerDBCursor    *cursor = NULL;
	TrackerDBInterface *temp_iface;
	TrackerDBStatement *stmt;
	GError             *error = NULL;
	gint                max_modseq = 0;

	temp_iface = tracker_data_manager_get_writable_db_interface (data->manager);

	stmt = tracker_db_interface_create_statement (temp_iface, TRACKER_DB_STATEMENT_CACHE_TYPE_SELECT, &error,
	                                              "SELECT MAX(\"tracker:modified\") AS A FROM \"rdfs:Resource\"");

	if (stmt) {
		cursor = tracker_db_statement_start_cursor (stmt, &error);
		g_object_unref (stmt);
	}

	if (cursor) {
		if (tracker_db_cursor_iter_next (cursor, NULL, &error)) {
			max_modseq = MAX (tracker_db_cursor_get_int (cursor, 0), max_modseq);
		}

		g_object_unref (cursor);
	}

	if (G_UNLIKELY (error)) {
		g_warning ("Could not get new resource ID: %s\n", error->message);
		g_error_free (error);
	}

	return ++max_modseq;
}

static void
tracker_data_init (TrackerData *data)
{
}

static void
tracker_data_set_property (GObject      *object,
                           guint         prop_id,
                           const GValue *value,
                           GParamSpec   *pspec)
{
	TrackerData *data = TRACKER_DATA (object);

	switch (prop_id) {
	case PROP_MANAGER:
		data->manager = g_value_get_object (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
tracker_data_get_property (GObject    *object,
                           guint       prop_id,
                           GValue     *value,
                           GParamSpec *pspec)
{
	TrackerData *data = TRACKER_DATA (object);

	switch (prop_id) {
	case PROP_MANAGER:
		g_value_set_object (value, data->manager);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
tracker_data_class_init (TrackerDataClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->set_property = tracker_data_set_property;
	object_class->get_property = tracker_data_get_property;

	g_object_class_install_property (object_class,
	                                 PROP_MANAGER,
	                                 g_param_spec_object ("manager",
	                                                      "manager",
	                                                      "manager",
	                                                      TRACKER_TYPE_DATA_MANAGER,
	                                                      G_PARAM_READWRITE |
	                                                      G_PARAM_CONSTRUCT_ONLY));
}

TrackerData *
tracker_data_new (TrackerDataManager *manager)
{
	return g_object_new (TRACKER_TYPE_DATA,
	                     "manager", manager,
	                     NULL);
}

static gint
get_transaction_modseq (TrackerData *data)
{
	if (G_UNLIKELY (data->transaction_modseq == 0)) {
		data->transaction_modseq = tracker_data_update_get_next_modseq (data);
	}

	/* Always use 1 for ontology transactions */
	if (data->in_ontology_transaction) {
		return 1;
	}

	return data->transaction_modseq;
}

static TrackerDataUpdateBufferTable *
cache_table_new (gboolean multiple_values)
{
	TrackerDataUpdateBufferTable *table;

	table = g_slice_new0 (TrackerDataUpdateBufferTable);
	table->multiple_values = multiple_values;
	table->properties = g_array_sized_new (FALSE, FALSE, sizeof (TrackerDataUpdateBufferProperty), 4);

	return table;
}

static void
cache_table_free (TrackerDataUpdateBufferTable *table)
{
	TrackerDataUpdateBufferProperty *property;
	gint                            i;

	for (i = 0; i < table->properties->len; i++) {
		property = &g_array_index (table->properties, TrackerDataUpdateBufferProperty, i);
		g_value_unset (&property->value);
	}

	g_array_free (table->properties, TRUE);
	g_slice_free (TrackerDataUpdateBufferTable, table);
}

static TrackerDataUpdateBufferTable *
cache_ensure_table (TrackerData *data,
                    const gchar *table_name,
                    gboolean     multiple_values,
                    gboolean     transient)
{
	TrackerDataUpdateBufferTable *table;

	if (!data->resource_buffer->modified && !transient) {
		/* first modification of this particular resource, update tracker:modified */

		GValue gvalue = { 0 };

		data->resource_buffer->modified = TRUE;

		g_value_init (&gvalue, G_TYPE_INT64);
		g_value_set_int64 (&gvalue, get_transaction_modseq (data));
		cache_insert_value (data, "rdfs:Resource", "tracker:modified",
		                    TRUE, &gvalue, 0,
		                    FALSE, FALSE, FALSE);
	}

	table = g_hash_table_lookup (data->resource_buffer->tables, table_name);
	if (table == NULL) {
		table = cache_table_new (multiple_values);
		g_hash_table_insert (data->resource_buffer->tables, g_strdup (table_name), table);
		table->insert = multiple_values;
	}

	return table;
}

static void
cache_insert_row (TrackerData  *data,
                  TrackerClass *class)
{
	TrackerDataUpdateBufferTable *table;

	table = cache_ensure_table (data, tracker_class_get_name (class), FALSE, FALSE);
	table->class = class;
	table->insert = TRUE;
}

static void
cache_insert_value (TrackerData *data,
                    const gchar *table_name,
                    const gchar *field_name,
                    gboolean     transient,
                    GValue      *value,
                    gint         graph,
                    gboolean     multiple_values,
                    gboolean     fts,
                    gboolean     date_time)
{
	TrackerDataUpdateBufferTable    *table;
	TrackerDataUpdateBufferProperty  property;

	/* No need to strdup here, the incoming string is either always static, or
	 * long-standing as tracker_property_get_name return value. */
	property.name = field_name;

	property.value = *value;
	property.graph = graph;
#if HAVE_TRACKER_FTS
	property.fts = fts;
#endif
	property.date_time = date_time;

	table = cache_ensure_table (data, table_name, multiple_values, transient);
	g_array_append_val (table->properties, property);
}

static void
cache_delete_row (TrackerData  *data,
                  TrackerClass *class)
{
	TrackerDataUpdateBufferTable    *table;

	table = cache_ensure_table (data, tracker_class_get_name (class), FALSE, FALSE);
	table->class = class;
	table->delete_row = TRUE;
}

static void
cache_delete_value (TrackerData *data,
                    const gchar *table_name,
                    const gchar *field_name,
                    gboolean     transient,
                    GValue      *value,
                    gboolean     multiple_values,
                    gboolean     fts,
                    gboolean     date_time)
{
	TrackerDataUpdateBufferTable    *table;
	TrackerDataUpdateBufferProperty  property;

	property.name = field_name;
	property.value = *value;
	property.graph = 0;
#if HAVE_TRACKER_FTS
	property.fts = fts;
#endif
	property.date_time = date_time;

	table = cache_ensure_table (data, table_name, multiple_values, transient);
	table->delete_value = TRUE;
	g_array_append_val (table->properties, property);
}

static gint
query_resource_id (TrackerData *data,
                   const gchar *uri)
{
	TrackerDBInterface *iface;
	gint id;

	id = GPOINTER_TO_INT (g_hash_table_lookup (data->update_buffer.resource_cache, uri));
	iface = tracker_data_manager_get_writable_db_interface (data->manager);

	if (id == 0) {
		id = tracker_data_query_resource_id (data->manager, iface, uri);

		if (id) {
			g_hash_table_insert (data->update_buffer.resource_cache, g_strdup (uri), GINT_TO_POINTER (id));
		}
	}

	return id;
}

static gint
ensure_resource_id (TrackerData *data,
                    const gchar *uri,
                    gboolean    *create)
{
	TrackerDBInterface *iface;
	TrackerDBStatement *stmt;
	GError *error = NULL;
	gint id;

	id = query_resource_id (data, uri);

	if (create) {
		*create = (id == 0);
	}

	if (id == 0) {
		iface = tracker_data_manager_get_writable_db_interface (data->manager);

		id = tracker_data_update_get_new_service_id (data);
		stmt = tracker_db_interface_create_statement (iface, TRACKER_DB_STATEMENT_CACHE_TYPE_UPDATE, &error,
		                                              "INSERT INTO Resource (ID, Uri) VALUES (?, ?)");

		if (stmt) {
			tracker_db_statement_bind_int (stmt, 0, id);
			tracker_db_statement_bind_text (stmt, 1, uri);
			tracker_db_statement_execute (stmt, &error);
			g_object_unref (stmt);
		}

		if (error) {
			g_critical ("Could not ensure resource existence: %s", error->message);
			g_error_free (error);
		}

#ifndef DISABLE_JOURNAL
		if (!data->in_journal_replay) {
			tracker_db_journal_append_resource (data->journal_writer, id, uri);
		}
#endif /* DISABLE_JOURNAL */

		g_hash_table_insert (data->update_buffer.resource_cache, g_strdup (uri), GINT_TO_POINTER (id));
	}

	return id;
}

static gint
ensure_graph_id (TrackerData *data,
                 const gchar *uri,
		 gboolean    *create)
{
	TrackerDBInterface *iface;
	TrackerDBStatement *stmt;
	GError *error = NULL;
	gint id;

	id = GPOINTER_TO_INT (g_hash_table_lookup (data->update_buffer.resource_cache, uri));
	if (id != 0)
		return id;

	id = ensure_resource_id (data, uri, create);
	iface = tracker_data_manager_get_writable_db_interface (data->manager);
	stmt = tracker_db_interface_create_statement (iface, TRACKER_DB_STATEMENT_CACHE_TYPE_UPDATE, &error,
	                                              "INSERT OR IGNORE INTO Graph (ID) VALUES (?)");

	if (stmt) {
		tracker_db_statement_bind_int (stmt, 0, id);
		tracker_db_statement_execute (stmt, &error);
		g_object_unref (stmt);
	}

	if (error) {
		g_critical ("Could not ensure graph existence: %s", error->message);
		g_error_free (error);
	}

	return id;
}

static void
statement_bind_gvalue (TrackerDBStatement *stmt,
                       gint               *idx,
                       const GValue       *value)
{
	GType type;

	type = G_VALUE_TYPE (value);
	switch (type) {
	case G_TYPE_STRING:
		tracker_db_statement_bind_text (stmt, (*idx)++, g_value_get_string (value));
		break;
	case G_TYPE_INT64:
		tracker_db_statement_bind_int (stmt, (*idx)++, g_value_get_int64 (value));
		break;
	case G_TYPE_DOUBLE:
		tracker_db_statement_bind_double (stmt, (*idx)++, g_value_get_double (value));
		break;
	default:
		if (type == TRACKER_TYPE_DATE_TIME) {
			tracker_db_statement_bind_double (stmt, (*idx)++, tracker_date_time_get_time (value));
			tracker_db_statement_bind_int (stmt, (*idx)++, tracker_date_time_get_local_date (value));
			tracker_db_statement_bind_int (stmt, (*idx)++, tracker_date_time_get_local_time (value));
		} else {
			g_warning ("Unknown type for binding: %s\n", G_VALUE_TYPE_NAME (value));
		}
		break;
	}
}

static void
add_class_count (TrackerData  *data,
                 TrackerClass *class,
                 gint          count)
{
	gint old_count_entry;

	tracker_class_set_count (class, tracker_class_get_count (class) + count);

	/* update class_counts table so that the count change can be reverted in case of rollback */
	if (!data->update_buffer.class_counts) {
		data->update_buffer.class_counts = g_hash_table_new (g_direct_hash, g_direct_equal);
	}

	old_count_entry = GPOINTER_TO_INT (g_hash_table_lookup (data->update_buffer.class_counts, class));
	g_hash_table_insert (data->update_buffer.class_counts, class,
	                     GINT_TO_POINTER (old_count_entry + count));
}

static void
tracker_data_resource_buffer_flush (TrackerData  *data,
                                    GError      **error)
{
	TrackerDBInterface             *iface;
	TrackerDBStatement             *stmt;
	TrackerDataUpdateBufferTable    *table;
	TrackerDataUpdateBufferProperty *property;
	GHashTableIter                  iter;
	const gchar                    *table_name;
	gint                            i, param;
	GError                         *actual_error = NULL;

	iface = tracker_data_manager_get_writable_db_interface (data->manager);

	g_hash_table_iter_init (&iter, data->resource_buffer->tables);
	while (g_hash_table_iter_next (&iter, (gpointer*) &table_name, (gpointer*) &table)) {
		if (table->multiple_values) {
			for (i = 0; i < table->properties->len; i++) {
				property = &g_array_index (table->properties, TrackerDataUpdateBufferProperty, i);

				if (table->delete_value) {
					/* delete rows for multiple value properties */
					stmt = tracker_db_interface_create_statement (iface, TRACKER_DB_STATEMENT_CACHE_TYPE_UPDATE, &actual_error,
					                                              "DELETE FROM \"%s\" WHERE ID = ? AND \"%s\" = ?",
					                                              table_name,
					                                              property->name);
				} else if (property->date_time) {
					stmt = tracker_db_interface_create_statement (iface, TRACKER_DB_STATEMENT_CACHE_TYPE_UPDATE, &actual_error,
					                                              "INSERT OR IGNORE INTO \"%s\" (ID, \"%s\", \"%s:localDate\", \"%s:localTime\", \"%s:graph\") VALUES (?, ?, ?, ?, ?)",
					                                              table_name,
					                                              property->name,
					                                              property->name,
					                                              property->name,
					                                              property->name);
				} else {
					stmt = tracker_db_interface_create_statement (iface, TRACKER_DB_STATEMENT_CACHE_TYPE_UPDATE, &actual_error,
					                                              "INSERT OR IGNORE INTO \"%s\" (ID, \"%s\", \"%s:graph\") VALUES (?, ?, ?)",
					                                              table_name,
					                                              property->name,
					                                              property->name);
				}

				if (actual_error) {
					g_propagate_error (error, actual_error);
					return;
				}

				param = 0;

				tracker_db_statement_bind_int (stmt, param++, data->resource_buffer->id);
				statement_bind_gvalue (stmt, &param, &property->value);

				if (property->graph != 0) {
					tracker_db_statement_bind_int (stmt, param++, property->graph);
				} else {
					tracker_db_statement_bind_null (stmt, param++);
				}

				tracker_db_statement_execute (stmt, &actual_error);
				g_object_unref (stmt);

				if (actual_error) {
					g_propagate_error (error, actual_error);
					return;
				}
			}
		} else {
			GString *sql, *values_sql;

			if (table->delete_row) {
				/* remove entry from rdf:type table */
				stmt = tracker_db_interface_create_statement (iface, TRACKER_DB_STATEMENT_CACHE_TYPE_UPDATE, &actual_error,
				                                              "DELETE FROM \"rdfs:Resource_rdf:type\" WHERE ID = ? AND \"rdf:type\" = ?");

				if (stmt) {
					tracker_db_statement_bind_int (stmt, 0, data->resource_buffer->id);
					tracker_db_statement_bind_int (stmt, 1, ensure_resource_id (data, tracker_class_get_uri (table->class), NULL));
					tracker_db_statement_execute (stmt, &actual_error);
					g_object_unref (stmt);
				}

				if (actual_error) {
					g_propagate_error (error, actual_error);
					return;
				}

				if (table->class) {
					add_class_count (data, table->class, -1);
				}

				/* remove row from class table */
				stmt = tracker_db_interface_create_statement (iface, TRACKER_DB_STATEMENT_CACHE_TYPE_UPDATE, &actual_error,
				                                              "DELETE FROM \"%s\" WHERE ID = ?", table_name);

				if (stmt) {
					tracker_db_statement_bind_int (stmt, 0, data->resource_buffer->id);
					tracker_db_statement_execute (stmt, &actual_error);
					g_object_unref (stmt);
				}

				if (actual_error) {
					g_propagate_error (error, actual_error);
					return;
				}

				continue;
			}

			if (table->insert) {
				sql = g_string_new ("INSERT INTO \"");
				values_sql = g_string_new ("VALUES (?");
			} else {
				sql = g_string_new ("UPDATE \"");
				values_sql = NULL;
			}

			g_string_append (sql, table_name);

			if (table->insert) {
				g_string_append (sql, "\" (ID");

				if (strcmp (table_name, "rdfs:Resource") == 0) {
					g_string_append (sql, ", \"tracker:added\", \"tracker:modified\"");
					g_string_append (values_sql, ", ?, ?");
				} else {
				}
			} else {
				g_string_append (sql, "\" SET ");
			}

			for (i = 0; i < table->properties->len; i++) {
				property = &g_array_index (table->properties, TrackerDataUpdateBufferProperty, i);
				if (table->insert) {
					g_string_append_printf (sql, ", \"%s\"", property->name);
					g_string_append (values_sql, ", ?");

					if (property->date_time) {
						g_string_append_printf (sql, ", \"%s:localDate\"", property->name);
						g_string_append_printf (sql, ", \"%s:localTime\"", property->name);
						g_string_append (values_sql, ", ?, ?");
					}

					g_string_append_printf (sql, ", \"%s:graph\"", property->name);
					g_string_append (values_sql, ", ?");
				} else {
					if (i > 0) {
						g_string_append (sql, ", ");
					}
					g_string_append_printf (sql, "\"%s\" = ?", property->name);

					if (property->date_time) {
						g_string_append_printf (sql, ", \"%s:localDate\" = ?", property->name);
						g_string_append_printf (sql, ", \"%s:localTime\" = ?", property->name);
					}

					g_string_append_printf (sql, ", \"%s:graph\" = ?", property->name);
				}
			}

			if (table->insert) {
				g_string_append (sql, ")");
				g_string_append (values_sql, ")");

				stmt = tracker_db_interface_create_statement (iface, TRACKER_DB_STATEMENT_CACHE_TYPE_UPDATE, &actual_error,
				                                              "%s %s", sql->str, values_sql->str);
				g_string_free (sql, TRUE);
				g_string_free (values_sql, TRUE);
			} else {
				g_string_append (sql, " WHERE ID = ?");

				stmt = tracker_db_interface_create_statement (iface, TRACKER_DB_STATEMENT_CACHE_TYPE_UPDATE, &actual_error,
				                                              "%s", sql->str);
				g_string_free (sql, TRUE);
			}

			if (actual_error) {
				g_propagate_error (error, actual_error);
				return;
			}

			if (table->insert) {
				tracker_db_statement_bind_int (stmt, 0, data->resource_buffer->id);

				if (strcmp (table_name, "rdfs:Resource") == 0) {
					g_warn_if_fail	(data->resource_time != 0);
					tracker_db_statement_bind_int (stmt, 1, (gint64) data->resource_time);
					tracker_db_statement_bind_int (stmt, 2, get_transaction_modseq (data));
					param = 3;
				} else {
					param = 1;
				}
			} else {
				param = 0;
			}

			for (i = 0; i < table->properties->len; i++) {
				property = &g_array_index (table->properties, TrackerDataUpdateBufferProperty, i);
				if (table->delete_value) {
					/* just set value to NULL for single value properties */
					tracker_db_statement_bind_null (stmt, param++);
					if (property->date_time) {
						/* also set localDate and localTime to NULL */
						tracker_db_statement_bind_null (stmt, param++);
						tracker_db_statement_bind_null (stmt, param++);
					}
				} else {
					statement_bind_gvalue (stmt, &param, &property->value);
				}
				if (property->graph != 0) {
					tracker_db_statement_bind_int (stmt, param++, property->graph);
				} else {
					tracker_db_statement_bind_null (stmt, param++);
				}
			}

			if (!table->insert) {
				tracker_db_statement_bind_int (stmt, param++, data->resource_buffer->id);
			}

			tracker_db_statement_execute (stmt, &actual_error);
			g_object_unref (stmt);

			if (actual_error) {
				g_propagate_error (error, actual_error);
				return;
			}
		}
	}

#if HAVE_TRACKER_FTS
	if (data->resource_buffer->fts_updated) {
		TrackerProperty *prop;
		GArray *values;
		GPtrArray *properties, *text;

		properties = text = NULL;
		g_hash_table_iter_init (&iter, data->resource_buffer->predicates);
		while (g_hash_table_iter_next (&iter, (gpointer*) &prop, (gpointer*) &values)) {
			if (tracker_property_get_fulltext_indexed (prop)) {
				GString *fts;

				fts = g_string_new ("");
				for (i = 0; i < values->len; i++) {
					GValue *v = &g_array_index (values, GValue, i);
					g_string_append (fts, g_value_get_string (v));
					g_string_append_c (fts, ' ');
				}

				if (!properties && !text) {
					properties = g_ptr_array_new ();
					text = g_ptr_array_new_with_free_func ((GDestroyNotify) g_free);
				}

				g_ptr_array_add (properties, (gpointer) tracker_property_get_name (prop));
				g_ptr_array_add (text, g_string_free (fts, FALSE));
			}
		}

		if (properties && text) {
			g_ptr_array_add (properties, NULL);
			g_ptr_array_add (text, NULL);

			tracker_db_interface_sqlite_fts_update_text (iface,
			                                             data->resource_buffer->id,
			                                             (const gchar **) properties->pdata,
			                                             (const gchar **) text->pdata);
			data->update_buffer.fts_ever_updated = TRUE;
			g_ptr_array_free (properties, TRUE);
			g_ptr_array_free (text, TRUE);
		}
	}
#endif
}

static void resource_buffer_free (TrackerDataUpdateBufferResource *resource)
{
	g_hash_table_unref (resource->predicates);
	g_hash_table_unref (resource->tables);
	resource->subject = NULL;

	g_ptr_array_free (resource->types, TRUE);
	resource->types = NULL;

	g_slice_free (TrackerDataUpdateBufferResource, resource);
}

void
tracker_data_update_buffer_flush (TrackerData  *data,
                                  GError      **error)
{
	GHashTableIter iter;
	GError *actual_error = NULL;

	if (data->in_journal_replay) {
		g_hash_table_iter_init (&iter, data->update_buffer.resources_by_id);
		while (g_hash_table_iter_next (&iter, NULL, (gpointer*) &data->resource_buffer)) {
			tracker_data_resource_buffer_flush (data, &actual_error);
			if (actual_error) {
				g_propagate_error (error, actual_error);
				break;
			}
		}

		g_hash_table_remove_all (data->update_buffer.resources_by_id);
	} else {
		g_hash_table_iter_init (&iter, data->update_buffer.resources);
		while (g_hash_table_iter_next (&iter, NULL, (gpointer*) &data->resource_buffer)) {
			tracker_data_resource_buffer_flush (data, &actual_error);
			if (actual_error) {
				g_propagate_error (error, actual_error);
				break;
			}
		}

		g_hash_table_remove_all (data->update_buffer.resources);
	}
	data->resource_buffer = NULL;
}

void
tracker_data_update_buffer_might_flush (TrackerData  *data,
                                        GError      **error)
{
	/* avoid high memory usage by update buffer */
	if (g_hash_table_size (data->update_buffer.resources) +
	    g_hash_table_size (data->update_buffer.resources_by_id) >= 1000) {
		tracker_data_update_buffer_flush (data, error);
	}
}

static void
tracker_data_update_buffer_clear (TrackerData *data)
{
	g_hash_table_remove_all (data->update_buffer.resources);
	g_hash_table_remove_all (data->update_buffer.resources_by_id);
	g_hash_table_remove_all (data->update_buffer.resource_cache);
	data->resource_buffer = NULL;

#if HAVE_TRACKER_FTS
	data->update_buffer.fts_ever_updated = FALSE;
#endif

	if (data->update_buffer.class_counts) {
		/* revert class count changes */

		GHashTableIter iter;
		TrackerClass *class;
		gpointer count_ptr;

		g_hash_table_iter_init (&iter, data->update_buffer.class_counts);
		while (g_hash_table_iter_next (&iter, (gpointer*) &class, &count_ptr)) {
			gint count;

			count = GPOINTER_TO_INT (count_ptr);
			tracker_class_set_count (class, tracker_class_get_count (class) - count);
		}

		g_hash_table_remove_all (data->update_buffer.class_counts);
	}
}

static void
tracker_data_blank_buffer_flush (TrackerData  *data,
                                 GError      **error)
{
	/* end of blank node */
	gint i;
	gint id;
	gchar *subject;
	gchar *blank_uri;
	const gchar *sha1;
	GChecksum *checksum;
	GError *actual_error = NULL;
	TrackerDBInterface *iface;

	subject = data->blank_buffer.subject;
	data->blank_buffer.subject = NULL;

	/* we share anonymous blank nodes with identical properties
	   to avoid blowing up the database with duplicates */

	checksum = g_checksum_new (G_CHECKSUM_SHA1);

	/* generate hash uri from data to find resource
	   assumes no collisions due to generally little contents of anonymous nodes */
	for (i = 0; i < data->blank_buffer.predicates->len; i++) {
		if (g_array_index (data->blank_buffer.graphs, guchar *, i) != NULL) {
			g_checksum_update (checksum, g_array_index (data->blank_buffer.graphs, guchar *, i), -1);
		}

		g_checksum_update (checksum, g_array_index (data->blank_buffer.predicates, guchar *, i), -1);
		g_checksum_update (checksum, g_array_index (data->blank_buffer.objects, guchar *, i), -1);
	}

	sha1 = g_checksum_get_string (checksum);

	/* generate name based uuid */
	blank_uri = g_strdup_printf ("urn:uuid:%.8s-%.4s-%.4s-%.4s-%.12s",
	                             sha1, sha1 + 8, sha1 + 12, sha1 + 16, sha1 + 20);

	iface = tracker_data_manager_get_writable_db_interface (data->manager);
	id = tracker_data_query_resource_id (data->manager, iface, blank_uri);

	if (id == 0) {
		/* uri not found
		   replay piled up statements to create resource */
		for (i = 0; i < data->blank_buffer.predicates->len; i++) {
			tracker_data_insert_statement (data,
			                               g_array_index (data->blank_buffer.graphs, gchar *, i),
			                               blank_uri,
			                               g_array_index (data->blank_buffer.predicates, gchar *, i),
			                               g_array_index (data->blank_buffer.objects, gchar *, i),
			                               &actual_error);
			if (actual_error) {
				break;
			}
		}
	}

	/* free piled up statements */
	for (i = 0; i < data->blank_buffer.predicates->len; i++) {
		g_free (g_array_index (data->blank_buffer.graphs, gchar *, i));
		g_free (g_array_index (data->blank_buffer.predicates, gchar *, i));
		g_free (g_array_index (data->blank_buffer.objects, gchar *, i));
	}
	g_array_remove_range (data->blank_buffer.graphs, 0, data->blank_buffer.graphs->len);
	g_array_remove_range (data->blank_buffer.predicates, 0, data->blank_buffer.predicates->len);
	g_array_remove_range (data->blank_buffer.objects, 0, data->blank_buffer.objects->len);

	g_hash_table_insert (data->blank_buffer.table, subject, blank_uri);
	g_checksum_free (checksum);

	if (actual_error) {
		g_propagate_error (error, actual_error);
	}
}

static void
cache_create_service_decomposed (TrackerData  *data,
                                 TrackerClass *cl,
                                 const gchar  *graph,
                                 gint          graph_id)
{
	TrackerClass       **super_classes;
	TrackerProperty    **domain_indexes;
	GValue              gvalue = { 0 };
	gint                i, final_graph_id, class_id;
	TrackerOntologies  *ontologies;

	/* also create instance of all super classes */
	super_classes = tracker_class_get_super_classes (cl);
	while (*super_classes) {
		cache_create_service_decomposed (data, *super_classes, graph, graph_id);
		super_classes++;
	}

	for (i = 0; i < data->resource_buffer->types->len; i++) {
		if (g_ptr_array_index (data->resource_buffer->types, i) == cl) {
			/* ignore duplicate statement */
			return;
		}
	}

	g_ptr_array_add (data->resource_buffer->types, cl);

	g_value_init (&gvalue, G_TYPE_INT64);

	cache_insert_row (data, cl);

	final_graph_id = (graph != NULL ? ensure_graph_id (data, graph, NULL) : graph_id);

	/* This is the original, no idea why tracker_class_get_id wasn't used here:
	 * class_id = ensure_resource_id (tracker_class_get_uri (cl), NULL); */

	class_id = tracker_class_get_id (cl);
	ontologies = tracker_data_manager_get_ontologies (data->manager);

	g_value_set_int64 (&gvalue, class_id);
	cache_insert_value (data, "rdfs:Resource_rdf:type", "rdf:type",
	                    FALSE, &gvalue, final_graph_id,
	                    TRUE, FALSE, FALSE);

	add_class_count (data, cl, 1);

	if (!data->in_journal_replay && data->insert_callbacks) {
		guint n;

		for (n = 0; n < data->insert_callbacks->len; n++) {
			TrackerStatementDelegate *delegate;

			delegate = g_ptr_array_index (data->insert_callbacks, n);
			delegate->callback (final_graph_id, graph, data->resource_buffer->id, data->resource_buffer->subject,
			                    tracker_property_get_id (tracker_ontologies_get_rdf_type (ontologies)),
			                    class_id,
			                    tracker_class_get_uri (cl),
			                    data->resource_buffer->types,
			                    delegate->user_data);
		}
	}

	/* When a new class created, make sure we propagate to the domain indexes
	 * the property values already set, if any. */
	domain_indexes = tracker_class_get_domain_indexes (cl);
	if (!domain_indexes) {
		/* Nothing else to do, return */
		return;
	}

	while (*domain_indexes) {
		GError *error = NULL;
		GArray *old_values;

		/* read existing property values */
		old_values = get_old_property_values (data, *domain_indexes, &error);
		if (error) {
			g_critical ("Couldn't get old values for property '%s': '%s'",
			            tracker_property_get_name (*domain_indexes),
			            error->message);
			g_clear_error (&error);
			domain_indexes++;
			continue;
		}

		if (old_values &&
		    old_values->len > 0) {
			GValue *v;
			GValue gvalue_copy = { 0 };

			/* Don't expect several values for property which is a domain index */
			g_assert_cmpint (old_values->len, ==, 1);

			g_debug ("Propagating '%s' property value from '%s' to domain index in '%s'",
			         tracker_property_get_name (*domain_indexes),
			         tracker_property_get_table_name (*domain_indexes),
			         tracker_class_get_name (cl));

			v = &g_array_index (old_values, GValue, 0);
			g_value_init (&gvalue_copy, G_VALUE_TYPE (v));
			g_value_copy (v, &gvalue_copy);

			cache_insert_value (data,
			                    tracker_class_get_name (cl),
			                    tracker_property_get_name (*domain_indexes),
			                    tracker_property_get_transient (*domain_indexes),
			                    &gvalue_copy,
			                    graph != NULL ? ensure_graph_id (data, graph, NULL) : graph_id,
			                    tracker_property_get_multiple_values (*domain_indexes),
			                    tracker_property_get_fulltext_indexed (*domain_indexes),
			                    tracker_property_get_data_type (*domain_indexes) == TRACKER_PROPERTY_TYPE_DATETIME);
		}

		domain_indexes++;
	}
}

static gboolean
value_equal (GValue *value1,
             GValue *value2)
{
	GType type = G_VALUE_TYPE (value1);

	if (type != G_VALUE_TYPE (value2)) {
		return FALSE;
	}

	switch (type) {
	case G_TYPE_STRING:
		return (strcmp (g_value_get_string (value1), g_value_get_string (value2)) == 0);
	case G_TYPE_INT64:
		return g_value_get_int64 (value1) == g_value_get_int64 (value2);
	case G_TYPE_DOUBLE:
		/* does RDF define equality for floating point values? */
		return g_value_get_double (value1) == g_value_get_double (value2);
	default:
		if (type == TRACKER_TYPE_DATE_TIME) {
			/* ignore UTC offset for comparison, irrelevant for comparison according to xsd:dateTime spec
			 * http://www.w3.org/TR/xmlschema-2/#dateTime
			 * also ignore sub-millisecond as this is a floating point comparison
			 */
			return fabs (tracker_date_time_get_time (value1) - tracker_date_time_get_time (value2)) < 0.001;
		}
		g_assert_not_reached ();
	}
}

static gboolean
value_set_add_value (GArray *value_set,
                     GValue *value)
{
	GValue gvalue_copy = { 0 };
	gint i;

	g_return_val_if_fail (G_VALUE_TYPE (value), FALSE);

	for (i = 0; i < value_set->len; i++) {
		GValue *v;

		v = &g_array_index (value_set, GValue, i);
		if (value_equal (v, value)) {
			/* no change, value already in set */
			return FALSE;
		}
	}

	g_value_init (&gvalue_copy, G_VALUE_TYPE (value));
	g_value_copy (value, &gvalue_copy);
	g_array_append_val (value_set, gvalue_copy);

	return TRUE;
}

static gboolean
value_set_remove_value (GArray *value_set,
                        GValue *value)
{
	gint i;

	g_return_val_if_fail (G_VALUE_TYPE (value), FALSE);

	for (i = 0; i < value_set->len; i++) {
		GValue *v = &g_array_index (value_set, GValue, i);

		if (value_equal (v, value)) {
			/* value found, remove from set */
			g_array_remove_index (value_set, i);

			return TRUE;
		}
	}

	/* no change, value not found */
	return FALSE;
}

static gboolean
check_property_domain (TrackerData     *data,
                       TrackerProperty *property)
{
	gint type_index;

	for (type_index = 0; type_index < data->resource_buffer->types->len; type_index++) {
		if (tracker_property_get_domain (property) == g_ptr_array_index (data->resource_buffer->types, type_index)) {
			return TRUE;
		}
	}
	return FALSE;
}

static GArray *
get_property_values (TrackerData     *data,
                     TrackerProperty *property)
{
	gboolean            multiple_values;
	GArray *old_values;

	multiple_values = tracker_property_get_multiple_values (property);

	old_values = g_array_sized_new (FALSE, TRUE, sizeof (GValue), multiple_values ? 4 : 1);
	g_array_set_clear_func (old_values, (GDestroyNotify) g_value_unset);
	g_hash_table_insert (data->resource_buffer->predicates, g_object_ref (property), old_values);

	if (!data->resource_buffer->create) {
		TrackerDBInterface *iface;
		TrackerDBStatement *stmt;
		TrackerDBCursor    *cursor = NULL;
		const gchar        *table_name;
		const gchar        *field_name;
		GError             *error = NULL;

		table_name = tracker_property_get_table_name (property);
		field_name = tracker_property_get_name (property);

		iface = tracker_data_manager_get_writable_db_interface (data->manager);

		stmt = tracker_db_interface_create_statement (iface, TRACKER_DB_STATEMENT_CACHE_TYPE_SELECT, &error,
		                                              "SELECT \"%s\" FROM \"%s\" WHERE ID = ?",
		                                              field_name, table_name);

		if (stmt) {
			tracker_db_statement_bind_int (stmt, 0, data->resource_buffer->id);
			cursor = tracker_db_statement_start_cursor (stmt, &error);
			g_object_unref (stmt);
		}

		if (error) {
			g_warning ("Could not get property values: %s\n", error->message);
			g_error_free (error);
		}

		if (cursor) {
			while (tracker_db_cursor_iter_next (cursor, NULL, &error)) {
				GValue gvalue = { 0 };

				tracker_db_cursor_get_value (cursor, 0, &gvalue);

				if (G_VALUE_TYPE (&gvalue)) {
					if (tracker_property_get_data_type (property) == TRACKER_PROPERTY_TYPE_DATETIME) {
						gdouble time;

						if (G_VALUE_TYPE (&gvalue) == G_TYPE_INT64) {
							time = g_value_get_int64 (&gvalue);
						} else {
							time = g_value_get_double (&gvalue);
						}
						g_value_unset (&gvalue);
						g_value_init (&gvalue, TRACKER_TYPE_DATE_TIME);
						/* UTC offset is irrelevant for comparison */
						tracker_date_time_set (&gvalue, time, 0);
					}

					g_array_append_val (old_values, gvalue);
				}
			}
			g_object_unref (cursor);
		}
	}

	return old_values;
}

static GArray *
get_old_property_values (TrackerData      *data,
                         TrackerProperty  *property,
                         GError          **error)
{
	GArray *old_values;

	/* read existing property values */
	old_values = g_hash_table_lookup (data->resource_buffer->predicates, property);
	if (old_values == NULL) {
		if (!check_property_domain (data, property)) {
			g_set_error (error, TRACKER_SPARQL_ERROR, TRACKER_SPARQL_ERROR_CONSTRAINT,
			             "Subject `%s' is not in domain `%s' of property `%s'",
			             data->resource_buffer->subject,
			             tracker_class_get_name (tracker_property_get_domain (property)),
			             tracker_property_get_name (property));
			return NULL;
		}

#if HAVE_TRACKER_FTS
		if (tracker_property_get_fulltext_indexed (property)) {
			TrackerDBInterface *iface;

			iface = tracker_data_manager_get_writable_db_interface (data->manager);

			if (!data->resource_buffer->fts_updated && !data->resource_buffer->create) {
				TrackerOntologies *ontologies;
				guint i, n_props;
				TrackerProperty   **properties, *prop;

				/* first fulltext indexed property to be modified
				 * retrieve values of all fulltext indexed properties
				 */
				ontologies = tracker_data_manager_get_ontologies (data->manager);
				properties = tracker_ontologies_get_properties (ontologies, &n_props);

				for (i = 0; i < n_props; i++) {
					prop = properties[i];

					if (tracker_property_get_fulltext_indexed (prop)
					    && check_property_domain (data, prop)) {
						const gchar *property_name;
						GString *str;
						gint i;

						old_values = get_property_values (data, prop);
						property_name = tracker_property_get_name (prop);
						str = g_string_new (NULL);

						/* delete old fts entries */
						for (i = 0; i < old_values->len; i++) {
							GValue *value = &g_array_index (old_values, GValue, i);
							if (i != 0)
								g_string_append_c (str, ',');
							g_string_append (str, g_value_get_string (value));
						}

						tracker_db_interface_sqlite_fts_delete_text (iface,
						                                             data->resource_buffer->id,
						                                             property_name,
						                                             str->str);
						g_string_free (str, TRUE);
					}
				}

				data->update_buffer.fts_ever_updated = TRUE;

				old_values = g_hash_table_lookup (data->resource_buffer->predicates, property);
			} else {
				old_values = get_property_values (data, property);
			}

			data->resource_buffer->fts_updated = TRUE;
		} else {
			old_values = get_property_values (data, property);
		}
#else
		old_values = get_property_values (data, property);
#endif
	}

	return old_values;
}

static void
string_to_gvalue (const gchar         *value,
                  TrackerPropertyType  type,
                  GValue              *gvalue,
                  TrackerData         *data,
                  GError             **error)
{
	gint object_id;
	gchar *datetime;

	switch (type) {
	case TRACKER_PROPERTY_TYPE_STRING:
		g_value_init (gvalue, G_TYPE_STRING);
		g_value_set_string (gvalue, value);
		break;
	case TRACKER_PROPERTY_TYPE_INTEGER:
		g_value_init (gvalue, G_TYPE_INT64);
		g_value_set_int64 (gvalue, atoll (value));
		break;
	case TRACKER_PROPERTY_TYPE_BOOLEAN:
		/* use G_TYPE_INT64 to be compatible with value stored in DB
		   (important for value_equal function) */
		g_value_init (gvalue, G_TYPE_INT64);
		g_value_set_int64 (gvalue, strcmp (value, "true") == 0);
		break;
	case TRACKER_PROPERTY_TYPE_DOUBLE:
		g_value_init (gvalue, G_TYPE_DOUBLE);
		g_value_set_double (gvalue, atof (value));
		break;
	case TRACKER_PROPERTY_TYPE_DATE:
		g_value_init (gvalue, G_TYPE_INT64);
		datetime = g_strdup_printf ("%sT00:00:00Z", value);
		g_value_set_int64 (gvalue, tracker_string_to_date (datetime, NULL, error));
		g_free (datetime);
		break;
	case TRACKER_PROPERTY_TYPE_DATETIME:
		g_value_init (gvalue, TRACKER_TYPE_DATE_TIME);
		tracker_date_time_set_from_string (gvalue, value, error);
		break;
	case TRACKER_PROPERTY_TYPE_RESOURCE:
		object_id = ensure_resource_id (data, value, NULL);
		g_value_init (gvalue, G_TYPE_INT64);
		g_value_set_int64 (gvalue, object_id);
		break;
	default:
		g_warn_if_reached ();
		break;
	}
}

static gchar*
gvalue_to_string (TrackerPropertyType  type,
                  GValue              *gvalue)
{
	gchar *retval = NULL;
	gint64 datet;

	switch (type) {
	case TRACKER_PROPERTY_TYPE_STRING:
		retval = g_value_dup_string (gvalue);
		break;
	case TRACKER_PROPERTY_TYPE_INTEGER:
		retval = g_strdup_printf ("%" G_GINT64_FORMAT, g_value_get_int64 (gvalue));
		break;
	case TRACKER_PROPERTY_TYPE_BOOLEAN:
		retval = g_value_get_int64 (gvalue) == 0 ? g_strdup ("false") : g_strdup ("true");
		break;
	case TRACKER_PROPERTY_TYPE_DOUBLE:
		retval = g_strdup_printf ("%f", g_value_get_double (gvalue));
		break;
	case TRACKER_PROPERTY_TYPE_DATE:
		datet = g_value_get_int64 (gvalue);
		retval = tracker_date_to_string (datet);
		/* it's a date-only, cut off the time */
		retval[10] = '\0';
		break;
	case TRACKER_PROPERTY_TYPE_DATETIME:
		datet = tracker_date_time_get_time (gvalue);
		retval = tracker_date_to_string (datet);
		break;
	case TRACKER_PROPERTY_TYPE_RESOURCE:
	default:
		g_warn_if_reached ();
		break;
	}

	return retval;
}

static gboolean
resource_in_domain_index_class (TrackerData  *data,
                                TrackerClass *domain_index_class)
{
	guint i;
	for (i = 0; i < data->resource_buffer->types->len; i++) {
		if (g_ptr_array_index (data->resource_buffer->types, i) == domain_index_class) {
			return TRUE;
		}
	}
	return FALSE;
}

static void
process_domain_indexes (TrackerData     *data,
                        TrackerProperty *property,
                        GValue          *gvalue,
                        const gchar     *field_name,
                        const gchar     *graph,
                        gint             graph_id)
{
	TrackerClass **domain_index_classes;

	domain_index_classes = tracker_property_get_domain_indexes (property);
	while (*domain_index_classes) {
		if (resource_in_domain_index_class (data, *domain_index_classes)) {
			GValue gvalue_copy = { 0 };

			g_value_init (&gvalue_copy, G_VALUE_TYPE (gvalue));
			g_value_copy (gvalue, &gvalue_copy);

			cache_insert_value (data,
			                    tracker_class_get_name (*domain_index_classes),
			                    field_name,
			                    tracker_property_get_transient (property),
			                    &gvalue_copy,
			                    graph != NULL ? ensure_graph_id (data, graph, NULL) : graph_id,
			                    FALSE,
			                    tracker_property_get_fulltext_indexed (property),
			                    tracker_property_get_data_type (property) == TRACKER_PROPERTY_TYPE_DATETIME);
		}
		domain_index_classes++;
	}
}

static gboolean
cache_insert_metadata_decomposed (TrackerData      *data,
                                  TrackerProperty  *property,
                                  const gchar      *value,
                                  gint              value_id,
                                  const gchar      *graph,
                                  gint              graph_id,
                                  GError          **error)
{
	gboolean            multiple_values;
	const gchar        *table_name;
	const gchar        *field_name;
	TrackerProperty   **super_properties;
	GValue              gvalue = { 0 };
	GArray             *old_values;
	GError             *new_error = NULL;
	gboolean            change = FALSE;

	/* read existing property values */
	old_values = get_old_property_values (data, property, &new_error);
	if (new_error) {
		g_propagate_error (error, new_error);
		return FALSE;
	}

	/* also insert super property values */
	super_properties = tracker_property_get_super_properties (property);
	multiple_values = tracker_property_get_multiple_values (property);

	while (*super_properties) {
		gboolean super_is_multi;

		super_is_multi = tracker_property_get_multiple_values (*super_properties);

		if (super_is_multi || old_values->len == 0) {
			change |= cache_insert_metadata_decomposed (data, *super_properties, value, value_id,
			                                            graph, graph_id, &new_error);
			if (new_error) {
				g_propagate_error (error, new_error);
				return FALSE;
			}
		}
		super_properties++;
	}

	table_name = tracker_property_get_table_name (property);
	field_name = tracker_property_get_name (property);

	if (value) {
		string_to_gvalue (value, tracker_property_get_data_type (property), &gvalue, data, &new_error);
		if (new_error) {
			g_propagate_error (error, new_error);
			return FALSE;
		}
	} else {
		g_value_init (&gvalue, G_TYPE_INT64);
		g_value_set_int64 (&gvalue, value_id);
	}

	if (!value_set_add_value (old_values, &gvalue)) {
		/* value already inserted */
		g_value_unset (&gvalue);
	} else if (!multiple_values && old_values->len > 1) {
		/* trying to add second value to single valued property */
		GValue old_value = { 0 };
		GValue new_value = { 0 };
		GValue *v;
		gchar *old_value_str = NULL;
		gchar *new_value_str = NULL;

		g_value_init (&old_value, G_TYPE_STRING);
		g_value_init (&new_value, G_TYPE_STRING);

		/* Get both old and new values as strings letting glib do
		 * whatever transformation needed */
		v = &g_array_index (old_values, GValue, 0);
		if (g_value_transform (v, &old_value)) {
			old_value_str = tracker_utf8_truncate (g_value_get_string (&old_value), 255);
		}

		v = &g_array_index (old_values, GValue, 1);
		if (g_value_transform (v, &new_value)) {
			new_value_str = tracker_utf8_truncate (g_value_get_string (&new_value), 255);
		}

		g_set_error (error, TRACKER_SPARQL_ERROR, TRACKER_SPARQL_ERROR_CONSTRAINT,
		             "Unable to insert multiple values for subject `%s' and single valued property `%s' "
		             "(old_value: '%s', new value: '%s')",
		             data->resource_buffer->subject,
		             field_name,
		             old_value_str ? old_value_str : "<untransformable>",
		             new_value_str ? new_value_str : "<untransformable>");

		g_free (old_value_str);
		g_free (new_value_str);
		g_value_unset (&old_value);
		g_value_unset (&new_value);
		g_value_unset (&gvalue);

	} else {
		cache_insert_value (data, table_name, field_name,
		                    tracker_property_get_transient (property),
		                    &gvalue,
		                    graph != NULL ? ensure_graph_id (data, graph, NULL) : graph_id,
		                    multiple_values,
		                    tracker_property_get_fulltext_indexed (property),
		                    tracker_property_get_data_type (property) == TRACKER_PROPERTY_TYPE_DATETIME);

		if (!multiple_values) {
			process_domain_indexes (data, property, &gvalue, field_name, graph, graph_id);
		}

		change = TRUE;
	}

	return change;
}

static gboolean
delete_first_object (TrackerData      *data,
                     TrackerProperty  *field,
                     GArray           *old_values,
                     const gchar      *graph,
                     GError          **error)
{
	gint pred_id = 0, graph_id = 0;
	gint object_id = 0;
	gboolean change = FALSE;

	if (old_values->len == 0) {
		return change;
	}

	pred_id = tracker_property_get_id (field);
	graph_id = (graph != NULL ? query_resource_id (data, graph) : 0);

	if (tracker_property_get_data_type (field) == TRACKER_PROPERTY_TYPE_RESOURCE) {
		GError *new_error = NULL;
		GValue *v;

		v = &g_array_index (old_values, GValue, 0);
		object_id = (gint) g_value_get_int64 (v);

		/* This influences old_values, which is a reference, not a copy */
		change = delete_metadata_decomposed (data, field, NULL, object_id, &new_error);

		if (new_error) {
			g_propagate_error (error, new_error);
			return change;
		}

#ifndef DISABLE_JOURNAL
		if (!data->in_journal_replay && change && !tracker_property_get_transient (field)) {
			tracker_db_journal_append_delete_statement_id (data->journal_writer,
			                                               graph_id,
			                                               data->resource_buffer->id,
			                                               pred_id,
			                                               object_id);
		}
#endif /* DISABLE_JOURNAL */
	} else {
		GValue *v;
		GError *new_error = NULL;
		gchar *object_str = NULL;

		object_id = 0;
		v = &g_array_index (old_values, GValue, 0);
		object_str = gvalue_to_string (tracker_property_get_data_type (field), v);

		/* This influences old_values, which is a reference, not a copy */
		change = delete_metadata_decomposed (data, field, object_str, 0, &new_error);

		if (new_error) {
			g_propagate_error (error, new_error);
			return change;
		}

#ifndef DISABLE_JOURNAL
		if (!data->in_journal_replay && change && !tracker_property_get_transient (field)) {
			if (!tracker_property_get_force_journal (field) &&
				g_strcmp0 (graph, TRACKER_OWN_GRAPH_URN) == 0) {
				/* do not journal this statement extracted from filesystem */
				TrackerProperty *damaged;
				TrackerOntologies *ontologies;

				ontologies = tracker_data_manager_get_ontologies (data->manager);
				damaged = tracker_ontologies_get_property_by_uri (ontologies, TRACKER_PREFIX_TRACKER "damaged");

				tracker_db_journal_append_insert_statement (data->journal_writer,
				                                            graph_id,
				                                            data->resource_buffer->id,
				                                            tracker_property_get_id (damaged),
				                                            "true");
			} else {
				tracker_db_journal_append_delete_statement (data->journal_writer,
				                                            graph_id,
				                                            data->resource_buffer->id,
				                                            pred_id,
				                                            object_str);
			}
		}

#endif /* DISABLE_JOURNAL */

		if (data->delete_callbacks && change) {
			guint n;
			for (n = 0; n < data->delete_callbacks->len; n++) {
				TrackerStatementDelegate *delegate;

				delegate = g_ptr_array_index (data->delete_callbacks, n);
				delegate->callback (graph_id, graph,
				                    data->resource_buffer->id,
				                    data->resource_buffer->subject,
				                    pred_id, object_id,
				                    object_str,
				                    data->resource_buffer->types,
				                    delegate->user_data);
			}
		}

		g_free (object_str);
	}

	return change;
}

static gboolean
cache_update_metadata_decomposed (TrackerData      *data,
                                  TrackerProperty  *property,
                                  const gchar      *value,
                                  gint              value_id,
                                  const gchar      *graph,
                                  gint              graph_id,
                                  GError          **error)
{
	gboolean            multiple_values;
	const gchar        *table_name;
	const gchar        *field_name;
	TrackerProperty   **super_properties;
	GValue              gvalue = { 0 };
	GError             *new_error = NULL;
	gboolean            change = FALSE;

	multiple_values = tracker_property_get_multiple_values (property);

	/* also insert super property values */
	super_properties = tracker_property_get_super_properties (property);
	while (*super_properties) {
		gboolean super_is_multi;
		super_is_multi = tracker_property_get_multiple_values (*super_properties);

		if (!multiple_values && super_is_multi) {
			gint subject_id;
			gchar *subject;

			GArray *old_values;

			/* read existing property values */
			old_values = get_old_property_values (data, property, &new_error);
			if (new_error) {
				g_propagate_error (error, new_error);
				return FALSE;
			}

			/* Delete old values from super */
			change |= delete_first_object (data, *super_properties,
			                               old_values,
			                               graph,
			                               &new_error);

			if (new_error) {
				g_propagate_error (error, new_error);
				return FALSE;
			}

			subject_id = data->resource_buffer->id;
			subject = g_strdup (data->resource_buffer->subject);

			/* We need to flush to apply the delete */
			tracker_data_update_buffer_flush (data, &new_error);
			if (new_error) {
				g_propagate_error (error, new_error);
				g_free (subject);
				return FALSE;
			}

			/* After flush we need to switch the resource_buffer */
			resource_buffer_switch (data, graph, subject, subject_id);

			g_free (subject);
		}

		change |= cache_update_metadata_decomposed (data, *super_properties, value, value_id,
		                                            graph, graph_id, &new_error);
		if (new_error) {
			g_propagate_error (error, new_error);
			return FALSE;
		}
		super_properties++;
	}

	table_name = tracker_property_get_table_name (property);
	field_name = tracker_property_get_name (property);

	if (value) {
		string_to_gvalue (value, tracker_property_get_data_type (property), &gvalue, data, &new_error);
		if (new_error) {
			g_propagate_error (error, new_error);
			return FALSE;
		}
	} else {
		g_value_init (&gvalue, G_TYPE_INT64);
		g_value_set_int64 (&gvalue, value_id);
	}

	cache_insert_value (data, table_name, field_name,
	                    tracker_property_get_transient (property),
	                    &gvalue,
	                    graph != NULL ? ensure_graph_id (data, graph, NULL) : graph_id,
	                    multiple_values,
	                    tracker_property_get_fulltext_indexed (property),
	                    tracker_property_get_data_type (property) == TRACKER_PROPERTY_TYPE_DATETIME);

	if (!multiple_values) {
		process_domain_indexes (data, property, &gvalue, field_name, graph, graph_id);
	}

	return TRUE;
}

static gboolean
delete_metadata_decomposed (TrackerData      *data,
                            TrackerProperty  *property,
                            const gchar      *value,
                            gint              value_id,
                            GError          **error)
{
	gboolean            multiple_values;
	const gchar        *table_name;
	const gchar        *field_name;
	TrackerProperty   **super_properties;
	GValue gvalue = { 0 };
	GArray             *old_values;
	GError             *new_error = NULL;
	gboolean            change = FALSE;

	multiple_values = tracker_property_get_multiple_values (property);
	table_name = tracker_property_get_table_name (property);
	field_name = tracker_property_get_name (property);

	/* read existing property values */
	old_values = get_old_property_values (data, property, &new_error);
	if (new_error) {
		/* no need to error out if statement does not exist for any reason */
		g_clear_error (&new_error);
		return FALSE;
	}

	if (value) {
		string_to_gvalue (value, tracker_property_get_data_type (property), &gvalue, data, &new_error);
		if (new_error) {
			g_propagate_error (error, new_error);
			return FALSE;
		}
	} else {
		g_value_init (&gvalue, G_TYPE_INT64);
		g_value_set_int64 (&gvalue, value_id);
	}

	if (!value_set_remove_value (old_values, &gvalue)) {
		/* value not found */
		g_value_unset (&gvalue);
	} else {
		cache_delete_value (data, table_name, field_name,
		                    tracker_property_get_transient (property),
		                    &gvalue, multiple_values,
		                    tracker_property_get_fulltext_indexed (property),
		                    tracker_property_get_data_type (property) == TRACKER_PROPERTY_TYPE_DATETIME);

		if (!multiple_values) {
			TrackerClass **domain_index_classes;

			domain_index_classes = tracker_property_get_domain_indexes (property);

			while (*domain_index_classes) {
				if (resource_in_domain_index_class (data, *domain_index_classes)) {
					GValue gvalue_copy = { 0 };
					g_value_init (&gvalue_copy, G_VALUE_TYPE (&gvalue));
					g_value_copy (&gvalue, &gvalue_copy);
					cache_delete_value (data,
					                    tracker_class_get_name (*domain_index_classes),
					                    field_name,
					                    tracker_property_get_transient (property),
					                    &gvalue_copy, multiple_values,
					                    tracker_property_get_fulltext_indexed (property),
					                    tracker_property_get_data_type (property) == TRACKER_PROPERTY_TYPE_DATETIME);
				}
				domain_index_classes++;
			}
		}

		change = TRUE;
	}

	/* also delete super property values */
	super_properties = tracker_property_get_super_properties (property);
	while (*super_properties) {
		change |= delete_metadata_decomposed (data, *super_properties, value, value_id, error);
		super_properties++;
	}

	return change;
}

static void
db_delete_row (TrackerDBInterface *iface,
               const gchar        *table_name,
               gint                id)
{
	TrackerDBStatement *stmt;
	GError *error = NULL;

	stmt = tracker_db_interface_create_statement (iface, TRACKER_DB_STATEMENT_CACHE_TYPE_UPDATE, &error,
	                                              "DELETE FROM \"%s\" WHERE ID = ?",
	                                              table_name);

	if (stmt) {
		tracker_db_statement_bind_int (stmt, 0, id);
		tracker_db_statement_execute (stmt, &error);
		g_object_unref (stmt);
	}

	if (error) {
		g_warning ("%s", error->message);
		g_error_free (error);
		error = NULL;
	}
}

static void
cache_delete_resource_type_full (TrackerData  *data,
                                 TrackerClass *class,
                                 const gchar  *graph,
                                 gint          graph_id,
                                 gboolean      single_type)
{
	TrackerDBInterface *iface;
	TrackerDBStatement *stmt;
	TrackerDBCursor    *cursor = NULL;
	TrackerProperty   **properties, *prop;
	gboolean            found, direct_delete;
	gint                i;
	guint               p, n_props;
	GError             *error = NULL;
	TrackerOntologies  *ontologies;

	iface = tracker_data_manager_get_writable_db_interface (data->manager);
	ontologies = tracker_data_manager_get_ontologies (data->manager);

	if (!single_type) {
		if (strcmp (tracker_class_get_uri (class), TRACKER_PREFIX_RDFS "Resource") == 0 &&
		    g_hash_table_size (data->resource_buffer->tables) == 0) {
#if HAVE_TRACKER_FTS
			tracker_db_interface_sqlite_fts_delete_id (iface, data->resource_buffer->id);
#endif
			/* skip subclass query when deleting whole resource
			   to improve performance */

			while (data->resource_buffer->types->len > 0) {
				TrackerClass *type;

				type = g_ptr_array_index (data->resource_buffer->types,
				                          data->resource_buffer->types->len - 1);
				cache_delete_resource_type_full (data, type,
				                                 graph,
				                                 graph_id,
				                                 TRUE);
			}

			return;
		}

		found = FALSE;
		for (i = 0; i < data->resource_buffer->types->len; i++) {
			if (g_ptr_array_index (data->resource_buffer->types, i) == class) {
				found = TRUE;
				break;
			}
		}

		if (!found) {
			/* type not found, nothing to do */
			return;
		}

		/* retrieve all subclasses we need to remove from the subject
		 * before we can remove the class specified as object of the statement */
		stmt = tracker_db_interface_create_statement (iface, TRACKER_DB_STATEMENT_CACHE_TYPE_SELECT, &error,
			                                      "SELECT (SELECT Uri FROM Resource WHERE ID = \"rdfs:Class_rdfs:subClassOf\".ID) "
			                                      "FROM \"rdfs:Resource_rdf:type\" INNER JOIN \"rdfs:Class_rdfs:subClassOf\" ON (\"rdf:type\" = \"rdfs:Class_rdfs:subClassOf\".ID) "
			                                      "WHERE \"rdfs:Resource_rdf:type\".ID = ? AND \"rdfs:subClassOf\" = (SELECT ID FROM Resource WHERE Uri = ?)");

		if (stmt) {
			tracker_db_statement_bind_int (stmt, 0, data->resource_buffer->id);
			tracker_db_statement_bind_text (stmt, 1, tracker_class_get_uri (class));
			cursor = tracker_db_statement_start_cursor (stmt, &error);
			g_object_unref (stmt);
		}

		if (cursor) {
			while (tracker_db_cursor_iter_next (cursor, NULL, &error)) {
				const gchar *class_uri;

				class_uri = tracker_db_cursor_get_string (cursor, 0, NULL);
				cache_delete_resource_type_full (data, tracker_ontologies_get_class_by_uri (ontologies, class_uri),
					                         graph, graph_id, FALSE);
			}

			g_object_unref (cursor);
		}

		if (error) {
			g_warning ("Could not delete cache resource (selecting subclasses): %s", error->message);
			g_error_free (error);
			error = NULL;
		}
	}

	/* bypass buffer if possible */
	direct_delete = g_hash_table_size (data->resource_buffer->tables) == 0;

	/* delete all property values */

	properties = tracker_ontologies_get_properties (ontologies, &n_props);

	for (p = 0; p < n_props; p++) {
		gboolean            multiple_values;
		const gchar        *table_name;
		const gchar        *field_name;
		GArray *old_values;
		gint                y;

		prop = properties[p];

		if (tracker_property_get_domain (prop) != class) {
			continue;
		}

		multiple_values = tracker_property_get_multiple_values (prop);
		table_name = tracker_property_get_table_name (prop);
		field_name = tracker_property_get_name (prop);

		if (direct_delete) {
			if (multiple_values) {
				db_delete_row (iface, table_name, data->resource_buffer->id);
			}
			/* single-valued property values are deleted right after the loop by deleting the row in the class table */
			continue;
		}

		old_values = get_old_property_values (data, prop, NULL);

		for (y = old_values->len - 1; y >= 0 ; y--) {
			GValue *old_gvalue;
			GValue  gvalue = { 0 };

			old_gvalue = &g_array_index (old_values, GValue, y);
			g_value_init (&gvalue, G_VALUE_TYPE (old_gvalue));
			g_value_copy (old_gvalue, &gvalue);

			value_set_remove_value (old_values, &gvalue);
			cache_delete_value (data, table_name, field_name,
			                    tracker_property_get_transient (prop),
			                    &gvalue, multiple_values,
			                    tracker_property_get_fulltext_indexed (prop),
			                    tracker_property_get_data_type (prop) == TRACKER_PROPERTY_TYPE_DATETIME);


			if (!multiple_values) {
				TrackerClass **domain_index_classes;

				domain_index_classes = tracker_property_get_domain_indexes (prop);
				while (*domain_index_classes) {
					if (resource_in_domain_index_class (data, *domain_index_classes)) {
						GValue gvalue_copy = { 0 };
						g_value_init (&gvalue_copy, G_VALUE_TYPE (&gvalue));
						g_value_copy (&gvalue, &gvalue_copy);
						cache_delete_value (data,
						                    tracker_class_get_name (*domain_index_classes),
						                    field_name,
						                    tracker_property_get_transient (prop),
						                    &gvalue_copy, multiple_values,
						                    tracker_property_get_fulltext_indexed (prop),
						                    tracker_property_get_data_type (prop) == TRACKER_PROPERTY_TYPE_DATETIME);
					}
					domain_index_classes++;
				}
			}

		}
	}

	if (direct_delete) {
		/* delete row from class table */
		db_delete_row (iface, tracker_class_get_name (class), data->resource_buffer->id);

		if (!single_type) {
			/* delete row from rdfs:Resource_rdf:type table */
			/* this is not necessary when deleting the whole resource
			   as all property values are deleted implicitly */
			stmt = tracker_db_interface_create_statement (iface, TRACKER_DB_STATEMENT_CACHE_TYPE_UPDATE, &error,
						                      "DELETE FROM \"rdfs:Resource_rdf:type\" WHERE ID = ? AND \"rdf:type\" = ?");

			if (stmt) {
				tracker_db_statement_bind_int (stmt, 0, data->resource_buffer->id);
				tracker_db_statement_bind_int (stmt, 1, tracker_class_get_id (class));
				tracker_db_statement_execute (stmt, &error);
				g_object_unref (stmt);
			}

			if (error) {
				g_warning ("Could not delete cache resource: %s", error->message);
				g_error_free (error);
				error = NULL;
			}
		}

		add_class_count (data, class, -1);
	} else {
		cache_delete_row (data, class);
	}

	if (!data->in_journal_replay && data->delete_callbacks) {
		guint n;
		gint final_graph_id;

		final_graph_id = (graph != NULL ? ensure_graph_id (data, graph, NULL) : graph_id);

		for (n = 0; n < data->delete_callbacks->len; n++) {
			TrackerStatementDelegate *delegate;

			delegate = g_ptr_array_index (data->delete_callbacks, n);
			delegate->callback (final_graph_id, graph, data->resource_buffer->id, data->resource_buffer->subject,
			                    tracker_property_get_id (tracker_ontologies_get_rdf_type (ontologies)),
			                    tracker_class_get_id (class),
			                    tracker_class_get_uri (class),
			                    data->resource_buffer->types,
			                    delegate->user_data);
		}
	}

	g_ptr_array_remove (data->resource_buffer->types, class);
}

static void
cache_delete_resource_type (TrackerData  *data,
                            TrackerClass *class,
                            const gchar  *graph,
                            gint          graph_id)
{
	cache_delete_resource_type_full (data, class, graph, graph_id, FALSE);
}

static void
resource_buffer_switch (TrackerData *data,
                        const gchar *graph,
                        const gchar *subject,
                        gint         subject_id)
{
	if (data->in_journal_replay) {
		/* journal replay only provides subject id
		   resource_buffer->subject is only used in error messages and callbacks
		   both should never occur when in journal replay */
		if (data->resource_buffer == NULL || data->resource_buffer->id != subject_id) {
			/* switch subject */
			data->resource_buffer = g_hash_table_lookup (data->update_buffer.resources_by_id, GINT_TO_POINTER (subject_id));
		}
	} else {
		if (data->resource_buffer == NULL || strcmp (data->resource_buffer->subject, subject) != 0) {
			/* switch subject */
			data->resource_buffer = g_hash_table_lookup (data->update_buffer.resources, subject);
		}
	}

	if (data->resource_buffer == NULL) {
		TrackerDataUpdateBufferResource *resource_buffer;
		gchar *subject_dup = NULL;

		/* large INSERTs with thousands of resources could lead to
		   high peak memory usage due to the update buffer
		   flush the buffer if it already contains 1000 resources */
		tracker_data_update_buffer_might_flush (data, NULL);

		/* subject not yet in cache, retrieve or create ID */
		resource_buffer = g_slice_new0 (TrackerDataUpdateBufferResource);
		if (subject != NULL) {
			subject_dup = g_strdup (subject);
			resource_buffer->subject = subject_dup;
		}
		if (subject_id > 0) {
			resource_buffer->id = subject_id;
		} else {
			resource_buffer->id = ensure_resource_id (data, resource_buffer->subject, &resource_buffer->create);
		}
#if HAVE_TRACKER_FTS
		resource_buffer->fts_updated = FALSE;
#endif
		if (resource_buffer->create) {
			resource_buffer->types = g_ptr_array_new ();
		} else {
			resource_buffer->types = tracker_data_query_rdf_type (data->manager, resource_buffer->id);
		}
		resource_buffer->predicates = g_hash_table_new_full (g_direct_hash, g_direct_equal, g_object_unref, (GDestroyNotify) g_array_unref);
		resource_buffer->tables = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, (GDestroyNotify) cache_table_free);

		if (data->in_journal_replay) {
			g_hash_table_insert (data->update_buffer.resources_by_id, GINT_TO_POINTER (subject_id), resource_buffer);
		} else {
			g_hash_table_insert (data->update_buffer.resources, subject_dup, resource_buffer);

			/* Ensure the graph gets an ID */
			if (graph != NULL) {
				ensure_graph_id (data, graph, NULL);
			}
		}

		data->resource_buffer = resource_buffer;
	}
}

void
tracker_data_delete_statement (TrackerData  *data,
                               const gchar  *graph,
                               const gchar  *subject,
                               const gchar  *predicate,
                               const gchar  *object,
                               GError      **error)
{
	TrackerClass       *class;
	gint                subject_id = 0;
	gboolean            change = FALSE;
	TrackerOntologies  *ontologies;
	TrackerDBInterface *iface;

	g_return_if_fail (subject != NULL);
	g_return_if_fail (predicate != NULL);
	g_return_if_fail (object != NULL);
	g_return_if_fail (data->in_transaction);

	subject_id = query_resource_id (data, subject);

	if (subject_id == 0) {
		/* subject not in database */
		return;
	}

	resource_buffer_switch (data, graph, subject, subject_id);
	ontologies = tracker_data_manager_get_ontologies (data->manager);
	iface = tracker_data_manager_get_writable_db_interface (data->manager);

	if (object && g_strcmp0 (predicate, TRACKER_PREFIX_RDF "type") == 0) {
		class = tracker_ontologies_get_class_by_uri (ontologies, object);
		if (class != NULL) {
			data->has_persistent = TRUE;

#ifndef DISABLE_JOURNAL
			if (!data->in_journal_replay) {
				tracker_db_journal_append_delete_statement_id (
				       data->journal_writer,
				       (graph != NULL ? query_resource_id (data, graph) : 0),
				       data->resource_buffer->id,
				       tracker_data_query_resource_id (data->manager, iface, predicate),
				       tracker_class_get_id (class));
			}
#endif /* DISABLE_JOURNAL */

			cache_delete_resource_type (data, class, graph, 0);
		} else {
			g_set_error (error, TRACKER_SPARQL_ERROR, TRACKER_SPARQL_ERROR_UNKNOWN_CLASS,
			             "Class '%s' not found in the ontology", object);
		}
	} else {
		gint pred_id = 0, graph_id = 0, object_id = 0;
		gboolean tried = FALSE;
		TrackerProperty *field;

		field = tracker_ontologies_get_property_by_uri (ontologies, predicate);
		if (field != NULL) {
			if (!tracker_property_get_transient (field)) {
				data->has_persistent = TRUE;
			}

			change = delete_metadata_decomposed (data, field, object, 0, error);
			if (!data->in_journal_replay && change && !tracker_property_get_transient (field)) {
				if (tracker_property_get_data_type (field) == TRACKER_PROPERTY_TYPE_RESOURCE) {

					graph_id = (graph != NULL ? query_resource_id (data, graph) : 0);
					pred_id = tracker_property_get_id (field);
					object_id = query_resource_id (data, object);
					tried = TRUE;

#ifndef DISABLE_JOURNAL
					tracker_db_journal_append_delete_statement_id (data->journal_writer,
					                                               graph_id,
					                                               data->resource_buffer->id,
					                                               pred_id,
					                                               object_id);
#endif /* DISABLE_JOURNAL */
				} else {
					pred_id = tracker_property_get_id (field);
					graph_id = (graph != NULL ? query_resource_id (data, graph) : 0);
					object_id = 0;
					tried = TRUE;

#ifndef DISABLE_JOURNAL
					if (!tracker_property_get_force_journal (field) &&
					    g_strcmp0 (graph, TRACKER_OWN_GRAPH_URN) == 0) {
						/* do not journal this statement extracted from filesystem */
						TrackerProperty *damaged;

						damaged = tracker_ontologies_get_property_by_uri (ontologies, TRACKER_PREFIX_TRACKER "damaged");

						tracker_db_journal_append_insert_statement (data->journal_writer,
						                                            graph_id,
						                                            data->resource_buffer->id,
						                                            tracker_property_get_id (damaged),
						                                            "true");
					} else {
						tracker_db_journal_append_delete_statement (data->journal_writer,
						                                            graph_id,
						                                            data->resource_buffer->id,
						                                            pred_id,
						                                            object);
					}
#endif /* DISABLE_JOURNAL */
				}
			}
		} else {
			/* I wonder why in case of error the delete_callbacks are still executed */
			g_set_error (error, TRACKER_SPARQL_ERROR, TRACKER_SPARQL_ERROR_UNKNOWN_PROPERTY,
			             "Property '%s' not found in the ontology", predicate);
		}

		if (!tried) {
			graph_id = (graph != NULL ? query_resource_id (data, graph) : 0);
			if (field == NULL) {
				pred_id = tracker_data_query_resource_id (data->manager, iface, predicate);
			} else {
				pred_id = tracker_property_get_id (field);
			}
		}

		if (data->delete_callbacks && change) {
			guint n;
			for (n = 0; n < data->delete_callbacks->len; n++) {
				TrackerStatementDelegate *delegate;

				delegate = g_ptr_array_index (data->delete_callbacks, n);
				delegate->callback (graph_id, graph, subject_id, subject,
				                    pred_id, object_id,
				                    object,
				                    data->resource_buffer->types,
				                    delegate->user_data);
			}
		}
	}
}

static void
delete_all_objects (TrackerData  *data,
                    const gchar  *graph,
                    const gchar  *subject,
                    const gchar  *predicate,
                    GError      **error)
{
	gint subject_id = 0;
	gboolean change = FALSE;
	GError *new_error = NULL;
	TrackerProperty *field;
	TrackerOntologies *ontologies;

	g_return_if_fail (subject != NULL);
	g_return_if_fail (predicate != NULL);
	g_return_if_fail (data->in_transaction);

	subject_id = query_resource_id (data, subject);

	if (subject_id == 0) {
		/* subject not in database */
		return;
	}

	resource_buffer_switch (data, graph, subject, subject_id);
	ontologies = tracker_data_manager_get_ontologies (data->manager);

	field = tracker_ontologies_get_property_by_uri (ontologies, predicate);
	if (field != NULL) {
		GArray *old_values;

		if (!tracker_property_get_transient (field)) {
			data->has_persistent = TRUE;
		}

		old_values = get_old_property_values (data, field, &new_error);
		if (new_error) {
			g_propagate_error (error, new_error);
			return;
		}

		while (old_values->len > 0) {
			GError *new_error = NULL;

			change |= delete_first_object (data, field, old_values, graph, &new_error);

			if (new_error) {
				g_propagate_error (error, new_error);
				return;
			}
		}
	} else {
		g_set_error (error, TRACKER_SPARQL_ERROR, TRACKER_SPARQL_ERROR_UNKNOWN_PROPERTY,
		             "Property '%s' not found in the ontology", predicate);
	}
}

static gboolean
tracker_data_insert_statement_common (TrackerData  *data,
                                      const gchar  *graph,
                                      const gchar  *subject,
                                      const gchar  *predicate,
                                      const gchar  *object,
                                      GError      **error)
{
	if (g_str_has_prefix (subject, ":")) {
		/* blank node definition
		   pile up statements until the end of the blank node */
		gchar *value;
		GError *actual_error = NULL;

		if (data->blank_buffer.subject != NULL) {
			/* active subject in buffer */
			if (strcmp (data->blank_buffer.subject, subject) != 0) {
				/* subject changed, need to flush buffer */
				tracker_data_blank_buffer_flush (data, &actual_error);

				if (actual_error) {
					g_propagate_error (error, actual_error);
					return FALSE;
				}
			}
		}

		if (data->blank_buffer.subject == NULL) {
			data->blank_buffer.subject = g_strdup (subject);
			if (data->blank_buffer.graphs == NULL) {
				data->blank_buffer.graphs = g_array_sized_new (FALSE, FALSE, sizeof (char*), 4);
				data->blank_buffer.predicates = g_array_sized_new (FALSE, FALSE, sizeof (char*), 4);
				data->blank_buffer.objects = g_array_sized_new (FALSE, FALSE, sizeof (char*), 4);
			}
		}

		value = g_strdup (graph);
		g_array_append_val (data->blank_buffer.graphs, value);
		value = g_strdup (predicate);
		g_array_append_val (data->blank_buffer.predicates, value);
		value = g_strdup (object);
		g_array_append_val (data->blank_buffer.objects, value);

		return FALSE;
	}

	resource_buffer_switch (data, graph, subject, 0);

	return TRUE;
}

void
tracker_data_insert_statement (TrackerData  *data,
                               const gchar  *graph,
                               const gchar  *subject,
                               const gchar  *predicate,
                               const gchar  *object,
                               GError      **error)
{
	TrackerProperty *property;
	TrackerOntologies *ontologies;

	g_return_if_fail (subject != NULL);
	g_return_if_fail (predicate != NULL);
	g_return_if_fail (object != NULL);
	g_return_if_fail (data->in_transaction);

	ontologies = tracker_data_manager_get_ontologies (data->manager);

	property = tracker_ontologies_get_property_by_uri (ontologies, predicate);
	if (property != NULL) {
		if (tracker_property_get_data_type (property) == TRACKER_PROPERTY_TYPE_RESOURCE) {
			tracker_data_insert_statement_with_uri (data, graph, subject, predicate, object, error);
		} else {
			tracker_data_insert_statement_with_string (data, graph, subject, predicate, object, error);
		}
	} else {
		g_set_error (error, TRACKER_SPARQL_ERROR, TRACKER_SPARQL_ERROR_UNKNOWN_PROPERTY,
		             "Property '%s' not found in the ontology", predicate);
	}
}


static gboolean
handle_blank_node (TrackerData  *data,
                   const gchar  *subject,
                   const gchar  *predicate,
                   const gchar  *object,
                   const gchar  *graph,
                   gboolean      update,
                   GError      **error)
{
	GError *actual_error = NULL;
	/* anonymous blank node used as object in a statement */
	const gchar *blank_uri;

	if (data->blank_buffer.subject != NULL) {
		if (strcmp (data->blank_buffer.subject, object) == 0) {
			/* object still in blank buffer, need to flush buffer */
			tracker_data_blank_buffer_flush (data, &actual_error);

			if (actual_error) {
				g_propagate_error (error, actual_error);
				return FALSE;
			}
		}
	}

	blank_uri = g_hash_table_lookup (data->blank_buffer.table, object);

	if (blank_uri != NULL) {
		/* now insert statement referring to blank node */
		if (update) {
			tracker_data_update_statement (data, graph, subject, predicate, blank_uri, &actual_error);
		} else {
			tracker_data_insert_statement (data, graph, subject, predicate, blank_uri, &actual_error);
		}

		g_hash_table_remove (data->blank_buffer.table, object);

		if (actual_error) {
			g_propagate_error (error, actual_error);
			return FALSE;
		}

		return TRUE;
	} else {
		g_critical ("Blank node '%s' not found", object);

		return FALSE;
	}
}

void
tracker_data_insert_statement_with_uri (TrackerData  *data,
                                        const gchar  *graph,
                                        const gchar  *subject,
                                        const gchar  *predicate,
                                        const gchar  *object,
                                        GError      **error)
{
	GError          *actual_error = NULL;
	TrackerClass    *class;
	TrackerProperty *property;
	gint             prop_id = 0, graph_id = 0;
	gint             final_prop_id = 0, object_id = 0;
	gboolean change = FALSE;
	TrackerOntologies *ontologies;
	TrackerDBInterface *iface;

	g_return_if_fail (subject != NULL);
	g_return_if_fail (predicate != NULL);
	g_return_if_fail (object != NULL);
	g_return_if_fail (data->in_transaction);

	ontologies = tracker_data_manager_get_ontologies (data->manager);
	iface = tracker_data_manager_get_writable_db_interface (data->manager);

	property = tracker_ontologies_get_property_by_uri (ontologies, predicate);
	if (property == NULL) {
		g_set_error (error, TRACKER_SPARQL_ERROR, TRACKER_SPARQL_ERROR_UNKNOWN_PROPERTY,
		             "Property '%s' not found in the ontology", predicate);
		return;
	} else {
		if (tracker_property_get_data_type (property) != TRACKER_PROPERTY_TYPE_RESOURCE) {
			g_set_error (error, TRACKER_SPARQL_ERROR, TRACKER_SPARQL_ERROR_TYPE,
			             "Property '%s' does not accept URIs", predicate);
			return;
		}
		prop_id = tracker_property_get_id (property);
	}

	if (!tracker_property_get_transient (property)) {
		data->has_persistent = TRUE;
	}

	/* subjects and objects starting with `:' are anonymous blank nodes */
	if (g_str_has_prefix (object, ":")) {
		if (handle_blank_node (data, subject, predicate, object, graph, FALSE, &actual_error)) {
			return;
		}

		if (actual_error) {
			g_propagate_error (error, actual_error);
			return;
		}
	}

	if (!tracker_data_insert_statement_common (data, graph, subject, predicate, object, &actual_error)) {
		if (actual_error) {
			g_propagate_error (error, actual_error);
			return;
		}

		return;
	}

	if (property == tracker_ontologies_get_rdf_type (ontologies)) {
		/* handle rdf:type statements specially to
		   cope with inference and insert blank rows */
		class = tracker_ontologies_get_class_by_uri (ontologies, object);
		if (class != NULL) {
			cache_create_service_decomposed (data, class, graph, 0);
		} else {
			g_set_error (error, TRACKER_SPARQL_ERROR, TRACKER_SPARQL_ERROR_UNKNOWN_CLASS,
			             "Class '%s' not found in the ontology", object);
			return;
		}

		if (!data->in_journal_replay && !tracker_property_get_transient (property)) {
			graph_id = (graph != NULL ? query_resource_id (data, graph) : 0);
			final_prop_id = (prop_id != 0) ? prop_id : tracker_data_query_resource_id (data->manager, iface, predicate);
			object_id = query_resource_id (data, object);
		}

		change = TRUE;
	} else {
		/* add value to metadata database */
		change = cache_insert_metadata_decomposed (data, property, object, 0, graph, 0, &actual_error);
		if (actual_error) {
			g_propagate_error (error, actual_error);
			return;
		}

		if (change) {
			graph_id = (graph != NULL ? query_resource_id (data, graph) : 0);
			final_prop_id = (prop_id != 0) ? prop_id : tracker_data_query_resource_id (data->manager, iface, predicate);
			object_id = query_resource_id (data, object);

			if (data->insert_callbacks) {
				guint n;
				for (n = 0; n < data->insert_callbacks->len; n++) {
					TrackerStatementDelegate *delegate;

					delegate = g_ptr_array_index (data->insert_callbacks, n);
					delegate->callback (graph_id, graph, data->resource_buffer->id, subject,
					                    final_prop_id, object_id,
					                    object,
					                    data->resource_buffer->types,
					                    delegate->user_data);
				}
			}
		}
	}

#ifndef DISABLE_JOURNAL
	if (!data->in_journal_replay && change && !tracker_property_get_transient (property)) {
		tracker_db_journal_append_insert_statement_id (
			data->journal_writer,
			(graph != NULL ? query_resource_id (data, graph) : 0),
			data->resource_buffer->id,
			final_prop_id,
			object_id);
	}
#endif /* DISABLE_JOURNAL */

}

void
tracker_data_insert_statement_with_string (TrackerData  *data,
                                           const gchar  *graph,
                                           const gchar  *subject,
                                           const gchar  *predicate,
                                           const gchar  *object,
                                           GError      **error)
{
	GError          *actual_error = NULL;
	TrackerProperty *property;
	gboolean         change;
	gint             graph_id = 0, pred_id = 0;
	TrackerOntologies *ontologies;
	TrackerDBInterface *iface;
#ifndef DISABLE_JOURNAL
	gboolean         tried = FALSE;
#endif

	g_return_if_fail (subject != NULL);
	g_return_if_fail (predicate != NULL);
	g_return_if_fail (object != NULL);
	g_return_if_fail (data->in_transaction);

	ontologies = tracker_data_manager_get_ontologies (data->manager);
	iface = tracker_data_manager_get_writable_db_interface (data->manager);

	property = tracker_ontologies_get_property_by_uri (ontologies, predicate);
	if (property == NULL) {
		g_set_error (error, TRACKER_SPARQL_ERROR, TRACKER_SPARQL_ERROR_UNKNOWN_PROPERTY,
		             "Property '%s' not found in the ontology", predicate);
		return;
	} else {
		if (tracker_property_get_data_type (property) == TRACKER_PROPERTY_TYPE_RESOURCE) {
			g_set_error (error, TRACKER_SPARQL_ERROR, TRACKER_SPARQL_ERROR_TYPE,
			             "Property '%s' only accepts URIs", predicate);
			return;
		}
		pred_id = tracker_property_get_id (property);
	}

	if (!tracker_property_get_transient (property)) {
		data->has_persistent = TRUE;
	}

	if (!tracker_data_insert_statement_common (data, graph, subject, predicate, object, &actual_error)) {
		if (actual_error) {
			g_propagate_error (error, actual_error);
			return;
		}

		return;
	}

	/* add value to metadata database */
	change = cache_insert_metadata_decomposed (data, property, object, 0, graph, 0, &actual_error);
	if (actual_error) {
		g_propagate_error (error, actual_error);
		return;
	}

	if (data->insert_callbacks && change) {
		guint n;

		graph_id = (graph != NULL ? query_resource_id (data, graph) : 0);
		pred_id = (pred_id != 0) ? pred_id : tracker_data_query_resource_id (data->manager, iface, predicate);
#ifndef DISABLE_JOURNAL
		tried = TRUE;
#endif

		for (n = 0; n < data->insert_callbacks->len; n++) {
			TrackerStatementDelegate *delegate;

			delegate = g_ptr_array_index (data->insert_callbacks, n);
			delegate->callback (graph_id, graph, data->resource_buffer->id, subject,
			                    pred_id, 0 /* Always a literal */,
			                    object,
			                    data->resource_buffer->types,
			                    delegate->user_data);
		}
	}

#ifndef DISABLE_JOURNAL
	if (!data->in_journal_replay && change && !tracker_property_get_transient (property)) {
		if (!tried) {
			graph_id = (graph != NULL ? query_resource_id (data, graph) : 0);
			pred_id = (pred_id != 0) ? pred_id : tracker_data_query_resource_id (data->manager, iface, predicate);
		}
		if (!tracker_property_get_force_journal (property) &&
		    g_strcmp0 (graph, TRACKER_OWN_GRAPH_URN) == 0) {
			/* do not journal this statement extracted from filesystem */
			TrackerProperty *damaged;

			damaged = tracker_ontologies_get_property_by_uri (ontologies, TRACKER_PREFIX_TRACKER "damaged");
			tracker_db_journal_append_insert_statement (data->journal_writer,
			                                            graph_id,
				                                    data->resource_buffer->id,
				                                    tracker_property_get_id (damaged),
				                                    "true");
		} else {
			tracker_db_journal_append_insert_statement (data->journal_writer,
			                                            graph_id,
				                                    data->resource_buffer->id,
				                                    pred_id,
				                                    object);
		}
	}
#endif /* DISABLE_JOURNAL */
}

static void
tracker_data_update_statement_with_uri (TrackerData  *data,
                                        const gchar  *graph,
                                        const gchar  *subject,
                                        const gchar  *predicate,
                                        const gchar  *object,
                                        GError      **error)
{
	GError          *actual_error = NULL;
	TrackerClass    *class;
	TrackerProperty *property;
	gint             prop_id = 0, graph_id = 0;
	gint             final_prop_id = 0, object_id = 0;
	gboolean         change = FALSE;
	TrackerOntologies *ontologies;
	TrackerDBInterface *iface;

	g_return_if_fail (subject != NULL);
	g_return_if_fail (predicate != NULL);
	g_return_if_fail (data->in_transaction);

	ontologies = tracker_data_manager_get_ontologies (data->manager);
	iface = tracker_data_manager_get_writable_db_interface (data->manager);

	property = tracker_ontologies_get_property_by_uri (ontologies, predicate);
	if (property == NULL) {
		g_set_error (error, TRACKER_SPARQL_ERROR, TRACKER_SPARQL_ERROR_UNKNOWN_PROPERTY,
		             "Property '%s' not found in the ontology", predicate);
		return;
	} else {
		if (tracker_property_get_data_type (property) != TRACKER_PROPERTY_TYPE_RESOURCE) {
			g_set_error (error, TRACKER_SPARQL_ERROR, TRACKER_SPARQL_ERROR_TYPE,
			             "Property '%s' does not accept URIs", predicate);
			return;
		}
		prop_id = tracker_property_get_id (property);
	}

	if (!tracker_property_get_transient (property)) {
		data->has_persistent = TRUE;
	}

	/* subjects and objects starting with `:' are anonymous blank nodes */
	if (g_str_has_prefix (object, ":")) {
		if (handle_blank_node (data, subject, predicate, object, graph, TRUE, &actual_error)) {
			return;
		}

		if (actual_error) {
			g_propagate_error (error, actual_error);
			return;
		}
	}

	/* Update and insert share the exact same code here */
	if (!tracker_data_insert_statement_common (data, graph, subject, predicate, object, &actual_error)) {
		if (actual_error) {
			g_propagate_error (error, actual_error);
			return;
		}

		return;
	}

	if (property == tracker_ontologies_get_rdf_type (ontologies)) {
		/* handle rdf:type statements specially to
		   cope with inference and insert blank rows */
		class = tracker_ontologies_get_class_by_uri (ontologies, object);
		if (class != NULL) {
			/* Create here is fine for Update too */
			cache_create_service_decomposed (data, class, graph, 0);
		} else {
			g_set_error (error, TRACKER_SPARQL_ERROR, TRACKER_SPARQL_ERROR_UNKNOWN_CLASS,
			             "Class '%s' not found in the ontology", object);
			return;
		}

		if (!data->in_journal_replay && !tracker_property_get_transient (property)) {
			graph_id = (graph != NULL ? query_resource_id (data, graph) : 0);
			final_prop_id = (prop_id != 0) ? prop_id : tracker_data_query_resource_id (data->manager, iface, predicate);
			object_id = query_resource_id (data, object);
		}

		change = TRUE;
	} else {
		gint old_object_id = 0;
		GArray *old_values;
		gboolean multiple_values;
		GError *new_error = NULL;
		gboolean domain_unchecked = TRUE;

		multiple_values = tracker_property_get_multiple_values (property);

#if HAVE_TRACKER_FTS
		/* This is unavoidable with FTS */
		/* This does a check_property_domain too */
		old_values = get_old_property_values (data, property, &new_error);
		domain_unchecked = FALSE;
		if (!new_error) {
			if (old_values->len > 0) {
				/* evel knievel cast */
				GValue *v;

				v = &g_array_index (old_values, GValue, 0);
				old_object_id = (gint) g_value_get_int64 (v);
			}
		} else {
			g_propagate_error (error, new_error);
			return;
		}
#else
		/* We can disable correct object-id for deletes array here */
		if (!multiple_values) {
			guint r;

			for (r = 0; r < data->resource_buffer->types->len; r++) {
				TrackerClass *m_class = g_ptr_array_index (data->resource_buffer->types, r);

				/* We only do the old_values for GraphUpdated in tracker-store.
				 * The subject's known classes are in resource_buffer->types
				 * since resource_buffer_switch, so we can quickly check if any
				 * of them has tracker:notify annotated. If so, get the old
				 * values for the old_object_id */

				if (tracker_class_get_notify (m_class)) {
					/* This does a check_property_domain too */
					old_values = get_old_property_values (data, property, &new_error);
					domain_unchecked = FALSE;
					if (!new_error) {
						if (old_values->len > 0) {
							/* evel knievel cast */
							old_object_id = (gint) g_value_get_int64 (g_value_array_get_nth (old_values, 0));
						}
					} else {
						g_propagate_error (error, new_error);
						return;
					}

					break;
				}
			}
		}
#endif /* HAVE_TRACKER_FTS */

		if (domain_unchecked && !check_property_domain (data, property)) {
			g_set_error (error, TRACKER_SPARQL_ERROR, TRACKER_SPARQL_ERROR_CONSTRAINT,
			             "Subject `%s' is not in domain `%s' of property `%s'",
			             data->resource_buffer->subject,
			             tracker_class_get_name (tracker_property_get_domain (property)),
			             tracker_property_get_name (property));
			return;
		}

		/* update or add value to metadata database */
		change = cache_update_metadata_decomposed (data, property, object, 0, graph, 0, &actual_error);
		if (actual_error) {
			g_propagate_error (error, actual_error);
			return;
		}

		if (change) {
			graph_id = (graph != NULL ? query_resource_id (data, graph) : 0);
			final_prop_id = (prop_id != 0) ? prop_id : tracker_data_query_resource_id (data->manager, iface, predicate);
			object_id = query_resource_id (data, object);

			if (!multiple_values && data->delete_callbacks) {
				guint n;

				for (n = 0; n < data->delete_callbacks->len; n++) {
					TrackerStatementDelegate *delegate;

					/* Don't pass object to the delete, it's not correct */
					delegate = g_ptr_array_index (data->delete_callbacks, n);
					delegate->callback (graph_id, graph, data->resource_buffer->id, subject,
					                    final_prop_id, old_object_id,
					                    NULL,
					                    data->resource_buffer->types,
					                    delegate->user_data);
				}
			}

			if (data->insert_callbacks) {
				guint n;

				for (n = 0; n < data->insert_callbacks->len; n++) {
					TrackerStatementDelegate *delegate;

					delegate = g_ptr_array_index (data->insert_callbacks, n);
					delegate->callback (graph_id, graph, data->resource_buffer->id, subject,
					                    final_prop_id, object_id,
					                    object,
					                    data->resource_buffer->types,
					                    delegate->user_data);
				}
			}
		}
	}

#ifndef DISABLE_JOURNAL
	if (!data->in_journal_replay && change && !tracker_property_get_transient (property)) {
		tracker_db_journal_append_update_statement_id (
			data->journal_writer,
			(graph != NULL ? query_resource_id (data, graph) : 0),
			data->resource_buffer->id,
			final_prop_id,
			object_id);
	}
#endif /* DISABLE_JOURNAL */
}

static void
tracker_data_update_statement_with_string (TrackerData  *data,
                                           const gchar  *graph,
                                           const gchar  *subject,
                                           const gchar  *predicate,
                                           const gchar  *object,
                                           GError      **error)
{
	GError *actual_error = NULL;
	TrackerProperty *property;
	gboolean change;
	gint graph_id = 0, pred_id = 0;
	gboolean multiple_values;
	TrackerOntologies *ontologies;
	TrackerDBInterface *iface;
#ifndef DISABLE_JOURNAL
	gboolean tried = FALSE;
#endif
#if HAVE_TRACKER_FTS
	GError *new_error = NULL;
#endif /* HAVE_TRACKER_FTS */

	g_return_if_fail (subject != NULL);
	g_return_if_fail (predicate != NULL);
	g_return_if_fail (data->in_transaction);

	ontologies = tracker_data_manager_get_ontologies (data->manager);
	iface = tracker_data_manager_get_writable_db_interface (data->manager);

	property = tracker_ontologies_get_property_by_uri (ontologies, predicate);
	if (property == NULL) {
		g_set_error (error, TRACKER_SPARQL_ERROR, TRACKER_SPARQL_ERROR_UNKNOWN_PROPERTY,
		             "Property '%s' not found in the ontology", predicate);
		return;
	} else {
		if (tracker_property_get_data_type (property) == TRACKER_PROPERTY_TYPE_RESOURCE) {
			g_set_error (error, TRACKER_SPARQL_ERROR, TRACKER_SPARQL_ERROR_TYPE,
			             "Property '%s' only accepts URIs", predicate);
			return;
		}
		pred_id = tracker_property_get_id (property);
	}

	multiple_values = tracker_property_get_multiple_values (property);

	if (!tracker_property_get_transient (property)) {
		data->has_persistent = TRUE;
	}

	/* Update and insert share the exact same code here */
	if (!tracker_data_insert_statement_common (data, graph, subject, predicate, object, &actual_error)) {
		if (actual_error) {
			g_propagate_error (error, actual_error);
			return;
		}

		return;
	}

#if HAVE_TRACKER_FTS
	/* This is unavoidable with FTS */
	get_old_property_values (data, property, &new_error);
	if (new_error) {
		g_propagate_error (error, new_error);
		return;
	}
#else
	if (!check_property_domain (data, property)) {
		g_set_error (error, TRACKER_SPARQL_ERROR, TRACKER_SPARQL_ERROR_CONSTRAINT,
		             "Subject `%s' is not in domain `%s' of property `%s'",
		             data->resource_buffer->subject,
		             tracker_class_get_name (tracker_property_get_domain (property)),
		             tracker_property_get_name (property));
		return;
	}
#endif /* HAVE_TRACKER_FTS */

	/* add or update value to metadata database */
	change = cache_update_metadata_decomposed (data, property, object, 0, graph, 0, &actual_error);
	if (actual_error) {
		g_propagate_error (error, actual_error);
		return;
	}

	if (((!multiple_values && data->delete_callbacks) || data->insert_callbacks) && change) {
		graph_id = (graph != NULL ? query_resource_id (data, graph) : 0);
		pred_id = (pred_id != 0) ? pred_id : tracker_data_query_resource_id (data->manager, iface, predicate);
#ifndef DISABLE_JOURNAL
		tried = TRUE;
#endif
	}

	if ((!multiple_values && data->delete_callbacks) && change) {
		guint n;

		for (n = 0; n < data->delete_callbacks->len; n++) {
			TrackerStatementDelegate *delegate;

			/* Don't pass object to the delete, it's not correct */
			delegate = g_ptr_array_index (data->delete_callbacks, n);
			delegate->callback (graph_id, graph, data->resource_buffer->id, subject,
			                    pred_id, 0 /* Always a literal */,
			                    NULL,
			                    data->resource_buffer->types,
			                    delegate->user_data);
		}
	}

	if (data->insert_callbacks && change) {
		guint n;
		for (n = 0; n < data->insert_callbacks->len; n++) {
			TrackerStatementDelegate *delegate;

			delegate = g_ptr_array_index (data->insert_callbacks, n);
			delegate->callback (graph_id, graph, data->resource_buffer->id, subject,
			                    pred_id, 0 /* Always a literal */,
			                    object,
			                    data->resource_buffer->types,
			                    delegate->user_data);
		}
	}

#ifndef DISABLE_JOURNAL
	if (!data->in_journal_replay && change && !tracker_property_get_transient (property)) {
		if (!tried) {
			graph_id = (graph != NULL ? query_resource_id (data, graph) : 0);
			pred_id = (pred_id != 0) ? pred_id : tracker_data_query_resource_id (data->manager, iface, predicate);
		}
		if (!tracker_property_get_force_journal (property) &&
		    g_strcmp0 (graph, TRACKER_OWN_GRAPH_URN) == 0) {
			/* do not journal this statement extracted from filesystem */
			TrackerProperty *damaged;

			damaged = tracker_ontologies_get_property_by_uri (ontologies, TRACKER_PREFIX_TRACKER "damaged");
			tracker_db_journal_append_update_statement (data->journal_writer,
			                                            graph_id,
			                                            data->resource_buffer->id,
			                                            tracker_property_get_id (damaged),
			                                            "true");
		} else {
			tracker_db_journal_append_update_statement (data->journal_writer,
			                                            graph_id,
			                                            data->resource_buffer->id,
			                                            pred_id,
			                                            object);
		}
	}
#endif /* DISABLE_JOURNAL */
}

void
tracker_data_update_statement (TrackerData  *data,
                               const gchar  *graph,
                               const gchar  *subject,
                               const gchar  *predicate,
                               const gchar  *object,
                               GError      **error)
{
	TrackerProperty *property;
	TrackerOntologies *ontologies;

	g_return_if_fail (subject != NULL);
	g_return_if_fail (predicate != NULL);
	g_return_if_fail (data->in_transaction);

	ontologies = tracker_data_manager_get_ontologies (data->manager);
	property = tracker_ontologies_get_property_by_uri (ontologies, predicate);

	if (property != NULL) {
		if (object == NULL) {
			GError *new_error = NULL;
			if (property == tracker_ontologies_get_rdf_type (ontologies)) {
				g_set_error (error, TRACKER_SPARQL_ERROR, TRACKER_SPARQL_ERROR_UNSUPPORTED,
				             "Using 'null' with '%s' is not supported", predicate);
				return;
			}

			/* Flush upfront to make a null,x,null,y,z work: When x is set then
			 * if a null comes, we need to be flushed */

			tracker_data_update_buffer_flush (data, &new_error);
			if (new_error) {
				g_propagate_error (error, new_error);
				return;
			}

			delete_all_objects (data, graph, subject, predicate, error);

			/* Flush at the end to make null, x work. When x arrives the null
			 * (delete all values of the multivalue) must be flushed */

			tracker_data_update_buffer_flush (data, &new_error);
			if (new_error) {
				g_propagate_error (error, new_error);
			}
		} else {
			if (tracker_property_get_data_type (property) == TRACKER_PROPERTY_TYPE_RESOURCE) {
				tracker_data_update_statement_with_uri (data, graph, subject, predicate, object, error);
			} else {
				tracker_data_update_statement_with_string (data, graph, subject, predicate, object, error);
			}
		}
	} else {
		g_set_error (error, TRACKER_SPARQL_ERROR, TRACKER_SPARQL_ERROR_UNKNOWN_PROPERTY,
		             "Property '%s' not found in the ontology", predicate);
	}
}

void
tracker_data_begin_transaction (TrackerData  *data,
                                GError      **error)
{
	TrackerDBInterface *iface;
	TrackerDBManager *db_manager;

	g_return_if_fail (!data->in_transaction);

	db_manager = tracker_data_manager_get_db_manager (data->manager);

	if (!tracker_db_manager_has_enough_space (db_manager)) {
		g_set_error (error, TRACKER_SPARQL_ERROR, TRACKER_SPARQL_ERROR_NO_SPACE,
			"There is not enough space on the file system for update operations");
		return;
	}

	data->resource_time = time (NULL);

	data->has_persistent = FALSE;

	if (data->update_buffer.resource_cache == NULL) {
		data->update_buffer.resource_cache = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
		/* used for normal transactions */
		data->update_buffer.resources = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, (GDestroyNotify) resource_buffer_free);
		/* used for journal replay */
		data->update_buffer.resources_by_id = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, (GDestroyNotify) resource_buffer_free);
	}

	data->resource_buffer = NULL;
	if (data->blank_buffer.table == NULL) {
		data->blank_buffer.table = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
	}

	iface = tracker_data_manager_get_writable_db_interface (data->manager);

	tracker_db_interface_execute_query (iface, NULL, "PRAGMA cache_size = %d", TRACKER_DB_CACHE_SIZE_UPDATE);

	tracker_db_interface_start_transaction (iface);

#ifndef DISABLE_JOURNAL
	if (!data->in_journal_replay) {
		g_assert (data->journal_writer == NULL);
		/* Pick the right journal writer for this transaction */
		data->journal_writer = data->in_ontology_transaction ?
			tracker_data_manager_get_ontology_writer (data->manager) :
			tracker_data_manager_get_journal_writer (data->manager);

		tracker_db_journal_start_transaction (data->journal_writer, data->resource_time);
	}
#endif /* DISABLE_JOURNAL */

	data->in_transaction = TRUE;
}

void
tracker_data_begin_ontology_transaction (TrackerData  *data,
                                         GError      **error)
{
	data->in_ontology_transaction = TRUE;
	tracker_data_begin_transaction (data, error);
}

void
tracker_data_begin_transaction_for_replay (TrackerData  *data,
                                           time_t        time,
                                           GError      **error)
{
	data->in_journal_replay = TRUE;
	tracker_data_begin_transaction (data, error);
	data->resource_time = time;
}

void
tracker_data_commit_transaction (TrackerData  *data,
                                 GError      **error)
{
	TrackerDBInterface *iface;
	GError *actual_error = NULL;

	g_return_if_fail (data->in_transaction);

	iface = tracker_data_manager_get_writable_db_interface (data->manager);

	tracker_data_update_buffer_flush (data, &actual_error);
	if (actual_error) {
		tracker_data_rollback_transaction (data);
		g_propagate_error (error, actual_error);
		return;
	}

	tracker_db_interface_end_db_transaction (iface,
	                                         &actual_error);

	if (actual_error) {
		tracker_data_rollback_transaction (data);
		g_propagate_error (error, actual_error);
		return;
	}

#ifndef DISABLE_JOURNAL
	if (!data->in_journal_replay) {
		g_assert (data->journal_writer != NULL);
		if (data->has_persistent || data->in_ontology_transaction) {
			tracker_db_journal_commit_db_transaction (data->journal_writer, &actual_error);
		} else {
			/* If we only had transient properties, then we must not write
			 * anything to the journal. So we roll it back, but only the
			 * journal's part. */
			tracker_db_journal_rollback_transaction (data->journal_writer);
		}

		data->journal_writer = NULL;

		if (actual_error) {
			/* Can't write in journal anymore; quite a serious problem */
			g_propagate_error (error, actual_error);
			/* Don't return, remainder of the function cleans things up */
		}
	}
#endif /* DISABLE_JOURNAL */

	get_transaction_modseq (data);
	if (data->has_persistent && !data->in_ontology_transaction) {
		data->transaction_modseq++;
	}

	data->resource_time = 0;
	data->in_transaction = FALSE;
	data->in_ontology_transaction = FALSE;

	if (data->update_buffer.class_counts) {
		/* successful transaction, no need to rollback class counts,
		   so remove them */
		g_hash_table_remove_all (data->update_buffer.class_counts);
	}

#if HAVE_TRACKER_FTS
	if (data->update_buffer.fts_ever_updated) {
		data->update_buffer.fts_ever_updated = FALSE;
	}
#endif

	tracker_db_interface_execute_query (iface, NULL, "PRAGMA cache_size = %d", TRACKER_DB_CACHE_SIZE_DEFAULT);

	g_hash_table_remove_all (data->update_buffer.resources);
	g_hash_table_remove_all (data->update_buffer.resources_by_id);
	g_hash_table_remove_all (data->update_buffer.resource_cache);

	data->in_journal_replay = FALSE;
}

void
tracker_data_notify_transaction (TrackerData           *data,
                                 TrackerDataCommitType  commit_type)
{
	if (data->commit_callbacks) {
		guint n;
		for (n = 0; n < data->commit_callbacks->len; n++) {
			TrackerCommitDelegate *delegate;
			delegate = g_ptr_array_index (data->commit_callbacks, n);
			delegate->callback (commit_type, delegate->user_data);
		}
	}
}

void
tracker_data_rollback_transaction (TrackerData *data)
{
	TrackerDBInterface *iface;
	GError *ignorable = NULL;

	g_return_if_fail (data->in_transaction);

	data->in_transaction = FALSE;
	data->in_ontology_transaction = FALSE;

	iface = tracker_data_manager_get_writable_db_interface (data->manager);

	tracker_data_update_buffer_clear (data);

	tracker_db_interface_execute_query (iface, &ignorable, "ROLLBACK");

	if (ignorable) {
		g_warning ("Transaction rollback failed: %s\n", ignorable->message);
		g_clear_error (&ignorable);
	}

	tracker_db_interface_execute_query (iface, NULL, "PRAGMA cache_size = %d", TRACKER_DB_CACHE_SIZE_DEFAULT);

	/* Runtime false in case of DISABLE_JOURNAL */
	if (!data->in_journal_replay) {

#ifndef DISABLE_JOURNAL
		g_assert (data->journal_writer != NULL);
		tracker_db_journal_rollback_transaction (data->journal_writer);
		data->journal_writer = NULL;
#endif /* DISABLE_JOURNAL */

		if (data->rollback_callbacks) {
			guint n;
			for (n = 0; n < data->rollback_callbacks->len; n++) {
				TrackerCommitDelegate *delegate;
				delegate = g_ptr_array_index (data->rollback_callbacks, n);
				delegate->callback (TRUE, delegate->user_data);
			}
		}
	}
}

static GVariant *
update_sparql (TrackerData  *data,
               const gchar  *update,
               gboolean      blank,
               GError      **error)
{
	GError *actual_error = NULL;
	TrackerSparqlQuery *sparql_query;
	GVariant *blank_nodes;

	g_return_val_if_fail (update != NULL, NULL);

	tracker_data_begin_transaction (data, &actual_error);
	if (actual_error) {
		g_propagate_error (error, actual_error);
		return NULL;
	}

	sparql_query = tracker_sparql_query_new_update (data->manager, update);
	blank_nodes = tracker_sparql_query_execute_update (sparql_query, blank, &actual_error);
	g_object_unref (sparql_query);

	if (actual_error) {
		tracker_data_rollback_transaction (data);
		g_propagate_error (error, actual_error);
		return NULL;
	}

	tracker_data_commit_transaction (data, &actual_error);
	if (actual_error) {
		g_propagate_error (error, actual_error);
		return NULL;
	}

	return blank_nodes;
}

void
tracker_data_update_sparql (TrackerData  *data,
                            const gchar  *update,
                            GError      **error)
{
	update_sparql (data, update, FALSE, error);
}

GVariant *
tracker_data_update_sparql_blank (TrackerData  *data,
                                  const gchar  *update,
                                  GError      **error)
{
	return update_sparql (data, update, TRUE, error);
}

void
tracker_data_load_turtle_file (TrackerData  *data,
                               GFile        *file,
                               GError      **error)
{
	g_return_if_fail (G_IS_FILE (file));

	tracker_turtle_reader_load (file, data, error);
}

void
tracker_data_sync (TrackerData *data)
{
#ifndef DISABLE_JOURNAL
	TrackerDBJournal *writer;

	writer = tracker_data_manager_get_journal_writer (data->manager);
	if (writer)
		tracker_db_journal_fsync (writer);

	writer = tracker_data_manager_get_ontology_writer (data->manager);
	if (writer)
		tracker_db_journal_fsync (writer);
#endif
}

#ifndef DISABLE_JOURNAL

void
tracker_data_replay_journal (TrackerData          *data,
                             TrackerBusyCallback   busy_callback,
                             gpointer              busy_user_data,
                             const gchar          *busy_status,
                             GError              **error)
{
	GError *journal_error = NULL;
	TrackerProperty *rdf_type = NULL;
	gint last_operation_type = 0;
	const gchar *uri;
	GError *n_error = NULL;
	GFile *data_location;
	TrackerDBJournalReader *reader;
	TrackerOntologies *ontologies;

	ontologies = tracker_data_manager_get_ontologies (data->manager);
	rdf_type = tracker_ontologies_get_rdf_type (ontologies);

	data_location = tracker_data_manager_get_data_location (data->manager);
	reader = tracker_db_journal_reader_new (data_location, &n_error);
	g_object_unref (data_location);

	if (!reader) {
		/* This is fatal (doesn't happen when file doesn't exist, does happen
		 * when for some other reason the reader can't be created) */
		g_propagate_error (error, n_error);
		return;
	}

	while (tracker_db_journal_reader_next (reader, &journal_error)) {
		TrackerDBJournalEntryType type;
		const gchar *object;
		gint graph_id, subject_id, predicate_id, object_id;

		type = tracker_db_journal_reader_get_entry_type (reader);
		if (type == TRACKER_DB_JOURNAL_RESOURCE) {
			GError *new_error = NULL;
			TrackerDBInterface *iface;
			TrackerDBStatement *stmt;
			gint id;

			tracker_db_journal_reader_get_resource (reader, &id, &uri);

			iface = tracker_data_manager_get_writable_db_interface (data->manager);

			stmt = tracker_db_interface_create_statement (iface, TRACKER_DB_STATEMENT_CACHE_TYPE_UPDATE, &new_error,
			                                              "INSERT INTO Resource (ID, Uri) VALUES (?, ?)");

			if (stmt) {
				tracker_db_statement_bind_int (stmt, 0, id);
				tracker_db_statement_bind_text (stmt, 1, uri);
				tracker_db_statement_execute (stmt, &new_error);
				g_object_unref (stmt);
			}

			if (new_error) {
				g_warning ("Journal replay error: '%s'", new_error->message);
				g_error_free (new_error);
			}

		} else if (type == TRACKER_DB_JOURNAL_START_TRANSACTION) {
			tracker_data_begin_transaction_for_replay (data, tracker_db_journal_reader_get_time (reader), NULL);
		} else if (type == TRACKER_DB_JOURNAL_END_TRANSACTION) {
			GError *new_error = NULL;
			tracker_data_update_buffer_might_flush (data, &new_error);

			tracker_data_commit_transaction (data, &new_error);
			if (new_error) {
				/* Out of disk is an unrecoverable fatal error */
				if (g_error_matches (new_error, TRACKER_DB_INTERFACE_ERROR, TRACKER_DB_NO_SPACE)) {
					g_propagate_error (error, new_error);
					return;
				} else {
					g_warning ("Journal replay error: '%s'", new_error->message);
					g_clear_error (&new_error);
				}
			}
		} else if (type == TRACKER_DB_JOURNAL_INSERT_STATEMENT ||
		           type == TRACKER_DB_JOURNAL_UPDATE_STATEMENT) {
			GError *new_error = NULL;
			TrackerProperty *property = NULL;

			tracker_db_journal_reader_get_statement (reader, &graph_id, &subject_id, &predicate_id, &object);

			if (last_operation_type == -1) {
				tracker_data_update_buffer_flush (data, &new_error);
				if (new_error) {
					g_warning ("Journal replay error: '%s'", new_error->message);
					g_clear_error (&new_error);
				}
			}
			last_operation_type = 1;

			uri = tracker_ontologies_get_uri_by_id (ontologies, predicate_id);
			if (uri) {
				property = tracker_ontologies_get_property_by_uri (ontologies, uri);
			}

			if (property) {
				resource_buffer_switch (data, NULL, NULL, subject_id);

				if (type == TRACKER_DB_JOURNAL_UPDATE_STATEMENT) {
					cache_update_metadata_decomposed (data, property, object, 0, NULL, graph_id, &new_error);
				} else {
					cache_insert_metadata_decomposed (data, property, object, 0, NULL, graph_id, &new_error);
				}
				if (new_error) {
					g_warning ("Journal replay error: '%s'", new_error->message);
					g_clear_error (&new_error);
				}

			} else {
				g_warning ("Journal replay error: 'property with ID %d doesn't exist'", predicate_id);
			}

		} else if (type == TRACKER_DB_JOURNAL_INSERT_STATEMENT_ID ||
		           type == TRACKER_DB_JOURNAL_UPDATE_STATEMENT_ID) {
			GError *new_error = NULL;
			TrackerClass *class = NULL;
			TrackerProperty *property = NULL;

			tracker_db_journal_reader_get_statement_id (reader, &graph_id, &subject_id, &predicate_id, &object_id);

			if (last_operation_type == -1) {
				tracker_data_update_buffer_flush (data, &new_error);
				if (new_error) {
					g_warning ("Journal replay error: '%s'", new_error->message);
					g_clear_error (&new_error);
				}
			}
			last_operation_type = 1;

			uri = tracker_ontologies_get_uri_by_id (ontologies, predicate_id);
			if (uri) {
				property = tracker_ontologies_get_property_by_uri (ontologies, uri);
			}

			if (property) {
				if (tracker_property_get_data_type (property) != TRACKER_PROPERTY_TYPE_RESOURCE) {
					g_warning ("Journal replay error: 'property with ID %d does not account URIs'", predicate_id);
				} else {
					resource_buffer_switch (data, NULL, NULL, subject_id);

					if (property == rdf_type) {
						uri = tracker_ontologies_get_uri_by_id (ontologies, object_id);
						if (uri) {
							class = tracker_ontologies_get_class_by_uri (ontologies, uri);
						}
						if (class) {
							cache_create_service_decomposed (data, class, NULL, graph_id);
						} else {
							g_warning ("Journal replay error: 'class with ID %d not found in the ontology'", object_id);
						}
					} else {
						GError *new_error = NULL;

						/* add value to metadata database */
						if (type == TRACKER_DB_JOURNAL_UPDATE_STATEMENT_ID) {
							cache_update_metadata_decomposed (data, property, NULL, object_id, NULL, graph_id, &new_error);
						} else {
							cache_insert_metadata_decomposed (data, property, NULL, object_id, NULL, graph_id, &new_error);
						}

						if (new_error) {
							g_warning ("Journal replay error: '%s'", new_error->message);
							g_error_free (new_error);
						}
					}
				}
			} else {
				g_warning ("Journal replay error: 'property with ID %d doesn't exist'", predicate_id);
			}

		} else if (type == TRACKER_DB_JOURNAL_DELETE_STATEMENT) {
			GError *new_error = NULL;
			TrackerProperty *property = NULL;

			tracker_db_journal_reader_get_statement (reader, &graph_id, &subject_id, &predicate_id, &object);

			if (last_operation_type == 1) {
				tracker_data_update_buffer_flush (data, &new_error);
				if (new_error) {
					g_warning ("Journal replay error: '%s'", new_error->message);
					g_clear_error (&new_error);
				}
			}
			last_operation_type = -1;

			resource_buffer_switch (data, NULL, NULL, subject_id);

			uri = tracker_ontologies_get_uri_by_id (ontologies, predicate_id);
			if (uri) {
				property = tracker_ontologies_get_property_by_uri (ontologies, uri);
			}

			if (property) {
				GError *new_error = NULL;

				if (object && rdf_type == property) {
					TrackerClass *class;

					class = tracker_ontologies_get_class_by_uri (ontologies, object);
					if (class != NULL) {
						cache_delete_resource_type (data, class, NULL, graph_id);
					} else {
						g_warning ("Journal replay error: 'class with '%s' not found in the ontology'", object);
					}
				} else {
					delete_metadata_decomposed (data, property, object, 0, &new_error);
				}

				if (new_error) {
					g_warning ("Journal replay error: '%s'", new_error->message);
					g_error_free (new_error);
				}

			} else {
				g_warning ("Journal replay error: 'property with ID %d doesn't exist'", predicate_id);
			}

		} else if (type == TRACKER_DB_JOURNAL_DELETE_STATEMENT_ID) {
			GError *new_error = NULL;
			TrackerClass *class = NULL;
			TrackerProperty *property = NULL;

			tracker_db_journal_reader_get_statement_id (reader, &graph_id, &subject_id, &predicate_id, &object_id);

			if (last_operation_type == 1) {
				tracker_data_update_buffer_flush (data, &new_error);
				if (new_error) {
					g_warning ("Journal replay error: '%s'", new_error->message);
					g_clear_error (&new_error);
				}
			}
			last_operation_type = -1;

			uri = tracker_ontologies_get_uri_by_id (ontologies, predicate_id);
			if (uri) {
				property = tracker_ontologies_get_property_by_uri (ontologies, uri);
			}

			if (property) {

				resource_buffer_switch (data, NULL, NULL, subject_id);

				if (property == rdf_type) {
					uri = tracker_ontologies_get_uri_by_id (ontologies, object_id);
					if (uri) {
						class = tracker_ontologies_get_class_by_uri (ontologies, uri);
					}
					if (class) {
						cache_delete_resource_type (data, class, NULL, graph_id);
					} else {
						g_warning ("Journal replay error: 'class with ID %d not found in the ontology'", object_id);
					}
				} else {
					GError *new_error = NULL;

					delete_metadata_decomposed (data, property, NULL, object_id, &new_error);

					if (new_error) {
						g_warning ("Journal replay error: '%s'", new_error->message);
						g_error_free (new_error);
					}
				}
			} else {
				g_warning ("Journal replay error: 'property with ID %d doesn't exist'", predicate_id);
			}
		}

		if (busy_callback) {
			busy_callback (busy_status,
			               tracker_db_journal_reader_get_progress (reader),
			               busy_user_data);
		}
	}


	if (journal_error) {
		GError *n_error = NULL;
		gsize size;
		GFile *cache_location, *data_location;
		TrackerDBJournal *writer;

		size = tracker_db_journal_reader_get_size_of_correct (reader);
		tracker_db_journal_reader_free (reader);

		cache_location = tracker_data_manager_get_cache_location(data->manager);
		data_location = tracker_data_manager_get_data_location (data->manager);

		writer = tracker_db_journal_new (data_location, FALSE, &n_error);
		g_object_unref (cache_location);
		g_object_unref (data_location);

		if (n_error) {
			g_clear_error (&journal_error);
			/* This is fatal (journal file not writable, etc) */
			g_propagate_error (error, n_error);
			return;
		}
		tracker_db_journal_truncate (writer, size);
		tracker_db_journal_free (writer, &n_error);

		if (n_error) {
			g_clear_error (&journal_error);
			/* This is fatal (close of journal file failed after truncate) */
			g_propagate_error (error, n_error);
			return;
		}

		g_clear_error (&journal_error);
	} else {
		tracker_db_journal_reader_free (reader);
	}
}

#else

void
tracker_data_replay_journal (TrackerData          *data,
                             TrackerBusyCallback   busy_callback,
                             gpointer              busy_user_data,
                             const gchar          *busy_status,
                             GError              **error)
{
	g_critical ("Not good. We disabled the journal and yet replaying it got called");
}

#endif /* DISABLE_JOURNAL */
