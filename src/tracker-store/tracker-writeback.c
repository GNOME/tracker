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

#include "config.h"

#include <libtracker-data/tracker-data.h>

#include "tracker-writeback.h"

typedef struct {
	/* Accessed by updates thread */
	GHashTable *allowances;
	GHashTable *pending_events;

	/* Accessed by both updates and dbus threads */
	GMutex mutex;
	GHashTable *ready_events;
} WritebackPrivate;

static WritebackPrivate *private;

static GArray*
rdf_types_to_array (GPtrArray *rdf_types)
{
	GArray *new_types;
	guint n;

	new_types =  g_array_sized_new (FALSE, FALSE, sizeof (gint), rdf_types->len);

	for (n = 0; n < rdf_types->len; n++) {
		gint id = tracker_class_get_id (rdf_types->pdata[n]);
		g_array_append_val (new_types, id);
	}

	return new_types;
}

static void
array_free (GArray *array)
{
	g_array_free (array, TRUE);
}

void
tracker_writeback_check (gint         graph_id,
                         const gchar *graph,
                         gint         subject_id,
                         const gchar *subject,
                         gint         pred_id,
                         gint         object_id,
                         const gchar *object,
                         GPtrArray   *rdf_types)
{
	/* When graph is NULL, the graph is the default one. We only do
	 * writeback reporting in the default graph (update queries that
	 * aren't coming from the miner)
	 */

	if (graph != NULL) {
		/* g_debug ("Not doing writeback check, no graph"); */
		return;
	}

	g_return_if_fail (private != NULL);

	if (g_hash_table_lookup (private->allowances, GINT_TO_POINTER (pred_id))) {
		if (!private->pending_events) {
			private->pending_events = g_hash_table_new_full (g_direct_hash, g_direct_equal,
			                                                 (GDestroyNotify) NULL,
			                                                 (GDestroyNotify) NULL);
		}

		g_hash_table_insert (private->pending_events,
		                     GINT_TO_POINTER (subject_id),
		                     rdf_types_to_array (rdf_types));
	}
}

void
tracker_writeback_reset_pending ()
{
	g_return_if_fail (private != NULL);

	if (private->pending_events) {
		g_hash_table_remove_all (private->pending_events);
	}
}

void
tracker_writeback_reset_ready ()
{
	g_return_if_fail (private != NULL);

	if (private->ready_events) {
		g_hash_table_unref (private->ready_events);
		private->ready_events = NULL;
	}
}

GHashTable *
tracker_writeback_get_ready (void)
{
	GHashTable *events;

	g_return_val_if_fail (private != NULL, NULL);

	g_mutex_lock (&private->mutex);
	events = private->ready_events;
	private->ready_events = NULL;
	g_mutex_unlock (&private->mutex);

	return events;
}

static void
free_private (gpointer user_data)
{
	WritebackPrivate *private;

	private = user_data;
	if (private->ready_events)
		g_hash_table_unref (private->ready_events);
	if (private->pending_events)
		g_hash_table_unref (private->pending_events);
	g_hash_table_unref (private->allowances);
	g_free (private);
}

void
tracker_writeback_init (TrackerDataManager                *data_manager,
                        TrackerWritebackGetPredicatesFunc  func)
{
	TrackerOntologies *ontologies;
	GStrv predicates_to_signal;
	gint i, count;

	g_return_if_fail (private == NULL);

	private = g_new0 (WritebackPrivate, 1);
	g_mutex_init (&private->mutex);

	private->allowances = g_hash_table_new_full (g_direct_hash,
	                                             g_direct_equal,
	                                             NULL,
	                                             NULL);

	g_message ("Setting up predicates for writeback notification...");

	if (!func) {
		g_message ("  No predicates set, no TrackerWritebackGetPredicatesFunc");
		return;
	}

	predicates_to_signal = (*func)();

	if (!predicates_to_signal) {
		g_message ("  No predicates set, none are configured in ontology");
		return;
	}

	count = g_strv_length (predicates_to_signal);
	ontologies = tracker_data_manager_get_ontologies (data_manager);

	for (i = 0; i < count; i++) {
		TrackerProperty *predicate = tracker_ontologies_get_property_by_uri (ontologies, predicates_to_signal[i]);
		if (predicate) {
			gint id = tracker_property_get_id (predicate);
			g_message ("  Adding:'%s'", predicates_to_signal[i]);
			g_hash_table_insert (private->allowances,
			                     GINT_TO_POINTER (id),
			                     GINT_TO_POINTER (TRUE));
		}
	}

	g_strfreev (predicates_to_signal);
}

void
tracker_writeback_transact (void)
{
	GHashTableIter iter;
	gpointer key, value;

	g_return_if_fail (private != NULL);

	if (!private->pending_events)
		return;

	g_mutex_lock (&private->mutex);

	if (!private->ready_events) {
		private->ready_events = g_hash_table_new_full (g_direct_hash, g_direct_equal,
		                                               (GDestroyNotify) NULL,
		                                               (GDestroyNotify) array_free);
	}

	g_hash_table_iter_init (&iter, private->pending_events);

	while (g_hash_table_iter_next (&iter, &key, &value)) {
		g_hash_table_insert (private->ready_events, key, value);
		g_hash_table_iter_remove (&iter);
	}

	g_mutex_unlock (&private->mutex);
}

void
tracker_writeback_shutdown (void)
{
	g_return_if_fail (private != NULL);

	tracker_writeback_reset_pending ();

	/* Perhaps hurry an emit of the ready events here? We're shutting down,
	 * so I guess we're not required to do that here ... ? */
	g_clear_pointer (&private->ready_events, g_hash_table_unref);

	free_private (private);
	private = NULL;
}
