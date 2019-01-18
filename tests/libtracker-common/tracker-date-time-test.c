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

#include <glib.h>
#include <glib-object.h>

#include <libtracker-common/tracker-date-time.h>

/* This define was committed in glib 18.07.2011
 * https://bugzilla.gnome.org/show_bug.cgi?id=577231
 */
#ifndef G_VALUE_INIT
#define G_VALUE_INIT { 0, { { 0 } } }
#endif

static void
test_string_to_date_failures_subprocess ()
{
	GError *error = NULL;

	tracker_string_to_date (NULL, NULL, &error);

	if (error) {
		g_warning ("%s", error->message);
		g_error_free (error);
	}
}

static void
test_string_to_date_failures ()
{
	g_test_trap_subprocess ("/libtracker-common/date-time/string_to_date_failures/subprocess", 0, 0);
	g_test_trap_assert_failed ();
	g_test_trap_assert_stderr ("*Empty date string*");
}

static void
test_string_to_date (void)
{
	GDate     *expected;
	GDate     *result;
	time_t     result_time_t;
	const gchar  *input = "2008-06-16T11:10:10+0600";
	gchar  *timezone = g_strdup (g_getenv ("TZ"));
	GError *error = NULL;

	if (! g_setenv ("TZ", "UTC", TRUE)) {
		g_test_message ("unable to set timezone, test results are invalid, skipping\n");
		if (timezone) {
			g_free (timezone);
		}
		return;
	}

	expected = g_date_new_dmy (16, G_DATE_JUNE, 2008);

	result_time_t = tracker_string_to_date (input, NULL, &error);
	g_assert_no_error (error);

	result = g_date_new ();
	g_date_set_time_t (result, result_time_t);

	if (timezone) {
		g_setenv ("TZ", timezone, TRUE);
		g_free (timezone);
	} else {
		g_unsetenv ("TZ");
	}

	g_assert_cmpint (g_date_get_year (expected), ==, g_date_get_year (result));
	g_assert_cmpint (g_date_get_day (expected), ==, g_date_get_day (result));
	g_assert_cmpint (g_date_get_month (expected), ==, g_date_get_month (result));

	g_date_free (expected);
	g_date_free (result);


	result_time_t = tracker_string_to_date ("", NULL, &error);
	g_assert_cmpint (result_time_t, ==, -1);
	g_assert_error (error, TRACKER_DATE_ERROR, TRACKER_DATE_ERROR_INVALID_ISO8601);
	g_error_free (error);
	error = NULL;

	result_time_t = tracker_string_to_date ("i am not a date", NULL, &error);
	g_assert_cmpint (result_time_t, ==, -1);
	g_assert_error (error, TRACKER_DATE_ERROR, TRACKER_DATE_ERROR_INVALID_ISO8601);
	g_error_free (error);
	error = NULL;

	/* Fails! Check the code
	   result_time_t = tracker_string_to_date ("2008-06-32T04:23:10+0000", NULL);
	   g_assert_cmpint (result_time_t, ==, -1);
	*/

        /* More cases of string->date are tested in tracker_date_time_from_string...
         *  it is more convinient to test them there
         */
}

static void
test_date_to_string (void)
{
	struct tm *original;
	time_t     input;
	gchar     *result;

	original = g_new0 (struct tm, 1);
	original->tm_sec = 10;
	original->tm_min = 53;
	original->tm_hour = 23;
	original->tm_mday = 16;
	original->tm_mon = 5;
	original->tm_year = 108;
	original->tm_isdst = 0;

#if !(defined(__FreeBSD__) || defined(__OpenBSD__))
	input = mktime (original) - timezone;
#else
	input = timegm (original);
#endif

	result = tracker_date_to_string (input);

	g_assert (result != NULL && strncmp (result, "2008-06-16T23:53:10Z", 19) == 0);

	g_free (result);
	g_free (original);
}

