/*
 * Copyright © 2014 Red Hat, Inc
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 *
 * Authors:
 *       Alexander Larsson <alexl@redhat.com>
 *
 * Code borrowed from xdg-desktop-portal/src/xdp-utils.[ch]
 */
#ifndef __TRACKER_PORTAL_UTILS_H__
#define __TRACKER_PORTAL_UTILS_H__

#include <gio/gio.h>

GKeyFile * tracker_invocation_lookup_app_info_sync (GDBusMethodInvocation *invocation,
                                                    GCancellable          *cancellable,
                                                    GError               **error);

#endif /* __TRACKER_PORTAL_UTILS_H__ */
