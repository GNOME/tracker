/*
 * Copyright (C) 2008, Nokia <ivan.frade@nokia.com>
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

#include "config.h"

#include <libtracker-data/tracker-data.h>

#include "tracker-events.h"

typedef struct _TrackerEventBatch TrackerEventBatch;

struct _TrackerEventBatch
{
	struct {
		GArray *sub_pred_ids;
		GArray *obj_graph_ids;
	} deletes;
	struct {
		GArray *sub_pred_ids;
		GArray *obj_graph_ids;
	} inserts;
};

typedef struct {
	/* Accessed by updates/dbus threads */
	GMutex mutex;
	GHashTable *ready;

	/* Only accessed by updates thread */
	GHashTable *pending;
	guint total;
} EventsPrivate;

static EventsPrivate *private;

static TrackerEventBatch *
tracker_event_batch_new (void)
{
	TrackerEventBatch *events;

	events = g_new0 (TrackerEventBatch, 1);
	events->deletes.sub_pred_ids = g_array_new (FALSE, FALSE, sizeof (gint64));
	events->deletes.obj_graph_ids = g_array_new (FALSE, FALSE, sizeof (gint64));
	events->inserts.sub_pred_ids = g_array_new (FALSE, FALSE, sizeof (gint64));
	events->inserts.obj_graph_ids = g_array_new (FALSE, FALSE, sizeof (gint64));

	return events;
}

static void
tracker_event_batch_free (TrackerEventBatch *events)
{
	g_array_unref (events->deletes.sub_pred_ids);
	g_array_unref (events->deletes.obj_graph_ids);
	g_array_unref (events->inserts.sub_pred_ids);
	g_array_unref (events->inserts.obj_graph_ids);
	g_free (events);
}

static void
insert_vals_into_arrays (GArray *sub_pred_ids,
                         GArray *obj_graph_ids,
                         gint    graph_id,
                         gint    subject_id,
                         gint    pred_id,
                         gint    object_id)
{
	gint i, j, k;
	gint64 tmp;
	gint64 sub_pred_id;
	gint64 obj_graph_id;

	sub_pred_id = (gint64) subject_id;
	sub_pred_id = sub_pred_id << 32 | pred_id;
	obj_graph_id = (gint64) object_id;
	obj_graph_id = obj_graph_id << 32 | graph_id;

	i = 0;
	j = sub_pred_ids->len - 1;

	while (j - i > 0) {
		k = (i + j) / 2;
		tmp = g_array_index (sub_pred_ids, gint64, k);
		if (tmp == sub_pred_id) {
			i = k + 1;
			break;
		} else if (tmp > sub_pred_id)
			j = k;
		else
			i = k + 1;
	}

	g_array_insert_val (sub_pred_ids, i, sub_pred_id);
	g_array_insert_val (obj_graph_ids, i, obj_graph_id);
}

static void
tracker_event_batch_add_insert_event (TrackerEventBatch *events,
                                      gint               graph_id,
                                      gint               subject_id,
                                      gint               pred_id,
                                      gint               object_id)
{
	insert_vals_into_arrays (events->inserts.sub_pred_ids,
	                         events->inserts.obj_graph_ids,
	                         graph_id,
	                         subject_id,
	                         pred_id,
	                         object_id);
}

static void
tracker_event_batch_add_delete_event (TrackerEventBatch *events,
                                      gint               graph_id,
                                      gint               subject_id,
                                      gint               pred_id,
                                      gint               object_id)
{
	insert_vals_into_arrays (events->deletes.sub_pred_ids,
	                         events->deletes.obj_graph_ids,
	                         graph_id,
	                         subject_id,
	                         pred_id,
	                         object_id);
}

static void
foreach_event_in_arrays (GArray               *sub_pred_ids,
                         GArray               *obj_graph_ids,
                         TrackerEventsForeach  foreach,
                         gpointer              user_data)
{
	guint i;

	g_assert (sub_pred_ids->len == obj_graph_ids->len);

	for (i = 0; i < sub_pred_ids->len; i++) {
		gint graph_id, subject_id, pred_id, object_id;
		gint64 sub_pred_id;
		gint64 obj_graph_id;

		sub_pred_id = g_array_index (sub_pred_ids, gint64, i);
		obj_graph_id = g_array_index (obj_graph_ids, gint64, i);

		pred_id = sub_pred_id & 0xffffffff;
		subject_id = sub_pred_id >> 32;
		graph_id = obj_graph_id & 0xffffffff;
		object_id = obj_graph_id >> 32;

		foreach (graph_id, subject_id, pred_id, object_id, user_data);
	}
}

void
tracker_event_batch_foreach_insert_event (TrackerEventBatch    *events,
                                          TrackerEventsForeach  foreach,
                                          gpointer              user_data)
{
	g_return_if_fail (events != NULL);
	g_return_if_fail (foreach != NULL);

	foreach_event_in_arrays (events->inserts.sub_pred_ids,
	                         events->inserts.obj_graph_ids,
	                         foreach, user_data);
}

void
tracker_event_batch_foreach_delete_event (TrackerEventBatch    *events,
                                          TrackerEventsForeach  foreach,
                                          gpointer              user_data)
{
	g_return_if_fail (events != NULL);
	g_return_if_fail (foreach != NULL);

	foreach_event_in_arrays (events->deletes.sub_pred_ids,
	                         events->deletes.obj_graph_ids,
	                         foreach, user_data);
}

