/*
 * Copyright (C) 2020, Red Hat Inc.
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

#include <string.h>
#include <locale.h>

#include <glib/gstdio.h>
#include <gio/gio.h>

#include <libtracker-sparql/tracker-sparql.h>

typedef struct _TestInfo TestInfo;

struct _TestInfo {
	const gchar *test_name;
	gboolean expect_query_error;
};

const TestInfo tests[] = {
	{ "service/service-query-1", FALSE },
	{ "service/service-after-triples-1", FALSE },
	{ "service/service-before-triples-1", FALSE },
	{ "service/service-local-filter-1", FALSE },
	{ "service/service-union-with-local-1", FALSE },
};

static GDBusConnection *dbus_conn = NULL;
static TrackerSparqlConnection *local = NULL;
static TrackerSparqlConnection *remote = NULL;
static TrackerEndpointDBus *endpoint = NULL;
static GMainLoop *endpoint_loop = NULL;

static void
check_result (TrackerSparqlCursor *cursor,
              const TestInfo      *test_info,
              const gchar         *results_filename,
              GError              *error)
{
	GString *test_results;
	gchar *results;
	GError *nerror = NULL;

	if (test_info->expect_query_error) {
		g_assert (error != NULL);
	} else {
		g_assert_no_error (error);
	}

	g_file_get_contents (results_filename, &results, NULL, &nerror);
	g_assert_no_error (nerror);
	g_clear_error (&nerror);

	/* compare results with reference output */

	test_results = g_string_new ("");

	if (cursor) {
		gint col;

		while (tracker_sparql_cursor_next (cursor, NULL, &error)) {
			GString *row_str = g_string_new (NULL);

			for (col = 0; col < tracker_sparql_cursor_get_n_columns (cursor); col++) {
				const gchar *str;

				if (col > 0) {
					g_string_append (row_str, "\t");
				}

				str = tracker_sparql_cursor_get_string (cursor, col, NULL);

				/* Hack to avoid misc properties that might tamper with
				 * test reproduceability in DESCRIBE and other unrestricted
				 * queries.
				 */
				if (g_strcmp0 (str, TRACKER_PREFIX_NRL "modified") == 0 ||
				    g_strcmp0 (str, TRACKER_PREFIX_NRL "added") == 0) {
					g_string_free (row_str, TRUE);
					row_str = NULL;
					break;
				}

				if (str != NULL) {
					/* bound variable */
					g_string_append_printf (row_str, "\"%s\"", str);
				}
			}

			if (row_str) {
				g_string_append (test_results, row_str->str);
				g_string_free (row_str, TRUE);
				g_string_append (test_results, "\n");
			}
		}
	} else if (test_info->expect_query_error) {
		g_assert (error != NULL && error->domain == TRACKER_SPARQL_ERROR);
		g_string_free (test_results, TRUE);
		g_free (results);
		return;
	}

	g_assert_no_error (error);

	if (strcmp (results, test_results->str) != 0) {
		/* print result difference */
		gchar *quoted_results;
		gchar *command_line;
		gchar *quoted_command_line;
		gchar *shell;
		gchar *diff;

		quoted_results = g_shell_quote (test_results->str);
		command_line = g_strdup_printf ("echo -n %s | diff -u %s -", quoted_results, results_filename);
		quoted_command_line = g_shell_quote (command_line);
		shell = g_strdup_printf ("sh -c %s", quoted_command_line);
		g_spawn_command_line_sync (shell, &diff, NULL, NULL, &error);
		g_assert_no_error (error);

		g_error ("%s", diff);

		g_free (quoted_results);
		g_free (command_line);
		g_free (quoted_command_line);
		g_free (shell);
		g_free (diff);
	}

	g_string_free (test_results, TRUE);
	g_free (results);
}

static gpointer
thread_func (gpointer user_data)
{
	GMainContext *context;
	GError *error = NULL;

	context = g_main_context_new ();
	g_main_context_push_thread_default (context);

	endpoint_loop = g_main_loop_new (context, FALSE);

	endpoint = tracker_endpoint_dbus_new (user_data, dbus_conn, NULL, NULL, &error);
	g_assert_no_error (error);

	if (!endpoint)
		return NULL;

	g_main_loop_run (endpoint_loop);

	return NULL;
}

static void
test_sparql_query (TestInfo      *test_info,
                   gconstpointer  context)
{
	TrackerSparqlCursor *cursor;
	GError *error = NULL;
	gchar *query, *query_filename, *service_query;
	gchar *results_filename;
	gchar *prefix, *test_prefix;
	GFile *ontology;

	/* initialization */
	prefix = g_build_filename (TOP_SRCDIR, "tests", "libtracker-data", NULL);
	test_prefix = g_build_filename (prefix, test_info->test_name, NULL);
	g_free (prefix);

	ontology = g_file_new_for_path (TEST_ONTOLOGIES_DIR);
	local = tracker_sparql_connection_new (0, NULL, ontology, NULL, &error);
	g_assert_no_error (error);
	g_object_unref (ontology);

	remote = tracker_sparql_connection_new (0, NULL, ontology, NULL, &error);
	g_assert_no_error (error);

	g_thread_new (NULL, thread_func, remote);
	while (!endpoint) {
		g_usleep (100);
	}

	query_filename = g_strconcat (test_prefix, ".rq", NULL);
	g_file_get_contents (query_filename, &query, NULL, &error);
	g_assert_no_error (error);
	g_free (query_filename);

	results_filename = g_strconcat (test_prefix, ".out", NULL);
	g_free (test_prefix);

	/* perform actual query */
	service_query = g_strdup_printf (query,
					 g_dbus_connection_get_unique_name (dbus_conn));
	cursor = tracker_sparql_connection_query (local, service_query, NULL, &error);
	g_free (service_query);
	g_free (query);

	check_result (cursor, test_info, results_filename, error);
	g_free (results_filename);
	g_clear_object (&cursor);

	/* cleanup */
	g_main_loop_quit (endpoint_loop);
	g_main_loop_unref (endpoint_loop);
	endpoint_loop = NULL;

	g_clear_object (&local);
	g_clear_object (&remote);
	g_clear_object (&endpoint);
}

static void
setup (TestInfo      *info,
       gconstpointer  context)
{
	const TestInfo *test = context;

	*info = *test;
}

static void
teardown (TestInfo      *info,
          gconstpointer  context)
{
}

int
main (int argc, char **argv)
{
	GError *error = NULL;
	gint result;
	gint i;

	setlocale (LC_COLLATE, "en_US.utf8");

	g_test_init (&argc, &argv, NULL);

	dbus_conn = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, &error);
	g_assert_no_error (error);

	/* add test cases */
	for (i = 0; i < G_N_ELEMENTS (tests); i++) {
		gchar *testpath;

		testpath = g_strconcat ("/libtracker-data/", tests[i].test_name, NULL);
		g_test_add (testpath, TestInfo, &tests[i], setup, test_sparql_query, teardown);
		g_free (testpath);
	}

	/* run tests */
	result = g_test_run ();

	return result;
}
