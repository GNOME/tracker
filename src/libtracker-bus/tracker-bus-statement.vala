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

	void pipe (out UnixInputStream input, out UnixOutputStream output) throws IOError {
		int pipefd[2];
		if (Posix.pipe (pipefd) < 0) {
			throw new IOError.FAILED ("Pipe creation failed");
		}
		input = new UnixInputStream (pipefd[0], true);
		output = new UnixOutputStream (pipefd[1], true);
	}

	void handle_error_reply (DBusMessage message) throws Sparql.Error, IOError, DBusError {
		try {
			message.to_gerror ();
		} catch (IOError e_io) {
			throw e_io;
		} catch (Sparql.Error e_sparql) {
			throw e_sparql;
		} catch (DBusError e_dbus) {
			throw e_dbus;
		} catch (Error e) {
			throw new IOError.FAILED (e.message);
		}
	}

	private void send_query (string sparql, UnixOutputStream output, Cancellable? cancellable, AsyncReadyCallback? callback) throws GLib.IOError, GLib.Error {
		var message = new DBusMessage.method_call (dbus_name, object_path, ENDPOINT_IFACE, "Query");
		var fd_list = new UnixFDList ();
		message.set_body (new Variant ("(sha{sv})", sparql, fd_list.append (output.fd), get_arguments ()));
		message.set_unix_fd_list (fd_list);

		bus.send_message_with_reply.begin (message, DBusSendMessageFlags.NONE, int.MAX, null, cancellable, callback);
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
		UnixInputStream input;
		UnixOutputStream output;
		pipe (out input, out output);

		// send D-Bus request
		AsyncResult dbus_res = null;
		bool received_result = false;
		send_query (query, output, cancellable, (o, res) => {
			dbus_res = res;
			if (received_result) {
				execute_async.callback ();
			}
		});

		output = null;

		// receive query results via FD
		var mem_stream = new MemoryOutputStream (null, GLib.realloc, GLib.free);

		try {
			yield mem_stream.splice_async (input, OutputStreamSpliceFlags.CLOSE_SOURCE | OutputStreamSpliceFlags.CLOSE_TARGET, Priority.DEFAULT, cancellable);
		} finally {
			// wait for D-Bus reply
			received_result = true;
			if (dbus_res == null) {
				yield;
			}
		}

		var reply = bus.send_message_with_reply.end (dbus_res);
		handle_error_reply (reply);

		string[] variable_names = (string[]) reply.get_body ().get_child_value (0);
		mem_stream.close ();
		return new FDCursor (mem_stream.steal_data (), mem_stream.data_size, variable_names);
	}
}
