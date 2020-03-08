/*
 * Copyright (C) 2009, Nokia <ivan.frade@nokia.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

#include "config.h"

#include <string.h>

#include <glib.h>
#include <glib/gstdio.h>
#include <gio/gio.h>

#include <libtracker-data/tracker-data.h>
#include <libtracker-data/tracker-turtle-reader.h>

static gchar         *ontology_dir = NULL;
static gchar         *ttl_file = NULL;

static GOptionEntry   entries[] = {
	{ "ttl-file", 't', 0, G_OPTION_ARG_FILENAME, &ttl_file,
	  "Turtle file to validate",
	  NULL
	},
	{ "ontology-dir", 'o', 0, G_OPTION_ARG_FILENAME, &ontology_dir,
	  "Directory containing the ontology description files (TTL FORMAT)",
	  NULL
	},
	{ NULL }
};

#define CLASS "http://www.w3.org/2000/01/rdf-schema#Class"
#define PROPERTY "http://www.w3.org/1999/02/22-rdf-syntax-ns#Property"
#define IS "http://www.w3.org/1999/02/22-rdf-syntax-ns#type"

static gboolean error_flag = FALSE;

static GList *unknown_items = NULL;
static GList *known_items = NULL;

static gboolean
exists_or_already_reported (const gchar *item)
{
	if (!g_list_find_custom (known_items,
	                         item,
	                         (GCompareFunc) g_strcmp0)){
		if (!g_list_find_custom (unknown_items,
		                         item,
		                         (GCompareFunc) g_strcmp0)) {
			return FALSE;
		}
	}

	return TRUE;
}

static void
turtle_load_ontology (const gchar *turtle_subject,
                      const gchar *turtle_predicate,
                      const gchar *turtle_object)
{

	if (!g_strcmp0 (turtle_predicate, IS)) {
		known_items = g_list_prepend (known_items, g_strdup (turtle_subject));
	}
}

static void
turtle_statement_handler (const gchar *turtle_subject,
                          const gchar *turtle_predicate,
                          const gchar *turtle_object)
{
	/* Check that predicate exists in the ontology */
	if (!exists_or_already_reported (turtle_predicate)){
		g_print ("Unknown property %s\n", turtle_predicate);
		unknown_items = g_list_prepend (unknown_items, g_strdup (turtle_predicate));
		error_flag = TRUE;
	}

	/* And if it is a type... check the object is also there */
	if (!g_strcmp0 (turtle_predicate, IS)) {
		if (!exists_or_already_reported (turtle_object)){
			g_print ("Unknown class %s\n", turtle_object);
			error_flag = TRUE;
			unknown_items = g_list_prepend (unknown_items, g_strdup (turtle_object));
		}
	}
}

static void
load_ontology_files (const gchar *services_dir)
{
	GList       *files = NULL;
	GDir        *services;
	const gchar *conf_file;
	GFile       *f;
	gchar       *dir_uri, *fullpath;
	gint         counter = 0;

	f = g_file_new_for_path (services_dir);
	dir_uri = g_file_get_path (f);

	services = g_dir_open (dir_uri, 0, NULL);

	conf_file = g_dir_read_name (services);

	while (conf_file) {
		const gchar *subject, *predicate, *object;
		TrackerTurtleReader *reader;
		GError *error = NULL;
		GFile *file;

		if (!g_str_has_suffix (conf_file, "ontology")) {
			conf_file = g_dir_read_name (services);
			continue;
		}

		fullpath = g_build_filename (dir_uri, conf_file, NULL);
		file = g_file_new_for_path (fullpath);

		reader = tracker_turtle_reader_new_for_file (file, NULL);
		g_object_unref (file);

		while (tracker_turtle_reader_next (reader,
		                                   &subject, &predicate, &object,
		                                   NULL, &error)) {
			turtle_load_ontology (subject, predicate, object);
		}

		g_object_unref (reader);

		if (error) {
			g_message ("Turtle parse error: %s", error->message);
			g_error_free (error);
		}

		g_free (fullpath);
		counter += 1;
		conf_file = g_dir_read_name (services);
	}

	g_dir_close (services);

	g_list_foreach (files, (GFunc) g_free, NULL);
	g_object_unref (f);
	g_free (dir_uri);
	g_debug ("Loaded %d ontologies\n", counter);
}

gint
main (gint argc, gchar **argv)
{
	const gchar *subject, *predicate, *object;
	GOptionContext *context;
	TrackerTurtleReader *reader;
	GError *error = NULL;
	GFile *file;

	/* Translators: this messagge will apper immediately after the  */
	/* usage string - Usage: COMMAND [OPTION]... <THIS_MESSAGE>     */
	context = g_option_context_new ("- Validate a turtle file against the ontology");

	/* Translators: this message will appear after the usage string */
	/* and before the list of options.                              */
	g_option_context_add_main_entries (context, entries, NULL);
	g_option_context_parse (context, &argc, &argv, NULL);

	if (!ontology_dir || !ttl_file) {
		gchar *help;

		g_printerr ("%s\n\n",
		            "Ontology directory and turtle file are mandatory");

		help = g_option_context_get_help (context, TRUE, NULL);
		g_option_context_free (context);
		g_printerr ("%s", help);
		g_free (help);

		return -1;
	}

	//"/home/ivan/devel/codethink/tracker-ssh/data/services"
	load_ontology_files (ontology_dir);

	file = g_file_new_for_commandline_arg (ttl_file);
	reader = tracker_turtle_reader_new_for_file (file, NULL);
	g_object_unref (file);

	while (tracker_turtle_reader_next (reader,
	                                   &subject, &predicate, &object,
	                                   NULL, &error)) {
		turtle_statement_handler (subject, predicate, object);
	}

	g_object_unref (reader);

	if (error) {
		g_message ("Turtle parse error: %s", error->message);
		g_error_free (error);
	}

	if (!error_flag) {
		g_debug ("%s seems OK.", ttl_file);
	}

	return 0;
}
