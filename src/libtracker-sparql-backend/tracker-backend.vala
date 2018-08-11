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

static string domain_name = null;
static Tracker.DomainOntology domain_ontology = null;
static DBusConnection global_dbus_connection = null;

class Tracker.Sparql.Backend : Connection {
	bool initialized;
	Tracker.Sparql.Connection direct = null;
	Tracker.Sparql.Connection bus = null;
	enum Backend {
		AUTO,
		DIRECT,
		BUS
	}

	public Backend () throws Sparql.Error, IOError, DBusError, SpawnError {
		try {
			domain_ontology = new Tracker.DomainOntology (domain_name, null);
			load_plugins ();
		} catch (GLib.Error e) {
			throw new Sparql.Error.INTERNAL ("Failed to load SPARQL backend: " + e.message);
		}

		initialized = true;
	}

	public override void dispose () {
		// trying to lock on partially initialized instances will deadlock
		if (initialized) {
			door.lock ();

			try {
				// Ensure this instance is not used for any new calls to Tracker.Sparql.Connection.get.
				// However, a call to Tracker.Sparql.Connection.get between g_object_unref and the
				// above lock might have increased the reference count of this instance to 2 (or more).
				// Therefore, we must not clean up direct/bus connection in dispose.
				if (singleton == this) {
					singleton = null;
				}
			} finally {
				door.unlock ();
			}
		}

		base.dispose ();
	}

	public override Cursor query (string sparql, Cancellable? cancellable = null) throws Sparql.Error, IOError, DBusError, GLib.Error {
		debug ("%s(): '%s'", GLib.Log.METHOD, sparql);
		if (direct != null) {
			return direct.query (sparql, cancellable);
		} else {
			return bus.query (sparql, cancellable);
		}
	}

	public async override Cursor query_async (string sparql, Cancellable? cancellable = null) throws Sparql.Error, IOError, DBusError, GLib.Error {
		debug ("%s(): '%s'", GLib.Log.METHOD, sparql);
		if (direct != null) {
			return yield direct.query_async (sparql, cancellable);
		} else {
			return yield bus.query_async (sparql, cancellable);
		}
	}

	public override void update (string sparql, int priority = GLib.Priority.DEFAULT, Cancellable? cancellable = null) throws Sparql.Error, IOError, DBusError, GLib.Error {
		debug ("%s(priority:%d): '%s'", GLib.Log.METHOD, priority, sparql);
		if (bus == null) {
			throw new Sparql.Error.UNSUPPORTED ("Update support not available for direct-only connection");
		}
		bus.update (sparql, priority, cancellable);
	}

	public override GLib.Variant? update_blank (string sparql, int priority = GLib.Priority.DEFAULT, Cancellable? cancellable = null) throws Sparql.Error, IOError, DBusError, GLib.Error {
		debug ("%s(priority:%d): '%s'", GLib.Log.METHOD, priority, sparql);
		if (bus == null) {
			throw new Sparql.Error.UNSUPPORTED ("Update support not available for direct-only connection");
		}
		return bus.update_blank (sparql, priority, cancellable);
	}

	public async override void update_async (string sparql, int priority = GLib.Priority.DEFAULT, Cancellable? cancellable = null) throws Sparql.Error, IOError, DBusError, GLib.Error {
		debug ("%s(priority:%d): '%s'", GLib.Log.METHOD, priority, sparql);
		if (bus == null) {
			throw new Sparql.Error.UNSUPPORTED ("Update support not available for direct-only connection");
		}
		yield bus.update_async (sparql, priority, cancellable);
	}

	public async override GenericArray<Sparql.Error?>? update_array_async (string[] sparql, int priority = GLib.Priority.DEFAULT, Cancellable? cancellable = null) throws Sparql.Error, IOError, DBusError, GLib.Error {
		if (bus == null) {
			throw new Sparql.Error.UNSUPPORTED ("Update support not available for direct-only connection");
		}
		return yield bus.update_array_async (sparql, priority, cancellable);
	}

	public async override GLib.Variant? update_blank_async (string sparql, int priority = GLib.Priority.DEFAULT, Cancellable? cancellable = null) throws Sparql.Error, IOError, DBusError, GLib.Error {
		debug ("%s(priority:%d): '%s'", GLib.Log.METHOD, priority, sparql);
		if (bus == null) {
			throw new Sparql.Error.UNSUPPORTED ("Update support not available for direct-only connection");
		}
		return yield bus.update_blank_async (sparql, priority, cancellable);
	}

