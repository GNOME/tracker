/*
 * Copyright (C) 2020, Red Hat Inc.
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
 *
 * Author: Carlos Garnacho <carlosg@gnome.org>
 */

#include "config.h"

#include <stdlib.h>
#include <locale.h>

#include <glib.h>
#include <glib-unix.h>
#include <glib/gi18n.h>
#include <gio/gio.h>

#include <libtracker-common/tracker-common.h>

#include "tracker-portal.h"

static gboolean version = FALSE;

const char usage_string[] =
	"xdg-tracker-portal [--version | -v]";

const char about[] =
	"Tracker XDG portal " PACKAGE_VERSION "\n"
	"\n"
	"This program is free software and comes without any warranty.\n"
	"It is licensed under version 2 or later of the General Public "
	"License which can be viewed at:\n"
	"\n"
	"  http://www.gnu.org/licenses/gpl.txt"
	"\n";

static GOptionEntry entries[] = {
	{ "version", 'v', 0, G_OPTION_ARG_NONE, &version,
	  N_("Version"),
	  NULL
	},
};

static int
print_version (void)
{
	puts (about);
	return 0;
}

static gboolean
sigterm_cb (gpointer user_data)
{
	g_main_loop_quit (user_data);

	return G_SOURCE_REMOVE;
}

static void
name_acquired_callback (GDBusConnection *connection,
                        const gchar     *name,
                        gpointer         user_data)
{
	g_debug ("Name '%s' acquired", name);
	g_main_loop_quit (user_data);
}

static void
name_lost_callback (GDBusConnection *connection,
                    const gchar     *name,
                    gpointer         user_data)
{
	g_critical ("Name '%s' lost", name);
	exit (EXIT_FAILURE);
}

int
main (int argc, char *argv[])
{
	GMainLoop *main_loop;
	g_autoptr(GError) error = NULL;
	GDBusConnection *connection;
	g_autoptr(GOptionContext) context = NULL;
	TrackerPortal *portal;

	setlocale (LC_ALL, "");

	bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);

	context = g_option_context_new (NULL);
	g_option_context_add_main_entries (context, entries, NULL);

	if (!g_option_context_parse (context, &argc, (char***) &argv, &error)) {
		g_printerr ("%s, %s\n", _("Unrecognized options"), error->message);
		g_error_free (error);
		return EXIT_FAILURE;
	}

	if (version) {
		print_version ();
		return EXIT_SUCCESS;
	}

	connection = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, &error);
	if (!connection) {
		g_printerr ("%s", error->message);
		return EXIT_FAILURE;
	}

	main_loop = g_main_loop_new (NULL, FALSE);

	g_bus_own_name_on_connection (connection,
	                              "org.freedesktop.portal.Tracker",
	                              G_BUS_NAME_OWNER_FLAGS_NONE,
	                              name_acquired_callback,
	                              name_lost_callback,
	                              g_main_loop_ref (main_loop),
	                              (GDestroyNotify) g_main_loop_unref);

	g_main_loop_run (main_loop);

	portal = tracker_portal_new (connection, NULL, &error);
	if (!portal) {
		g_printerr ("%s", error->message);
		return EXIT_FAILURE;
	}

	g_unix_signal_add (SIGINT, sigterm_cb, main_loop);
	g_unix_signal_add (SIGTERM, sigterm_cb, main_loop);
	g_main_loop_run (main_loop);

	g_main_loop_unref (main_loop);

	g_clear_object (&portal);

	return EXIT_FAILURE;
}
