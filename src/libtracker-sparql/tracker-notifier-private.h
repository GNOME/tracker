/*
 * Copyright (C) 2019 Red Hat Inc.
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
 *
 * Author: Carlos Garnacho <carlosg@gnome.org>
 */

#ifndef __TRACKER_NOTIFIER_PRIVATE_H__
#define __TRACKER_NOTIFIER_PRIVATE_H__

#include "tracker-notifier.h"

typedef struct _TrackerNotifierEventCache TrackerNotifierEventCache;

TrackerNotifierEventCache * _tracker_notifier_event_cache_new (TrackerNotifier *notifier,
                                                               const gchar     *service,
                                                               const gchar     *graph);
void _tracker_notifier_event_cache_free (TrackerNotifierEventCache *event_cache);

gpointer _tracker_notifier_get_connection (TrackerNotifier *notifier);

void
_tracker_notifier_event_cache_push_event (TrackerNotifierEventCache *cache,
                                          gint64                     id,
                                          TrackerNotifierEventType   event_type);

void _tracker_notifier_event_cache_flush_events (TrackerNotifierEventCache *cache);

#endif /* __TRACKER_NOTIFIER_PRIVATE_H__ */
