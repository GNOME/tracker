/*
 * Copyright (C) 2014, Lanedo <martyn@lanedo.com>
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
 */

#ifndef __TRACKER_SPARQL_H__
#define __TRACKER_SPARQL_H__

#include <glib.h>

#include <libtracker-sparql/tracker-sparql.h>

int         tracker_sparql               (int                       argc,
                                          const char              **argv);

GHashTable *tracker_sparql_get_prefixes  (TrackerSparqlConnection  *connection);
gchar *     tracker_sparql_get_longhand  (GHashTable               *prefixes,
                                          const gchar              *shorthand);
gchar *     tracker_sparql_get_shorthand (GHashTable               *prefixes,
                                          const gchar              *longhand);

#endif /* __TRACKER_SPARQL_H__ */
