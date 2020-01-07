/*
 * Copyright (C) 2010, Nokia <ivan.frade@nokia.com>
 * Copyright (C) 2017, Red Hat, Inc.
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

#include "tracker-direct.h"
#include "tracker-direct-statement.h"
#include "libtracker-sparql/tracker-private.h"
#include <libtracker-data/tracker-data.h>
#include <libtracker-data/tracker-sparql.h>
#include <libtracker-sparql/tracker-notifier-private.h>

typedef struct _TrackerDirectConnectionPrivate TrackerDirectConnectionPrivate;

struct _TrackerDirectConnectionPrivate
{
	TrackerSparqlConnectionFlags flags;
	GFile *store;
	GFile *ontology;

	TrackerNamespaceManager *namespace_manager;
	TrackerDataManager *data_manager;
	GMutex mutex;

	GThreadPool *update_thread; /* Contains 1 exclusive thread */
	GThreadPool *select_pool;

	GList *notifiers;

	guint initialized : 1;
};

enum {
	PROP_0,
	PROP_FLAGS,
	PROP_STORE_LOCATION,
	PROP_ONTOLOGY_LOCATION,
	N_PROPS
};

static GParamSpec *props[N_PROPS] = { NULL };

typedef enum {
	TASK_TYPE_QUERY,
	TASK_TYPE_UPDATE,
	TASK_TYPE_UPDATE_BLANK,
} TaskType;

typedef struct {
	TaskType type;
	gchar *query;
} TaskData;

static void tracker_direct_connection_initable_iface_init (GInitableIface *iface);
static void tracker_direct_connection_async_initable_iface_init (GAsyncInitableIface *iface);

G_DEFINE_QUARK (TrackerDirectNotifier, tracker_direct_notifier)

G_DEFINE_TYPE_WITH_CODE (TrackerDirectConnection, tracker_direct_connection,
                         TRACKER_TYPE_SPARQL_CONNECTION,
                         G_ADD_PRIVATE (TrackerDirectConnection)
                         G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE,
                                                tracker_direct_connection_initable_iface_init)
                         G_IMPLEMENT_INTERFACE (G_TYPE_ASYNC_INITABLE,
                                                tracker_direct_connection_async_initable_iface_init))

static TaskData *
task_data_query_new (TaskType     type,
                     const gchar *sparql)
{
	TaskData *data;

	data = g_new0 (TaskData, 1);
	data->type = type;
	data->query = g_strdup (sparql);

	return data;
}

static void
task_data_free (TaskData *task)
{
	g_free (task->query);
	g_free (task);
}

static void
update_thread_func (gpointer data,
                    gpointer user_data)
{
	TrackerDirectConnectionPrivate *priv;
	TrackerDirectConnection *conn;
	GTask *task = data;
	TaskData *task_data = g_task_get_task_data (task);
	TrackerData *tracker_data;
	GError *error = NULL;
	gpointer retval = NULL;
	GDestroyNotify destroy_notify = NULL;

	conn = user_data;
	priv = tracker_direct_connection_get_instance_private (conn);

	g_mutex_lock (&priv->mutex);
	tracker_data = tracker_data_manager_get_data (priv->data_manager);

	switch (task_data->type) {
	case TASK_TYPE_QUERY:
		g_warning ("Queries don't go through this thread");
		break;
	case TASK_TYPE_UPDATE:
		tracker_data_update_sparql (tracker_data, task_data->query, &error);
		break;
	case TASK_TYPE_UPDATE_BLANK:
		retval = tracker_data_update_sparql_blank (tracker_data, task_data->query, &error);
		destroy_notify = (GDestroyNotify) g_variant_unref;
		break;
	}

	if (error)
		g_task_return_error (task, error);
	else if (retval)
		g_task_return_pointer (task, retval, destroy_notify);
	else
		g_task_return_boolean (task, TRUE);

	g_object_unref (task);
	g_mutex_unlock (&priv->mutex);
}

