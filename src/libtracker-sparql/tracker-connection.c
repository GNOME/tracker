/*
 * Copyright (C) 2010, Nokia <ivan.frade@nokia.com>
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

/**
 * SECTION: tracker-sparql-connection
 * @short_description: Connection to SPARQL triple store
 * @title: TrackerSparqlConnection
 * @stability: Stable
 * @include: tracker-sparql.h
 *
 * <para>
 * #TrackerSparqlConnection is an object that represents a connection to a
 * SPARQL triple store. This store may be local and private (see
 * tracker_sparql_connection_new()), or it may be a remote connection to a
 * public endpoint (See tracker_sparql_connection_bus_new() and
 * tracker_sparql_connection_remote_new()).
 * </para>
 *
 * <para>
 * A #TrackerSparqlConnection is private to the calling process, it can be
 * exposed publicly via a #TrackerEndpoint, see tracker_endpoint_dbus_new().
 * </para>
 *
 * <para>
 * Updates on a connection are performed via the tracker_sparql_connection_update()
 * family of calls. tracker_sparql_connection_update_array() may be used for batched
 * updates. All functions have asynchronous variants.
 * </para>
 *
 * <para>
 * Queries on a connection are performed via tracker_sparql_connection_query()
 * and tracker_sparql_connection_query_statement(). The first call receives a
 * query string and returns a #TrackerSparqlCursor to iterate the results. The
 * second call returns a #TrackerSparqlStatement object that may be reused for
 * repeatable queries with variable parameters. tracker_sparql_statement_execute()
 * will returns a #TrackerSparqlCursor.
 * </para>
 *
 * <para>
 * Depending on the ontology definition, #TrackerSparqlConnection may emit
 * notifications whenever changes happen in the stored data. These notifications
 * can be processed via a #TrackerNotifier obtained with
 * tracker_sparql_connection_create_notifier().
 * </para>
 *
 * <para>
 * After use, a #TrackerSparqlConnection should be closed. See
 * tracker_sparql_connection_close() and tracker_sparql_connection_close_async().
 * </para>
 */
#include "config.h"

#include "tracker-connection.h"
#include "tracker-private.h"

#include "direct/tracker-direct.h"

G_DEFINE_ABSTRACT_TYPE (TrackerSparqlConnection, tracker_sparql_connection,
                        G_TYPE_OBJECT)

static void
tracker_sparql_connection_init (TrackerSparqlConnection *connection)
{
}

static void
tracker_sparql_connection_dispose (GObject *object)
{
	tracker_sparql_connection_close (TRACKER_SPARQL_CONNECTION (object));

	G_OBJECT_CLASS (tracker_sparql_connection_parent_class)->dispose (object);
}

static void
tracker_sparql_connection_class_init (TrackerSparqlConnectionClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->dispose = tracker_sparql_connection_dispose;
}

gboolean
tracker_sparql_connection_lookup_dbus_service (TrackerSparqlConnection  *connection,
                                               const gchar              *dbus_name,
                                               const gchar              *dbus_path,
                                               gchar                   **name,
                                               gchar                   **path)
{
	TrackerSparqlConnectionClass *connection_class;

	g_return_val_if_fail (TRACKER_IS_SPARQL_CONNECTION (connection), FALSE);
	g_return_val_if_fail (dbus_name != NULL, FALSE);

	connection_class = TRACKER_SPARQL_CONNECTION_GET_CLASS (connection);
	if (!connection_class->lookup_dbus_service)
		return FALSE;

	return TRACKER_SPARQL_CONNECTION_GET_CLASS (connection)->lookup_dbus_service (connection,
	                                                                              dbus_name,
	                                                                              dbus_path,
	                                                                              name,
	                                                                              path);
}

/* The constructor functions are defined in the libtracker-sparql-backend, but
 * documented here. */

