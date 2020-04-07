/*
 * Copyright (C) 2006, Jamie McCracken <jamiemcc@gnome.org>
 * Copyright (C) 2008-2010, Nokia <ivan.frade@nokia.com>
 * Copyright (C) 2014, SoftAtHome <contact@softathome.com>
 * Copyright (C) 2014, Lanedo <martyn@lanedo.com>
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

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <locale.h>

#include <glib.h>
#include <glib/gi18n.h>

#include <libtracker-sparql/tracker-sparql.h>

#include "tracker-info.h"
#include "tracker-sparql.h"

#define INFO_OPTIONS_ENABLED() \
	(filenames && g_strv_length (filenames) > 0);

static gchar **filenames;
static gboolean full_namespaces;
static gboolean plain_text_content;
static gboolean resource_is_iri;
static gboolean turtle;
static gchar *url_property;
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
	{ "full-namespaces", 'f', 0, G_OPTION_ARG_NONE, &full_namespaces,
	  N_("Show full namespaces (i.e. don’t use nie:title, use full URLs)"),
	  NULL,
	},
	{ "plain-text-content", 'c', 0, G_OPTION_ARG_NONE, &plain_text_content,
	  N_("Show plain text content if available for resources"),
	  NULL,
	},
	{ "resource-is-iri", 'i', 0, G_OPTION_ARG_NONE, &resource_is_iri,
	  /* To translators:
	   * IRI (International Resource Identifier) is a generalization
	   * of the URI. While URI supports only ASCI encoding, IRI
	   * fully supports international characters. In practice, UTF-8
	   * is the most popular encoding used for IRI.
	   */
	  N_("Instead of looking up a file name, treat the FILE arguments as actual IRIs (e.g. <file:///path/to/some/file.txt>)"),
	  NULL,
	},
	{ "turtle", 't', 0, G_OPTION_ARG_NONE, &turtle,
	  N_("Output results as RDF in Turtle format"),
	  NULL,
	},
	{ "url", 'u', 0, G_OPTION_ARG_STRING, &url_property,
	  N_("RDF property to treat as URL (eg. “nie:url”)"),
	  NULL,
	},
	{ G_OPTION_REMAINING, 0, 0, G_OPTION_ARG_FILENAME_ARRAY, &filenames,
	  N_("FILE"),
	  N_("FILE")},
	{ NULL }
};

static gboolean
has_valid_uri_scheme (const gchar *uri)
{
	const gchar *s;

	s = uri;

	if (!g_ascii_isalpha (*s)) {
		return FALSE;
	}

	do {
		s++;
	} while (g_ascii_isalnum (*s) || *s == '+' || *s == '.' || *s == '-');

	return (*s == ':');
}

static inline void
print_key_and_value (GHashTable  *prefixes,
                     const gchar *key,
                     const gchar *value)
{
	if (G_UNLIKELY (full_namespaces)) {
		g_print ("  '%s' = '%s'\n", key, value);
	} else {
		gchar *shorthand;

		shorthand = tracker_sparql_get_shorthand (prefixes, key);
		g_print ("  '%s' = '%s'\n", shorthand, value);
		g_free (shorthand);
	}
}

static void
print_plain (gchar               *urn_or_filename,
             gchar               *urn,
             TrackerSparqlCursor *cursor,
             GHashTable          *prefixes,
             gboolean             full_namespaces)
{
	gchar *fts_key = NULL;
	gchar *fts_value = NULL;

	while (tracker_sparql_cursor_next (cursor, NULL, NULL)) {
		const gchar *key = tracker_sparql_cursor_get_string (cursor, 0, NULL);
		const gchar *value = tracker_sparql_cursor_get_string (cursor, 1, NULL);

		if (!key || !value) {
			continue;
		}

		/* Don't display nie:plainTextContent */
		if (strcmp (key, "http://www.semanticdesktop.org/ontologies/2007/01/19/nie#plainTextContent") == 0) {
			if (plain_text_content) {
				fts_key = g_strdup (key);
				fts_value = g_strdup (value);
			}

			/* Always print FTS data at the end because of it's length */
			continue;
		}

		print_key_and_value (prefixes, key, value);
	}

	if (fts_key && fts_value) {
		print_key_and_value (prefixes, fts_key, fts_value);
	}

	g_free (fts_key);
	g_free (fts_value);
}

/* print a URI prefix in Turtle format */
static void
print_prefix (gpointer key,
              gpointer value,
              gpointer user_data)
{
	g_print ("@prefix %s: <%s#> .\n", (gchar *) value, (gchar *) key);
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

/* Print triples for a urn in Turtle format */
static void
print_turtle (gchar               *urn,
              TrackerSparqlCursor *cursor,
              GHashTable          *prefixes,
              gboolean             full_namespaces)
{
	gchar *subject;
	gchar *predicate;
	gchar *object;

	if (G_UNLIKELY (full_namespaces)) {
		subject = g_strdup (urn);
	} else {
		/* truncate subject */
		subject = tracker_sparql_get_shorthand (prefixes, urn);
	}

	while (tracker_sparql_cursor_next (cursor, NULL, NULL)) {
		const gchar *key = tracker_sparql_cursor_get_string (cursor, 0, NULL);
		const gchar *value = tracker_sparql_cursor_get_string (cursor, 1, NULL);
		const gchar *is_resource = tracker_sparql_cursor_get_string (cursor, 2, NULL);

		if (!key || !value || !is_resource) {
			continue;
		}

		/* Don't display nie:plainTextContent */
		if (!plain_text_content && strcmp (key, "http://www.semanticdesktop.org/ontologies/2007/01/19/nie#plainTextContent") == 0) {
			continue;
		}

		predicate = format_urn (prefixes, key, full_namespaces);

		if (g_ascii_strcasecmp (is_resource, "true") == 0) {
			object = g_strdup_printf ("<%s>", value);
		} else {
			gchar *escaped_value;

			/* Escape value and make sure it is encapsulated properly */
			escaped_value = tracker_sparql_escape_string (value);
			object = g_strdup_printf ("\"%s\"", escaped_value);
			g_free (escaped_value);
		}

		/* Print final statement */
		g_print ("<%s> %s %s .\n", subject, predicate, object);

		g_free (predicate);
		g_free (object);
	}

	g_free (subject);
}

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
		/* Default to tracker-miner-fs service */
		return tracker_sparql_connection_bus_new ("org.freedesktop.Tracker3.Miner.Files",
		                                          NULL, NULL, error);
	}
}

