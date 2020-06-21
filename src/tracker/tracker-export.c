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

static gchar *data_type;
static gboolean show_graphs_option;

static GOptionEntry entries[] = {
	{ "show-graphs", 'g', 0, G_OPTION_ARG_NONE, &show_graphs_option,
	  N_("Output TriG format which includes named graph information"),
	  NULL
	},
	{ "type", 't', 0, G_OPTION_ARG_STRING, &data_type,
	  N_("Export a specific type of data."),
	  N_("TYPE")
	},
	{ NULL }
};

static TrackerSparqlConnection *
create_connection (GError **error)
{
	return tracker_sparql_connection_get (NULL, error);
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

/* Print triples in Turtle format */
static void
print_turtle (TrackerSparqlCursor *cursor,
              GHashTable          *prefixes,
              gboolean             full_namespaces,
              gboolean             show_prefixes)
{
	gchar *predicate;
	gchar *object;

	if (!tracker_sparql_cursor_next (cursor, NULL, NULL)) {
		g_print ("# No data to export\n");
		return;
	}

	if (show_prefixes) {
		g_hash_table_foreach (prefixes, (GHFunc) print_prefix, NULL);
		g_print ("\n");
	}

	do {
		const gchar *resource = tracker_sparql_cursor_get_string (cursor, 1, NULL);
		const gchar *key = tracker_sparql_cursor_get_string (cursor, 2, NULL);
		const gchar *value = tracker_sparql_cursor_get_string (cursor, 3, NULL);
		gboolean value_is_resource = tracker_sparql_cursor_get_boolean (cursor, 4);

		if (!resource || !key || !value) {
			continue;
		}

		/* Don't display nie:plainTextContent */
		//if (!plain_text_content && strcmp (key, "http://www.semanticdesktop.org/ontologies/2007/01/19/nie#plainTextContent") == 0) {
		//	continue;
		//}

		predicate = format_urn (prefixes, key, full_namespaces);

		if (value_is_resource) {
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
	} while (tracker_sparql_cursor_next (cursor, NULL, NULL));
}

/* Print graphs and triples in TriG format */
static void
print_trig (TrackerSparqlCursor *cursor,
            GHashTable          *prefixes,
            gboolean             full_namespaces,
            gboolean             show_prefixes)
{
	gchar *predicate;
	gchar *object;
	gchar *previous_graph = NULL;
	const gchar *graph;

	if (!tracker_sparql_cursor_next (cursor, NULL, NULL)) {
		g_print ("# No data to export\n");
		return;
	}

	if (show_prefixes) {
		g_hash_table_foreach (prefixes, (GHFunc) print_prefix, NULL);
		g_print ("\n");
	}

	do {
		graph = tracker_sparql_cursor_get_string (cursor, 0, NULL);
		const gchar *resource = tracker_sparql_cursor_get_string (cursor, 1, NULL);
		const gchar *key = tracker_sparql_cursor_get_string (cursor, 2, NULL);
		const gchar *value = tracker_sparql_cursor_get_string (cursor, 3, NULL);
		const gchar *value_is_resource = tracker_sparql_cursor_get_string (cursor, 4, NULL);

		if (!resource || !key || !value || !value_is_resource) {
			continue;
		}

		if (g_strcmp0 (previous_graph, graph) != 0) {
			if (previous_graph != NULL) {
				/* Close previous graph */
				g_print ("}\n");
				g_free (previous_graph);
			}
			previous_graph = g_strdup (graph);
			g_print ("GRAPH <%s>\n{\n", graph);
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
		g_print ("  <%s> %s %s .\n", resource, predicate, object);

		g_free (predicate);
		g_free (object);
	} while (tracker_sparql_cursor_next (cursor, NULL, NULL));

	if (graph != NULL) {
		g_print ("}\n");
	}
	g_free (previous_graph);
}

/* Execute a query and export the resulting triples or quads to stdout.
 *
 * The query should return quads (graph, subject, predicate, object) plus an extra
 * boolean column that is false when the 'object' value is a simple type or a resource.
 */
static gboolean
export_with_query (const gchar  *query,
                   gboolean      show_graphs,
                   gboolean      show_prefixes,
                   GError      **error)
{
	g_autoptr(TrackerSparqlConnection) connection = NULL;
	g_autoptr(TrackerSparqlCursor) cursor = NULL;
	g_autoptr(GError) inner_error = NULL;
	g_autoptr(GHashTable) prefixes = NULL;

	connection = create_connection (&inner_error);

	if (!connection) {
		g_propagate_prefixed_error (error, inner_error,
		                            "%s: ", _("Could not establish a connection to Tracker"));
		return FALSE;
	}

	prefixes = tracker_sparql_get_prefixes ();

	cursor = tracker_sparql_connection_query (connection, query, NULL, &inner_error);

	if (!cursor) {
		g_propagate_prefixed_error (error, inner_error,
		                            "%s: ", _("Could not run query"));
		return FALSE;
	}

	if (show_graphs) {
		print_trig (cursor, prefixes, FALSE, show_prefixes);
	} else {
		print_turtle (cursor, prefixes, FALSE, show_prefixes);
	}

	return TRUE;
}

static int
export_run_photo_albums (void)
{
	const gchar *albums_query, *contents_query;
	g_autoptr(GError) error = NULL;

	/* We must use two separate queries due to
	 * https://gitlab.gnome.org/GNOME/tracker/-/issues/216 */

	albums_query = "SELECT (\"\" as ?graph) ?u ?p ?v "
	               "       (EXISTS { ?p rdfs:range [ rdfs:subClassOf rdfs:Resource ] }) AS ?is_resource "
	               "{ "
	               "    { "
	               "        ?u a nfo:DataContainer ; "
	               "           nao:identifier ?id . "
	               "        FILTER (fn:starts-with (?id, \"photos:collection:local:\")) "
	               "        ?u ?p ?v . "
	               "    } "
	               "} ORDER BY ?u ";

	contents_query = "SELECT \"\" COALESCE(nie:url(?u), ?u) ?p ?v"
	                 "       (EXISTS { ?p rdfs:range [ rdfs:subClassOf rdfs:Resource ] }) AS ?is_resource "
	                 "{"
	                 "    { "
	                 "        SELECT ?u (rdf:type AS ?p) (nmm:Photo AS ?v) "
	                 "        { "
	                 "            ?collection a nfo:DataContainer ; nao:identifier ?id . "
	                 "            FILTER (fn:starts-with (?id, \"photos:collection:local:\")) "
	                 "            ?u nie:isPartOf ?collection . "
	                 "        } "
	                 "    } "
	                 "    UNION "
	                 "    { "
	                 "        SELECT ?u (nie:isPartOf AS ?p) (?collection AS ?v) "
	                 "        { "
	                 "            ?collection a nfo:DataContainer ; nao:identifier ?id . "
	                 "            FILTER (fn:starts-with (?id, \"photos:collection:local:\")) "
	                 "            ?u nie:isPartOf ?collection . "
	                 "        } "
	                 "    } "
	                 "} ORDER BY ?u";

	export_with_query (albums_query, FALSE, TRUE, &error);

	if (error) {
		g_printerr ("%s\n", error->message);
		return EXIT_FAILURE;
	}

	export_with_query (contents_query, FALSE, FALSE, &error);

	if (error) {
		g_printerr ("%s\n", error->message);
		return EXIT_FAILURE;
	} else {
		return EXIT_SUCCESS;
	}
}

static int
export_run_photo_favourites (void)
{
	const gchar *query;
	g_autoptr(GError) error = NULL;

	query = "SELECT \"\" COALESCE(nie:url(?u), ?u) ?p ?v ?is_resource"
	        "{"
	        "    { "
	        "        SELECT ?u (rdf:type AS ?p) (nmm:Photo AS ?v) (true AS ?is_resource)"
	        "        { "
	        "            ?u a nmm:Photo ; nao:hasTag nao:predefined-tag-favorite . "
	        "        } "
	        "    } "
	        "    UNION "
	        "    { "
	        "        SELECT ?u (nao:hasTag AS ?p) (nao:predefined-tag-favorite AS ?v) (true AS ?is_resource)"
	        "        { "
	        "            ?u a nmm:Photo ; nao:hasTag nao:predefined-tag-favorite . "
	        "        } "
	        "    } "
	        "} ORDER BY ?url";

	export_with_query (query, FALSE, TRUE, &error);

	if (error) {
		g_printerr ("%s\n", error->message);
		return EXIT_FAILURE;
	} else {
		return EXIT_SUCCESS;
	}
}

static int
export_run_files_starred (void)
{
	const gchar *query;
	g_autoptr(GError) error = NULL;

	query = "SELECT \"\" COALESCE(nie:url(?u), ?u) ?p ?v ?is_resource"
	        "{"
	        "    { "
	        "        SELECT ?u (rdf:type AS ?p) (nfo:FileDataObject AS ?v) (true AS ?is_resource)"
	        "        { "
	        "            ?u a nfo:FileDataObject ; nao:hasTag <urn:gnome:nautilus:starred> "
	        "        } "
	        "    } "
	        "    UNION "
	        "    { "
	        "        SELECT ?u (nao:hasTag AS ?p) (<urn:gnome:nautilus:starred> AS ?v) (true AS ?is_resource)"
	        "        { "
	        "            ?u a nfo:FileDataObject ; nao:hasTag <urn:gnome:nautilus:starred> "
	        "        } "
	        "    } "
	        "} ORDER BY ?u";

	export_with_query (query, FALSE, TRUE, &error);

	if (error) {
		g_printerr ("%s\n", error->message);
		return EXIT_FAILURE;
	} else {
		return EXIT_SUCCESS;
	}
}

static int
export_run_default (void)
{
	g_autoptr(GError) error = NULL;
	const gchar *query;

	query = "SELECT ?g ?u ?p ?v "
	        "       (EXISTS { ?p rdfs:range [ rdfs:subClassOf rdfs:Resource ] }) AS ?is_resource "
	        "{ "
	        "    GRAPH ?g { "
	        "        ?u ?p ?v "
	        "        FILTER NOT EXISTS { ?u a rdf:Property } "
	        "        FILTER NOT EXISTS { ?u a rdfs:Class } "
	        "        FILTER NOT EXISTS { ?u a tracker:Namespace } "
	        "    } "
	        "} ORDER BY ?g ?u";

	export_with_query (query, show_graphs_option, TRUE, &error);

	if (error) {
		g_printerr ("%s\n", error->message);
		return EXIT_FAILURE;
	} else {
		return EXIT_SUCCESS;
	}
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

	if (data_type == NULL) {
		return export_run_default ();
	} else if (strcmp (data_type, "photos-albums") == 0) {
		return export_run_photo_albums ();
	} else if (strcmp (data_type, "photos-favorites") == 0 || strcmp (data_type, "photos-favourites") == 0) {
		return export_run_photo_favourites ();
	} else if (strcmp (data_type, "files-starred") == 0) {
		return export_run_files_starred ();
	} else {
		g_printerr ("%s: %s\n", _("Unrecognized value for '--type' option"), data_type);
		return EXIT_FAILURE;
	}
}
