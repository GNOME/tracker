/*
 * Copyright (C) 2014 Carlos Garnacho  <carlosg@gnome.org>
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

#include "tracker-decorator.h"
#include "tracker-priority-queue.h"
#include "tracker-decorator-private.h"

#define QUERY_BATCH_SIZE 100
#define DEFAULT_BATCH_SIZE 200

/**
 * SECTION:tracker-decorator
 * @short_description: A miner tasked with listening for DB resource changes and extracting metadata
 * @include: libtracker-miner/tracker-miner.h
 * @title: TrackerDecorator
 * @see_also: #TrackerDecoratorFS
 *
 * #TrackerDecorator watches for signal updates based on content changes
 * in the database. When new files are added initially, only simple
 * metadata exists, for example, name, size, mtime, etc. The
 * #TrackerDecorator queues files for extended metadata extraction
 * (i.e. for tracker-extract to fetch metadata specific to the file
 * type) for example 'nmm:whiteBalance' for a picture.
**/

typedef struct _TrackerDecoratorPrivate TrackerDecoratorPrivate;
typedef struct _SparqlUpdate SparqlUpdate;
typedef struct _ClassInfo ClassInfo;

struct _TrackerDecoratorInfo {
	GTask *task;
	gchar *urn;
	gchar *url;
	gchar *mimetype;
	gint id;
	gint ref_count;
};

struct _ClassInfo {
	gchar *class_name;
	gint priority;
};

struct _SparqlUpdate {
	gchar *sparql;
	gint id;
};

struct _TrackerDecoratorPrivate {
	TrackerNotifier *notifier;
	gchar *data_source;

	GArray *classes; /* Array of ClassInfo */
	gchar **class_names;

	gssize n_remaining_items;
	gssize n_processed_items;

	GQueue item_cache; /* Queue of TrackerDecoratorInfo */

	/* Arrays of tracker IDs */
	GArray *prepended_ids;
	GSequence *blocklist_items;

	GHashTable *tasks; /* Associative array of GTasks */
	GArray *sparql_buffer; /* Array of SparqlUpdate */
	GArray *commit_buffer; /* Array of SparqlUpdate */
	GTimer *timer;
	GQueue next_elem_queue; /* Queue of incoming tasks */

	GCancellable *cancellable;

	gint batch_size;

	guint processing : 1;
	guint querying   : 1;
};

enum {
	PROP_DATA_SOURCE = 1,
	PROP_CLASS_NAMES,
	PROP_COMMIT_BATCH_SIZE,
	PROP_PRIORITY_RDF_TYPES,
};

enum {
	ITEMS_AVAILABLE,
	FINISHED,
	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };
static GInitableIface *parent_initable_iface;

static void   tracker_decorator_initable_iface_init (GInitableIface   *iface);

static void decorator_task_done (GObject      *object,
                                 GAsyncResult *result,
                                 gpointer      user_data);
static void decorator_cache_next_items (TrackerDecorator *decorator);
static gboolean decorator_check_commit (TrackerDecorator *decorator);

static void notifier_events_cb (TrackerDecorator *decorator,
				GPtrArray        *events,
				TrackerNotifier  *notifier);

/**
 * tracker_decorator_error_quark:
 *
 * Gives the caller the #GQuark used to identify #TrackerDecorator errors
 * in #GError structures. The #GQuark is used as the domain for the error.
 *
 * Returns: the #GQuark used for the domain of a #GError.
 *
 * Since: 0.18
 **/
G_DEFINE_QUARK (TrackerDecoratorError, tracker_decorator_error)

G_DEFINE_ABSTRACT_TYPE_WITH_CODE (TrackerDecorator, tracker_decorator, TRACKER_TYPE_MINER,
                                  G_ADD_PRIVATE (TrackerDecorator)
                                  G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE, tracker_decorator_initable_iface_init))

static TrackerDecoratorInfo *
tracker_decorator_info_new (TrackerDecorator    *decorator,
                            TrackerSparqlCursor *cursor)
{
	TrackerSparqlBuilder *sparql;
	TrackerDecoratorInfo *info;
	GCancellable *cancellable;

	info = g_slice_new0 (TrackerDecoratorInfo);
	info->urn = g_strdup (tracker_sparql_cursor_get_string (cursor, 0, NULL));
	info->id = tracker_sparql_cursor_get_integer (cursor, 1);
	info->url = g_strdup (tracker_sparql_cursor_get_string (cursor, 2, NULL));
	info->mimetype = g_strdup (tracker_sparql_cursor_get_string (cursor, 3, NULL));
	info->ref_count = 1;

	cancellable = g_cancellable_new ();
	info->task = g_task_new (decorator, cancellable,
	                         decorator_task_done, info);
	g_object_unref (cancellable);

	sparql = tracker_sparql_builder_new_update ();
	g_task_set_task_data (info->task, sparql,
	                      (GDestroyNotify) g_object_unref);

	return info;
}

/**
 * tracker_decorator_info_ref:
 * @info: a #TrackerDecoratorInfo
 *
 * Increases the reference count of @info by 1.
 *
 * Returns: the same @info passed in, or %NULL on error.
 *
 * Since: 0.18
 **/
TrackerDecoratorInfo *
tracker_decorator_info_ref (TrackerDecoratorInfo *info)
{
	g_atomic_int_inc (&info->ref_count);
	return info;
}

/**
 * tracker_decorator_info_unref:
 * @info: a #TrackerDecoratorInfo
 *
 * Decreases the reference count of @info by 1 and frees it when the
 * reference count reaches 0.
 *
 * Since: 0.18
 **/
void
tracker_decorator_info_unref (TrackerDecoratorInfo *info)
{
	if (!g_atomic_int_dec_and_test (&info->ref_count))
		return;

	if (info->task)
		g_object_unref (info->task);
	g_free (info->urn);
	g_free (info->url);
	g_free (info->mimetype);
	g_slice_free (TrackerDecoratorInfo, info);
}

G_DEFINE_BOXED_TYPE (TrackerDecoratorInfo,
                     tracker_decorator_info,
                     tracker_decorator_info_ref,
                     tracker_decorator_info_unref)

static gint
sequence_compare_func (gconstpointer data1,
                       gconstpointer data2,
                       gpointer      user_data)
{
	return GPOINTER_TO_INT (data1) - GPOINTER_TO_INT (data2);
}