static void
test_date_time_get_set ()
{
        GValue value = G_VALUE_INIT;
        GValue copy = G_VALUE_INIT;

        g_value_init (&value, TRACKER_TYPE_DATE_TIME);
        g_value_init (&copy, TRACKER_TYPE_DATE_TIME);

        tracker_date_time_set (&value, 123456789, 3600);

        g_assert_cmpint (tracker_date_time_get_time (&value), ==, 123456789);
        g_assert_cmpint (tracker_date_time_get_offset (&value), ==, 3600);

        g_value_copy (&value, &copy);

        g_assert_cmpint (tracker_date_time_get_time (&copy), ==, 123456789);
        g_assert_cmpint (tracker_date_time_get_offset (&copy), ==, 3600);
}

static void
test_date_time_from_string ()
{
        GValue value = G_VALUE_INIT;
        GError *error = NULL;

        g_value_init (&value, TRACKER_TYPE_DATE_TIME);

        tracker_date_time_set_from_string (&value, "2011-10-28T17:43:00+03:00", &error);
        g_assert (!error);
        g_assert_cmpint (tracker_date_time_get_time (&value), ==, 1319812980);
        g_assert_cmpint (tracker_date_time_get_offset (&value), ==, 10800);


        /* Negative offset */
        tracker_date_time_set_from_string (&value, "2011-10-28T17:43:00-03:00", &error);
        g_assert (!error);
        g_assert_cmpint (tracker_date_time_get_time (&value), ==, 1319834580);
        g_assert_cmpint (tracker_date_time_get_offset (&value), ==, -10800);

        /* No offset */
        tracker_date_time_set_from_string (&value, "2011-10-28T17:43:00Z", &error);
        g_assert (!error);
        g_assert_cmpint (tracker_date_time_get_time (&value), ==, 1319823780);
        g_assert_cmpint (tracker_date_time_get_offset (&value), ==, 0);

        /* Invalid format */
        tracker_date_time_set_from_string (&value, "2011-10-28T17:43:00Z0900", &error);
        g_assert (error);
        g_error_free (error);
        error = NULL;

        /* There are no 28 months... */
        tracker_date_time_set_from_string (&value, "2011-28-10T17:43:00Z0900", &error);
        g_assert (error);
        g_error_free (error);
        error = NULL;

        /* ... nor more than +-12 offsets */
        tracker_date_time_set_from_string (&value, "2011-28-10T17:43:00+17:00", &error);
        g_assert (error);
        g_error_free (error);
        error = NULL;

        /* ... the same for the glory of the branch % */
        tracker_date_time_set_from_string (&value, "2011-28-10T17:43:00-17:00", &error);
        g_assert (error);
        g_error_free (error);
        error = NULL;
}

static void
test_date_time_get_local_date ()
{
        GValue value = G_VALUE_INIT;
        GError *error = NULL;

        g_value_init (&value, TRACKER_TYPE_DATE_TIME);

        tracker_date_time_set_from_string (&value, "2011-10-28T17:43:00+03:00", &error);
        g_assert (!error);

        g_assert_cmpint (tracker_date_time_get_local_date (&value), ==, 15275);
}

static void
test_date_time_get_local_time ()
{
        GValue value = G_VALUE_INIT;
        GError *error = NULL;

        g_value_init (&value, TRACKER_TYPE_DATE_TIME);

        tracker_date_time_set_from_string (&value, "2011-10-28T17:43:00+03:00", &error);
        g_assert (!error);

        g_assert_cmpint (tracker_date_time_get_local_time (&value), ==, 63780);
}

gint
main (gint argc, gchar **argv) 
{
        g_test_init (&argc, &argv, NULL);

        g_test_add_func ("/libtracker-common/date-time/date_to_string",
                         test_date_to_string);
        g_test_add_func ("/libtracker-common/date-time/string_to_date",
                         test_string_to_date);
        g_test_add_func ("/libtracker-common/date-time/string_to_date_failures",
                         test_string_to_date_failures);
        g_test_add_func ("/libtracker-common/date-time/string_to_date_failures/subprocess",
                         test_string_to_date_failures_subprocess);
        g_test_add_func ("/libtracker-common/date-time/get_set",
                         test_date_time_get_set);
        g_test_add_func ("/libtracker-common/date-time/from_string",
                         test_date_time_from_string);
        g_test_add_func ("/libtracker-common/date-time/get_local_date",
                         test_date_time_get_local_date);
        g_test_add_func ("/libtracker-common/date-time/get_local_time",
                         test_date_time_get_local_time);

        return g_test_run ();
}
