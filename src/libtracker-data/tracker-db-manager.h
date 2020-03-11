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

#define TRACKER_TYPE_DB_MANAGER (tracker_db_manager_get_type ())
G_DECLARE_FINAL_TYPE (TrackerDBManager, tracker_db_manager,
                      TRACKER, DB_MANAGER, GObject)

#define TRACKER_DB_CACHE_SIZE_DEFAULT 250
#define TRACKER_DB_CACHE_SIZE_UPDATE 2000

typedef enum {
	TRACKER_DB_MANAGER_FLAGS_NONE            = 0,
	TRACKER_DB_MANAGER_READONLY              = 1 << 1,
	TRACKER_DB_MANAGER_DO_NOT_CHECK_ONTOLOGY = 1 << 2,
	TRACKER_DB_MANAGER_ENABLE_MUTEXES        = 1 << 3,
	TRACKER_DB_MANAGER_FTS_ENABLE_STEMMER    = 1 << 4,
	TRACKER_DB_MANAGER_FTS_ENABLE_UNACCENT   = 1 << 5,
	TRACKER_DB_MANAGER_FTS_ENABLE_STOP_WORDS = 1 << 6,
	TRACKER_DB_MANAGER_FTS_IGNORE_NUMBERS    = 1 << 7,
	TRACKER_DB_MANAGER_IN_MEMORY             = 1 << 8,
} TrackerDBManagerFlags;

typedef struct _TrackerDBManager TrackerDBManager;

gboolean            tracker_db_manager_db_exists              (GFile *cache_location);

TrackerDBManager   *tracker_db_manager_new                    (TrackerDBManagerFlags   flags,
                                                               GFile                  *cache_location,
                                                               gboolean               *first_time,
                                                               gboolean                shared_cache,
                                                               guint                   select_cache_size,
                                                               guint                   update_cache_size,
                                                               TrackerBusyCallback     busy_callback,
                                                               gpointer                busy_user_data,
                                                               GObject                *iface_data,
                                                               gpointer                vtab_data,
                                                               GError                **error);
TrackerDBInterface *tracker_db_manager_get_db_interface       (TrackerDBManager      *db_manager);
TrackerDBInterface *tracker_db_manager_get_writable_db_interface (TrackerDBManager   *db_manager);

gboolean            tracker_db_manager_has_enough_space       (TrackerDBManager      *db_manager);

TrackerDBManagerFlags
                    tracker_db_manager_get_flags              (TrackerDBManager      *db_manager,
							       guint                 *select_cache_size,
                                                               guint                 *update_cache_size);

gboolean            tracker_db_manager_locale_changed         (TrackerDBManager      *db_manager,
                                                               GError               **error);
void                tracker_db_manager_set_current_locale     (TrackerDBManager      *db_manager);

gboolean            tracker_db_manager_get_tokenizer_changed  (TrackerDBManager      *db_manager);
void                tracker_db_manager_tokenizer_update       (TrackerDBManager      *db_manager);

void                tracker_db_manager_check_perform_vacuum   (TrackerDBManager      *db_manager);

gboolean            tracker_db_manager_attach_database        (TrackerDBManager      *db_manager,
                                                               TrackerDBInterface    *iface,
                                                               const gchar           *name,
                                                               gboolean               create,
                                                               GError               **error);
gboolean            tracker_db_manager_detach_database        (TrackerDBManager      *db_manager,
                                                               TrackerDBInterface    *iface,
                                                               const gchar           *name,
                                                               GError               **error);

G_END_DECLS

#endif /* __LIBTRACKER_DB_MANAGER_H__ */
