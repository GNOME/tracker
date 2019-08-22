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

#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <locale.h>

#include <glib.h>
#include <glib/gstdio.h>
#include <gio/gio.h>

#include <libtracker-common/tracker-file-utils.h>
#include <libtracker-common/tracker-locale.h>

#include <tracker-test-helpers.h>

#define TEST_FILENAME "./file-utils-test.txt"
#define TEST_HIDDEN_FILENAME "./.hidden-file.txt"

static void
ensure_file_exists (const gchar *filename)
{
        if (!g_file_test (filename, G_FILE_TEST_EXISTS)) {
                g_file_set_contents (filename, "Just some stuff", -1, NULL);
        }
}

static void
remove_file (const gchar *filename)
{
        g_assert (g_file_test (filename, G_FILE_TEST_EXISTS));
        g_assert_cmpint (g_remove (filename), ==, 0);
}

static GSList *
array_as_list (const gchar **array)
{
	gint i;
	GSList *result = NULL;

	for (i = 0; array[i] != NULL; i++) {
		result = g_slist_prepend (result, g_strdup(array[i]));

	}

	return result;
}

static gboolean
string_in_list (GSList *list, const gchar *string)
{
	GSList *it;
	for ( it = list; it != NULL; it = it->next) {
		if (strcmp (it->data, string) == 0) {
			return TRUE;
		}
	}
	return FALSE;
}

static void
test_path_list_filter_duplicates (void)
{
	const gchar *input_roots [] = {"/home/ivan",
                                       "/home",
                                       "/tmp",
                                       "/usr/",
                                       "/usr/share/local", NULL};

	GSList *input_as_list = NULL;
	GSList *result;

	input_as_list = array_as_list (input_roots);

	result = tracker_path_list_filter_duplicates (input_as_list, ".", TRUE);
	g_assert_cmpint (3, ==, g_slist_length (result));

	g_assert (string_in_list (result, "/home"));
	g_assert (string_in_list (result, "/tmp"));
	g_assert (string_in_list (result, "/usr"));

	g_slist_foreach (input_as_list, (GFunc) g_free, NULL);
	g_slist_foreach (result, (GFunc) g_free, NULL);
}

static void
test_path_list_filter_duplicates_with_exceptions ()
{
        const gchar *input_roots [] = { "/home/user/MyDocs",
                                        "/home/user/MyDocs/.sounds",
                                        "/home/user/MyDocs/visible",
                                        NULL};
        GSList *input_as_list = NULL, *result = NULL;

        input_as_list = array_as_list (input_roots);

        result = tracker_path_list_filter_duplicates (input_as_list, "/home/user/MyDocs", FALSE);
        g_assert_cmpint (g_slist_length (result), ==, 3);
	g_assert (string_in_list (result, "/home/user/MyDocs"));
	g_assert (string_in_list (result, "/home/user/MyDocs/.sounds"));
	g_assert (string_in_list (result, "/home/user/MyDocs/visible"));
	g_slist_foreach (result, (GFunc) g_free, NULL);


        result = tracker_path_list_filter_duplicates (input_as_list, "/home/user/MyDocs", TRUE);
        g_assert_cmpint (g_slist_length (result), ==, 1);
	g_assert (string_in_list (result, "/home/user/MyDocs"));
	g_slist_foreach (result, (GFunc) g_free, NULL);

	g_slist_foreach (input_as_list, (GFunc) g_free, NULL);
}