static void
query_thread_pool_func (gpointer data,
                        gpointer user_data)
{
	TrackerSparqlCursor *cursor;
	GTask *task = data;
	TaskData *task_data = g_task_get_task_data (task);
	GError *error = NULL;

	g_assert (task_data->type == TASK_TYPE_QUERY);
	cursor = tracker_sparql_connection_query (TRACKER_SPARQL_CONNECTION (g_task_get_source_object (task)),
	                                          task_data->query,
	                                          g_task_get_cancellable (task),
	                                          &error);
	if (cursor)
		g_task_return_pointer (task, cursor, g_object_unref);
	else
		g_task_return_error (task, error);

	g_object_unref (task);
}

static gint
task_compare_func (GTask    *a,
                   GTask    *b,
                   gpointer  user_data)
{
	return g_task_get_priority (b) - g_task_get_priority (a);
}

static gboolean
set_up_thread_pools (TrackerDirectConnection  *conn,
		     GError                  **error)
{
	TrackerDirectConnectionPrivate *priv;

	priv = tracker_direct_connection_get_instance_private (conn);

	priv->select_pool = g_thread_pool_new (query_thread_pool_func,
	                                       conn, 16, FALSE, error);
	if (!priv->select_pool)
		return FALSE;

	priv->update_thread = g_thread_pool_new (update_thread_func,
	                                         conn, 1, TRUE, error);
	if (!priv->update_thread)
		return FALSE;

	g_thread_pool_set_sort_function (priv->select_pool,
	                                 (GCompareDataFunc) task_compare_func,
	                                 conn);
	g_thread_pool_set_sort_function (priv->update_thread,
	                                 (GCompareDataFunc) task_compare_func,
	                                 conn);
	return TRUE;
}

static gboolean
tracker_direct_connection_initable_init (GInitable     *initable,
                                         GCancellable  *cancellable,
                                         GError       **error)
{
	TrackerDirectConnectionPrivate *priv;
	TrackerDirectConnection *conn;
	TrackerDBManagerFlags db_flags = TRACKER_DB_MANAGER_ENABLE_MUTEXES;
	GHashTable *namespaces;
	GHashTableIter iter;
	gchar *prefix, *ns;

	conn = TRACKER_DIRECT_CONNECTION (initable);
	priv = tracker_direct_connection_get_instance_private (conn);

	tracker_locale_sanity_check ();

	if (!set_up_thread_pools (conn, error))
		return FALSE;

	/* Init data manager */
	if (priv->flags & TRACKER_SPARQL_CONNECTION_FLAGS_READONLY) {
		db_flags |= TRACKER_DB_MANAGER_READONLY;
	} else if (!priv->ontology) {
		gchar *filename;

		/* If the connection is read/write, and no ontology is specified,
		 * we use the Nepomuk one.
		 */
		filename = g_build_filename (SHAREDIR, "tracker", "ontologies",
		                             "nepomuk", NULL);
		priv->ontology = g_file_new_for_path (filename);
		g_free (filename);
	}

	priv->data_manager = tracker_data_manager_new (db_flags, priv->store,
	                                               priv->ontology,
	                                               FALSE, 100, 100);
	if (!g_initable_init (G_INITABLE (priv->data_manager), cancellable, error)) {
		g_clear_object (&priv->data_manager);
		return FALSE;
	}

	/* Initialize namespace manager */
	priv->namespace_manager = tracker_namespace_manager_new ();
	namespaces = tracker_data_manager_get_namespaces (priv->data_manager);
	g_hash_table_iter_init (&iter, namespaces);

	while (g_hash_table_iter_next (&iter, (gpointer*) &prefix, (gpointer*) &ns)) {
		tracker_namespace_manager_add_prefix (priv->namespace_manager,
		                                      prefix, ns);
	}

	g_hash_table_unref (namespaces);

	return TRUE;
}

static void
tracker_direct_connection_initable_iface_init (GInitableIface *iface)
{
	iface->init = tracker_direct_connection_initable_init;
}

