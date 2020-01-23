/*
 * Copyright (C) 2020, Red Hat Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 * Author: Carlos Garnacho <carlosg@gnome.org>
 */
#ifndef __TRACKER_PORTAL_ENDPOINT_H__
#define __TRACKER_PORTAL_ENDPOINT_H__

#include <gio/gio.h>

#define TRACKER_TYPE_PORTAL_ENDPOINT tracker_portal_endpoint_get_type ()
#define TRACKER_PORTAL_ENDPOINT(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), TRACKER_TYPE_PORTAL_ENDPOINT, TrackerPortalEndpoint))
#define TRACKER_PORTAL_ENDPOINT_CLASS(c)     (G_TYPE_CHECK_CLASS_CAST ((c), TRACKER_TYPE_PORTAL_ENDPOINT, TrackerPortalEndpointClass))
#define TRACKER_IS_PORTAL_ENDPOINT(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), TRACKER_TYPE_PORTAL_ENDPOINT))
#define TRACKER_IS_PORTAL_ENDPOINT_CLASS(c)  (G_TYPE_CHECK_CLASS_TYPE ((c),  TRACKER_TYPE_PORTAL_ENDPOINT))
#define TRACKER_PORTAL_ENDPOINT_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), TRACKER_TYPE_PORTAL_ENDPOINT, TrackerPortalEndpointClass))

typedef struct _TrackerPortalEndpoint TrackerPortalEndpoint;

TrackerEndpoint * tracker_portal_endpoint_new (TrackerSparqlConnection  *sparql_connection,
                                               GDBusConnection          *dbus_connection,
                                               const gchar              *object_path,
                                               const gchar              *peer,
                                               const gchar * const      *graphs,
                                               GCancellable             *cancellable,
                                               GError                  **error);

#endif /* __TRACKER_PORTAL_ENDPOINT_H__ */
