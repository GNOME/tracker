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

// Convenience, hidden in the documentation
namespace Tracker {
	public const string DBUS_SERVICE = "org.freedesktop.Tracker1";
	public const string DBUS_INTERFACE_RESOURCES = "org.freedesktop.Tracker1.Resources";
	public const string DBUS_OBJECT_RESOURCES = "/org/freedesktop/Tracker1/Resources";
	public const string DBUS_INTERFACE_STATISTICS = "org.freedesktop.Tracker1.Statistics";
	public const string DBUS_OBJECT_STATISTICS = "/org/freedesktop/Tracker1/Statistics";
	public const string DBUS_INTERFACE_STATUS = "org.freedesktop.Tracker1.Status";
	public const string DBUS_OBJECT_STATUS = "/org/freedesktop/Tracker1/Status";
	public const string DBUS_INTERFACE_STEROIDS = "org.freedesktop.Tracker1.Steroids";
	public const string DBUS_OBJECT_STEROIDS = "/org/freedesktop/Tracker1/Steroids";
}

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
	UNSUPPORTED
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
	 * tracker_sparql_connection_get_finish:
	 * @_res_: The #GAsyncResult from the callback used to return the #TrackerSparqlConnection
	 * @error: The error which occurred or %NULL
	 *
	 * This function is called from the callback provided for
	 * tracker_sparql_connection_get_async() to return the connection requested
	 * or an error in cases of failure.
	 *
	 * Returns: a new #TrackerSparqlConnection. Call g_object_unref() on the
	 * object when no longer used.
	 *
	 * Since: 0.10
	 */

	/**
	 * tracker_sparql_connection_get_async:
	 * @cancellable: a #GCancellable used to cancel the operation
	 * @_callback_: user-defined #GAsyncReadyCallback to be called when
	 *              asynchronous operation is finished.
	 * @_user_data_: user-defined data to be passed to @_callback_
	 *
	 * A #TrackerSparqlConnection is returned asynchronously in the @_callback_ of
	 * your choosing. You must call tracker_sparql_connection_get_finish() to
	 * find out if the connection was returned without error.
	 *
	 * See also: tracker_sparql_connection_get().
	 *
	 * Since: 0.10
	 */
	public extern async static new Connection get_async (Cancellable? cancellable = null) throws Sparql.Error, IOError, DBusError, SpawnError;

	/**
	 * tracker_sparql_connection_get:
	 * @cancellable: a #GCancellable used to cancel the operation
	 * @error: #GError for error reporting.
	 *
	 * This function is used to give the caller a connection to Tracker they can
	 * use for future requests. The best backend available to connect to
	 * Tracker is returned. These backends include direct-access (for read-only
	 * queries) and D-Bus (for both read and write queries).
	 *
	 * You can use <link linkend="tracker-overview-environment-variables">
	 * environment variables</link> to influence how backends are used. If
	 * no environment variables are provided, both backends are loaded and
	 * chosen based on their merits. If you try to force a backend for a query
	 * which it won't support (i.e. an update for a read-only backend), you will
	 * see critical warnings.
	 *
	 * When calling either tracker_sparql_connection_get(),
	 * tracker_sparql_connection_get_direct() or the asynchronous variants of
	 * these functions, a mutex is used to protect the loading of backends
	 * against potential race conditions. For synchronous calls, this function
	 * will always block if a previous connection get method has been called.
	 *
	 * All backends will call the D-Bus tracker-store API Wait() to make sure
	 * the store and databases are in the right state before any user based
	 * requests can proceed. There may be a small delay during this call if the
	 * databases weren't shutdown cleanly and need to be checked on start up. If
	 * the journal needs to be replayed in such an event, the delay may be
	 * substantial while data is restored as best as possible.
	 *
	 * Returns: a new #TrackerSparqlConnection. Call g_object_unref() on the
	 * object when no longer used.
	 *
	 * Since: 0.10
	 */
	public extern static new Connection get (Cancellable? cancellable = null) throws Sparql.Error, IOError, DBusError, SpawnError;

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
	 * tracker_sparql_connection_local_new:
	 * @flags: Flags to define connection behavior
	 * @store: Location for the database
	 * @journal: Location for the operation journal, or %NULL
	 * @ontology: Location of the ontology used for this connection, or %NULL
	 * @cancellable: A #GCancellable
	 * @error: The error which occurred or %NULL
	 *
	 * Returns: a new local #TrackerSparqlConnection using the specified
	 * @cache/@journal locations, and the ontology specified in the @ontology
	 * directory. Call g_object_unref() on the object when no longer used.
	 *
	 * This database connection is considered entirely private to the calling
	 * process, if multiple processes use the same journal/cache locations,
	 * the results are unpredictable.
	 *
	 * The @journal is used to rebuild the database in case of data corruption,
	 * if %NULL is provided, the same location than @store will be assumed.
	 *
	 * The caller is entirely free to define an ontology or reuse Nepomuk for
	 * its purposes. For the former see the "Defining ontologies" section in
	 * this library docs. For the latter pass a %NULL @ontology.
	 *
	 * The @ontology argument may be a resource:/// URI, a directory location
	 * must be provided, all children .ontology and .description files will
	 * be read.
	 *
	 * The @store and @journal arguments expect directories, and those are
	 * assumed to be entirely private to Tracker.
	 *
	 * Since: 2.0
	 */
	public extern static new Connection local_new (Tracker.Sparql.ConnectionFlags flags, File store, File? journal, File? ontology, Cancellable? cancellable = null) throws Sparql.Error, IOError;

	/**
	 * tracker_sparql_connection_local_new_async:
	 * @flags: Flags to define connection behavior
	 * @store: Location for the database
	 * @journal: Location for the operation journal, or %NULL
	 * @ontology: Location of the ontology used for this connection, or %NULL
	 * @cancellable: A #GCancellable
	 * @_callback_: user-defined #GAsyncReadyCallback to be called when
	 *              asynchronous operation is finished.
	 * @_user_data_: user-defined data to be passed to @_callback_
	 *
	 * Returns: a new local #TrackerSparqlConnection using the specified
	 * @cache/@journal locations, and the ontology specified in the @ontology
	 * directory. Call g_object_unref() on the object when no longer used.
	 *
	 * See tracker_sparql_connection_local_new() for more details.
	 *
	 * Since: 2.0
	 */

	/**
	 * tracker_sparql_connection_local_new_finish:
	 * @_res_: a #GAsyncResult with the result of the operation
	 * @error: #GError for error reporting.
	 *
	 * Finishes the asynchronous local database creation/loading.
	 *
	 * Returns: The #TrackerSparqlConnection for the local endpoint.
	 * On error, #NULL is returned and the @error is set accordingly.
	 * Call g_object_unref() on the returned connection when no longer needed.
	 *
	 * Since: 2.0
	 */
	public extern async static new Connection local_new_async (Tracker.Sparql.ConnectionFlags flags, File store, File? journal, File? ontology, Cancellable? cancellable = null) throws Sparql.Error, IOError;

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
	 * The @sparql query should be built with #TrackerSparqlBuilder, or
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
	 * The @sparql query should be built with #TrackerSparqlBuilder, or
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
	 * Executes asynchronously an array of SPARQL updates. Each update in the
	 * array is its own transaction. This means that update n+1 is not halted
	 * due to an error in update n.
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
	 * Returns: a #GPtrArray of size @sparql_length1 with elements that are
	 * either NULL or a GError instance. The returned array should be freed with
	 * g_ptr_array_unref when no longer used, not with g_ptr_array_free. When
	 * you use errors of the array, you must g_error_copy them. Errors inside of
	 * the array must be considered as const data and not freed. The index of
	 * the error corresponds to the index of the update query in the array that
	 * you passed to tracker_sparql_connection_update_array_async.
	 *
	 * Since: 0.10
	 */
	public async virtual GenericArray<Sparql.Error?>? update_array_async (string[] sparql, int priority = GLib.Priority.DEFAULT, Cancellable? cancellable = null) throws Sparql.Error, GLib.Error, GLib.IOError, DBusError {
		warning ("Interface 'update_array_async' not implemented");
		return null;
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
	 * The @sparql query should be built with #TrackerSparqlBuilder, or
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
	 * tracker_sparql_connection_load:
	 * @self: a #TrackerSparqlConnection
	 * @file: a #GFile
	 * @cancellable: a #GCancellable used to cancel the operation
	 * @error: #GError for error reporting.
	 *
	 * Loads a Turtle file (TTL) into the store. The API call is completely
	 * synchronous, so it may block.
	 *
	 * Since: 0.10
	 */
	public virtual void load (File file, Cancellable? cancellable = null) throws Sparql.Error, IOError, DBusError {
		warning ("Interface 'load' not implemented");
	}

	/**
	 * tracker_sparql_connection_load_async:
	 * @self: a #TrackerSparqlConnection
	 * @file: a #GFile
	 * @cancellable: a #GCancellable used to cancel the operation
	 * @_callback_: user-defined #GAsyncReadyCallback to be called when
	 *              asynchronous operation is finished.
	 * @_user_data_: user-defined data to be passed to @_callback_
	 *
	 * Loads, asynchronously, a Turtle file (TTL) into the store.
	 *
	 * Since: 0.10
	 */

	/**
	 * tracker_sparql_connection_load_finish:
	 * @self: a #TrackerSparqlConnection
	 * @_res_: a #GAsyncResult with the result of the operation
	 * @error: #GError for error reporting.
	 *
	 * Finishes the asynchronous load of the Turtle file.
	 *
	 * Since: 0.10
	 */
	public async virtual void load_async (File file, Cancellable? cancellable = null) throws Sparql.Error, IOError, DBusError {
		warning ("Interface 'load_async' not implemented");
	}

	/**
	 * tracker_sparql_connection_statistics:
	 * @self: a #TrackerSparqlConnection
	 * @cancellable: a #GCancellable used to cancel the operation
	 * @error: #GError for error reporting.
	 *
	 * Retrieves the statistics from the Store. The API call is completely
	 * synchronous, so it may block.
	 *
	 * Returns: a #TrackerSparqlCursor to iterate the reply if successful, #NULL
	 * on error. Call g_object_unref() on the returned cursor when no longer
	 * needed.
	 *
	 * Since: 0.10
	 */
	public virtual Cursor? statistics (Cancellable? cancellable = null) throws Sparql.Error, IOError, DBusError {
		warning ("Interface 'statistics' not implemented");
		return null;
	}

	/**
	 * tracker_sparql_connection_statistics_async:
	 * @self: a #TrackerSparqlConnection
	 * @cancellable: a #GCancellable used to cancel the operation
	 * @_callback_: user-defined #GAsyncReadyCallback to be called when
	 *              asynchronous operation is finished.
	 * @_user_data_: user-defined data to be passed to @_callback_
	 *
	 * Retrieves, asynchronously, the statistics from the Store.
	 *
	 * Since: 0.10
	 */

	/**
	 * tracker_sparql_connection_statistics_finish:
	 * @self: a #TrackerSparqlConnection
	 * @_res_: a #GAsyncResult with the result of the operation
	 * @error: #GError for error reporting.
	 *
	 * Finishes the asynchronous retrieval of statistics from the Store.
	 *
	 * Returns: a #TrackerSparqlCursor to iterate the reply if successful, #NULL
	 * on error. Call g_object_unref() on the returned cursor when no longer
	 * needed.
	 *
	 * Since: 0.10
	 */
	public async virtual Cursor? statistics_async (Cancellable? cancellable = null) throws Sparql.Error, IOError, DBusError {
		warning ("Interface 'statistics_async' not implemented");
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
	 * tracker_sparql_connection_set_domain:
	 * @domain: The domain name for the default connection
	 *
	 * Sets the domain (usually a DBus name or application ID) that
	 * will be used on on the connection obtained by
	 * tracker_sparql_connection_get(). See the "Isolating tracker-store
	 * clients" section in the docs for this library.
	 *
	 * This function must be called before any tracker_sparql_connection_get()
	 * calls happen.
	 *
	 * Since: 2.0
	 */
	public extern static void set_domain (string? domain);

	/**
	 * tracker_sparql_connection_get_domain:
	 *
	 * Gets the domain (usually a DBus name or application ID) that
	 * will be used on on the connection obtained by
	 * tracker_sparql_connection_get().
	 * See tracker_sparql_connection_set_domain() for more information.
	 *
	 * Returns: (transfer full): The domain string, or %NULL if none is set
	 *
	 * Since: 2.0
	 */
	public extern static string? get_domain ();

	/**
	 * tracker_sparql_connection_set_dbus_connection:
	 * @dbus_connection: A #GDBusConnection to a suitable message bus.
	 *
	 * By default, a connection is opened to the session-wide Tracker services
	 * running on the D-Bus session bus. This function allows you to connect to
	 * Tracker services that are running on a different bus.
	 *
	 * This function must be called before any tracker_sparql_connection_get()
	 * calls happen.
	 *
	 * See also: the TRACKER_IPC_BUS environment variable.
	 *
	 * Since: 2.2
	 */
	public extern static void set_dbus_connection (DBusConnection dbus_connection);

	/**
	 * tracker_sparql_connection_get_dbus_connection:
	 *
	 * Gets the D-Bus connection that is used to contact the Tracker services.
	 *
	 * Returns: (transfer none): A #GDBusConnection instance, or %NULL if the
	 *                           default is being used.
	 *
	 * Since: 2.0
	 */
	public extern static DBusConnection? get_dbus_connection ();

	/**
	 * tracker_sparql_connection_query_statement:
	 *
	 * Prepares the given @sparql as a #TrackerSparqlStatement.
	 *
	 * Since: 2.2
	 */
	public virtual Statement? query_statement (string sparql, Cancellable? cancellable = null) throws Sparql.Error {
		warning ("Interface 'query_statement' not implemented");
		return null;
	}
}
