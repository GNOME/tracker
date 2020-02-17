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

#ifndef __LIBTRACKER_FTS_FTS_H__
#define __LIBTRACKER_FTS_FTS_H__

#include <sqlite3.h>

#include <glib.h>
#include <libtracker-data/tracker-db-manager.h>

G_BEGIN_DECLS

gboolean    tracker_fts_init_db          (sqlite3               *db,
                                          TrackerDBInterface    *interface,
                                          TrackerDBManagerFlags  flags,
                                          GHashTable            *tables);
gboolean    tracker_fts_create_table     (sqlite3     *db,
                                          const gchar *database,
                                          gchar       *table_name,
                                          GHashTable  *tables,
                                          GHashTable  *grouped_columns);
gboolean    tracker_fts_delete_table     (sqlite3     *db,
                                          const gchar *database,
                                          gchar       *table_name);
gboolean    tracker_fts_alter_table      (sqlite3     *db,
                                          const gchar *database,
                                          gchar       *table_name,
                                          GHashTable  *tables,
                                          GHashTable  *grouped_columns);
void        tracker_fts_rebuild_tokens   (sqlite3     *db,
                                          const gchar *database,
                                          const gchar *table_name);

G_END_DECLS

#endif /* __LIBTRACKER_FTS_FTS_H__ */

