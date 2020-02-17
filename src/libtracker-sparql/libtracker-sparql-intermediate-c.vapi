/*
 * Copyright (C) 2017 - Red Hat Inc
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

namespace Tracker {
	[CCode (cheader_filename = "libtracker-sparql/tracker-connection.h")]
	public errordomain Sparql.Error {
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

	[CCode (cheader_filename = "libtracker-sparql/tracker-connection.h")]
	public enum Sparql.ConnectionFlags {
		NONE     = 0,
		READONLY = 1 << 0,
	}

	[CCode (cheader_filename = "libtracker-sparql/tracker-cursor.h")]
	public enum Sparql.ValueType {
		UNBOUND,
		URI,
		STRING,
		INTEGER,
		DOUBLE,
		DATETIME,
		BLANK_NODE,
		BOOLEAN
	}

	namespace Sparql {
		[CCode (cheader_filename = "libtracker-sparql/tracker-sparql.h")]
		public static string escape_string (string literal);
		[CCode (cheader_filename = "libtracker-sparql/tracker-sparql.h")]
		public static string escape_uri (string uri);
		[CCode (cheader_filename = "libtracker-sparql/tracker-sparql.h")]
		public static string escape_uri_printf (string format, ...);
		[CCode (cheader_filename = "libtracker-sparql/tracker-sparql.h")]
		public static string escape_uri_vprintf (string format, va_list args);
		[CCode (cheader_filename = "libtracker-sparql/tracker-sparql.h")]
		public static string get_uuid_urn ();
	}

	[CCode (cheader_filename = "libtracker-sparql/tracker-connection.h")]
        public abstract class Sparql.Connection : GLib.Object {
		public extern static new Connection remote_new (string uri_base);
		public extern static new Connection new (Sparql.ConnectionFlags flags, GLib.File store, GLib.File? ontology, GLib.Cancellable? cancellable = null) throws Sparql.Error, GLib.IOError;
		public extern async static new Connection new_async (Sparql.ConnectionFlags flags, GLib.File store, GLib.File? ontology, GLib.Cancellable? cancellable = null) throws Sparql.Error, GLib.IOError;
		public extern static new Connection bus_new (string service_name, string? object_path, GLib.DBusConnection? dbus_connection = null) throws Sparql.Error, GLib.IOError, GLib.DBusError, GLib.Error;

		public abstract Cursor query (string sparql, GLib.Cancellable? cancellable = null) throws Sparql.Error, GLib.Error, GLib.IOError, GLib.DBusError;
		public async abstract Cursor query_async (string sparql, GLib.Cancellable? cancellable = null) throws Sparql.Error, GLib.Error, GLib.IOError, GLib.DBusError;

		public virtual void update (string sparql, int priority = GLib.Priority.DEFAULT, GLib.Cancellable? cancellable = null) throws Sparql.Error, GLib.Error, GLib.IOError, GLib.DBusError;
		public async virtual void update_async (string sparql, int priority = GLib.Priority.DEFAULT, GLib.Cancellable? cancellable = null) throws Sparql.Error, GLib.Error, GLib.IOError, GLib.DBusError;
		public async virtual bool update_array_async (string[] sparql, int priority = GLib.Priority.DEFAULT, GLib.Cancellable? cancellable = null) throws Sparql.Error, GLib.Error, GLib.IOError, GLib.DBusError;
		public virtual GLib.Variant? update_blank (string sparql, int priority = GLib.Priority.DEFAULT, GLib.Cancellable? cancellable = null) throws Sparql.Error, GLib.Error, GLib.IOError, GLib.DBusError;
		public async virtual GLib.Variant? update_blank_async (string sparql, int priority = GLib.Priority.DEFAULT, GLib.Cancellable? cancellable = null) throws Sparql.Error, GLib.Error, GLib.IOError, GLib.DBusError;

		public virtual NamespaceManager? get_namespace_manager ();

		public extern static void set_domain (string? domain);
		public extern static string? get_domain ();

		public extern static void set_dbus_connection (GLib.DBusConnection dbus_connection);
		public extern static GLib.DBusConnection? get_dbus_connection ();

		public virtual Statement? query_statement (string sparql, GLib.Cancellable? cancellable = null) throws Sparql.Error;

                public virtual Notifier? create_notifier (NotifierFlags flags);
                public virtual void close ();
                public async virtual bool close_async () throws GLib.IOError;
	}

	[CCode (cheader_filename = "libtracker-sparql/tracker-statement.h")]
	public abstract class Sparql.Statement : GLib.Object {
		public string sparql { get; construct set; }
		public Connection connection { get; construct set; }

		public abstract void bind_int (string name, int64 value);
		public abstract void bind_boolean (string name, bool value);
		public abstract void bind_string (string name, string value);
		public abstract void bind_double (string name, double value);

		public abstract Cursor execute (GLib.Cancellable? cancellable) throws Sparql.Error, GLib.Error, GLib.IOError, GLib.DBusError;
		public async abstract Cursor execute_async (GLib.Cancellable? cancellable) throws Sparql.Error, GLib.Error, GLib.IOError, GLib.DBusError;
	}

	[CCode (cheader_filename = "libtracker-sparql/tracker-cursor.h")]
	public abstract class Sparql.Cursor : GLib.Object {
		public Connection connection {
			get;
			set;
		}

		public abstract int n_columns {
			get;
		}

		public abstract Sparql.ValueType get_value_type (int column);

		public abstract unowned string? get_variable_name (int column);
		public abstract unowned string? get_string (int column, out long length = null);

		public abstract bool next (GLib.Cancellable? cancellable = null) throws GLib.Error;
		public async abstract bool next_async (GLib.Cancellable? cancellable = null) throws GLib.Error;
		public abstract void rewind ();
		public virtual void close ();

		public virtual int64 get_integer (int column);
		public virtual double get_double (int column);
		public virtual bool get_boolean (int column);
		public virtual bool is_bound (int column);
	}

	[CCode (cheader_filename = "libtracker-sparql/tracker-namespace-manager.h")]
	public class NamespaceManager : GLib.Object {
		public NamespaceManager ();
		public void add_prefix (string prefix, string namespace);
		public bool has_prefix (string prefix);
		public string lookup_prefix (string prefix);
		public string expand_uri (string compact_uri);

		public static NamespaceManager get_default ();
	}

	[CCode (cheader_filename = "libtracker-sparql/tracker-resource.h")]
	public class Resource : GLib.Object {
		public Resource (string identifier);

		public void set_value (string predicate, GLib.Value value);
		public void set_boolean (string predicate, bool object);
		public void set_double (string predicate, double object);
		public void set_int (string predicate, int object);
		public void set_int64 (string predicate, int64 object);
		public void set_relation (string predicate, Resource object);
		public void set_string (string predicate, string object);
		public void set_uri (string predicate, string object);

		public void add_value (string predicate, GLib.Value value);
		public void add_boolean (string predicate, bool object);
		public void add_double (string predicate, double object);
		public void add_int (string predicate, int object);
		public void add_int64 (string predicate, int64 object);
		public void add_relation (string predicate, Resource object);
		public void add_string (string predicate, string object);
		public void add_uri (string predicate, string object);

		public GLib.List<GLib.Value?> get_values (string predicate);

		public bool get_first_boolean (string predicate);
		public double get_first_double (string predicate);
		public int get_first_int (string predicate);
		public int64 get_first_int64 (string predicate);
		public unowned Resource get_first_relation (string predicate);
		public string get_first_string (string predicate);
		public string get_first_uri (string predicate);

		public string get_identifier ();
		public void set_identifier (string identifier);

		public int identifier_compare_func (string identifier);

		public string print_turtle (NamespaceManager? namespace_manager);
		public string print_sparql_update (NamespaceManager? namespace_manager, string graph);
	}

	[CCode (cprefix = "TRACKER_NOTIFIER_FLAG_", cheader_filename = "libtracker-sparql/tracker-notifier.h")]
	public enum NotifierFlags {
		QUERY_URN,
		QUERY_LOCATION,
		NOTIFY_UNEXTRACTED
	}

	public enum NotifierEventType {
		QUERY_URN,
		QUERY_LOCATION,
		NOTIFY_UNEXTRACTED
	}

	[CCode (cheader_filename = "libtracker-sparql/tracker-notifier.h")]
	public class Notifier : GLib.Object {
		public class NotifierEvent {
			public enum Type {
				CREATE,
				DELETE,
				UPDATE
			}

			public int64 get_id ();
			public string get_type ();
			public string get_urn ();
			public string get_location ();
		}

		public uint signal_subscribe (GLib.DBusConnection dbus_conn, string dbus_name, string? graph);
		public void signal_unsubscribe (uint handler_id);
	}

	[CCode (cheader_filename = "libtracker-sparql/tracker-endpoint.h")]
	public class Endpoint : GLib.Object {
		Sparql.Connection get_sparql_connection ();
	}

	[CCode (cheader_filename = "libtracker-sparql/tracker-endpoint-dbus.h")]
	public class EndpointDBus : GLib.Object, GLib.Initable {
		public EndpointDBus (Sparql.Connection sparql_conn, GLib.DBusConnection? dbus_conn, string object_path, GLib.Cancellable? cancellable) throws GLib.Error;
	}
}