static void
decorator_blocklist_add (TrackerDecorator *decorator,
                         gint              id)
{
	TrackerDecoratorPrivate *priv = decorator->priv;
	GSequenceIter *iter;

	iter = g_sequence_search (priv->blocklist_items,
	                          GINT_TO_POINTER (id),
	                          sequence_compare_func,
	                          NULL);

	if (g_sequence_iter_is_end (iter) ||
	    g_sequence_get (g_sequence_iter_prev (iter)) != GINT_TO_POINTER (id))
		g_sequence_insert_before (iter, GINT_TO_POINTER (id));
}

static void
decorator_blocklist_remove (TrackerDecorator *decorator,
                            gint              id)
{
	TrackerDecoratorPrivate *priv = decorator->priv;
	GSequenceIter *iter;

	iter = g_sequence_lookup (priv->blocklist_items,
	                          GINT_TO_POINTER (id),
	                          sequence_compare_func,
	                          NULL);
	if (iter)
		g_sequence_remove (iter);
}

static void
decorator_update_state (TrackerDecorator *decorator,
                        const gchar      *message,
                        gboolean          estimate_time)
{
	TrackerDecoratorPrivate *priv;
	gint remaining_time = -1;
	gdouble progress = 1;
	gsize total_items;

	priv = decorator->priv;
	remaining_time = 0;
	total_items = priv->n_remaining_items + priv->n_processed_items;

	if (priv->n_remaining_items > 0)
		progress = ((gdouble) priv->n_processed_items / total_items);

	if (priv->timer && estimate_time &&
	    !tracker_miner_is_paused (TRACKER_MINER (decorator))) {
		gdouble elapsed;

		/* FIXME: Quite naive calculation */
		elapsed = g_timer_elapsed (priv->timer, NULL);

		if (priv->n_processed_items > 0)
			remaining_time = (priv->n_remaining_items * elapsed) / priv->n_processed_items;
	}

	g_object_set (decorator,
	              "progress", progress,
	              "remaining-time", remaining_time,
	              NULL);

	if (message)
		g_object_set (decorator, "status", message, NULL);
}

static void
item_warn (TrackerSparqlConnection *conn,
           gint                     id,
           const gchar             *sparql,
           const GError            *error)
{
	TrackerSparqlCursor *cursor;
	const gchar *elem;
	gchar *query;

	query = g_strdup_printf ("SELECT COALESCE (nie:url (?u), ?u) {"
	                         "  ?u a rdfs:Resource. "
	                         "  FILTER (tracker:id (?u) = %d)"
	                         "}", id);

	cursor = tracker_sparql_connection_query (conn, query, NULL, NULL);
	g_free (query);

	g_debug ("--8<------------------------------");
	g_debug ("The information relevant for a bug report is between "
	         "the dotted lines");

	if (cursor &&
	    tracker_sparql_cursor_next (cursor, NULL, NULL)) {
		elem = tracker_sparql_cursor_get_string (cursor, 0, NULL);
		g_warning ("Could not insert metadata for item \"%s\": %s",
		           elem, error->message);
	} else {
		g_warning ("Could not insert metadata for item with ID %d: %s",
		           id, error->message);
	}

	g_warning ("If the error above is recurrent for the same item/ID, "
	           "consider running \"%s\" in the terminal with the "
	           "TRACKER_VERBOSITY=3 environment variable, and filing a "
	           "bug with the additional information", g_get_prgname ());

	g_debug ("Sparql was:\n%s", sparql);
	g_debug ("NOTE: The information above may contain data you "
	         "consider sensitive. Feel free to edit it out, but please "
	         "keep it as unmodified as you possibly can.");
	g_debug ("------------------------------>8--");

	g_clear_object (&cursor);
}

static void
decorator_commit_cb (GObject      *object,
                     GAsyncResult *result,
                     gpointer      user_data)
{
	TrackerSparqlConnection *conn;
	TrackerDecoratorPrivate *priv;
	TrackerDecorator *decorator;
	GError *error = NULL;
	GPtrArray *errors;
	guint i;

	decorator = user_data;
	priv = decorator->priv;
	conn = TRACKER_SPARQL_CONNECTION (object);
	errors = tracker_sparql_connection_update_array_finish (conn, result, &error);

	if (error) {
		g_warning ("There was an error pushing metadata: %s\n", error->message);
	}

	if (errors) {
		for (i = 0; i < errors->len; i++) {
			SparqlUpdate *update;
			GError *child_error;

			child_error = g_ptr_array_index (errors, i);
			update = &g_array_index (priv->commit_buffer, SparqlUpdate, i);

			if (!child_error)
				continue;

			decorator_blocklist_add (decorator, update->id);
			item_warn (conn, update->id, update->sparql, child_error);
		}

		g_ptr_array_unref (errors);
	}

	g_clear_pointer (&priv->commit_buffer, g_array_unref);

	if (!decorator_check_commit (decorator))
		decorator_cache_next_items (decorator);
}

static void
sparql_update_clear (SparqlUpdate *update)
{
	g_free (update->sparql);
}

static GArray *
sparql_buffer_new (void)
{
	GArray *array;

	array = g_array_new (FALSE, FALSE, sizeof (SparqlUpdate));
	g_array_set_clear_func (array, (GDestroyNotify) sparql_update_clear);

	return array;
}

static gboolean
decorator_commit_info (TrackerDecorator *decorator)
{
	TrackerSparqlConnection *sparql_conn;
	TrackerDecoratorPrivate *priv;
	GPtrArray *array;
	gint i;

	priv = decorator->priv;

	if (!priv->sparql_buffer || priv->sparql_buffer->len == 0)
		return FALSE;

	if (priv->commit_buffer)
		return FALSE;

	/* Move sparql buffer to commit buffer */
	priv->commit_buffer = priv->sparql_buffer;
	priv->sparql_buffer = NULL;
	array = g_ptr_array_new ();

	for (i = 0; i < priv->commit_buffer->len; i++) {
		SparqlUpdate *update;

		update = &g_array_index (priv->commit_buffer, SparqlUpdate, i);
		g_ptr_array_add (array, update->sparql);
	}

	sparql_conn = tracker_miner_get_connection (TRACKER_MINER (decorator));
	tracker_sparql_connection_update_array_async (sparql_conn,
	                                              (gchar **) array->pdata,
	                                              array->len,
	                                              G_PRIORITY_DEFAULT,
						      priv->cancellable,
	                                              decorator_commit_cb,
	                                              decorator);

	decorator_update_state (decorator, NULL, TRUE);
	g_ptr_array_unref (array);
	return TRUE;
}

