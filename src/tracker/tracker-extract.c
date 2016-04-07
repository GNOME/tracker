/*
 * Copyright (C) 2015-2016, Sam Thursfield <sam@afuera.me.uk>
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

#include <stdlib.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <gio/gio.h>

#include <libtracker-common/tracker-common.h>

#include "tracker-config.h"
#include "tracker-extract.h"

static gchar *verbosity;
static gchar *output_format = "turtle";
static gchar **filenames;

#define EXTRACT_OPTIONS_ENABLED()	  \
	((filenames && g_strv_length (filenames) > 0))

static GOptionEntry entries[] = {
	{ "verbosity", 'v', 0, G_OPTION_ARG_STRING, &verbosity,
	  N_("Sets the logging verbosity to LEVEL (“debug”, “detailed”, “minimal”, “errors”) for all processes"),
	  N_("LEVEL") },
	{ "output-format", 'o', 0, G_OPTION_ARG_STRING, &output_format,
	  N_("Output results format: 'sparql', 'turtle' or 'json-ld'"),
	  N_("FORMAT") },
	{ G_OPTION_REMAINING, 0, 0, G_OPTION_ARG_FILENAME_ARRAY, &filenames,
	  N_("FILE"),
	  N_("FILE") },
	{ NULL }
};


static gint
extract_files (TrackerVerbosity  verbosity,
               char             *output_format)
{
	char **p;
	char *tracker_extract_path;
	char verbosity_str[2];
	GError *error = NULL;

	snprintf (verbosity_str, 2, "%i", verbosity);

	tracker_extract_path = g_build_filename(LIBEXECDIR, "tracker-extract", NULL);

	for (p = filenames; *p; p++) {
		char *argv[] = {tracker_extract_path,
		                "--output-format", output_format,
		                "--verbosity", verbosity_str,
		                "--file", *p, NULL };

		g_spawn_sync(NULL, argv, NULL, G_SPAWN_DEFAULT, NULL, NULL, NULL, NULL, NULL, &error);

		if (error) {
			g_printerr ("%s: %s\n",
			            _("Could not run tracker-extract: "),
			            error->message);
			g_error_free (error);
			g_free (tracker_extract_path);
			return EXIT_FAILURE;
		}
	}

	g_free (tracker_extract_path);
	return EXIT_SUCCESS;
}

static int
extract_run (void)
{
	TrackerVerbosity verbosity_level = TRACKER_VERBOSITY_ERRORS;

	if (verbosity) {
		if (g_ascii_strcasecmp (verbosity, "debug") == 0) {
			verbosity_level = TRACKER_VERBOSITY_DEBUG;
		} else if (g_ascii_strcasecmp (verbosity, "detailed") == 0) {
			verbosity_level = TRACKER_VERBOSITY_DETAILED;
		} else if (g_ascii_strcasecmp (verbosity, "minimal") == 0) {
			verbosity_level = TRACKER_VERBOSITY_MINIMAL;
		} else if (g_ascii_strcasecmp (verbosity, "errors") == 0) {
			verbosity_level = TRACKER_VERBOSITY_ERRORS;
		} else {
			g_printerr ("%s\n",
			            _("Invalid log verbosity, try “debug”, “detailed”, “minimal” or “errors”"));
			return EXIT_FAILURE;
		}
	}

	return extract_files (verbosity_level, output_format);
}

static int
extract_run_default (void)
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
extract_options_enabled (void)
{
	return EXTRACT_OPTIONS_ENABLED ();
}

int
tracker_extract (int argc, const char **argv)
{
	GOptionContext *context;
	GError *error = NULL;

	context = g_option_context_new (NULL);
	g_option_context_add_main_entries (context, entries, NULL);

	argv[0] = "tracker extract";

	if (!g_option_context_parse (context, &argc, (char***) &argv, &error)) {
		g_printerr ("%s, %s\n", _("Unrecognized options"), error->message);
		g_error_free (error);
		g_option_context_free (context);
		return EXIT_FAILURE;
	}

	g_option_context_free (context);

	if (extract_options_enabled ()) {
		return extract_run ();
	}

	return extract_run_default ();
}
