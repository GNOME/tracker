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

#include "tracker-daemon.h"
#include "tracker-export.h"
#include "tracker-extract.h"
#include "tracker-help.h"
#include "tracker-index.h"
#include "tracker-info.h"
#include "tracker-reset.h"
#include "tracker-search.h"
#include "tracker-sparql.h"
#include "tracker-sql.h"
#include "tracker-status.h"
#include "tracker-tag.h"

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
tracker_version (int argc, const char **argv)
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
	{ "daemon", tracker_daemon, NEED_WORK_TREE, N_("Start, stop, pause and list processes responsible for indexing content") },
	{ "export", tracker_export, NEED_WORK_TREE, N_("Export data from a Tracker database") },
	{ "extract", tracker_extract, NEED_WORK_TREE, N_("Extract information from a file") },
	{ "help", tracker_help, NEED_NOTHING, N_("Get help on how to use Tracker and any of these commands") },
	{ "info", tracker_info, NEED_WORK_TREE, N_("Show information known about local files or items indexed") }, 
	{ "index", tracker_index, NEED_NOTHING, N_("Backup, restore, import and (re)index by MIME type or file name") },
	{ "reset", tracker_reset, NEED_NOTHING,  N_("Reset or remove index and revert configurations to defaults") },
	{ "search", tracker_search, NEED_WORK_TREE, N_("Search for content indexed or show content by type") },
	{ "sparql", tracker_sparql, NEED_WORK_TREE, N_("Query and update the index using SPARQL or search, list and tree the ontology") },
	{ "sql", tracker_sql, NEED_WORK_TREE, N_("Query the database at the lowest level using SQL") },
	{ "status", tracker_status, NEED_NOTHING, N_("Show the indexing progress, content statistics and index state") },
	{ "tag", tracker_tag, NEED_WORK_TREE, N_("Create, list or delete tags for indexed content") },
	{ "version", tracker_version, NEED_NOTHING, N_("Show the license and version in use") },
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
	const char *cmd = argv[0];
	int i;

	/* Turn "tracker cmd --help" into "tracker help cmd" */
	if (argc > 1 && !strcmp (argv[1], "--help")) {
		argv[1] = argv[0];
		argv[0] = cmd = "help";
	}

	for (i = 0; i < G_N_ELEMENTS (commands); i++) {
		struct cmd_struct *p = commands + i;

		if (strcmp (p->cmd, cmd)) {
			continue;
		}

		exit (run_builtin (p, argc, argv));
	}

	g_printerr (_("“%s” is not a tracker command. See “tracker --help”"), argv[0]);
	g_printerr ("\n");
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

	for (i = 0; i < G_N_ELEMENTS(commands); i++) {
		if (longest < strlen (commands[i].cmd))
			longest = strlen(commands[i].cmd);
	}

	puts (_("Available tracker commands are:"));

	for (i = 0; i < G_N_ELEMENTS(commands); i++) {
		/* Don't list version in commands */
		if (!strcmp (commands[i].cmd, "version") ||
		    !strcmp (commands[i].cmd, "help")) {
			continue;
		}

		g_print ("   %s   ", commands[i].cmd);
		mput_char (' ', longest - strlen (commands[i].cmd));
		puts (_(commands[i].help));
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
main (int original_argc, char **original_argv)
{
	const char **argv = (const char **) original_argv;
	int argc = original_argc;

	setlocale (LC_ALL, "");

	bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);

	argv++;
	argc--;

	if (argc > 0) {
		/* For cases like --version */
		if (g_str_has_prefix (argv[0], "--")) {
			argv[0] += 2;
		}
	} else {
		/* The user didn't specify a command; give them help */
		print_usage ();
		exit (1);
	}

	handle_command (argc, argv);

	if ((char **) argv != original_argv) {
		g_strfreev ((char **) argv);
	}

	return EXIT_FAILURE;
}
