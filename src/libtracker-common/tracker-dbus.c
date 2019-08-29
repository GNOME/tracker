/*
 * Copyright (C) 2008, Nokia <ivan.frade@nokia.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 */

#include "config.h"

#include <gio/gio.h>
#include <gio/gunixinputstream.h>
#include <gio/gunixoutputstream.h>

#ifdef __OpenBSD__
#include <sys/param.h>
#include <sys/proc.h>
#include <sys/sysctl.h>
#include <fcntl.h>
#include <kvm.h>
#endif

#ifdef __sun
#include <procfs.h>
#endif

#include "tracker-dbus.h"
#include "tracker-log.h"

/* How long clients can exist since last D-Bus call before being
 * cleaned up.
 */
#define CLIENT_CLEAN_UP_TIME  300

typedef struct {
	gchar *sender;
	gchar *binary;
	gulong pid;
	guint clean_up_id;
	gint n_active_requests;
} ClientData;

struct _TrackerDBusRequest {
	guint request_id;
	ClientData *cd;
};

static gboolean client_lookup_enabled;
static GHashTable *clients;
static GDBusConnection *connection;

static void     client_data_free    (gpointer data);
static gboolean client_clean_up_cb (gpointer data);

inline GBusType
tracker_ipc_bus (void)
{
	const gchar *bus = g_getenv ("TRACKER_BUS_TYPE");

	if (G_UNLIKELY (bus != NULL &&
	                g_ascii_strcasecmp (bus, "system") == 0)) {
		return G_BUS_TYPE_SYSTEM;
	}

	return G_BUS_TYPE_SESSION;
}

static gboolean
clients_init (void)
{
	GError *error = NULL;
	connection = g_bus_get_sync (TRACKER_IPC_BUS, NULL, &error);

	if (error) {
		g_critical ("Could not connect to the D-Bus session bus, %s",
		            error ? error->message : "no error given.");
		g_clear_error (&error);
		connection = NULL;
	}

	clients = g_hash_table_new_full (g_str_hash,
	                                 g_str_equal,
	                                 NULL,
	                                 client_data_free);

	return TRUE;
}

static gboolean
clients_shutdown (void)
{
	if (clients) {
		g_hash_table_unref (clients);
		clients = NULL;
	}

	if (connection) {
		g_object_unref (connection);
		connection = NULL;
	}

	return TRUE;
}

static void
client_data_free (gpointer data)
{
	ClientData *cd = data;

	if (!cd) {
		return;
	}

	g_source_remove (cd->clean_up_id);

	g_free (cd->sender);
	g_free (cd->binary);

	g_slice_free (ClientData, cd);
}

static ClientData *
client_data_new (gchar *sender)
{
	ClientData *cd;
	gboolean get_binary = TRUE;
	GError *error = NULL;

	cd = g_slice_new0 (ClientData);
	cd->sender = sender;

	if (connection) {
		GVariant *v;

		v = g_dbus_connection_call_sync (connection,
		                                 "org.freedesktop.DBus",
		                                 "/org/freedesktop/DBus",
		                                 "org.freedesktop.DBus",
		                                 "GetConnectionUnixProcessID",
		                                 g_variant_new ("(s)", sender),
		                                 G_VARIANT_TYPE ("(u)"),
		                                 G_DBUS_CALL_FLAGS_NONE,
		                                 -1,
		                                 NULL,
		                                 &error);

		if (!error) {
			g_variant_get (v, "(u)", &cd->pid);
			g_variant_unref (v);
		} else {
			g_error_free (error);
		}
	}

	if (get_binary) {
#ifndef __OpenBSD__
		gchar *filename;
		gchar *pid_str;
		gchar *contents = NULL;
		GError *error = NULL;
		gchar **strv;
#ifdef __sun /* Solaris */
		psinfo_t psinfo = { 0 };
#endif

		pid_str = g_strdup_printf ("%ld", cd->pid);
		filename = g_build_filename (G_DIR_SEPARATOR_S,
		                             "proc",
		                             pid_str,
#ifdef __sun /* Solaris */
		                             "psinfo",
#else
		                             "cmdline",
#endif
		                             NULL);
		g_free (pid_str);

		if (!g_file_get_contents (filename, &contents, NULL, &error)) {
			g_warning ("Could not get process name from id %ld, %s",
			           cd->pid,
			           error ? error->message : "no error given");
			g_clear_error (&error);
			g_free (filename);
			return cd;
		}

		g_free (filename);

#ifdef __sun /* Solaris */
		memcpy (&psinfo, contents, sizeof (psinfo));
		/* won't work with paths containing spaces :( */
		strv = g_strsplit (psinfo.pr_psargs, " ", 2);
#else
		strv = g_strsplit (contents, "^@", 2);
#endif
		if (strv && strv[0]) {
			cd->binary = g_path_get_basename (strv[0]);
		}

		g_strfreev (strv);
		g_free (contents);
#else
		gint nproc;
		struct kinfo_proc *kp;
		kvm_t *kd;
		gchar **strv;

		if ((kd = kvm_openfiles (NULL, NULL, NULL, KVM_NO_FILES, NULL)) == NULL)
			return cd;

		if ((kp = kvm_getprocs (kd, KERN_PROC_PID, cd->pid, sizeof (*kp), &nproc)) == NULL) {
			g_warning ("Could not get process name: %s", kvm_geterr (kd));
			kvm_close(kd);
			return cd;
		}

		if ((kp->p_flag & P_SYSTEM) != 0) {
			kvm_close(kd);
			return cd;
		}

		strv = kvm_getargv (kd, kp, 0);

		if (strv == NULL) {
			kvm_close(kd);
			return cd;
		} else {
			cd->binary = g_path_get_basename (strv[0]);
			kvm_close(kd);
		}
#endif
	}

	return cd;
}

