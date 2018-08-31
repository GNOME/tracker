/*
 * Copyright (C) 2006, Jamie McCracken <jamiemcc@gnome.org>
 * Copyright (C) 2007, Jason Kivlighn <jkivlighn@gmail.com>
 * Copyright (C) 2007, Creative Commons <http://creativecommons.org>
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

#ifndef __LIBTRACKER_DATA_QUERY_H__
#define __LIBTRACKER_DATA_QUERY_H__

#include <glib.h>

#include "tracker-db-interface.h"
#include "tracker-data-manager.h"

G_BEGIN_DECLS

#if !defined (__LIBTRACKER_DATA_INSIDE__) && !defined (TRACKER_COMPILATION)
#error "only <libtracker-data/tracker-data.h> must be included directly."
#endif

gint                 tracker_data_query_resource_id   (TrackerDataManager  *manager,
                                                       TrackerDBInterface  *iface,
                                                       const gchar         *uri);
gchar               *tracker_data_query_unused_uuid   (TrackerDataManager  *manager,
                                                       TrackerDBInterface  *iface);

TrackerDBCursor     *tracker_data_query_sparql_cursor (TrackerDataManager  *manager,
                                                       const gchar         *query,
                                                       GError             **error);

GPtrArray*           tracker_data_query_rdf_type      (TrackerDataManager *manager,
                                                       gint                id);

G_END_DECLS

#endif /* __LIBTRACKER_DATA_QUERY_H__ */