static gboolean
decorator_check_commit (TrackerDecorator *decorator)
{
	TrackerDecoratorPrivate *priv;

	priv = decorator->priv;

	if (!priv->sparql_buffer ||
	    (priv->n_remaining_items > 0 &&
	     priv->sparql_buffer->len < (guint) priv->batch_size))
		return FALSE;

	return decorator_commit_info (decorator);
}

static void
decorator_notify_task_error (TrackerDecorator *decorator,
                             GError           *error)
{
	TrackerDecoratorPrivate *priv = decorator->priv;
	GTask *task;

	while (!g_queue_is_empty (&priv->next_elem_queue)) {
		task = g_queue_pop_head (&priv->next_elem_queue);
		g_task_return_error (task, g_error_copy (error));
		g_object_unref (task);
	}
}

static void
decorator_notify_empty (TrackerDecorator *decorator)
{
	GError *error;

	error = g_error_new (tracker_decorator_error_quark (),
	                     TRACKER_DECORATOR_ERROR_EMPTY,
	                     "There are no items left");
	decorator_notify_task_error (decorator, error);
	g_error_free (error);
}

static void
decorator_start (TrackerDecorator *decorator)
{
	TrackerDecoratorPrivate *priv = decorator->priv;

	if (priv->processing)
		return;

	priv->processing = TRUE;
	g_signal_emit (decorator, signals[ITEMS_AVAILABLE], 0);
	decorator_update_state (decorator, "Extracting metadata", TRUE);
}

static void
decorator_finish (TrackerDecorator *decorator)
{
	TrackerDecoratorPrivate *priv = decorator->priv;

	priv->processing = FALSE;
	priv->n_remaining_items = priv->n_processed_items = 0;
	g_signal_emit (decorator, signals[FINISHED], 0);
	decorator_commit_info (decorator);
	decorator_notify_empty (decorator);
	decorator_update_state (decorator, "Idle", FALSE);
}

static void
decorator_rebuild_cache (TrackerDecorator *decorator)
{
	TrackerDecoratorPrivate *priv = decorator->priv;

	priv->n_remaining_items = 0;
	g_queue_foreach (&priv->item_cache,
	                 (GFunc) tracker_decorator_info_unref, NULL);
	g_queue_clear (&priv->item_cache);

        decorator_cache_next_items (decorator);
}

/* This function is called after the caller has completed the
 * GTask given on the TrackerDecoratorInfo, this definitely removes
 * the element being processed from queues.
 */
static void
decorator_task_done (GObject      *object,
                     GAsyncResult *result,
                     gpointer      user_data)
{
	TrackerDecorator *decorator = TRACKER_DECORATOR (object);
	TrackerDecoratorInfo *info = user_data;
	TrackerDecoratorPrivate *priv;
	GError *error = NULL;
	gchar *sparql;

	priv = decorator->priv;
	sparql = g_task_propagate_pointer (G_TASK (result), &error);

	if (!sparql) {
		/* Blocklist item */
		decorator_blocklist_add (decorator, info->id);

		if (error) {
			g_warning ("Task for '%s' finished with error: %s\n",
			           info->url, error->message);
			g_error_free (error);
		}
	} else {
		SparqlUpdate update;

		/* Add resulting sparql to buffer and check whether flushing */
		update.sparql = sparql;
		update.id = info->id;

		if (!priv->sparql_buffer)
			priv->sparql_buffer = sparql_buffer_new ();

		g_array_append_val (priv->sparql_buffer, update);
	}

	g_hash_table_remove (priv->tasks, result);

	if (priv->n_remaining_items > 0)
		priv->n_remaining_items--;
	priv->n_processed_items++;

	decorator_check_commit (decorator);

	if (priv->n_remaining_items == 0) {
		decorator_finish (decorator);
		decorator_rebuild_cache (decorator);
	} else if (g_queue_is_empty (&priv->item_cache) &&
	           g_hash_table_size (priv->tasks) == 0 &&
	           (!priv->sparql_buffer || !priv->commit_buffer)) {
		decorator_cache_next_items (decorator);
	}
}

static void
decorator_cancel_active_tasks (TrackerDecorator *decorator)
{
	TrackerDecoratorPrivate *priv = decorator->priv;
	GHashTableIter iter;
	GTask *task;

	g_hash_table_iter_init (&iter, priv->tasks);

	while (g_hash_table_iter_next (&iter, NULL, (gpointer*) &task)) {
		g_cancellable_cancel (g_task_get_cancellable (task));
	}

	g_hash_table_remove_all (priv->tasks);
}

static void
query_append_id (GString *string,
                 gint     id)
{
	if (string->len > 1 && string->str[string->len - 1] != '(')
		g_string_append_c (string, ',');

	g_string_append_printf (string, "%d", id);
}

static void
query_add_blocklisted_filter (TrackerDecorator *decorator,
                              GString          *query)
{
	TrackerDecoratorPrivate *priv = decorator->priv;
	GSequenceIter *iter;

	if (g_sequence_get_length (priv->blocklist_items) == 0)
		return;

	g_string_append (query, "&& tracker:id(?urn) NOT IN (");

	iter = g_sequence_get_begin_iter (priv->blocklist_items);

	while (!g_sequence_iter_is_end (iter)) {
		query_append_id (query, GPOINTER_TO_INT (g_sequence_get (iter)));
		iter = g_sequence_iter_next (iter);
	}

	g_string_append (query, ")");
}

static void
query_add_update_buffer_ids (GString *query,
                             GArray  *commit_buffer)
{
	SparqlUpdate *update;
	gint i;

	for (i = 0; i < commit_buffer->len; i++) {
		update = &g_array_index (commit_buffer, SparqlUpdate, i);
		query_append_id (query, update->id);
	}
}

