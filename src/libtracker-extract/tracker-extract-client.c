/*
 * Copyright (C) 2010, Nokia <ivan.frade@nokia.com>
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
#include "tracker-extract-client.h"

#include <string.h>
#include <gio/gunixfdlist.h>
#include <gio/gunixinputstream.h>

/* Size of buffers used when sending data over a pipe, using DBus FD passing */
#define DBUS_PIPE_BUFFER_SIZE      65536

#define DBUS_SERVICE_EXTRACT       "org.freedesktop.Tracker1.Extract"
#define DBUS_PATH_EXTRACT          "/org/freedesktop/Tracker1/Extract"
#define DBUS_INTERFACE_EXTRACT     "org.freedesktop.Tracker1.Extract"

static GDBusConnection *connection = NULL;

typedef void (* SendAndSpliceCallback) (void     *buffer,
                                        gssize    buffer_size,
                                        GError   *error, /* Don't free */
                                        gpointer  user_data);

typedef struct {
	GInputStream *unix_input_stream;
	GInputStream *buffered_input_stream;
	GOutputStream *output_stream;
	SendAndSpliceCallback callback;
	GCancellable *cancellable;
	gpointer data;
	gboolean splice_finished;
	gboolean dbus_finished;
	GError *error;
} SendAndSpliceData;

typedef struct {
	TrackerExtractInfo *info;
	GSimpleAsyncResult *res;
} MetadataCallData;

static SendAndSpliceData *
send_and_splice_data_new (GInputStream          *unix_input_stream,
                          GInputStream          *buffered_input_stream,
                          GOutputStream         *output_stream,
                          GCancellable          *cancellable,
                          SendAndSpliceCallback  callback,
                          gpointer               user_data)
{
	SendAndSpliceData *data;

	data = g_slice_new0 (SendAndSpliceData);
	data->unix_input_stream = unix_input_stream;
	data->buffered_input_stream = buffered_input_stream;
	data->output_stream = output_stream;

	if (cancellable) {
		data->cancellable = g_object_ref (cancellable);
	}

	data->callback = callback;
	data->data = user_data;

	return data;
}

static void
send_and_splice_data_free (SendAndSpliceData *data)
{
	g_object_unref (data->unix_input_stream);
	g_object_unref (data->buffered_input_stream);
	g_object_unref (data->output_stream);

	if (data->cancellable) {
		g_object_unref (data->cancellable);
	}

	if (data->error) {
		g_error_free (data->error);
	}

	g_slice_free (SendAndSpliceData, data);
}

static MetadataCallData *
metadata_call_data_new (TrackerExtractInfo *info,
                        GSimpleAsyncResult *res)
{
	MetadataCallData *data;

	data = g_slice_new (MetadataCallData);
	data->res = g_object_ref (res);
	data->info = tracker_extract_info_ref (info);

	return data;
}

static void
metadata_call_data_free (MetadataCallData *data)
{
	tracker_extract_info_unref (data->info);
	g_object_unref (data->res);
	g_slice_free (MetadataCallData, data);
}

static void
dbus_send_and_splice_async_finish (SendAndSpliceData *data)
{
	if (!data->error) {
		(* data->callback) (g_memory_output_stream_get_data (G_MEMORY_OUTPUT_STREAM (data->output_stream)),
		                    g_memory_output_stream_get_data_size (G_MEMORY_OUTPUT_STREAM (data->output_stream)),
		                    NULL,
		                    data->data);
	} else {
		(* data->callback) (NULL, -1, data->error, data->data);
	}

	send_and_splice_data_free (data);
}

static void
send_and_splice_splice_callback (GObject      *source,
                                 GAsyncResult *result,
                                 gpointer      user_data)
{
	SendAndSpliceData *data = user_data;
	GError *error = NULL;

	g_output_stream_splice_finish (G_OUTPUT_STREAM (source), result, &error);

	if (error) {
		if (!data->error) {
			data->error = error;
		} else {
			g_error_free (error);
		}
	}

	data->splice_finished = TRUE;

	if (data->dbus_finished) {
		dbus_send_and_splice_async_finish (data);
	}
}

