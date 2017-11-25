/*
 * Copyright (C) 2006, Jamie McCracken <jamiemcc@gnome.org>
 * Copyright (C) 2008-2011, Nokia <ivan.frade@nokia.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 */

[DBus (name = "org.freedesktop.Tracker1.Resources")]
public class Tracker.Resources : Object {
	public const string PATH = "/org/freedesktop/Tracker1/Resources";

	/* I *know* that this is some arbitrary number that doesn't seem to
	 * resemble anything. In fact it's what I experimentally measured to
	 * be a good value on a default Debian testing which has
	 * max_message_size set to 1 000 000 000 in session.conf. I didn't have
	 * the feeling that this value was very much respected, as the size
	 * of the DBusMessage when libdbus decided to exit() the process was
	 * around 160 MB, and not ~ 1000 MB. So if you take 160 MB and you
	 * devide it by 1000000 you have an average string size of ~ 160
	 * bytes plus DBusMessage's overhead. If that makes this number less
	 * arbitrary for you, then fine.
	 *
	 * I really hope that the libdbus people get to their senses and
	 * either stop doing their exit() nonsense in a library, and instead
	 * return a clean DBusError or something, or create crystal clear
	 * clarity about the maximum size of a message. And make it both so
	 * that I can get this length at runtime (without having to parse
	 * libdbus's own configuration files) and my DBusMessage's current
	 * total length. As far as I know are both not possible. So that for
	 * me means that libdbus's exit() is unacceptable.
	 *
	 * Note for the debugger of the future, the "Disconnected" signal gets
	 * sent to us by the bus, which in turn makes libdbus-glib perform exit(). */

	const int DBUS_ARBITRARY_MAX_MSG_SIZE = 10000000;

	DBusConnection connection;

	public signal void writeback ([DBus (signature = "a{iai}")] Variant subjects);
	public signal void graph_updated (string classname, [DBus (signature = "a(iiii)")] Variant deletes, [DBus (signature = "a(iiii)")] Variant inserts);

	public Resources (DBusConnection connection) {
		this.connection = connection;
		Tracker.Store.set_signal_callback (on_emit_signals);
	}

	public async void load (BusName sender, string uri) throws Error {
		var request = DBusRequest.begin (sender, "Resources.Load (uri: '%s')", uri);
		try {
			var file = File.new_for_uri (uri);
			var sparql_conn = Tracker.Main.get_sparql_connection ();

			yield Tracker.Store.queue_turtle_import (sparql_conn, file, sender);

			request.end ();
		} catch (DBInterfaceError.NO_SPACE ie) {
			throw new Sparql.Error.NO_SPACE (ie.message);
		} catch (Error e) {
			request.end (e);
			if (e is Sparql.Error) {
				throw e;
			} else {
				throw new Sparql.Error.INTERNAL (e.message);
			}
		}
	}

	[DBus (signature = "aas")]
	public async Variant sparql_query (BusName sender, string query) throws Error {
		var request = DBusRequest.begin (sender, "Resources.SparqlQuery");
		request.debug ("query: %s", query);
		try {
			var builder = new VariantBuilder ((VariantType) "aas");
			var sparql_conn = Tracker.Main.get_sparql_connection ();

			yield Tracker.Store.sparql_query (sparql_conn, query, Priority.HIGH, cursor => {
				while (cursor.next ()) {
					builder.open ((VariantType) "as");

					for (int i = 0; i < cursor.n_columns; i++) {
						unowned string str = cursor.get_string (i);

						if (str == null) {
							str = "";
						}

						builder.add ("s", str);
					}

					builder.close ();
				}
			}, sender);

			var result = builder.end ();
			if (result.get_size () > DBUS_ARBITRARY_MAX_MSG_SIZE) {
				throw new DBusError.FAILED ("result set of the query is too large");
			}

			request.end ();

			return result;
		} catch (Error e) {
			request.end (e);
			if (e is Sparql.Error) {
				throw e;
			} else {
				throw new Sparql.Error.INTERNAL (e.message);
			}
		}
	}

	public async void sparql_update (BusName sender, string update) throws Error {
		var request = DBusRequest.begin (sender, "Resources.SparqlUpdate");
		request.debug ("query: %s", update);
		try {
			var sparql_conn = Tracker.Main.get_sparql_connection ();
			yield Tracker.Store.sparql_update (sparql_conn, update, Priority.HIGH, sender);

			request.end ();
		} catch (DBInterfaceError.NO_SPACE ie) {
			throw new Sparql.Error.NO_SPACE (ie.message);
		} catch (Error e) {
			request.end (e);
			if (e is Sparql.Error) {
				throw e;
			} else {
				throw new Sparql.Error.INTERNAL (e.message);
			}
		}
	}

