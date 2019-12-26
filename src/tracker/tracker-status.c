/*
 * Copyright (C) 2006, Jamie McCracken <jamiemcc@gnome.org>
 * Copyright (C) 2008, Nokia <ivan.frade@nokia.com>
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

#include <libtracker-common/tracker-common.h>
#include <libtracker-sparql/tracker-sparql.h>

#include "tracker-status.h"
#include "tracker-config.h"
#include "tracker-miner-manager.h"

#define STATUS_OPTIONS_ENABLED()	  \
	(show_stat || \
	 collect_debug_info)

static gboolean show_stat;
static gboolean collect_debug_info;
static gchar **terms;

static GOptionEntry entries[] = {
	{ "stat", 'a', 0, G_OPTION_ARG_NONE, &show_stat,
	  N_("Show statistics for current index / data set"),
	  NULL
	},
	{ "collect-debug-info", 0, 0, G_OPTION_ARG_NONE, &collect_debug_info,
	  N_("Collect debug information useful for problem reporting and investigation, results are output to terminal"),
	  NULL },
	{ G_OPTION_REMAINING, 0, 0,
	  G_OPTION_ARG_STRING_ARRAY, &terms,
	  N_("search terms"),
	  N_("EXPRESSION") },
	{ NULL }
};

static int
status_stat (void)
{
	TrackerSparqlConnection *connection;
	TrackerSparqlCursor *cursor;
	GError *error = NULL;

	connection = tracker_sparql_connection_get (NULL, &error);

	if (!connection) {
		g_printerr ("%s: %s\n",
		            _("Could not establish a connection to Tracker"),
		            error ? error->message : _("No error given"));
		g_clear_error (&error);
		return EXIT_FAILURE;
	}

	cursor = tracker_sparql_connection_statistics (connection, NULL, &error);

	g_object_unref (connection);

	if (error) {
		g_printerr ("%s, %s\n",
		            _("Could not get Tracker statistics"),
		            error->message);
		g_error_free (error);
		return EXIT_FAILURE;
	}

	if (!cursor) {
		g_print ("%s\n", _("No statistics available"));
	} else {
		GString *output;

		output = g_string_new ("");

		while (tracker_sparql_cursor_next (cursor, NULL, NULL)) {
			const gchar *rdf_type;
			const gchar *rdf_type_count;

			rdf_type = tracker_sparql_cursor_get_string (cursor, 0, NULL);
			rdf_type_count = tracker_sparql_cursor_get_string (cursor, 1, NULL);

			if (terms) {
				gint i, n_terms;
				gboolean show_rdf_type = FALSE;

				n_terms = g_strv_length (terms);

				for (i = 0;
				     i < n_terms && !show_rdf_type;
				     i++) {
					show_rdf_type = g_str_match_string (terms[i], rdf_type, TRUE);
				}

				if (!show_rdf_type) {
					continue;
				}
			}

			g_string_append_printf (output,
			                        "  %s = %s\n",
			                        rdf_type,
			                        rdf_type_count);
		}

		if (output->len > 0) {
			g_string_prepend (output, "\n");
			/* To translators: This is to say there are no
			 * statistics found. We use a "Statistics:
			 * None" with multiple print statements */
			g_string_prepend (output, _("Statistics:"));
		} else {
			g_string_append_printf (output,
			                        "  %s\n", _("None"));
		}

		g_print ("%s\n", output->str);
		g_string_free (output, TRUE);

		g_object_unref (cursor);
	}

	return EXIT_SUCCESS;
}