static void
async_initable_thread_func (GTask        *task,
                            gpointer      source_object,
                            gpointer      task_data,
                            GCancellable *cancellable)
{
	GError *error = NULL;

	if (!g_initable_init (G_INITABLE (source_object), cancellable, &error))
		g_task_return_error (task, error);
	else
		g_task_return_boolean (task, TRUE);
}

static void
tracker_direct_connection_async_initable_init_async (GAsyncInitable      *async_initable,
                                                     gint                 priority,
                                                     GCancellable        *cancellable,
                                                     GAsyncReadyCallback  callback,
                                                     gpointer             user_data)
{
	GTask *task;

	task = g_task_new (async_initable, cancellable, callback, user_data);
	g_task_set_priority (task, priority);
	g_task_run_in_thread (task, async_initable_thread_func);
}

static gboolean
tracker_direct_connection_async_initable_init_finish (GAsyncInitable  *async_initable,
                                                      GAsyncResult    *res,
                                                      GError         **error)
{
	return g_task_propagate_boolean (G_TASK (res), error);
}

static void
tracker_direct_connection_async_initable_iface_init (GAsyncInitableIface *iface)
{
	iface->init_async = tracker_direct_connection_async_initable_init_async;
	iface->init_finish = tracker_direct_connection_async_initable_init_finish;
}

static void
tracker_direct_connection_init (TrackerDirectConnection *conn)
{
}

static GHashTable *
get_event_cache_ht (TrackerNotifier *notifier)
{
	GHashTable *events;

	events = g_object_get_qdata (G_OBJECT (notifier), tracker_direct_notifier_quark ());
	if (!events) {
		events = g_hash_table_new_full (NULL, NULL, NULL,
		                                (GDestroyNotify) _tracker_notifier_event_cache_free);
		g_object_set_qdata_full (G_OBJECT (notifier), tracker_direct_notifier_quark (),
		                         events, (GDestroyNotify) g_hash_table_unref);
	}

	return events;
}

static TrackerNotifierEventCache *
lookup_event_cache (TrackerNotifier *notifier,
                    gint             graph_id,
                    const gchar     *graph)
{
	TrackerNotifierEventCache *cache;
	GHashTable *events;

	events = get_event_cache_ht (notifier);
	cache = g_hash_table_lookup (events, GINT_TO_POINTER (graph_id));

	if (!cache) {
		cache = _tracker_notifier_event_cache_new (notifier, NULL, graph);
		g_hash_table_insert (events, GINT_TO_POINTER (graph_id), cache);
	}

	return cache;
}

/* These callbacks will be called from a different thread
 * (always the same one though), handle with care.
 */
static void
insert_statement_cb (gint         graph_id,
                     const gchar *graph,
                     gint         subject_id,
                     const gchar *subject,
                     gint         predicate_id,
                     gint         object_id,
                     const gchar *object,
                     GPtrArray   *rdf_types,
                     gpointer     user_data)
{
	TrackerNotifier *notifier = user_data;
	TrackerSparqlConnection *conn = _tracker_notifier_get_connection (notifier);
	TrackerDirectConnection *direct = TRACKER_DIRECT_CONNECTION (conn);
	TrackerDirectConnectionPrivate *priv = tracker_direct_connection_get_instance_private (direct);
	TrackerOntologies *ontologies = tracker_data_manager_get_ontologies (priv->data_manager);
	TrackerProperty *rdf_type = tracker_ontologies_get_rdf_type (ontologies);
	TrackerNotifierEventCache *cache;
	TrackerClass *new_class = NULL;
	gint i;

	cache = lookup_event_cache (notifier, graph_id, graph);

	if (predicate_id == tracker_property_get_id (rdf_type)) {
		const gchar *uri;

		uri = tracker_ontologies_get_uri_by_id (ontologies, predicate_id);
		new_class = tracker_ontologies_get_class_by_uri (ontologies, uri);
	}

	for (i = 0; i < rdf_types->len; i++) {
		TrackerClass *class = g_ptr_array_index (rdf_types, i);
		TrackerNotifierEventType event_type;

		if (!tracker_class_get_notify (class))
			continue;

		if (class == new_class)
			event_type = TRACKER_NOTIFIER_EVENT_CREATE;
		else
			event_type = TRACKER_NOTIFIER_EVENT_UPDATE;

		_tracker_notifier_event_cache_push_event (cache, subject_id, event_type);
	}
}