/**
 * tracker_sparql_connection_new:
 * @flags: values from #TrackerSparqlConnectionFlags
 * @store: (nullable): the directory that contains the database as a #GFile, or %NULL
 * @ontology: (nullable): the directory that contains the database schemas as a #GFile, or %NULL
 * @cancellable: (nullable): a #GCancellable, or %NULL
 * @error: pointer to a #GError
 *
 * Creates or opens a database.
 *
 * This method should only be used for databases owned by the current process.
 * To connect to databases managed by other processes, use
 * tracker_sparql_connection_bus_new().
 *
 * If @store is %NULL, the database will be created in memory.
 *
 * The @ontologies parameter must point to a location containing suitable
 * `.ontology` files in Turtle format. These control the database schema that
 * is used. You can use the default Nepomuk ontologies by calling
 * tracker_sparql_get_ontology_nepomuk ().
 *
 * If you open an existing database using a different @ontology to the one it
 * was created with, Tracker will attempt to migrate the existing data to the
 * new schema. This may raise an error. In particular, not all migrations are
 * possible without causing data loss and Tracker will refuse to delete data
 * during a migration.
 *
 * You can also pass %NULL for @ontologies to mean "use the ontologies that the
 * database was created with". This will fail if the database doesn't already
 * exist.
 *
 * Returns: (transfer full): a new #TrackerSparqlConnection. Call
 * g_object_unref() on the object when no longer used.
 *
 * Since: 3.0
 */

/**
 * tracker_sparql_connection_new_async:
 * @flags: values from #TrackerSparqlConnectionFlags
 * @store: (nullable): the directory that contains the database as a #GFile, or %NULL
 * @ontology: (nullable): the directory that contains the database schemas as a #GFile, or %NULL
 * @cancellable: (nullable): a #GCancellable, or %NULL
 * @callback: the #GAsyncReadyCallback called when the operation completes
 * @user_data: data passed to @callback
 *
 * Asynchronous version of tracker_sparql_connection_new().
 *
 * Since: 3.0
 */

/**
 * tracker_sparql_connection_new_finish:
 * @result: the #GAsyncResult
 * @error: pointer to a #GError
 *
 * Completion function for tracker_sparql_connection_new_async().
 *
 * Since: 3.0
 */

/**
 * tracker_sparql_connection_bus_new:
 * @service_name: The name of the D-Bus service to connect to.
 * @object_path: (nullable): The path to the object, or %NULL to use the default.
 * @dbus_connection: (nullable): The #GDBusConnection to use, or %NULL to use the session bus
 * @error: pointer to a #GError
 *
 * Connects to a database owned by another process on the
 * local machine.
 *
 * Returns: (transfer full): a new #TrackerSparqlConnection. Call g_object_unref() on the
 * object when no longer used.
 *
 * Since: 3.0
 */

/**
 * tracker_sparql_connection_bus_new_async:
 * @service_name: The name of the D-Bus service to connect to.
 * @object_path: (nullable): The path to the object, or %NULL to use the default.
 * @dbus_connection: (nullable): The #GDBusConnection to use, or %NULL to use the session bus
 * @cancellable: (nullable): a #GCancellable, or %NULL
 * @callback: the #GAsyncReadyCallback called when the operation completes
 * @user_data: data passed to @callback
 *
 * Connects to a database owned by another process on the
 * local machine. This is an asynchronous operation.
 *
 * Since: 3.1
 */

/**
 * tracker_sparql_connection_bus_new_finish:
 * @result: the #GAsyncResult
 * @error: pointer to a #GError
 *
 * Completion function for tracker_sparql_connection_bus_new_async().
 *
 * Returns: (transfer full): a new #TrackerSparqlConnection. Call g_object_unref() on the
 * object when no longer used.
 *
 * Since: 3.1
 */

/**
 * tracker_sparql_connection_remote_new:
 * @uri_base: Base URI of the remote connection
 *
 * Connects to a remote SPARQL endpoint. The connection is made using the libsoup
 * HTTP library. The connection will normally use the http:// or https:// protocol.
 *
 * Returns: (transfer full): a new remote #TrackerSparqlConnection. Call
 * g_object_unref() on the object when no longer used.
 */

