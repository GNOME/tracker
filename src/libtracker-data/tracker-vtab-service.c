/*
 * Copyright (C) 2019, Red Hat Inc.
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
 *
 * Author: Carlos Garnacho <carlosg@gnome.org>
 */
#include "config.h"

#include "libtracker-common/tracker-utils.h"
#include "tracker-vtab-service.h"
#include <libtracker-sparql/tracker-connection.h>

#define N_VARIABLES 100
#define N_PARAMETERS 50
#define COL_SERVICE 0
#define COL_QUERY 1
#define COL_SILENT 2
#define COL_LAST 3
#define COL_FIRST_PARAMETER COL_LAST
#define COL_FIRST_VARIABLE (COL_LAST + (N_PARAMETERS * 2))

typedef struct {
	sqlite3 *db;
} TrackerServiceModule;

typedef struct {
	struct sqlite3_vtab parent;
	TrackerServiceModule *module;
	GList *cursors;
} TrackerServiceVTab;

typedef struct {
	struct sqlite3_vtab_cursor parent;
	TrackerServiceVTab *vtab;
	TrackerSparqlConnection *conn;
	TrackerSparqlCursor *sparql_cursor;
	GHashTable *parameter_columns;
	gchar *service;
	gchar *query;
	guint64 rowid;
	guint silent   : 1;
	guint finished : 1;
} TrackerServiceCursor;

typedef struct {
	int column;
	int op;
} ConstraintData;

static void
tracker_service_cursor_set_vtab_error (TrackerServiceCursor *cursor,
                                       const gchar          *message)
{
	TrackerServiceVTab *vtab = cursor->vtab;

	if (vtab->parent.zErrMsg)
		sqlite3_free (vtab->parent.zErrMsg);
	vtab->parent.zErrMsg = sqlite3_mprintf ("In service '%s': %s",
	                                        cursor->service, message);
}

static void
tracker_service_module_free (gpointer data)
{
	TrackerServiceModule *module = data;

	g_free (module);
}

static void
tracker_service_vtab_free (gpointer data)
{
	TrackerServiceVTab *vtab = data;

	g_list_free (vtab->cursors);
	g_free (vtab);
}

static void
tracker_service_cursor_free (gpointer data)
{
	TrackerServiceCursor *cursor = data;

	g_clear_pointer (&cursor->parameter_columns, g_hash_table_unref);
	g_free (cursor->service);
	g_free (cursor->query);
	g_clear_object (&cursor->conn);
	g_clear_object (&cursor->sparql_cursor);

	g_free (cursor);
}

static int
service_create (sqlite3            *db,
		gpointer            data,
		int                 argc,
		const char *const  *argv,
		sqlite3_vtab      **vtab_out,
		char              **err_out)
{
	TrackerServiceModule *module = data;
	TrackerServiceVTab *vtab;
	GString *str;
	gint i, rc;

	vtab = g_new0 (TrackerServiceVTab, 1);
	vtab->module = module;

	str = g_string_new ("CREATE TABLE x(\n");

	g_string_append (str,
			 "service TEXT HIDDEN, "
			 "query TEXT HIDDEN, "
			 "silent INTEGER HIDDEN");

	for (i = 0; i < N_PARAMETERS; i++) {
		g_string_append_printf (str, ", valuename%d TEXT HIDDEN", i);
		g_string_append_printf (str, ", value%d TEXT HIDDEN", i);
	}

	for (i = 0; i < N_VARIABLES; i++)
		g_string_append_printf (str, ", col%d TEXT", i);

	g_string_append (str, ")");

	rc = sqlite3_declare_vtab (module->db, str->str);
	g_string_free (str, TRUE);

	if (rc == SQLITE_OK)
		*vtab_out = &vtab->parent;
	else
		g_free (vtab);

	return rc;
}