static void
delete_statement_cb (gint         graph_id,
                     const gchar *graph,
                     gint         subject_id,
                     const gchar *subject,
                     gint         predicate_id,
                     gint         object_id,
                     const gchar *object,
                     GPtrArray   *rdf_types,
                     gpointer     user_data)
{
	TrackerNotifier *notifier = user_data;
	TrackerSparqlConnection *conn = _tracker_notifier_get_connection (notifier);
	TrackerDirectConnection *direct = TRACKER_DIRECT_CONNECTION (conn);
	TrackerDirectConnectionPrivate *priv = tracker_direct_connection_get_instance_private (direct);
	TrackerOntologies *ontologies = tracker_data_manager_get_ontologies (priv->data_manager);
	TrackerProperty *rdf_type = tracker_ontologies_get_rdf_type (ontologies);
	TrackerNotifierEventCache *cache;
	TrackerClass *new_class = NULL;
	gint i;

	cache = lookup_event_cache (notifier, graph_id, graph);

	if (predicate_id == tracker_property_get_id (rdf_type)) {
		const gchar *uri;

		uri = tracker_ontologies_get_uri_by_id (ontologies, predicate_id);
		new_class = tracker_ontologies_get_class_by_uri (ontologies, uri);
	}

	for (i = 0; i < rdf_types->len; i++) {
		TrackerClass *class = g_ptr_array_index (rdf_types, i);
		TrackerNotifierEventType event_type;

		if (!tracker_class_get_notify (class))
			continue;

		if (class == new_class)
			event_type = TRACKER_NOTIFIER_EVENT_DELETE;
		else
			event_type = TRACKER_NOTIFIER_EVENT_UPDATE;

		_tracker_notifier_event_cache_push_event (cache, subject_id, event_type);
	}
}

static void
commit_statement_cb (gpointer user_data)
{
	TrackerNotifierEventCache *cache;
	TrackerNotifier *notifier = user_data;
	GHashTable *events;
	GHashTableIter iter;

	events = get_event_cache_ht (notifier);
	g_hash_table_iter_init (&iter, events);

	while (g_hash_table_iter_next (&iter, NULL, (gpointer *) &cache)) {
		g_hash_table_iter_steal (&iter);
		_tracker_notifier_event_cache_flush_events (cache);
		_tracker_notifier_event_cache_free (cache);
	}
}

static void
rollback_statement_cb (gpointer user_data)
{
	TrackerNotifier *notifier = user_data;
	GHashTable *events;

	events = get_event_cache_ht (notifier);
	g_hash_table_remove_all (events);
}

static void
detach_notifier (TrackerDirectConnection *conn,
                 TrackerNotifier         *notifier)
{
	TrackerDirectConnectionPrivate *priv;
	TrackerData *tracker_data;

	priv = tracker_direct_connection_get_instance_private (conn);

	priv->notifiers = g_list_remove (priv->notifiers, notifier);

	tracker_data = tracker_data_manager_get_data (priv->data_manager);
	tracker_data_remove_insert_statement_callback (tracker_data,
	                                               insert_statement_cb,
	                                               notifier);
	tracker_data_remove_delete_statement_callback (tracker_data,
	                                               delete_statement_cb,
	                                               notifier);
	tracker_data_remove_commit_statement_callback (tracker_data,
	                                               commit_statement_cb,
	                                               notifier);
	tracker_data_remove_rollback_statement_callback (tracker_data,
	                                                 rollback_statement_cb,
	                                                 notifier);
}

static void
weak_ref_notify (gpointer  data,
                 GObject  *prev_location)
{
	TrackerDirectConnection *conn = data;

	detach_notifier (conn, (TrackerNotifier *) prev_location);
}

