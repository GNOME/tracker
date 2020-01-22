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
#ifndef __TRACKER_PORTAL_H__
#define __TRACKER_PORTAL_H__

#include <gio/gio.h>

#define TRACKER_TYPE_PORTAL tracker_portal_get_type ()
G_DECLARE_FINAL_TYPE (TrackerPortal, tracker_portal, TRACKER, PORTAL, GObject)

TrackerPortal * tracker_portal_new (GDBusConnection  *connection,
                                    GCancellable     *cancellable,
                                    GError          **error);

#endif /* __TRACKER_PORTAL_H__ */
