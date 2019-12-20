/*
 * Copyright (C) 2019, Red Hat, Inc
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
 * Author: Carlos Garnacho <carlosg@gnome.org>
 */

#include "config.h"

#include "tracker-endpoint-dbus.h"

#include <gio/gio.h>
#include <gio/gunixinputstream.h>
#include <gio/gunixoutputstream.h>
#include <gio/gunixfdlist.h>

static const gchar introspection_xml[] =
	"<node>"
	"  <interface name='org.freedesktop.Tracker1.Endpoint'>"
	"    <method name='Query'>"
	"      <arg type='s' name='query' direction='in' />"
	"      <arg type='h' name='output_stream' direction='in' />"
	"      <arg type='as' name='result' direction='out' />"
	"    </method>"
	"    <method name='Update'>"
	"      <arg type='h' name='input_stream' direction='in' />"
	"    </method>"
	"    <method name='UpdateArray'>"
	"      <arg type='h' name='input_stream' direction='in' />"
	"    </method>"
	"    <method name='UpdateBlank'>"
	"      <arg type='h' name='input_stream' direction='in' />"
	"      <arg type='aaa{ss}' name='result' direction='out' />"
	"    </method>"
	"    <signal name='GraphUpdate'>"
	"      <arg type='sa(ii)' name='updates' />"
	"    </signal>"
	"  </interface>"
	"</node>";

enum {
	PROP_0,
	PROP_DBUS_CONNECTION,
	PROP_OBJECT_PATH,
	N_PROPS
};

struct _TrackerEndpointDBus {
	TrackerEndpoint parent_instance;
	GDBusConnection *dbus_connection;
	gchar *object_path;
	guint register_id;
	GDBusNodeInfo *node_info;
	GCancellable *cancellable;
};

typedef struct {
	TrackerEndpointDBus *endpoint;
	GDBusMethodInvocation *invocation;
	GDataOutputStream *data_stream;
} QueryRequest;

typedef struct {
	TrackerEndpointDBus *endpoint;
	GDBusMethodInvocation *invocation;
	GDataInputStream *input_stream;
	GPtrArray *queries;
	gboolean array_update;
	gint num_queries;
	gint cur_query;
} UpdateRequest;

GParamSpec *props[N_PROPS] = { 0 };

static void tracker_endpoint_dbus_initable_iface_init (GInitableIface *iface);

static void read_update_cb       (GObject      *object,
                                  GAsyncResult *res,
                                  gpointer      user_data);
static void read_update_blank_cb (GObject      *object,
                                  GAsyncResult *res,
                                  gpointer      user_data);

G_DEFINE_TYPE_WITH_CODE (TrackerEndpointDBus, tracker_endpoint_dbus, TRACKER_TYPE_ENDPOINT,
                         G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE, tracker_endpoint_dbus_initable_iface_init))

static QueryRequest *
query_request_new (TrackerEndpointDBus   *endpoint,
                   GDBusMethodInvocation *invocation,
                   int                    fd)
{
	GOutputStream *stream, *buffered_stream;
	QueryRequest *request;

	request = g_new0 (QueryRequest, 1);
	request->invocation = g_object_ref (invocation);
	request->endpoint = endpoint;

	stream = g_unix_output_stream_new (fd, TRUE);
	buffered_stream = g_buffered_output_stream_new_sized (stream,
	                                                      getpagesize ());

	request->data_stream = g_data_output_stream_new (buffered_stream);
	g_data_output_stream_set_byte_order (request->data_stream,
	                                     G_DATA_STREAM_BYTE_ORDER_HOST_ENDIAN);

	g_object_unref (buffered_stream);
	g_object_unref (stream);

	return request;
}

static void
query_request_free (QueryRequest *request)
{
	g_output_stream_close (G_OUTPUT_STREAM (request->data_stream),
	                       NULL, NULL);

	g_object_unref (request->invocation);
	g_object_unref (request->data_stream);
	g_free (request);
}

static UpdateRequest *
update_request_new (TrackerEndpointDBus   *endpoint,
                    GDBusMethodInvocation *invocation,
                    gboolean               array_update,
                    int                    input)
{
	UpdateRequest *request;
	GInputStream *stream;

	request = g_new0 (UpdateRequest, 1);
	request->invocation = g_object_ref (invocation);
	request->endpoint = endpoint;
	request->cur_query = 0;
	request->array_update = array_update;
	request->queries = g_ptr_array_new_with_free_func (g_free);

	stream = g_unix_input_stream_new (input, TRUE);
	request->input_stream = g_data_input_stream_new (stream);
	g_buffered_input_stream_set_buffer_size (G_BUFFERED_INPUT_STREAM (request->input_stream),
	                                         getpagesize ());
	g_data_input_stream_set_byte_order (request->input_stream,
	                                    G_DATA_STREAM_BYTE_ORDER_HOST_ENDIAN);
	g_object_unref (stream);

	if (array_update)
		request->num_queries = g_data_input_stream_read_int32 (request->input_stream, NULL, NULL);
	else
		request->num_queries = 1;

	return request;
}