/**
 * tracker_sparql_connection_query:
 * @connection: a #TrackerSparqlConnection
 * @sparql: string containing the SPARQL query
 * @cancellable: a #GCancellable used to cancel the operation
 * @error: #GError for error reporting.
 *
 * Executes a SPARQL query on. The API call is completely synchronous, so
 * it may block.
 *
 * The @sparql query should be built with #TrackerResource, or
 * its parts correctly escaped using tracker_sparql_escape_string(),
 * otherwise SPARQL injection is possible.
 *
 * Returns: (transfer full): a #TrackerSparqlCursor if results were found.
 * On error, #NULL is returned and the @error is set accordingly.
 * Call g_object_unref() on the returned cursor when no longer needed.
 */
TrackerSparqlCursor *
tracker_sparql_connection_query (TrackerSparqlConnection  *connection,
                                 const gchar              *sparql,
                                 GCancellable             *cancellable,
                                 GError                  **error)
{
	g_return_val_if_fail (TRACKER_IS_SPARQL_CONNECTION (connection), NULL);
	g_return_val_if_fail (sparql != NULL, NULL);
	g_return_val_if_fail (!cancellable || G_IS_CANCELLABLE (cancellable), NULL);
	g_return_val_if_fail (!error || !*error, NULL);

	return TRACKER_SPARQL_CONNECTION_GET_CLASS (connection)->query (connection,
	                                                                sparql,
	                                                                cancellable,
	                                                                error);
}

/**
 * tracker_sparql_connection_query_async:
 * @connection: a #TrackerSparqlConnection
 * @sparql: string containing the SPARQL query
 * @cancellable: a #GCancellable used to cancel the operation
 * @callback: user-defined #GAsyncReadyCallback to be called when
 *            asynchronous operation is finished.
 * @user_data: user-defined data to be passed to @callback
 *
 * Executes asynchronously a SPARQL query.
 */
void
tracker_sparql_connection_query_async (TrackerSparqlConnection *connection,
                                       const gchar             *sparql,
                                       GCancellable            *cancellable,
                                       GAsyncReadyCallback      callback,
                                       gpointer                 user_data)
{
	g_return_if_fail (TRACKER_IS_SPARQL_CONNECTION (connection));
	g_return_if_fail (sparql != NULL);
	g_return_if_fail (!cancellable || G_IS_CANCELLABLE (cancellable));

	TRACKER_SPARQL_CONNECTION_GET_CLASS (connection)->query_async (connection,
	                                                               sparql,
	                                                               cancellable,
	                                                               callback,
	                                                               user_data);
}

/**
 * tracker_sparql_connection_query_finish:
 * @connection: a #TrackerSparqlConnection
 * @res: a #GAsyncResult with the result of the operation
 * @error: #GError for error reporting.
 *
 * Finishes the asynchronous SPARQL query operation.
 *
 * Returns: (transfer full): a #TrackerSparqlCursor if results were found.
 * On error, #NULL is returned and the @error is set accordingly.
 * Call g_object_unref() on the returned cursor when no longer needed.
 */
TrackerSparqlCursor *
tracker_sparql_connection_query_finish (TrackerSparqlConnection  *connection,
                                        GAsyncResult             *res,
                                        GError                  **error)
{
	g_return_val_if_fail (TRACKER_IS_SPARQL_CONNECTION (connection), NULL);
	g_return_val_if_fail (G_IS_ASYNC_RESULT (res), NULL);
	g_return_val_if_fail (!error || !*error, NULL);

	return TRACKER_SPARQL_CONNECTION_GET_CLASS (connection)->query_finish (connection,
	                                                                       res,
	                                                                       error);
}

/**
 * tracker_sparql_connection_update:
 * @connection: a #TrackerSparqlConnection
 * @sparql: string containing the SPARQL update query
 * @cancellable: a #GCancellable used to cancel the operation
 * @error: #GError for error reporting.
 *
 * Executes a SPARQL update. The API call is completely
 * synchronous, so it may block.
 *
 * The @sparql query should be built with #TrackerResource, or
 * its parts correctly escaped using tracker_sparql_escape_string(),
 * otherwise SPARQL injection is possible.
 */
