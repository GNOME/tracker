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

#include <time.h>
#include <string.h>

#include <glib-object.h>

#include <libtracker-common/tracker-type-utils.h>

static void
test_string_in_string_list_failures_subprocess (void)
{
	const gchar *complete = "This is an extract of text with different terms an props like Audio:Title ...";
	gchar **pieces;

	pieces = g_strsplit (complete, " ", -1);

	g_assert_cmpint (tracker_string_in_string_list (NULL, pieces), ==, -1);

	g_strfreev (pieces);
}

static void
test_string_in_string_list_failures (void)
{

	g_test_trap_subprocess ("/libtracker-common/tracker-type-utils/string_in_string_list_failures/subprocess", 0, 0);
	g_test_trap_assert_failed ();
	g_test_trap_assert_stderr ("*assertion 'str != NULL' failed*");
}

static void
test_string_in_string_list (void)
{
	const gchar *complete = "This is an extract of text with different terms an props like Audio:Title ...";
	gchar **pieces;

	pieces = g_strsplit (complete, " ", -1);

	g_assert_cmpint (tracker_string_in_string_list ("is", pieces), ==, 1);
	g_assert_cmpint (tracker_string_in_string_list ("Audio:Title", pieces), ==, 12);
	g_strfreev (pieces);

	g_assert_cmpint (tracker_string_in_string_list ("terms", NULL), ==, -1);
}

static void
test_string_in_gslist (void)
{
        GSList *input = NULL;

        input = g_slist_prepend (input, g_strdup ("one"));
        input = g_slist_prepend (input, g_strdup ("two"));
        input = g_slist_prepend (input, g_strdup ("three"));
        input = g_slist_prepend (input, g_strdup ("four"));

        g_assert (tracker_string_in_gslist ("one", input));
        g_assert (tracker_string_in_gslist ("two", input));
        g_assert (tracker_string_in_gslist ("three", input));
        g_assert (tracker_string_in_gslist ("four", input));
        g_assert (!tracker_string_in_gslist ("five", input));

        g_slist_foreach (input, (GFunc)g_free, NULL);
        g_slist_free (input);
}

static void
test_gslist_to_string_list (void)
{
	GSList *input = NULL;
	gchar **result;

	input = g_slist_prepend (input, (gpointer) "four");
	input = g_slist_prepend (input, (gpointer) "three");
	input = g_slist_prepend (input, (gpointer) "two");
	input = g_slist_prepend (input, (gpointer) "one");

	result = tracker_gslist_to_string_list (input);

	g_assert_cmpstr (result[0], ==, "one");
	g_assert_cmpstr (result[1], ==, "two");
        g_assert_cmpstr (result[2], ==, "three");
	g_assert_cmpstr (result[3], ==, "four");

	g_strfreev (result);

	result = tracker_gslist_to_string_list (NULL);
	g_assert (result != NULL);
	g_strfreev (result);
}

static void
test_string_list_to_gslist (void)
{
        const gchar  *input [] = {"one", "two", "three", "four", NULL};
        GSList *result = NULL;

        result = tracker_string_list_to_gslist ((gchar **)input, -1);
        g_assert (result);
        g_assert_cmpint (g_slist_length (result), ==, 4);

        /* This function is tested in other test, so it should work or fail there also */
        g_assert (tracker_string_in_gslist ("one", result));
        g_assert (tracker_string_in_gslist ("two", result));
        g_assert (tracker_string_in_gslist ("three", result));
        g_assert (tracker_string_in_gslist ("four", result));

        g_slist_foreach (result, (GFunc)g_free, NULL);
        g_slist_free (result);

        result = tracker_string_list_to_gslist ((gchar **)input, 2);
        g_assert (result);
        g_assert_cmpint (g_slist_length (result), ==, 2);

        g_assert (tracker_string_in_gslist ("one", result));
        g_assert (tracker_string_in_gslist ("two", result));
        g_assert (!tracker_string_in_gslist ("three", result));
        g_assert (!tracker_string_in_gslist ("four", result));
}

static void 
test_string_to_string_list (void)
{
        const gchar *input = "first line";
        gchar **result;

        result = tracker_string_to_string_list (input);
        g_assert_cmpint (g_strv_length (result), ==, 1);
        g_assert_cmpstr (result [0], ==, "first line");
        g_strfreev (result);
}

static void
test_gslist_with_string_data_equal (void)
{
        GSList *list1 = NULL;
        GSList *list2 = NULL;
        GSList *shorty = NULL;
        GSList *list3 = NULL;

        list1 = g_slist_prepend (list1, g_strdup ("one"));
        list1 = g_slist_prepend (list1, g_strdup ("two"));
        list1 = g_slist_prepend (list1, g_strdup ("three"));

        g_assert (tracker_gslist_with_string_data_equal (list1, list1));

        shorty = g_slist_prepend (shorty, g_strdup ("one"));
        g_assert (!tracker_gslist_with_string_data_equal (list1, shorty));
        g_assert (!tracker_gslist_with_string_data_equal (shorty, list1));

        list2 = g_slist_prepend (list2, g_strdup ("one"));
        list2 = g_slist_prepend (list2, g_strdup ("two"));
        list2 = g_slist_prepend (list2, g_strdup ("three"));
        g_assert (tracker_gslist_with_string_data_equal (list1, list2));
        g_assert (tracker_gslist_with_string_data_equal (list2, list1));

        list3 = g_slist_prepend (list3, g_strdup ("one"));
        list3 = g_slist_prepend (list3, g_strdup ("something different"));
        list3 = g_slist_prepend (list3, g_strdup ("three"));
        g_assert (!tracker_gslist_with_string_data_equal (list1, list3));
        g_assert (!tracker_gslist_with_string_data_equal (list3, list1));

        g_slist_foreach (list1, (GFunc)g_free, NULL);
        g_slist_foreach (list2, (GFunc)g_free, NULL);
        g_slist_foreach (shorty, (GFunc)g_free, NULL);
        g_slist_foreach (list3, (GFunc)g_free, NULL);

        g_slist_free (list1);
        g_slist_free (list2);
        g_slist_free (shorty);
        g_slist_free (list3);
}

int
main (int argc, char **argv)
{
	gint result;

	g_test_init (&argc, &argv, NULL);

	g_test_add_func ("/libtracker-common/tracker-type-utils/string_list_as_list",
	                 test_string_to_string_list);
	g_test_add_func ("/libtracker-common/tracker-type-utils/gslist_to_string_list",
	                 test_gslist_to_string_list);
	g_test_add_func ("/libtracker-common/tracker-type-utils/string_in_string_list",
	                 test_string_in_string_list);
	g_test_add_func ("/libtracker-common/tracker-type-utils/string_in_string_list_failures",
	                 test_string_in_string_list_failures);
	g_test_add_func ("/libtracker-common/tracker-type-utils/string_in_string_list_failures/subprocess",
	                 test_string_in_string_list_failures_subprocess);
        g_test_add_func ("/libtracker-common/tracker-type-utils/string_in_gslist",
                         test_string_in_gslist);
        g_test_add_func ("/libtracker-common/tracker-type-utils/string_list_to_gslist",
                         test_string_list_to_gslist);
	g_test_add_func ("/libtracker-common/tracker-type-utils/gslist_with_string_data_equal",
	                 test_gslist_with_string_data_equal);
	result = g_test_run ();

	return result;
}
