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
 *
 * Authors:
 *  Philip Van Hoof <philip@codeminded.be>
 */

#ifndef __TRACKER_STORE_EVENTS_H__
#define __TRACKER_STORE_EVENTS_H__

#include <libtracker-common/tracker-common.h>
#include <libtracker-data/tracker-data.h>

G_BEGIN_DECLS

typedef struct _TrackerEventBatch TrackerEventBatch;

typedef void (*TrackerEventsForeach) (gint     graph_id,
                                      gint     subject_id,
                                      gint     pred_id,
                                      gint     object_id,
                                      gpointer user_data);

void           tracker_events_init              (void);
void           tracker_events_shutdown          (void);
void           tracker_events_add_insert        (gint         graph_id,
                                                 gint         subject_id,
                                                 const gchar *subject,
                                                 gint         pred_id,
                                                 gint         object_id,
                                                 const gchar *object,
                                                 GPtrArray   *rdf_types);
void           tracker_events_add_delete        (gint         graph_id,
                                                 gint         subject_id,
                                                 const gchar *subject,
                                                 gint         pred_id,
                                                 gint         object_id,
                                                 const gchar *object,
                                                 GPtrArray   *rdf_types);
guint          tracker_events_get_total         (void);
void           tracker_events_reset_pending     (void);

void           tracker_events_transact          (void);

GHashTable *   tracker_events_get_pending       (void);

void           tracker_event_batch_foreach_insert_event (TrackerEventBatch    *events,
                                                         TrackerEventsForeach  foreach,
                                                         gpointer              user_data);
void           tracker_event_batch_foreach_delete_event (TrackerEventBatch    *events,
                                                         TrackerEventsForeach  foreach,
                                                         gpointer              user_data);

G_END_DECLS

#endif /* __TRACKER_STORE_EVENTS_H__ */
