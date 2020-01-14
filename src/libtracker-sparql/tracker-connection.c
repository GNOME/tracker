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
#include "config.h"

#include "tracker-connection.h"
#include "tracker-private.h"

G_DEFINE_ABSTRACT_TYPE (TrackerSparqlConnection, tracker_sparql_connection,
                        G_TYPE_OBJECT)

G_DEFINE_QUARK (tracker-sparql-error-quark, tracker_sparql_error)

static void
tracker_sparql_connection_init (TrackerSparqlConnection *connection)
{
}

static void
tracker_sparql_connection_class_init (TrackerSparqlConnectionClass *klass)
{
}

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
 * @priority: the priority for the operation
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
                                  gint                      priority,
                                  GCancellable             *cancellable,
                                  GError                  **error)
{
	g_return_if_fail (TRACKER_IS_SPARQL_CONNECTION (connection));
	g_return_if_fail (sparql != NULL);
	g_return_if_fail (!cancellable || G_IS_CANCELLABLE (cancellable));
	g_return_if_fail (!error || !*error);

	return TRACKER_SPARQL_CONNECTION_GET_CLASS (connection)->update (connection,
	                                                                 sparql,
	                                                                 priority,
	                                                                 cancellable,
	                                                                 error);
}

/**
 * tracker_sparql_connection_update_async:
 * @connection: a #TrackerSparqlConnection
 * @sparql: string containing the SPARQL update query
 * @priority: the priority for the asynchronous operation
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
                                        gint                     priority,
                                        GCancellable            *cancellable,
                                        GAsyncReadyCallback      callback,
                                        gpointer                 user_data)
{
	g_return_if_fail (TRACKER_IS_SPARQL_CONNECTION (connection));
	g_return_if_fail (sparql != NULL);
	g_return_if_fail (!cancellable || G_IS_CANCELLABLE (cancellable));

	TRACKER_SPARQL_CONNECTION_GET_CLASS (connection)->update_async (connection,
	                                                                sparql,
	                                                                priority,
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
 * @priority: the priority for the asynchronous operation
 * @cancellable: a #GCancellable used to cancel the operation
 * @callback: user-defined #GAsyncReadyCallback to be called when
 *            asynchronous operation is finished.
 * @user_data: user-defined data to be passed to @callback
 *
 * Executes asynchronously an array of SPARQL updates. Each update in the
 * array is its own transaction. This means that update n+1 is not halted
 * due to an error in update n.
 */
void
tracker_sparql_connection_update_array_async (TrackerSparqlConnection  *connection,
                                              gchar                   **sparql,
                                              gint                      sparql_length,
                                              gint                      priority,
                                              GCancellable             *cancellable,
                                              GAsyncReadyCallback       callback,
                                              gpointer                  user_data)
{
	g_return_if_fail (TRACKER_IS_SPARQL_CONNECTION (connection));
	g_return_if_fail (sparql != NULL);
	g_return_if_fail (!cancellable || G_IS_CANCELLABLE (cancellable));

	TRACKER_SPARQL_CONNECTION_GET_CLASS (connection)->update_array_async (connection,
	                                                                      sparql,
	                                                                      sparql_length,
	                                                                      priority,
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
 * <example>
 * <programlisting>
 * static void
 * async_update_array_callback (GObject      *source_object,
 *                              GAsyncResult *result,
 *                              gpointer      user_data)
 * {
 *     GError *error = NULL;
 *     GPtrArray *errors;
 *     guint i;
 *
 *     errors = tracker_sparql_connection_update_array_finish (connection, result, &error);
 *     g_assert_no_error (error);
 *
 *     for (i = 0; i < errors->len; i++) {
 *         const GError *e = g_ptr_array_index (errors, i);
 *
 *         ...
 *     }
 *
 *     g_ptr_array_unref (errors);
 * }
 * </programlisting>
 * </example>
 *
 * Returns: a #GPtrArray of size @sparql_length with elements that are
 * either NULL or a GError instance. The returned array should be freed with
 * g_ptr_array_unref when no longer used, not with g_ptr_array_free. When
 * you use errors of the array, you must g_error_copy them. Errors inside of
 * the array must be considered as const data and not freed. The index of
 * the error corresponds to the index of the update query in the array that
 * you passed to tracker_sparql_connection_update_array_async.
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
 * @priority: the priority for the operation
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
                                        gint                      priority,
                                        GCancellable             *cancellable,
                                        GError                  **error)
{
	g_return_val_if_fail (TRACKER_IS_SPARQL_CONNECTION (connection), NULL);
	g_return_val_if_fail (sparql != NULL, NULL);
	g_return_val_if_fail (!cancellable || G_IS_CANCELLABLE (cancellable), NULL);
	g_return_val_if_fail (!error || !*error, NULL);

	return TRACKER_SPARQL_CONNECTION_GET_CLASS (connection)->update_blank (connection,
	                                                                       sparql,
	                                                                       priority,
	                                                                       cancellable,
	                                                                       error);
}

/**
 * tracker_sparql_connection_update_blank_async:
 * @connection: a #TrackerSparqlConnection
 * @sparql: string containing the SPARQL update query
 * @priority: the priority for the asynchronous operation
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
                                              gint                     priority,
                                              GCancellable            *cancellable,
                                              GAsyncReadyCallback      callback,
                                              gpointer                 user_data)
{
	g_return_if_fail (TRACKER_IS_SPARQL_CONNECTION (connection));
	g_return_if_fail (sparql != NULL);
	g_return_if_fail (!cancellable || G_IS_CANCELLABLE (cancellable));

	TRACKER_SPARQL_CONNECTION_GET_CLASS (connection)->update_blank_async (connection,
	                                                                      sparql,
	                                                                      priority,
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
 * @flags: flags to modify notifier behavior
 *
 * Creates a new #TrackerNotifier to notify about changes in @connection.
 * See #TrackerNotifier documentation for information about how to use this
 * object.
 *
 * Returns: (transfer full): a newly created notifier. Free with g_object_unref()
 *          when no longer needed.
 **/
TrackerNotifier *
tracker_sparql_connection_create_notifier (TrackerSparqlConnection *connection,
                                           TrackerNotifierFlags     flags)
{
	g_return_val_if_fail (TRACKER_IS_SPARQL_CONNECTION (connection), NULL);

	return TRACKER_SPARQL_CONNECTION_GET_CLASS (connection)->create_notifier (connection, flags);
}

/**
 * tracker_sparql_connection_close:
 * @self: a #TrackerSparqlConnection
 *
 * Closes a SPARQL connection. No other API calls than g_object_unref()
 * should happen after this call.
 *
 * This call is blocking. All pending updates will be flushed, and the
 * store databases will be closed orderly. All ongoing SELECT queries
 * will be cancelled.
 *
 * Since: 3.0
 */
void
tracker_sparql_connection_close (TrackerSparqlConnection *connection)
{
	g_return_if_fail (TRACKER_IS_SPARQL_CONNECTION (connection));

	TRACKER_SPARQL_CONNECTION_GET_CLASS (connection)->close (connection);
}
