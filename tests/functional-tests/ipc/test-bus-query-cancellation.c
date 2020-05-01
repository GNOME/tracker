/*
 * Copyright (C) 2014, Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 */

#include <locale.h>

#include <gio/gio.h>

#include <libtracker-sparql/tracker-sparql.h>

#define MAX_TRIES 100

static int counter = 0;
static gboolean started = FALSE;

TrackerSparqlConnection *
create_local_connection (GError **error)
{
        TrackerSparqlConnection *conn;
        GFile *store, *ontology;
        gchar *path;

        path = g_build_filename (g_get_tmp_dir (), "libtracker-sparql-test-XXXXXX", NULL);
        g_mkdtemp_full (path, 0700);
        store = g_file_new_for_path (path);
        g_free (path);

        ontology = g_file_new_for_path (TEST_ONTOLOGIES_DIR);

        conn = tracker_sparql_connection_new (0, store, ontology, NULL, error);
        g_object_unref (store);
        g_object_unref (ontology);

        return conn;
}

static gpointer
thread_func (gpointer user_data)
{
	GDBusConnection *dbus_conn = user_data;
	TrackerSparqlConnection *direct;
	TrackerEndpointDBus *endpoint;
	GMainContext *context;
	GMainLoop *main_loop;

	context = g_main_context_new ();
	g_main_context_push_thread_default (context);

	main_loop = g_main_loop_new (context, FALSE);

	direct = create_local_connection (NULL);
	if (!direct)
		return NULL;

	endpoint = tracker_endpoint_dbus_new (direct, dbus_conn, NULL, NULL, NULL);
	if (!endpoint)
		return NULL;

	started = TRUE;
	g_main_loop_run (main_loop);

	return NULL;
}

static TrackerSparqlConnection *
create_dbus_connection (GError **error)
{
	TrackerSparqlConnection *dbus;
	GDBusConnection *dbus_conn;

	dbus_conn = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, error);
	if (!dbus_conn)
		return NULL;

	g_thread_new (NULL, thread_func, dbus_conn);

	while (!started)
		g_usleep (100);

	dbus = tracker_sparql_connection_bus_new (g_dbus_connection_get_unique_name (dbus_conn),
						  NULL, dbus_conn, error);
	return dbus;
}

static void
query_cb (GObject *source_object, GAsyncResult *res, gpointer user_data)
{
	GCancellable *cancellable;
	GError *error = NULL;
	TrackerSparqlConnection *conn = TRACKER_SPARQL_CONNECTION (source_object);
	TrackerSparqlCursor *cursor = NULL;

	cursor = tracker_sparql_connection_query_finish (conn, res, &error);
	if (error != NULL) {
		g_error_free (error);
	}

	counter++;
	if (counter > MAX_TRIES) {
		g_main_loop_quit (user_data);
		return;
	}

	cancellable = g_cancellable_new ();
	tracker_sparql_connection_query_async (conn,
	                                       "SELECT ?urn WHERE {?urn a rdfs:Resource}",
	                                       cancellable,
	                                       query_cb, user_data);
	g_cancellable_cancel (cancellable);

	g_object_unref (cancellable);
	g_clear_object (&cursor);
}

static gboolean
quit_cb (gpointer user_data)
{
	g_main_loop_quit ((GMainLoop *) user_data);
	return G_SOURCE_REMOVE;
}

static void
test_tracker_sparql_gb737023 (void)
{
	GCancellable *cancellable;
	GError *error = NULL;
	GMainLoop *loop;
	TrackerSparqlConnection *conn;

	g_test_bug_base ("https://bugzilla.gnome.org/show_bug.cgi?id=");
	g_test_bug ("737023");

	conn = create_dbus_connection (&error);
        g_assert_no_error (error);

	loop = g_main_loop_new (NULL, FALSE);

	/* This should be enough. */
	g_timeout_add_seconds_full (G_PRIORITY_DEFAULT,
	                            2,
	                            quit_cb,
	                            loop,
	                            (GDestroyNotify) g_main_loop_unref);

	cancellable = g_cancellable_new ();
	tracker_sparql_connection_query_async (conn,
	                                       "SELECT ?urn WHERE {?urn a rdfs:Resource}",
	                                       cancellable,
	                                       query_cb, loop);
	g_cancellable_cancel (cancellable);
	g_main_loop_run (loop);

	g_object_unref (cancellable);
	g_object_unref (conn);
}

gint
main (gint argc, gchar **argv)
{
	gint result;

	setlocale (LC_ALL, "");

	g_test_init (&argc, &argv, NULL);

	g_test_add_func ("/libtracker-sparql/tracker/gb737023",
	                 test_tracker_sparql_gb737023);

	result = g_test_run ();

	return result;
}
