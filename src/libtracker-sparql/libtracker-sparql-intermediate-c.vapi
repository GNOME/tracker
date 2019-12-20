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
