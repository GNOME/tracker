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
#include <locale.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <gio/gio.h>

#include <libtracker-common/tracker-common.h>

#include "tracker-endpoint.h"
#include "tracker-export.h"
#include "tracker-help.h"
#include "tracker-import.h"
#include "tracker-info.h"
#include "tracker-sparql.h"
#include "tracker-sql.h"

const char usage_string[] =
	"tracker [--version] [--help]\n"
	"               <command> [<args>]";

const char usage_more_info_string[] =
	N_("See “tracker help <command>” to read about a specific subcommand.");

const char about[] =
	"Tracker " PACKAGE_VERSION "\n"
	"\n"
	"This program is free software and comes without any warranty.\n"
	"It is licensed under version 2 or later of the General Public "
	"License which can be viewed at:\n"
	"\n"
	"  http://www.gnu.org/licenses/gpl.txt"
	"\n";

static void print_usage (void);

static int
tracker_help (int argc, const char **argv)
{
	if (!argv[0] || !argv[1]) {
		print_usage ();
		return 0;
	}

	tracker_help_show_man_page (argv[1]);

	return 0;
}

static int
print_version (void)
{
	puts (about);
	return 0;
}

/*
 * require working tree to be present -- anything uses this needs
 * RUN_SETUP for reading from the configuration file.
 */
#define NEED_NOTHING		(1<<0)
#define NEED_WORK_TREE		(1<<1)

struct cmd_struct {
	const char *cmd;
	int (*fn)(int, const char **);
	int option;
	const char *help;
};

static struct cmd_struct commands[] = {
	{ "help", tracker_help, NEED_NOTHING, N_("Get help on how to use Tracker and any of these commands") },
	{ "endpoint", tracker_endpoint, NEED_NOTHING, N_("Create a SPARQL endpoint") },
	{ "export", tracker_export, NEED_WORK_TREE, N_("Export data from a Tracker database") },
	{ "import", tracker_import, NEED_WORK_TREE, N_("Import data into a Tracker database") },
	{ "info", tracker_info, NEED_WORK_TREE, N_("Show information known about local files or items indexed") },
	{ "sparql", tracker_sparql, NEED_WORK_TREE, N_("Query and update the index using SPARQL or search, list and tree the ontology") },
	{ "sql", tracker_sql, NEED_WORK_TREE, N_("Query the database at the lowest level using SQL") },
};

static int
run_builtin (struct cmd_struct *p, int argc, const char **argv)
{
	int status, help;

	help = argc == 2 && !strcmp (argv[1], "-h");

	if (!help && p->option & NEED_WORK_TREE) {
		/* Check we have working tree */
		/* FIXME: Finish */
	}

	status = p->fn (argc, argv);
	if (status) {
		return status;
	}

	return 0;
}

static void
handle_command (int argc, const char **argv)
{
	gchar *log_filename = NULL;
	char *cmd = g_path_get_basename (argv[0]);
	int i;

	tracker_log_init (0, &log_filename);
	if (log_filename != NULL) {
		g_message ("Using log file:'%s'", log_filename);
		g_free (log_filename);
	}

	for (i = 0; i < G_N_ELEMENTS (commands); i++) {
		struct cmd_struct *p = commands + i;

		if (strcmp (p->cmd, cmd)) {
			continue;
		}

		g_free (cmd);
		exit (run_builtin (p, argc, argv));
	}

	g_printerr (_("“%s” is not a tracker command. See “tracker --help”"), argv[0]);
	g_printerr ("\n");
	g_free (cmd);
	exit (EXIT_FAILURE);
}

static inline void
mput_char (char c, unsigned int num)
{
	while (num--) {
		putchar (c);
	}
}

