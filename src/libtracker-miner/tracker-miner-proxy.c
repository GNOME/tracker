/*
 * Copyright (C) 2017, Red Hat, Inc.
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
 *
 * Authors: Carlos Garnacho <carlosg@gnome.org>
 */

/**
 * SECTION:tracker-miner-proxy
 * @short_description: Proxies a #TrackerMiner on DBus
 * @include: libtracker-miner/tracker-miner.h
 *
 * #TrackerMinerProxy is a helper object to expose org.freedesktop.Tracker1.Miner
 * DBus interfaces for the given #TrackerMiner object. This is used to implement
 * miners as DBus services.
 *
 * This proxy allows the miner to be controlled through external means, such as
 * #TrackerMinerManager in libtracker-control.
 *
 * #TrackerMinerProxy implements the #GInitable interface, and thus all objects of
 * types inheriting from #TrackerMinerProxy must be initialized with g_initable_init()
 * just after creation (or directly created with g_initable_new()).
 **/

#include "config.h"

#include <glib/gi18n.h>
#include <libtracker-common/tracker-dbus.h>
#include <libtracker-common/tracker-type-utils.h>
#include <libtracker-common/tracker-domain-ontology.h>

#include "tracker-miner-proxy.h"

typedef struct {
	TrackerMiner *miner;
	GDBusConnection *d_connection;
	GDBusNodeInfo *introspection_data;
	gchar *dbus_path;
	guint registration_id;
	GHashTable *pauses;
} TrackerMinerProxyPrivate;

typedef struct {
	gint cookie;
	gchar *application;
	gchar *reason;
	gchar *watch_name;
	guint watch_name_id;
} PauseData;

enum {
	PROP_0,
	PROP_MINER,
	PROP_DBUS_CONNECTION,
	PROP_DBUS_PATH,
};

static void tracker_miner_proxy_initable_iface_init (GInitableIface *iface);

G_DEFINE_TYPE_WITH_CODE (TrackerMinerProxy, tracker_miner_proxy, G_TYPE_OBJECT,
                         G_ADD_PRIVATE (TrackerMinerProxy)
                         G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE, tracker_miner_proxy_initable_iface_init))

static const gchar introspection_xml[] =
  "<node>"
  "  <interface name='org.freedesktop.Tracker1.Miner'>"
  "    <method name='Start'>"
  "    </method>"
  "    <method name='GetStatus'>"
  "      <arg type='s' name='status' direction='out' />"
  "    </method>"
  "    <method name='GetProgress'>"
  "      <arg type='d' name='progress' direction='out' />"
  "    </method>"
  "    <method name='GetRemainingTime'>"
  "      <arg type='i' name='remaining_time' direction='out' />"
  "    </method>"
  "    <method name='GetPauseDetails'>"
  "      <arg type='as' name='pause_applications' direction='out' />"
  "      <arg type='as' name='pause_reasons' direction='out' />"
  "    </method>"
  "    <method name='Pause'>"
  "      <arg type='s' name='application' direction='in' />"
  "      <arg type='s' name='reason' direction='in' />"
  "      <arg type='i' name='cookie' direction='out' />"
  "    </method>"
  "    <method name='PauseForProcess'>"
  "      <arg type='s' name='application' direction='in' />"
  "      <arg type='s' name='reason' direction='in' />"
  "      <arg type='i' name='cookie' direction='out' />"
  "    </method>"
  "    <method name='Resume'>"
  "      <arg type='i' name='cookie' direction='in' />"
  "    </method>"
  "    <signal name='Started' />"
  "    <signal name='Stopped' />"
  "    <signal name='Paused' />"
  "    <signal name='Resumed' />"
  "    <signal name='Progress'>"
  "      <arg type='s' name='status' />"
  "      <arg type='d' name='progress' />"
  "      <arg type='i' name='remaining_time' />"
  "    </signal>"
  "  </interface>"
  "</node>";

#define TRACKER_SERVICE "org.freedesktop.Tracker1"

static PauseData *
pause_data_new (const gchar *application,
                const gchar *reason,
                const gchar *watch_name,
                guint        watch_name_id)
{
	PauseData *data;
	static gint cookie = 1;

	data = g_slice_new0 (PauseData);

	data->cookie = cookie++;
	data->application = g_strdup (application);
	data->reason = g_strdup (reason);
	data->watch_name = g_strdup (watch_name);
	data->watch_name_id = watch_name_id;

	return data;
}

