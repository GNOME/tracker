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

#include <glib/gstdio.h>
#include <gio/gio.h>

#include <libtracker-common/tracker-common.h>
#include <libtracker-data/tracker-data.h>

static gchar *tests_data_dir = NULL;
static gint backup_calls = 0;
static GMainLoop *loop = NULL;

typedef struct _TestInfo TestInfo;

struct _TestInfo {
	const gchar *test_name;
	gboolean use_journal;
	gchar *data_location;
};

const TestInfo tests[] = {
	{ "journal_then_save_and_restore", TRUE },
	{ "save_and_resource", FALSE },
	{ NULL }
};

static void
backup_finished_cb (GError   *error,
                    gpointer  user_data)
{
	g_assert (TRUE);
	backup_calls += 1;

	if (loop != NULL) {
		/* backup callback, quit main loop */
		g_main_loop_quit (loop);
	}
}

static gboolean
check_content_in_db (TrackerDataManager *manager,
                     gint                expected_instances,
                     gint                expected_relations)
{
	GError *error = NULL;
	const gchar  *query_instances_1 = "SELECT ?u WHERE { ?u a foo:class1. }";
	const gchar  *query_relation = "SELECT ?a ?b WHERE { ?a foo:propertyX ?b }";
	TrackerDBCursor *cursor;
	gint n_rows;

	cursor = tracker_data_query_sparql_cursor (manager, query_instances_1, &error);
	g_assert_no_error (error);
	n_rows = 0;
	while (tracker_db_cursor_iter_next (cursor, NULL, &error)) {
		n_rows++;
	}
	g_assert_no_error (error);
	g_assert_cmpint (n_rows, ==, expected_instances);
	g_object_unref (cursor);

	cursor = tracker_data_query_sparql_cursor (manager, query_relation, &error);
	g_assert_no_error (error);
	n_rows = 0;
	while (tracker_db_cursor_iter_next (cursor, NULL, &error)) {
		n_rows++;
	}
	g_assert_no_error (error);
	g_assert_cmpint (n_rows, ==, expected_relations);
	g_object_unref (cursor);

	return TRUE;
}

/*
 * Load ontology a few instances
 * Run a couple of queries to check it is ok
 * Back-up. 
 * Remove the DB.
 * Restore
 * Run again the queries
 */
