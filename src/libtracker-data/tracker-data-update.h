/*
 * Copyright (C) 2006, Jamie McCracken <jamiemcc@gnome.org>
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

#ifndef __LIBTRACKER_DATA_UPDATE_H__
#define __LIBTRACKER_DATA_UPDATE_H__

#if !defined (__LIBTRACKER_DATA_INSIDE__) && !defined (TRACKER_COMPILATION)
#error "only <libtracker-data/tracker-data.h> must be included directly."
#endif

#include <glib.h>
#include <gio/gio.h>

G_BEGIN_DECLS

typedef struct _TrackerData TrackerData;
typedef struct _TrackerDataClass TrackerDataClass;

#include <libtracker-sparql/tracker-sparql.h>
#include "tracker-db-interface.h"
#include "tracker-data-manager.h"

#define TRACKER_TYPE_DATA         (tracker_data_get_type ())
#define TRACKER_DATA(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), TRACKER_TYPE_DATA, TrackerData))
#define TRACKER_DATA_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST ((k), TRACKER_TYPE_DATA, TrackerDataClass))
#define TRACKER_IS_DATA(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), TRACKER_TYPE_DATA))
#define TRACKER_IS_DATA_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), TRACKER_TYPE_DATA))
#define TRACKER_DATA_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), TRACKER_TYPE_DATA, TrackerDataClass))

typedef struct _TrackerData TrackerData;
typedef struct _TrackerDataClass TrackerDataClass;

typedef struct _TrackerData TrackerData;
typedef struct _TrackerData TrackerDataUpdate;

typedef void (*TrackerStatementCallback) (gint                  graph_id,
                                          const gchar          *graph,
                                          gint                  subject_id,
                                          const gchar          *subject,
                                          gint                  predicate_id,
                                          gint                  object_id,
                                          const gchar          *object,
                                          GPtrArray            *rdf_types,
                                          gpointer              user_data);
typedef void (*TrackerCommitCallback)    (gpointer              user_data);

GQuark   tracker_data_error_quark                   (void);

/* Metadata */
void     tracker_data_delete_statement              (TrackerData               *data,
                                                     const gchar               *graph,
                                                     const gchar               *subject,
                                                     const gchar               *predicate,
                                                     const gchar               *object,
                                                     GError                   **error);
void     tracker_data_insert_statement              (TrackerData               *data,
                                                     const gchar               *graph,
                                                     const gchar               *subject,
                                                     const gchar               *predicate,
                                                     const gchar               *object,
                                                     GError                   **error);
void     tracker_data_insert_statement_with_uri     (TrackerData               *data,
                                                     const gchar               *graph,
                                                     const gchar               *subject,
                                                     const gchar               *predicate,
                                                     const gchar               *object,
                                                     GError                   **error);
void     tracker_data_insert_statement_with_string  (TrackerData               *data,
                                                     const gchar               *graph,
                                                     const gchar               *subject,
                                                     const gchar               *predicate,
                                                     const gchar               *object,
                                                     GError                   **error);
void     tracker_data_update_statement              (TrackerData               *data,
                                                     const gchar               *graph,
                                                     const gchar               *subject,
                                                     const gchar               *predicate,
                                                     const gchar               *object,
                                                     GError                   **error);
void     tracker_data_begin_transaction             (TrackerData               *data,
                                                     GError                   **error);
void     tracker_data_begin_ontology_transaction    (TrackerData               *data,
                                                     GError                   **error);
void     tracker_data_begin_transaction_for_replay  (TrackerData               *data,
                                                     time_t                     time,
                                                     GError                   **error);
void     tracker_data_commit_transaction            (TrackerData               *data,
                                                     GError                   **error);
void     tracker_data_rollback_transaction          (TrackerData               *data);
void     tracker_data_update_sparql                 (TrackerData               *data,
                                                     const gchar               *update,
                                                     GError                   **error);
GVariant *
         tracker_data_update_sparql_blank           (TrackerData               *data,
                                                     const gchar               *update,
                                                     GError                   **error);
void     tracker_data_update_buffer_flush           (TrackerData               *data,
                                                     GError                   **error);
void     tracker_data_update_buffer_might_flush     (TrackerData               *data,
                                                     GError                   **error);
void     tracker_data_load_turtle_file              (TrackerData               *data,
                                                     GFile                     *file,
                                                     GError                   **error);

void     tracker_data_sync                          (TrackerData               *data);
void     tracker_data_replay_journal                (TrackerData               *data,
                                                     TrackerBusyCallback        busy_callback,
                                                     gpointer                   busy_user_data,
                                                     const gchar               *busy_status,
                                                     GError                   **error);

/* Calling back */
void     tracker_data_add_insert_statement_callback      (TrackerData               *data,
                                                          TrackerStatementCallback   callback,
                                                          gpointer                   user_data);
void     tracker_data_add_delete_statement_callback      (TrackerData               *data,
                                                          TrackerStatementCallback   callback,
                                                          gpointer                   user_data);
void     tracker_data_add_commit_statement_callback      (TrackerData               *data,
                                                          TrackerCommitCallback      callback,
                                                          gpointer                   user_data);
void     tracker_data_add_rollback_statement_callback    (TrackerData               *data,
                                                          TrackerCommitCallback      callback,
                                                          gpointer                   user_data);
void     tracker_data_remove_insert_statement_callback   (TrackerData               *data,
                                                          TrackerStatementCallback   callback,
                                                          gpointer                   user_data);
void     tracker_data_remove_delete_statement_callback   (TrackerData               *data,
                                                          TrackerStatementCallback   callback,
                                                          gpointer                   user_data);
void     tracker_data_remove_commit_statement_callback   (TrackerData               *data,
                                                          TrackerCommitCallback      callback,
                                                          gpointer                   user_data);
void     tracker_data_remove_rollback_statement_callback (TrackerData               *data,
                                                          TrackerCommitCallback      callback,
                                                          gpointer                   user_data);

GType         tracker_data_get_type (void) G_GNUC_CONST;
TrackerData * tracker_data_new      (TrackerDataManager *manager);

G_END_DECLS

#endif /* __LIBTRACKER_DATA_UPDATE_H__ */
