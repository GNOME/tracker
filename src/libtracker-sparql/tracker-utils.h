/*
 * Copyright (C) 2010, Nokia <ivan.frade@nokia.com>
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
#ifndef __TRACKER_UTILS_H__
#define __TRACKER_UTILS_H__

#include <glib.h>
#include <gio/gio.h>
#include <libtracker-sparql/tracker-version.h>

TRACKER_AVAILABLE_IN_ALL
gchar * tracker_sparql_escape_uri_vprintf (const gchar *format,
                                           va_list      args);
TRACKER_AVAILABLE_IN_ALL
gchar * tracker_sparql_escape_uri_printf  (const gchar* format,
                                           ...);
TRACKER_AVAILABLE_IN_ALL
gchar * tracker_sparql_escape_uri         (const gchar *uri);

TRACKER_AVAILABLE_IN_ALL
gchar* tracker_sparql_escape_string (const gchar* literal);
TRACKER_AVAILABLE_IN_ALL
gchar* tracker_sparql_get_uuid_urn (void);

TRACKER_AVAILABLE_IN_ALL
GFile *
tracker_sparql_get_ontology_nepomuk (void);

#endif /* __TRACKER_UTILS_H__ */