void
tracker_sparql_connection_update (TrackerSparqlConnection  *connection,
                                  const gchar              *sparql,
                                  GCancellable             *cancellable,
                                  GError                  **error)
{
	g_return_if_fail (TRACKER_IS_SPARQL_CONNECTION (connection));
	g_return_if_fail (sparql != NULL);
	g_return_if_fail (!cancellable || G_IS_CANCELLABLE (cancellable));
	g_return_if_fail (!error || !*error);

	return TRACKER_SPARQL_CONNECTION_GET_CLASS (connection)->update (connection,
	                                                                 sparql,
	                                                                 cancellable,
	                                                                 error);
}

/**
 * tracker_sparql_connection_update_async:
 * @connection: a #TrackerSparqlConnection
 * @sparql: string containing the SPARQL update query
 * @cancellable: a #GCancellable used to cancel the operation
 * @callback: user-defined #GAsyncReadyCallback to be called when
 *            asynchronous operation is finished.
 * @user_data: user-defined data to be passed to @callback
 *
 * Executes asynchronously a SPARQL update.
 */
void
tracker_sparql_connection_update_async (TrackerSparqlConnection *connection,
                                        const gchar             *sparql,
                                        GCancellable            *cancellable,
                                        GAsyncReadyCallback      callback,
                                        gpointer                 user_data)
{
	g_return_if_fail (TRACKER_IS_SPARQL_CONNECTION (connection));
	g_return_if_fail (sparql != NULL);
	g_return_if_fail (!cancellable || G_IS_CANCELLABLE (cancellable));

	TRACKER_SPARQL_CONNECTION_GET_CLASS (connection)->update_async (connection,
	                                                                sparql,
	                                                                cancellable,
	                                                                callback,
	                                                                user_data);
}

/**
 * tracker_sparql_connection_update_finish:
 * @connection: a #TrackerSparqlConnection
 * @res: a #GAsyncResult with the result of the operation
 * @error: #GError for error reporting.
 *
 * Finishes the asynchronous SPARQL update operation.
 */
void
tracker_sparql_connection_update_finish (TrackerSparqlConnection  *connection,
                                         GAsyncResult             *res,
                                         GError                  **error)
{
	g_return_if_fail (TRACKER_IS_SPARQL_CONNECTION (connection));
	g_return_if_fail (G_IS_ASYNC_RESULT (res));
	g_return_if_fail (!error || !*error);

	return TRACKER_SPARQL_CONNECTION_GET_CLASS (connection)->update_finish (connection,
	                                                                        res,
	                                                                        error);
}

/**
 * tracker_sparql_connection_update_array_async:
 * @connection: a #TrackerSparqlConnection
 * @sparql: an array of strings containing the SPARQL update queries
 * @sparql_length: the amount of strings you pass as @sparql
 * @cancellable: a #GCancellable used to cancel the operation
 * @callback: user-defined #GAsyncReadyCallback to be called when
 *            asynchronous operation is finished.
 * @user_data: user-defined data to be passed to @callback
 *
 * Executes asynchronously an array of SPARQL updates. All updates in the
 * array are handled within a single transaction.
 */
void
tracker_sparql_connection_update_array_async (TrackerSparqlConnection  *connection,
                                              gchar                   **sparql,
                                              gint                      sparql_length,
                                              GCancellable             *cancellable,
                                              GAsyncReadyCallback       callback,
                                              gpointer                  user_data)
{
	g_return_if_fail (TRACKER_IS_SPARQL_CONNECTION (connection));
	g_return_if_fail (sparql != NULL || sparql_length == 0);
	g_return_if_fail (!cancellable || G_IS_CANCELLABLE (cancellable));

	TRACKER_SPARQL_CONNECTION_GET_CLASS (connection)->update_array_async (connection,
	                                                                      sparql,
	                                                                      sparql_length,
	                                                                      cancellable,
	                                                                      callback,
	                                                                      user_data);
}