static gboolean
update_request_read_next (UpdateRequest       *request,
                          GAsyncReadyCallback  cb)
{
	gchar *buffer;
	gint buffer_size;

	if (request->cur_query >= request->num_queries)
		return FALSE;

	request->cur_query++;
	buffer_size = g_data_input_stream_read_int32 (request->input_stream, NULL, NULL);
	buffer = g_new0 (char, buffer_size + 1);
	g_ptr_array_add (request->queries, buffer);

	g_input_stream_read_all_async (G_INPUT_STREAM (request->input_stream),
	                               buffer,
	                               buffer_size,
	                               G_PRIORITY_DEFAULT,
	                               request->endpoint->cancellable,
	                               cb, request);
	return TRUE;
}

static void
update_request_free (UpdateRequest *request)
{
	g_input_stream_close (G_INPUT_STREAM (request->input_stream),
	                      NULL, NULL);

	g_ptr_array_unref (request->queries);
	g_object_unref (request->invocation);
	g_object_unref (request->input_stream);
	g_free (request);
}

static void
query_cb (GObject      *object,
          GAsyncResult *res,
          gpointer      user_data)
{
	QueryRequest *request = user_data;
	TrackerSparqlCursor *cursor;
	GError *error = NULL;
	const gchar **values = NULL;
	const gchar **variable_names = NULL;
	glong *offsets = NULL;
	gint i, n_columns = 0;

	cursor = tracker_sparql_connection_query_finish (TRACKER_SPARQL_CONNECTION (object),
	                                                 res, &error);
	if (!cursor)
		goto error;

	n_columns = tracker_sparql_cursor_get_n_columns (cursor);
	variable_names = g_new0 (const gchar *, n_columns + 1);
	values = g_new0 (const char *, n_columns);
	offsets = g_new0 (glong, n_columns);

	for (i = 0; i < n_columns; i++)
		variable_names[i] = tracker_sparql_cursor_get_variable_name (cursor, i);

	while (tracker_sparql_cursor_next (cursor, NULL, &error)) {
		glong cur_offset = -1;

		g_data_output_stream_put_int32 (request->data_stream, n_columns, NULL, NULL);

		for (i = 0; i < n_columns; i++) {
			glong len;

			g_data_output_stream_put_int32 (request->data_stream,
			                                tracker_sparql_cursor_get_value_type (cursor, i),
			                                NULL, NULL);
			values[i] = tracker_sparql_cursor_get_string (cursor, i, &len);
			len++;
			cur_offset += len;
			offsets[i] = cur_offset;
		}

		for (i = 0; i < n_columns; i++) {
			g_data_output_stream_put_int32 (request->data_stream,
			                                offsets[i], NULL, NULL);
		}

		for (i = 0; i < n_columns; i++) {
			g_data_output_stream_put_string (request->data_stream,
			                                 values[i] ? values[i] : "",
			                                 NULL, NULL);
			g_data_output_stream_put_byte (request->data_stream, 0, NULL, NULL);
		}
	}

error:
	if (error)
		g_dbus_method_invocation_return_gerror (request->invocation, error);
	else
		g_dbus_method_invocation_return_value (request->invocation, g_variant_new ("(^as)", variable_names));

	g_free (variable_names);
	g_free (values);
	g_free (offsets);
	g_clear_object (&cursor);

	query_request_free (request);
}

static void
update_cb (GObject      *object,
           GAsyncResult *res,
           gpointer      user_data)
{
	UpdateRequest *request = user_data;
	GError *error = NULL;

	tracker_sparql_connection_update_array_finish (TRACKER_SPARQL_CONNECTION (object),
	                                               res, &error);
	if (error) {
		g_dbus_method_invocation_return_gerror (request->invocation, error);
	} else {
		g_dbus_method_invocation_return_value (request->invocation, NULL);
	}

	update_request_free (request);
}

static void
read_update_cb (GObject      *object,
                GAsyncResult *res,
                gpointer      user_data)
{
	TrackerSparqlConnection *conn;
	UpdateRequest *request = user_data;
	GError *error = NULL;

	if (!g_input_stream_read_all_finish (G_INPUT_STREAM (object),
	                                     res, NULL, &error)) {
		g_dbus_method_invocation_return_gerror (request->invocation, error);
		update_request_free (request);
		return;
	}

	if (!update_request_read_next (request, read_update_cb)) {
		conn = tracker_endpoint_get_sparql_connection (TRACKER_ENDPOINT (request->endpoint));
		tracker_sparql_connection_update_array_async (conn,
		                                              (gchar **) request->queries->pdata,
		                                              request->queries->len,
		                                              G_PRIORITY_DEFAULT,
		                                              request->endpoint->cancellable,
		                                              update_cb,
		                                              request);
	}
}