static void
test_path_evaluate_name (void)
{
	gchar *result, *expected, *pwd, *home;


	const gchar *test = "/one/two";
	gchar *parent_dir;

	home = g_strdup (g_getenv ("HOME"));
	pwd = g_get_current_dir ();
	g_setenv ("TEST_TRACKER_DIR", test, TRUE);


	result = tracker_path_evaluate_name ("/home/user/all/ok");
	g_assert_cmpstr (result, ==, "/home/user/all/ok");
	g_free (result);

	/* The result of this test and the next one are not consistent!
	 * Must it remove the end '/' or not?
	 */
	result = tracker_path_evaluate_name ("/home/user/all/dir/");
	g_assert_cmpstr (result, ==, "/home/user/all/dir");
	g_free (result);


	/*
	 * TODO: In valgrind this test shows a memory leak
	 */
	result = tracker_path_evaluate_name ("~/all/dir/");
	expected = g_build_path (G_DIR_SEPARATOR_S, home, "/all/dir/", NULL);
	g_assert_cmpstr (result, ==, expected);
	g_free (result);
	g_free (expected);

	result = tracker_path_evaluate_name ("just-a-filename");
	g_assert_cmpstr (result, ==, "just-a-filename");
	g_free (result);

	result = tracker_path_evaluate_name ("$HOME/all/dir/");
	expected = g_build_path (G_DIR_SEPARATOR_S, home, "/all/dir", NULL);
	g_assert_cmpstr (result, ==, expected);
	g_free (result);
	g_free (expected);

	result = tracker_path_evaluate_name ("${HOME}/all/dir/");
	expected = g_build_path (G_DIR_SEPARATOR_S, home, "/all/dir", NULL);
	g_assert_cmpstr (result, ==, expected);
	g_free (result);
	g_free (expected);

	result = tracker_path_evaluate_name ("./test/current/dir");
	expected = g_build_path (G_DIR_SEPARATOR_S, pwd, "/test/current/dir", NULL);
	g_assert_cmpstr (result, ==, expected);
	g_free (result);
	g_free (expected);

	result = tracker_path_evaluate_name ("$TEST_TRACKER_DIR/test/dir");
	expected = g_build_path (G_DIR_SEPARATOR_S, test, "/test/dir", NULL);
	g_assert_cmpstr (result, ==, expected);
	g_free (result);
	g_free (expected);

	result = tracker_path_evaluate_name ("../test/dir");
	parent_dir = g_path_get_dirname (pwd);
	expected = g_build_path (G_DIR_SEPARATOR_S, parent_dir, "/test/dir", NULL);
	g_assert_cmpstr (result, ==, expected);
	g_free (result);
	g_free (parent_dir);
	g_free (expected);

	result = tracker_path_evaluate_name ("");
	g_assert (!result);
	g_free (result);

	result = tracker_path_evaluate_name (NULL);
	g_assert (!result);
	g_free (result);

        g_setenv ("HOME", "", TRUE);
        result = tracker_path_evaluate_name ("~/but-no-home.txt");
        g_assert (!result);
        g_free (result);
        g_setenv ("HOME", home, TRUE);

        result = tracker_path_evaluate_name ("$UNDEFINED/something");
        g_assert_cmpstr (result, ==, "/something");
        g_free (result);

	result = tracker_path_evaluate_name (tracker_test_helpers_get_nonutf8 ());
	g_assert_cmpstr (result, ==, tracker_test_helpers_get_nonutf8 ());
	g_free (result);

	g_free (home);
	g_free (pwd);
	g_unsetenv ("TEST_TRACKER_DIR");
}


static void
test_file_get_mime_type (void)
{
	gchar *result;
	GFile *f;

        f = g_file_new_for_path (TEST_FILENAME);
        result = tracker_file_get_mime_type (f);
        g_assert_true (g_strcmp0 (result, "text/plain") == 0 ||
                       g_strcmp0 (result, "application/x-zerosize") == 0);

        g_object_unref (f);
        g_free (result);

        f = g_file_new_for_path ("./file-does-NOT-exist");
        result = tracker_file_get_mime_type (f);
        g_assert_cmpstr (result, ==, "unknown");

        g_object_unref (f);
        g_free (result);

}

#define assert_filename_match(a, b) { \
	g_assert_cmpint (tracker_filename_casecmp_without_extension (a, b), ==, TRUE); \
	g_assert_cmpint (tracker_filename_casecmp_without_extension (b, a), ==, TRUE); }

#define assert_no_filename_match(a, b) { \
	g_assert_cmpint (tracker_filename_casecmp_without_extension (a, b), ==, FALSE); \
	g_assert_cmpint (tracker_filename_casecmp_without_extension (b, a), ==, FALSE); }

static void
test_case_match_filename_without_extension ()
{
	assert_filename_match ("test.mp3", "test.mp3");
	assert_filename_match ("test.mp3", "test.wav");
	assert_filename_match ("test.mp3", "test.mp");
	assert_filename_match ("test.mp3", "test.");
	assert_filename_match ("test.mp3", "test");
	assert_filename_match ("01 - Song 1 (Remix).wav", "01 - Song 1 (Remix).flac");

	assert_no_filename_match ("test.mp3", "bacon.mp3");

	/* Pathological cases, mainly testing that nothing crashes */
	assert_no_filename_match (".", "\n");
	assert_no_filename_match ("as", "as..");
	assert_no_filename_match ("...as", "...as..");
	assert_no_filename_match (".", "test.");
	assert_filename_match ("", ".");
}

static void
test_file_utils_open_close ()
{
        FILE *f;

        f = tracker_file_open (TEST_FILENAME);
        g_assert (f);
        tracker_file_close (f, TRUE);

        f = tracker_file_open (TEST_FILENAME);
        g_assert (f);
        tracker_file_close (f, FALSE);

        f = tracker_file_open ("./file-does-NOT-exist");
        g_assert (!f);
}

static void
test_file_utils_get_size ()
{
        goffset size;
        struct stat st;

        size = tracker_file_get_size (TEST_FILENAME);
        g_assert_cmpint (size, >, 0);

        g_assert_cmpint (stat (TEST_FILENAME, &st), ==, 0);
        g_assert_cmpint (size, ==, st.st_size);

        /* File doesn't exist */
        size = tracker_file_get_size ("./file-does-NOT-exist");
        g_assert_cmpint (size, ==, 0);
}

