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

[DBus (name = "org.freedesktop.Tracker1.Backup")]
public class Tracker.Backup : Object {
	public const string PATH = "/org/freedesktop/Tracker1/Backup";

	public async void save (BusName sender, string destination_uri) throws Error {
		var resources = (Resources) Tracker.DBus.get_object (typeof (Resources));
		if (resources != null) {
			Tracker.Store.disable_signals ();
			Tracker.Events.shutdown ();
		}

		var request = DBusRequest.begin (sender, "D-Bus request to save backup into '%s'", destination_uri);
		try {
			var destination = File.new_for_uri (destination_uri);

			if (destination == null || destination.get_path() == null) {
				throw new DataBackupError.INVALID_URI ("'" + destination_uri + "' is not a valid uri");
			}

			yield Tracker.Store.pause ();

			Error backup_error = null;
			var data_manager = Tracker.Main.get_data_manager ();
			Data.backup_save (data_manager, destination, destination, error => {
				backup_error = error;
				save.callback ();
			});
			yield;

			if (backup_error != null) {
				throw backup_error;
			}

			request.end ();
		} catch (Error e) {
			request.end (e);
			throw e;
		} finally {
			if (resources != null) {
				Tracker.Events.init ();
				Tracker.Store.enable_signals ();
			}

			Tracker.Store.resume ();
		}
	}

	public async void restore (BusName sender, string journal_uri) throws Error {
		var resources = (Resources) Tracker.DBus.get_object (typeof (Resources));
		if (resources != null) {
			Tracker.Store.disable_signals ();
			Tracker.Events.shutdown ();
		}

		var request = DBusRequest.begin (sender, "D-Bus request to restore backup from '%s'", journal_uri);
		try {
			yield Tracker.Store.pause ();

			var journal = File.new_for_uri (journal_uri);

			if (journal == null || journal.get_path() == null) {
				throw new DataBackupError.INVALID_URI ("'" + journal_uri + "' is not a valid uri");
			}

			var notifier = (Status) (Tracker.DBus.get_object (typeof (Status)));
			var busy_callback = notifier.get_callback ();
			var data_manager = Tracker.Main.get_data_manager ();

			// TODO: set cache_location, data_location and ontology_location here from store session
			Data.backup_restore (data_manager, journal, null, null, null, busy_callback);

			request.end ();
		} catch (Error e) {
			request.end (e);
			throw e;
		} finally {
			if (resources != null) {
				Tracker.Events.init ();
				Tracker.Store.enable_signals ();
			}

			Tracker.Store.resume ();
		}
	}
}
