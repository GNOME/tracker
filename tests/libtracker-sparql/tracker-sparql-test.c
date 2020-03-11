/*
 * Copyright (C) 2010, Nokia <ivan.frade@nokia.com>
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

#include "config.h"

#include <locale.h>

#include <glib-object.h>

#include <libtracker-sparql/tracker-sparql.h>
#include <libtracker-sparql/tracker-version.h>

typedef struct {
	const gchar *input ;
	const gchar *output;
} ESCAPE_TEST_DATA;

ESCAPE_TEST_DATA test_data []  = {
	{ "SELECT \"a\"", "SELECT \\\"a\\\"" },
	{ "SELECT \'a\'", "SELECT \\\'a\\\'" },
	{ "SELECT ?u \t \n \r \b \f", "SELECT ?u \\t \\n \\r \\b \\f" },
	{ NULL, NULL }
};

static GMainLoop *main_loop;

#define N_QUERIES 3

static GCancellable *cancellables[N_QUERIES] = { NULL, };

/* OK:   query 0 with either query 4 or 5.
 * FAIL: query 4 and 5 together (requires data to exist)
 */
static const gchar *queries[N_QUERIES] = {
	/* #1 */
	"SELECT ?p WHERE { ?p tracker:indexed true }",
	/* #2 */
	"SELECT ?prefix ?ns WHERE { ?ns a tracker:Namespace ; tracker:prefix ?prefix }",
	/* #3 */
	"SELECT ?p WHERE { ?p tracker:fulltextIndexed true }",
};

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

	conn = tracker_sparql_connection_new_with_ontology (0, store, ontology, NULL, error);
	g_object_unref (store);
	g_object_unref (ontology);

	return conn;
}

static void
test_tracker_sparql_escape_string (void)
{
	gint i;
	gchar *result;

	for (i = 0; test_data[i].input != NULL; i++) {
		result = tracker_sparql_escape_string (test_data[i].input);
		g_assert_cmpstr (result, ==, test_data[i].output);
		g_free (result);
	}
}

static void
test_tracker_sparql_escape_uri_vprintf (void)
{
	gchar *result;

	result = tracker_sparql_escape_uri_printf ("test:uri:contact-%d", 14, NULL);
	g_assert_cmpstr (result, ==, "test:uri:contact-14");
	g_free (result);
}

static void test_tracker_sparql_cursor_next_async_query (TrackerSparqlConnection *connection,
                                                         gint                     query);

static void
test_tracker_sparql_cursor_next_async_cb (GObject      *source,
                                          GAsyncResult *result,
                                          gpointer      user_data)
{
	TrackerSparqlConnection *connection;
	TrackerSparqlCursor *cursor;
	GError *error = NULL;
	gboolean success;
	static gint finished = 0;
	static gint next = 0;
	gint next_to_cancel = 1;
	gint query;

	query = GPOINTER_TO_INT(user_data);

	g_assert (result != NULL);
	success = tracker_sparql_cursor_next_finish (TRACKER_SPARQL_CURSOR (source),
	                                             result,
	                                             &error);

	if (finished == 1 && next == next_to_cancel) {
		g_assert_error (error, G_IO_ERROR, G_IO_ERROR_CANCELLED);
		g_print ("Got Cancellation GError\n");
	} else {
		g_assert_no_error (error);
	}

	cursor = TRACKER_SPARQL_CURSOR (source);
	g_assert (cursor != NULL);
	connection = tracker_sparql_cursor_get_connection (cursor);

	g_print ("  %d: %s\n",
	         query,
	         tracker_sparql_cursor_get_string (cursor, 0, NULL));

	if (!success) {
		finished++;
		next = 0;

		g_print ("Finished %d\n", finished);

		if (finished == 1 || finished == 2) {
			test_tracker_sparql_cursor_next_async_query (connection,
			                                             finished);
		} else if (finished == 3) {
			g_main_loop_quit (main_loop);
		}
	} else {
		next++;

		/* Random number here for next_count_to_cancel is "2",
		 * just want to do this mid-cursor iteration
		 */
		if (next == next_to_cancel && finished == 1) {
			/* Cancel */
			g_print ("Cancelling cancellable:%p at count:%d\n",
			         cancellables[query],
			         next);
			g_cancellable_cancel (cancellables[query]);
		}

		tracker_sparql_cursor_next_async (cursor,
		                                  cancellables[query],
		                                  test_tracker_sparql_cursor_next_async_cb,
		                                  user_data);
	}
}

