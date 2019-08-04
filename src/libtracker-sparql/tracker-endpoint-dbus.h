/*
 * Copyright (C) 2019, Red Hat, Inc
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

#ifndef __TRACKER_ENDPOINT_DBUS_H__
#define __TRACKER_ENDPOINT_DBUS_H__

#if !defined (__LIBTRACKER_SPARQL_INSIDE__) && !defined (TRACKER_COMPILATION)
#error "only <libtracker-sparql/tracker-sparql.h> must be included directly."
#endif

#include <libtracker-sparql/tracker-endpoint.h>

#define TRACKER_TYPE_ENDPOINT_DBUS tracker_endpoint_dbus_get_type()
G_DECLARE_FINAL_TYPE (TrackerEndpointDBus, tracker_endpoint_dbus, TRACKER, ENDPOINT_DBUS, TrackerEndpoint)

TrackerEndpointDBus *
tracker_endpoint_dbus_new (TrackerSparqlConnection  *sparql_connection,
                           GDBusConnection          *dbus_connection,
                           const gchar              *object_path,
                           GCancellable             *cancellable,
                           GError                  **error);

#endif /* __TRACKER_ENDPOINT_DBUS_H__ */