static void
tracker_direct_connection_dispose (GObject *object)
{
	TrackerDirectConnectionPrivate *priv;
	TrackerDirectConnection *conn;

	conn = TRACKER_DIRECT_CONNECTION (object);
	priv = tracker_direct_connection_get_instance_private (conn);

	if (priv->update_thread) {
		g_thread_pool_free (priv->update_thread, TRUE, TRUE);
		priv->update_thread = NULL;
	}

	if (priv->select_pool) {
		g_thread_pool_free (priv->select_pool, TRUE, FALSE);
		priv->select_pool = NULL;
	}

	if (priv->data_manager) {
		tracker_data_manager_shutdown (priv->data_manager);
		g_clear_object (&priv->data_manager);
	}

	G_OBJECT_CLASS (tracker_direct_connection_parent_class)->dispose (object);
}

static void
tracker_direct_connection_finalize (GObject *object)
{
	TrackerDirectConnectionPrivate *priv;
	TrackerDirectConnection *conn;

	conn = TRACKER_DIRECT_CONNECTION (object);
	priv = tracker_direct_connection_get_instance_private (conn);

	while (priv->notifiers) {
		TrackerNotifier *notifier = priv->notifiers->data;

		g_object_weak_unref (G_OBJECT (notifier),
		                     weak_ref_notify,
		                     conn);
		detach_notifier (conn, notifier);
	}

	g_clear_object (&priv->store);
	g_clear_object (&priv->ontology);
	g_clear_object (&priv->namespace_manager);

	G_OBJECT_CLASS (tracker_direct_connection_parent_class)->finalize (object);
}

