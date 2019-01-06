/*
 * Copyright (C) 2008, Nokia <ivan.frade@nokia.com>
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
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 */

#ifndef __LIBTRACKER_DB_INTERFACE_SQLITE_H__
#define __LIBTRACKER_DB_INTERFACE_SQLITE_H__

#include "config.h"

#include "tracker-db-interface.h"

G_BEGIN_DECLS

#if !defined (__LIBTRACKER_DATA_INSIDE__) && !defined (TRACKER_COMPILATION)
#error "only <libtracker-data/tracker-data.h> must be included directly."
#endif

#define TRACKER_COLLATION_NAME "TRACKER"
#define TRACKER_TITLE_COLLATION_NAME "TRACKER_TITLE"

typedef void (*TrackerDBWalCallback) (TrackerDBInterface *iface,
                                      gint                n_pages,
                                      gpointer            user_data);

typedef enum {
	TRACKER_DB_INTERFACE_READONLY  = 1 << 0,
	TRACKER_DB_INTERFACE_USE_MUTEX = 1 << 1
} TrackerDBInterfaceFlags;

TrackerDBInterface *tracker_db_interface_sqlite_new                    (const gchar              *filename,
                                                                        TrackerDBInterfaceFlags   flags,
                                                                        GError                  **error);
gint64              tracker_db_interface_sqlite_get_last_insert_id     (TrackerDBInterface       *interface);
void                tracker_db_interface_sqlite_enable_shared_cache    (void);
void                tracker_db_interface_sqlite_fts_init               (TrackerDBInterface       *interface,
                                                                        GHashTable               *properties,
                                                                        GHashTable               *multivalued,
                                                                        gboolean                  create);
void                tracker_db_interface_sqlite_reset_collator         (TrackerDBInterface       *interface);
void                tracker_db_interface_sqlite_wal_hook               (TrackerDBInterface       *interface,
                                                                        TrackerDBWalCallback      callback,
                                                                        gpointer                  user_data);
gboolean            tracker_db_interface_sqlite_wal_checkpoint         (TrackerDBInterface       *interface,
                                                                        gboolean                  blocking,
                                                                        GError                  **error);
gboolean            tracker_db_interface_init_vtabs                    (TrackerDBInterface       *interface,
                                                                        TrackerOntologies        *ontologies);

#if HAVE_TRACKER_FTS
void                tracker_db_interface_sqlite_fts_delete_table       (TrackerDBInterface       *interface);

void                tracker_db_interface_sqlite_fts_alter_table        (TrackerDBInterface       *interface,
                                                                        GHashTable               *properties,
                                                                        GHashTable               *multivalued);
gboolean            tracker_db_interface_sqlite_fts_update_text        (TrackerDBInterface       *db_interface,
	                                                                int                       id,
                                                                        const gchar             **properties,
                                                                        const gchar             **text);

gboolean            tracker_db_interface_sqlite_fts_delete_text        (TrackerDBInterface       *interface,
                                                                        int                       rowid,
                                                                        const gchar              *property,
                                                                        const gchar              *old_text);
gboolean            tracker_db_interface_sqlite_fts_delete_id          (TrackerDBInterface       *interface,
                                                                        int                       rowid);
void                tracker_db_interface_sqlite_fts_update_commit      (TrackerDBInterface       *interface);
void                tracker_db_interface_sqlite_fts_update_rollback    (TrackerDBInterface       *interface);

void                tracker_db_interface_sqlite_fts_rebuild_tokens     (TrackerDBInterface       *interface);

#endif

G_END_DECLS

#endif /* __LIBTRACKER_DB_INTERFACE_SQLITE_H__ */