static void
query_add_processing_filter (TrackerDecorator *decorator,
                             GString          *query)
{
	TrackerDecoratorPrivate *priv = decorator->priv;

	if ((!priv->sparql_buffer || priv->sparql_buffer->len == 0) &&
	    (!priv->commit_buffer || priv->commit_buffer->len == 0))
	    return;

	g_string_append (query, "&& tracker:id(?urn) NOT IN (");

	if (priv->sparql_buffer && priv->sparql_buffer->len > 0)
		query_add_update_buffer_ids (query, priv->sparql_buffer);
	if (priv->commit_buffer && priv->commit_buffer->len > 0)
		query_add_update_buffer_ids (query, priv->commit_buffer);

	g_string_append (query, ")");
}

static void
query_add_id_filter (GString  *query,
                     GArray   *ids)
{
	gint i;

	if (!ids || ids->len == 0)
		return;

	g_string_append (query, "&& tracker:id(?urn) IN (");

	for (i = 0; i < ids->len; i++) {
		if (i != 0)
			g_string_append (query, ",");

		g_string_append_printf (query, "%d",
		                        g_array_index (ids, gint, i));
	}

	g_string_append (query, ")");
}

static void
query_append_current_tasks_filter (TrackerDecorator *decorator,
				   GString          *query)
{
	TrackerDecoratorPrivate *priv = decorator->priv;
	GHashTableIter iter;
	gint i = 0, id;
	GTask *task;

	if (g_hash_table_size (priv->tasks) == 0)
		return;

	g_string_append (query, "&& tracker:id(?urn) NOT IN (");
	g_hash_table_iter_init (&iter, priv->tasks);

	while (g_hash_table_iter_next (&iter, NULL, (gpointer *) &task)) {
		if (i != 0)
			g_string_append (query, ",");

		id = GPOINTER_TO_INT (g_task_get_task_data (task));
		g_string_append_printf (query, "%d", id);
		i++;
	}

	g_string_append (query, ")");
}

static gchar *
create_query_string (TrackerDecorator  *decorator,
                     gchar            **select_clauses,
                     gboolean           for_prepended)
{
	TrackerDecoratorPrivate *priv = decorator->priv;
	ClassInfo *prev = NULL, *cur;
	GString *query;
	gint i;

	query = g_string_new ("SELECT ");

	for (i = 0; select_clauses[i]; i++) {
		g_string_append_printf (query, "%s ", select_clauses[i]);
	}

	g_string_append (query, "{ SELECT ?urn WHERE {");

	for (i = 0; i < priv->classes->len; i++) {
		cur = &g_array_index (priv->classes, ClassInfo, i);

		if (!prev || prev->priority != cur->priority) {
			if (prev)
				g_string_append (query, "))} UNION ");

			g_string_append_printf (query,
			                        "{ ?urn a rdfs:Resource;"
			                        "       a ?type ;"
			                        "       tracker:available true ."
			                        "  FILTER (! EXISTS { ?urn nie:dataSource <%s> } ",
			                        priv->data_source);

			query_add_blocklisted_filter (decorator, query);
			query_add_processing_filter (decorator, query);

			if (for_prepended && priv->prepended_ids->len > 0) {
				query_add_id_filter (query, priv->prepended_ids);
				g_array_set_size (priv->prepended_ids, 0);
			}

			query_append_current_tasks_filter (decorator, query);
			g_string_append (query, " && ?type IN (");
		} else {
			g_string_append (query, ",");
		}

		g_string_append_printf (query, "%s", cur->class_name);
		prev = cur;
	}

	g_string_append_printf (query, "))}} GROUP BY ?urn } LIMIT %d", QUERY_BATCH_SIZE);

	return g_string_free (query, FALSE);
}

static gchar *
create_remaining_items_query (TrackerDecorator *decorator)
{
	gchar *clauses[] = {
		"?urn",
		"tracker:id(?urn)",
		"nie:url(?urn)",
		"nie:mimeType(?urn)",
		NULL
	};

	return create_query_string (decorator, clauses, TRUE);
}

static void
decorator_query_remaining_items_cb (GObject      *object,
                                    GAsyncResult *result,
                                    gpointer      user_data)
{
	TrackerDecorator *decorator = user_data;
	TrackerDecoratorPrivate *priv;
	TrackerSparqlCursor *cursor;
	GError *error = NULL;

	cursor = tracker_sparql_connection_query_finish (TRACKER_SPARQL_CONNECTION (object),
							 result, &error);

	if (error || !tracker_sparql_cursor_next (cursor, NULL, &error)) {
		decorator_notify_task_error (decorator, error);
		g_error_free (error);
		return;
	}

	priv = decorator->priv;
	priv->querying = FALSE;

	priv->n_remaining_items = g_queue_get_length (&priv->item_cache) +
		tracker_sparql_cursor_get_integer (cursor, 0);
	g_object_unref (cursor);

	g_debug ("Found %" G_GSIZE_FORMAT " items to extract", priv->n_remaining_items);

	if (priv->n_remaining_items > 0)
		decorator_cache_next_items (decorator);
	else
		decorator_finish (decorator);
}

static void
decorator_query_remaining_items (TrackerDecorator *decorator)
{
	gchar *query, *clauses[] = { "COUNT(?urn)", NULL };
	TrackerSparqlConnection *sparql_conn;
	TrackerDecoratorPrivate *priv;

	priv = decorator->priv;
	query = create_query_string (decorator, clauses, FALSE);

	if (query) {
		sparql_conn = tracker_miner_get_connection (TRACKER_MINER (decorator));
		tracker_sparql_connection_query_async (sparql_conn, query,
						       priv->cancellable,
		                                       decorator_query_remaining_items_cb,
		                                       decorator);
		g_free (query);
	} else {
		decorator_notify_empty (decorator);
	}
}

static void
decorator_pair_tasks (TrackerDecorator *decorator)
{
	TrackerDecoratorPrivate *priv = decorator->priv;
	TrackerDecoratorInfo *info;
	GTask *task;

	while (!g_queue_is_empty (&priv->item_cache) &&
	       !g_queue_is_empty (&priv->next_elem_queue)) {
		info = g_queue_pop_head (&priv->item_cache);
		task = g_queue_pop_head (&priv->next_elem_queue);

		g_task_set_task_data (task, GINT_TO_POINTER (info->id), NULL);

		/* Pass ownership of info */
		g_task_return_pointer (task, info,
		                       (GDestroyNotify) tracker_decorator_info_unref);
		g_object_unref (task);

		/* Store the decorator-side task in the active task pool */
		g_hash_table_add (priv->tasks, info->task);
	}
}

