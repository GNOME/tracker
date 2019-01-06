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

#ifndef __LIBTRACKER_DB_MANAGER_H__
#define __LIBTRACKER_DB_MANAGER_H__

#include <glib-object.h>

#include "tracker-db-interface.h"

G_BEGIN_DECLS

#if !defined (__LIBTRACKER_DATA_INSIDE__) && !defined (TRACKER_COMPILATION)
#error "only <libtracker-data/tracker-data.h> must be included directly."
#endif

#define TRACKER_DB_CACHE_SIZE_DEFAULT 250
#define TRACKER_DB_CACHE_SIZE_UPDATE 2000

typedef enum {
	TRACKER_DB_MANAGER_FORCE_REINDEX         = 1 << 1,
	TRACKER_DB_MANAGER_REMOVE_ALL            = 1 << 2,
	TRACKER_DB_MANAGER_READONLY              = 1 << 3,
	TRACKER_DB_MANAGER_DO_NOT_CHECK_ONTOLOGY = 1 << 4,
	TRACKER_DB_MANAGER_ENABLE_MUTEXES        = 1 << 5,
} TrackerDBManagerFlags;

typedef struct _TrackerDBManager TrackerDBManager;

TrackerDBManager   *tracker_db_manager_new                    (TrackerDBManagerFlags   flags,
                                                               GFile                  *cache_location,
                                                               GFile                  *data_location,
                                                               gboolean               *first_time,
                                                               gboolean                restoring_backup,
                                                               gboolean                shared_cache,
                                                               guint                   select_cache_size,
                                                               guint                   update_cache_size,
                                                               TrackerBusyCallback     busy_callback,
                                                               gpointer                busy_user_data,
                                                               const gchar            *busy_operation,
                                                               GObject                *iface_data,
                                                               GError                **error);
void                tracker_db_manager_free                   (TrackerDBManager      *db_manager);
void                tracker_db_manager_remove_all             (TrackerDBManager      *db_manager);
void                tracker_db_manager_optimize               (TrackerDBManager      *db_manager);
const gchar *       tracker_db_manager_get_file               (TrackerDBManager      *db_manager);
TrackerDBInterface *tracker_db_manager_get_db_interface       (TrackerDBManager      *db_manager);
TrackerDBInterface *tracker_db_manager_get_writable_db_interface (TrackerDBManager   *db_manager);

void                tracker_db_manager_ensure_locations       (TrackerDBManager      *db_manager,
							       GFile                 *cache_location,
                                                               GFile                 *data_location);
gboolean            tracker_db_manager_has_enough_space       (TrackerDBManager      *db_manager);
void                tracker_db_manager_create_version_file    (TrackerDBManager      *db_manager);
void                tracker_db_manager_remove_version_file    (TrackerDBManager      *db_manager);

TrackerDBManagerFlags
                    tracker_db_manager_get_flags              (TrackerDBManager      *db_manager,
							       guint                 *select_cache_size,
                                                               guint                 *update_cache_size);

gboolean            tracker_db_manager_get_first_index_done   (TrackerDBManager      *db_manager);
guint64             tracker_db_manager_get_last_crawl_done    (TrackerDBManager      *db_manager);
gboolean            tracker_db_manager_get_need_mtime_check   (TrackerDBManager      *db_manager);

void                tracker_db_manager_set_first_index_done   (TrackerDBManager      *db_manager,
							       gboolean               done);
void                tracker_db_manager_set_last_crawl_done    (TrackerDBManager      *db_manager,
							       gboolean               done);
void                tracker_db_manager_set_need_mtime_check   (TrackerDBManager      *db_manager,
							       gboolean               needed);

gboolean            tracker_db_manager_locale_changed         (TrackerDBManager      *db_manager,
                                                               GError               **error);
void                tracker_db_manager_set_current_locale     (TrackerDBManager      *db_manager);

gboolean            tracker_db_manager_get_tokenizer_changed  (TrackerDBManager      *db_manager);
void                tracker_db_manager_tokenizer_update       (TrackerDBManager      *db_manager);

void                tracker_db_manager_check_perform_vacuum   (TrackerDBManager      *db_manager);

void                tracker_db_manager_set_vtab_user_data     (TrackerDBManager      *db_manager,
                                                               gpointer               user_data);

G_END_DECLS

#endif /* __LIBTRACKER_DB_MANAGER_H__ */