static int
service_best_index (sqlite3_vtab       *vtab,
		    sqlite3_index_info *info)
{
	int i, argv_idx = 1;
	ConstraintData *data;

	data = sqlite3_malloc (sizeof (ConstraintData) * info->nConstraint);
	bzero (data, sizeof (ConstraintData) * info->nConstraint);

	for (i = 0; i < info->nConstraint; i++) {
		if (!info->aConstraint[i].usable)
			continue;

		if (info->aConstraint[i].iColumn >= COL_FIRST_VARIABLE) {
			info->aConstraintUsage[i].argvIndex = -1;
			continue;
		}

		if (info->aConstraint[i].op != SQLITE_INDEX_CONSTRAINT_EQ)
			goto error;

		data[i].column = info->aConstraint[i].iColumn;
		data[i].op = info->aConstraint[i].op;

		info->aConstraintUsage[i].argvIndex = argv_idx;
		info->aConstraintUsage[i].omit = FALSE;
		argv_idx++;
	}

	info->orderByConsumed = FALSE;
	info->idxStr = (char *) data;
	info->needToFreeIdxStr = TRUE;

	return SQLITE_OK;

error:
	sqlite3_free (data);
	return SQLITE_ERROR;
}

static int
service_destroy (sqlite3_vtab *vtab)
{
	tracker_service_vtab_free (vtab);
	return SQLITE_OK;
}

static int
service_open (sqlite3_vtab         *vtab_sqlite,
	      sqlite3_vtab_cursor **cursor_ret)
{
	TrackerServiceVTab *vtab = (TrackerServiceVTab *) vtab_sqlite;
	TrackerServiceCursor *cursor;

	cursor = g_new0 (TrackerServiceCursor, 1);
	cursor->vtab = vtab;

	vtab->cursors = g_list_prepend (vtab->cursors, cursor);
	*cursor_ret = (sqlite3_vtab_cursor *) cursor;

	return SQLITE_OK;
}

static int
service_close (sqlite3_vtab_cursor *vtab_cursor)
{
	TrackerServiceCursor *cursor = (TrackerServiceCursor *) vtab_cursor;
	TrackerServiceVTab *vtab = cursor->vtab;

	vtab->cursors = g_list_remove (vtab->cursors, cursor);
	tracker_service_cursor_free (cursor);
	return SQLITE_OK;
}

static void
apply_to_statement (TrackerSparqlStatement *statement,
                    const gchar            *name,
                    sqlite3_value          *value)
{
	switch (sqlite3_value_type (value)) {
	case SQLITE_INTEGER:
		tracker_sparql_statement_bind_int (statement,
		                                   name,
		                                   sqlite3_value_int64 (value));
		break;
	case SQLITE_FLOAT:
		tracker_sparql_statement_bind_double (statement,
		                                      name,
		                                      sqlite3_value_double (value));
		break;
	case SQLITE_TEXT:
	case SQLITE_BLOB:
		tracker_sparql_statement_bind_string (statement,
		                                      name,
		                                      sqlite3_value_text (value));
	case SQLITE_NULL:
	default:
		break;
	}
}

static void
apply_statement_parameters (TrackerSparqlStatement *statement,
                            GHashTable             *names,
                            GHashTable             *values)
{
	GHashTableIter iter;
	sqlite3_value *name, *value;
	gpointer key;

	if (!names || !values)
		return;

	g_hash_table_iter_init (&iter, names);

	while (g_hash_table_iter_next (&iter, &key, (gpointer *) &name)) {
		value = g_hash_table_lookup (values, key);
		if (!value)
			continue;

		apply_to_statement (statement,
		                    sqlite3_value_text (name),
		                    value);
	}
}

