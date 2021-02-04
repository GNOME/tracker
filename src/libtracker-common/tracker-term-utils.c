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

#include "tracker-term-utils.h"

#include <gio/gio.h>
#include <glib-unix.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>

static guint n_columns = 0;
static guint n_rows = 0;
static GSubprocess *pager = NULL;
static gint stdout_fd = 0;
static guint signal_handler_id = 0;

gchar *
tracker_term_ellipsize (const gchar          *str,
                        gint                  max_len,
                        TrackerEllipsizeMode  mode)
{
	gsize size = strlen (str);
	glong len = g_utf8_strlen (str, size);
	const gchar *begin, *end, *pos;
	gchar *substr, *retval;
	gint i;

	if (len < max_len)
		return g_strdup (str);

	/* Account for the ellipsizing char */
	max_len--;
	if (max_len <= 0)
		return g_strdup ("…");

	begin = str;
	end = &str[size];

	if (mode == TRACKER_ELLIPSIZE_END) {
		pos = begin;
		for (i = 0; i < max_len; i++)
			pos = g_utf8_find_next_char (pos, end);

		substr = g_strndup (begin, pos - begin);
		retval = g_strdup_printf ("%s…", substr);
		g_free (substr);
	} else {
		pos = end;
		for (i = 0; i < max_len; i++)
			pos = g_utf8_find_prev_char (begin, pos);

		substr = g_strndup (pos, end - pos);
		retval = g_strdup_printf ("…%s", substr);
		g_free (substr);
	}

	return retval;
}

static gboolean
fd_term_dimensions (gint  fd,
                    gint *cols,
                    gint *rows)
{
        struct winsize ws = {};

        if (ioctl(fd, TIOCGWINSZ, &ws) < 0)
                return FALSE;

        if (ws.ws_col <= 0 || ws.ws_row <= 0)
                return FALSE;

        *cols = ws.ws_col;
        *rows = ws.ws_row;

        return TRUE;
}

void
tracker_term_dimensions (guint *columns,
                         guint *rows)
{
	if (n_columns == 0 || n_rows == 0)
		fd_term_dimensions (STDOUT_FILENO, &n_columns, &n_rows);

	if (n_columns <= 0)
		n_columns = 80;
	if (n_rows <= 0)
		n_rows = 24;

	if (columns)
		*columns = n_columns;
	if (rows)
		*rows = n_rows;
}

gboolean
tracker_term_is_tty (void)
{
	return isatty (STDOUT_FILENO) > 0;
}

static gboolean
ignore_signal_cb (gpointer user_data)
{
	return G_SOURCE_CONTINUE;
}

static gchar *
best_pager (void)
{
	guint i;
	gchar *command;
	const gchar *pagers[] = {
		"pager",
		"less",
		"most",
		"more",
	};

	for (i = 0; i < G_N_ELEMENTS (pagers); i++) {
		command = g_find_program_in_path (pagers[i]);
		if (command)
			return command;
	}

	return NULL;
}

gboolean
tracker_term_pipe_to_pager (void)
{
	GSubprocessLauncher *launcher;
	gchar *pager_command;
	gint fds[2];

	if (!tracker_term_is_tty ())
		return FALSE;

	if (pipe2 (fds, O_CLOEXEC) < 0)
		return FALSE;

	pager_command = best_pager ();
	if (!pager_command)
		return FALSE;

	/* Ensure this is cached before we redirect to the pager */
	tracker_term_dimensions (NULL, NULL);

	launcher = g_subprocess_launcher_new (G_SUBPROCESS_FLAGS_NONE);
	g_subprocess_launcher_take_stdin_fd (launcher, fds[0]);
	g_subprocess_launcher_setenv (launcher, "LESS", "FRSXMK", TRUE);

	pager = g_subprocess_launcher_spawn (launcher, NULL, pager_command, NULL);
	g_free (pager_command);

	stdout_fd = dup (STDOUT_FILENO);
	close (fds[0]);

	if (dup2(fds[1], STDOUT_FILENO) < 0)
	        return FALSE;

	close (fds[1]);
	signal_handler_id = g_unix_signal_add (SIGINT, ignore_signal_cb, NULL);

	return TRUE;
}

gboolean
tracker_term_pager_close (void)
{
	if (!pager)
		return FALSE;

	fflush (stdout);

	/* Restore stdout */
	dup2 (stdout_fd, STDOUT_FILENO);
	close (stdout_fd);

	g_subprocess_send_signal (pager, SIGCONT);
	g_subprocess_wait (pager, NULL, NULL);
	g_source_remove (signal_handler_id);

	return TRUE;
}
