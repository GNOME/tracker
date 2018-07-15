/*
 * Copyright (C) 2014, Softathome <contact@softathome.com>
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

#include <libtracker-miner/tracker-miner.h>
/* Normally private */
#include <libtracker-miner/tracker-file-data-provider.h>

static void
test_enumerator_and_provider (void)
{
	TrackerDataProvider *data_provider;
	GFileEnumerator *enumerator;
	GFileInfo *info;
	GFile *url;
	GError *error = NULL;
	gint count = 0;

	data_provider = tracker_file_data_provider_new ();
	g_assert_nonnull (data_provider);

	/* FIXME: Use better tmp data structure */
	url = g_file_new_for_path (g_get_tmp_dir ());
	g_assert_nonnull (url);

	/* fe = g_file_enumerate_children ( */
	/*                                 0, */
	/*                                 NULL, */
	/*                                 &error); */

	/* g_assert_no_error (error); */
	/* g_assert_nonnull (fe); */

	/* enumerator = tracker_file_enumerator_new (fe); */
	/* g_assert_nonnull (enumerator); */

	enumerator = tracker_data_provider_begin (data_provider,
	                                          url,
	                                          G_FILE_ATTRIBUTE_STANDARD_NAME "," \
	                                          G_FILE_ATTRIBUTE_STANDARD_TYPE,
	                                          G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
	                                          NULL,
	                                          &error);
	g_assert_no_error (error);
	g_assert_nonnull (enumerator);

	while ((info = g_file_enumerator_next_file (enumerator, NULL, &error)) != NULL) {
		g_assert_no_error (error);
		count++;
	}

	g_assert_no_error (error);
	g_assert (count > 0);

	g_object_unref (enumerator);
	g_object_unref (data_provider);
}

int
main (int argc, char **argv)
{
	setlocale (LC_ALL, "");

	g_test_init (&argc, &argv, NULL);

	g_test_message ("Testing file enumerator");

	g_test_add_func ("/libtracker-miner/tracker-enumerator-and-provider",
	                 test_enumerator_and_provider);

	return g_test_run ();
}