static void
test_file_utils_get_mtime ()
{
        guint64 mtime;
        struct stat st;
        gchar *pwd, *uri;

        mtime = tracker_file_get_mtime (TEST_FILENAME);
        g_assert_cmpint (mtime, >, 0);

        g_assert_cmpint (stat (TEST_FILENAME, &st), ==, 0);
        // This comparison could lead a problem in 32/64 bits?
        g_assert_cmpint (mtime, ==, st.st_mtime);

        pwd = g_get_current_dir ();
        uri = g_strdup_printf ("file://%s/%s", pwd, TEST_FILENAME);
        mtime = tracker_file_get_mtime_uri (uri);
        // This comparison could lead a problem in 32/64 bits?
        g_assert_cmpint (mtime, ==, st.st_mtime);

        g_free (pwd);
        g_free (uri);

        mtime = tracker_file_get_mtime_uri ("./file-does-NOT-exist");
        g_assert_cmpint (mtime, ==, 0);
}

static void
test_file_system_get_remaining_space ()
{
        guint64 space;

        space = tracker_file_system_get_remaining_space ("/home");
        g_assert_cmpint (space, >, 0);

        // This is a critical (aborts the process)
        //space = tracker_file_system_get_remaining_space ("/unlikely/to/have/this/folder");
}

static void
test_file_system_get_remaining_space_percentage ()
{
        gdouble space;

        space = tracker_file_system_get_remaining_space_percentage ("/home");
        g_assert_cmpfloat (space, >=, 0);
        g_assert_cmpfloat (space, <=, 100);

        // This is a critical (aborts the process)
        //space = tracker_file_system_get_remaining_space_percentage ("/unlikely/to/have/this/folder");
}

static void
test_file_system_has_enough_space ()
{
        /* Hopefully we will always have 1 byte free... */
        g_assert (tracker_file_system_has_enough_space ("/home", 1, FALSE));
        g_assert (tracker_file_system_has_enough_space ("/home", 1, TRUE));

        /* gulong goes only up to 4Gb. Cannot ask for unreasonable amount of space */
        //g_assert (!tracker_file_system_has_enough_space ("/home", G_MAXULONG, FALSE));
}

static void
test_file_utils_is_hidden ()
{
        GFile *f;

        ensure_file_exists ("./non-hidden-test-file");

        f = g_file_new_for_path (TEST_HIDDEN_FILENAME);
        g_assert (tracker_file_is_hidden (f));
        g_object_unref (f);

        f = g_file_new_for_path ("./non-hidden-test-file");
        g_assert (!tracker_file_is_hidden (f));
        g_object_unref (f);

        remove_file ("./non-hidden-test-file");
}

static void
test_file_utils_cmp ()
{
        GFile *one, *two, *three;

        one = g_file_new_for_path (TEST_FILENAME);
        two = g_file_new_for_path (TEST_FILENAME);
        three = g_file_new_for_path (TEST_HIDDEN_FILENAME);

        g_assert (!tracker_file_cmp (one, two));
        g_assert (tracker_file_cmp (two, three));
}

int
main (int argc, char **argv)
{
	int result;

	g_test_init (&argc, &argv, NULL);

	setlocale (LC_ALL, "");

        ensure_file_exists (TEST_FILENAME);
        ensure_file_exists (TEST_HIDDEN_FILENAME);

	g_test_add_func ("/libtracker-common/file-utils/path_evaluate_name",
	                 test_path_evaluate_name);
	g_test_add_func ("/libtracker-common/file-utils/path_list_filter_duplicates",
	                 test_path_list_filter_duplicates);
	g_test_add_func ("/libtracker-common/file-utils/path_list_filter_duplicates_with_exceptions",
	                 test_path_list_filter_duplicates_with_exceptions);
	g_test_add_func ("/libtracker-common/file-utils/file_get_mime_type",
	                 test_file_get_mime_type);
	g_test_add_func ("/libtracker-common/file-utils/case_match_filename_without_extension",
	                 test_case_match_filename_without_extension);

        g_test_add_func ("/libtracker-common/file-utils/open_close",
                         test_file_utils_open_close);
        g_test_add_func ("/libtracker-common/file-utils/get_size",
                         test_file_utils_get_size);
        g_test_add_func ("/libtracker-common/file-utils/get_mtime",
                         test_file_utils_get_mtime);
        g_test_add_func ("/libtracker-common/file-utils/get_remaining_space",
                         test_file_system_get_remaining_space);
        g_test_add_func ("/libtracker-common/file-utils/get_remaining_space_percentage",
                         test_file_system_get_remaining_space_percentage);
        g_test_add_func ("/libtracker-common/file-utils/has_enough_space",
                         test_file_system_has_enough_space);
        g_test_add_func ("/libtracker-common/file-utils/is_hidden",
                         test_file_utils_is_hidden);
        g_test_add_func ("/libtracker-common/file-utils/cmp",
                         test_file_utils_cmp);

	result = g_test_run ();

        remove_file (TEST_FILENAME);
        remove_file (TEST_HIDDEN_FILENAME);

	return result;
}