	[DBus (signature = "aaa{ss}")]
	public async Variant sparql_update_blank (BusName sender, string update) throws Error {
		var request = DBusRequest.begin (sender, "Resources.SparqlUpdateBlank");
		request.debug ("query: %s", update);
		try {
			var sparql_conn = Tracker.Main.get_sparql_connection ();
			var variant = yield Tracker.Store.sparql_update_blank (sparql_conn, update, Priority.HIGH, sender);

			request.end ();

			return variant;
		} catch (DBInterfaceError.NO_SPACE ie) {
			throw new Sparql.Error.NO_SPACE (ie.message);
		} catch (Error e) {
			request.end (e);
			if (e is Sparql.Error) {
				throw e;
			} else {
				throw new Sparql.Error.INTERNAL (e.message);
			}
		}
	}

	public void sync (BusName sender) {
		var request = DBusRequest.begin (sender, "Resources.Sync");
		var data_manager = Tracker.Main.get_data_manager ();
		var data = data_manager.get_data ();

		var sparql_conn = Tracker.Main.get_sparql_connection ();
		sparql_conn.sync ();

		// sync journal if available
		data.sync ();

		request.end ();
	}

	public async void batch_sparql_update (BusName sender, string update) throws Error {
		var request = DBusRequest.begin (sender, "Resources.BatchSparqlUpdate");
		request.debug ("query: %s", update);
		try {
			var sparql_conn = Tracker.Main.get_sparql_connection ();
			yield Tracker.Store.sparql_update (sparql_conn, update, Priority.LOW, sender);

			request.end ();
		} catch (DBInterfaceError.NO_SPACE ie) {
			throw new Sparql.Error.NO_SPACE (ie.message);
		} catch (Error e) {
			request.end (e);
			if (e is Sparql.Error) {
				throw e;
			} else {
				throw new Sparql.Error.INTERNAL (e.message);
			}
		}
	}

	public void batch_commit () {
		/* no longer needed, just return */
	}

	void emit_graph_updated (Class cl, Events.Batch events) {
		var builder = new VariantBuilder ((VariantType) "a(iiii)");
		events.foreach_delete_event ((graph_id, subject_id, pred_id, object_id) => {
			builder.add ("(iiii)", graph_id, subject_id, pred_id, object_id);
		});
		var deletes = builder.end ();

		builder = new VariantBuilder ((VariantType) "a(iiii)");
		events.foreach_insert_event ((graph_id, subject_id, pred_id, object_id) => {
			builder.add ("(iiii)", graph_id, subject_id, pred_id, object_id);
		});
		var inserts = builder.end ();

		graph_updated (cl.uri, deletes, inserts);
	}

	void emit_writeback (HashTable<int, Array<int>> events) {
		var builder = new VariantBuilder ((VariantType) "a{iai}");
		var wb_iter = HashTableIter<int, GLib.Array<int>> (events);

		int subject_id;
		unowned Array<int> types;
		while (wb_iter.next (out subject_id, out types)) {
			builder.open ((VariantType) "{iai}");

			builder.add ("i", subject_id);

			builder.open ((VariantType) "ai");
			for (int i = 0; i < types.length; i++) {
				builder.add ("i", types.index (i));
			}
			builder.close ();

			builder.close ();
		}

		writeback (builder.end ());
	}

	void on_emit_signals (HashTable<Tracker.Class, Tracker.Events.Batch>? events, HashTable<int, GLib.Array<int>>? writebacks) {
		if (events != null) {
			var iter = HashTableIter<Tracker.Class, Tracker.Events.Batch> (events);
			unowned Events.Batch class_events;
			unowned Class cl;

			while (iter.next (out cl, out class_events)) {
				emit_graph_updated (cl, class_events);
			}
		}

		if (writebacks != null) {
			emit_writeback (writebacks);
		}
	}

	~Resources () {
		Tracker.Store.set_signal_callback (null);
	}

	[DBus (visible = false)]
	public void unreg_batches (string old_owner) {
		Tracker.Store.unreg_batches (old_owner);
	}
}
