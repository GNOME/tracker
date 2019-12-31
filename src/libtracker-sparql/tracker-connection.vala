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
 * @short_description: Connecting to the Store
 * @title: TrackerSparqlConnection
 * @stability: Stable
 * @include: tracker-sparql.h
 *
 * <para>
 * #TrackerSparqlConnection is an object which sets up connections to the
 * Tracker Store.
 * </para>
 */

/**
 * TrackerSparqlError:
 * @TRACKER_SPARQL_ERROR_PARSE: Error parsing the SPARQL string.
 * @TRACKER_SPARQL_UNKNOWN_CLASS: Unknown class.
 * @TRACKER_SPARQL_UNKNOWN_PROPERTY: Unknown property.
 * @TRACKER_SPARQL_TYPE: Wrong type.
 * @TRACKER_SPARQL_CONSTRAINT: Subject is not in the domain of a property or
 *                             trying to set multiple values for a single valued
 *                             property.
 * @TRACKER_SPARQL_NO_SPACE: There was no disk space available to perform the request.
 * @TRACKER_SPARQL_INTERNAL: Internal error.
 * @TRACKER_SPARQL_UNSUPPORTED: Unsupported feature or method.
 * @TRACKER_SPARQL_UNKNOWN_GRAPH: Unknown graph.
 *
 * Error domain for Tracker Sparql. Errors in this domain will be from the
 * #TrackerSparqlError enumeration. See #GError for more information on error
 * domains.
 *
 * Since: 0.10
 */
[DBus (name = "org.freedesktop.Tracker1.SparqlError")]
public errordomain Tracker.Sparql.Error {
	PARSE,
	UNKNOWN_CLASS,
	UNKNOWN_PROPERTY,
	TYPE,
	CONSTRAINT,
	NO_SPACE,
	INTERNAL,
	UNSUPPORTED,
	UNKNOWN_GRAPH
}

public enum Tracker.Sparql.ConnectionFlags {
	NONE     = 0,
	READONLY = 1 << 0,
}

/**
 * TrackerSparqlConnection:
 *
 * The <structname>TrackerSparqlConnection</structname> object represents a
 * connection with the Tracker store or databases depending on direct or
 * non-direct requests.
 */
public abstract class Tracker.Sparql.Connection : Object {
	/**
	 * tracker_sparql_connection_remote_new:
	 * @uri_base: Base URI of the remote connection
	 *
	 * Returns: a new remote #TrackerSparqlConnection. Call g_object_unref() on the
	 * object when no longer used.
	 *
	 * Since: 1.12
	 */
	public extern static new Connection remote_new (string uri_base);

	/**
	 * tracker_sparql_connection_new:
	 * @flags: Flags to define connection behavior
	 * @store: Location for the database
	 * @ontology: Location of the ontology used for this connection, or %NULL
	 * @cancellable: A #GCancellable
	 * @error: The error which occurred or %NULL
	 *
	 * Returns: a new #TrackerSparqlConnection using the specified
	 * @cache location, and the ontology specified in the @ontology
	 * directory. Call g_object_unref() on the object when no longer used.
	 *
	 * This database connection is considered entirely private to the calling
	 * process, if multiple processes use the same cache location,
	 * the results are unpredictable.
	 *
	 * The caller is entirely free to define an ontology or reuse Nepomuk for
	 * its purposes. For the former see the "Defining ontologies" section in
	 * this library documentation. For the latter pass a %NULL @ontology.
	 *
	 * If the connection is readonly the @ontology argument will be ignored,
	 * and the ontology reconstructed from the database itself.
	 *
	 * The @ontology argument may be a resource:/// URI, a directory location
	 * must be provided, all children .ontology and .description files will
	 * be read.
	 *
	 * The @store argument expects a directory, and it is
	 * assumed to be entirely private to Tracker.
	 *
	 * Since: 3.0
	 */
	public extern static new Connection new (Tracker.Sparql.ConnectionFlags flags, File store, File? ontology, Cancellable? cancellable = null) throws Sparql.Error, IOError;

	/**
	 * tracker_sparql_connection_new_async:
	 * @flags: Flags to define connection behavior
	 * @store: Location for the database
	 * @ontology: Location of the ontology used for this connection, or %NULL
	 * @cancellable: A #GCancellable
	 * @_callback_: user-defined #GAsyncReadyCallback to be called when
	 *              asynchronous operation is finished.
	 * @_user_data_: user-defined data to be passed to @_callback_
	 *
	 * Returns: a new #TrackerSparqlConnection using the specified
	 * @cache location, and the ontology specified in the @ontology
	 * directory. Call g_object_unref() on the object when no longer used.
	 *
	 * See tracker_sparql_connection_new() for more details.
	 *
	 * Since: 3.0
	 */