	public override void load (File file, Cancellable? cancellable = null) throws Sparql.Error, IOError, DBusError {
		var uri = file.get_uri ();
		debug ("%s(): '%s'", GLib.Log.METHOD, uri);
		if (bus == null) {
			throw new Sparql.Error.UNSUPPORTED ("Update support not available for direct-only connection");
		}
		bus.load (file, cancellable);
	}

	public async override void load_async (File file, Cancellable? cancellable = null) throws Sparql.Error, IOError, DBusError {
		var uri = file.get_uri ();
		debug ("%s(): '%s'", GLib.Log.METHOD, uri);
		if (bus == null) {
			throw new Sparql.Error.UNSUPPORTED ("Update support not available for direct-only connection");
		}
		yield bus.load_async (file, cancellable);
	}

	public override Cursor? statistics (Cancellable? cancellable = null) throws Sparql.Error, IOError, DBusError {
		debug ("%s()", GLib.Log.METHOD);
		if (bus == null) {
			throw new Sparql.Error.UNSUPPORTED ("Statistics support not available for direct-only connection");
		}
		return bus.statistics (cancellable);
	}

	public async override Cursor? statistics_async (Cancellable? cancellable = null) throws Sparql.Error, IOError, DBusError {
		debug ("%s()", GLib.Log.METHOD);
		if (bus == null) {
			throw new Sparql.Error.UNSUPPORTED ("Statistics support not available for direct-only connection");
		}
		return yield bus.statistics_async (cancellable);
	}

	public override NamespaceManager? get_namespace_manager () {
		if (direct != null)
			return direct.get_namespace_manager ();
		else
			return NamespaceManager.get_default ();
	}

	private Connection create_readonly_direct () throws GLib.Error, Sparql.Error, IOError, DBusError {
		var conn = new Tracker.Direct.Connection (Tracker.Sparql.ConnectionFlags.READONLY,
		                                          domain_ontology.get_cache (),
		                                          domain_ontology.get_journal (),
		                                          domain_ontology.get_ontology ());
		conn.init ();
		return conn;
	}

	// Plugin loading functions
	private void load_plugins () throws GLib.Error {
		string env_backend = Environment.get_variable ("TRACKER_SPARQL_BACKEND");
		Backend backend = Backend.AUTO;

		if (env_backend != null) {
			if (env_backend.ascii_casecmp ("direct") == 0) {
				backend = Backend.DIRECT;
				debug ("Using backend = 'DIRECT'");
			} else if (env_backend.ascii_casecmp ("bus") == 0) {
				backend = Backend.BUS;
				debug ("Using backend = 'BUS'");
			} else {
				warning ("Environment variable TRACKER_SPARQL_BACKEND set to unknown value '%s'", env_backend);
			}
		}

		if (backend == Backend.AUTO) {
			debug ("Using backend = 'AUTO'");
		}

		switch (backend) {
		case Backend.AUTO:
			bus = new Tracker.Bus.Connection (domain_ontology.get_domain ("Tracker1"), global_dbus_connection);

			try {
				direct = create_readonly_direct ();
			} catch (GLib.Error e) {
				warning ("Falling back to bus backend, the direct backend failed to initialize: " + e.message);
			}

			break;

		case Backend.DIRECT:
			direct = create_readonly_direct ();
			break;

		case Backend.BUS:
			bus = new Tracker.Bus.Connection (domain_ontology.get_domain ("Tracker1"), global_dbus_connection);
			break;

		default:
			assert_not_reached ();
		}
	}

	static weak Connection? singleton;
	static Mutex door;

	static new Connection get (Cancellable? cancellable = null) throws Sparql.Error, IOError, DBusError, SpawnError {
		door.lock ();

		try {
			// assign to owned variable to ensure it doesn't get freed between check and return
			var result = singleton;

			if (result == null) {
				result = new Tracker.Sparql.Backend ();

				if (cancellable != null && cancellable.is_cancelled ()) {
					throw new IOError.CANCELLED ("Operation was cancelled");
				}

				singleton = result;
			}

			return result;
		} finally {
			door.unlock ();
		}
	}

