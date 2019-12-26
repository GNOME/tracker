/*
 * Copyright (C) 2009, Nokia <ivan.frade@nokia.com>
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

#ifndef __LIBTRACKER_CONTROL_MANAGER_H__
#define __LIBTRACKER_CONTROL_MANAGER_H__

#if !defined (__LIBTRACKER_CONTROL_H_INSIDE__) && !defined (TRACKER_COMPILATION)
#error "Only <libtracker-control/tracker-control.h> can be included directly."
#endif

#include <gio/gio.h>

G_BEGIN_DECLS

#define TRACKER_TYPE_MINER_MANAGER         (tracker_miner_manager_get_type())
#define TRACKER_MINER_MANAGER(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), TRACKER_TYPE_MINER_MANAGER, TrackerMinerManager))
#define TRACKER_MINER_MANAGER_CLASS(c)     (G_TYPE_CHECK_CLASS_CAST ((c),    TRACKER_TYPE_MINER_MANAGER, TrackerMinerManagerClass))
#define TRACKER_IS_MINER_MANAGER(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), TRACKER_TYPE_MINER_MANAGER))
#define TRACKER_IS_MINER_MANAGER_CLASS(c)  (G_TYPE_CHECK_CLASS_TYPE ((c),    TRACKER_TYPE_MINER_MANAGER))
#define TRACKER_MINER_MANAGER_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o),  TRACKER_TYPE_MINER_MANAGER, TrackerMinerManagerClass))

#define TRACKER_MINER_MANAGER_ERROR tracker_miner_manager_error_quark ()

typedef struct _TrackerMinerManager TrackerMinerManager;

/**
 * TrackerMinerManagerError:
 * @TRACKER_MINER_MANAGER_ERROR_NOT_AVAILABLE: The miner in question
 * is not active and can so can not be used.
 * @TRACKER_MINER_MANAGER_ERROR_NOENT: The resource that the
 * miner is handling (for example a file or URI) does not exist.
 *
 * Enumeration values used in errors returned by the
 * #TrackerMinerManager API.
 *
 * Since: 0.8
 **/
typedef enum {
	TRACKER_MINER_MANAGER_ERROR_NOT_AVAILABLE,
	TRACKER_MINER_MANAGER_ERROR_NOENT
} TrackerMinerManagerError;

/**
 * TrackerMinerManager:
 *
 * Object to query and control miners.
 **/
struct _TrackerMinerManager {
	GObject parent_instance;
};

/**
 * TrackerMinerManagerClass:
 * @miner_progress: The progress signal for all miners including name,
 * status and progress as a percentage between 0 and 1.
 * @miner_paused: The paused signal for all miners known about.
 * @miner_resumed: The resumed signal for all miners known about.
 * @miner_activated: The activated signal for all miners which
 * indicates the miner is available on d-bus.
 * @miner_deactivated: The deactivate for all miners which indicates
 * the miner is no longer available on d-bus.
 **/
typedef struct {
	GObjectClass parent_class;

	void (* miner_progress)    (TrackerMinerManager *manager,
	                            const gchar         *miner_name,
	                            const gchar         *status,
	                            gdouble              progress);
	void (* miner_paused)      (TrackerMinerManager *manager,
	                            const gchar         *miner_name);
	void (* miner_resumed)     (TrackerMinerManager *manager,
	                            const gchar         *miner_name);
	void (* miner_activated)   (TrackerMinerManager *manager,
	                            const gchar         *miner_name);
	void (* miner_deactivated) (TrackerMinerManager *manager,
	                            const gchar         *miner_name);
} TrackerMinerManagerClass;

GType                tracker_miner_manager_get_type           (void) G_GNUC_CONST;
GQuark               tracker_miner_manager_error_quark        (void) G_GNUC_CONST;

TrackerMinerManager *tracker_miner_manager_new                (void);
TrackerMinerManager *tracker_miner_manager_new_full           (gboolean              auto_start,
                                                               GError              **error);
GSList *             tracker_miner_manager_get_running        (TrackerMinerManager  *manager);
GSList *             tracker_miner_manager_get_available      (TrackerMinerManager  *manager);
gboolean             tracker_miner_manager_pause              (TrackerMinerManager  *manager,
                                                               const gchar          *miner,
                                                               const gchar          *reason,
                                                               guint32              *cookie);
gboolean             tracker_miner_manager_pause_for_process  (TrackerMinerManager  *manager,
                                                               const gchar          *miner,
                                                               const gchar          *reason,
                                                               guint32              *cookie);
gboolean             tracker_miner_manager_resume             (TrackerMinerManager  *manager,
                                                               const gchar          *miner,
                                                               guint32               cookie);
gboolean             tracker_miner_manager_is_active          (TrackerMinerManager  *manager,
                                                               const gchar          *miner);
gboolean             tracker_miner_manager_is_paused          (TrackerMinerManager  *manager,
                                                               const gchar          *miner,
                                                               GStrv                *applications,
                                                               GStrv                *reasons);
gboolean             tracker_miner_manager_get_status         (TrackerMinerManager  *manager,
                                                               const gchar          *miner,
                                                               gchar               **status,
                                                               gdouble              *progress,
                                                               gint                 *remaining_time);
const gchar *        tracker_miner_manager_get_display_name   (TrackerMinerManager  *manager,
                                                               const gchar          *miner);
const gchar *        tracker_miner_manager_get_description    (TrackerMinerManager  *manager,
                                                               const gchar          *miner);

gboolean             tracker_miner_manager_reindex_by_mimetype (TrackerMinerManager  *manager,
                                                                const GStrv           mimetypes,
                                                                GError              **error);
gboolean             tracker_miner_manager_index_file          (TrackerMinerManager  *manager,
                                                                GFile                *file,
                                                                GCancellable         *cancellable,
                                                                GError              **error);
void                 tracker_miner_manager_index_file_async    (TrackerMinerManager  *manager,
                                                                GFile                *file,
                                                                GCancellable         *cancellable,
                                                                GAsyncReadyCallback   callback,
                                                                gpointer              user_data);
gboolean             tracker_miner_manager_index_file_finish   (TrackerMinerManager  *manager,
                                                                GAsyncResult         *result,
                                                                GError              **error);
gboolean             tracker_miner_manager_index_file_for_process        (TrackerMinerManager  *manager,
                                                                          GFile                *file,
                                                                          GCancellable         *cancellable,
                                                                          GError              **error);
void                 tracker_miner_manager_index_file_for_process_async  (TrackerMinerManager  *manager,
                                                                          GFile                *file,
                                                                          GCancellable         *cancellable,
                                                                          GAsyncReadyCallback   callback,
                                                                          gpointer              user_data);
gboolean             tracker_miner_manager_index_file_for_process_finish (TrackerMinerManager  *manager,
                                                                          GAsyncResult         *result,
                                                                          GError              **error);

G_END_DECLS

#endif /* __LIBTRACKER_CONTROL_MANAGER_H__ */