static int
collect_debug (void)
{
	/* What to collect?
	 * This is based on information usually requested from maintainers to users.
	 *
	 * 1. Package details, e.g. version.
	 * 2. Disk size, space left, type (SSD/etc)
	 * 3. Size of dataset (tracker-stats), size of databases
	 * 4. Current configuration (libtracker-fts, tracker-miner-fs, tracker-extract)
	 *    All txt files in ~/.cache/
	 * 5. Statistics about data (tracker-stats)
	 */

	GDir *d;
	gchar *data_dir;
	gchar *str;

	data_dir = g_build_filename (g_get_user_cache_dir (), "tracker", NULL);

	/* 1. Package details, e.g. version. */
	g_print ("[Package Details]\n");
	g_print ("%s: " PACKAGE_VERSION "\n", _("Version"));
	g_print ("\n\n");

	/* 2. Disk size, space left, type (SSD/etc) */
	guint64 remaining_bytes;
	gdouble remaining;

	g_print ("[%s]\n", _("Disk Information"));

	remaining_bytes = tracker_file_system_get_remaining_space (data_dir);
	str = g_format_size (remaining_bytes);

	remaining = tracker_file_system_get_remaining_space_percentage (data_dir);
	g_print ("%s: %s (%3.2lf%%)\n",
	         _("Remaining space on database partition"),
	         str,
	         remaining);
	g_free (str);
	g_print ("\n\n");

	/* 3. Size of dataset (tracker-stats), size of databases */
	g_print ("[%s]\n", _("Data Set"));

	for (d = g_dir_open (data_dir, 0, NULL); d != NULL;) {
		const gchar *f;
		gchar *path;
		goffset size;

		f = g_dir_read_name (d);
		if (!f) {
			break;
		}

		if (g_str_has_suffix (f, ".txt")) {
			continue;
		}

		path = g_build_filename (data_dir, f, NULL);
		size = tracker_file_get_size (path);
		str = g_format_size (size);

		g_print ("%s\n%s\n\n", path, str);
		g_free (str);
		g_free (path);
	}
	g_dir_close (d);
	g_print ("\n");

	/* 4. Current configuration (libtracker-fts, tracker-miner-fs, tracker-extract)
	 *    All txt files in ~/.cache/
	 */
	GSList *all, *l;

	g_print ("[%s]\n", _("Configuration"));

	all = tracker_gsettings_get_all (NULL);

	if (all) {
		for (l = all; l; l = l->next) {
			ComponentGSettings *c = l->data;
			gchar **keys, **p;

			if (!c) {
				continue;
			}

			keys = g_settings_schema_list_keys (c->schema);
			for (p = keys; p && *p; p++) {
				GVariant *v;
				gchar *printed;

				v = g_settings_get_value (c->settings, *p);
				printed = g_variant_print (v, FALSE);
				g_print ("%s.%s: %s\n", c->name, *p, printed);
				g_free (printed);
				g_variant_unref (v);
			}
		}

		tracker_gsettings_free (all);
	} else {
		g_print ("** %s **\n", _("No configuration was found"));
	}
	g_print ("\n\n");

	g_print ("[%s]\n", _("States"));

	for (d = g_dir_open (data_dir, 0, NULL); d != NULL;) {
		const gchar *f;
		gchar *path;
		gchar *content = NULL;

		f = g_dir_read_name (d);
		if (!f) {
			break;
		}

		if (!g_str_has_suffix (f, ".txt")) {
			continue;
		}

		path = g_build_filename (data_dir, f, NULL);
		if (g_file_get_contents (path, &content, NULL, NULL)) {
			/* Special case last-index.txt which is time() dump to file */
			if (g_str_has_suffix (path, "last-crawl.txt")) {
				guint64 then, now;

				now = (guint64) time (NULL);
				then = g_ascii_strtoull (content, NULL, 10);
				str = tracker_seconds_to_string (now - then, FALSE);

				g_print ("%s\n%s (%s)\n\n", path, content, str);
			} else {
				g_print ("%s\n%s\n\n", path, content);
			}
			g_free (content);
		}
		g_free (path);
	}
	g_dir_close (d);
	g_print ("\n");

	/* 5. Statistics about data (tracker-stats) */
	TrackerSparqlConnection *connection;
	GError *error = NULL;

	g_print ("[%s]\n", _("Data Statistics"));

	connection = tracker_sparql_connection_get (NULL, &error);

	if (!connection) {
		g_print ("** %s, %s **\n",
		         _("No connection available"),
		         error ? error->message : _("No error given"));
		g_clear_error (&error);
	} else {
		TrackerSparqlCursor *cursor;

		cursor = tracker_sparql_connection_statistics (connection, NULL, &error);

		if (error) {
			g_print ("** %s, %s **\n",
			         _("Could not get statistics"),
			         error ? error->message : _("No error given"));
			g_error_free (error);
		} else {
			if (!cursor) {
				g_print ("** %s **\n",
				         _("No statistics were available"));
			} else {
				gint count = 0;

				while (tracker_sparql_cursor_next (cursor, NULL, NULL)) {
					g_print ("%s: %s\n",
					         tracker_sparql_cursor_get_string (cursor, 0, NULL),
					         tracker_sparql_cursor_get_string (cursor, 1, NULL));
					count++;
				}

				if (count == 0) {
					g_print ("%s\n",
					         _("Database is currently empty"));
				}

				g_object_unref (cursor);
			}
		}
	}

	g_object_unref (connection);
	g_print ("\n\n");

	g_print ("\n");

	g_free (data_dir);

	return EXIT_SUCCESS;
}

static int
status_run (void)
{
	if (show_stat) {
		return status_stat ();
	}

	if (collect_debug_info) {
		return collect_debug ();
	}

	/* All known options have their own exit points */
	g_warn_if_reached ();

	return EXIT_FAILURE;
}