static void
decorator_item_cache_remove (TrackerDecorator *decorator,
                             gint              id)
{
	TrackerDecoratorPrivate *priv = decorator->priv;
	GList *item;

	for (item = g_queue_peek_head_link (&priv->item_cache);
	     item; item = item->next) {
		TrackerDecoratorInfo *info = item->data;

		if (info->id != id)
			continue;

		g_queue_remove (&priv->item_cache, info);
		tracker_decorator_info_unref (info);
	}
}

static void
decorator_cache_items_cb (GObject      *object,
                          GAsyncResult *result,
                          gpointer      user_data)
{
	TrackerDecorator *decorator = user_data;
	TrackerDecoratorPrivate *priv;
	TrackerSparqlConnection *conn;
	TrackerSparqlCursor *cursor;
	TrackerDecoratorInfo *info;
	GError *error = NULL;

	conn = TRACKER_SPARQL_CONNECTION (object);
	cursor = tracker_sparql_connection_query_finish (conn, result, &error);
	priv = decorator->priv;
        priv->querying = FALSE;

	if (error) {
		decorator_notify_task_error (decorator, error);
		g_error_free (error);
	} else {
		while (tracker_sparql_cursor_next (cursor, NULL, NULL)) {
			info = tracker_decorator_info_new (decorator, cursor);
			g_queue_push_tail (&priv->item_cache, info);
		}
	}

	if (!g_queue_is_empty (&priv->item_cache) && !priv->processing) {
		decorator_start (decorator);
	} else if (g_queue_is_empty (&priv->item_cache) && priv->processing) {
		decorator_finish (decorator);
	}

	decorator_pair_tasks (decorator);
	g_object_unref (cursor);
}

static void
decorator_cache_next_items (TrackerDecorator *decorator)
{
	TrackerDecoratorPrivate *priv = decorator->priv;

	if (priv->querying ||
	    g_hash_table_size (priv->tasks) > 0 ||
	    !g_queue_is_empty (&priv->item_cache))
		return;

        priv->querying = TRUE;

	if (priv->n_remaining_items == 0) {
		decorator_query_remaining_items (decorator);
	} else {
		TrackerSparqlConnection *sparql_conn;
		gchar *query;

		sparql_conn = tracker_miner_get_connection (TRACKER_MINER (decorator));
		query = create_remaining_items_query (decorator);
		tracker_sparql_connection_query_async (sparql_conn, query,
						       priv->cancellable,
						       decorator_cache_items_cb,
						       decorator);
		g_free (query);
	}
}

static void
update_notifier (TrackerDecorator *decorator)
{
	TrackerDecoratorPrivate *priv = decorator->priv;

	g_clear_object (&priv->notifier);

	if (priv->class_names) {
		GError *error = NULL;

		priv->notifier = tracker_notifier_new ((const gchar * const *) priv->class_names,
						       TRACKER_NOTIFIER_FLAG_NOTIFY_UNEXTRACTED,
						       NULL, &error);

		if (error) {
			g_warning ("Could not create notifier: %s\n",
				   error->message);
			g_error_free (error);
		}

		g_signal_connect_swapped (priv->notifier, "events",
					  G_CALLBACK (notifier_events_cb),
					  decorator);
	}
}

static void
tracker_decorator_get_property (GObject    *object,
                                guint       param_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
	TrackerDecoratorPrivate *priv;

	priv = TRACKER_DECORATOR (object)->priv;

	switch (param_id) {
	case PROP_DATA_SOURCE:
		g_value_set_string (value, priv->data_source);
		break;
	case PROP_CLASS_NAMES:
		g_value_set_boxed (value, priv->class_names);
		break;
	case PROP_COMMIT_BATCH_SIZE:
		g_value_set_int (value, priv->batch_size);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
	}
}

static void
decorator_add_class (TrackerDecorator *decorator,
                     const gchar      *class)
{
	TrackerDecoratorPrivate *priv = decorator->priv;
	ClassInfo info;

	info.class_name = g_strdup (class);
	info.priority = G_PRIORITY_DEFAULT;
	g_array_append_val (priv->classes, info);
}

static void
decorator_set_classes (TrackerDecorator  *decorator,
                       const gchar      **classes)
{
	TrackerDecoratorPrivate *priv = decorator->priv;
	gint i;

	g_strfreev (priv->class_names);
	priv->class_names = g_strdupv ((gchar **) classes);

	if (priv->classes->len > 0) {
		g_array_remove_range (priv->classes, 0,
		                      priv->classes->len);
	}

	for (i = 0; classes[i]; i++) {
		decorator_add_class (decorator, classes[i]);
	}

	update_notifier (decorator);
}

