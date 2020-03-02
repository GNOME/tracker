/*
 * Copyright (C) 2020, Red Hat Ltd.
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
 *
 * Author: Carlos Garnacho <carlosg@gnome.org>
 */

public class Tracker.Bus.Statement : Tracker.Sparql.Statement {
	private DBusConnection bus;
	private string query;
	private string dbus_name;
	private string object_path;
	private HashTable<string,GLib.Variant> arguments;

	private const string ENDPOINT_IFACE = "org.freedesktop.Tracker1.Endpoint";

	public Statement (DBusConnection bus, string dbus_name, string object_path, string query) {
		Object ();
		this.bus = bus;
		this.dbus_name = dbus_name;
		this.object_path = object_path;
		this.query = query;
		this.arguments = new HashTable<string, GLib.Variant> (str_hash, str_equal);
	}

	public override void bind_boolean (string name, bool value) {
		this.arguments.insert (name, new GLib.Variant.boolean (value));
	}

	public override void bind_double (string name, double value) {
		this.arguments.insert (name, new GLib.Variant.double (value));
	}

	public override void bind_int (string name, int64 value) {
		this.arguments.insert (name, new GLib.Variant.int64 (value));
	}

	public override void bind_string (string name, string value) {
		this.arguments.insert (name, new GLib.Variant.string (value));
	}

	public override void clear_bindings () {
		this.arguments.remove_all ();
	}

	private VariantBuilder? get_arguments () {
		if (this.arguments.size () == 0)
			return null;

		VariantBuilder builder = new VariantBuilder (new VariantType ("a{sv}"));
		HashTableIter<string, Variant> iter = HashTableIter<string, Variant> (this.arguments);
		unowned string arg;
		unowned GLib.Variant value;

		while (iter.next (out arg, out value))
			builder.add ("{sv}", arg, value);

		return builder;
	}

	public override Sparql.Cursor execute (GLib.Cancellable? cancellable) throws Sparql.Error, GLib.Error, GLib.IOError, GLib.DBusError {
		// use separate main context for sync operation
		var context = new MainContext ();
		var loop = new MainLoop (context, false);
		context.push_thread_default ();
		AsyncResult async_res = null;
		execute_async.begin (cancellable, (o, res) => {
			async_res = res;
			loop.quit ();
		});
		loop.run ();
		context.pop_thread_default ();
		return execute_async.end (async_res);
	}

	public async override Sparql.Cursor execute_async (GLib.Cancellable? cancellable) throws Sparql.Error, GLib.Error, GLib.IOError, GLib.DBusError {
		return yield Tracker.Bus.Connection.perform_query_call (bus, dbus_name, object_path, query, get_arguments (), cancellable);
	}
}