static void
pause_data_destroy (gpointer data)
{
	PauseData *pd;

	pd = data;

	if (pd->watch_name_id) {
		g_bus_unwatch_name (pd->watch_name_id);
	}

	g_free (pd->watch_name);

	g_free (pd->reason);
	g_free (pd->application);

	g_slice_free (PauseData, pd);
}

static void
tracker_miner_proxy_set_property (GObject      *object,
                                  guint         prop_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
	TrackerMinerProxy *proxy = TRACKER_MINER_PROXY (object);
	TrackerMinerProxyPrivate *priv = tracker_miner_proxy_get_instance_private (proxy);

	switch (prop_id) {
	case PROP_MINER:
		priv->miner = g_value_dup_object (value);
		break;
	case PROP_DBUS_CONNECTION:
		priv->d_connection = g_value_dup_object (value);
		break;
	case PROP_DBUS_PATH:
		priv->dbus_path = g_value_dup_string (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
tracker_miner_proxy_get_property (GObject    *object,
                                  guint       prop_id,
                                  GValue     *value,
                                  GParamSpec *pspec)
{
	TrackerMinerProxy *proxy = TRACKER_MINER_PROXY (object);
	TrackerMinerProxyPrivate *priv = tracker_miner_proxy_get_instance_private (proxy);

	switch (prop_id) {
	case PROP_MINER:
		g_value_set_object (value, priv->miner);
		break;
	case PROP_DBUS_CONNECTION:
		g_value_set_object (value, priv->d_connection);
		break;
	case PROP_DBUS_PATH:
		g_value_set_string (value, priv->dbus_path);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
tracker_miner_proxy_finalize (GObject *object)
{
	TrackerMinerProxy *proxy = TRACKER_MINER_PROXY (object);
	TrackerMinerProxyPrivate *priv = tracker_miner_proxy_get_instance_private (proxy);

	g_signal_handlers_disconnect_by_data (priv->miner, proxy);
	g_clear_object (&priv->miner);
	g_free (priv->dbus_path);
	g_hash_table_unref (priv->pauses);

	if (priv->registration_id != 0) {
		g_dbus_connection_unregister_object (priv->d_connection,
		                                     priv->registration_id);
	}

	if (priv->introspection_data) {
		g_dbus_node_info_unref (priv->introspection_data);
	}

	if (priv->d_connection) {
		g_object_unref (priv->d_connection);
	}

	G_OBJECT_CLASS (tracker_miner_proxy_parent_class)->finalize (object);
}

static void
tracker_miner_proxy_class_init (TrackerMinerProxyClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->set_property = tracker_miner_proxy_set_property;
	object_class->get_property = tracker_miner_proxy_get_property;
	object_class->finalize = tracker_miner_proxy_finalize;

	g_object_class_install_property (object_class,
	                                 PROP_MINER,
	                                 g_param_spec_object ("miner",
	                                                      "Miner to manage",
	                                                      "Miner to manage",
	                                                      TRACKER_TYPE_MINER,
	                                                      G_PARAM_READWRITE |
	                                                      G_PARAM_CONSTRUCT_ONLY));
	g_object_class_install_property (object_class,
	                                 PROP_DBUS_CONNECTION,
	                                 g_param_spec_object ("dbus-connection",
	                                                      "DBus connection",
	                                                      "DBus connection",
	                                                      G_TYPE_DBUS_CONNECTION,
	                                                      G_PARAM_READWRITE |
	                                                      G_PARAM_CONSTRUCT_ONLY));
	g_object_class_install_property (object_class,
	                                 PROP_DBUS_PATH,
	                                 g_param_spec_string ("dbus-path",
	                                                      "DBus path",
	                                                      "DBus path for this miner",
	                                                      NULL,
	                                                      G_PARAM_READWRITE |
	                                                      G_PARAM_CONSTRUCT_ONLY));
}

static void
tracker_miner_proxy_init (TrackerMinerProxy *proxy)
{
	TrackerMinerProxyPrivate *priv = tracker_miner_proxy_get_instance_private (proxy);

	priv->pauses = g_hash_table_new_full (g_direct_hash,
	                                      g_direct_equal,
	                                      NULL,
	                                      pause_data_destroy);
}

static void
handle_method_call_start (TrackerMinerProxy     *proxy,
                          GDBusMethodInvocation *invocation,
                          GVariant              *parameters)
{
	TrackerDBusRequest *request;
	TrackerMinerProxyPrivate *priv;

	priv = tracker_miner_proxy_get_instance_private (proxy);

	request = tracker_g_dbus_request_begin (invocation,
	                                        "%s",
	                                        __PRETTY_FUNCTION__);

	tracker_miner_start (priv->miner);

	tracker_dbus_request_end (request, NULL);
	g_dbus_method_invocation_return_value (invocation, NULL);
}

static void
sync_miner_pause_state (TrackerMinerProxy *proxy)
{
	TrackerMinerProxyPrivate *priv;
	guint n_pauses;
	gboolean is_paused;

	priv = tracker_miner_proxy_get_instance_private (proxy);
	n_pauses = g_hash_table_size (priv->pauses);
	is_paused = tracker_miner_is_paused (priv->miner);

	if (!is_paused && n_pauses > 0) {
		tracker_miner_pause (priv->miner);
	} else if (is_paused && n_pauses == 0) {
		tracker_miner_resume (priv->miner);
	}
}

static void
handle_method_call_resume (TrackerMinerProxy     *proxy,
                           GDBusMethodInvocation *invocation,
                           GVariant              *parameters)
{
	gint cookie;
	TrackerDBusRequest *request;
	TrackerMinerProxyPrivate *priv;

	priv = tracker_miner_proxy_get_instance_private (proxy);

	g_variant_get (parameters, "(i)", &cookie);

	request = tracker_g_dbus_request_begin (invocation,
	                                        "%s(cookie:%d)",
	                                        __PRETTY_FUNCTION__,
	                                        cookie);

	if (!g_hash_table_remove (priv->pauses, GINT_TO_POINTER (cookie))) {
		tracker_dbus_request_end (request, NULL);
		g_dbus_method_invocation_return_error (invocation,
		                                       tracker_miner_error_quark (),
		                                       TRACKER_MINER_ERROR_INVALID_COOKIE,
		                                       _("Cookie not recognized to resume paused miner"));
	} else {
		sync_miner_pause_state (proxy);
		tracker_dbus_request_end (request, NULL);
		g_dbus_method_invocation_return_value (invocation, NULL);
	}
}

static void
pause_process_disappeared_cb (GDBusConnection *connection,
                              const gchar     *name,
                              gpointer         user_data)
{
	TrackerMinerProxy *proxy = user_data;
	TrackerMinerProxyPrivate *priv = tracker_miner_proxy_get_instance_private (proxy);
	GHashTableIter iter;
	gpointer key, value;

	g_message ("Process with name:'%s' has disappeared", name);

	g_hash_table_iter_init (&iter, priv->pauses);
	while (g_hash_table_iter_next (&iter, &key, &value)) {
		PauseData *pd_iter = value;

		if (g_strcmp0 (name, pd_iter->watch_name) == 0)
			g_hash_table_iter_remove (&iter);
	}

	sync_miner_pause_state (proxy);
}

static gint
pause_miner (TrackerMinerProxy  *proxy,
             const gchar        *application,
             const gchar        *reason,
             const gchar        *calling_name,
             GError            **error)
{
	TrackerMinerProxyPrivate *priv;
	PauseData *pd;
	GHashTableIter iter;
	gpointer key, value;
	guint watch_name_id = 0;

	priv = tracker_miner_proxy_get_instance_private (proxy);

	/* Check this is not a duplicate pause */
	g_hash_table_iter_init (&iter, priv->pauses);
	while (g_hash_table_iter_next (&iter, &key, &value)) {
		PauseData *pd = value;

		if (g_strcmp0 (application, pd->application) == 0 &&
		    g_strcmp0 (reason, pd->reason) == 0) {
			/* Can't use duplicate pauses */
			g_set_error_literal (error,
			                     tracker_miner_error_quark (),
			                     TRACKER_MINER_ERROR_PAUSED_ALREADY,
			                     _("Pause application and reason match an already existing pause request"));
			return -1;
		}
	}

	if (calling_name) {
		g_message ("Watching process with name:'%s'", calling_name);
		watch_name_id = g_bus_watch_name (TRACKER_IPC_BUS,
		                                  calling_name,
		                                  G_BUS_NAME_WATCHER_FLAGS_NONE,
		                                  NULL,
		                                  pause_process_disappeared_cb,
		                                  proxy,
		                                  NULL);
	}

	pd = pause_data_new (application, reason, calling_name, watch_name_id);

	g_hash_table_insert (priv->pauses,
	                     GINT_TO_POINTER (pd->cookie),
	                     pd);

	sync_miner_pause_state (proxy);

	return pd->cookie;
}

static void
handle_method_call_pause (TrackerMinerProxy     *proxy,
                          GDBusMethodInvocation *invocation,
                          GVariant              *parameters)
{
	GError *local_error = NULL;
	gint cookie;
	const gchar *application = NULL, *reason = NULL;
	TrackerDBusRequest *request;

	g_variant_get (parameters, "(&s&s)", &application, &reason);

	tracker_gdbus_async_return_if_fail (application != NULL, invocation);
	tracker_gdbus_async_return_if_fail (reason != NULL, invocation);

	request = tracker_g_dbus_request_begin (invocation,
	                                        "%s(application:'%s', reason:'%s')",
	                                        __PRETTY_FUNCTION__,
	                                        application,
	                                        reason);

	cookie = pause_miner (proxy, application, reason, NULL, &local_error);
	if (cookie == -1) {
		tracker_dbus_request_end (request, local_error);

		g_dbus_method_invocation_return_gerror (invocation, local_error);

		g_error_free (local_error);

		return;
	}

	tracker_dbus_request_end (request, NULL);
	g_dbus_method_invocation_return_value (invocation,
	                                       g_variant_new ("(i)", cookie));
}

static void
handle_method_call_pause_for_process (TrackerMinerProxy     *proxy,
                                      GDBusMethodInvocation *invocation,
                                      GVariant              *parameters)
{
	GError *local_error = NULL;
	gint cookie;
	const gchar *application = NULL, *reason = NULL;
	TrackerDBusRequest *request;

	g_variant_get (parameters, "(&s&s)", &application, &reason);

	tracker_gdbus_async_return_if_fail (application != NULL, invocation);
	tracker_gdbus_async_return_if_fail (reason != NULL, invocation);

	request = tracker_g_dbus_request_begin (invocation,
	                                        "%s(application:'%s', reason:'%s')",
	                                        __PRETTY_FUNCTION__,
	                                        application,
	                                        reason);

	cookie = pause_miner (proxy,
	                      application,
	                      reason,
	                      g_dbus_method_invocation_get_sender (invocation),
	                      &local_error);
	if (cookie == -1) {
		tracker_dbus_request_end (request, local_error);

		g_dbus_method_invocation_return_gerror (invocation, local_error);

		g_error_free (local_error);

		return;
	}

	tracker_dbus_request_end (request, NULL);
	g_dbus_method_invocation_return_value (invocation,
	                                       g_variant_new ("(i)", cookie));
}

static void
handle_method_call_get_pause_details (TrackerMinerProxy     *proxy,
                                      GDBusMethodInvocation *invocation,
                                      GVariant              *parameters)
{
	GSList *applications, *reasons;
	GStrv applications_strv, reasons_strv;
	GHashTableIter iter;
	gpointer key, value;
	TrackerDBusRequest *request;
	TrackerMinerProxyPrivate *priv;

	priv = tracker_miner_proxy_get_instance_private (proxy);
	request = tracker_g_dbus_request_begin (invocation, "%s()", __PRETTY_FUNCTION__);

	applications = NULL;
	reasons = NULL;
	g_hash_table_iter_init (&iter, priv->pauses);
	while (g_hash_table_iter_next (&iter, &key, &value)) {
		PauseData *pd = value;

		applications = g_slist_prepend (applications, pd->application);
		reasons = g_slist_prepend (reasons, pd->reason);
	}
	applications = g_slist_reverse (applications);
	reasons = g_slist_reverse (reasons);
	applications_strv = tracker_gslist_to_string_list (applications);
	reasons_strv = tracker_gslist_to_string_list (reasons);

	tracker_dbus_request_end (request, NULL);
	g_dbus_method_invocation_return_value (invocation,
	                                       g_variant_new ("(^as^as)",
	                                                      applications_strv,
	                                                      reasons_strv));

	g_strfreev (applications_strv);
	g_strfreev (reasons_strv);
	g_slist_free (applications);
	g_slist_free (reasons);
}

static void
handle_method_call_get_remaining_time (TrackerMinerProxy     *proxy,
                                       GDBusMethodInvocation *invocation,
                                       GVariant              *parameters)
{
	TrackerDBusRequest *request;
	TrackerMinerProxyPrivate *priv;
	gint remaining_time;

	priv = tracker_miner_proxy_get_instance_private (proxy);

	request = tracker_g_dbus_request_begin (invocation, "%s()", __PRETTY_FUNCTION__);

	tracker_dbus_request_end (request, NULL);
	g_object_get (G_OBJECT (priv->miner), "remaining-time", &remaining_time, NULL);
	g_dbus_method_invocation_return_value (invocation,
	                                       g_variant_new ("(i)", remaining_time));
}

static void
handle_method_call_get_progress (TrackerMinerProxy     *proxy,
                                 GDBusMethodInvocation *invocation,
                                 GVariant              *parameters)
{
	TrackerDBusRequest *request;
	TrackerMinerProxyPrivate *priv;
	gdouble progress;

	priv = tracker_miner_proxy_get_instance_private (proxy);

	request = tracker_g_dbus_request_begin (invocation, "%s()", __PRETTY_FUNCTION__);

	tracker_dbus_request_end (request, NULL);
	g_object_get (G_OBJECT (priv->miner), "progress", &progress, NULL);
	g_dbus_method_invocation_return_value (invocation,
	                                       g_variant_new ("(d)", progress));
}

static void
handle_method_call_get_status (TrackerMinerProxy     *proxy,
                               GDBusMethodInvocation *invocation,
                               GVariant              *parameters)
{
	TrackerDBusRequest *request;
	TrackerMinerProxyPrivate *priv;
	gchar *status;

	priv = tracker_miner_proxy_get_instance_private (proxy);

	request = tracker_g_dbus_request_begin (invocation, "%s()", __PRETTY_FUNCTION__);

	tracker_dbus_request_end (request, NULL);
	g_object_get (G_OBJECT (priv->miner), "status", &status, NULL);
	g_dbus_method_invocation_return_value (invocation,
	                                       g_variant_new ("(s)",
	                                                      status ? status : ""));
	g_free (status);
}

static void
handle_method_call (GDBusConnection       *connection,
                    const gchar           *sender,
                    const gchar           *object_path,
                    const gchar           *interface_name,
                    const gchar           *method_name,
                    GVariant              *parameters,
                    GDBusMethodInvocation *invocation,
                    gpointer               user_data)
{
	TrackerMinerProxy *proxy = user_data;

	if (g_strcmp0 (method_name, "Start") == 0) {
		handle_method_call_start (proxy, invocation, parameters);
	} else if (g_strcmp0 (method_name, "Resume") == 0) {
		handle_method_call_resume (proxy, invocation, parameters);
	} else if (g_strcmp0 (method_name, "Pause") == 0) {
		handle_method_call_pause (proxy, invocation, parameters);
	} else if (g_strcmp0 (method_name, "PauseForProcess") == 0) {
		handle_method_call_pause_for_process (proxy, invocation, parameters);
	} else if (g_strcmp0 (method_name, "GetPauseDetails") == 0) {
		handle_method_call_get_pause_details (proxy, invocation, parameters);
	} else if (g_strcmp0 (method_name, "GetRemainingTime") == 0) {
		handle_method_call_get_remaining_time (proxy, invocation, parameters);
	} else if (g_strcmp0 (method_name, "GetProgress") == 0) {
		handle_method_call_get_progress (proxy, invocation, parameters);
	} else if (g_strcmp0 (method_name, "GetStatus") == 0) {
		handle_method_call_get_status (proxy, invocation, parameters);
	} else {
		g_dbus_method_invocation_return_error (invocation,
		                                       G_DBUS_ERROR,
		                                       G_DBUS_ERROR_UNKNOWN_METHOD,
		                                       "Unknown method %s",
		                                       method_name);
	}
}

static GVariant *
handle_get_property (GDBusConnection  *connection,
                     const gchar      *sender,
                     const gchar      *object_path,
                     const gchar      *interface_name,
                     const gchar      *property_name,
                     GError          **error,
                     gpointer          user_data)
{
	return NULL;
}

static gboolean
handle_set_property (GDBusConnection  *connection,
                     const gchar      *sender,
                     const gchar      *object_path,
                     const gchar      *interface_name,
                     const gchar      *property_name,
                     GVariant         *value,
                     GError          **error,
                     gpointer          user_data)
{
	return FALSE;
}

static void
emit_dbus_signal (TrackerMinerProxy *proxy,
                  const gchar       *signal,
                  GVariant          *variant)
{
	TrackerMinerProxyPrivate *priv;

	priv = tracker_miner_proxy_get_instance_private (proxy);
	g_dbus_connection_emit_signal (priv->d_connection,
	                               NULL,
	                               priv->dbus_path,
	                               TRACKER_MINER_DBUS_INTERFACE,
	                               signal,
	                               variant,
	                               NULL);
}

static void
miner_started_cb (TrackerMiner      *miner,
                  TrackerMinerProxy *proxy)
{
	emit_dbus_signal (proxy, "Started", NULL);
}

static void
miner_stopped_cb (TrackerMiner      *miner,
                  TrackerMinerProxy *proxy)
{
	emit_dbus_signal (proxy, "Stopped", NULL);
}

static void
miner_paused_cb (TrackerMiner      *miner,
                 TrackerMinerProxy *proxy)
{
	emit_dbus_signal (proxy, "Paused", NULL);
}

static void
miner_resumed_cb (TrackerMiner      *miner,
                  TrackerMinerProxy *proxy)
{
	emit_dbus_signal (proxy, "Resumed", NULL);
}

static void
miner_progress_cb (TrackerMiner      *miner,
                   const gchar       *status,
                   gdouble            progress,
                   gint               remaining_time,
                   TrackerMinerProxy *proxy)
{
	GVariant *variant;

	variant = g_variant_new ("(sdi)", status, progress, remaining_time);
	/* variant reference is sunk here */
	emit_dbus_signal (proxy, "Progress", variant);
}

static gboolean
tracker_miner_proxy_initable_init (GInitable     *initable,
                                   GCancellable  *cancellable,
                                   GError       **error)
{
	TrackerMinerProxy *proxy = TRACKER_MINER_PROXY (initable);
	TrackerMinerProxyPrivate *priv = tracker_miner_proxy_get_instance_private (proxy);
	GError *inner_error = NULL;
	TrackerDomainOntology *domain_ontology;
	GDBusInterfaceVTable interface_vtable = {
		handle_method_call,
		handle_get_property,
		handle_set_property
	};

	priv->introspection_data = g_dbus_node_info_new_for_xml (introspection_xml,
	                                                         &inner_error);
	if (inner_error) {
		g_propagate_error (error, inner_error);
		return FALSE;
	}

	priv->registration_id =
		g_dbus_connection_register_object (priv->d_connection,
		                                   priv->dbus_path,
		                                   priv->introspection_data->interfaces[0],
		                                   &interface_vtable,
		                                   proxy,
		                                   NULL,
		                                   &inner_error);
	if (inner_error) {
		g_propagate_error (error, inner_error);
		return FALSE;
	}

	domain_ontology = tracker_domain_ontology_new (tracker_sparql_connection_get_domain (),
	                                               cancellable, &inner_error);
	if (inner_error) {
		g_propagate_error (error, inner_error);
		return FALSE;
	}

	g_signal_connect (priv->miner, "started",
	                  G_CALLBACK (miner_started_cb), proxy);
	g_signal_connect (priv->miner, "stopped",
	                  G_CALLBACK (miner_stopped_cb), proxy);
	g_signal_connect (priv->miner, "paused",
	                  G_CALLBACK (miner_paused_cb), proxy);
	g_signal_connect (priv->miner, "resumed",
	                  G_CALLBACK (miner_resumed_cb), proxy);
	g_signal_connect (priv->miner, "progress",
	                  G_CALLBACK (miner_progress_cb), proxy);

	tracker_domain_ontology_unref (domain_ontology);

	return TRUE;
}

static void
tracker_miner_proxy_initable_iface_init (GInitableIface *iface)
{
	iface->init = tracker_miner_proxy_initable_init;
}

TrackerMinerProxy *
tracker_miner_proxy_new (TrackerMiner     *miner,
                         GDBusConnection  *connection,
                         const gchar      *dbus_path,
                         GCancellable     *cancellable,
                         GError          **error)
{
	return g_initable_new (TRACKER_TYPE_MINER_PROXY,
	                       cancellable, error,
	                       "miner", miner,
	                       "dbus-connection", connection,
	                       "dbus-path", dbus_path,
	                       NULL);
}
