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

public class Tracker.Bus.Connection : Tracker.Sparql.Connection {
	DBusConnection bus;
	string dbus_name;
	string object_path;

	private const string DBUS_PEER_IFACE = "org.freedesktop.DBus.Peer";

	private const string PORTAL_NAME = "org.freedesktop.portal.Tracker";
	private const string PORTAL_PATH = "/org/freedesktop/portal/Tracker";
	private const string PORTAL_IFACE = "org.freedesktop.portal.Tracker";

	private const string ENDPOINT_IFACE = "org.freedesktop.Tracker3.Endpoint";

	private const int timeout = 30000;

	public string bus_name {
		get { return dbus_name; }
	}

	public string bus_object_path {
		get { return object_path; }
	}

	public Connection (string dbus_name, string object_path, DBusConnection? dbus_connection) throws Sparql.Error, IOError, DBusError, GLib.Error {
		Object ();
		this.bus = dbus_connection;

		// ensure that error domain is registered with GDBus
		new Sparql.Error.INTERNAL ("");

		var message = new DBusMessage.method_call (dbus_name, object_path, DBUS_PEER_IFACE, "Ping");

		try {
			this.bus.send_message_with_reply_sync (message, 0, timeout, null).to_gerror();
			this.dbus_name = dbus_name;
			this.object_path = object_path;
		} catch (GLib.Error e) {
			if (GLib.FileUtils.test ("/.flatpak-info", GLib.FileTest.EXISTS)) {
				/* We are in a flatpak sandbox, check going through the portal */

				if (object_path == "/org/freedesktop/Tracker1/Endpoint")
					object_path = null;

				string uri = Tracker.util_build_dbus_uri (GLib.BusType.SESSION, dbus_name, object_path);
				message = new DBusMessage.method_call (PORTAL_NAME, PORTAL_PATH, PORTAL_IFACE, "CreateSession");
				message.set_body (new Variant ("(s)", uri));

				var reply = this.bus.send_message_with_reply_sync (message, 0, timeout, null);

				reply.to_gerror();

				var variant = reply.get_body ();
				variant.get_child(0, "o", out object_path);

				this.dbus_name = PORTAL_NAME;
				this.object_path = object_path;
			} else {
				throw e;
			}
		}
	}

	static void pipe (out UnixInputStream input, out UnixOutputStream output) throws IOError {
		int pipefd[2];
		if (Posix.pipe (pipefd) < 0) {
			throw new IOError.FAILED ("Pipe creation failed");
		}
		input = new UnixInputStream (pipefd[0], true);
		output = new UnixOutputStream (pipefd[1], true);
	}