static void
update_blank_cb (GObject      *object,
                 GAsyncResult *res,
                 gpointer      user_data)
{
	UpdateRequest *request = user_data;
	GError *error = NULL;
	GVariant *results;

	results = tracker_sparql_connection_update_blank_finish (TRACKER_SPARQL_CONNECTION (object),
	                                                         res, &error);
	if (results)
		g_dbus_method_invocation_return_value (request->invocation, results);
	else
		g_dbus_method_invocation_return_gerror (request->invocation, error);

	update_request_free (request);
}

static void
read_update_blank_cb (GObject      *object,
                      GAsyncResult *res,
                      gpointer      user_data)
{
	TrackerSparqlConnection *conn;
	UpdateRequest *request = user_data;
	GError *error = NULL;

	if (!g_input_stream_read_all_finish (G_INPUT_STREAM (object),
	                                     res, NULL, &error)) {
		g_dbus_method_invocation_return_gerror (request->invocation, error);
		update_request_free (request);
		return;
	}

	conn = tracker_endpoint_get_sparql_connection (TRACKER_ENDPOINT (request->endpoint));
	tracker_sparql_connection_update_blank_async (conn,
	                                              g_ptr_array_index (request->queries, 0),
	                                              G_PRIORITY_DEFAULT,
	                                              request->endpoint->cancellable,
	                                              update_blank_cb,
	                                              request);
}

static void
endpoint_dbus_iface_method_call (GDBusConnection       *connection,
                                 const gchar           *sender,
                                 const gchar           *object_path,
                                 const gchar           *interface_name,
                                 const gchar           *method_name,
                                 GVariant              *parameters,
                                 GDBusMethodInvocation *invocation,
                                 gpointer               user_data)
{
	TrackerEndpointDBus *endpoint_dbus = user_data;
	TrackerSparqlConnection *conn;
	GUnixFDList *fd_list;
	GError *error = NULL;
	gchar *query;
	gint handle, fd = -1;

	conn = tracker_endpoint_get_sparql_connection (TRACKER_ENDPOINT (endpoint_dbus));
	fd_list = g_dbus_message_get_unix_fd_list (g_dbus_method_invocation_get_message (invocation));

	if (g_strcmp0 (method_name, "Query") == 0) {
		g_variant_get (parameters, "(sh)", &query, &handle);

		if (fd_list)
			fd = g_unix_fd_list_get (fd_list, handle, &error);

		if (fd < 0) {
			g_dbus_method_invocation_return_error (invocation,
			                                       G_DBUS_ERROR,
			                                       G_DBUS_ERROR_INVALID_ARGS,
			                                       "Did not get a file descriptor");
		} else {
			QueryRequest *request;

			request = query_request_new (endpoint_dbus, invocation, fd);
			tracker_sparql_connection_query_async (conn,
			                                       query,
			                                       endpoint_dbus->cancellable,
			                                       query_cb,
			                                       request);
		}

		g_free (query);
	} else if (g_strcmp0 (method_name, "Update") == 0 ||
	           g_strcmp0 (method_name, "UpdateArray") == 0) {
		g_variant_get (parameters, "(h)", &handle);

		if (fd_list)
			fd = g_unix_fd_list_get (fd_list, handle, &error);

		if (fd < 0) {
			g_dbus_method_invocation_return_error (invocation,
			                                       G_DBUS_ERROR,
			                                       G_DBUS_ERROR_INVALID_ARGS,
			                                       "Did not get a file descriptor");
		} else {
			UpdateRequest *request;

			request = update_request_new (endpoint_dbus, invocation,
			                              g_strcmp0 (method_name, "UpdateArray") == 0,
			                              fd);
			update_request_read_next (request, read_update_cb);
		}
	} else if (g_strcmp0 (method_name, "UpdateBlank") == 0) {
		g_variant_get (parameters, "(h)", &handle);

		if (fd_list)
			fd = g_unix_fd_list_get (fd_list, handle, &error);

		if (fd < 0) {
			g_dbus_method_invocation_return_error (invocation,
			                                       G_DBUS_ERROR,
			                                       G_DBUS_ERROR_INVALID_ARGS,
			                                       "Did not get a file descriptor");
		} else {
			UpdateRequest *request;

			request = update_request_new (endpoint_dbus, invocation, FALSE, fd);
			update_request_read_next (request, read_update_blank_cb);
		}
	} else {
		g_dbus_method_invocation_return_error (invocation,
		                                       G_DBUS_ERROR,
		                                       G_DBUS_ERROR_UNKNOWN_METHOD,
		                                       "Unknown method '%s'", method_name);
	}
}

