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

#ifndef __LIBTRACKER_RESOURCE_H__
#define __LIBTRACKER_RESOURCE_H__

#include <glib-object.h>

#include "tracker-namespace-manager.h"

G_BEGIN_DECLS

#define TRACKER_TYPE_RESOURCE tracker_resource_get_type()
G_DECLARE_DERIVABLE_TYPE (TrackerResource, tracker_resource, TRACKER, RESOURCE, GObject)

struct _TrackerResourceClass
{
	GObjectClass parent_class;
};

TrackerResource *tracker_resource_new (const char *identifier);

void tracker_resource_set_gvalue (TrackerResource *self, const char *property_uri, const GValue *value);
void tracker_resource_set_boolean (TrackerResource *self, const char *property_uri, gboolean value);
void tracker_resource_set_double (TrackerResource *self, const char *property_uri, double value);
void tracker_resource_set_int (TrackerResource *self, const char *property_uri, int value);
void tracker_resource_set_int64 (TrackerResource *self, const char *property_uri, gint64 value);
void tracker_resource_set_relation (TrackerResource *self, const char *property_uri, TrackerResource *resource);
void tracker_resource_set_take_relation (TrackerResource *self, const char *property_uri, TrackerResource *resource);
void tracker_resource_set_string (TrackerResource *self, const char *property_uri, const char *value);
void tracker_resource_set_uri (TrackerResource *self, const char *property_uri, const char *value);

void tracker_resource_add_gvalue (TrackerResource *self, const char *property_uri, const GValue *value);
void tracker_resource_add_boolean (TrackerResource *self, const char *property_uri, gboolean value);
void tracker_resource_add_double (TrackerResource *self, const char *property_uri, double value);
void tracker_resource_add_int (TrackerResource *self, const char *property_uri, int value);
void tracker_resource_add_int64 (TrackerResource *self, const char *property_uri, gint64 value);
void tracker_resource_add_relation (TrackerResource *self, const char *property_uri, TrackerResource *resource);
void tracker_resource_add_take_relation (TrackerResource *self, const char *property_uri, TrackerResource *resource);
void tracker_resource_add_string (TrackerResource *self, const char *property_uri, const char *value);
void tracker_resource_add_uri (TrackerResource *self, const char *property_uri, const char *value);

GList *tracker_resource_get_values (TrackerResource *self, const char *property_uri);

gboolean tracker_resource_get_first_boolean (TrackerResource *self, const char *property_uri);
double tracker_resource_get_first_double (TrackerResource *self, const char *property_uri);
int tracker_resource_get_first_int (TrackerResource *self, const char *property_uri);
gint64 tracker_resource_get_first_int64 (TrackerResource *self, const char *property_uri);
TrackerResource *tracker_resource_get_first_relation (TrackerResource *self, const char *property_uri);
const char *tracker_resource_get_first_string (TrackerResource *self, const char *property_uri);
const char *tracker_resource_get_first_uri (TrackerResource *self, const char *property_uri);

const char *tracker_resource_get_identifier (TrackerResource *self);
void tracker_resource_set_identifier (TrackerResource *self, const char *identifier);
gint tracker_resource_identifier_compare_func (TrackerResource *resource, const char *identifier);

char *tracker_resource_print_turtle(TrackerResource *self, TrackerNamespaceManager *namespaces);

char *tracker_resource_print_sparql_update (TrackerResource *self, TrackerNamespaceManager *namespaces, const char *graph_id);

char *tracker_resource_print_jsonld (TrackerResource *self, TrackerNamespaceManager *namespaces);

G_END_DECLS

#endif /* __LIBTRACKER_RESOURCE_H__ */