	public static new Connection get_internal (Cancellable? cancellable = null) throws Sparql.Error, IOError, DBusError, SpawnError {
		if (MainContext.get_thread_default () == null) {
			// ok to initialize without extra thread
			return get (cancellable);
		}

		// run with separate main context to be able to wait for async method
		var context = new MainContext ();
		var loop = new MainLoop (context);
		AsyncResult async_result = null;

		context.push_thread_default ();

		get_internal_async.begin (cancellable, (obj, res) => {
			async_result = res;
			loop.quit ();
		});

		loop.run ();

		context.pop_thread_default ();

		return get_internal_async.end (async_result);
	}

	public async static new Connection get_internal_async (Cancellable? cancellable = null) throws Sparql.Error, IOError, DBusError, SpawnError {
		// fast path: avoid extra thread if connection is already available
		if (door.trylock ()) {
			// assign to owned variable to ensure it doesn't get freed between unlock and return
			var result = singleton;

			door.unlock ();

			if (result != null) {
				return result;
			}
		}

		// run in a separate thread
		Sparql.Error sparql_error = null;
		IOError io_error = null;
		DBusError dbus_error = null;
		SpawnError spawn_error = null;
		Connection result = null;
		var context = MainContext.get_thread_default ();

		IOSchedulerJob.push (job => {
			try {
				result = get (cancellable);
			} catch (IOError e_io) {
				io_error = e_io;
			} catch (Sparql.Error e_spql) {
				sparql_error = e_spql;
			} catch (DBusError e_dbus) {
				dbus_error = e_dbus;
			} catch (SpawnError e_spawn) {
				spawn_error = e_spawn;
			}

			var source = new IdleSource ();
			source.set_callback (() => {
				get_internal_async.callback ();
				return false;
			});
			source.attach (context);

			return false;
		}, GLib.Priority.DEFAULT);
		yield;

		if (sparql_error != null) {
			throw sparql_error;
		} else if (io_error != null) {
			throw io_error;
		} else if (dbus_error != null) {
			throw dbus_error;
		} else if (spawn_error != null) {
			throw spawn_error;
		} else {
			return result;
		}
	}
}

public async static Tracker.Sparql.Connection tracker_sparql_connection_get_async (Cancellable? cancellable = null) throws Tracker.Sparql.Error, IOError, DBusError, SpawnError {
	return yield Tracker.Sparql.Backend.get_internal_async (cancellable);
}

public static Tracker.Sparql.Connection tracker_sparql_connection_get (Cancellable? cancellable = null) throws Tracker.Sparql.Error, IOError, DBusError, SpawnError {
	return Tracker.Sparql.Backend.get_internal (cancellable);
}

public static Tracker.Sparql.Connection tracker_sparql_connection_remote_new (string url_base) {
	return new Tracker.Remote.Connection (url_base);
}

public static Tracker.Sparql.Connection tracker_sparql_connection_local_new (Tracker.Sparql.ConnectionFlags flags, File store, File? journal, File? ontology, Cancellable? cancellable = null) throws GLib.Error, Tracker.Sparql.Error, IOError {
	var conn = new Tracker.Direct.Connection (flags, store, journal, ontology);
	conn.init (cancellable);
	return conn;
}

public static async Tracker.Sparql.Connection tracker_sparql_connection_local_new_async (Tracker.Sparql.ConnectionFlags flags, File store, File? journal, File? ontology, Cancellable? cancellable = null) throws GLib.Error, Tracker.Sparql.Error, IOError {
	var conn = new Tracker.Direct.Connection (flags, store, journal, ontology);
	conn.init_async.begin (Priority.DEFAULT, cancellable);
	yield;
	return conn;
}

public static void tracker_sparql_connection_set_domain (string? domain) {
	if (domain_name == null)
		domain_name = domain;
}

public static string? tracker_sparql_connection_get_domain () {
	return domain_name;
}

public static void tracker_sparql_connection_set_dbus_connection (DBusConnection dbus_connection) {
	global_dbus_connection = dbus_connection;
}

public static DBusConnection? tracker_sparql_connection_get_dbus_connection () {
	return global_dbus_connection;
}
