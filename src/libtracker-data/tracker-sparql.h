/*
 * Copyright (C) 2008-2010, Nokia
 * Copyright (C) 2018, Red Hat Inc.
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

#ifndef __TRACKER_SPARQL_H__
#define __TRACKER_SPARQL_H__

#if !defined (__LIBTRACKER_DATA_INSIDE__) && !defined (TRACKER_COMPILATION)
#error "only <libtracker-data/tracker-data.h> must be included directly."
#endif

#include <glib.h>
#include "tracker-data-manager.h"

#define TRACKER_TYPE_SPARQL (tracker_sparql_get_type ())
G_DECLARE_FINAL_TYPE (TrackerSparql, tracker_sparql,
                      TRACKER, SPARQL, GObject)

TrackerSparql *       tracker_sparql_new (TrackerDataManager *manager,
                                          const gchar        *sparql);

TrackerSparqlCursor * tracker_sparql_execute_cursor (TrackerSparql  *sparql,
                                                     GHashTable     *parameters,
                                                     GError        **error);

TrackerSparql * tracker_sparql_new_update (TrackerDataManager *manager,
					   const gchar        *query);
GVariant *      tracker_sparql_execute_update (TrackerSparql  *sparql,
					       gboolean        blank,
					       GError        **error);

#endif /* __TRACKER_SPARQL_H__ */