	static void handle_error_reply (DBusMessage message) throws Sparql.Error, IOError, DBusError {
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

	static void send_query (DBusConnection bus, string dbus_name, string object_path, string sparql, VariantBuilder? arguments, UnixOutputStream output, Cancellable? cancellable, AsyncReadyCallback? callback) throws GLib.IOError, GLib.Error {
		var message = new DBusMessage.method_call (dbus_name, object_path, ENDPOINT_IFACE, "Query");
		var fd_list = new UnixFDList ();
		message.set_body (new Variant ("(sha{sv})", sparql, fd_list.append (output.fd), arguments));
		message.set_unix_fd_list (fd_list);

		bus.send_message_with_reply.begin (message, DBusSendMessageFlags.NONE, int.MAX, null, cancellable, callback);
	}

	public static async Sparql.Cursor perform_query_call (DBusConnection bus, string dbus_name, string object_path, string sparql, VariantBuilder? arguments, Cancellable? cancellable) throws GLib.IOError, GLib.Error {
		UnixInputStream input;
		UnixOutputStream output;
		pipe (out input, out output);

		// send D-Bus request
		AsyncResult dbus_res = null;
		bool received_result = false;
		send_query (bus, dbus_name, object_path, sparql, arguments, output, cancellable, (o, res) => {
			dbus_res = res;
			if (received_result) {
				perform_query_call.callback ();
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

	public override Sparql.Cursor query (string sparql, Cancellable? cancellable) throws Sparql.Error, GLib.Error, GLib.IOError, DBusError {
		// use separate main context for sync operation
		var context = new MainContext ();
		var loop = new MainLoop (context, false);
		context.push_thread_default ();
		AsyncResult async_res = null;
		query_async.begin (sparql, cancellable, (o, res) => {
			async_res = res;
			loop.quit ();
		});
		loop.run ();
		context.pop_thread_default ();
		return query_async.end (async_res);
	}

	public async override Sparql.Cursor query_async (string sparql, Cancellable? cancellable = null) throws Sparql.Error, GLib.Error, GLib.IOError, DBusError {
		return yield perform_query_call (bus, dbus_name, object_path, sparql, null, cancellable);
	}

	public override Sparql.Statement? query_statement (string sparql, GLib.Cancellable? cancellable = null) throws Sparql.Error {
		return new Bus.Statement (bus, dbus_name, object_path, sparql);
	}

	void send_update (string method, UnixInputStream input, Cancellable? cancellable, AsyncReadyCallback? callback) throws GLib.Error, GLib.IOError {
		var message = new DBusMessage.method_call (dbus_name, object_path, ENDPOINT_IFACE, method);
		var fd_list = new UnixFDList ();
		message.set_body (new Variant ("(h)", fd_list.append (input.fd)));
		message.set_unix_fd_list (fd_list);

		bus.send_message_with_reply.begin (message, DBusSendMessageFlags.NONE, int.MAX, null, cancellable, callback);
	}

	public override void update (string sparql, Cancellable? cancellable = null) throws Sparql.Error, GLib.Error, GLib.IOError, DBusError {
		// use separate main context for sync operation
		var context = new MainContext ();
		var loop = new MainLoop (context, false);
		context.push_thread_default ();
		AsyncResult async_res = null;
		update_async.begin (sparql, cancellable, (o, res) => {
			async_res = res;
			loop.quit ();
		});
		loop.run ();
		context.pop_thread_default ();
		update_async.end (async_res);
	}

	public async override void update_async (string sparql, Cancellable? cancellable = null) throws Sparql.Error, GLib.Error, GLib.IOError, DBusError {
		UnixInputStream input;
		UnixOutputStream output;
		pipe (out input, out output);

		// send D-Bus request
		AsyncResult dbus_res = null;
		bool sent_update = false;
		send_update ("Update", input, cancellable, (o, res) => {
			dbus_res = res;
			if (sent_update) {
				update_async.callback ();
			}
		});

		// send sparql string via fd
		var data_stream = new DataOutputStream (output);
		data_stream.set_byte_order (DataStreamByteOrder.HOST_ENDIAN);
		data_stream.put_int32 ((int32) sparql.length);
		data_stream.put_string (sparql);
		data_stream = null;

		// wait for D-Bus reply
		sent_update = true;
		if (dbus_res == null) {
			yield;
		}

		var reply = bus.send_message_with_reply.end (dbus_res);
		handle_error_reply (reply);
	}

	public async override bool update_array_async (string[] sparql, Cancellable? cancellable = null) throws Sparql.Error, GLib.Error, GLib.IOError, DBusError {
		UnixInputStream input;
		UnixOutputStream output;
		pipe (out input, out output);

		// send D-Bus request
		AsyncResult dbus_res = null;
		bool sent_update = false;
		send_update ("UpdateArray", input, cancellable, (o, res) => {
			dbus_res = res;
			if (sent_update) {
				update_array_async.callback ();
			}
		});

		// send sparql strings via fd
		var data_stream = new DataOutputStream (output);
		data_stream.set_byte_order (DataStreamByteOrder.HOST_ENDIAN);
		data_stream.put_int32 ((int32) sparql.length);
		for (int i = 0; i < sparql.length; i++) {
			data_stream.put_int32 ((int32) sparql[i].length);
			data_stream.put_string (sparql[i]);
		}
		data_stream = null;

		// wait for D-Bus reply
		sent_update = true;
		if (dbus_res == null) {
			yield;
		}

		var reply = bus.send_message_with_reply.end (dbus_res);
		handle_error_reply (reply);

                return true;
	}

	public override GLib.Variant? update_blank (string sparql, Cancellable? cancellable = null) throws Sparql.Error, GLib.Error, GLib.IOError, DBusError {
		// use separate main context for sync operation
		var context = new MainContext ();
		var loop = new MainLoop (context, false);
		context.push_thread_default ();
		AsyncResult async_res = null;
		update_blank_async.begin (sparql, cancellable, (o, res) => {
			async_res = res;
			loop.quit ();
		});
		loop.run ();
		context.pop_thread_default ();
		return update_blank_async.end (async_res);
	}

	public async override GLib.Variant? update_blank_async (string sparql, Cancellable? cancellable = null) throws Sparql.Error, GLib.Error, GLib.IOError, DBusError {
		UnixInputStream input;
		UnixOutputStream output;
		pipe (out input, out output);

		// send D-Bus request
		AsyncResult dbus_res = null;
		bool sent_update = false;
		send_update ("UpdateBlank", input, cancellable, (o, res) => {
			dbus_res = res;
			if (sent_update) {
				update_blank_async.callback ();
			}
		});

		// send sparql strings via fd
		var data_stream = new DataOutputStream (output);
		data_stream.set_byte_order (DataStreamByteOrder.HOST_ENDIAN);
		data_stream.put_int32 ((int32) sparql.length);
		data_stream.put_string (sparql);
		data_stream = null;

		// wait for D-Bus reply
		sent_update = true;
		if (dbus_res == null) {
			yield;
		}

		var reply = bus.send_message_with_reply.end (dbus_res);
		handle_error_reply (reply);
		return reply.get_body ().get_child_value (0);
	}

	public override Tracker.Notifier? create_notifier () {
		var notifier = (Tracker.Notifier) Object.new (typeof (Tracker.Notifier),
		                                              "connection", this,
		                                              null);

		notifier.signal_subscribe (this.bus, this.dbus_name, null, null);

		return notifier;
	}

	public override void close () {
	}

	public async override bool close_async () throws GLib.IOError {
		return true;
	}
}
