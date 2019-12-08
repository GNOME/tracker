/*
 * Copyright (C) 2011 Nokia <ivan.frade@nokia.com>
 *
 * Author: Carlos Garnacho <carlos@lanedo.com>
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
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301  USA
 */

#include "config.h"

#include <libtracker-common/tracker-common.h>

#include "tracker-fts-tokenizer.h"
#include "tracker-fts.h"

#ifndef HAVE_BUILTIN_FTS

#include "sqlite3.h"
#include "fts5.h"

int sqlite3_fts5_init ();

#endif

static gchar **
get_fts_properties (GHashTable  *tables)
{
	GList *table_columns, *columns;
	gchar **property_names;
	GList *keys, *l;

	columns = NULL;
	keys = g_hash_table_get_keys (tables);
	keys = g_list_sort (keys, (GCompareFunc) strcmp);

	for (l = keys; l; l = l->next) {
		table_columns = g_hash_table_lookup (tables, l->data);
		columns = g_list_concat (columns, g_list_copy (table_columns));
	}

	property_names = tracker_glist_to_string_list (columns);
	g_list_free (columns);
	g_list_free (keys);

	return property_names;
}

gboolean
tracker_fts_init_db (sqlite3            *db,
                     TrackerDBInterface *interface,
                     GHashTable         *tables)
{
	gchar **property_names;
	gboolean retval;
#ifndef HAVE_BUILTIN_FTS
	gchar *err;

	if (sqlite3_load_extension (db, NULL, "sqlite3_fts5_init", &err) != SQLITE_OK) {
		g_warning ("Could not load fts5 module: %s", err);
		return FALSE;
	}
#endif

	property_names = get_fts_properties (tables);
	retval = tracker_tokenizer_initialize (db, interface, (const gchar **) property_names);
	g_strfreev (property_names);

	return retval;
}

gboolean
tracker_fts_create_table (sqlite3     *db,
                          const gchar *database,
                          gchar       *table_name,
                          GHashTable  *tables,
                          GHashTable  *grouped_columns)
{
	GString *str, *from, *fts;
	gchar *index_table;
	GList *columns, *keys, *l;
	gint rc;

	if (g_hash_table_size (tables) == 0)
		return TRUE;

	/* Create view on tables/columns marked as FTS-indexed */
	str = g_string_new ("CREATE VIEW ");
	g_string_append_printf (str, "\"%s\".fts_view AS SELECT \"rdfs:Resource\".ID as rowid ",
				database);
	from = g_string_new ("FROM \"rdfs:Resource\" ");

	fts = g_string_new ("CREATE VIRTUAL TABLE ");
	g_string_append_printf (fts, "\"%s\".%s USING fts5(content=\"fts_view\", ",
				database, table_name);

	keys = g_hash_table_get_keys (tables);
	keys = g_list_sort (keys, (GCompareFunc) strcmp);

	for (l = keys; l; l = l->next) {
		index_table = l->data;
		columns = g_hash_table_lookup (tables, l->data);

		while (columns) {
			if (grouped_columns &&
			    g_hash_table_lookup (grouped_columns, columns->data)) {
				g_string_append_printf (str, ", group_concat(\"%s\".\"%s\")",
							index_table,
							(gchar *) columns->data);
			} else {
				g_string_append_printf (str, ", \"%s\".\"%s\"",
							index_table,
							(gchar *) columns->data);
			}

			g_string_append_printf (str, " AS \"%s\" ",
						(gchar *) columns->data);
			g_string_append_printf (fts, "\"%s\", ",
						(gchar *) columns->data);

			columns = columns->next;
		}

		g_string_append_printf (from, "LEFT OUTER JOIN \"%s\".\"%s\" ON "
					" \"rdfs:Resource\".ID = \"%s\".ID ",
					database, index_table, index_table);
	}

	g_list_free (keys);

	g_string_append (str, from->str);
	g_string_free (from, TRUE);

	rc = sqlite3_exec(db, str->str, NULL, NULL, NULL);
	g_string_free (str, TRUE);

	if (rc != SQLITE_OK) {
		g_assert_not_reached();
		return FALSE;
	}

	g_string_append (fts, "tokenize=TrackerTokenizer)");
	rc = sqlite3_exec(db, fts->str, NULL, NULL, NULL);

	if (rc != SQLITE_OK)
		return FALSE;

	str = g_string_new (NULL);
	g_string_append_printf (str,
	                        "INSERT INTO %s(%s, rank) VALUES('rank', 'tracker_rank()')",
	                        table_name, table_name);
	rc = sqlite3_exec (db, str->str, NULL, NULL, NULL);
	g_string_free (str, TRUE);

	return (rc == SQLITE_OK);
}

gboolean
tracker_fts_delete_table (sqlite3     *db,
			  const gchar *database,
                          gchar       *table_name)
{
	gchar *query;
	gint rc;

	query = g_strdup_printf ("DROP VIEW fts_view");
	rc = sqlite3_exec (db, query, NULL, NULL, NULL);
	g_free (query);

	if (rc != SQLITE_OK)
		return FALSE;

	query = g_strdup_printf ("DROP TABLE \"%s\".%s",
	                         database, table_name);
	rc = sqlite3_exec (db, query, NULL, NULL, NULL);
	g_free (query);

	return rc == SQLITE_OK;
}

gboolean
tracker_fts_alter_table (sqlite3     *db,
			 const gchar *database,
			 gchar       *table_name,
			 GHashTable  *tables,
			 GHashTable  *grouped_columns)
{
	tracker_fts_delete_table (db, database, table_name);

	if (!tracker_fts_create_table (db, database, table_name, tables, grouped_columns))
		return FALSE;

	tracker_fts_rebuild_tokens (db, database, table_name);
	return TRUE;
}

void
tracker_fts_rebuild_tokens (sqlite3     *db,
			    const gchar *database,
                            const gchar *table_name)
{
	gchar *query;

	/* This special query rebuilds the tokens in the given FTS table */
	query = g_strdup_printf ("INSERT INTO \"%s\".%s(%s) VALUES('rebuild')",
				 database, table_name, table_name);
	sqlite3_exec(db, query, NULL, NULL, NULL);
	g_free (query);
}