static void
tracker_direct_connection_set_property (GObject      *object,
                                        guint         prop_id,
                                        const GValue *value,
                                        GParamSpec   *pspec)
{
	TrackerDirectConnectionPrivate *priv;
	TrackerDirectConnection *conn;

	conn = TRACKER_DIRECT_CONNECTION (object);
	priv = tracker_direct_connection_get_instance_private (conn);

	switch (prop_id) {
	case PROP_FLAGS:
		priv->flags = g_value_get_flags (value);
		break;
	case PROP_STORE_LOCATION:
		priv->store = g_value_dup_object (value);
		break;
	case PROP_ONTOLOGY_LOCATION:
		priv->ontology = g_value_dup_object (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
tracker_direct_connection_get_property (GObject    *object,
                                        guint       prop_id,
                                        GValue     *value,
                                        GParamSpec *pspec)
{
	TrackerDirectConnectionPrivate *priv;
	TrackerDirectConnection *conn;

	conn = TRACKER_DIRECT_CONNECTION (object);
	priv = tracker_direct_connection_get_instance_private (conn);

	switch (prop_id) {
	case PROP_FLAGS:
		g_value_set_flags (value, priv->flags);
		break;
	case PROP_STORE_LOCATION:
		g_value_set_object (value, priv->store);
		break;
	case PROP_ONTOLOGY_LOCATION:
		g_value_set_object (value, priv->ontology);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static TrackerSparqlCursor *
tracker_direct_connection_query (TrackerSparqlConnection  *self,
                                 const gchar              *sparql,
                                 GCancellable             *cancellable,
                                 GError                  **error)
{
	TrackerDirectConnectionPrivate *priv;
	TrackerDirectConnection *conn;
	TrackerSparql *query;
	TrackerSparqlCursor *cursor;

	conn = TRACKER_DIRECT_CONNECTION (self);
	priv = tracker_direct_connection_get_instance_private (conn);

	g_mutex_lock (&priv->mutex);
	query = tracker_sparql_new (priv->data_manager, sparql);
	cursor = tracker_sparql_execute_cursor (query, NULL, error);
	g_object_unref (query);

	if (cursor)
		tracker_sparql_cursor_set_connection (cursor, self);
	g_mutex_unlock (&priv->mutex);

	return cursor;
}

static void
tracker_direct_connection_query_async (TrackerSparqlConnection *self,
                                       const gchar             *sparql,
                                       GCancellable            *cancellable,
                                       GAsyncReadyCallback      callback,
                                       gpointer                 user_data)
{
	TrackerDirectConnectionPrivate *priv;
	TrackerDirectConnection *conn;
	GError *error = NULL;
	GTask *task;

	conn = TRACKER_DIRECT_CONNECTION (self);
	priv = tracker_direct_connection_get_instance_private (conn);

	task = g_task_new (self, cancellable, callback, user_data);
	g_task_set_task_data (task,
	                      task_data_query_new (TASK_TYPE_QUERY, sparql),
	                      (GDestroyNotify) task_data_free);

	if (!g_thread_pool_push (priv->select_pool, task, &error))
		g_task_return_error (task, error);
}

static TrackerSparqlCursor *
tracker_direct_connection_query_finish (TrackerSparqlConnection  *self,
                                        GAsyncResult             *res,
                                        GError                  **error)
{
	return g_task_propagate_pointer (G_TASK (res), error);
}

static TrackerSparqlStatement *
tracker_direct_connection_query_statement (TrackerSparqlConnection  *self,
                                           const gchar              *query,
                                           GCancellable             *cancellable,
                                           GError                  **error)
{
	return TRACKER_SPARQL_STATEMENT (tracker_direct_statement_new (self, query, error));
}

static void
tracker_direct_connection_update (TrackerSparqlConnection  *self,
                                  const gchar              *sparql,
                                  gint                      priority,
                                  GCancellable             *cancellable,
                                  GError                  **error)
{
	TrackerDirectConnectionPrivate *priv;
	TrackerDirectConnection *conn;
	TrackerData *data;

	conn = TRACKER_DIRECT_CONNECTION (self);
	priv = tracker_direct_connection_get_instance_private (conn);

	g_mutex_lock (&priv->mutex);
	data = tracker_data_manager_get_data (priv->data_manager);
	tracker_data_update_sparql (data, sparql, error);
	g_mutex_unlock (&priv->mutex);
}

static void
tracker_direct_connection_update_async (TrackerSparqlConnection *self,
                                        const gchar             *sparql,
                                        gint                     priority,
                                        GCancellable            *cancellable,
                                        GAsyncReadyCallback      callback,
                                        gpointer                 user_data)
{
	TrackerDirectConnectionPrivate *priv;
	TrackerDirectConnection *conn;
	GTask *task;

	conn = TRACKER_DIRECT_CONNECTION (self);
	priv = tracker_direct_connection_get_instance_private (conn);

	task = g_task_new (self, cancellable, callback, user_data);
	g_task_set_priority (task, priority);
	g_task_set_task_data (task,
	                      task_data_query_new (TASK_TYPE_UPDATE, sparql),
	                      (GDestroyNotify) task_data_free);

	g_thread_pool_push (priv->update_thread, task, NULL);
}

static void
tracker_direct_connection_update_finish (TrackerSparqlConnection  *self,
                                         GAsyncResult             *res,
                                         GError                  **error)
{
	g_task_propagate_boolean (G_TASK (res), error);
}

static void
error_free (GError *error)
{
	if (error)
		g_error_free (error);
}

static void
update_array_async_thread_func (GTask        *task,
                                gpointer      source_object,
                                gpointer      task_data,
                                GCancellable *cancellable)
{
	gchar **updates = task_data;
	gchar *concatenated;
	GPtrArray *errors;
	GError *error = NULL;

	errors = g_ptr_array_new_with_free_func ((GDestroyNotify) error_free);
	g_ptr_array_set_size (errors, g_strv_length (updates));

	concatenated = g_strjoinv ("\n", updates);
	tracker_sparql_connection_update (source_object, concatenated,
	                                  g_task_get_priority (task),
	                                  cancellable, &error);
	g_free (concatenated);

	if (error)
		g_task_return_error (task, error);
	else
		g_task_return_boolean (task, TRUE);

	g_object_unref (task);
}

static void
tracker_direct_connection_update_array_async (TrackerSparqlConnection  *self,
                                              gchar                   **updates,
                                              gint                      n_updates,
                                              gint                      priority,
                                              GCancellable             *cancellable,
                                              GAsyncReadyCallback       callback,
                                              gpointer                  user_data)
{
	GTask *task;
	gchar **copy;
	gint i = 0;

	copy = g_new0 (gchar*, n_updates + 1);

	for (i = 0; i < n_updates; i++) {
		g_return_if_fail (updates[i] != NULL);
		copy[i] = g_strdup (updates[i]);
	}

	task = g_task_new (self, cancellable, callback, user_data);
	g_task_set_priority (task, priority);
	g_task_set_task_data (task, copy, (GDestroyNotify) g_strfreev);

	g_task_run_in_thread (task, update_array_async_thread_func);
}

static gboolean
tracker_direct_connection_update_array_finish (TrackerSparqlConnection  *self,
                                               GAsyncResult             *res,
                                               GError                  **error)
{
	return g_task_propagate_boolean (G_TASK (res), error);
}

static GVariant *
tracker_direct_connection_update_blank (TrackerSparqlConnection  *self,
                                        const gchar              *sparql,
                                        gint                      priority,
                                        GCancellable             *cancellable,
                                        GError                  **error)
{
	TrackerDirectConnectionPrivate *priv;
	TrackerDirectConnection *conn;
	TrackerData *data;
	GVariant *blank_nodes;

	conn = TRACKER_DIRECT_CONNECTION (self);
	priv = tracker_direct_connection_get_instance_private (conn);

	g_mutex_lock (&priv->mutex);
	data = tracker_data_manager_get_data (priv->data_manager);
	blank_nodes = tracker_data_update_sparql_blank (data, sparql, error);
	g_mutex_unlock (&priv->mutex);

	return blank_nodes;
}

static void
tracker_direct_connection_update_blank_async (TrackerSparqlConnection *self,
                                              const gchar             *sparql,
                                              gint                     priority,
                                              GCancellable            *cancellable,
                                              GAsyncReadyCallback      callback,
                                              gpointer                 user_data)
{
	TrackerDirectConnectionPrivate *priv;
	TrackerDirectConnection *conn;
	GTask *task;

	conn = TRACKER_DIRECT_CONNECTION (self);
	priv = tracker_direct_connection_get_instance_private (conn);

	task = g_task_new (self, cancellable, callback, user_data);
	g_task_set_priority (task, priority);
	g_task_set_task_data (task,
	                      task_data_query_new (TASK_TYPE_UPDATE_BLANK, sparql),
	                      (GDestroyNotify) task_data_free);

	g_thread_pool_push (priv->update_thread, task, NULL);
}

static GVariant *
tracker_direct_connection_update_blank_finish (TrackerSparqlConnection  *self,
                                               GAsyncResult             *res,
                                               GError                  **error)
{
	return g_task_propagate_pointer (G_TASK (res), error);
}

static TrackerNamespaceManager *
tracker_direct_connection_get_namespace_manager (TrackerSparqlConnection *self)
{
	TrackerDirectConnectionPrivate *priv;

	priv = tracker_direct_connection_get_instance_private (TRACKER_DIRECT_CONNECTION (self));

	return priv->namespace_manager;
}

static TrackerNotifier *
tracker_direct_connection_create_notifier (TrackerSparqlConnection *self,
					   TrackerNotifierFlags     flags)
{
	TrackerDirectConnectionPrivate *priv;
	TrackerNotifier *notifier;
	TrackerData *tracker_data;

	priv = tracker_direct_connection_get_instance_private (TRACKER_DIRECT_CONNECTION (self));

	notifier = g_object_new (TRACKER_TYPE_NOTIFIER,
	                         "connection", self,
				 "flags", flags,
				 NULL);

	tracker_data = tracker_data_manager_get_data (priv->data_manager);
	tracker_data_add_insert_statement_callback (tracker_data,
						    insert_statement_cb,
						    notifier);
	tracker_data_add_delete_statement_callback (tracker_data,
						    delete_statement_cb,
						    notifier);
	tracker_data_add_commit_statement_callback (tracker_data,
						    commit_statement_cb,
						    notifier);
	tracker_data_add_rollback_statement_callback (tracker_data,
						      rollback_statement_cb,
						      notifier);

	g_object_weak_ref (G_OBJECT (notifier), weak_ref_notify, self);
	priv->notifiers = g_list_prepend (priv->notifiers, notifier);

	return notifier;
}

static void
tracker_direct_connection_close (TrackerSparqlConnection *self)
{
	g_object_run_dispose (G_OBJECT (self));
}

static void
tracker_direct_connection_class_init (TrackerDirectConnectionClass *klass)
{
	TrackerSparqlConnectionClass *sparql_connection_class;
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS (klass);
	sparql_connection_class = TRACKER_SPARQL_CONNECTION_CLASS (klass);

	object_class->finalize = tracker_direct_connection_finalize;
	object_class->dispose = tracker_direct_connection_dispose;
	object_class->set_property = tracker_direct_connection_set_property;
	object_class->get_property = tracker_direct_connection_get_property;

	sparql_connection_class->query = tracker_direct_connection_query;
	sparql_connection_class->query_async = tracker_direct_connection_query_async;
	sparql_connection_class->query_finish = tracker_direct_connection_query_finish;
	sparql_connection_class->query_statement = tracker_direct_connection_query_statement;
	sparql_connection_class->update = tracker_direct_connection_update;
	sparql_connection_class->update_async = tracker_direct_connection_update_async;
	sparql_connection_class->update_finish = tracker_direct_connection_update_finish;
	sparql_connection_class->update_array_async = tracker_direct_connection_update_array_async;
	sparql_connection_class->update_array_finish = tracker_direct_connection_update_array_finish;
	sparql_connection_class->update_blank = tracker_direct_connection_update_blank;
	sparql_connection_class->update_blank_async = tracker_direct_connection_update_blank_async;
	sparql_connection_class->update_blank_finish = tracker_direct_connection_update_blank_finish;
	sparql_connection_class->get_namespace_manager = tracker_direct_connection_get_namespace_manager;
	sparql_connection_class->create_notifier = tracker_direct_connection_create_notifier;
	sparql_connection_class->close = tracker_direct_connection_close;

	props[PROP_FLAGS] =
		g_param_spec_flags ("flags",
		                    "Flags",
		                    "Flags",
		                    TRACKER_TYPE_SPARQL_CONNECTION_FLAGS,
		                    TRACKER_SPARQL_CONNECTION_FLAGS_NONE,
		                    G_PARAM_READWRITE |
		                    G_PARAM_CONSTRUCT_ONLY);
	props[PROP_STORE_LOCATION] =
		g_param_spec_object ("store-location",
		                     "Store location",
		                     "Store location",
		                     G_TYPE_FILE,
		                     G_PARAM_READWRITE |
		                     G_PARAM_CONSTRUCT_ONLY);
	props[PROP_ONTOLOGY_LOCATION] =
		g_param_spec_object ("ontology-location",
		                     "Ontology location",
		                     "Ontology location",
		                     G_TYPE_FILE,
		                     G_PARAM_READWRITE |
		                     G_PARAM_CONSTRUCT_ONLY);

	g_object_class_install_properties (object_class, N_PROPS, props);
}

TrackerDirectConnection *
tracker_direct_connection_new (TrackerSparqlConnectionFlags   flags,
			       GFile                         *store,
                               GFile                         *ontology,
                               GError                       **error)
{
	g_return_val_if_fail (G_IS_FILE (store), NULL);
	g_return_val_if_fail (!ontology || G_IS_FILE (ontology), NULL);
	g_return_val_if_fail (!error || !*error, NULL);

	return g_object_new (TRACKER_TYPE_DIRECT_CONNECTION,
	                     "flags", flags,
	                     "store-location", store,
	                     "ontology-location", ontology,
	                     NULL);
}

TrackerDataManager *
tracker_direct_connection_get_data_manager (TrackerDirectConnection *conn)
{
	TrackerDirectConnectionPrivate *priv;

	priv = tracker_direct_connection_get_instance_private (conn);
	return priv->data_manager;
}
