/*
 * Copyright (C) 2020, Red Hat Inc.
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

#include <gio/gio.h>

#ifndef __TRACKER_TURTLE_READER_H__
#define __TRACKER_TURTLE_READER_H__

#define TRACKER_TYPE_TURTLE_READER (tracker_turtle_reader_get_type ())
G_DECLARE_FINAL_TYPE (TrackerTurtleReader,
                      tracker_turtle_reader,
                      TRACKER, TURTLE_READER,
                      GObject);

TrackerTurtleReader * tracker_turtle_reader_new (GInputStream *stream);
TrackerTurtleReader * tracker_turtle_reader_new_for_file (GFile   *file,
                                                          GError **error);

gboolean tracker_turtle_reader_next (TrackerTurtleReader  *reader,
                                     const gchar         **subject,
                                     const gchar         **predicate,
                                     const gchar         **object,
                                     gboolean             *object_is_uri,
                                     GError              **error);

GHashTable *tracker_turtle_reader_get_prefixes (TrackerTurtleReader *reader);

#endif /* __TRACKER_TURTLE_READER_H__ */
