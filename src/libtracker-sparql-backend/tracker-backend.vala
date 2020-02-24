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

public static Tracker.Sparql.Connection tracker_sparql_connection_remote_new (string url_base) {
	return new Tracker.Remote.Connection (url_base);
}

public static Tracker.Sparql.Connection tracker_sparql_connection_bus_new (string service, string? object_path, DBusConnection? conn) throws Tracker.Sparql.Error, IOError, DBusError, GLib.Error {
	GLib.DBusConnection dbus_conn;
	string path;

	if (conn != null)
		dbus_conn = conn;
	else
		dbus_conn = GLib.Bus.get_sync (GLib.BusType.SESSION, null);

	if (object_path != null)
		path = object_path;
	else
		path = "/org/freedesktop/Tracker3/Endpoint";

	return new Tracker.Bus.Connection (service, path, dbus_conn);
}

public static Tracker.Sparql.Connection tracker_sparql_connection_new (Tracker.Sparql.ConnectionFlags flags, File? store, File? ontology, Cancellable? cancellable = null) throws GLib.Error, Tracker.Sparql.Error, IOError {
	var conn = new Tracker.Direct.Connection (flags, store, ontology);
	conn.init (cancellable);
	return conn;
}

public static async Tracker.Sparql.Connection tracker_sparql_connection_new_async (Tracker.Sparql.ConnectionFlags flags, File store, File? ontology, Cancellable? cancellable = null) throws GLib.Error, Tracker.Sparql.Error, IOError {
	var conn = new Tracker.Direct.Connection (flags, store, ontology);
	conn.init_async.begin (Priority.DEFAULT, cancellable);
	yield;
	return conn;
}
