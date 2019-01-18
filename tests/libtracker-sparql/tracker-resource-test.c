/*
 * Copyright (C) 2016, Sam Thursfield <sam@afuera.me.uk>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

#include "config.h"

#include <locale.h>

#include <libtracker-sparql/tracker-resource.h>

static void
test_resource_get_empty (void)
{
	TrackerResource *resource;

	resource = tracker_resource_new ("http://example.com/resource");

	g_assert (tracker_resource_get_values (resource, "http://example.com/0") == NULL);

	g_assert (tracker_resource_get_first_double (resource, "http://example.com/0") == 0.0);
	g_assert (tracker_resource_get_first_int (resource, "http://example.com/0") == 0);
	g_assert (tracker_resource_get_first_int64 (resource, "http://example.com/0") == 0);
	g_assert (tracker_resource_get_first_string (resource, "http://example.com/0") == NULL);
	g_assert (tracker_resource_get_first_uri (resource, "http://example.com/0") == NULL);

	g_object_unref (resource);
}

static void
test_resource_get_set_simple (void)
{
	TrackerResource *resource;

	resource = tracker_resource_new ("http://example.com/resource");

	tracker_resource_set_double (resource, "http://example.com/1", 0.6);
	tracker_resource_set_int (resource, "http://example.com/2", 60);
	tracker_resource_set_int64 (resource, "http://example.com/3", 123456789);
	tracker_resource_set_string (resource, "http://example.com/4", "Hello");
	tracker_resource_set_uri (resource, "http://example.com/5", "http://example.com/");

	g_assert (tracker_resource_get_first_double (resource, "http://example.com/1") == 0.6);
	g_assert_cmpint (tracker_resource_get_first_int (resource, "http://example.com/2"), ==, 60);
	g_assert (tracker_resource_get_first_int64 (resource, "http://example.com/3") == 123456789);
	g_assert_cmpstr (tracker_resource_get_first_string (resource, "http://example.com/4"), ==, "Hello");
	g_assert_cmpstr (tracker_resource_get_first_uri (resource, "http://example.com/5"), ==, "http://example.com/");

	g_object_unref (resource);
}

static void
test_resource_get_set_gvalue (void)
{
	TrackerResource *resource;
	GValue value = G_VALUE_INIT;
	GList *list;

	resource = tracker_resource_new ("http://example.com/resource");

	g_value_init (&value, G_TYPE_STRING);
	g_value_set_static_string (&value, "xyzzy");
	tracker_resource_set_gvalue (resource, "http://example.com/0", &value);

	list = tracker_resource_get_values (resource, "http://example.com/0");
	g_assert_cmpint (g_list_length (list), ==, 1);

	g_object_unref (resource);
}

#define RANDOM_GVALUE_TYPE (G_TYPE_HASH_TABLE)

static void
init_gvalue_with_random_type (GValue *value)
{
	/* Hash table is used here as an "unexpected" value type. It makes no sense
	 * to do this in real code, but the code shouldn't crash or assert if
	 * someone does do it.
	 */
	g_value_init (value, G_TYPE_HASH_TABLE);
	g_value_take_boxed (value, g_hash_table_new (NULL, NULL));
}

static void
test_resource_get_set_many (void)
{
	TrackerResource *resource;
	GValue value = G_VALUE_INIT;
	GList *list;

	resource = tracker_resource_new ("http://example.com/resource");

	/* All the add_* functions except for add_gvalue are generated using the
	 * same macro, so we only need to test one or two of them here.
	 */
	tracker_resource_add_int (resource, "http://example.com/0", 60);
	tracker_resource_add_string (resource, "http://example.com/0", "Hello");

	init_gvalue_with_random_type (&value);
	tracker_resource_add_gvalue (resource, "http://example.com/0", &value);
	g_value_unset (&value);

	list = tracker_resource_get_values (resource, "http://example.com/0");
	g_assert_cmpint (g_list_length (list), ==, 3);

	g_assert_cmpint (g_value_get_int (list->data), ==, 60);
	g_assert_cmpstr (g_value_get_string (list->next->data), ==, "Hello");
	g_assert (G_VALUE_HOLDS (list->next->next->data, RANDOM_GVALUE_TYPE));

	g_list_free_full (list, (GDestroyNotify) g_value_unset);

	g_object_unref (resource);
}

static void
test_resource_get_set_pointer_validation (void)
{
	if (g_test_subprocess ()) {
		TrackerResource *resource;

		resource = tracker_resource_new ("http://example.com/resource");

		/* This should trigger a g_warning(), and abort. */
		tracker_resource_set_string (resource, "http://example.com/1", NULL);

		return;
	}

	g_test_trap_subprocess (NULL, 0, 0);
	g_test_trap_assert_failed ();
	g_test_trap_assert_stderr ("*tracker_resource_set_string: NULL is not a valid value.*");
}

int
main (int    argc,
      char **argv)
{
	g_test_init (&argc, &argv, NULL);

	g_test_message ("Testing Tracker resource abstraction");

	g_test_add_func ("/libtracker-sparql/tracker-resource/get_empty",
	                 test_resource_get_empty);
	g_test_add_func ("/libtracker-sparql/tracker-resource/get_set_simple",
	                 test_resource_get_set_simple);
	g_test_add_func ("/libtracker-sparql/tracker-resource/get_set_gvalue",
	                 test_resource_get_set_gvalue);
	g_test_add_func ("/libtracker-sparql/tracker-resource/get_set_many",
	                 test_resource_get_set_many);
	g_test_add_func ("/libtracker-sparql/tracker-resource/get_set_pointer_validation",
	                 test_resource_get_set_pointer_validation);

	return g_test_run ();
}
