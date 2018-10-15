/*
 * Copyright (C) 2018, Red Hat Ltd.
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
 * SECTION: tracker-sparql-statement
 * @short_description: Prepared statements
 * @title: TrackerSparqlStatement
 * @stability: Stable
 * @include: tracker-sparql.h
 *
 * The <structname>TrackerSparqlStatement</structname> object represents
 * a SPARQL query. This query may contain parameterized variables
 * (expressed as ~var in the syntax), which may be mapped to arbitrary
 * values prior to execution. This statement may be reused for future
 * queries with different values.
 *
 * The argument bindings may be changed through tracker_sparql_statement_bind_int(),
 * tracker_sparql_statement_bind_boolean(), tracker_sparql_statement_bind_double()
 * and tracker_sparql_statement_bind_string(). Those functions receive
 * a @name argument corresponding for the variable name in the SPARQL query
 * (eg. "var" for ~var) and a @value to map the variable to.
 *
 * Once all arguments have a value, the query may be executed through
 * tracker_sparql_statement_execute() or tracker_sparql_statement_execute_async().
 *
 * This object was added in Tracker 2.2.
 */
public abstract class Tracker.Sparql.Statement : Object {
	public string sparql { get; construct set; }
	public Connection connection { get; construct set; }

	/**
	 * tracker_sparql_statement_bind_int:
	 * @self: a #TrackerSparqlStatement
	 * @name: variable name
	 * @value: value
	 *
	 * Binds the integer @value to variable @name.
	 */
	public abstract void bind_int (string name, int64 value);

	/**
	 * tracker_sparql_statement_bind_boolean:
	 * @self: a #TrackerSparqlStatement
	 * @name: variable name
	 * @value: value
	 *
	 * Binds the boolean @value to variable @name.
	 */
	public abstract void bind_boolean (string name, bool value);

	/**
	 * tracker_sparql_statement_bind_string:
	 * @self: a #TrackerSparqlStatement
	 * @name: variable name
	 * @value: value
	 *
	 * Binds the string @value to variable @name.
	 */
	public abstract void bind_string (string name, string value);

	/**
	 * tracker_sparql_statement_bind_double:
	 * @self: a #TrackerSparqlStatement
	 * @name: variable name
	 * @value: value
	 *
	 * Binds the double @value to variable @name.
	 */
	public abstract void bind_double (string name, double value);

	/**
	 * tracker_sparql_statement_execute:
	 * @cancellable: a #GCancellable used to cancel the operation
	 * @error: #GError for error reporting.
	 *
	 * Executes the SPARQL query with the currently bound values.
	 *
	 * Returns: (transfer full): A #TrackerSparqlCursor
	 */
	public abstract Cursor execute (Cancellable? cancellable) throws Sparql.Error, GLib.Error, GLib.IOError, DBusError;

	/**
	 * tracker_sparql_statement_execute_finish:
	 * @self: a #TrackerSparqlStatement
	 * @_res_: The #GAsyncResult from the callback used to return the #TrackerSparqlCursor
	 * @error: The error which occurred or %NULL
	 *
	 * Finishes the asynchronous operation started through
	 * tracker_sparql_statement_execute_async().
	 *
	 * Returns: (transfer full): A #TrackerSparqlCursor
	 */

	/**
	 * tracker_sparql_statement_execute_async:
	 * @self: a #TrackerSparqlStatement
	 * @cancellable: a #GCancellable used to cancel the operation
	 * @_callback_: user-defined #GAsyncReadyCallback to be called when
	 *              asynchronous operation is finished.
	 * @_user_data_: user-defined data to be passed to @_callback_
	 *
	 * Asynchronously executes the SPARQL query with the currently bound values.
	 */
	public async abstract Cursor execute_async (Cancellable? cancellable) throws Sparql.Error, GLib.Error, GLib.IOError, DBusError;
}
