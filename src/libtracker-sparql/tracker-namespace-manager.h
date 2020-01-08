/*
 * Copyright (C) 2016, Sam Thursfield <sam@afuera.me.uk>
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

#ifndef __LIBTRACKER_SPARQL_NAMESPACE_MANAGER_H__
#define __LIBTRACKER_SPARQL_NAMESPACE_MANAGER_H__

#include <glib-object.h>

G_BEGIN_DECLS

#if !defined (__LIBTRACKER_SPARQL_INSIDE__) && !defined (TRACKER_COMPILATION)
#error "only <libtracker-sparql/tracker-sparql.h> must be included directly."
#endif

#include <libtracker-sparql/tracker-version.h>

#define TRACKER_TYPE_NAMESPACE_MANAGER (tracker_namespace_manager_get_type())
TRACKER_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (TrackerNamespaceManager, tracker_namespace_manager, TRACKER, NAMESPACE_MANAGER, GObject)

TRACKER_AVAILABLE_IN_ALL
TrackerNamespaceManager *tracker_namespace_manager_new (void);
TRACKER_AVAILABLE_IN_ALL
TrackerNamespaceManager *tracker_namespace_manager_get_default (void);

TRACKER_AVAILABLE_IN_ALL
char *tracker_namespace_manager_expand_uri (TrackerNamespaceManager *self, const char *compact_uri);

TRACKER_AVAILABLE_IN_ALL
gboolean tracker_namespace_manager_has_prefix (TrackerNamespaceManager *self, const char *prefix);
TRACKER_AVAILABLE_IN_ALL
const char *tracker_namespace_manager_lookup_prefix (TrackerNamespaceManager *self, const char *prefix);

TRACKER_AVAILABLE_IN_ALL
void tracker_namespace_manager_add_prefix (TrackerNamespaceManager *self, const char *prefix, const char *ns);

TRACKER_AVAILABLE_IN_ALL
char *tracker_namespace_manager_print_turtle (TrackerNamespaceManager *self);

TRACKER_AVAILABLE_IN_ALL
void tracker_namespace_manager_foreach (TrackerNamespaceManager *self, GHFunc func, gpointer user_data);

G_END_DECLS

#endif /* __LIBTRACKER_SPARQL_NAMESPACE_MANAGER_H__ */