/**
 * tracker_sparql_connection_update_array_finish:
 * @connection: a #TrackerSparqlConnection
 * @res: a #GAsyncResult with the result of the operation
 * @error: #GError for error reporting.
 *
 * Finishes the asynchronous SPARQL update_array operation.
 *
 * Returns: #TRUE if there were no errors.
 */
gboolean
tracker_sparql_connection_update_array_finish (TrackerSparqlConnection  *connection,
                                               GAsyncResult             *res,
                                               GError                  **error)
{
	g_return_val_if_fail (TRACKER_IS_SPARQL_CONNECTION (connection), FALSE);
	g_return_val_if_fail (G_IS_ASYNC_RESULT (res), FALSE);
	g_return_val_if_fail (!error || !*error, FALSE);

	return TRACKER_SPARQL_CONNECTION_GET_CLASS (connection)->update_array_finish (connection,
	                                                                              res,
	                                                                              error);

}

/**
 * tracker_sparql_connection_update_blank:
 * @connection: a #TrackerSparqlConnection
 * @sparql: string containing the SPARQL update query
 * @cancellable: a #GCancellable used to cancel the operation
 * @error: #GError for error reporting.
 *
 * Executes a SPARQL update and returns the URNs of the generated nodes,
 * if any. The API call is completely synchronous, so it may block.
 *
 * The @sparql query should be built with #TrackerResource, or
 * its parts correctly escaped using tracker_sparql_escape_string(),
 * otherwise SPARQL injection is possible.
 *
 * Returns: a #GVariant with the generated URNs, which should be freed with
 * g_variant_unref() when no longer used.
 */
GVariant *
tracker_sparql_connection_update_blank (TrackerSparqlConnection  *connection,
                                        const gchar              *sparql,
                                        GCancellable             *cancellable,
                                        GError                  **error)
{
	g_return_val_if_fail (TRACKER_IS_SPARQL_CONNECTION (connection), NULL);
	g_return_val_if_fail (sparql != NULL, NULL);
	g_return_val_if_fail (!cancellable || G_IS_CANCELLABLE (cancellable), NULL);
	g_return_val_if_fail (!error || !*error, NULL);

	return TRACKER_SPARQL_CONNECTION_GET_CLASS (connection)->update_blank (connection,
	                                                                       sparql,
	                                                                       cancellable,
	                                                                       error);
}

/**
 * tracker_sparql_connection_update_blank_async:
 * @connection: a #TrackerSparqlConnection
 * @sparql: string containing the SPARQL update query
 * @cancellable: a #GCancellable used to cancel the operation
 * @callback: user-defined #GAsyncReadyCallback to be called when
 *            asynchronous operation is finished.
 * @user_data: user-defined data to be passed to @callback
 *
 * Executes asynchronously a SPARQL update.
 */
void
tracker_sparql_connection_update_blank_async (TrackerSparqlConnection *connection,
                                              const gchar             *sparql,
                                              GCancellable            *cancellable,
                                              GAsyncReadyCallback      callback,
                                              gpointer                 user_data)
{
	g_return_if_fail (TRACKER_IS_SPARQL_CONNECTION (connection));
	g_return_if_fail (sparql != NULL);
	g_return_if_fail (!cancellable || G_IS_CANCELLABLE (cancellable));

	TRACKER_SPARQL_CONNECTION_GET_CLASS (connection)->update_blank_async (connection,
	                                                                      sparql,
	                                                                      cancellable,
	                                                                      callback,
	                                                                      user_data);
}

/**
 * tracker_sparql_connection_update_blank_finish:
 * @connection: a #TrackerSparqlConnection
 * @res: a #GAsyncResult with the result of the operation
 * @error: #GError for error reporting.
 *
 * Finishes the asynchronous SPARQL update operation, and returns
 * the URNs of the generated nodes, if any.
 *
 * Returns: a #GVariant with the generated URNs, which should be freed with
 * g_variant_unref() when no longer used.
 */
