/*
 * Copyright (C) 2008, Nokia <ivan.frade@nokia.com>
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

#include <glib/gstdio.h>

#include <libtracker-data/tracker-db-journal.h>

#ifndef DISABLE_JOURNAL

static void
test_init_and_shutdown (void)
{
	GError *error = NULL;
	gboolean result;
	gchar *path;
	GFile *data_location, *child;
	TrackerDBJournal *writer;

	path = g_build_filename (TOP_BUILDDIR, "tests", "libtracker-db", NULL);
	data_location = g_file_new_for_path (path);
	g_free (path);

	child = g_file_get_child (data_location, "tracker-store.journal");
	path = g_file_get_path (child);
	g_unlink (path);
	g_object_unref (child);

	/* check double init/shutdown */
	tracker_db_journal_set_rotating (FALSE, G_MAXSIZE, NULL);
	writer = tracker_db_journal_new (data_location, FALSE, &error);
	g_assert_no_error (error);
	g_assert_nonnull (writer);

	result = tracker_db_journal_free (writer, &error);
	g_assert_no_error (error);
	g_assert (result == TRUE);

	tracker_db_journal_set_rotating (FALSE, G_MAXSIZE, NULL);
	writer = tracker_db_journal_new (data_location, FALSE, &error);
	g_assert_no_error (error);
	g_assert_nonnull (writer);

	result = tracker_db_journal_free (writer, &error);
	g_assert_no_error (error);
	g_assert (result == TRUE);

	g_object_unref (data_location);
	g_free (path);
}

static void
test_write_functions (void)
{
	gchar *path;
	gsize initial_size, actual_size;
	gboolean result;
	GError *error = NULL;
	GFile *data_location, *child;
	TrackerDBJournal *writer;

	path = g_build_filename (TOP_BUILDDIR, "tests", "libtracker-db", NULL);
	data_location = g_file_new_for_path (path);
	g_free (path);

	child = g_file_get_child (data_location, "tracker-store.journal");
	path = g_file_get_path (child);
	g_unlink (path);
	g_object_unref (child);

	tracker_db_journal_set_rotating (FALSE, G_MAXSIZE, NULL);
	writer = tracker_db_journal_new (data_location, FALSE, &error);
	g_object_unref (data_location);
	g_assert_no_error (error);

	/* Size is 8 due to header */
	actual_size = tracker_db_journal_get_size (writer);
	g_assert_cmpint (actual_size, ==, 8);

	/* Check with rollback, nothing is added */
	initial_size = tracker_db_journal_get_size (writer);
	result = tracker_db_journal_start_transaction (writer, time (NULL));
	g_assert_cmpint (result, ==, TRUE);
	result = tracker_db_journal_append_resource (writer, 10, "http://resource");
	g_assert_cmpint (result, ==, TRUE);
	result = tracker_db_journal_append_resource (writer, 11, "http://predicate");
	g_assert_cmpint (result, ==, TRUE);
	result = tracker_db_journal_append_delete_statement (writer, 0, 10, 11, "test");
	g_assert_cmpint (result, ==, TRUE);
	result = tracker_db_journal_rollback_transaction (writer);
	g_assert_cmpint (result, ==, TRUE);
	actual_size = tracker_db_journal_get_size (writer);
	g_assert_cmpint (initial_size, ==, actual_size);

	/* Check with commit, somethign is added */
	result = tracker_db_journal_start_transaction (writer, time (NULL));
	g_assert_cmpint (result, ==, TRUE);
	result = tracker_db_journal_append_resource (writer, 12, "http://resource");
	g_assert_cmpint (result, ==, TRUE);
	result = tracker_db_journal_append_resource (writer, 13, "http://predicate");
	g_assert_cmpint (result, ==, TRUE);
	result = tracker_db_journal_append_resource (writer, 14, "http://resource");
	g_assert_cmpint (result, ==, TRUE);
	result = tracker_db_journal_append_delete_statement_id (writer, 0, 12, 13, 14);
	g_assert_cmpint (result, ==, TRUE);
	result = tracker_db_journal_commit_db_transaction (writer, &error);
	g_assert_no_error (error);
	g_assert_cmpint (result, ==, TRUE);
	actual_size = tracker_db_journal_get_size (writer);
	g_assert_cmpint (initial_size, !=, actual_size);

	/* Test insert statement */
	result = tracker_db_journal_start_transaction (writer, time (NULL));
	g_assert_cmpint (result, ==, TRUE);
	result = tracker_db_journal_append_resource (writer, 15, "http://resource");
	g_assert_cmpint (result, ==, TRUE);
	result = tracker_db_journal_append_resource (writer, 16, "http://predicate");
	g_assert_cmpint (result, ==, TRUE);
	result = tracker_db_journal_append_insert_statement (writer, 0, 15, 16, "test");
	g_assert_cmpint (result, ==, TRUE);
	result = tracker_db_journal_commit_db_transaction (writer, &error);
	g_assert_no_error (error);
	g_assert_cmpint (result, ==, TRUE);

	/* Test insert id */
	result = tracker_db_journal_start_transaction (writer, time (NULL));
	g_assert_cmpint (result, ==, TRUE);
	result = tracker_db_journal_append_resource (writer, 17, "http://resource");
	g_assert_cmpint (result, ==, TRUE);
	result = tracker_db_journal_append_resource (writer, 18, "http://predicate");
	g_assert_cmpint (result, ==, TRUE);
	result = tracker_db_journal_append_resource (writer, 19, "http://resource");
	g_assert_cmpint (result, ==, TRUE);
	result = tracker_db_journal_append_insert_statement_id (writer, 0, 17, 18, 19);
	g_assert_cmpint (result, ==, TRUE);
	result = tracker_db_journal_commit_db_transaction (writer, &error);
	g_assert_no_error (error);
	g_assert_cmpint (result, ==, TRUE);

	/* Test fsync */
	result = tracker_db_journal_fsync (writer);
	g_assert_cmpint (result, ==, TRUE);

	tracker_db_journal_free (writer, &error);
	g_assert_no_error (error);

	g_free (path);
}

