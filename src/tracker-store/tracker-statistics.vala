/*
 * Copyright (C) 2011, Nokia <ivan.frade@nokia.com>
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

[DBus (name = "org.freedesktop.Tracker1.Statistics")]
public class Tracker.Statistics : Object {
	public const string PATH = "/org/freedesktop/Tracker1/Statistics";

	static bool initialized;

	static GLib.HashTable<Tracker.Class, int> class_counts;

	[DBus (signature = "aas")]
	public new Variant get (BusName sender) throws GLib.Error {
		var request = DBusRequest.begin (sender, "Statistics.Get");
		var data_manager = Tracker.Main.get_data_manager ();
		var ontologies = data_manager.get_ontologies ();

		class_counts = new HashTable<Tracker.Class, int> (direct_hash, direct_equal);

		if (!initialized) {
			var iface = data_manager.get_db_interface ();

			foreach (var cl in ontologies.get_classes ()) {
				/* xsd classes do not derive from rdfs:Resource and do not use separate tables */
				if (!cl.name.has_prefix ("xsd:")) {
					/* update statistics */
					var stmt = iface.create_statement (DBStatementCacheType.NONE,
						                           "SELECT COUNT(1) FROM \"%s\"",
						                            cl.name);

					var stat_cursor = stmt.start_cursor ();
					if (stat_cursor.next ()) {
						class_counts.insert (cl, (int) stat_cursor.get_integer (0));
					} else {
						warning ("Unable to query instance count for class %s", cl.name);
					}
				}
			}

			initialized = true;
		}

		var builder = new VariantBuilder ((VariantType) "aas");

		foreach (var cl in ontologies.get_classes ()) {
			int count = class_counts.lookup (cl);
			if (count == 0) {
				/* skip classes without resources */
				continue;
			}

			builder.open ((VariantType) "as");
			builder.add ("s", cl.name);
			builder.add ("s", count.to_string ());
			builder.close ();
		}

		request.end ();

		return builder.end ();
	}
}