static int
service_filter (sqlite3_vtab_cursor  *vtab_cursor,
		int                   idx,
		const char           *idx_str,
		int                   argc,
		sqlite3_value       **argv)
{
	TrackerServiceCursor *cursor = (TrackerServiceCursor *) vtab_cursor;
	const ConstraintData *constraints = (const ConstraintData *) idx_str;
	TrackerSparqlStatement *statement;
	GHashTable *names = NULL, *values = NULL;
	gchar *uri_scheme = NULL;
	GError *error = NULL;
	gint i;

	cursor->finished = FALSE;
	cursor->rowid = 0;

	for (i = 0; i < argc; i++) {
		if (constraints[i].column == COL_SERVICE) {
			cursor->service = g_strdup (sqlite3_value_text (argv[i]));
		} else if (constraints[i].column == COL_QUERY) {
			cursor->query = g_strdup (sqlite3_value_text (argv[i]));
		} if (constraints[i].column == COL_SILENT) {
			cursor->silent = !!sqlite3_value_int (argv[i]);
		} else if (constraints[i].column >= COL_FIRST_PARAMETER &&
		           constraints[i].column < COL_FIRST_VARIABLE) {
			guint param_num;

			if (!names)
				names = g_hash_table_new (NULL, NULL);
			if (!values)
				values = g_hash_table_new (NULL, NULL);
			if (!cursor->parameter_columns)
				cursor->parameter_columns = g_hash_table_new_full (NULL, NULL, NULL,
				                                                   (GDestroyNotify) sqlite3_value_free);

			if ((constraints[i].column - COL_FIRST_PARAMETER) % 2 == 0) {
				/* Parameter name */
				param_num = (constraints[i].column - COL_FIRST_PARAMETER) / 2;
				g_hash_table_insert (names,
				                     GUINT_TO_POINTER (param_num),
				                     argv[i]);
			} else {
				/* Parameter value */
				param_num = (constraints[i].column - COL_FIRST_PARAMETER - 1) / 2;
				g_hash_table_insert (values,
				                     GUINT_TO_POINTER (param_num),
				                     argv[i]);
			}

			g_hash_table_insert (cursor->parameter_columns,
			                     GINT_TO_POINTER (constraints[i].column),
			                     sqlite3_value_dup (argv[i]));
		}
	}

	if (!cursor->service || !cursor->query)
		goto fail;

	uri_scheme = g_uri_parse_scheme (cursor->service);
	if (g_strcmp0 (uri_scheme, "dbus") == 0) {
		gchar *bus_name, *object_path;
		GDBusConnection *dbus_connection;
		GBusType bus_type;

		if (!tracker_util_parse_dbus_uri (cursor->service,
						  &bus_type,
		                                  &bus_name, &object_path)) {
			g_set_error (&error,
			             TRACKER_SPARQL_ERROR,
			             TRACKER_SPARQL_ERROR_PARSE,
			             "Failed to parse uri '%s'",
			             cursor->service);
			goto fail;
		}

		if (!g_dbus_is_name (bus_name)) {
			g_set_error (&error,
			             TRACKER_SPARQL_ERROR,
			             TRACKER_SPARQL_ERROR_PARSE,
			             "Invalid bus name '%s'",
			             bus_name);
			goto fail;
		}

		dbus_connection = g_bus_get_sync (bus_type, NULL, &error);
		if (!dbus_connection)
			goto fail;

		cursor->conn = tracker_sparql_connection_bus_new (bus_name, object_path,
		                                                  dbus_connection, &error);
		g_free (bus_name);
		g_free (object_path);

		if (!cursor->conn)
			goto fail;
	} else if (g_strcmp0 (uri_scheme, "http") == 0) {
		cursor->conn = tracker_sparql_connection_remote_new (cursor->service);
	}

	if (!cursor->conn) {
		g_set_error (&error,
		             TRACKER_SPARQL_ERROR,
		             TRACKER_SPARQL_ERROR_UNSUPPORTED,
		             "Unsupported uri '%s'",
		             cursor->service);
		goto fail;
	}

	statement = tracker_sparql_connection_query_statement (cursor->conn,
	                                                       cursor->query,
	                                                       NULL, &error);
	if (error)
		goto fail;

	apply_statement_parameters (statement, names, values);
	cursor->sparql_cursor = tracker_sparql_statement_execute (statement,
	                                                          NULL,
	                                                          &error);
	g_object_unref (statement);

	if (error)
		goto fail;

	cursor->finished =
		!tracker_sparql_cursor_next (cursor->sparql_cursor, NULL, &error);

	if (error)
		goto fail;

	g_free (uri_scheme);

	return SQLITE_OK;

fail:
	g_clear_pointer (&names, g_hash_table_unref);
	g_clear_pointer (&values, g_hash_table_unref);
	g_free (uri_scheme);

	if (cursor->silent) {
		cursor->finished = TRUE;
		g_error_free (error);
		return SQLITE_OK;
	} else {
		tracker_service_cursor_set_vtab_error (cursor, error->message);
		g_error_free (error);
		return SQLITE_ERROR;
	}
}

static int
service_next (sqlite3_vtab_cursor *vtab_cursor)
{
	TrackerServiceCursor *cursor = (TrackerServiceCursor *) vtab_cursor;

	if (!cursor->sparql_cursor)
		return SQLITE_ERROR;

	cursor->finished =
		!tracker_sparql_cursor_next (cursor->sparql_cursor, NULL, NULL);

	cursor->rowid++;
	return SQLITE_OK;
}

