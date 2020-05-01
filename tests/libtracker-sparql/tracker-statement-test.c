/*
 * Copyright (C) 2020, Red Hat Ltd.
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

#include <stdlib.h>
#include <string.h>
#include <libtracker-sparql/tracker-sparql.h>

typedef struct {
	const gchar *test_name;
	const gchar *query_file;
	const gchar *output_file;
	const gchar *arg1;
	const gchar *arg2;
	const gchar *arg3;
	TrackerSparqlConnection *conn;
} TestInfo;

TestInfo tests[] = {
	{ "simple", "statement/simple.rq", "statement/simple.out", "hello" },
	{ "simple-error", "statement/simple-error.rq" },
	{ "object", "statement/object.rq", "statement/object.out", "Music album" },
	{ "object-iri", "statement/object-iri.rq", "statement/object-iri.out", "http://tracker.api.gnome.org/ontology/v3/nfo#MediaList" },
	{ "subject", "statement/subject.rq", "statement/subject.out", "http://tracker.api.gnome.org/ontology/v3/nmm#MusicAlbum" },
	{ "subject-2", "statement/subject.rq", "statement/subject-2.out", "urn:nonexistent" },
	{ "filter", "statement/filter.rq", "statement/filter.out", "http://tracker.api.gnome.org/ontology/v3/nmm#MusicAlbum", "Music album" },
};

typedef struct {
	TrackerSparqlConnection *direct;
	GDBusConnection *dbus_conn;
} StartupData;

static gboolean started = FALSE;

static void
check_result (TrackerSparqlCursor *cursor,
              const gchar         *results_filename)
{
	GString *test_results;
	gchar *results;
	GError *nerror = NULL;
	GError *error = NULL;
	gint col;

	g_file_get_contents (results_filename, &results, NULL, &nerror);
	g_assert_no_error (nerror);
	g_clear_error (&nerror);

	/* compare results with reference output */

	test_results = g_string_new ("");

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
			if (g_strcmp0 (str, TRACKER_PREFIX_TRACKER "modified") == 0 ||
			    g_strcmp0 (str, TRACKER_PREFIX_TRACKER "added") == 0) {
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

static void
setup (TestInfo      *test_info,
       gconstpointer  context)
{
	const TestInfo *test = context;

	*test_info = *test;
}

static void
query_statement (TestInfo      *test_info,
                 gconstpointer  context)
{
	TrackerSparqlStatement *stmt;
	TrackerSparqlCursor *cursor;
	GError *error = NULL;
	gchar *path, *query;

	path = g_build_filename (TOP_SRCDIR, "tests", "libtracker-sparql",
	                         test_info->query_file, NULL);
	g_file_get_contents (path, &query, NULL, &error);
	g_assert_no_error (error);
	g_free (path);

	stmt = tracker_sparql_connection_query_statement (test_info->conn, query,
	                                                  NULL, &error);
	g_free (query);
	g_assert_no_error (error);

	if (test_info->arg1)
		tracker_sparql_statement_bind_string (stmt, "arg1", test_info->arg1);
	if (test_info->arg2)
		tracker_sparql_statement_bind_string (stmt, "arg2", test_info->arg2);
	if (test_info->arg3)
		tracker_sparql_statement_bind_string (stmt, "arg3", test_info->arg3);

	cursor = tracker_sparql_statement_execute (stmt, NULL, &error);

	if (test_info->output_file) {
		g_assert_no_error (error);

		path = g_build_filename (TOP_SRCDIR, "tests", "libtracker-sparql",
		                         test_info->output_file, NULL);
		check_result (cursor, path);
		g_free (path);
	} else {
		g_assert_nonnull (error);
	}
}

TrackerSparqlConnection *
create_local_connection (GError **error)
{
        TrackerSparqlConnection *conn;
        GFile *ontology;

        ontology = g_file_new_for_path (TEST_ONTOLOGIES_DIR);

        conn = tracker_sparql_connection_new (0, NULL, ontology, NULL, error);
        g_object_unref (ontology);

        return conn;
}

static gpointer
thread_func (gpointer user_data)
{
	StartupData *data = user_data;;
	TrackerEndpointDBus *endpoint;
	GMainContext *context;
	GMainLoop *main_loop;

	context = g_main_context_new ();
	g_main_context_push_thread_default (context);

	main_loop = g_main_loop_new (context, FALSE);

	endpoint = tracker_endpoint_dbus_new (data->direct, data->dbus_conn, NULL, NULL, NULL);
	if (!endpoint)
		return NULL;

	started = TRUE;
	g_main_loop_run (main_loop);

	return NULL;
}

static gboolean
create_connections (TrackerSparqlConnection **dbus,
                    TrackerSparqlConnection **direct,
                    GError                  **error)
{
	StartupData data;

	data.direct = create_local_connection (NULL);
	if (!data.direct)
		return FALSE;
	data.dbus_conn = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, error);
	if (!data.dbus_conn)
		return FALSE;

	g_thread_new (NULL, thread_func, &data);

	while (!started)
		g_usleep (100);

	*dbus = tracker_sparql_connection_bus_new (g_dbus_connection_get_unique_name (data.dbus_conn),
	                                           NULL, data.dbus_conn, error);
	*direct = data.direct;

	return TRUE;
}

static void
add_tests (TrackerSparqlConnection *conn,
           const gchar             *name)
{
	gint i;

	for (i = 0; i < G_N_ELEMENTS (tests); i++) {
		gchar *testpath;

		tests[i].conn = conn;
		testpath = g_strconcat ("/libtracker-sparql/statement/", name, "/", tests[i].test_name, NULL);
		g_test_add (testpath, TestInfo, &tests[i], setup, query_statement, NULL);
		g_free (testpath);
	}
}

gint
main (gint argc, gchar **argv)
{
	TrackerSparqlConnection *dbus = NULL, *direct = NULL;
	GError *error = NULL;

	g_test_init (&argc, &argv, NULL);

	g_assert_true (create_connections (&dbus, &direct, &error));
	g_assert_no_error (error);

	add_tests (direct, "direct");
	add_tests (dbus, "dbus");

	return g_test_run ();
}