static void
test_read_functions (void)
{
	GError *error = NULL;
	gchar *path;
	gboolean result;
	TrackerDBJournalEntryType type;
	gint id, s_id, p_id, o_id;
	const gchar *uri, *str;
	GFile *data_location;
	TrackerDBJournalReader *reader;

	path = g_build_filename (TOP_BUILDDIR, "tests", "libtracker-db", NULL);
	data_location = g_file_new_for_path (path);
	g_free (path);

	/* NOTE: we don't unlink here so we can use the data from the write tests */

	/* Create an iterator */
	reader = tracker_db_journal_reader_new (data_location, &error);
	g_object_unref (data_location);
	g_assert_no_error (error);
	g_assert_nonnull (reader);

	type = tracker_db_journal_reader_get_entry_type (reader);
	g_assert_cmpint (type, ==, TRACKER_DB_JOURNAL_START);

	/* First transaction */
	result = tracker_db_journal_reader_next (reader, &error);
	g_assert_no_error (error);
	g_assert_cmpint (result, ==, TRUE);

	type = tracker_db_journal_reader_get_entry_type (reader);
	g_assert_cmpint (type, ==, TRACKER_DB_JOURNAL_START_TRANSACTION);

	result = tracker_db_journal_reader_next (reader, &error);
	g_assert_no_error (error);
	g_assert_cmpint (result, ==, TRUE);

	type = tracker_db_journal_reader_get_entry_type (reader);
	g_assert_cmpint (type, ==, TRACKER_DB_JOURNAL_RESOURCE);

	result = tracker_db_journal_reader_get_resource (reader, &id, &uri);
	g_assert_cmpint (result, ==, TRUE);
	g_assert_cmpint (id, ==, 12);
	g_assert_cmpstr (uri, ==, "http://resource");

	result = tracker_db_journal_reader_next (reader, &error);
	g_assert_no_error (error);
	g_assert_cmpint (result, ==, TRUE);

	type = tracker_db_journal_reader_get_entry_type (reader);
	g_assert_cmpint (type, ==, TRACKER_DB_JOURNAL_RESOURCE);

	result = tracker_db_journal_reader_get_resource (reader, &id, &uri);
	g_assert_cmpint (result, ==, TRUE);
	g_assert_cmpint (id, ==, 13);
	g_assert_cmpstr (uri, ==, "http://predicate");

	result = tracker_db_journal_reader_next (reader, &error);
	g_assert_no_error (error);
	g_assert_cmpint (result, ==, TRUE);

	type = tracker_db_journal_reader_get_entry_type (reader);
	g_assert_cmpint (type, ==, TRACKER_DB_JOURNAL_RESOURCE);

	result = tracker_db_journal_reader_get_resource (reader, &id, &uri);
	g_assert_cmpint (result, ==, TRUE);
	g_assert_cmpint (id, ==, 14);
	g_assert_cmpstr (uri, ==, "http://resource");

	result = tracker_db_journal_reader_next (reader, &error);
	g_assert_no_error (error);
	g_assert_cmpint (result, ==, TRUE);

	type = tracker_db_journal_reader_get_entry_type (reader);
	g_assert_cmpint (type, ==, TRACKER_DB_JOURNAL_DELETE_STATEMENT_ID);

	result = tracker_db_journal_reader_get_statement_id (reader, NULL, &s_id, &p_id, &o_id);
	g_assert_cmpint (result, ==, TRUE);
	g_assert_cmpint (s_id, ==, 12);
	g_assert_cmpint (p_id, ==, 13);
	g_assert_cmpint (o_id, ==, 14);

	result = tracker_db_journal_reader_next (reader, &error);
	g_assert_no_error (error);
	g_assert_cmpint (result, ==, TRUE);

	type = tracker_db_journal_reader_get_entry_type (reader);
	g_assert_cmpint (type, ==, TRACKER_DB_JOURNAL_END_TRANSACTION);

	/* Second transaction */
	result = tracker_db_journal_reader_next (reader, &error);
	g_assert_no_error (error);
	g_assert_cmpint (result, ==, TRUE);

	type = tracker_db_journal_reader_get_entry_type (reader);
	g_assert_cmpint (type, ==, TRACKER_DB_JOURNAL_START_TRANSACTION);

	result = tracker_db_journal_reader_next (reader, &error);
	g_assert_no_error (error);
	g_assert_cmpint (result, ==, TRUE);

	type = tracker_db_journal_reader_get_entry_type (reader);
	g_assert_cmpint (type, ==, TRACKER_DB_JOURNAL_RESOURCE);

	result = tracker_db_journal_reader_get_resource (reader, &id, &uri);
	g_assert_cmpint (result, ==, TRUE);
	g_assert_cmpint (id, ==, 15);
	g_assert_cmpstr (uri, ==, "http://resource");

	result = tracker_db_journal_reader_next (reader, &error);
	g_assert_no_error (error);
	g_assert_cmpint (result, ==, TRUE);

	type = tracker_db_journal_reader_get_entry_type (reader);
	g_assert_cmpint (type, ==, TRACKER_DB_JOURNAL_RESOURCE);

	result = tracker_db_journal_reader_get_resource (reader, &id, &uri);
	g_assert_cmpint (result, ==, TRUE);
	g_assert_cmpint (id, ==, 16);
	g_assert_cmpstr (uri, ==, "http://predicate");

	result = tracker_db_journal_reader_next (reader, &error);
	g_assert_no_error (error);
	g_assert_cmpint (result, ==, TRUE);

	type = tracker_db_journal_reader_get_entry_type (reader);
	g_assert_cmpint (type, ==, TRACKER_DB_JOURNAL_INSERT_STATEMENT);

	result = tracker_db_journal_reader_get_statement (reader, NULL, &s_id, &p_id, &str);
	g_assert_cmpint (result, ==, TRUE);
	g_assert_cmpint (s_id, ==, 15);
	g_assert_cmpint (p_id, ==, 16);
	g_assert_cmpstr (str, ==, "test");

	result = tracker_db_journal_reader_next (reader, &error);
	g_assert_no_error (error);
	g_assert_cmpint (result, ==, TRUE);

	type = tracker_db_journal_reader_get_entry_type (reader);
	g_assert_cmpint (type, ==, TRACKER_DB_JOURNAL_END_TRANSACTION);

	/* Third transaction */
	result = tracker_db_journal_reader_next (reader, &error);
	g_assert_no_error (error);
	g_assert_cmpint (result, ==, TRUE);

	type = tracker_db_journal_reader_get_entry_type (reader);
	g_assert_cmpint (type, ==, TRACKER_DB_JOURNAL_START_TRANSACTION);

	result = tracker_db_journal_reader_next (reader, &error);
	g_assert_no_error (error);
	g_assert_cmpint (result, ==, TRUE);

	type = tracker_db_journal_reader_get_entry_type (reader);
	g_assert_cmpint (type, ==, TRACKER_DB_JOURNAL_RESOURCE);

	result = tracker_db_journal_reader_get_resource (reader, &id, &uri);
	g_assert_cmpint (result, ==, TRUE);
	g_assert_cmpint (id, ==, 17);
	g_assert_cmpstr (uri, ==, "http://resource");

	result = tracker_db_journal_reader_next (reader, &error);
	g_assert_no_error (error);
	g_assert_cmpint (result, ==, TRUE);

	type = tracker_db_journal_reader_get_entry_type (reader);
	g_assert_cmpint (type, ==, TRACKER_DB_JOURNAL_RESOURCE);

	result = tracker_db_journal_reader_get_resource (reader, &id, &uri);
	g_assert_cmpint (result, ==, TRUE);
	g_assert_cmpint (id, ==, 18);
	g_assert_cmpstr (uri, ==, "http://predicate");

	result = tracker_db_journal_reader_next (reader, &error);
	g_assert_no_error (error);
	g_assert_cmpint (result, ==, TRUE);

	type = tracker_db_journal_reader_get_entry_type (reader);
	g_assert_cmpint (type, ==, TRACKER_DB_JOURNAL_RESOURCE);

	result = tracker_db_journal_reader_get_resource (reader, &id, &uri);
	g_assert_cmpint (result, ==, TRUE);
	g_assert_cmpint (id, ==, 19);
	g_assert_cmpstr (uri, ==, "http://resource");

	result = tracker_db_journal_reader_next (reader, &error);
	g_assert_no_error (error);
	g_assert_cmpint (result, ==, TRUE);

	type = tracker_db_journal_reader_get_entry_type (reader);
	g_assert_cmpint (type, ==, TRACKER_DB_JOURNAL_INSERT_STATEMENT_ID);

	result = tracker_db_journal_reader_get_statement_id (reader, NULL, &s_id, &p_id, &o_id);
	g_assert_cmpint (result, ==, TRUE);
	g_assert_cmpint (s_id, ==, 17);
	g_assert_cmpint (p_id, ==, 18);
	g_assert_cmpint (o_id, ==, 19);

	result = tracker_db_journal_reader_next (reader, &error);
	g_assert_no_error (error);
	g_assert_cmpint (result, ==, TRUE);

	type = tracker_db_journal_reader_get_entry_type (reader);
	g_assert_cmpint (type, ==, TRACKER_DB_JOURNAL_END_TRANSACTION);

	tracker_db_journal_reader_free (reader);
}

#endif /* DISABLE_JOURNAL */

int
main (int argc, char **argv) 
{
	gchar *path;
	int result;

	g_test_init (&argc, &argv, NULL);

#ifndef DISABLE_JOURNAL
	/* None of these tests make sense in case of disabled journal */
	g_test_add_func ("/libtracker-db/tracker-db-journal/write-functions",
	                 test_write_functions);
	g_test_add_func ("/libtracker-db/tracker-db-journal/read-functions",
	                 test_read_functions);
	g_test_add_func ("/libtracker-db/tracker-db-journal/init-and-shutdown",
	                 test_init_and_shutdown);
#endif /* DISABLE_JOURNAL */

	result = g_test_run ();

	/* Clean up */
	path = g_build_filename (TOP_BUILDDIR, "tests", "libtracker-db", "tracker-store.journal", NULL);
	g_unlink (path);
	g_free (path);

	return result;
}
