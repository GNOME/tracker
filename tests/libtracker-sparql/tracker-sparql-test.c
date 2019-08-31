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

	connection = tracker_sparql_connection_get (NULL, &error);
	g_assert_no_error (error);
	g_assert (connection != NULL);

	test_tracker_sparql_cursor_next_async_query (connection, 0);
	g_main_loop_run (main_loop);
}

static void
test_tracker_sparql_connection_locking_sync (void)
{
	TrackerSparqlConnection *c1, *c2, *c3;

	c1 = tracker_sparql_connection_get (NULL, NULL);
	c2 = tracker_sparql_connection_get (NULL, NULL);
	c3 = tracker_sparql_connection_get (NULL, NULL);
	g_assert (c1 == c2);
	g_assert (c2 == c3);

	g_object_unref (c1);
	g_object_unref (c2);
	g_object_unref (c3);
}

static TrackerSparqlConnection *c1 = NULL;
static TrackerSparqlConnection *c2 = NULL;
static TrackerSparqlConnection *c3 = NULL;

static void
test_tracker_sparql_connection_locking_async_cb (GObject      *source,
                                                 GAsyncResult *result,
                                                 gpointer      user_data)
{
	TrackerSparqlConnection *connection;
	TrackerSparqlConnection *connection_waiting;
	GError *error = NULL;

	g_assert (result != NULL);
	connection = tracker_sparql_connection_get_finish (result, &error);
	g_assert_no_error (error);
	g_assert (connection != NULL);

	if (!c1) {
		g_print ("GOT connection #1, waiting connection:%p (expecting NULL)\n", user_data);
		c1 = connection;
	} else if (!c2) {
		g_print ("GOT connection #2, waiting connection:%p (expecting NULL)\n", user_data);
		c2 = connection;
	}

	connection_waiting = user_data;
	g_assert (connection_waiting == NULL);
}

static void
test_tracker_sparql_connection_locking_async (void)
{
	tracker_sparql_connection_get_async (NULL, test_tracker_sparql_connection_locking_async_cb, c2);
	tracker_sparql_connection_get_async (NULL, test_tracker_sparql_connection_locking_async_cb, c3);
	c3 = tracker_sparql_connection_get (NULL, NULL);
	g_assert (c3 != NULL);
}

static void
test_tracker_sparql_nb237150_cb (GObject      *source_object,
                                 GAsyncResult *result,
                                 gpointer      user_data)
{
	TrackerSparqlConnection *connection;
	GError *error = NULL;
	static gboolean had_1 = FALSE;
	static gboolean had_2 = FALSE;

	connection = tracker_sparql_connection_get_finish (result, &error);
	g_assert_no_error (error);
	g_assert (connection != NULL);

	/* Not actually worried about this being called */
	g_print ("Called back for #%d\n", GPOINTER_TO_INT(user_data));

	if (GPOINTER_TO_INT(user_data) == 1)
		had_1 = TRUE;
	if (GPOINTER_TO_INT(user_data) == 2)
		had_2 = TRUE;

	if (had_1 && had_2) {
		g_print ("Called back ALL\n");
		g_main_loop_quit (main_loop);
	}
}

static void
test_tracker_sparql_nb237150_subprocess (void)
{
	main_loop = g_main_loop_new (NULL, TRUE);

	g_print ("\n");
	g_print ("Calling #1 - tracker_sparql_connection_get_async()\n");
	tracker_sparql_connection_get_async (NULL, test_tracker_sparql_nb237150_cb, GINT_TO_POINTER(1));

	g_print ("Calling #2 - tracker_sparql_connection_get_async()\n");
	tracker_sparql_connection_get_async (NULL, test_tracker_sparql_nb237150_cb, GINT_TO_POINTER(2));

	g_print ("Calling both finished\n");
	g_main_loop_run (main_loop);
}

static void
test_tracker_sparql_nb237150 (void)
{
	/* Test NB#237150 - Second tracker_sparql_connection_get_async
	 * never returns
	 */
	g_test_trap_subprocess ("/libtracker-sparql/tracker-sparql/nb237150/subprocess",
	                        G_USEC_PER_SEC * 2,
	                        G_TEST_SUBPROCESS_INHERIT_STDOUT |
	                        G_TEST_SUBPROCESS_INHERIT_STDERR);

	g_test_trap_assert_passed ();

	/* Check we called the functions in the test */
	g_test_trap_assert_stdout ("*Calling #1*");
	g_test_trap_assert_stdout ("*Calling #2*");
	g_test_trap_assert_stdout ("*Calling both finished*");

	/* Check the callbacks from the functions we called were
	 * called in the test */
	g_test_trap_assert_stdout ("*Called back for #1*");
	g_test_trap_assert_stdout ("*Called back for #2*");
	g_test_trap_assert_stdout ("*Called back ALL*");
}

static void
test_tracker_sparql_connection_interleaved (void)
{
	GError *error = NULL;

	TrackerSparqlCursor *cursor1;
	TrackerSparqlCursor *cursor2;
	TrackerSparqlConnection *connection;

	const gchar* query = "select ?u {?u a rdfs:Resource .}";

	connection = tracker_sparql_connection_get (NULL, &error);
	g_assert_no_error (error);

	cursor1 = tracker_sparql_connection_query (connection, query, 0, &error);
	g_assert_no_error (error);

	/* intentionally not freeing cursor1 here */
	g_object_unref(connection);

	connection = tracker_sparql_connection_get (NULL, &error);
	g_assert_no_error (error);

	cursor2 = tracker_sparql_connection_query (connection, query, 0, &error);
	g_assert_no_error (error);

	g_object_unref(connection);

	g_object_unref(cursor2);
	g_object_unref(cursor1);
}

gint
main (gint argc, gchar **argv)
{
	int result;

	setlocale (LC_ALL, "");

	g_setenv ("TRACKER_TEST_DOMAIN_ONTOLOGY_RULE", TEST_DOMAIN_ONTOLOGY_RULE, TRUE);
	g_setenv ("TRACKER_DB_ONTOLOGIES_DIR", TEST_ONTOLOGIES_DIR, TRUE);

	g_test_init (&argc, &argv, NULL);

	/* NOTE: this first test must come BEFORE any others because
	 * connections are cached by libtracker-sparql.
	 */
	g_test_add_func ("/libtracker-sparql/tracker-sparql/nb237150",
	                 test_tracker_sparql_nb237150);
	g_test_add_func ("/libtracker-sparql/tracker-sparql/nb237150/subprocess",
	                 test_tracker_sparql_nb237150_subprocess);
	g_test_add_func ("/libtracker-sparql/tracker-sparql/tracker_sparql_escape_string",
	                 test_tracker_sparql_escape_string);
	g_test_add_func ("/libtracker-sparql/tracker-sparql/tracker_sparql_escape_uri_vprintf",
	                 test_tracker_sparql_escape_uri_vprintf);
	g_test_add_func ("/libtracker-sparql/tracker-sparql/tracker_sparql_connection_interleaved",
	                 test_tracker_sparql_connection_interleaved);
	g_test_add_func ("/libtracker-sparql/tracker-sparql/tracker_sparql_connection_locking_sync",
	                 test_tracker_sparql_connection_locking_sync);
	g_test_add_func ("/libtracker-sparql/tracker-sparql/tracker_sparql_connection_locking_async",
	                 test_tracker_sparql_connection_locking_async);
	g_test_add_func ("/libtracker-sparql/tracker-sparql/tracker_sparql_cursor_next_async",
	                 test_tracker_sparql_cursor_next_async);

	result = g_test_run ();

	return result;
}
