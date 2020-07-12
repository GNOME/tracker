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

#include <libtracker-sparql/tracker-sparql.h>
#include <libtracker-common/tracker-common.h>

#include "tracker-portal.h"
#include "tracker-portal-endpoint.h"
#include "tracker-portal-utils.h"

typedef struct _TrackerPortal TrackerPortal;
typedef struct _TrackerSession TrackerSession;

struct _TrackerSession
{
	TrackerSparqlConnection *connection;
	GDBusConnection *dbus_connection;
	TrackerEndpoint *endpoint;
	gchar *object_path;
};

struct _TrackerPortal
{
	GObject parent_instance;
	GDBusConnection *dbus_connection;
	guint register_id;
	GDBusNodeInfo *node_info;
	GCancellable *cancellable;
	GArray *sessions;
	guint64 session_ids;
};

enum
{
	PROP_DBUS_CONNECTION = 1,
	N_PROPS
};

static GParamSpec *props[N_PROPS] = { 0 };

static void tracker_portal_initable_iface_init (GInitableIface *iface);

G_DEFINE_TYPE_WITH_CODE (TrackerPortal, tracker_portal, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE, tracker_portal_initable_iface_init))

#define TRACKER_GROUP_NAME "Policy Tracker3"
#define DEFAULT_URI_SUFFIX ":/org/freedesktop/Tracker3/Endpoint"

static const gchar portal_xml[] =
	"<node>"
	"  <interface name='org.freedesktop.portal.Tracker'>"
	"    <method name='CreateSession'>"
	"      <arg type='s' name='service' direction='in' />"
	"      <arg type='o' name='result' direction='out' />"
	"    </method>"
	"    <method name='CloseSession'>"
	"      <arg type='o' name='object_path' direction='in' />"
	"    </method>"
	"  </interface>"
	"</node>";

