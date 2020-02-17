/*
 * Copyright (C) 2010, Nokia
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

using Tracker.Sparql;
using GLib;

static Connection conn;

static void usage (string[] args) {
	stderr.printf("Usage: %s urn\n", args[0]);
}

static GLib.GenericSet<string> looked_up_iris;

static bool dump_resource (string urn) {
	GLib.List<string> iris_to_lookup = new GLib.List<string> ();

	looked_up_iris.add (urn);

	try {
		Cursor cursor = conn.query ("SELECT ?p rdfs:range(?p) ?o {<%s> ?p ?o}".printf (urn));

		GLib.List<string> type_statements = new GLib.List<string>();
		GLib.List<string> statements = new GLib.List<string>();

		while (cursor.next ()) {
			// Skip tracker internal stuff
			if (cursor.get_string (0).has_prefix ("http://www.tracker-project.org/ontologies/tracker#")) {
				continue;
			}

			string statement = "<%s> <%s> ".printf (urn, cursor.get_string (0));

			switch (cursor.get_string(1)) {
				case "http://www.w3.org/2001/XMLSchema#string":
				case "http://www.w3.org/2001/XMLSchema#dateTime":
					statement += "\"%s\"".printf (escape_string (cursor.get_string (2)));
					break;
				case "http://www.w3.org/2001/XMLSchema#integer":
				case "http://www.w3.org/2001/XMLSchema#double":
				case "http://www.w3.org/2001/XMLSchema#boolean":
					statement += "%s".printf (cursor.get_string (2));
					break;
				default:
					// Assume resource
					unowned string obj = cursor.get_string (2);
					if (!looked_up_iris.contains (obj)) {
						iris_to_lookup.append (obj);
					}
					statement += "<%s>".printf (obj);
					break;
			}

			if (cursor.get_string (0) == "http://www.w3.org/1999/02/22-rdf-syntax-ns#type") {
				type_statements.append (statement);
			} else {
				statements.append (statement);
			}
		}

		foreach (string s in type_statements) {
			stdout.printf ("%s .\n", s);
		}

		foreach (string s in statements) {
			stdout.printf ("%s .\n", s);
		}

		foreach (string s in iris_to_lookup) {
			if (!dump_resource (s)) {
				return false;
			}
		}
	} catch (GLib.Error e) {
		critical ("Couldn't query info for resource %s: %s", urn, e.message);
		return false;
	}

	return true;
}

static int main(string[] args)
{
	if (args.length != 2) {
		usage (args);
		return 1;
	}

	try {
		conn = Connection.bus_new("org.freedesktop.Tracker1", null);
	} catch (GLib.Error e) {
		critical("Couldn't connect to Tracker: %s", e.message);
		return 1;
	}

	looked_up_iris = new GLib.GenericSet<string>(GLib.str_hash, GLib.str_equal);

	if (dump_resource (args[1])) {
		return 0;
	}

	return 1;
}