static void
test_backup_and_restore_helper (const gchar *db_location,
                                gboolean     journal)
{
	gchar  *data_prefix, *data_filename, *backup_location, *backup_filename, *meta_db, *ontologies;
	GError *error = NULL;
	GFile  *backup_file;
	GFile  *data_location, *test_schemas;
	TrackerDataManager *manager;
	TrackerData *data_update;

	data_location = g_file_new_for_path (db_location);

	data_prefix = g_build_path (G_DIR_SEPARATOR_S, 
	                            TOP_SRCDIR, "tests", "libtracker-data", "backup", "backup",
	                            NULL);

	ontologies = g_build_path (G_DIR_SEPARATOR_S,
	                           TOP_SRCDIR, "tests", "libtracker-data", "backup",
	                           NULL);
	test_schemas = g_file_new_for_path (ontologies);

	tracker_db_journal_set_rotating (FALSE, G_MAXSIZE, NULL);

	manager = tracker_data_manager_new (TRACKER_DB_MANAGER_FORCE_REINDEX,
	                                    data_location, data_location, test_schemas,
	                                    FALSE, FALSE, 100, 100);
	g_initable_init (G_INITABLE (manager), NULL, &error);
	g_assert_no_error (error);

	/* load data set */
	data_filename = g_strconcat (data_prefix, ".data", NULL);
	if (g_file_test (data_filename, G_FILE_TEST_IS_REGULAR)) {
		GFile *file = g_file_new_for_path (data_filename);
		data_update = tracker_data_manager_get_data (manager);
		tracker_turtle_reader_load (file, data_update, &error);
		g_assert_no_error (error);
		g_object_unref (file);
	} else {
		g_assert_not_reached ();
	}
	g_free (data_filename);


	/* Check everything is correct */
	check_content_in_db (manager, 3, 1);

	backup_location = g_build_filename (db_location, "backup", NULL);
	g_assert_cmpint (g_mkdir (backup_location, 0777), ==, 0);
	backup_filename = g_build_filename (backup_location, "tracker.dump", NULL);
	backup_file = g_file_new_for_path (backup_filename);
	g_free (backup_filename);
	g_free (backup_location);
	tracker_data_backup_save (manager,
	                          backup_file,
				  data_location,
	                          backup_finished_cb,
	                          NULL,
	                          NULL);

	/* Backup is asynchronous, wait until it is finished */
	loop = g_main_loop_new (NULL, FALSE);
	g_main_loop_run (loop);
	g_main_loop_unref (loop);
	loop = NULL;

	g_object_unref (manager);

	meta_db = g_build_path (G_DIR_SEPARATOR_S, db_location, "meta.db", NULL);
	g_unlink (meta_db);
	g_free (meta_db);

#ifndef DISABLE_JOURNAL
	if (!journal) {
		meta_db = g_build_path (G_DIR_SEPARATOR_S, db_location, "data", "tracker-store.journal", NULL);
		g_unlink (meta_db);
		g_free (meta_db);

		meta_db = g_build_path (G_DIR_SEPARATOR_S, db_location, "data", "tracker-store.ontology.journal", NULL);
		g_unlink (meta_db);
		g_free (meta_db);
	}
#endif /* DISABLE_JOURNAL */

	meta_db = g_build_path (G_DIR_SEPARATOR_S, db_location, "data", ".meta.isrunning", NULL);
	g_unlink (meta_db);
	g_free (meta_db);

#ifndef DISABLE_JOURNAL
	tracker_db_journal_set_rotating (FALSE, G_MAXSIZE, NULL);
#endif /* DISABLE_JOURNAL */

	manager = tracker_data_manager_new (TRACKER_DB_MANAGER_FORCE_REINDEX,
	                                    data_location, data_location, test_schemas,
	                                    FALSE, FALSE, 100, 100);
	g_initable_init (G_INITABLE (manager), NULL, &error);
	g_assert_no_error (error);

	check_content_in_db (manager, 0, 0);

	tracker_data_backup_restore (manager, backup_file, data_location, data_location, test_schemas, NULL, NULL, &error);
	g_assert_no_error (error);
	g_object_unref (manager);

	manager = tracker_data_manager_new (0, data_location, data_location, test_schemas,
	                                    FALSE, FALSE, 100, 100);
	g_initable_init (G_INITABLE (manager), NULL, &error);
	g_assert_no_error (error);
	check_content_in_db (manager, 3, 1);

	g_object_unref (test_schemas);
	g_free (ontologies);

	g_assert_cmpint (backup_calls, ==, 1);

	g_free (data_prefix);
	g_object_unref (data_location);
	g_object_unref (manager);
}

static void
test_backup_and_restore (TestInfo      *info,
                         gconstpointer  context)
{
	test_backup_and_restore_helper (info->data_location, info->use_journal);
	backup_calls = 0;
}

static void
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
	g_print ("Removing temporary data (%s)\n", info->data_location);

	cleanup_command = g_strdup_printf ("rm -Rf %s/", info->data_location);
	g_spawn_command_line_sync (cleanup_command, NULL, NULL, NULL, NULL);
	g_free (cleanup_command);

	g_free (info->data_location);
}

int
main (int argc, char **argv)
{
	gint result;
	gchar *current_dir;

	setlocale (LC_COLLATE, "en_US.utf8");

	current_dir = g_get_current_dir ();
	tests_data_dir = g_build_filename (current_dir, "backup-test-data-XXXXXX", NULL);
	g_free (current_dir);

	g_mkdtemp (tests_data_dir);

	g_test_init (&argc, &argv, NULL);

	g_test_add ("/libtracker-data/backup/journal_then_save_and_restore", TestInfo, &tests[0], setup, test_backup_and_restore, teardown);
	g_test_add ("/libtracker-data/backup/save_and_restore", TestInfo, &tests[1], setup, test_backup_and_restore, teardown);

	result = g_test_run ();

	g_assert_cmpint (g_remove (tests_data_dir), ==, 0);
	g_free (tests_data_dir);

	return result;
}