static gboolean
tracker_endpoint_dbus_initable_init (GInitable     *initable,
                                     GCancellable  *cancellable,
                                     GError       **error)
{
	TrackerEndpointDBus *endpoint_dbus = TRACKER_ENDPOINT_DBUS (initable);

	endpoint_dbus->node_info = g_dbus_node_info_new_for_xml (introspection_xml,
	                                                         error);
	if (!endpoint_dbus->node_info)
		return FALSE;

	endpoint_dbus->register_id =
		g_dbus_connection_register_object (endpoint_dbus->dbus_connection,
		                                   endpoint_dbus->object_path,
		                                   endpoint_dbus->node_info->interfaces[0],
		                                   &(GDBusInterfaceVTable) {
			                                   endpoint_dbus_iface_method_call,
				                           NULL,
				                           NULL
				                   },
		                                   endpoint_dbus,
		                                   NULL,
		                                   error);
	return TRUE;
}

static void
tracker_endpoint_dbus_initable_iface_init (GInitableIface *iface)
{
	iface->init = tracker_endpoint_dbus_initable_init;
}

static void
tracker_endpoint_dbus_finalize (GObject *object)
{
	TrackerEndpointDBus *endpoint_dbus = TRACKER_ENDPOINT_DBUS (object);

	g_cancellable_cancel (endpoint_dbus->cancellable);

	if (endpoint_dbus->register_id != 0) {
		g_dbus_connection_unregister_object (endpoint_dbus->dbus_connection,
		                                     endpoint_dbus->register_id);
		endpoint_dbus->register_id = 0;
	}

	g_clear_object (&endpoint_dbus->cancellable);
	g_clear_object (&endpoint_dbus->dbus_connection);
	g_clear_pointer (&endpoint_dbus->object_path, g_free);
	g_clear_pointer (&endpoint_dbus->node_info,
	                 g_dbus_node_info_unref);

	G_OBJECT_CLASS (tracker_endpoint_dbus_parent_class)->finalize (object);
}

static void
tracker_endpoint_dbus_set_property (GObject      *object,
                                    guint         prop_id,
                                    const GValue *value,
                                    GParamSpec   *pspec)
{
	TrackerEndpointDBus *endpoint_dbus = TRACKER_ENDPOINT_DBUS (object);

	switch (prop_id) {
	case PROP_DBUS_CONNECTION:
		endpoint_dbus->dbus_connection = g_value_dup_object (value);
		break;
	case PROP_OBJECT_PATH:
		endpoint_dbus->object_path = g_value_dup_string (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
tracker_endpoint_dbus_get_property (GObject    *object,
                                    guint       prop_id,
                                    GValue     *value,
                                    GParamSpec *pspec)
{
	TrackerEndpointDBus *endpoint_dbus = TRACKER_ENDPOINT_DBUS (object);

	switch (prop_id) {
	case PROP_DBUS_CONNECTION:
		g_value_set_object (value, endpoint_dbus->dbus_connection);
		break;
	case PROP_OBJECT_PATH:
		g_value_set_string (value, endpoint_dbus->object_path);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
tracker_endpoint_dbus_class_init (TrackerEndpointDBusClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = tracker_endpoint_dbus_finalize;
	object_class->set_property = tracker_endpoint_dbus_set_property;
	object_class->get_property = tracker_endpoint_dbus_get_property;

	props[PROP_DBUS_CONNECTION] =
		g_param_spec_object ("dbus-connection",
		                     "DBus connection",
		                     "DBus connection",
		                     G_TYPE_DBUS_CONNECTION,
		                     G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);
	props[PROP_OBJECT_PATH] =
		g_param_spec_string ("object-path",
		                     "DBus object path",
		                     "DBus object path",
		                     NULL,
		                     G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);

	g_object_class_install_properties (object_class, N_PROPS, props);
}

static void
tracker_endpoint_dbus_init (TrackerEndpointDBus *endpoint)
{
}

TrackerEndpointDBus *
tracker_endpoint_dbus_new (TrackerSparqlConnection  *sparql_connection,
                           GDBusConnection          *dbus_connection,
                           const gchar              *object_path,
                           GCancellable             *cancellable,
                           GError                  **error)
{
	g_return_val_if_fail (TRACKER_SPARQL_IS_CONNECTION (sparql_connection), NULL);
	g_return_val_if_fail (G_IS_DBUS_CONNECTION (dbus_connection), NULL);
	g_return_val_if_fail (!cancellable || G_IS_CANCELLABLE (cancellable), NULL);
	g_return_val_if_fail (!error || !*error, NULL);

	return g_initable_new (TRACKER_TYPE_ENDPOINT_DBUS, cancellable, error,
	                       "dbus-connection", dbus_connection,
	                       "sparql-connection", sparql_connection,
	                       "object-path", object_path,
	                       NULL);
}
