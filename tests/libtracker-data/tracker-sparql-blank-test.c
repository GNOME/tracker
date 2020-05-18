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
#include <glib/gstdio.h>
#include <gio/gio.h>

#include <libtracker-data/tracker-data.h>

static gchar *tests_data_dir = NULL;

typedef struct {
	void *user_data;
	gchar *data_location;
} TestInfo;

static void
test_blank (TestInfo      *info,
            gconstpointer  context)
{
	GError *error;
	GVariant *updates;
	GVariantIter iter;
	GVariant *rows;
	guint len = 0;
	gchar *solutions[3][3];
	GFile *data_location;
	TrackerDataManager *manager;

	error = NULL;

	data_location = g_file_new_for_path (info->data_location);

	/* initialization */
	manager = tracker_data_manager_new (TRACKER_DB_MANAGER_FLAGS_NONE,
	                                    data_location, data_location, /* loc, domain and ontology_name */
	                                    100, 100);
	g_initable_init (G_INITABLE (manager), NULL, &error);
	g_assert_no_error (error);

	/* perform update in transaction */

	updates = tracker_data_update_sparql_blank (tracker_data_manager_get_data (manager),
	                                            "INSERT { _:foo a rdfs:Resource } "
	                                            "INSERT { _:foo a rdfs:Resource . _:bar a rdfs:Resource } ",
	                                            &error);
	g_assert_no_error (error);
	g_assert (updates != NULL);

	g_variant_iter_init (&iter, updates);
	while ((rows = g_variant_iter_next_value (&iter))) {
		GVariantIter sub_iter;
		GVariant *sub_value;

		g_variant_iter_init (&sub_iter, rows);

		while ((sub_value = g_variant_iter_next_value (&sub_iter))) {
			gchar *a = NULL, *b = NULL;
			GVariantIter sub_sub_iter;
			GVariant *sub_sub_value;

			g_variant_iter_init (&sub_sub_iter, sub_value);

			while ((sub_sub_value = g_variant_iter_next_value (&sub_sub_iter))) {
				g_variant_get (sub_sub_value, "{ss}", &a, &b);
				solutions[len][0] = a;
				solutions[len][1] = b;
				len++;
				g_assert_cmpint (len, <=, 3);
				g_variant_unref (sub_sub_value);
			}
			g_variant_unref (sub_value);
		}
		g_variant_unref (rows);
	}

	g_assert_cmpint (len, ==, 3);

	g_assert_cmpstr (solutions[0][0], ==, "foo");
	g_assert (solutions[0][1] != NULL);

	g_assert_cmpstr (solutions[1][0], ==, "foo");
	g_assert (solutions[1][1] != NULL);

	g_assert_cmpstr (solutions[0][1], ==, solutions[1][1]);

	g_assert_cmpstr (solutions[2][0], ==, "bar");
	g_assert (solutions[2][1] != NULL);

	g_assert_cmpstr (solutions[2][1], !=, solutions[1][1]);

	/* cleanup */

	g_free (solutions[0][0]);
	g_free (solutions[0][1]);
	g_free (solutions[1][0]);
	g_free (solutions[1][1]);
	g_free (solutions[2][0]);
	g_free (solutions[2][1]);

	g_variant_unref (updates);
	g_object_unref (data_location);
	g_object_unref (manager);
}

static void
setup (TestInfo      *info,
       gconstpointer  context)
{
	gchar *basename;

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

	setlocale (LC_COLLATE, "en_US.utf8");

	current_dir = g_get_current_dir ();
	tests_data_dir = g_build_path (G_DIR_SEPARATOR_S, current_dir, "sparql-blank-test-data-XXXXXX", NULL);
	g_free (current_dir);

	g_mkdtemp (tests_data_dir);

	g_test_init (&argc, &argv, NULL);

	g_test_add ("/libtracker-data/sparql-blank", TestInfo, NULL, setup, test_blank, teardown);

	/* run tests */
	result = g_test_run ();

	g_assert_cmpint (g_remove (tests_data_dir), ==, 0);
	g_free (tests_data_dir);

	return result;
}
