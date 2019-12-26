/*
 * Copyright (C) 2014, Lanedo <martyn@lanedo.com>
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
#include <errno.h>

#ifdef __sun
#include <procfs.h>
#endif

#include <glib.h>
#include <glib/gi18n.h>
#include <gio/gio.h>

#include <libtracker-sparql/tracker-sparql.h>

#include "tracker-index.h"
#include "tracker-dbus.h"
#include "tracker-miner-manager.h"

static gchar **reindex_mime_types;
static gboolean index_file;
static gboolean backup;
static gboolean restore;
static gboolean import;
static gchar **filenames;

#define INDEX_OPTIONS_ENABLED()	  \
	((filenames && g_strv_length (filenames) > 0) || \
	 (index_file || \
	  backup || \
	  restore || \
	  import) || \
	 reindex_mime_types)

static GOptionEntry entries[] = {
	{ "reindex-mime-type", 'm', 0, G_OPTION_ARG_STRING_ARRAY, &reindex_mime_types,
	  N_("Tell miners to reindex files which match the mime type supplied (for new extractors), use -m MIME1 -m MIME2"),
	  N_("MIME") },
	{ "file", 'f', 0, G_OPTION_ARG_NONE, &index_file,
	  N_("Tell miners to (re)index a given file"),
	  N_("FILE") },
	{ "backup", 'b', 0, G_OPTION_ARG_NONE, &backup,
	  N_("Backup current index / database to the file provided"),
	  NULL },
	{ "restore", 'o', 0, G_OPTION_ARG_NONE, &restore,
	  N_("Restore a database from a previous backup (see --backup)"),
	  NULL },
	{ "import", 'i', 0, G_OPTION_ARG_NONE, &import,
	  N_("Import a dataset from the provided file (in Turtle format)"),
	  NULL },
	{ G_OPTION_REMAINING, 0, 0, G_OPTION_ARG_FILENAME_ARRAY, &filenames,
	  N_("FILE"),
	  N_("FILE") },
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

static gchar *
get_uri_from_arg (const gchar *arg)
{
	gchar *uri;

	/* support both, URIs and local file paths */
	if (has_valid_uri_scheme (arg)) {
		uri = g_strdup (arg);
	} else {
		GFile *file;

		file = g_file_new_for_commandline_arg (arg);
		uri = g_file_get_uri (file);
		g_object_unref (file);
	}

	return uri;
}

static int
reindex_mimes (void)
{
	GError *error = NULL;
	TrackerMinerManager *manager;

	/* Auto-start the miners here if we need to */
	manager = tracker_miner_manager_new_full (TRUE, &error);
	if (!manager) {
		g_printerr (_("Could not reindex mimetypes, manager could not be created, %s"),
		            error ? error->message : _("No error given"));
		g_printerr ("\n");
		g_clear_error (&error);
		return EXIT_FAILURE;
	}

	tracker_miner_manager_reindex_by_mimetype (manager, (GStrv) reindex_mime_types, &error);
	if (error) {
		g_printerr ("%s: %s\n",
		            _("Could not reindex mimetypes"),
		            error->message);
		g_error_free (error);
		return EXIT_FAILURE;
	}

	g_print ("%s\n", _("Reindexing mime types was successful"));
	g_object_unref (manager);

	return EXIT_SUCCESS;
}

static gint
index_or_reindex_file (void)
{
	TrackerMinerManager *manager;
	GError *error = NULL;
	gchar **p;

	/* Auto-start the miners here if we need to */
	manager = tracker_miner_manager_new_full (TRUE, &error);
	if (!manager) {
		g_printerr (_("Could not (re)index file, manager could not be created, %s"),
		            error ? error->message : _("No error given"));
		g_printerr ("\n");
		g_clear_error (&error);
		return EXIT_FAILURE;
	}

	for (p = filenames; *p; p++) {
		GFile *file;

		file = g_file_new_for_commandline_arg (*p);
		tracker_miner_manager_index_file (manager, file, NULL, &error);

		if (error) {
			g_printerr ("%s: %s\n",
			            _("Could not (re)index file"),
			            error->message);
			g_error_free (error);
			return EXIT_FAILURE;
		}

		g_print ("%s\n", _("(Re)indexing file was successful"));
		g_object_unref (file);
	}

	g_object_unref (manager);

	return EXIT_SUCCESS;
}

static int
import_turtle_files (void)
{
	TrackerSparqlConnection *connection;
	GError *error = NULL;
	gchar **p;

	connection = tracker_sparql_connection_get (NULL, &error);

	if (!connection) {
		g_printerr ("%s: %s\n",
		            _("Could not establish a connection to Tracker"),
		            error ? error->message : _("No error given"));
		g_clear_error (&error);
		return EXIT_FAILURE;
	}

	for (p = filenames; *p; p++) {
		GError *error = NULL;
		GFile *file;

		g_print ("%s:'%s'\n",
		         _("Importing Turtle file"),
		         *p);

		file = g_file_new_for_commandline_arg (*p);
		tracker_sparql_connection_load (connection, file, NULL, &error);
		g_object_unref (file);

		if (error) {
			g_printerr ("  %s, %s\n",
			            _("Unable to import Turtle file"),
			            error->message);

			g_error_free (error);
			continue;
		}

		g_print ("  %s\n", _("Done"));
		g_print ("\n");
	}

	g_object_unref (connection);

	return EXIT_SUCCESS;
}

