/*
 * Copyright (C) 2009, Nokia <ivan.frade@nokia.com>
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

#include <glib.h>
#include <gio/gio.h>
#include <glib/gstdio.h>

#include <libtracker-data/tracker-data.h>

static gchar *tests_data_dir = NULL;

typedef struct _TestInfo TestInfo;

struct _TestInfo {
	const gchar *test_name;
	const gchar *data;
	gchar *data_location;
};

typedef struct _ChangeInfo ChangeInfo;

struct _ChangeInfo {
	const gchar *ontology;
	const gchar *update;
	const gchar *test_name;
	const gchar *ptr;
};

const TestInfo all_other_tests[] = {
	{ "init", NULL },
	{ NULL }
};

const TestInfo nie_tests[] = {
	{ "nie/filter-subject-1", "nie/data-1" },
	{ "nie/filter-characterset-1", "nie/data-1" },
	{ "nie/filter-comment-1", "nie/data-1" },
	{ "nie/filter-description-1", "nie/data-1" },
	{ "nie/filter-generator-1", "nie/data-1" },
	{ "nie/filter-identifier-1", "nie/data-1" },
	{ "nie/filter-keyword-1", "nie/data-1" },
	{ "nie/filter-language-1", "nie/data-1" },
	{ "nie/filter-legal-1", "nie/data-1" },
	{ "nie/filter-title-1", "nie/data-1" },
	{ "nie/filter-version-1", "nie/data-1" },
	{ NULL, NULL }
};

static void
query_helper (TrackerDataManager *manager, const gchar *query_filename, const gchar *results_filename)
{
	GError *error = NULL;
	gchar *queries = NULL, *query;
	gchar *results = NULL;
	GString *test_results = NULL;

	g_file_get_contents (query_filename, &queries, NULL, &error);
	g_assert_no_error (error);

	g_file_get_contents (results_filename, &results, NULL, &error);
	g_assert_no_error (error);

	/* perform actual query */

	query = strtok (queries, "~");

	while (query) {
		TrackerDBCursor *cursor;

		cursor = tracker_data_query_sparql_cursor (manager, query, &error);
		g_assert_no_error (error);

		/* compare results with reference output */

		if (!test_results) {
			test_results = g_string_new ("");
		} else {
			g_string_append (test_results, "~\n");
		}

		if (cursor) {
			gint col;

			while (tracker_db_cursor_iter_next (cursor, NULL, &error)) {
				for (col = 0; col < tracker_db_cursor_get_n_columns (cursor); col++) {
					const gchar *str;

					if (col > 0) {
						g_string_append (test_results, "\t");
					}

					str = tracker_db_cursor_get_string (cursor, col, NULL);
					if (str != NULL) {
						/* bound variable */
						g_string_append_printf (test_results, "\"%s\"", str);
					}
				}

				g_string_append (test_results, "\n");
			}

			g_object_unref (cursor);
		}

		query = strtok (NULL, "~");
	}

	if (strcmp (results, test_results->str)) {
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
	g_free (queries);
}

static void
test_ontology_init (TestInfo      *test_info,
                    gconstpointer  context)
{
	TrackerDataManager *manager;
	GError *error = NULL;
	GFile *data_location;

	data_location = g_file_new_for_path (test_info->data_location);

	/* first-time initialization */
	manager = tracker_data_manager_new (TRACKER_DB_MANAGER_FLAGS_NONE,
	                                    data_location, data_location,
	                                    100, 100);
	g_initable_init (G_INITABLE (manager), NULL, &error);
	g_assert_no_error (error);

	g_object_unref (manager);

	/* initialization from existing database */
	manager = tracker_data_manager_new (0, data_location, data_location,
	                                    100, 100);
	g_initable_init (G_INITABLE (manager), NULL, &error);
	g_assert_no_error (error);

	g_object_unref (manager);
	g_object_unref (data_location);
}

