/*
 * Copyright (C) 2011, Nokia <ivan.frade@nokia.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.          See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 */

#include <stdlib.h>

#include <libtracker-sparql/tracker-sparql.h>

typedef struct {
	TrackerSparqlConnection *connection;
	GCancellable *cancellable;
	GTimer *timer;
	GMainLoop *loop;
} MyData;

static void
cursor_cb (GObject      *object,
           GAsyncResult *res,
           gpointer      user_data)
{
	TrackerSparqlCursor *cursor;
	GError *error = NULL;
	MyData *md = user_data;
	gboolean more_results;

	cursor = TRACKER_SPARQL_CURSOR (object);
	more_results = tracker_sparql_cursor_next_finish (cursor,
	                                                  res,
	                                                  &error);

	if (!error) {
		static gint i = 0;

		if (more_results) {
			if (i++ < 5) {
				if (i == 1) {
					g_print ("Printing first 5 results:\n");
				}

				g_print ("  %s\n", tracker_sparql_cursor_get_string (cursor, 0, NULL));

				if (i == 5) {
					g_print ("  ...\n");
					g_print ("  Printing nothing for remaining results\n");
				}
			}

			tracker_sparql_cursor_next_async (cursor,
			                                  md->cancellable,
			                                  cursor_cb,
			                                  md);
		} else {
			g_print ("\n");
			g_print ("Async cursor next took: %.6f (for all %d results)\n",
			         g_timer_elapsed (md->timer, NULL), i);

			g_object_unref (cursor);
			g_main_loop_quit (md->loop);
		}
	} else {
		g_critical ("Could not run cursor next: %s", error->message);

		g_error_free (error);
		g_main_loop_quit (md->loop);
	}
}

static void
query_cb (GObject      *object,
          GAsyncResult *res,
          gpointer      user_data)
{
	TrackerSparqlCursor *cursor;
	GError *error = NULL;
	MyData *md = user_data;

	cursor = tracker_sparql_connection_query_finish (TRACKER_SPARQL_CONNECTION (object),
	                                                 res,
	                                                 &error);
	g_print ("Async query took: %.6f\n", g_timer_elapsed (md->timer, NULL));

	g_timer_start (md->timer);

	if (!error) {
		tracker_sparql_cursor_next_async (cursor,
		                                  md->cancellable,
		                                  cursor_cb,
		                                  md);
	} else {
		g_critical ("Could not run query: %s", error->message);
		g_error_free (error);
		g_main_loop_quit (md->loop);
	}
}

static void
connection_cb (GObject      *object,
               GAsyncResult *res,
               gpointer      user_data)
{
	MyData *md = user_data;
	GError *error = NULL;

	md->connection = tracker_sparql_connection_new_finish (res, &error);
	g_print ("Async connection took: %.6f\n", g_timer_elapsed (md->timer, NULL));

	g_timer_start (md->timer);

	if (!error) {
		tracker_sparql_connection_query_async (md->connection,
		                                       "SELECT ?r { ?r a rdfs:Resource }",
		                                       md->cancellable,
		                                       query_cb,
		                                       md);
	} else {
		g_critical ("Could not connect: %s", error->message);
		g_error_free (error);
		g_main_loop_quit (md->loop);
	}
}

gint
main (gint argc, gchar *argv[])
{
	g_autoptr(GFile) store, ontology;
	MyData *md;

	if (argc > 1) {
		store = g_file_new_for_commandline_arg (argv[1]);
	} else {
		g_print ("Usage: <command> <store-path>\n");
		exit (1);
	}

	md = g_new0 (MyData, 1);
	md->loop = g_main_loop_new (NULL, FALSE);
	md->timer = g_timer_new ();
	md->cancellable = g_cancellable_new ();

	ontology = tracker_sparql_get_ontology_nepomuk ();
	tracker_sparql_connection_new_async (0,
	                                     store,
	                                     ontology,
	                                     md->cancellable,
	                                     connection_cb,
	                                     md);

	g_main_loop_run (md->loop);

	if (md->connection) {
		g_object_unref (md->connection);
	}

	g_cancellable_cancel (md->cancellable);
	g_object_unref (md->cancellable);
	g_timer_destroy (md->timer);
	g_main_loop_unref (md->loop);

	g_free (md);

	return EXIT_SUCCESS;
}