static gboolean
client_clean_up_cb (gpointer data)
{
	ClientData *cd;

	cd = data;

	g_debug ("Removing D-Bus client data for '%s' (pid: %lu) with id:'%s'",
	         cd->binary, cd->pid, cd->sender);
	g_hash_table_remove (clients, cd->sender);

	if (g_hash_table_size (clients) < 1) {
		/* Clean everything up. */
		clients_shutdown ();
	}

	return FALSE;
}

static ClientData *
client_get_for_sender (const gchar *sender)
{
	ClientData *cd;

	if (!client_lookup_enabled) {
		return NULL;
	}

	/* Only really done with tracker-extract where we use
	 * functions from the command line with dbus code in them.
	 */
	if (!sender) {
		return NULL;
	}

	if (G_UNLIKELY (!clients)) {
		clients_init ();
	}

	cd = g_hash_table_lookup (clients, sender);
	if (!cd) {
		gchar *sender_dup;

		sender_dup = g_strdup (sender);
		cd = client_data_new (sender_dup);
		g_hash_table_insert (clients, sender_dup, cd);
	}

	if (cd->clean_up_id) {
		g_source_remove (cd->clean_up_id);
		cd->clean_up_id = 0;
	}

	cd->n_active_requests++;

	return cd;
}

GQuark
tracker_dbus_error_quark (void)
{
	return g_quark_from_static_string (TRACKER_DBUS_ERROR_DOMAIN);
}

gchar **
tracker_dbus_slist_to_strv (GSList *list)
{
	GSList  *l;
	gchar  **strv;
	gint     i = 0;

	strv = g_new0 (gchar*, g_slist_length (list) + 1);

	for (l = list; l != NULL; l = l->next) {
		if (!g_utf8_validate (l->data, -1, NULL)) {
			g_message ("Could not add string:'%s' to GStrv, invalid UTF-8",
			           (gchar*) l->data);
			continue;
		}

		strv[i++] = g_strdup (l->data);
	}

	strv[i] = NULL;

	return strv;
}

static guint
get_next_request_id (void)
{
	static guint request_id = 1;

	return request_id++;
}

TrackerDBusRequest *
tracker_dbus_request_begin (const gchar *sender,
                            const gchar *format,
                            ...)
{
	TrackerDBusRequest *request;
	gchar *str;
	va_list args;

	va_start (args, format);
	str = g_strdup_vprintf (format, args);
	va_end (args);

	request = g_slice_new (TrackerDBusRequest);
	request->request_id = get_next_request_id ();
	request->cd = client_get_for_sender (sender);

	g_debug ("<--- [%d%s%s|%lu] %s",
	         request->request_id,
	         request->cd ? "|" : "",
	         request->cd ? request->cd->binary : "",
	         request->cd ? request->cd->pid : 0,
	         str);

	g_free (str);

	return request;
}

