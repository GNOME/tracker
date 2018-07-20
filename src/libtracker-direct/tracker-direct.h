/*
 * Copyright (C) 2010, Nokia <ivan.frade@nokia.com>
 * Copyright (C) 2017, Red Hat, Inc.
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

#ifndef __TRACKER_LOCAL_CONNECTION_H__
#define __TRACKER_LOCAL_CONNECTION_H__

#include <libtracker-sparql/tracker-sparql.h>
#include <libtracker-data/tracker-data-manager.h>

#define TRACKER_TYPE_DIRECT_CONNECTION         (tracker_direct_connection_get_type())
#define TRACKER_DIRECT_CONNECTION(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), TRACKER_TYPE_DIRECT_CONNECTION, TrackerDirectConnection))
#define TRACKER_DIRECT_CONNECTION_CLASS(c)     (G_TYPE_CHECK_CLASS_CAST ((c), TRACKER_TYPE_DIRECT_CONNECTION, TrackerDirectConnectionClass))
#define TRACKER_IS_DIRECT_CONNECTION(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), TRACKER_TYPE_DIRECT_CONNECTION))
#define TRACKER_IS_DIRECT_CONNECTION_CLASS(c)  (G_TYPE_CHECK_CLASS_TYPE ((c),  TRACKER_TYPE_DIRECT_CONNECTION))
#define TRACKER_DIRECT_CONNECTION_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), TRACKER_TYPE_DIRECT_CONNECTION, TrackerDirectConnectionClass))

typedef struct _TrackerDirectConnection TrackerDirectConnection;
typedef struct _TrackerDirectConnectionClass TrackerDirectConnectionClass;

struct _TrackerDirectConnectionClass
{
	TrackerSparqlConnectionClass parent_class;
};

struct _TrackerDirectConnection
{
	TrackerSparqlConnection parent_instance;
};

TrackerDirectConnection *tracker_direct_connection_new (TrackerSparqlConnectionFlags   flags,
                                                        GFile                         *store,
                                                        GFile                         *journal,
                                                        GFile                         *ontology,
                                                        GError                       **error);

TrackerDataManager *tracker_direct_connection_get_data_manager (TrackerDirectConnection *conn);

void tracker_direct_connection_set_default_flags (TrackerDBManagerFlags flags);

void tracker_direct_connection_sync (TrackerDirectConnection *conn);

#endif /* __TRACKER_LOCAL_CONNECTION_H__ */