GVariant *
tracker_sparql_connection_update_blank_finish (TrackerSparqlConnection  *connection,
                                               GAsyncResult             *res,
                                               GError                  **error)
{
	g_return_val_if_fail (TRACKER_IS_SPARQL_CONNECTION (connection), NULL);
	g_return_val_if_fail (G_IS_ASYNC_RESULT (res), NULL);
	g_return_val_if_fail (!error || !*error, NULL);

	return TRACKER_SPARQL_CONNECTION_GET_CLASS (connection)->update_blank_finish (connection,
	                                                                        res,
	                                                                        error);
}

/**
 * tracker_sparql_connection_update_resource:
 * @connection: a #TrackerSparqlConnection
 * @graph: (nullable): RDF graph where the resource should be inserted/updated, or %NULL for the default graph
 * @resource: a #TrackerResource
 * @cancellable: (nullable): a #GCancellable, or %NULL
 * @error: pointer to a #GError, or %NULL
 *
 * Inserts a resource as described by @resource, on the graph described by @graph.
 * This operation blocks until done.
 *
 * Returns: #TRUE if there were no errors.
 *
 * Since: 3.1
 **/
gboolean
tracker_sparql_connection_update_resource (TrackerSparqlConnection  *connection,
                                           const gchar              *graph,
                                           TrackerResource          *resource,
                                           GCancellable             *cancellable,
                                           GError                  **error)
{
	g_return_val_if_fail (TRACKER_IS_SPARQL_CONNECTION (connection), FALSE);
	g_return_val_if_fail (TRACKER_IS_RESOURCE (resource), FALSE);
	g_return_val_if_fail (!cancellable || G_IS_CANCELLABLE (cancellable), FALSE);
	g_return_val_if_fail (!error || !*error, FALSE);

	return TRACKER_SPARQL_CONNECTION_GET_CLASS (connection)->update_resource (connection,
	                                                                          graph,
	                                                                          resource,
	                                                                          cancellable,
	                                                                          error);
}

/**
 * tracker_sparql_connection_update_resource_async:
 * @connection: a #TrackerSparqlConnection
 * @graph: (nullable): RDF graph where the resource should be inserted/updated, or %NULL for the default graph
 * @resource: a #TrackerResource
 * @cancellable: (nullable): a #GCancellable, or %NULL
 * @callback: the #GAsyncReadyCallback called when the operation completes
 * @user_data: data passed to @callback
 *
 * Inserts a resource as described by @resource, on the graph described by @graph.
 * This operation is executed asynchronously, when finished @callback will be
 * executed.
 *
 * Since: 3.1
 **/
void
tracker_sparql_connection_update_resource_async (TrackerSparqlConnection *connection,
                                                 const gchar             *graph,
                                                 TrackerResource         *resource,
                                                 GCancellable            *cancellable,
                                                 GAsyncReadyCallback      callback,
                                                 gpointer                 user_data)
{
	g_return_if_fail (TRACKER_IS_SPARQL_CONNECTION (connection));
	g_return_if_fail (TRACKER_IS_RESOURCE (resource));
	g_return_if_fail (!cancellable || G_IS_CANCELLABLE (cancellable));
	g_return_if_fail (callback != NULL);

	TRACKER_SPARQL_CONNECTION_GET_CLASS (connection)->update_resource_async (connection,
	                                                                         graph,
	                                                                         resource,
	                                                                         cancellable,
	                                                                         callback,
	                                                                         user_data);
}

/**
 * tracker_sparql_connection_update_resource_finish:
 * @connection: a #TrackerSparqlConnection
 * @res: a #GAsyncResult with the result of the operation
 * @error: pointer to a #GError, or %NULL
 *
 * Finishes a tracker_sparql_connection_update_resource_async() operation.
 *
 * Returns: #TRUE if there were no errors.
 *
 * Since: 3.1
 **/