	/**
	 * tracker_sparql_connection_new_finish:
	 * @_res_: a #GAsyncResult with the result of the operation
	 * @error: #GError for error reporting.
	 *
	 * Finishes the asynchronous local database creation/loading.
	 *
	 * Returns: The #TrackerSparqlConnection for the local endpoint.
	 * On error, #NULL is returned and the @error is set accordingly.
	 * Call g_object_unref() on the returned connection when no longer needed.
	 *
	 * Since: 3.0
	 */
	public extern async static new Connection new_async (Tracker.Sparql.ConnectionFlags flags, File store, File? ontology, Cancellable? cancellable = null) throws Sparql.Error, IOError;

	public extern static new Connection bus_new (string service_name, string? object_path, DBusConnection? dbus_connection = null) throws Sparql.Error, IOError, DBusError, GLib.Error;

	/**
	 * tracker_sparql_connection_query:
	 * @self: a #TrackerSparqlConnection
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
	 * Returns: a #TrackerSparqlCursor if results were found, #NULL otherwise.
	 * On error, #NULL is returned and the @error is set accordingly.
	 * Call g_object_unref() on the returned cursor when no longer needed.
	 *
	 * Since: 0.10
	 */
	public abstract Cursor query (string sparql, Cancellable? cancellable = null) throws Sparql.Error, GLib.Error, GLib.IOError, DBusError;

	/**
	 * tracker_sparql_connection_query_finish:
	 * @self: a #TrackerSparqlConnection
	 * @_res_: a #GAsyncResult with the result of the operation
	 * @error: #GError for error reporting.
	 *
	 * Finishes the asynchronous SPARQL query operation.
	 *
	 * Returns: a #TrackerSparqlCursor if results were found, #NULL otherwise.
	 * On error, #NULL is returned and the @error is set accordingly.
	 * Call g_object_unref() on the returned cursor when no longer needed.
	 *
	 * Since: 0.10
	 */

	/**
	 * tracker_sparql_connection_query_async:
	 * @self: a #TrackerSparqlConnection
	 * @sparql: string containing the SPARQL query
	 * @cancellable: a #GCancellable used to cancel the operation
	 * @_callback_: user-defined #GAsyncReadyCallback to be called when
	 *              asynchronous operation is finished.
	 * @_user_data_: user-defined data to be passed to @_callback_
	 *
	 * Executes asynchronously a SPARQL query.
	 *
	 * Since: 0.10
	 */
	public async abstract Cursor query_async (string sparql, Cancellable? cancellable = null) throws Sparql.Error, GLib.Error, GLib.IOError, DBusError;

	/**
	 * tracker_sparql_connection_update:
	 * @self: a #TrackerSparqlConnection
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
	 *
	 * Since: 0.10
	 */
	public virtual void update (string sparql, int priority = GLib.Priority.DEFAULT, Cancellable? cancellable = null) throws Sparql.Error, GLib.Error, GLib.IOError, DBusError {
		warning ("Interface 'update' not implemented");
	}

	/**
	 * tracker_sparql_connection_update_async:
	 * @self: a #TrackerSparqlConnection
	 * @sparql: string containing the SPARQL update query
	 * @priority: the priority for the asynchronous operation
	 * @cancellable: a #GCancellable used to cancel the operation
	 * @_callback_: user-defined #GAsyncReadyCallback to be called when
	 *              asynchronous operation is finished.
	 * @_user_data_: user-defined data to be passed to @_callback_
	 *
	 * Executes asynchronously a SPARQL update.
	 *
	 * Since: 0.10
	 */

	/**
	 * tracker_sparql_connection_update_finish:
	 * @self: a #TrackerSparqlConnection
	 * @_res_: a #GAsyncResult with the result of the operation
	 * @error: #GError for error reporting.
	 *
	 * Finishes the asynchronous SPARQL update operation.
	 *
	 * Since: 0.10
	 */
	public async virtual void update_async (string sparql, int priority = GLib.Priority.DEFAULT, Cancellable? cancellable = null) throws Sparql.Error, GLib.Error, GLib.IOError, DBusError {
		warning ("Interface 'update_async' not implemented");
	}