static void
tracker_portal_set_property (GObject       *object,
                             guint          prop_id,
                             const GValue  *value,
                             GParamSpec    *pspec)
{
	TrackerPortal *portal = TRACKER_PORTAL (object);

	switch (prop_id) {
	case PROP_DBUS_CONNECTION:
		portal->dbus_connection = g_value_dup_object (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
tracker_portal_get_property (GObject    *object,
                             guint       prop_id,
                             GValue     *value,
                             GParamSpec *pspec)
{
	TrackerPortal *portal = TRACKER_PORTAL (object);

	switch (prop_id) {
	case PROP_DBUS_CONNECTION:
		g_value_set_object (value, portal->dbus_connection);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
tracker_portal_finalize (GObject *object)
{
	TrackerPortal *portal = TRACKER_PORTAL (object);

	g_debug ("Finalizing Tracker portal");

	g_clear_pointer (&portal->sessions, g_array_unref);

	if (portal->register_id != 0) {
		g_dbus_connection_unregister_object (portal->dbus_connection,
		                                     portal->register_id);
		portal->register_id = 0;
	}

	g_clear_object (&portal->dbus_connection);
	g_clear_pointer (&portal->node_info,
	                 g_dbus_node_info_unref);

	G_OBJECT_CLASS (tracker_portal_parent_class)->finalize (object);
	g_debug ("Tracker portal finalized");
}

static void
tracker_portal_class_init (TrackerPortalClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->set_property = tracker_portal_set_property;
	object_class->get_property = tracker_portal_get_property;
	object_class->finalize = tracker_portal_finalize;

	props[PROP_DBUS_CONNECTION] =
		g_param_spec_object ("dbus-connection",
		                     "DBus connection",
		                     "DBus connection",
		                     G_TYPE_DBUS_CONNECTION,
		                     G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);

	g_object_class_install_properties (object_class, N_PROPS, props);
}

static void
clear_session (gpointer user_data)
{
	TrackerSession *session = user_data;

	g_debug ("Closing session '%s'", session->object_path);
	g_clear_object (&session->endpoint);
	g_clear_object (&session->connection);
	g_clear_object (&session->dbus_connection);
	g_clear_pointer (&session->object_path, g_free);
}

static void
tracker_portal_init (TrackerPortal *portal)
{
	portal->sessions = g_array_new (FALSE, TRUE, sizeof (TrackerSession));
	g_array_set_clear_func (portal->sessions, clear_session);
}

static void
endpoint_closed_cb (TrackerPortalEndpoint *endpoint,
                    gpointer               user_data)
{
	TrackerPortal *portal = user_data;
	gint i;

	for (i = 0; i < portal->sessions->len; i++) {
		TrackerSession *session;

		session = &g_array_index (portal->sessions, TrackerSession, i);
		if (session->endpoint != TRACKER_ENDPOINT (endpoint))
			continue;

		g_array_remove_index_fast (portal->sessions, i);
		break;
	}
}

static GStrv
load_client_configuration (GDBusMethodInvocation  *invocation,
                           const gchar            *service_uri,
                           GError                **error)
{
	g_autoptr (GKeyFile) flatpak_info = NULL;
	GError *inner_error = NULL;
	GStrv graphs;

	flatpak_info = tracker_invocation_lookup_app_info_sync (invocation,
	                                                        NULL, &inner_error);
	if (!flatpak_info) {
		GStrv default_graphs = { NULL };

		if (inner_error) {
			g_warning ("Error reading .flatpak-info.");
			g_propagate_error (error, inner_error);
			return NULL;
		}

		g_debug ("No .flatpak-info found, peer is not sandboxed");
		/* Still, fishy. Allow access to no graphs */
		return g_strdupv (default_graphs);
	}

	graphs = g_key_file_get_string_list (flatpak_info,
	                                     TRACKER_GROUP_NAME,
	                                     service_uri,
	                                     NULL, NULL);

	if (!graphs && g_str_has_suffix (service_uri, DEFAULT_URI_SUFFIX)) {
		gchar *default_service_uri;

		default_service_uri = g_strndup (service_uri,
		                                 strlen (service_uri) -
		                                 strlen (DEFAULT_URI_SUFFIX));

		graphs = g_key_file_get_string_list (flatpak_info,
		                                     TRACKER_GROUP_NAME,
		                                     default_service_uri,
		                                     NULL, error);
		g_free (default_service_uri);
	}

	if (!graphs) {
		g_debug ("Service '%s' not found in Tracker policy", service_uri);
		return NULL;
	}

	return graphs;
}

static void
portal_iface_method_call (GDBusConnection       *connection,
                          const gchar           *sender,
                          const gchar           *object_path,
                          const gchar           *interface_name,
                          const gchar           *method_name,
                          GVariant              *parameters,
                          GDBusMethodInvocation *invocation,
                          gpointer               user_data)
{
	TrackerPortal *portal = user_data;

	if (g_strcmp0 (method_name, "CreateSession") == 0) {
		g_autofree gchar *uri = NULL;
		g_autofree gchar *service = NULL;
		g_autofree gchar *object_path = NULL;
		g_autofree gchar *session_object_path = NULL;
		g_autoptr(GDBusConnection) dbus_connection = NULL;
		g_autoptr(TrackerSparqlConnection) connection = NULL;
		g_autoptr(TrackerEndpoint) endpoint = NULL;
		g_autoptr(GError) error = NULL;
		g_auto(GStrv) graphs = NULL;
		GBusType bus_type;
		TrackerSession session;

		g_variant_get (parameters, "(s)", &uri);
		g_debug ("Creating session for service URI '%s'", uri);

		graphs = load_client_configuration (invocation, uri, &error);
		if (!graphs) {
			g_debug ("Session rejected by policy");
			g_dbus_method_invocation_return_gerror (invocation, error);
			return;
		}

		if (!tracker_util_parse_dbus_uri (uri,
		                                  &bus_type,
		                                  &service,
		                                  &object_path)) {
			g_debug ("Could not parse URI '%s'", uri);
			g_dbus_method_invocation_return_error (invocation,
			                                       G_DBUS_ERROR,
			                                       G_DBUS_ERROR_INVALID_ARGS,
			                                       "Service cannot be parsed");
			return;
		}

		dbus_connection = g_bus_get_sync (bus_type, NULL, &error);

		if (!dbus_connection) {
			g_debug ("Could not get bus connection");
			g_dbus_method_invocation_return_gerror (invocation, error);
			return;
		}

		connection = tracker_sparql_connection_bus_new (service, object_path, dbus_connection, &error);
		if (!connection) {
			g_debug ("Could not stablish connection to service");
			g_dbus_method_invocation_return_gerror (invocation, error);
			return;
		}

		session_object_path = g_strdup_printf ("/org/freedesktop/portal/Tracker/Session_%" G_GUINT64_FORMAT,
		                                       portal->session_ids++);

		endpoint = tracker_portal_endpoint_new (connection,
		                                        dbus_connection,
		                                        session_object_path,
		                                        sender,
		                                        (const gchar * const *) graphs,
		                                        NULL,
		                                        &error);
		if (!endpoint) {
			g_debug ("Could not create endpoint");
			g_dbus_method_invocation_return_gerror (invocation, error);
			return;
		}

		g_signal_connect (endpoint, "closed",
		                  G_CALLBACK (endpoint_closed_cb),
		                  portal);

		session = (TrackerSession) {
			.dbus_connection = g_steal_pointer (&dbus_connection),
			.connection = g_steal_pointer (&connection),
			.endpoint = g_steal_pointer (&endpoint),
			.object_path = g_steal_pointer (&session_object_path),
		};
		g_array_append_val (portal->sessions, session);

		g_debug ("Created session object '%s'", session.object_path);
		g_dbus_method_invocation_return_value (invocation,
		                                       g_variant_new ("(o)", session.object_path));
	} else if (g_strcmp0 (method_name, "CloseSession") == 0) {
		g_autofree gchar *uri = NULL;
		g_autofree gchar *object_path = NULL;
		gboolean found = FALSE;
		gint i;

		g_variant_get (parameters, "(o)", &object_path);
		g_debug ("Got request for closing session '%s'", object_path);

		for (i = 0; i < portal->sessions->len; i++) {
			TrackerSession *session;
			gchar *peer;

			session = &g_array_index (portal->sessions, TrackerSession, i);

			if (g_strcmp0 (session->object_path, object_path) != 0)
				continue;

			found = TRUE;
			g_object_get (session->endpoint, "peer", &peer, NULL);

			if (g_strcmp0 (peer, sender) == 0) {
				g_array_remove_index_fast (portal->sessions, i);
				g_dbus_method_invocation_return_value (invocation, NULL);
			} else {
				g_dbus_method_invocation_return_error (invocation,
				                                       G_DBUS_ERROR,
				                                       G_DBUS_ERROR_AUTH_FAILED,
				                                       "Sender '%s' is not owner of the session",
				                                       sender);
			}

			break;
		}

		if (!found) {
			g_dbus_method_invocation_return_error (invocation,
			                                       G_DBUS_ERROR,
			                                       G_DBUS_ERROR_INVALID_ARGS,
			                                       "Object path '%s' not found",
			                                       object_path);
		}
	} else {
		g_dbus_method_invocation_return_error (invocation,
		                                       G_DBUS_ERROR,
		                                       G_DBUS_ERROR_UNKNOWN_METHOD,
		                                       "Unknown method '%s'", method_name);
	}
}

static gboolean
tracker_portal_initable_init (GInitable     *initable,
                              GCancellable  *cancellable,
                              GError       **error)
{
	TrackerPortal *portal = TRACKER_PORTAL (initable);

	portal->node_info = g_dbus_node_info_new_for_xml (portal_xml, error);
	if (!portal->node_info)
		return FALSE;

	portal->register_id =
		g_dbus_connection_register_object (portal->dbus_connection,
		                                   "/org/freedesktop/portal/Tracker",
		                                   portal->node_info->interfaces[0],
		                                   &(GDBusInterfaceVTable) {
			                                   portal_iface_method_call,
				                           NULL,
				                           NULL
				                   },
		                                   portal,
		                                   NULL,
		                                   error);
	return TRUE;
}

static void
tracker_portal_initable_iface_init (GInitableIface *iface)
{
	iface->init = tracker_portal_initable_init;
}

TrackerPortal *
tracker_portal_new (GDBusConnection  *connection,
                    GCancellable     *cancellable,
                    GError          **error)
{
	return g_initable_new (TRACKER_TYPE_PORTAL, cancellable, error,
	                       "dbus-connection", connection,
	                       NULL);
}
