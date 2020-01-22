/*
 * Copyright © 2014 Red Hat, Inc
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 *
 * Authors:
 *       Alexander Larsson <alexl@redhat.com>
 *
 * Code borrowed from xdg-desktop-portal/src/xdp-utils.[ch]
 */

#include "config.h"

#include <glib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include "tracker-portal-utils.h"

#define DBUS_NAME_DBUS "org.freedesktop.DBus"
#define DBUS_INTERFACE_DBUS DBUS_NAME_DBUS
#define DBUS_PATH_DBUS "/org/freedesktop/DBus"

static GKeyFile *
parse_app_info_from_flatpak_info (int pid, GError **error)
{
	g_autofree char *root_path = NULL;
	int root_fd = -1;
	int info_fd = -1;
	struct stat stat_buf;
	g_autoptr(GError) local_error = NULL;
	g_autoptr(GMappedFile) mapped = NULL;
	g_autoptr(GKeyFile) metadata = NULL;
	g_autofree char *id = NULL;

	root_path = g_strdup_printf ("/proc/%u/root", pid);
	root_fd = openat (AT_FDCWD, root_path, O_RDONLY | O_NONBLOCK | O_DIRECTORY | O_CLOEXEC | O_NOCTTY);
	if (root_fd == -1) {
		/* Not able to open the root dir shouldn't happen. Probably the app died and
		 * we're failing due to /proc/$pid not existing. In that case fail instead
		 * of treating this as privileged. */
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
		             "Unable to open %s", root_path);
		return NULL;
	}

	metadata = g_key_file_new ();

	info_fd = openat (root_fd, ".flatpak-info", O_RDONLY | O_CLOEXEC | O_NOCTTY);
	close (root_fd);
	if (info_fd == -1) {
		if (errno == ENOENT) {
			/* No file => on the host, return NULL with no error */
			return NULL;
		}

		/* Some weird error => failure */
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
		             "Unable to open application info file");
		return NULL;
	}

	if (fstat (info_fd, &stat_buf) != 0 || !S_ISREG (stat_buf.st_mode)) {
		/* Some weird fd => failure */
		close (info_fd);
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
		             "Unable to open application info file");
		return NULL;
	}

	mapped = g_mapped_file_new_from_fd  (info_fd, FALSE, &local_error);
	if (mapped == NULL) {
		close (info_fd);
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
		             "Can't map .flatpak-info file: %s", local_error->message);
		return NULL;
	}

	if (!g_key_file_load_from_data (metadata,
	                                g_mapped_file_get_contents (mapped),
	                                g_mapped_file_get_length (mapped),
	                                G_KEY_FILE_NONE, &local_error)) {
		close (info_fd);
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
		             "Can't load .flatpak-info file: %s", local_error->message);
		return NULL;
	}

	close (info_fd);

	return g_steal_pointer (&metadata);
}

static GKeyFile *
tracker_connection_lookup_app_info_sync (GDBusConnection       *connection,
                                         const char            *sender,
                                         GCancellable          *cancellable,
                                         GError               **error)
{
	g_autoptr(GDBusMessage) msg = NULL;
	g_autoptr(GDBusMessage) reply = NULL;
	GVariant *body;
	g_autoptr(GVariantIter) iter = NULL;
	g_autoptr(GKeyFile) app_info = NULL;
	const char *key;
	GVariant *value;
	g_autoptr(GError) local_error = NULL;
	guint32 pid = 0;

	msg = g_dbus_message_new_method_call (DBUS_NAME_DBUS,
	                                      DBUS_PATH_DBUS,
	                                      DBUS_INTERFACE_DBUS,
	                                      "GetConnectionCredentials");
	g_dbus_message_set_body (msg, g_variant_new ("(s)", sender));

	reply = g_dbus_connection_send_message_with_reply_sync (connection, msg,
	                                                        G_DBUS_SEND_MESSAGE_FLAGS_NONE,
	                                                        30000,
	                                                        NULL,
	                                                        cancellable,
	                                                        error);
	if (reply == NULL)
		return NULL;

	if (g_dbus_message_get_message_type (reply) == G_DBUS_MESSAGE_TYPE_ERROR) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "Can't find peer app id");
		return NULL;
	}

	body = g_dbus_message_get_body (reply);

	g_variant_get (body, "(a{sv})", &iter);
	while (g_variant_iter_loop (iter, "{&sv}", &key, &value)) {
		if (strcmp (key, "ProcessID") == 0)
			pid = g_variant_get_uint32 (value);
	}

	if (pid == 0) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "Can't find app PID");
		return NULL;
	}

	app_info = parse_app_info_from_flatpak_info (pid, &local_error);

	if (app_info == NULL && local_error) {
		g_propagate_error (error, g_steal_pointer (&local_error));
		return NULL;
	}

	return g_steal_pointer (&app_info);
}

GKeyFile *
tracker_invocation_lookup_app_info_sync (GDBusMethodInvocation *invocation,
					 GCancellable          *cancellable,
					 GError               **error)
{
	GDBusConnection *connection = g_dbus_method_invocation_get_connection (invocation);
	const gchar *sender = g_dbus_method_invocation_get_sender (invocation);

	return tracker_connection_lookup_app_info_sync (connection, sender, cancellable, error);
}
