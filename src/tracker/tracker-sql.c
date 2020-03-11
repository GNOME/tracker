/*
 * Copyright (C) 2010, Nokia
 * Copyright (C) 2014, Lanedo <martyn@lanedo.com>
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

#include "config.h"

#include <string.h>
#include <locale.h>

#include <glib.h>
#include <glib/gi18n.h>

#include <libtracker-common/tracker-common.h>
#include <libtracker-data/tracker-data.h>

#include "tracker-sql.h"

#define SQL_OPTIONS_ENABLED()	  \
	(file || \
	 query)

static gchar *file;
static gchar *query;
static gchar *database_path;

static GOptionEntry entries[] = {
	{ "database", 'd', 0, G_OPTION_ARG_FILENAME, &database_path,
	  N_("Location of the database"),
	  N_("FILE")
	},
	{ "file", 'f', 0, G_OPTION_ARG_FILENAME, &file,
	  N_("Path to use to run a query from file"),
	  N_("FILE"),
	},
	{ "query", 'q', 0, G_OPTION_ARG_STRING, &query,
	  N_("SQL query"),
	  N_("SQL"),
	},
	{ NULL }
};

static int sql_by_query (void);

static int
sql_by_file (void)
{
	GError *error = NULL;
	gchar *path_in_utf8;
	gsize size;

	path_in_utf8 = g_filename_to_utf8 (file, -1, NULL, NULL, &error);
	if (error) {
		g_printerr ("%s:'%s', %s\n",
		            _("Could not get UTF-8 path from path"),
		            file,
		            error->message);
		g_error_free (error);

		return EXIT_FAILURE;
	}

	g_file_get_contents (path_in_utf8, &query, &size, &error);
	if (error) {
		g_printerr ("%s:'%s', %s\n",
		            _("Could not read file"),
		            path_in_utf8,
		            error->message);
		g_error_free (error);
		g_free (path_in_utf8);

		return EXIT_FAILURE;
	}

	g_free (path_in_utf8);

	return sql_by_query ();
}

static int
sql_by_query (void)
{
	TrackerDBInterface *iface;
	TrackerDBStatement *stmt;
	TrackerDBCursor *cursor = NULL;
	GError *error = NULL;
	gint n_rows = 0;
	GFile *db_location;
	TrackerDataManager *data_manager;

	db_location = g_file_new_for_commandline_arg (database_path);
	data_manager = tracker_data_manager_new (TRACKER_DB_MANAGER_READONLY,
	                                         db_location, NULL,
	                                         100, 100);

	if (!g_initable_init (G_INITABLE (data_manager), NULL, &error)) {
		g_printerr ("%s: %s\n",
		            _("Failed to initialize data manager"),
		            error->message);
		g_error_free (error);
		return EXIT_FAILURE;
	}

	g_print ("--------------------------------------------------\n");
	g_print ("\n\n");

	iface = tracker_data_manager_get_db_interface (data_manager);

	stmt = tracker_db_interface_create_statement (iface, TRACKER_DB_STATEMENT_CACHE_TYPE_NONE, &error, "%s", query);

	if (stmt) {
		cursor = tracker_db_statement_start_cursor (stmt, &error);
	}

	if (error) {
		g_printerr ("%s: %s\n",
		            _("Could not run query"),
		            error->message);
		g_error_free (error);

		return EXIT_FAILURE;
	}

	g_print ("%s:\n", _("Results"));

	while (tracker_db_cursor_iter_next (cursor, NULL, &error)) {
		guint i;

		for (i =  0; i < tracker_db_cursor_get_n_columns (cursor); i++) {
			const gchar *str;

			if (i)
				g_print (" | ");

			str = tracker_db_cursor_get_string (cursor, i, NULL);
			if (str) {
				g_print ("%s", str);
			} else {
				g_print ("(null)");
			}
		}

		g_print ("\n");

		n_rows++;
	}

	if (error) {
		g_printerr ("%s: %s\n",
		            _("Could not run query"),
		            error->message);
		g_error_free (error);

		return EXIT_FAILURE;
	}

	if (n_rows == 0) {
		g_print ("%s\n", _("Empty result set"));
	}

	return EXIT_SUCCESS;
}

static int
sql_run (void)
{
	if (file) {
		return sql_by_file ();
	}

	if (query) {
		return sql_by_query ();
	}

	/* All known options have their own exit points */
	g_warn_if_reached ();

	return EXIT_SUCCESS;
}

static int
sql_run_default (void)
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
sql_options_enabled (void)
{
	return SQL_OPTIONS_ENABLED ();
}

int
tracker_sql (int argc, const char **argv)
{
	GOptionContext *context;
	GError *error = NULL;
	const gchar *failed;

	context = g_option_context_new (NULL);
	g_option_context_add_main_entries (context, entries, NULL);

	argv[0] = "tracker sql";

	if (!g_option_context_parse (context, &argc, (char***) &argv, &error)) {
		g_printerr ("%s, %s\n", _("Unrecognized options"), error->message);
		g_error_free (error);
		g_option_context_free (context);
		return EXIT_FAILURE;
	}

	g_option_context_free (context);

	if (!database_path) {
		failed = _("A database path must be specified");
	} else if (file && query) {
		failed = _("File and query can not be used together");
	} else {
		failed = NULL;
	}

	if (failed) {
		g_printerr ("%s\n\n", failed);
		return EXIT_FAILURE;
	}

	if (sql_options_enabled ()) {
		return sql_run ();
	}

	return sql_run_default ();
}
