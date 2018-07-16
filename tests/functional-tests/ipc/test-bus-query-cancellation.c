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

	cancellable = g_cancellable_new ();
	tracker_sparql_connection_query_async (conn,
	                                       "SELECT ?urn WHERE {?urn a rdfs:Resource}",
	                                       cancellable,
	                                       query_cb,
	                                       NULL);
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

	g_setenv ("TRACKER_SPARQL_BACKEND", "bus", TRUE);
	conn = tracker_sparql_connection_get (NULL, &error);
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
	                                       query_cb,
	                                       NULL);
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

	g_setenv ("TRACKER_TEST_DOMAIN_ONTOLOGY_RULE", TEST_DOMAIN_ONTOLOGY_RULE, TRUE);
	g_setenv ("TRACKER_DB_ONTOLOGIES_DIR", TEST_ONTOLOGIES_DIR, TRUE);

	g_test_init (&argc, &argv, NULL);

	g_test_add_func ("/libtracker-sparql/tracker/gb737023",
	                 test_tracker_sparql_gb737023);

	result = g_test_run ();

	return result;
}
