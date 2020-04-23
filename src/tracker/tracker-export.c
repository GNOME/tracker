/*
 * Copyright (C) 2020, Sam Thursfield <sam@afuera.me.uk>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 */

#include "config.h"

#include <sys/param.h>
#include <stdlib.h>
#include <time.h>
#include <locale.h>

#include <glib.h>
#include <glib/gi18n.h>

#include <libtracker-sparql/tracker-sparql.h>

#include "tracker-sparql.h"
#include "tracker-color.h"

static gchar *database_path;
static gchar *dbus_service;
static gchar *remote_service;

static GOptionEntry entries[] = {
	{ "database", 'd', 0, G_OPTION_ARG_FILENAME, &database_path,
	  N_("Location of the database"),
	  N_("FILE")
	},
	{ "dbus-service", 'b', 0, G_OPTION_ARG_STRING, &dbus_service,
	  N_("Connects to a DBus service"),
	  N_("DBus service name")
	},
	{ "remote-service", 'r', 0, G_OPTION_ARG_STRING, &remote_service,
	  N_("Connects to a remote service"),
	  N_("Remote service URI")
	},
	{ NULL }
};

static TrackerSparqlConnection *
create_connection (GError **error)
{
	if (database_path && !dbus_service && !remote_service) {
		GFile *file;

		file = g_file_new_for_commandline_arg (database_path);
		return tracker_sparql_connection_new (TRACKER_SPARQL_CONNECTION_FLAGS_READONLY,
		                                      file, NULL, NULL, error);
	} else if (dbus_service && !database_path && !remote_service) {
		GDBusConnection *dbus_conn;

		dbus_conn = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, error);
		if (!dbus_conn)
			return NULL;

		return tracker_sparql_connection_bus_new (dbus_service, NULL, dbus_conn, error);
	} else if (remote_service && !database_path && !dbus_service) {
		return tracker_sparql_connection_remote_new (remote_service);
	} else {
		/* TRANSLATORS: Those are commandline arguments */
		g_printerr (_("Specify one --database, --dbus-service or --remote-service option"));
		exit (EXIT_FAILURE);
	}
}


/* format a URI for Turtle; if it has a prefix, display uri
 * as prefix:rest_of_uri; if not, display as <uri>
 */
inline static gchar *
format_urn (GHashTable  *prefixes,
            const gchar *urn,
            gboolean     full_namespaces)
{
	gchar *urn_out;

	if (full_namespaces) {
		urn_out = g_strdup_printf ("<%s>", urn);
	} else {
		gchar *shorthand = tracker_sparql_get_shorthand (prefixes, urn);

		/* If the shorthand is the same as the urn passed, we
		 * assume it is a resource and pass it in as one,
		 *
		 *   e.g.: http://purl.org/dc/elements/1.1/date
		 *     to: http://purl.org/dc/elements/1.1/date
		 *
		 * Otherwise, we use the shorthand version instead.
		 *
		 *   e.g.: http://www.w3.org/1999/02/22-rdf-syntax-ns
		 *     to: rdf
		 */
		if (g_strcmp0 (shorthand, urn) == 0) {
			urn_out = g_strdup_printf ("<%s>", urn);
			g_free (shorthand);
		} else {
			urn_out = shorthand;
		}
	}

	return urn_out;
}

/* print a URI prefix in Turtle format */
static void
print_prefix (gpointer key,
              gpointer value,
              gpointer user_data)
{
	g_print ("@prefix %s: <%s#> .\n", (gchar *) value, (gchar *) key);
}

/* Print triples for a urn in Turtle format */
static void
print_turtle (TrackerSparqlCursor *cursor,
              GHashTable          *prefixes,
              gboolean             full_namespaces)
{
	gchar *predicate;
	gchar *object;

	while (tracker_sparql_cursor_next (cursor, NULL, NULL)) {
		const gchar *resource = tracker_sparql_cursor_get_string (cursor, 0, NULL);
		const gchar *key = tracker_sparql_cursor_get_string (cursor, 1, NULL);
		const gchar *value = tracker_sparql_cursor_get_string (cursor, 2, NULL);
		const gchar *value_is_resource = tracker_sparql_cursor_get_string (cursor, 3, NULL);

		if (!resource || !key || !value || !value_is_resource) {
			continue;
		}

		/* Don't display nie:plainTextContent */
		//if (!plain_text_content && strcmp (key, "http://tracker.api.gnome.org/ontology/v3/nie#plainTextContent") == 0) {
		//	continue;
		//}

		predicate = format_urn (prefixes, key, full_namespaces);

		if (g_ascii_strcasecmp (value_is_resource, "true") == 0) {
			object = g_strdup_printf ("<%s>", value);
		} else {
			gchar *escaped_value;

			/* Escape value and make sure it is encapsulated properly */
			escaped_value = tracker_sparql_escape_string (value);
			object = g_strdup_printf ("\"%s\"", escaped_value);
			g_free (escaped_value);
		}

		/* Print final statement */
		g_print ("<%s> %s %s .\n", resource, predicate, object);

		g_free (predicate);
		g_free (object);
	}
}

static int
export_run_default (void)
{
	g_autoptr(TrackerSparqlConnection) connection = NULL;
	g_autoptr(TrackerSparqlCursor) cursor = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GHashTable) prefixes = NULL;
	const gchar *query;

	connection = create_connection (&error);

	if (!connection) {
		g_printerr ("%s: %s\n",
		            _("Could not establish a connection to Tracker"),
		            error ? error->message : _("No error given"));
		return EXIT_FAILURE;
	}

	prefixes = tracker_sparql_get_prefixes (connection);

	query = "SELECT ?u ?p ?v "
	        "       (EXISTS { ?p rdfs:range [ rdfs:subClassOf rdfs:Resource ] }) AS ?is_resource "
	        "{ "
	        "    ?u ?p ?v "
	        "    FILTER NOT EXISTS { ?u a rdf:Property } "
	        "    FILTER NOT EXISTS { ?u a rdfs:Class } "
	        "    FILTER NOT EXISTS { ?u a tracker:Namespace } "
	        "} ORDER BY ?u";

	cursor = tracker_sparql_connection_query (connection, query, NULL, &error);

	if (error) {
		g_printerr ("%s, %s\n",
		            _("Could not run query"),
		            error->message);
		return EXIT_FAILURE;
	}

	g_hash_table_foreach (prefixes, (GHFunc) print_prefix, NULL);
	g_print ("\n");

	print_turtle (cursor, prefixes, FALSE);

	return EXIT_SUCCESS;
}

int
tracker_export (int argc, const char **argv)
{
	GOptionContext *context;
	GError *error = NULL;

	context = g_option_context_new (NULL);
	g_option_context_add_main_entries (context, entries, NULL);

	argv[0] = "tracker export";

	if (!g_option_context_parse (context, &argc, (char***) &argv, &error)) {
		g_printerr ("%s, %s\n", _("Unrecognized options"), error->message);
		g_error_free (error);
		g_option_context_free (context);
		return EXIT_FAILURE;
	}

	g_option_context_free (context);

	return export_run_default ();
}