static int
service_eof (sqlite3_vtab_cursor *vtab_cursor)
{
	TrackerServiceCursor *cursor = (TrackerServiceCursor *) vtab_cursor;

	return cursor->finished;
}

static void
cursor_column_to_result (TrackerSparqlCursor *cursor,
                         gint                 column,
                         sqlite3_context     *context)
{
	const gchar *str;

	if (column >= tracker_sparql_cursor_get_n_columns (cursor)) {
		sqlite3_result_null (context);
		return;
	}

	switch (tracker_sparql_cursor_get_value_type (cursor, column)) {
	case TRACKER_SPARQL_VALUE_TYPE_URI:
	case TRACKER_SPARQL_VALUE_TYPE_STRING:
	case TRACKER_SPARQL_VALUE_TYPE_DATETIME:
	case TRACKER_SPARQL_VALUE_TYPE_BLANK_NODE:
		str = tracker_sparql_cursor_get_string (cursor, column, NULL);
		sqlite3_result_text (context, g_strdup (str), -1, g_free);
		break;
	case TRACKER_SPARQL_VALUE_TYPE_INTEGER:
	case TRACKER_SPARQL_VALUE_TYPE_BOOLEAN:
		sqlite3_result_int64 (context,
		                      tracker_sparql_cursor_get_integer (cursor, column));
		break;
	case TRACKER_SPARQL_VALUE_TYPE_DOUBLE:
		sqlite3_result_double (context,
		                       tracker_sparql_cursor_get_double (cursor, column));
		break;
	case TRACKER_SPARQL_VALUE_TYPE_UNBOUND:
	default:
		sqlite3_result_null (context);
	}
}

static int
service_column (sqlite3_vtab_cursor *vtab_cursor,
		sqlite3_context     *context,
		int                  n_col)
{
	TrackerServiceCursor *cursor = (TrackerServiceCursor *) vtab_cursor;

	if (n_col == COL_SERVICE) {
		sqlite3_result_text (context, cursor->service, -1, NULL);
	} else if (n_col == COL_QUERY) {
		sqlite3_result_text (context, cursor->query, -1, NULL);
	} else if (n_col == COL_SILENT) {
		sqlite3_result_int (context, cursor->silent);
	} else if (n_col >= COL_FIRST_PARAMETER &&
	           n_col < COL_FIRST_VARIABLE) {
		sqlite3_value *value = NULL;

		if (cursor->parameter_columns)
			value = g_hash_table_lookup (cursor->parameter_columns,
			                             GINT_TO_POINTER (n_col));

		if (value)
			sqlite3_result_value (context, value);
		else
			sqlite3_result_null (context);
	} else if (n_col >= COL_FIRST_VARIABLE &&
	           n_col < COL_FIRST_VARIABLE + N_VARIABLES) {
		cursor_column_to_result (cursor->sparql_cursor,
		                         n_col - COL_FIRST_VARIABLE,
		                         context);
	} else {
		sqlite3_result_null (context);
	}

	return SQLITE_OK;
}

static int
service_rowid (sqlite3_vtab_cursor *vtab_cursor,
	       sqlite_int64        *rowid_out)
{
	TrackerServiceCursor *cursor = (TrackerServiceCursor *) vtab_cursor;

	*rowid_out = cursor->rowid;
	return SQLITE_OK;
}

void
tracker_vtab_service_init (sqlite3           *db,
			   TrackerOntologies *ontologies)
{
	TrackerServiceModule *module;
	static const sqlite3_module service_module = {
		2, /* version */
		service_create,
		service_create,
		service_best_index,
		service_destroy,
		service_destroy,
		service_open,
		service_close,
		service_filter,
		service_next,
		service_eof,
		service_column,
		service_rowid,
		NULL, /* update */
		NULL, /* begin */
		NULL, /* sync */
		NULL, /* commit */
		NULL, /* rollback */
		NULL, /* find function */
		NULL, /* rename */
		NULL, /* savepoint */
		NULL, /* release */
		NULL, /* rollback to */
	};

	module = g_new0 (TrackerServiceModule, 1);
	module->db = db;
	sqlite3_create_module_v2 (db, "tracker_service", &service_module,
	                          module, tracker_service_module_free);
}
