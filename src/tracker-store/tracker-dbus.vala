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

public class Tracker.DBus {
	static DBusConnection connection;

	const string SERVICE = "org.freedesktop.Tracker1";

	static uint name_owner_changed_id;
	static Tracker.Statistics statistics;
	static uint statistics_id;
	static Tracker.Resources resources;
	static uint resources_id;
	static Tracker.Steroids steroids;
	static uint steroids_id;
	static Tracker.Status notifier;
	static uint notifier_id;
	static Tracker.Backup backup;
	static uint backup_id;
	static uint domain_watch_id;
	static MainLoop watch_main_loop;

	static bool dbus_register_service (string name) {
		message ("Registering D-Bus service...\n  Name:'%s'", name);

		try {
			Variant reply = connection.call_sync ("org.freedesktop.DBus",
				"/org/freedesktop/DBus",
				"org.freedesktop.DBus", "RequestName",
				new Variant ("(su)", name, 1 << 2 /* DBUS_NAME_FLAG_DO_NOT_QUEUE */),
				(VariantType) "(u)",
				0, -1);

			uint result;
			reply.get ("(u)", out result);
			if (result != 1 /* DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER */) {
				critical ("D-Bus service name:'%s' is already taken, " +
				          "perhaps the daemon is already running?",
					  name);
				return false;
			}

			return true;
		} catch (Error e) {
			critical ("Could not aquire name:'%s', %s", name, e.message);
			return false;
		}
	}

	static uint register_object<T> (DBusConnection lconnection, T object, string path) {
		message ("Registering D-Bus object...");
		message ("  Path:'%s'", path);
		message ("  Type:'%s'", typeof (T).name ());

		try {
			uint id = lconnection.register_object (path, object);
			return id;
		} catch (Error e) {
			critical ("Could not register D-Bus object: %s", e.message);
			return 0;
		}
	}

	public static bool register_names (string? domain) {
		string service_name;

		if (domain != null) {
			service_name = domain;
		} else {
			/* Register the service name for org.freedesktop.Tracker */
			service_name = SERVICE;
		}

		return dbus_register_service (service_name);
	}

	public static void on_domain_name_disappeared (DBusConnection connection, string name) {
		notifier.wait.begin ();
		yield;
		watch_main_loop.quit ();
	}

	public static void watch_domain (string? domain, MainLoop main_loop) {
		if (domain_watch_id == 0 && domain != null) {
			watch_main_loop = main_loop;
			domain_watch_id = GLib.Bus.watch_name_on_connection (connection,
			                                                     domain,
			                                                     GLib.BusNameWatcherFlags.NONE,
			                                                     null, on_domain_name_disappeared);
		}
	}

	public static bool init () {
		/* Don't reinitialize */
		if (connection != null) {
			return true;
		}

		try {
			connection = GLib.Bus.get_sync (Tracker.IPC.bus ());
		} catch (Error e) {
			critical ("Could not connect to the D-Bus session bus, %s", e.message);
			return false;
		}

		return true;
	}

	static void name_owner_changed_cb (DBusConnection connection, string sender_name, string object_path, string interface_name, string signal_name, Variant parameters) {

		unowned string name, old_owner, new_owner;
		parameters.get ("(&s&s&s)", out name, out old_owner, out new_owner);

		if (old_owner != "" && new_owner == "") {
			/* This means that old_owner got removed */
			resources.unreg_batches (old_owner);
		}
	}

	static void set_available (bool available) {
		if (available) {
			if (resources_id == 0) {
				register_objects ();
			}
		} else {
			if (resources_id != 0) {
				connection.signal_unsubscribe (name_owner_changed_id);
				name_owner_changed_id = 0;

				connection.unregister_object (resources_id);
				resources = null;
				resources_id = 0;

				connection.unregister_object (steroids_id);
				steroids = null;
				steroids_id = 0;
			}
		}
	}

	public static void shutdown () {
		set_available (false);

		if (backup != null) {
			connection.unregister_object (backup_id);
			backup = null;
			backup_id = 0;
		}

		if (notifier != null) {
			connection.unregister_object (notifier_id);
			notifier = null;
			notifier_id = 0;
		}

		if (domain_watch_id != 0) {
			GLib.Bus.unwatch_name (domain_watch_id);
			domain_watch_id = 0;
		}

		connection = null;
	}

	public static Tracker.Status? register_notifier () {
		if (connection == null) {
			critical ("D-Bus support must be initialized before registering objects!");
			return null;
		}

		/* Add org.freedesktop.Tracker */
		notifier = new Tracker.Status ();
		if (notifier == null) {
			critical ("Could not create TrackerStatus object to register");
			return null;
		}

		notifier_id = register_object (connection, notifier, Tracker.Status.PATH);

		return notifier;
	}

	public static bool register_objects () {
		//gpointer object, resources;

		if (connection == null) {
			critical ("D-Bus support must be initialized before registering objects!");
			return false;
		}

		/* Add org.freedesktop.Tracker.Statistics */
		statistics = new Tracker.Statistics ();
		if (statistics == null) {
			critical ("Could not create TrackerStatistics object to register");
			return false;
		}

		statistics_id = register_object (connection, statistics, Tracker.Statistics.PATH);

		/* Add org.freedesktop.Tracker1.Resources */
		resources = new Tracker.Resources (connection);
		if (resources == null) {
			critical ("Could not create TrackerResources object to register");
			return false;
		}

		name_owner_changed_id = connection.signal_subscribe ("org.freedesktop.DBus",
			"org.freedesktop.DBus", "NameOwnerChanged",
			"/org/freedesktop/DBus",
			null,
			0,
			name_owner_changed_cb);

		resources_id = register_object (connection, resources, Tracker.Resources.PATH);

		/* Add org.freedesktop.Tracker1.Steroids */
		steroids = new Tracker.Steroids ();
		if (steroids == null) {
			critical ("Could not create TrackerSteroids object to register");
			return false;
		}

		steroids_id = register_object (connection, steroids, Tracker.Steroids.PATH);

		if (backup == null) {
			/* Add org.freedesktop.Tracker1.Backup */
			backup = new Tracker.Backup ();
			if (backup == null) {
				critical ("Could not create TrackerBackup object to register");
				return false;
			}


			backup_id = register_object (connection, backup, Tracker.Backup.PATH);
		}

		return true;
	}

	public static bool register_prepare_class_signal () {
		if (resources == null) {
			message ("Error during initialization, Resources DBus object not available");
			return false;
		}

		Tracker.Store.enable_signals ();

		return true;
	}

	public static Object? get_object (Type type) {
		if (type == typeof (Resources)) {
			return resources;
		}

		if (type == typeof (Steroids)) {
			return steroids;
		}

		if (type == typeof (Status)) {
			return notifier;
		}

		if (type == typeof (Backup)) {
			return backup;
		}

		return null;
	}
}