static void
send_and_splice_dbus_callback (GObject      *source,
                               GAsyncResult *result,
                               gpointer      user_data)
{
	SendAndSpliceData *data = user_data;
	GDBusMessage *reply;
	GError *error = NULL;

	reply = g_dbus_connection_send_message_with_reply_finish (G_DBUS_CONNECTION (source),
	                                                          result, &error);

	if (reply) {
		if (g_dbus_message_get_message_type (reply) == G_DBUS_MESSAGE_TYPE_ERROR) {
			g_dbus_message_to_gerror (reply, &error);
		}

		g_object_unref (reply);
	}

	if (error) {
		if (!data->error) {
			data->error = error;
		} else {
			g_error_free (error);
		}
	}

	data->dbus_finished = TRUE;

	if (data->splice_finished) {
		dbus_send_and_splice_async_finish (data);
	}
}

static void
dbus_send_and_splice_async (GDBusConnection       *connection,
                            GDBusMessage          *message,
                            int                    fd,
                            GCancellable          *cancellable,
                            SendAndSpliceCallback  callback,
                            gpointer               user_data)
{
	SendAndSpliceData *data;
	GInputStream *unix_input_stream;
	GInputStream *buffered_input_stream;
	GOutputStream *output_stream;

	unix_input_stream = g_unix_input_stream_new (fd, TRUE);
	buffered_input_stream = g_buffered_input_stream_new_sized (unix_input_stream,
	                                                           DBUS_PIPE_BUFFER_SIZE);
	output_stream = g_memory_output_stream_new (NULL, 0, g_realloc, g_free);

	data = send_and_splice_data_new (unix_input_stream,
	                                 buffered_input_stream,
	                                 output_stream,
	                                 cancellable,
	                                 callback,
	                                 user_data);

	g_dbus_connection_send_message_with_reply (connection,
	                                           message,
	                                           G_DBUS_SEND_MESSAGE_FLAGS_NONE,
	                                           -1,
	                                           NULL,
	                                           cancellable,
	                                           send_and_splice_dbus_callback,
	                                           data);

	g_output_stream_splice_async (data->output_stream,
	                              data->buffered_input_stream,
	                              G_OUTPUT_STREAM_SPLICE_CLOSE_SOURCE |
	                              G_OUTPUT_STREAM_SPLICE_CLOSE_TARGET,
	                              0,
	                              cancellable,
	                              send_and_splice_splice_callback,
	                              data);
}

static void
get_metadata_fast_cb (void     *buffer,
                      gssize    buffer_size,
                      GError   *error,
                      gpointer  user_data)
{
	MetadataCallData *data;

	data = user_data;

	if (G_UNLIKELY (error)) {
		g_simple_async_result_set_from_error (data->res, error);
	} else {
		const gchar *preupdate, *sparql, *where, *end;
		TrackerSparqlBuilder *builder;
		gsize len;

		preupdate = sparql = where = NULL;
		end = (gchar *) buffer + buffer_size;

		if (buffer) {
			preupdate = buffer;
			len = strlen (preupdate);

			if (preupdate + len < end) {
				buffer_size -= len;
				sparql = preupdate + len + 1;
				len = strlen (sparql);

				if (sparql + len < end) {
					where = sparql + len + 1;
				}
			}
		}

		tracker_extract_info_set_where_clause (data->info, where);

		builder = tracker_extract_info_get_preupdate_builder (data->info);
		tracker_sparql_builder_prepend (builder, preupdate);

		builder = tracker_extract_info_get_metadata_builder (data->info);
		tracker_sparql_builder_prepend (builder, sparql);

		g_simple_async_result_set_op_res_gpointer (data->res,
		                                           tracker_extract_info_ref (data->info),
		                                           (GDestroyNotify) tracker_extract_info_unref);
	}

	g_simple_async_result_complete_in_idle (data->res);
	metadata_call_data_free (data);
}