static int
info_run (void)
{
	TrackerSparqlConnection *connection;
	GError *error = NULL;
	GHashTable *prefixes;
	gchar **p;

	connection = create_connection (&error);

	if (!connection) {
		g_printerr ("%s: %s\n",
		            _("Could not establish a connection to Tracker"),
		            error ? error->message : _("No error given"));
		g_clear_error (&error);
		return EXIT_FAILURE;
	}

	prefixes = tracker_sparql_get_prefixes (connection);

	/* print all prefixes if using turtle format and not showing full namespaces */
	if (turtle && !full_namespaces) {
		g_hash_table_foreach (prefixes, (GHFunc) print_prefix, NULL);
		g_print ("\n");
	}

	if (!url_property)
		url_property = g_strdup ("nie:url");

	for (p = filenames; *p; p++) {
		TrackerSparqlCursor *cursor = NULL;
		GError *error = NULL;
		gchar *uri = NULL;
		gchar *query;
		gchar *urn = NULL;

		if (!turtle && !resource_is_iri) {
			g_print ("%s: '%s'\n", _("Querying information for entity"), *p);
		}

		/* support both, URIs and local file paths */
		if (has_valid_uri_scheme (*p)) {
			uri = g_strdup (*p);
		} else if (resource_is_iri) {
			uri = g_strdup (*p);
		} else {
			GFile *file;

			file = g_file_new_for_commandline_arg (*p);
			uri = g_file_get_uri (file);
			g_object_unref (file);
		}

		if (!resource_is_iri) {
			/* First check whether there's some entity with nie:url like this */
			query = g_strdup_printf ("SELECT ?urn WHERE { ?urn %s \"%s\" }", url_property, uri);
			cursor = tracker_sparql_connection_query (connection, query, NULL, &error);
			g_free (query);

			if (error) {
				g_printerr ("  %s, %s\n",
				            _("Unable to retrieve URN for URI"),
				            error->message);
				g_clear_error (&error);
				continue;
			}
		}

		if (!cursor || !tracker_sparql_cursor_next (cursor, NULL, &error)) {
			if (error) {
				g_printerr ("  %s, %s\n",
				            _("Unable to retrieve data for URI"),
				            error->message);
				g_object_unref (cursor);
				g_clear_error (&error);

				continue;
			}

			/* No URN matches, use uri as URN */
			urn = g_strdup (uri);
		} else {
			urn = g_strdup (tracker_sparql_cursor_get_string (cursor, 0, NULL));

			if (!turtle) {
				g_print ("  '%s'\n", urn);
			}

			g_object_unref (cursor);
		}

		query = g_strdup_printf ("SELECT ?predicate ?object"
		                         "  ( EXISTS { ?predicate rdfs:range [ rdfs:subClassOf rdfs:Resource ] } )"
		                         "WHERE {"
		                         "  <%s> ?predicate ?object "
		                         "}",
		                         urn);

		cursor = tracker_sparql_connection_query (connection, query, NULL, &error);

		g_free (uri);
		g_free (query);

		if (error) {
			g_printerr ("  %s, %s\n",
			            _("Unable to retrieve data for URI"),
			            error->message);

			g_clear_error (&error);
			continue;
		}

		if (!cursor) {
			g_print ("  %s\n",
			         _("No metadata available for that URI"));
		} else {
			if (turtle) {
				print_turtle (urn, cursor, prefixes, full_namespaces);
			} else {
				g_print ("%s:\n", _("Results"));

				print_plain (*p, urn, cursor, prefixes, full_namespaces);
			}

			g_print ("\n");

			g_object_unref (cursor);
		}

		g_print ("\n");

		g_free (urn);
	}

	g_hash_table_unref (prefixes);
	g_object_unref (connection);

	return EXIT_SUCCESS;
}

static int
info_run_default (void)
{
	GOptionContext *context;
	gchar *help;

	context = g_option_context_new (NULL);
	g_option_context_add_main_entries (context, entries, NULL);
	help = g_option_context_get_help (context, TRUE, NULL);
	g_option_context_free (context);
	g_printerr ("%s\n", help);
	g_free (help);

	return EXIT_FAILURE;
}

static gboolean
info_options_enabled (void)
{
	return INFO_OPTIONS_ENABLED ();
}

int
tracker_info (int argc, const char **argv)
{
	GOptionContext *context;
	GError *error = NULL;

	context = g_option_context_new (NULL);
	g_option_context_add_main_entries (context, entries, NULL);

	argv[0] = "tracker info";

	if (!g_option_context_parse (context, &argc, (char***) &argv, &error)) {
		g_printerr ("%s, %s\n", _("Unrecognized options"), error->message);
		g_error_free (error);
		g_option_context_free (context);
		return EXIT_FAILURE;
	}

	g_option_context_free (context);

	if (info_options_enabled ()) {
		return info_run ();
	}

	return info_run_default ();
}
