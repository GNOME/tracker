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

#include <unistd.h>
#include <errno.h>

#include <glib/gi18n.h>

#include "tracker-help.h"

static void
setup_man_path (void)
{
	GString *new_path = g_string_new ("");
	const char *old_path = g_getenv ("MANPATH");

	/* We should always put ':' after our path. If there is no
	 * old_path, the ':' at the end will let 'man' to try
	 * system-wide paths after ours to find the manual page. If
	 * there is old_path, we need ':' as delimiter. */
	g_string_append (new_path, MANDIR);
	g_string_append_c (new_path, ':');

	if (old_path) {
		g_string_append (new_path, old_path); 
	}

	g_setenv ("MANPATH", new_path->str, TRUE);

	g_string_free (new_path, TRUE);
}

static int
exec_man_man (const char *path, const char *page)
{
	if (!path) {
		path = "man";
	}

	execlp (path, "man", page, (char *) NULL);
	g_warning(_("failed to exec “%s”: %s"), path, strerror (errno));

	return -1;
}

static int
exec_man_cmd (const char *cmd, const char *page)
{
	gchar *shell_cmd;

	shell_cmd = g_strdup_printf ("%s %s", cmd, page);
	execl ("/bin/sh", "sh", "-c", shell_cmd, (char *) NULL);
	g_warning (_("failed to exec “%s”: %s"), cmd, strerror (errno));
	g_free (shell_cmd);

	return -1;
}

static char *
cmd_to_page (const char *cmd)
{
	if (!cmd) {
		return g_strdup (COMMANDNAME);
	} else if (g_str_has_prefix (cmd, COMMANDNAME "-")) {
		return g_strdup (cmd);
	} else {
		return g_strdup_printf (COMMANDNAME "-%s", cmd);
	}
}

int
tracker_help_show_man_page (const char *cmd)
{
	const char *page = cmd_to_page (cmd);

	setup_man_path ();

	if (0) {
		exec_man_cmd ("man", page);
	}

	return exec_man_man ("man", page);
}