gboolean
tracker_sparql_connection_update_resource_finish (TrackerSparqlConnection  *connection,
                                                  GAsyncResult             *res,
                                                  GError                  **error)
{
	g_return_val_if_fail (TRACKER_IS_SPARQL_CONNECTION (connection), FALSE);
	g_return_val_if_fail (G_IS_ASYNC_RESULT (res), FALSE);
	g_return_val_if_fail (!error || !*error, FALSE);

	return TRACKER_SPARQL_CONNECTION_GET_CLASS (connection)->update_resource_finish (connection,
	                                                                                 res,
	                                                                                 error);
}

/**
 * tracker_sparql_connection_get_namespace_manager:
 * @connection: a #TrackerSparqlConnection
 *
 * Retrieves a #TrackerNamespaceManager that contains all
 * prefixes in the ontology of @connection.
 *
 * Returns: (transfer none): a #TrackerNamespaceManager for this
 * connection. This object is owned by @connection and must not be freed.
 */
TrackerNamespaceManager *
tracker_sparql_connection_get_namespace_manager (TrackerSparqlConnection *connection)
{
	g_return_val_if_fail (TRACKER_IS_SPARQL_CONNECTION (connection), NULL);

	return TRACKER_SPARQL_CONNECTION_GET_CLASS (connection)->get_namespace_manager (connection);
}

/**
 * tracker_sparql_connection_query_statement:
 * @connection: a #TrackerSparqlConnection
 * @sparql: the SPARQL query
 * @cancellable: a #GCancellable used to cancel the operation, or %NULL
 * @error: a #TrackerSparqlError or %NULL if no error occured
 *
 * Prepares the given @sparql as a #TrackerSparqlStatement.
 *
 * Returns: (transfer full) (nullable): a prepared statement
 */
TrackerSparqlStatement *
tracker_sparql_connection_query_statement (TrackerSparqlConnection  *connection,
                                           const gchar              *sparql,
                                           GCancellable             *cancellable,
                                           GError                  **error)
{
	g_return_val_if_fail (TRACKER_IS_SPARQL_CONNECTION (connection), NULL);
	g_return_val_if_fail (sparql != NULL, NULL);
	g_return_val_if_fail (!cancellable || G_IS_CANCELLABLE (cancellable), NULL);
	g_return_val_if_fail (!error || !*error, NULL);

	return TRACKER_SPARQL_CONNECTION_GET_CLASS (connection)->query_statement (connection,
	                                                                          sparql,
	                                                                          cancellable,
	                                                                          error);
}

/**
 * tracker_sparql_connection_create_notifier:
 * @connection: a #TrackerSparqlConnection
 *
 * Creates a new #TrackerNotifier to notify about changes in @connection.
 * See #TrackerNotifier documentation for information about how to use this
 * object.
 *
 * Returns: (transfer full): a newly created notifier. Free with g_object_unref()
 *          when no longer needed.
 **/
TrackerNotifier *
tracker_sparql_connection_create_notifier (TrackerSparqlConnection *connection)
{
	g_return_val_if_fail (TRACKER_IS_SPARQL_CONNECTION (connection), NULL);

	return TRACKER_SPARQL_CONNECTION_GET_CLASS (connection)->create_notifier (connection);
}

/**
 * tracker_sparql_connection_close:
 * @connection: a #TrackerSparqlConnection
 *
 * Closes a SPARQL connection. No other API calls than g_object_unref()
 * should happen after this call.
 *
 * This call is blocking. All pending updates will be flushed, and the
 * store databases will be closed orderly. All ongoing SELECT queries
 * will be cancelled. Notifiers will no longer emit events.
 *
 * Since: 3.0
 */
void
tracker_sparql_connection_close (TrackerSparqlConnection *connection)
{
	g_return_if_fail (TRACKER_IS_SPARQL_CONNECTION (connection));

	TRACKER_SPARQL_CONNECTION_GET_CLASS (connection)->close (connection);
}