static void
get_metadata_fast_async (GDBusConnection    *connection,
                         GFile              *file,
                         const gchar        *mime_type,
                         GCancellable       *cancellable,
                         GSimpleAsyncResult *res)
{
	MetadataCallData *data;
	TrackerExtractInfo *info;
	GDBusMessage *message;
	GUnixFDList *fd_list;
	int pipefd[2];
	gchar *uri;

	if (pipe (pipefd) < 0) {
		g_critical ("Coudln't open pipe");
		/* FIXME: Report async error */
		return;
	}

	message = g_dbus_message_new_method_call (DBUS_SERVICE_EXTRACT,
	                                          DBUS_PATH_EXTRACT,
	                                          DBUS_INTERFACE_EXTRACT,
	                                          "GetMetadataFast");

	fd_list = g_unix_fd_list_new ();
	uri = g_file_get_uri (file);

	g_dbus_message_set_body (message,
	                         g_variant_new ("(ssh)",
	                                        uri,
	                                        mime_type,
	                                        g_unix_fd_list_append (fd_list,
	                                                               pipefd[1],
	                                                               NULL)));
	g_dbus_message_set_unix_fd_list (message, fd_list);

	/* We need to close the fd as g_unix_fd_list_append duplicates the fd */

	close (pipefd[1]);
	g_object_unref (fd_list);
	g_free (uri);

	info = tracker_extract_info_new (file, mime_type, NULL);
	data = metadata_call_data_new (info, res);

	dbus_send_and_splice_async (connection,
	                            message,
	                            pipefd[0],
	                            cancellable,
	                            get_metadata_fast_cb,
	                            data);
	g_object_unref (message);
	tracker_extract_info_unref (info);
}

/**
 * tracker_extract_client_get_metadata:
 * @file: a #GFile
 * @mime_type: mimetype of @file
 * @cancellable: (allow-none): cancellable for the async operation, or %NULL
 * @callback: (scope async): callback to call when the request is satisfied.
 * @user_data: (closure): data for the callback function
 *
 * Asynchronously requests metadata for @file, this request is sent to the
 * tracker-extract daemon.
 *
 * When the request is finished, @callback will be executed. You can then
 * call tracker_extract_client_get_metadata_finish() to get the result of
 * the operation.
 **/
void
tracker_extract_client_get_metadata (GFile               *file,
                                     const gchar         *mime_type,
                                     GCancellable        *cancellable,
                                     GAsyncReadyCallback  callback,
                                     gpointer             user_data)
{
	GSimpleAsyncResult *res;
	GError *error = NULL;

	g_return_if_fail (G_IS_FILE (file));
	g_return_if_fail (mime_type != NULL);
	g_return_if_fail (!cancellable || G_IS_CANCELLABLE (cancellable));
	g_return_if_fail (callback != NULL);

	if (G_UNLIKELY (!connection)) {
		connection = g_bus_get_sync (G_BUS_TYPE_SESSION, cancellable, &error);

		if (error) {
			g_simple_async_report_gerror_in_idle (G_OBJECT (file), callback, user_data, error);
			g_error_free (error);
			return;
		}
	}

	res = g_simple_async_result_new (G_OBJECT (file), callback, user_data, NULL);
	g_simple_async_result_set_handle_cancellation (res, TRUE);

	get_metadata_fast_async (connection, file, mime_type, cancellable, res);
	g_object_unref (res);
}

/**
 * tracker_extract_client_get_metadata_finish:
 * @file: a #GFile
 * @res: a #GAsyncResult
 * @error: return location for error, or %NULL to ignore.
 *
 * Finishes an asynchronous metadata request.
 *
 * Returns: (transfer full): the #TrackerExtractInfo holding the result.
 **/
TrackerExtractInfo *
tracker_extract_client_get_metadata_finish (GFile         *file,
                                            GAsyncResult  *res,
                                            GError       **error)
{
	g_return_val_if_fail (G_IS_FILE (file), NULL);
	g_return_val_if_fail (G_IS_ASYNC_RESULT (res), NULL);
	g_return_val_if_fail (!error || !*error, NULL);

	if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error)) {
		return NULL;
	}

	return g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (res));
}

/**
 * tracker_extract_client_cancel_for_prefix:
 * @prefix: a #GFile
 *
 * Cancels any ongoing extraction task for (or within) @prefix.
 **/
void
tracker_extract_client_cancel_for_prefix (GFile *prefix)
{
	GDBusMessage *message;
	gchar *uris[2];

	uris[0] = g_file_get_uri (prefix);
	uris[1] = NULL;

	message = g_dbus_message_new_method_call (DBUS_SERVICE_EXTRACT,
	                                          DBUS_PATH_EXTRACT,
	                                          DBUS_INTERFACE_EXTRACT,
	                                          "CancelTasks");

	g_dbus_message_set_body (message, g_variant_new ("(^as)", uris));
	g_dbus_connection_send_message (connection, message,
	                                G_DBUS_SEND_MESSAGE_FLAGS_NONE,
	                                NULL, NULL);

	g_free (uris[0]);
}