static int
get_file_and_folder_count (int *files,
                           int *folders)
{
	TrackerSparqlConnection *connection;
	TrackerSparqlCursor *cursor;
	GError *error = NULL;

	connection = tracker_sparql_connection_get (NULL, &error);

	if (files) {
		*files = 0;
	}

	if (folders) {
		*folders = 0;
	}

	if (!connection) {
		g_printerr ("%s: %s\n",
		            _("Could not establish a connection to Tracker"),
		            error ? error->message : _("No error given"));
		g_clear_error (&error);
		return EXIT_FAILURE;
	}

	if (files) {
		const gchar query[] =
			"\nSELECT COUNT(?file) "
			"\nWHERE { "
			"\n  ?file a nfo:FileDataObject ;"
			"\n        tracker:available true ."
			"\n  FILTER (?file != nfo:Folder) "
			"\n}";

		cursor = tracker_sparql_connection_query (connection, query, NULL, &error);

		if (cursor)
			tracker_sparql_cursor_next (cursor, NULL, &error);

		if (error) {
			g_printerr ("%s, %s\n",
			            _("Could not get basic status for Tracker"),
			            error->message);
			g_error_free (error);
			return EXIT_FAILURE;
		}

		*files = tracker_sparql_cursor_get_integer (cursor, 0);

		g_object_unref (cursor);
	}

	if (folders) {
		const gchar query[] =
			"\nSELECT COUNT(?folders)"
			"\nWHERE { "
			"\n  ?folders a nfo:Folder ;"
			"\n           tracker:available true ."
			"\n}";

		cursor = tracker_sparql_connection_query (connection, query, NULL, &error);

		if (error || !tracker_sparql_cursor_next (cursor, NULL, NULL)) {
			g_printerr ("%s, %s\n",
			            _("Could not get basic status for Tracker"),
			            error ? error->message : _("No error given"));
			g_error_free (error);
			return EXIT_FAILURE;
		}

		*folders = tracker_sparql_cursor_get_integer (cursor, 0);

		g_object_unref (cursor);
	}

	g_object_unref (connection);

	return EXIT_SUCCESS;
}

static gboolean
are_miners_finished (gint *max_remaining_time)
{
	TrackerMinerManager *manager;
	GError *error = NULL;
	GSList *miners_running;
	GSList *l;
	gboolean finished = TRUE;
	gint _max_remaining_time = 0;

	/* Don't auto-start the miners here */
	manager = tracker_miner_manager_new_full (FALSE, &error);
	if (!manager) {
		g_printerr (_("Could not get status, manager could not be created, %s"),
		            error ? error->message : _("No error given"));
		g_printerr ("\n");
		g_clear_error (&error);
		return EXIT_FAILURE;
	}

	miners_running = tracker_miner_manager_get_running (manager);

	for (l = miners_running; l; l = l->next) {
		gchar *status;
		gdouble progress;
		gint remaining_time;

		if (!tracker_miner_manager_get_status (manager,
		                                       l->data,
		                                       &status,
		                                       &progress,
		                                       &remaining_time)) {
			continue;
		}

		g_free (status);

		finished &= progress == 1.0;
		_max_remaining_time = MAX(remaining_time, _max_remaining_time);
	}

	g_slist_foreach (miners_running, (GFunc) g_free, NULL);
	g_slist_free (miners_running);

	if (max_remaining_time) {
		*max_remaining_time = _max_remaining_time;
	}

	g_object_unref (manager);

	return finished;
}

static int
get_no_args (void)
{
	gchar *str;
	gchar *data_dir;
	guint64 remaining_bytes;
	gdouble remaining;
	gint remaining_time;
	gint files, folders;

	/* How many files / folders do we have? */
	if (get_file_and_folder_count (&files, &folders) != 0) {
		return EXIT_FAILURE;
	}

	g_print (_("Currently indexed"));
	g_print (": ");
	g_print (g_dngettext (NULL,
	                      "%d file",
	                      "%d files",
	                      files),
	         files);
	g_print (", ");
	g_print (g_dngettext (NULL,
	                      "%d folder",
	                      "%d folders",
	                      folders),
	         folders);
	g_print ("\n");

	/* How much space is left? */
	data_dir = g_build_filename (g_get_user_cache_dir (), "tracker", NULL);

	remaining_bytes = tracker_file_system_get_remaining_space (data_dir);
	str = g_format_size (remaining_bytes);

	remaining = tracker_file_system_get_remaining_space_percentage (data_dir);
	g_print ("%s: %s (%3.2lf%%)\n",
	         _("Remaining space on database partition"),
	         str,
	         remaining);
	g_free (str);
	g_free (data_dir);

	/* Are we finished indexing? */
	if (!are_miners_finished (&remaining_time)) {
		gchar *remaining_time_str;

		remaining_time_str = tracker_seconds_to_string (remaining_time, TRUE);

		g_print ("%s: ", _("Data is still being indexed"));
		g_print (_("Estimated %s left"), remaining_time_str);
		g_print ("\n");
		g_free (remaining_time_str);
	} else {
		g_print ("%s\n", _("All data miners are idle, indexing complete"));
	}

	g_print ("\n\n");

	return EXIT_SUCCESS;
}

static int
status_run_default (void)
{
	return get_no_args ();
}

static gboolean
status_options_enabled (void)
{
	return STATUS_OPTIONS_ENABLED ();
}

int
tracker_status (int argc, const char **argv)
{
	GOptionContext *context;
	GError *error = NULL;

	context = g_option_context_new (NULL);
	g_option_context_add_main_entries (context, entries, NULL);

	argv[0] = "tracker status";

	if (!g_option_context_parse (context, &argc, (char***) &argv, &error)) {
		g_printerr ("%s, %s\n", _("Unrecognized options"), error->message);
		g_error_free (error);
		g_option_context_free (context);
		return EXIT_FAILURE;
	}

	g_option_context_free (context);

	if (status_options_enabled ()) {
		return status_run ();
	}

	return status_run_default ();
}