static void
tracker_decorator_set_property (GObject      *object,
                                guint         param_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
	TrackerDecorator *decorator = TRACKER_DECORATOR (object);
	TrackerDecoratorPrivate *priv;

	priv = decorator->priv;

	switch (param_id) {
	case PROP_DATA_SOURCE:
		priv->data_source = g_value_dup_string (value);
		break;
	case PROP_CLASS_NAMES:
		decorator_set_classes (decorator, g_value_get_boxed (value));
		break;
	case PROP_COMMIT_BATCH_SIZE:
		priv->batch_size = g_value_get_int (value);
		break;
	case PROP_PRIORITY_RDF_TYPES:
		tracker_decorator_set_priority_rdf_types (decorator,
		                                          g_value_get_boxed (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
	}
}

static void
notifier_events_cb (TrackerDecorator *decorator,
		    GPtrArray       *events,
		    TrackerNotifier *notifier)
{
	gboolean check_added = FALSE;
	gint64 id;
	gint i;

	for (i = 0; i < events->len; i++) {
		TrackerNotifierEvent *event;

		event = g_ptr_array_index (events, i);
		id = tracker_notifier_event_get_id (event);

		switch (tracker_notifier_event_get_event_type (event)) {
		case TRACKER_NOTIFIER_EVENT_CREATE:
		case TRACKER_NOTIFIER_EVENT_UPDATE:
			/* Merely use this as a hint that there is something
			 * left to be processed.
			 */
			check_added = TRUE;
			break;
		case TRACKER_NOTIFIER_EVENT_DELETE:
			decorator_item_cache_remove (decorator, id);
			decorator_blocklist_remove (decorator, id);
			break;
		}
	}

	if (check_added)
		decorator_cache_next_items (decorator);
}

static gboolean
tracker_decorator_initable_init (GInitable     *initable,
                                 GCancellable  *cancellable,
                                 GError       **error)
{
	TrackerDecorator *decorator;

	if (!parent_initable_iface->init (initable, cancellable, error))
		return FALSE;

	decorator = TRACKER_DECORATOR (initable);

	if (g_cancellable_is_cancelled (cancellable))
		return FALSE;

	update_notifier (decorator);

	decorator_update_state (decorator, "Idle", FALSE);
	return TRUE;
}

static void
tracker_decorator_initable_iface_init (GInitableIface *iface)
{
	parent_initable_iface = g_type_interface_peek_parent (iface);
	iface->init = tracker_decorator_initable_init;
}

static void
tracker_decorator_constructed (GObject *object)
{
	TrackerDecoratorPrivate *priv;

	G_OBJECT_CLASS (tracker_decorator_parent_class)->constructed (object);

	priv = TRACKER_DECORATOR (object)->priv;
	g_assert (priv->data_source);
}

static void
tracker_decorator_finalize (GObject *object)
{
	TrackerDecoratorPrivate *priv;
	TrackerDecorator *decorator;

	decorator = TRACKER_DECORATOR (object);
	priv = decorator->priv;

	g_cancellable_cancel (priv->cancellable);
	g_clear_object (&priv->cancellable);

	g_clear_object (&priv->notifier);

	g_queue_foreach (&priv->item_cache,
	                 (GFunc) tracker_decorator_info_unref,
	                 NULL);
	g_queue_clear (&priv->item_cache);

	decorator_cancel_active_tasks (decorator);
	decorator_notify_empty (decorator);

	g_strfreev (priv->class_names);
	g_hash_table_destroy (priv->tasks);
	g_array_unref (priv->classes);
	g_array_unref (priv->prepended_ids);
	g_clear_pointer (&priv->sparql_buffer, g_array_unref);
	g_clear_pointer (&priv->commit_buffer, g_array_unref);
	g_sequence_free (priv->blocklist_items);
	g_free (priv->data_source);
	g_timer_destroy (priv->timer);

	G_OBJECT_CLASS (tracker_decorator_parent_class)->finalize (object);
}

static void
tracker_decorator_paused (TrackerMiner *miner)
{
	TrackerDecoratorPrivate *priv;

	decorator_cancel_active_tasks (TRACKER_DECORATOR (miner));
	priv = TRACKER_DECORATOR (miner)->priv;
	g_timer_stop (priv->timer);
}

static void
tracker_decorator_resumed (TrackerMiner *miner)
{
	TrackerDecoratorPrivate *priv;

	decorator_cache_next_items (TRACKER_DECORATOR (miner));
	priv = TRACKER_DECORATOR (miner)->priv;
	g_timer_continue (priv->timer);
}

static void
tracker_decorator_stopped (TrackerMiner *miner)
{
	TrackerDecoratorPrivate *priv;

	decorator_cancel_active_tasks (TRACKER_DECORATOR (miner));
	priv = TRACKER_DECORATOR (miner)->priv;
	g_timer_stop (priv->timer);
}

static void
tracker_decorator_started (TrackerMiner *miner)
{
	TrackerDecoratorPrivate *priv;
	TrackerDecorator *decorator;

	decorator = TRACKER_DECORATOR (miner);
	priv = decorator->priv;

	g_timer_start (priv->timer);
	decorator_rebuild_cache (decorator);
}

static void
tracker_decorator_class_init (TrackerDecoratorClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	TrackerMinerClass *miner_class = TRACKER_MINER_CLASS (klass);

	object_class->get_property = tracker_decorator_get_property;
	object_class->set_property = tracker_decorator_set_property;
	object_class->constructed = tracker_decorator_constructed;
	object_class->finalize = tracker_decorator_finalize;

	miner_class->paused = tracker_decorator_paused;
	miner_class->resumed = tracker_decorator_resumed;
	miner_class->started = tracker_decorator_started;
	miner_class->stopped = tracker_decorator_stopped;

	g_object_class_install_property (object_class,
	                                 PROP_DATA_SOURCE,
	                                 g_param_spec_string ("data-source",
	                                                      "Data source URN",
	                                                      "nie:DataSource to use in this decorator",
	                                                      NULL,
	                                                      G_PARAM_READWRITE |
	                                                      G_PARAM_CONSTRUCT_ONLY));
	g_object_class_install_property (object_class,
	                                 PROP_CLASS_NAMES,
	                                 g_param_spec_boxed ("class-names",
	                                                     "Class names",
	                                                     "rdfs:Class objects to listen to for changes",
	                                                     G_TYPE_STRV,
	                                                     G_PARAM_READWRITE));
	g_object_class_install_property (object_class,
	                                 PROP_COMMIT_BATCH_SIZE,
	                                 g_param_spec_int ("commit-batch-size",
	                                                   "Commit batch size",
	                                                   "Number of items per update batch",
	                                                   0, G_MAXINT, DEFAULT_BATCH_SIZE,
	                                                   G_PARAM_READWRITE));
	g_object_class_install_property (object_class,
	                                 PROP_PRIORITY_RDF_TYPES,
	                                 g_param_spec_boxed ("priority-rdf-types",
	                                                     "Priority RDF types",
	                                                     "rdf:type that needs to be extracted first",
	                                                     G_TYPE_STRV,
	                                                     G_PARAM_WRITABLE));
	/**
	 * TrackerDecorator::items-available:
	 * @decorator: the #TrackerDecorator
	 *
	 * The ::items-available signal will be emitted whenever the
	 * #TrackerDecorator sees resources that are available for
	 * extended metadata extraction.
	 *
	 * Since: 0.18
	 **/
	signals[ITEMS_AVAILABLE] =
		g_signal_new ("items-available",
		              G_OBJECT_CLASS_TYPE (object_class),
		              G_SIGNAL_RUN_LAST,
		              G_STRUCT_OFFSET (TrackerDecoratorClass,
		                               items_available),
		              NULL, NULL, NULL,
		              G_TYPE_NONE, 0);
	/**
	 * TrackerDecorator::finished:
	 * @decorator: the #TrackerDecorator
	 *
	 * The ::finished signal will be emitted whenever the
	 * #TrackerDecorator has finished extracted extended metadata
	 * for resources in the database.
	 *
	 * Since: 0.18
	 **/
	signals[FINISHED] =
		g_signal_new ("finished",
		              G_OBJECT_CLASS_TYPE (object_class),
		              G_SIGNAL_RUN_LAST,
		              G_STRUCT_OFFSET (TrackerDecoratorClass, finished),
		              NULL, NULL, NULL,
		              G_TYPE_NONE, 0);
}

static void
class_info_clear (ClassInfo *info)
{
	g_free (info->class_name);
}

static void
tracker_decorator_init (TrackerDecorator *decorator)
{
	TrackerDecoratorPrivate *priv;

	decorator->priv = priv = tracker_decorator_get_instance_private (decorator);
	priv->classes = g_array_new (FALSE, FALSE, sizeof (ClassInfo));
	g_array_set_clear_func (priv->classes, (GDestroyNotify) class_info_clear);
	priv->blocklist_items = g_sequence_new (NULL);
	priv->prepended_ids = g_array_new (FALSE, FALSE, sizeof (gint));
	priv->batch_size = DEFAULT_BATCH_SIZE;
	priv->timer = g_timer_new ();
	priv->cancellable = g_cancellable_new ();

	g_queue_init (&priv->next_elem_queue);
	g_queue_init (&priv->item_cache);
	priv->tasks = g_hash_table_new (NULL, NULL);
}

/**
 * tracker_decorator_get_data_source:
 * @decorator: a #TrackerDecorator.
 *
 * The unique string identifying this #TrackerDecorator that has
 * extracted the extended metadata. This is essentially an identifier
 * so it's clear WHO has extracted this extended metadata.
 *
 * Returns: a const gchar* or #NULL if an error happened.
 *
 * Since: 0.18
 **/
const gchar *
tracker_decorator_get_data_source (TrackerDecorator *decorator)
{
	TrackerDecoratorPrivate *priv;

	g_return_val_if_fail (TRACKER_IS_DECORATOR (decorator), NULL);

	priv = decorator->priv;
	return priv->data_source;
}

/**
 * tracker_decorator_get_class_names:
 * @decorator: a #TrackerDecorator.
 *
 * This function returns a string list of class names which are being
 * updated with extended metadata. An example would be 'nfo:Document'.
 *
 * Returns: (transfer none): a const gchar** or #NULL.
 *
 * Since: 0.18
 **/
const gchar **
tracker_decorator_get_class_names (TrackerDecorator *decorator)
{
	TrackerDecoratorPrivate *priv;

	g_return_val_if_fail (TRACKER_IS_DECORATOR (decorator), NULL);

	priv = decorator->priv;
	return (const gchar **) priv->class_names;
}

/**
 * tracker_decorator_get_n_items:
 * @decorator: a #TrackerDecorator
 *
 * Get the number of items left in the queue to be processed. This
 * indicates content that may already exist in Tracker but is waiting
 * to be further flurished with metadata with a 2nd pass extraction or
 * index.
 *
 * Returns: the number of items queued to be processed, always >= 0.
 *
 * Since: 0.18
 **/
guint
tracker_decorator_get_n_items (TrackerDecorator *decorator)
{
	TrackerDecoratorPrivate *priv;

	g_return_val_if_fail (TRACKER_IS_DECORATOR (decorator), 0);

	priv = decorator->priv;

	return priv->n_remaining_items;
}

/**
 * tracker_decorator_prepend_id:
 * @decorator: a #TrackerDecorator.
 * @id: the ID of the resource ID.
 * @class_name_id: the ID of the resource's class.
 *
 * Adds resource needing extended metadata extraction to the queue.
 * @id is the same IDs emitted by tracker-store when the database is updated for
 * consistency. For details, see the GraphUpdated signal.
 *
 * Since: 0.18
 **/
void
tracker_decorator_prepend_id (TrackerDecorator *decorator,
                              gint              id,
                              gint              class_name_id)
{
	TrackerDecoratorPrivate *priv;

	g_return_if_fail (TRACKER_IS_DECORATOR (decorator));

	priv = decorator->priv;
	g_array_append_val (priv->prepended_ids, id);

	/* The resource was explicitly requested, remove it from blocklists */
	decorator_blocklist_remove (decorator, id);
}

/**
 * tracker_decorator_delete_id:
 * @decorator: a #TrackerDecorator.
 * @id: an ID.
 *
 * Deletes resource needing extended metadata extraction from the
 * queue. @id is the same IDs emitted by tracker-store when the database is
 * updated for consistency. For details, see the GraphUpdated signal.
 *
 * Since: 0.18
 **/
void
tracker_decorator_delete_id (TrackerDecorator *decorator,
                             gint              id)
{
	TrackerDecoratorPrivate *priv;
	guint i;

	g_return_if_fail (TRACKER_IS_DECORATOR (decorator));

	priv = decorator->priv;

	for (i = 0; i < priv->prepended_ids->len; i++) {
		if (id == g_array_index (priv->prepended_ids, gint, i)) {
			g_array_remove_index (priv->prepended_ids, i);
			break;
		}
	}

	/* Blocklist the item so it's not processed in the future */
	decorator_blocklist_add (decorator, id);
}

/**
 * tracker_decorator_next:
 * @decorator: a #TrackerDecorator.
 * @cancellable: a #GCancellable.
 * @callback: a #GAsyncReadyCallback.
 * @user_data: user_data for @callback.
 *
 * Processes the next resource in the queue to have extended metadata
 * extracted. If the item in the queue has been completed already, it
 * signals it's completion instead.
 *
 * This function will give a #GError if the miner is paused at the
 * time it is called.
 *
 * Since: 0.18
 **/
void
tracker_decorator_next (TrackerDecorator    *decorator,
                        GCancellable        *cancellable,
                        GAsyncReadyCallback  callback,
                        gpointer             user_data)
{
	TrackerDecoratorPrivate *priv;
	GTask *task;

	g_return_if_fail (TRACKER_IS_DECORATOR (decorator));

	priv = decorator->priv;

	task = g_task_new (decorator, cancellable, callback, user_data);

	if (tracker_miner_is_paused (TRACKER_MINER (decorator))) {
		GError *error;

		error = g_error_new (tracker_decorator_error_quark (),
		                     TRACKER_DECORATOR_ERROR_PAUSED,
		                     "Decorator is paused");
		g_task_return_error (task, error);
		g_object_unref (task);
		return;
	}

	g_queue_push_tail (&priv->next_elem_queue, task);
	decorator_pair_tasks (decorator);
}

/**
 * tracker_decorator_next_finish:
 * @decorator: a #TrackerDecorator.
 * @result: a #GAsyncResult.
 * @error: return location for a #GError, or NULL.
 *
 * Should be called in the callback function provided to
 * tracker_decorator_next() to return the result of the task be it an
 * error or not.
 *
 * Returns: (transfer full): a #TrackerDecoratorInfo on success or
 *  #NULL on error. Free with tracker_decorator_info_unref().
 *
 * Since: 0.18
 **/
TrackerDecoratorInfo *
tracker_decorator_next_finish (TrackerDecorator  *decorator,
                               GAsyncResult      *result,
                               GError           **error)
{
	g_return_val_if_fail (TRACKER_DECORATOR (decorator), NULL);
	g_return_val_if_fail (G_IS_ASYNC_RESULT (result), NULL);
	g_return_val_if_fail (!error || !*error, NULL);

	return g_task_propagate_pointer (G_TASK (result), error);
}

static gint
class_compare_func (const ClassInfo *a,
                    const ClassInfo *b)
{
	return b->priority - a->priority;
}

static void
decorator_set_class_priority (TrackerDecorator *decorator,
                              const gchar      *class,
                              gint              priority)
{
	TrackerDecoratorPrivate *priv = decorator->priv;
	ClassInfo *info;
	gint i;

	for (i = 0; i < priv->classes->len; i++) {
		info = &g_array_index (priv->classes, ClassInfo, i);

		if (strcmp (info->class_name, class) != 0)
			continue;

		info->priority = priority;
		break;
	}
}

/**
 * tracker_decorator_set_priority_rdf_types:
 * @decorator: a #TrackerDecorator
 * @rdf_types: a string array of rdf types
 *
 * Re-evaluate the priority queues internally to ensure that
 * @rdf_types are handled before all other content. This is useful for
 * applications that need their content available sooner than the
 * standard time it would take to index content.
 *
 * Since: 0.18
 **/
void
tracker_decorator_set_priority_rdf_types (TrackerDecorator    *decorator,
                                          const gchar * const *rdf_types)
{
	TrackerDecoratorPrivate *priv;
	gint i;

	g_return_if_fail (TRACKER_DECORATOR (decorator));
	g_return_if_fail (rdf_types != NULL);

	priv = decorator->priv;

	for (i = 0; rdf_types[i]; i++) {
		decorator_set_class_priority (decorator, rdf_types[i],
		                              G_PRIORITY_HIGH);
	}

	g_array_sort (priv->classes, (GCompareFunc) class_compare_func);
	decorator_rebuild_cache (decorator);
}

/**
 * tracker_decorator_info_get_urn:
 * @info: a #TrackerDecoratorInfo.
 *
 * A URN is a Uniform Resource Name and should be a unique identifier
 * for a resource in the database.
 *
 * Returns: the URN for #TrackerDecoratorInfo on success or #NULL on error.
 *
 * Since: 0.18
 **/
const gchar *
tracker_decorator_info_get_urn (TrackerDecoratorInfo *info)
{
	g_return_val_if_fail (info != NULL, NULL);
	return info->urn;
}

/**
 * tracker_decorator_info_get_url:
 * @info: a #TrackerDecoratorInfo.
 *
 * A URL is a Uniform Resource Locator and should be a location associated
 * with a resource in the database. For example, 'file:///tmp/foo.txt'.
 *
 * Returns: the URL for #TrackerDecoratorInfo on success or #NULL on error.
 *
 * Since: 0.18
 **/
const gchar *
tracker_decorator_info_get_url (TrackerDecoratorInfo *info)
{
	g_return_val_if_fail (info != NULL, NULL);
	return info->url;
}

/**
 * tracker_decorator_info_get_mimetype:
 * @info: a #TrackerDecoratorInfo.
 *
 * A MIME¹ type is a way of describing the content type of a file or
 * set of data. An example would be 'text/plain' for a clear text
 * document or file.
 *
 * ¹: http://en.wikipedia.org/wiki/MIME
 *
 * Returns: the MIME type for #TrackerDecoratorInfo on success or #NULL on error.
 *
 * Since: 0.18
 **/
const gchar *
tracker_decorator_info_get_mimetype (TrackerDecoratorInfo *info)
{
       g_return_val_if_fail (info != NULL, NULL);
       return info->mimetype;
}


/**
 * tracker_decorator_info_get_task:
 * @info: a #TrackerDecoratorInfo.
 *
 * Get the #GTask associated with retrieving extended metadata and
 * information for a URN in Tracker.
 *
 * The task object's data (accessible with g_task_get_task_data()) is the
 * #TrackerSparqlBuilder. Use tracker_decorator_info_complete() to complete
 * the task instead of using this object.
 *
 * Returns: (transfer none): the #GTask for #TrackerDecoratorInfo on
 * success or #NULL if there is no existing #GTask.
 *
 * Since: 0.18
 **/
GTask *
tracker_decorator_info_get_task (TrackerDecoratorInfo *info)
{
	g_return_val_if_fail (info != NULL, NULL);
	return info->task;
}

/**
 * tracker_decorator_info_complete:
 * @info: a #TrackerDecoratorInfo
 * @sparql: (transfer full): SPARQL string
 *
 * Completes the task associated to this #TrackerDecoratorInfo.
 * Takes ownership of @sparql.
 *
 * Since: 2.0
 **/
void
tracker_decorator_info_complete (TrackerDecoratorInfo *info,
                                 gchar                *sparql)
{
	g_task_return_pointer (info->task, sparql, g_free);
}

/**
 * tracker_decorator_info_complete_error:
 * @info: a #TrackerDecoratorInfo
 * @error: (transfer full): An error occurred during SPARQL generation
 *
 * Completes the task associated to this #TrackerDecoratorInfo,
 * returning the given @error happened during SPARQL generation.
 *
 * Since: 2.0
 **/
void
tracker_decorator_info_complete_error (TrackerDecoratorInfo *info,
                                       GError               *error)
{
	g_task_return_error (info->task, error);
}

void
_tracker_decorator_invalidate_cache (TrackerDecorator *decorator)
{
	decorator_rebuild_cache (decorator);
}
