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

typedef struct {
	guint total;
	GPtrArray *notify_classes;
} EventsPrivate;

static EventsPrivate *private;

guint
tracker_events_get_total (gboolean and_reset)
{
	guint total;

	g_return_val_if_fail (private != NULL, 0);

	total = private->total;

	if (and_reset) {
		private->total = 0;
	}

	return total;
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
	guint i;

	g_return_if_fail (rdf_types != NULL);
	g_return_if_fail (private != NULL);

	for (i = 0; i < rdf_types->len; i++) {
		if (tracker_class_get_notify (rdf_types->pdata[i])) {
			tracker_class_add_insert_event (rdf_types->pdata[i],
			                                graph_id,
			                                subject_id,
			                                pred_id,
			                                object_id);
			private->total++;
		}
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
	guint i;

	g_return_if_fail (rdf_types != NULL);
	g_return_if_fail (private != NULL);

	for (i = 0; i < rdf_types->len; i++) {
		if (tracker_class_get_notify (rdf_types->pdata[i])) {
			tracker_class_add_delete_event (rdf_types->pdata[i],
			                                graph_id,
			                                subject_id,
			                                pred_id,
			                                object_id);
			private->total++;
		}
	}
}

void
tracker_events_reset_pending (void)
{
	guint i;

	g_return_if_fail (private != NULL);

	for (i = 0; i < private->notify_classes->len; i++) {
		TrackerClass *class = g_ptr_array_index (private->notify_classes, i);

		tracker_class_reset_pending_events (class);
	}
}

static void
free_private (EventsPrivate *private)
{
	guint i;

	for (i = 0; i < private->notify_classes->len; i++) {
		TrackerClass *class = g_ptr_array_index (private->notify_classes, i);

		tracker_class_reset_pending_events (class);

		/* Perhaps hurry an emit of the ready events here? We're shutting down,
		 * so I guess we're not required to do that here ... ? */
		tracker_class_reset_ready_events (class);
	}

	g_ptr_array_unref (private->notify_classes);

	g_free (private);
}

TrackerClass **
tracker_events_get_classes (guint *length)
{
	g_return_val_if_fail (private != NULL, NULL);

	*length = private->notify_classes->len;

	return (TrackerClass **) (private->notify_classes->pdata);
}

void
tracker_events_init (TrackerDataManager *data_manager)
{
	TrackerOntologies *ontologies;
	TrackerClass **classes;
	guint length = 0, i;

	private = g_new0 (EventsPrivate, 1);

	ontologies = tracker_data_manager_get_ontologies (data_manager);
	classes = tracker_ontologies_get_classes (ontologies, &length);

	private->notify_classes = g_ptr_array_sized_new (length);
	g_ptr_array_set_free_func (private->notify_classes, (GDestroyNotify) g_object_unref);

	for (i = 0; i < length; i++) {
		TrackerClass *class = classes[i];

		if (tracker_class_get_notify (class)) {
			g_ptr_array_add (private->notify_classes, g_object_ref (class));
		}
	}

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