static void
test_tracker_sparql_cursor_next_async_query (TrackerSparqlConnection *connection,
                                             gint                     query)
{
	TrackerSparqlCursor *cursor;
	GError *error = NULL;

	g_assert (query < G_N_ELEMENTS (queries));
	g_print ("ASYNC query %d starting:\n", query);

	cancellables[query] = g_cancellable_new ();
	g_assert (cancellables[query] != NULL);

	cursor = tracker_sparql_connection_query (connection,
	                                          queries[query],
	                                          NULL,
	                                          &error);
	g_assert_no_error (error);
	g_assert (cursor != NULL);

	tracker_sparql_cursor_next_async (cursor,
	                                  cancellables[query],
	                                  test_tracker_sparql_cursor_next_async_cb,
	                                  GINT_TO_POINTER(query));
}

static void
test_tracker_sparql_cursor_next_async (void)
{
	TrackerSparqlConnection *connection;
	GError *error = NULL;

	main_loop = g_main_loop_new (NULL, TRUE);

	/* So, the idea here:
	 * 1. Test async cursor_next() call.
	 * 2. Make sure we can cancel a cursor_next() call and start a new query (was failing)
	 * 3. Handle multiple async queries + async cursor_next() calls.
	 */

	connection = create_local_connection (&error);
	g_assert_no_error (error);
	g_assert (connection != NULL);

	test_tracker_sparql_cursor_next_async_query (connection, 0);
	g_main_loop_run (main_loop);
}

static void
test_tracker_sparql_connection_interleaved (void)
{
	GError *error = NULL;

	TrackerSparqlCursor *cursor1;
	TrackerSparqlCursor *cursor2;
	TrackerSparqlConnection *connection;

	const gchar* query = "select ?u {?u a rdfs:Resource .}";

	connection = create_local_connection (&error);
	g_assert_no_error (error);

	cursor1 = tracker_sparql_connection_query (connection, query, 0, &error);
	g_assert_no_error (error);

	/* intentionally not freeing cursor1 here */

	cursor2 = tracker_sparql_connection_query (connection, query, 0, &error);
	g_assert_no_error (error);

	g_object_unref(connection);

	g_object_unref(cursor2);
	g_object_unref(cursor1);
}

static void
test_tracker_check_version (void)
{
	g_assert_true(tracker_check_version(TRACKER_MAJOR_VERSION,
	                                    TRACKER_MINOR_VERSION,
	                                    TRACKER_MICRO_VERSION) == NULL);
}

gint
main (gint argc, gchar **argv)
{
	int result;

	setlocale (LC_ALL, "");

	g_setenv ("TRACKER_DB_ONTOLOGIES_DIR", TEST_ONTOLOGIES_DIR, TRUE);

	g_test_init (&argc, &argv, NULL);

	/* g_test_init() enables verbose logging by default, but Tracker is too
	 * verbose. To make the logs managable, we hide DEBUG and INFO messages
	 * unless TRACKER_TESTS_VERBOSE is set.
	 */
	if (! g_getenv ("TRACKER_TESTS_VERBOSE")) {
		g_log_set_handler ("Tracker", G_LOG_LEVEL_DEBUG | G_LOG_LEVEL_INFO, g_log_default_handler, NULL);
	}

	g_test_add_func ("/libtracker-sparql/tracker-sparql/tracker_sparql_escape_string",
	                 test_tracker_sparql_escape_string);
	g_test_add_func ("/libtracker-sparql/tracker-sparql/tracker_sparql_escape_uri_vprintf",
	                 test_tracker_sparql_escape_uri_vprintf);
	g_test_add_func ("/libtracker-sparql/tracker-sparql/tracker_sparql_connection_interleaved",
	                 test_tracker_sparql_connection_interleaved);
	g_test_add_func ("/libtracker-sparql/tracker-sparql/tracker_sparql_cursor_next_async",
	                 test_tracker_sparql_cursor_next_async);
	g_test_add_func ("/libtracker-sparql/tracker-sparql/tracker_check_version",
	                 test_tracker_check_version);

	result = g_test_run ();

	return result;
}