	/**
	 * tracker_sparql_connection_update_array_async:
	 * @self: a #TrackerSparqlConnection
	 * @sparql: an array of strings containing the SPARQL update queries
	 * @sparql_length1: the amount of strings you pass as @sparql
	 * @priority: the priority for the asynchronous operation
	 * @cancellable: a #GCancellable used to cancel the operation
	 * @_callback_: user-defined #GAsyncReadyCallback to be called when
	 *              asynchronous operation is finished.
	 * @_user_data_: user-defined data to be passed to @_callback_
	 *
	 * Executes asynchronously an array of SPARQL updates.
	 *
	 * Since: 0.10
	 */

	/**
	 * tracker_sparql_connection_update_array_finish:
	 * @self: a #TrackerSparqlConnection
	 * @_res_: a #GAsyncResult with the result of the operation
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
	 * Returns: %TRUE if the update was successful, %FALSE otherwise.
	 *
	 * Since: 0.10
	 */
	public async virtual bool update_array_async (string[] sparql, int priority = GLib.Priority.DEFAULT, Cancellable? cancellable = null) throws Sparql.Error, GLib.Error, GLib.IOError, DBusError {
		warning ("Interface 'update_array_async' not implemented");
		return false;
	}

	/**
	 * tracker_sparql_connection_update_blank:
	 * @self: a #TrackerSparqlConnection
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
	 *
	 * Since: 0.10
	 */
	public virtual GLib.Variant? update_blank (string sparql, int priority = GLib.Priority.DEFAULT, Cancellable? cancellable = null) throws Sparql.Error, GLib.Error, GLib.IOError, DBusError {
		warning ("Interface 'update_blank' not implemented");
		return null;
	}

	/**
	 * tracker_sparql_connection_update_blank_async:
	 * @self: a #TrackerSparqlConnection
	 * @sparql: string containing the SPARQL update query
	 * @priority: the priority for the asynchronous operation
	 * @cancellable: a #GCancellable used to cancel the operation
	 * @_callback_: user-defined #GAsyncReadyCallback to be called when
	 *              asynchronous operation is finished.
	 * @_user_data_: user-defined data to be passed to @_callback_
	 *
	 * Executes asynchronously a SPARQL update.
	 *
	 * Since: 0.10
	 */

	/**
	 * tracker_sparql_connection_update_blank_finish:
	 * @self: a #TrackerSparqlConnection
	 * @_res_: a #GAsyncResult with the result of the operation
	 * @error: #GError for error reporting.
	 *
	 * Finishes the asynchronous SPARQL update operation, and returns
	 * the URNs of the generated nodes, if any.
	 *
	 * Returns: a #GVariant with the generated URNs, which should be freed with
	 * g_variant_unref() when no longer used.
	 *
	 * Since: 0.10
	 */
	public async virtual GLib.Variant? update_blank_async (string sparql, int priority = GLib.Priority.DEFAULT, Cancellable? cancellable = null) throws Sparql.Error, GLib.Error, GLib.IOError, DBusError {
		warning ("Interface 'update_blank_async' not implemented");
		return null;
	}

	/**
	 * tracker_sparql_connection_get_namespace_manager:
	 * @self: a #TrackerSparqlConnection
	 *
	 * Retrieves a #TrackerNamespaceManager that contains all
	 * prefixes in the ontology of @self.
	 *
	 * Returns: (transfer none): a #TrackerNamespaceManager for this
	 * connection. This object is owned by @self and must not be freed.
	 *
	 * Since: 2.0
	 */
	public virtual NamespaceManager? get_namespace_manager () {
		warning ("Not implemented");
		return null;
	}

	/**
	 * tracker_sparql_connection_query_statement:
	 * @self: a #TrackerSparqlConnection
	 * @sparql: the SPARQL query
	 * @cancellable: a #GCancellable used to cancel the operation, or %NULL
	 * @error: a #TrackerSparqlError or %NULL if no error occured
	 *
	 * Prepares the given @sparql as a #TrackerSparqlStatement.
	 *
	 * Since: 2.2
	 */
	public virtual Statement? query_statement (string sparql, Cancellable? cancellable = null) throws Sparql.Error {
		warning ("Interface 'query_statement' not implemented");
		return null;
	}

	public virtual Tracker.Notifier? create_notifier (Tracker.NotifierFlags flags) {
		warning ("Interface 'create_notifier' not implemented");
		return null;
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
	public virtual void close () {
		warning ("Not implemented");
	}
}
