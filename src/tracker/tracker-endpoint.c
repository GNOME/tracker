/*
 * Copyright (C) 2020, Red Hat Inc.
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
 *
 * Author: Carlos Garnacho <carlosg@gnome.org>
 */

#include "config.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <locale.h>

#include <glib.h>
#include <glib-unix.h>
#include <glib/gi18n.h>

#include <libtracker-sparql/tracker-sparql.h>

#include "tracker-endpoint.h"

static gchar *database_path = NULL;
static gchar *dbus_service = NULL;
static gchar *ontology_name = NULL;
static gchar *ontology_path = NULL;
static gboolean session_bus = FALSE;
static gboolean system_bus = FALSE;
static gboolean name_owned = FALSE;

static GOptionEntry entries[] = {
	{ "database", 'd', 0, G_OPTION_ARG_FILENAME, &database_path,
	  N_("Location of the database"),
	  N_("DIR")
	},
	{ "dbus-service", 'b', 0, G_OPTION_ARG_STRING, &dbus_service,
	  N_("Specify the DBus name of this endpoint"),
	  N_("NAME")
	},
	{ "ontology", 'o', 0, G_OPTION_ARG_STRING, &ontology_name,
	  N_("Specify the ontology name used in this endpoint"),
	  N_("NAME")
	},
	{ "ontology-path", 'p', 0, G_OPTION_ARG_FILENAME, &ontology_path,
	  N_("Specify a path to an ontology to be used in this endpoint"),
	  N_("DIR")
	},
	{ "session", 0, 0, G_OPTION_ARG_NONE, &session_bus,
	  N_("Use session bus"),
	  NULL
	},
	{ "system", 0, 0, G_OPTION_ARG_NONE, &system_bus,
	  N_("Use system bus"),
	  NULL
	},
	{ NULL }
};

#define TRACKER_ENDPOINT_ERROR tracker_endpoint_error_quark ()

static GQuark
tracker_endpoint_error_quark (void)
{
	return g_quark_from_static_string ("tracker-endpoint-error-quark");
}

typedef enum _TrackerEndpointError {
	TRACKER_ENDPOINT_ERROR_COULD_NOT_OWN_NAME,
	TRACKER_ENDPOINT_ERROR_NAME_LOST,
} TrackerEndpointError;

static gboolean
sanity_check (void)
{
	if (!database_path) {
		g_printerr ("%s\n", _("No database path was provided"));
		return FALSE;
	}

	if (!!ontology_path == !!ontology_name) {
		/* TRANSLATORS: those are commandline arguments */
		g_printerr ("%s\n", _("One “ontology” or “ontology-path” option should be provided"));
		return FALSE;
	}

	return TRUE;
}

static gboolean
sigterm_cb (gpointer user_data)
{
	g_main_loop_quit (user_data);

	return G_SOURCE_REMOVE;
}

static void
name_acquired_cb (GDBusConnection *connection,
                  const gchar     *name,
                  gpointer         user_data)
{
	name_owned = TRUE;
	g_main_loop_quit (user_data);
}

static void
name_lost_cb (GDBusConnection *connection,
              const gchar     *name,
              gpointer         user_data)
{
	name_owned = FALSE;
	g_main_loop_quit (user_data);
}

static gboolean
run_endpoint (TrackerSparqlConnection  *connection,
              GError                  **error)
{
	TrackerEndpoint *endpoint = NULL;
	GDBusConnection *dbus_connection;
	g_autoptr(GMainLoop) main_loop;
	GError *inner_error = NULL;

	g_print (_("Creating endpoint at %s…"), dbus_service);
	g_print ("\n");

	if (system_bus) {
		dbus_connection = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, &inner_error);
	} else {
		dbus_connection = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, &inner_error);
	}

	if (dbus_connection) {
		endpoint = TRACKER_ENDPOINT (tracker_endpoint_dbus_new (connection,
		                                                        dbus_connection,
		                                                        NULL, NULL, &inner_error));
	}

	main_loop = g_main_loop_new (NULL, FALSE);

	if (endpoint) {
		g_bus_own_name_on_connection (dbus_connection,
		                              dbus_service,
		                              G_BUS_NAME_OWNER_FLAGS_NONE,
		                              name_acquired_cb,
		                              name_lost_cb,
		                              main_loop, NULL);

		g_main_loop_run (main_loop);
	}

	if (inner_error) {
		g_propagate_error (error, inner_error);
		return FALSE;
	}

	if (!name_owned) {
		g_set_error_literal (error, TRACKER_ENDPOINT_ERROR,
		                     TRACKER_ENDPOINT_ERROR_COULD_NOT_OWN_NAME,
		                     _("Could not own DBus name"));
		return FALSE;
	}

	g_print ("%s\n", _("Listening to SPARQL commands. Press Ctrl-C to stop."));

	g_unix_signal_add (SIGINT, sigterm_cb, main_loop);
	g_unix_signal_add (SIGTERM, sigterm_cb, main_loop);

	g_main_loop_run (main_loop);

	if (!name_owned) {
		g_set_error_literal (error, TRACKER_ENDPOINT_ERROR,
		                     TRACKER_ENDPOINT_ERROR_COULD_NOT_OWN_NAME,
		                     _("DBus name lost"));
		return FALSE;
	}

	/* Carriage return, so we paper over the ^C */
	g_print ("\r%s\n", _("Closing connection…"));
	g_clear_object (&endpoint);

	return TRUE;
}

int
tracker_endpoint (int argc, const char **argv)
{
	TrackerSparqlConnection *connection;
	GOptionContext *context;
	GError *error = NULL;
	GFile *database, *ontology = NULL;

	context = g_option_context_new (NULL);
	g_option_context_add_main_entries (context, entries, NULL);

	argv[0] = "tracker endpoint";

	if (!g_option_context_parse (context, &argc, (char***) &argv, &error)) {
		g_printerr ("%s, %s\n", _("Unrecognized options"), error->message);
		g_error_free (error);
		g_option_context_free (context);
		return EXIT_FAILURE;
	}

	if (!sanity_check ()) {
		gchar *help;

		help = g_option_context_get_help (context, TRUE, NULL);
		g_printerr ("%s\n", help);
		g_free (help);
		g_option_context_free (context);
		return EXIT_FAILURE;
	}

	database = g_file_new_for_commandline_arg (database_path);
	if (ontology_path) {
		ontology = g_file_new_for_commandline_arg (ontology_path);
	} else if (ontology_name) {
		gchar *path = g_build_filename (SHAREDIR, "tracker", "ontologies", ontology_name, NULL);
		ontology = g_file_new_for_path (path);
		g_free (path);
	}

	g_assert (ontology != NULL);
	g_print (_("Opening database at %s…"), database_path);
	g_print ("\n");

	connection = tracker_sparql_connection_new (0, database, ontology, NULL, &error);
	if (!connection) {
		g_printerr ("%s\n", error->message);
		g_error_free (error);
		g_option_context_free (context);
		return EXIT_FAILURE;
	}

	if (dbus_service) {
		run_endpoint (connection, &error);

		if (error) {
			g_printerr ("%s\n", error->message);
			g_error_free (error);
		}
	} else {
		g_print (_("New database created. Use the --dbus-service option to "
		           "share this database on a message bus."));
	}

	if (connection) {
		tracker_sparql_connection_close (connection);
		g_clear_object (&connection);
	}

	g_option_context_free (context);

	return EXIT_SUCCESS;
}
