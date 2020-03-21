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

#define IMPORT_OPTIONS_ENABLED() \
	(filenames && g_strv_length (filenames) > 0);

static gchar **filenames;
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
	{ G_OPTION_REMAINING, 0, 0, G_OPTION_ARG_FILENAME_ARRAY, &filenames,
	  N_("FILE"),
	  N_("FILE")},
	{ NULL }
};

static TrackerSparqlConnection *
create_connection (GError **error)
{
	if (database_path && !dbus_service && !remote_service) {
		GFile *file;

		file = g_file_new_for_commandline_arg (database_path);
		return tracker_sparql_connection_new (TRACKER_SPARQL_CONNECTION_FLAGS_NONE,
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

static int
import_run (void)
{
	g_autoptr(TrackerSparqlConnection) connection = NULL;
	g_autoptr(GError) error = NULL;
	gchar **p;

	connection = create_connection (&error);

	if (!connection) {
		g_printerr ("%s: %s\n",
		            _("Could not establish a connection to Tracker"),
		            error ? error->message : _("No error given"));
		return EXIT_FAILURE;
	}

	for (p = filenames; *p; p++) {
		g_autoptr(GFile) file = NULL;
		g_autofree gchar *update = NULL;
		g_autofree gchar *uri = NULL;

		file = g_file_new_for_commandline_arg (*p);
		uri = g_file_get_uri (file);
		update = g_strdup_printf ("LOAD <%s>", uri);

		tracker_sparql_connection_update (connection, update, 0, NULL, &error);

		if (error) {
			g_printerr ("%s, %s\n",
			            _("Could not run import"),
			            error->message);
			return EXIT_FAILURE;
		}

		g_print ("Successfully imported %s", g_file_peek_path (file));
	}

	return EXIT_SUCCESS;
}

static int
import_run_default (void)
{
	g_autoptr(GOptionContext) context = NULL;
	g_autofree gchar *help = NULL;

	context = g_option_context_new (NULL);
	g_option_context_add_main_entries (context, entries, NULL);
	help = g_option_context_get_help (context, TRUE, NULL);
	g_printerr ("%s\n", help);

	return EXIT_FAILURE;
}

static gboolean
import_options_enabled (void)
{
	return IMPORT_OPTIONS_ENABLED ();
}

int
tracker_import (int argc, const char **argv)
{
	GOptionContext *context;
	GError *error = NULL;

	context = g_option_context_new (NULL);
	g_option_context_add_main_entries (context, entries, NULL);

	argv[0] = "tracker import";

	if (!g_option_context_parse (context, &argc, (char***) &argv, &error)) {
		g_printerr ("%s, %s\n", _("Unrecognized options"), error->message);
		g_error_free (error);
		g_option_context_free (context);
		return EXIT_FAILURE;
	}

	g_option_context_free (context);

	if (import_options_enabled ()) {
		return import_run ();
	}

	return import_run_default ();
}