static GHashTable *
tracker_event_batch_hashtable_new (void)
{
	return g_hash_table_new_full (NULL, NULL,
	                              (GDestroyNotify) g_object_unref,
	                              (GDestroyNotify) tracker_event_batch_free);
}

void
tracker_event_batch_merge (TrackerEventBatch *dest,
                           TrackerEventBatch *to_copy)
{
	g_array_append_vals (dest->deletes.sub_pred_ids,
	                     to_copy->deletes.sub_pred_ids->data,
	                     to_copy->deletes.sub_pred_ids->len);
	g_array_append_vals (dest->deletes.obj_graph_ids,
	                     to_copy->deletes.obj_graph_ids->data,
	                     to_copy->deletes.obj_graph_ids->len);
	g_array_append_vals (dest->inserts.sub_pred_ids,
	                     to_copy->inserts.sub_pred_ids->data,
	                     to_copy->inserts.sub_pred_ids->len);
	g_array_append_vals (dest->inserts.obj_graph_ids,
	                     to_copy->inserts.obj_graph_ids->data,
	                     to_copy->inserts.obj_graph_ids->len);
}

guint
tracker_events_get_total (void)
{
	g_return_val_if_fail (private != NULL, 0);

	return private->total;
}

static inline TrackerEventBatch *
ensure_event_batch (TrackerClass *rdf_type)
{
	TrackerEventBatch *events;

	g_assert (private != NULL);

	if (!private->pending)
		private->pending = tracker_event_batch_hashtable_new ();

	events = g_hash_table_lookup (private->pending, rdf_type);

	if (!events) {
		events = tracker_event_batch_new ();
		g_hash_table_insert (private->pending,
		                     g_object_ref (rdf_type),
		                     events);
	}

	return events;
}

void
tracker_events_add_insert (gint         graph_id,
                           gint         subject_id,
                           const gchar *subject,
                           gint         pred_id,
                           gint         object_id,
                           const gchar *object,
                           GPtrArray   *rdf_types)
{
	TrackerEventBatch *events;
	guint i;

	g_return_if_fail (rdf_types != NULL);
	g_return_if_fail (private != NULL);

	for (i = 0; i < rdf_types->len; i++) {
		if (!tracker_class_get_notify (rdf_types->pdata[i]))
			continue;

		events = ensure_event_batch (rdf_types->pdata[i]);
		tracker_event_batch_add_insert_event (events,
		                                      graph_id,
		                                      subject_id,
		                                      pred_id,
		                                      object_id);
		private->total++;
	}
}

void
tracker_events_add_delete (gint         graph_id,
                           gint         subject_id,
                           const gchar *subject,
                           gint         pred_id,
                           gint         object_id,
                           const gchar *object,
                           GPtrArray   *rdf_types)
{
	TrackerEventBatch *events;
	guint i;

	g_return_if_fail (rdf_types != NULL);
	g_return_if_fail (private != NULL);

	for (i = 0; i < rdf_types->len; i++) {
		if (!tracker_class_get_notify (rdf_types->pdata[i]))
			continue;

		events = ensure_event_batch (rdf_types->pdata[i]);
		tracker_event_batch_add_delete_event (events,
		                                      graph_id,
		                                      subject_id,
		                                      pred_id,
		                                      object_id);
		private->total++;
	}
}

void
tracker_events_transact (void)
{
	TrackerEventBatch *prev_events, *events;
	TrackerClass *rdf_type;
	GHashTableIter iter;

	g_return_if_fail (private != NULL);

	if (!private->pending || g_hash_table_size (private->pending) == 0)
		return;

	g_mutex_lock (&private->mutex);

	if (!private->ready) {
		private->ready = tracker_event_batch_hashtable_new ();
	}

	g_hash_table_iter_init (&iter, private->pending);

	while (g_hash_table_iter_next (&iter,
	                               (gpointer *) &rdf_type,
	                               (gpointer *) &events)) {
		prev_events = g_hash_table_lookup (private->ready,
		                                   rdf_type);
		if (prev_events) {
			tracker_event_batch_merge (prev_events, events);
			g_hash_table_iter_remove (&iter);
		} else {
			g_hash_table_iter_steal (&iter);
			g_hash_table_insert (private->ready,
			                     g_object_ref (rdf_type),
			                     events);
			/* Drop the reference stolen from the pending HT */
			g_object_unref (rdf_type);
		}
	}

	private->total = 0;

	g_mutex_unlock (&private->mutex);
}

void
tracker_events_reset_pending (void)
{
	g_return_if_fail (private != NULL);

	g_clear_pointer (&private->pending, g_hash_table_unref);
}

static void
free_private (EventsPrivate *private)
{
	tracker_events_reset_pending ();
	g_free (private);
}

void
tracker_events_init (void)
{
	private = g_new0 (EventsPrivate, 1);
	g_mutex_init (&private->mutex);
}

void
tracker_events_shutdown (void)
{
	if (private != NULL) {
		free_private (private);
		private = NULL;
	} else {
		g_warning ("tracker_events already shutdown");
	}
}

GHashTable *
tracker_events_get_pending (void)
{
	GHashTable *pending;

	g_return_val_if_fail (private != NULL, NULL);

	g_mutex_lock (&private->mutex);
	pending = private->ready;
	private->ready = NULL;
	g_mutex_unlock (&private->mutex);

	return pending;
}