static int
backup_index (void)
{
	GDBusConnection *connection;
	GDBusProxy *proxy;
	GError *error = NULL;
	GVariant *v;
	gchar *uri;

	if (!tracker_dbus_get_connection ("org.freedesktop.Tracker1",
	                                  "/org/freedesktop/Tracker1/Backup",
	                                  "org.freedesktop.Tracker1.Backup",
	                                  G_DBUS_PROXY_FLAGS_NONE,
	                                  &connection,
	                                  &proxy)) {
		return EXIT_FAILURE;
	}

	uri = get_uri_from_arg (filenames[0]);

	g_print ("%s\n", _("Backing up database"));
	g_print ("  %s\n", uri);

	/* Backup/Restore can take some time */
	g_dbus_proxy_set_default_timeout (proxy, G_MAXINT);

	v = g_dbus_proxy_call_sync (proxy,
	                            "Save",
	                            g_variant_new ("(s)", uri),
	                            G_DBUS_CALL_FLAGS_NONE,
	                            -1,
	                            NULL,
	                            &error);

	if (proxy) {
		g_object_unref (proxy);
	}

	if (error) {
		g_critical ("%s, %s",
		            _("Could not backup database"),
		            error ? error->message : _("No error given"));
		g_clear_error (&error);
		g_free (uri);

		return EXIT_FAILURE;
	}

	if (v) {
		g_variant_unref (v);
	}

	g_free (uri);

	return EXIT_SUCCESS;
}

static int
restore_index (void)
{
	GDBusConnection *connection;
	GDBusProxy *proxy;
	GError *error = NULL;
	GVariant *v;
	gchar *uri;

	if (!tracker_dbus_get_connection ("org.freedesktop.Tracker1",
	                                  "/org/freedesktop/Tracker1/Backup",
	                                  "org.freedesktop.Tracker1.Backup",
	                                  G_DBUS_PROXY_FLAGS_NONE,
	                                  &connection,
	                                  &proxy)) {
		return EXIT_FAILURE;
	}

	uri = get_uri_from_arg (filenames[0]);

	g_print ("%s\n", _("Restoring database from backup"));
	g_print ("  %s\n", uri);

	/* Backup/Restore can take some time */
	g_dbus_proxy_set_default_timeout (proxy, G_MAXINT);

	v = g_dbus_proxy_call_sync (proxy,
	                            "Restore",
	                            g_variant_new ("(s)", uri),
	                            G_DBUS_CALL_FLAGS_NONE,
	                            -1,
	                            NULL,
	                            &error);

	if (proxy) {
		g_object_unref (proxy);
	}

	if (error) {
		g_critical ("%s, %s",
		            _("Could not backup database"),
		            error ? error->message : _("No error given"));
		g_clear_error (&error);
		g_free (uri);

		return EXIT_FAILURE;
	}

	if (v) {
		g_variant_unref (v);
	}

	g_free (uri);

	return EXIT_SUCCESS;
}

static int
index_run (void)
{
	if (reindex_mime_types) {
		return reindex_mimes ();
	}

	if (index_file) {
		return index_or_reindex_file ();
	}

	if (import) {
		return import_turtle_files ();
	}

	if (backup) {
		return backup_index ();
	}

	if (restore) {
		return restore_index ();
	}

	/* All known options have their own exit points */
	g_printerr("Use `tracker index --file` when giving a specific file or "
	           "directory to index. See `tracker help index` for more "
	           "information.\n");

	return EXIT_FAILURE;
}

static int
index_run_default (void)
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
index_options_enabled (void)
{
	return INDEX_OPTIONS_ENABLED ();
}

int
tracker_index (int argc, const char **argv)
{
	GOptionContext *context;
	GError *error = NULL;
	const gchar *failed;
	gint actions = 0;

	context = g_option_context_new (NULL);
	g_option_context_add_main_entries (context, entries, NULL);

	argv[0] = "tracker index";

	if (!g_option_context_parse (context, &argc, (char***) &argv, &error)) {
		g_printerr ("%s, %s\n", _("Unrecognized options"), error->message);
		g_error_free (error);
		g_option_context_free (context);
		return EXIT_FAILURE;
	}

	g_option_context_free (context);

	if (backup) {
		actions++;
	}

	if (restore) {
		actions++;
	}

	if (index_file) {
		actions++;
	}

	if (import) {
		actions++;
	}

	if (actions > 1) {
		failed = _("Only one action (--backup, --restore, --index-file or --import) can be used at a time");
	} else if (actions > 0 && (!filenames || g_strv_length (filenames) < 1)) {
		failed = _("Missing one or more files which are required");
	} else if ((backup || restore) && (filenames && g_strv_length (filenames) > 1)) {
		failed = _("Only one file can be used with --backup and --restore");
	} else if (actions > 0 && (reindex_mime_types && g_strv_length (reindex_mime_types) > 0)) {
		failed = _("Actions (--backup, --restore, --index-file and --import) can not be used with --reindex-mime-type");
	} else {
		failed = NULL;
	}

	if (failed) {
		g_printerr ("%s\n\n", failed);
		return EXIT_FAILURE;
	}

	if (index_options_enabled ()) {
		return index_run ();
	}

	return index_run_default ();
}