static void
test_query (TestInfo      *test_info,
            gconstpointer  context)
{
	GError *error = NULL;
	gchar *data_filename;
	gchar *query_filename;
	gchar *results_filename;
	gchar *prefix, *data_prefix, *test_prefix, *ontology_path;
	GFile *file, *data_location, *ontology_location;
	TrackerDataManager *manager;
	TrackerData *data_update;

	data_location = g_file_new_for_path (test_info->data_location);

	prefix = g_build_path (G_DIR_SEPARATOR_S, TOP_SRCDIR, "tests", "libtracker-data", NULL);
	data_prefix = g_build_filename (prefix, test_info->data, NULL);
	test_prefix = g_build_filename (prefix, test_info->test_name, NULL);
	g_free (prefix);

	ontology_path = g_build_filename (TOP_SRCDIR, "src", "ontologies", "nepomuk", NULL);
	ontology_location = g_file_new_for_path (ontology_path);
	g_free (ontology_path);

	/* initialization */
	manager = tracker_data_manager_new (TRACKER_DB_MANAGER_FLAGS_NONE,
	                                    data_location, ontology_location,
	                                    100, 100);
	g_initable_init (G_INITABLE (manager), NULL, &error);
	g_assert_no_error (error);

	data_update = tracker_data_manager_get_data (manager);

	/* load data set */
	data_filename = g_strconcat (data_prefix, ".ttl", NULL);
	file = g_file_new_for_path (data_filename);
	data_update = tracker_data_manager_get_data (manager);
	tracker_data_begin_transaction (data_update, &error);
	g_assert_no_error (error);
	tracker_data_load_turtle_file (data_update, file, NULL, &error);
	g_assert_no_error (error);
	tracker_data_commit_transaction (data_update, &error);
	g_assert_no_error (error);
	g_object_unref (file);

	query_filename = g_strconcat (test_prefix, ".rq", NULL);
	results_filename = g_strconcat (test_prefix, ".out", NULL);

	g_free (data_prefix);
	g_free (test_prefix);

	query_helper (manager, query_filename, results_filename);

	/* cleanup */

	g_free (data_filename);
	g_free (query_filename);
	g_free (results_filename);

	g_object_unref (ontology_location);
	g_object_unref (data_location);
	g_object_unref (manager);
}

static inline void
setup (TestInfo      *info,
       gconstpointer  context)
{
	const TestInfo *test = context;
	gchar *basename;

	*info = *test;

	/* NOTE: g_test_build_filename() doesn't work env vars G_TEST_* are not defined?? */
	basename = g_strdup_printf ("%d", g_test_rand_int_range (0, G_MAXINT));
	info->data_location = g_build_path (G_DIR_SEPARATOR_S, tests_data_dir, basename, NULL);
	g_free (basename);
}

static void
teardown (TestInfo      *info,
          gconstpointer  context)
{
	gchar *cleanup_command;

	/* clean up */
	cleanup_command = g_strdup_printf ("rm -Rf %s/", info->data_location);
	g_spawn_command_line_sync (cleanup_command, NULL, NULL, NULL, NULL);
	g_free (cleanup_command);

	g_free (info->data_location);
}

int
main (int argc, char **argv)
{
	gchar *current_dir;
	gint result;
	gint i;

	/* Warning warning!!! We need to impose a proper LC_COLLATE here, so
	 * that the expected order in the test results is always the same! */
	setlocale (LC_COLLATE, "en_US.utf8");

	current_dir = g_get_current_dir ();
	tests_data_dir = g_build_filename (current_dir, "ontology-test-data-XXXXXX", NULL);
	g_free (current_dir);

	g_mkdtemp (tests_data_dir);

	g_test_init (&argc, &argv, NULL);

	/* g_test_init() enables verbose logging by default, but Tracker is too
	 * verbose. To make the logs managable, we hide DEBUG and INFO messages
	 * unless TRACKER_TESTS_VERBOSE is set.
	 */
	if (! g_getenv ("TRACKER_TESTS_VERBOSE")) {
		g_log_set_handler ("Tracker", G_LOG_LEVEL_DEBUG | G_LOG_LEVEL_INFO, g_log_default_handler, NULL);
	}

	/* add test cases */
	g_test_add ("/libtracker-data/ontology-init", TestInfo, &all_other_tests[0], setup, test_ontology_init, teardown);

	for (i = 0; nie_tests[i].test_name; i++) {
		gchar *testpath;

		testpath = g_strconcat ("/libtracker-data/nie/", nie_tests[i].test_name, NULL);
		g_test_add (testpath, TestInfo, &nie_tests[i], setup, test_query, teardown);
		g_free (testpath);
	}

	/* run tests */
	result = g_test_run ();

	g_assert_cmpint (g_remove (tests_data_dir), ==, 0);
	g_free (tests_data_dir);

	return result;
}