static void
print_usage_list_cmds (void)
{
	int i, longest = 0;
	GList *extra_commands = NULL;
	GFileEnumerator *enumerator;
	GFileInfo *info;
	GFile *dir;
	GError *error = NULL;
	const gchar *subcommands_dir;

	subcommands_dir = g_getenv ("TRACKER_CLI_SUBCOMMANDS_DIR");
	if (!subcommands_dir) {
		subcommands_dir = SUBCOMMANDSDIR;
	}

	for (i = 0; i < G_N_ELEMENTS(commands); i++) {
		if (longest < strlen (commands[i].cmd))
			longest = strlen(commands[i].cmd);
	}

	puts (_("Available tracker commands are:"));

	for (i = 0; i < G_N_ELEMENTS(commands); i++) {
		g_print ("   %s   ", commands[i].cmd);
		mput_char (' ', longest - strlen (commands[i].cmd));
		puts (_(commands[i].help));
	}

	dir = g_file_new_for_path (subcommands_dir);
	enumerator = g_file_enumerate_children (dir,
	                                        G_FILE_ATTRIBUTE_STANDARD_NAME ","
	                                        G_FILE_ATTRIBUTE_STANDARD_IS_SYMLINK ","
	                                        G_FILE_ATTRIBUTE_STANDARD_SYMLINK_TARGET,
	                                        G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
	                                        NULL, &error);
	g_object_unref (dir);

	if (enumerator) {
		while ((info = g_file_enumerator_next_file (enumerator, NULL, NULL)) != NULL) {
			/* Filter builtin commands */
			if (g_file_info_get_is_symlink (info) &&
			    g_strcmp0 (g_file_info_get_symlink_target (info), BINDIR "/tracker") == 0)
				continue;

			extra_commands = g_list_prepend (extra_commands,
			                                 g_strdup (g_file_info_get_name (info)));
			g_object_unref (info);
		}

		g_object_unref (enumerator);
	} else {
		g_warning ("Failed to list extra commands: %s", error->message);
	}

	if (extra_commands) {
		extra_commands = g_list_sort (extra_commands, (GCompareFunc) g_strcmp0);

		g_print ("\n");
		puts (_("Additional / third party commands are:"));

		while (extra_commands) {
			g_print ("   %s   \n", (gchar *) extra_commands->data);
			g_free (extra_commands->data);
			extra_commands = g_list_remove (extra_commands, extra_commands->data);
		}
	}
}

static void
print_usage (void)
{
	g_print ("usage: %s\n\n", usage_string);
	print_usage_list_cmds ();
	g_print ("\n%s\n", _(usage_more_info_string));
}

int
main (int argc, char *argv[])
{
	gboolean basename_is_bin = FALSE;
	gchar *command_basename;
	const gchar *subcommands_dir;

	setlocale (LC_ALL, "");

	bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);

	command_basename = g_path_get_basename (argv[0]);
	basename_is_bin = g_strcmp0 (command_basename, COMMANDNAME) == 0;
	g_free (command_basename);

	subcommands_dir = g_getenv ("TRACKER_CLI_SUBCOMMANDS_DIR");
	if (!subcommands_dir) {
		subcommands_dir = SUBCOMMANDSDIR;
	}

	if (g_path_is_absolute (argv[0]) &&
	    g_str_has_prefix (argv[0], subcommands_dir)) {
		/* This is a subcommand call */
		handle_command (argc, (const gchar **) argv);
		exit (EXIT_FAILURE);
	} else if (basename_is_bin) {
		/* This is a call to the main tracker executable,
		 * look up and exec the subcommand if any.
		 */
		if (argc > 1) {
			const gchar *subcommand = argv[1];
			gchar *path;

			if (g_strcmp0 (subcommand, "--version") == 0) {
				print_version ();
				exit (EXIT_SUCCESS);
			} else if (g_strcmp0 (subcommand, "--help") == 0) {
				subcommand = "help";
			}

			path = g_build_filename (subcommands_dir, subcommand, NULL);

			if (g_file_test (path, G_FILE_TEST_EXISTS)) {
				/* Manipulate argv in place, in order to launch subcommand */
				argv[1] = path;
				execv (path, &argv[1]);
			} else {
				print_usage ();
			}

			g_free (path);
		} else {
			/* The user didn't specify a command; give them help */
			print_usage ();
			exit (EXIT_SUCCESS);
		}
	}

	return EXIT_FAILURE;
}