void
tracker_dbus_request_end (TrackerDBusRequest *request,
                          GError             *error)
{
	if (!error) {
		g_debug ("---> [%d%s%s|%lu] Success, no error given",
			 request->request_id,
			 request->cd ? "|" : "",
			 request->cd ? request->cd->binary : "",
			 request->cd ? request->cd->pid : 0);
	} else {
		g_message ("---> [%d%s%s|%lu] Failed, %s",
			   request->request_id,
			   request->cd ? "|" : "",
			   request->cd ? request->cd->binary : "",
			   request->cd ? request->cd->pid : 0,
			   error->message);
	}

	if (request->cd) {
		request->cd->n_active_requests--;

		if (request->cd->n_active_requests == 0) {
			request->cd->clean_up_id = g_timeout_add_seconds (CLIENT_CLEAN_UP_TIME, client_clean_up_cb, request->cd);
		}
	}

	g_slice_free (TrackerDBusRequest, request);
}

void
tracker_dbus_request_info (TrackerDBusRequest    *request,
                           const gchar           *format,
                           ...)
{
	gchar *str;
	va_list args;

	va_start (args, format);
	str = g_strdup_vprintf (format, args);
	va_end (args);

	g_info ("---- [%d%s%s|%lu] %s",
	        request->request_id,
	        request->cd ? "|" : "",
	        request->cd ? request->cd->binary : "",
	        request->cd ? request->cd->pid : 0,
	        str);
	g_free (str);
}

void
tracker_dbus_request_comment (TrackerDBusRequest    *request,
                              const gchar           *format,
                              ...)
{
	gchar *str;
	va_list args;

	va_start (args, format);
	str = g_strdup_vprintf (format, args);
	va_end (args);

	g_message ("---- [%d%s%s|%lu] %s",
	           request->request_id,
	           request->cd ? "|" : "",
	           request->cd ? request->cd->binary : "",
	           request->cd ? request->cd->pid : 0,
	           str);
	g_free (str);
}

void
tracker_dbus_request_debug (TrackerDBusRequest    *request,
                            const gchar           *format,
                            ...)
{
	gchar *str;
	va_list args;

	va_start (args, format);
	str = g_strdup_vprintf (format, args);
	va_end (args);

	g_debug ("---- [%d%s%s|%lu] %s",
	         request->request_id,
	         request->cd ? "|" : "",
	         request->cd ? request->cd->binary : "",
	         request->cd ? request->cd->pid : 0,
	         str);
	g_free (str);
}

void
tracker_dbus_enable_client_lookup (gboolean enabled)
{
	/* If this changed and we disabled everything, simply shut it
	 * all down.
	 */
	if (client_lookup_enabled != enabled && !enabled) {
		clients_shutdown ();
	}

	client_lookup_enabled = enabled;
}

TrackerDBusRequest *
tracker_g_dbus_request_begin (GDBusMethodInvocation *invocation,
                              const gchar           *format,
                              ...)
{
	TrackerDBusRequest *request;
	gchar *str;
	const gchar *sender;
	va_list args;

	va_start (args, format);
	str = g_strdup_vprintf (format, args);
	va_end (args);

	sender = g_dbus_method_invocation_get_sender (invocation);
	request = tracker_dbus_request_begin (sender, "%s", str);

	g_free (str);

	return request;
}

gboolean
tracker_dbus_request_name (GDBusConnection  *connection,
                           const gchar      *name,
                           GError          **error)
{
	GError *inner_error = NULL;
	GVariant *reply;
	gint rval;

	reply = g_dbus_connection_call_sync (connection,
	                                     "org.freedesktop.DBus",
	                                     "/org/freedesktop/DBus",
	                                     "org.freedesktop.DBus",
	                                     "RequestName",
	                                     g_variant_new ("(su)",
	                                                    name,
	                                                    0x4 /* DBUS_NAME_FLAG_DO_NOT_QUEUE */),
	                                     G_VARIANT_TYPE ("(u)"),
	                                     0, -1, NULL, &inner_error);
	if (inner_error) {
		g_propagate_prefixed_error (error, inner_error,
		                            "Could not acquire name:'%s'. ",
		                            name);
		return FALSE;
	}

	g_variant_get (reply, "(u)", &rval);
	g_variant_unref (reply);

	if (rval != 1 /* DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER */) {
		g_set_error (error,
		             G_DBUS_ERROR,
		             G_DBUS_ERROR_ADDRESS_IN_USE,
		             "D-Bus service name:'%s' is already taken, "
		             "perhaps the application is already running?",
		             name);
		return FALSE;
	}

	return TRUE;
}