/**
 * tracker_sparql_connection_close_async:
 * @connection: a #TrackerSparqlConnection
 * @cancellable: a #GCancellable, or %NULL
 * @callback: user-defined #GAsyncReadyCallback to be called when
 *            asynchronous operation is finished.
 * @user_data: user-defined data to be passed to @callback
 *
 * Closes a connection asynchronously. No other API calls than g_object_unref()
 * should happen after this call. See tracker_sparql_connection_close() for more
 * information.
 *
 * Since: 3.0
 **/
void
tracker_sparql_connection_close_async (TrackerSparqlConnection *connection,
                                       GCancellable            *cancellable,
                                       GAsyncReadyCallback      callback,
                                       gpointer                 user_data)
{
	g_return_if_fail (TRACKER_IS_SPARQL_CONNECTION (connection));

	TRACKER_SPARQL_CONNECTION_GET_CLASS (connection)->close_async (connection,
	                                                               cancellable,
	                                                               callback,
	                                                               user_data);
}

/**
 * tracker_sparql_connection_close_finish:
 * @connection: a #TrackerSparqlConnection
 * @res: the #GAsyncResult
 * @error: pointer to a #GError
 *
 * Finishes the asynchronous connection close.
 *
 * Returns: %FALSE if some error occurred, %TRUE otherwise
 *
 * Since: 3.0
 **/
gboolean
tracker_sparql_connection_close_finish (TrackerSparqlConnection  *connection,
                                        GAsyncResult             *res,
                                        GError                  **error)
{
	g_return_val_if_fail (TRACKER_IS_SPARQL_CONNECTION (connection), FALSE);

	return TRACKER_SPARQL_CONNECTION_GET_CLASS (connection)->close_finish (connection,
	                                                                       res, error);
}

/**
 * tracker_sparql_connection_create_batch:
 * @connection: a #TrackerSparqlConnection
 *
 * Creates a new batch to store and execute update commands. If the connection
 * is readonly or cannot issue SPARQL updates, %NULL will be returned.
 *
 * Returns: (transfer full): (nullable): A new #TrackerBatch
 **/
TrackerBatch *
tracker_sparql_connection_create_batch (TrackerSparqlConnection *connection)
{
	g_return_val_if_fail (TRACKER_IS_SPARQL_CONNECTION (connection), NULL);

	if (!TRACKER_SPARQL_CONNECTION_GET_CLASS (connection)->create_batch)
		return NULL;

	return TRACKER_SPARQL_CONNECTION_GET_CLASS (connection)->create_batch (connection);
}

/**
 * tracker_sparql_connection_map_connection:
 * @connection: a #TrackerSparqlConnection
 * @handle_name: handle name for @service_connection
 * @service_connection: a #TrackerSparqlConnection to use from @connection
 *
 * Maps @service_connection so it is available as a "local:@handle_name" URI
 * in @connection. This can be accessed via the SERVICE SPARQL syntax in
 * queries from @connection. E.g.:
 *
 * <example>
 * <programlisting>
 * SELECT ?u {
 *   SERVICE &lt;local:other-connection&gt; {
 *     ?u a rdfs:Resource
 *   }
 * }
 * </programlisting>
 * </example>
 *
 * This is useful to interrelate data from multiple
 * #TrackerSparqlConnection<!-- -->s maintained by the same process,
 * without creating a public endpoint for @service_connection.
 *
 * This call only applies to connections created via
 * tracker_sparql_connection_new() and tracker_sparql_connection_new_async().
 *
 * Since: 3.2
 **/
void
tracker_sparql_connection_map_connection (TrackerSparqlConnection *connection,
					  const gchar             *handle_name,
					  TrackerSparqlConnection *service_connection)
{
	g_return_if_fail (TRACKER_IS_DIRECT_CONNECTION (connection));
	g_return_if_fail (TRACKER_IS_DIRECT_CONNECTION (service_connection));
	g_return_if_fail (handle_name && *handle_name);

	if (!TRACKER_SPARQL_CONNECTION_GET_CLASS (connection)->map_connection)
		return;

	return TRACKER_SPARQL_CONNECTION_GET_CLASS (connection)->map_connection (connection,
	                                                                         handle_name,
	                                                                         service_connection);
}
