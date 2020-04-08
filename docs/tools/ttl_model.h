/*
 * Copyright (C) 2009, Nokia <ivan.frade@nokia.com>
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

#ifndef __TTL_MODEL_H__
#define __TTL_MODEL_H__

#include <glib.h>

G_BEGIN_DECLS

typedef struct {
	gchar *classname;
	gchar *specification;
	GList *superclasses;
	GList *subclasses;
	GList *in_domain_of;
	GList *in_range_of;
	gchar *description;
	GList *instances;
	gboolean notify;
	gboolean deprecated;
} OntologyClass;

typedef struct {
	gchar *propertyname;
	gchar *specification;
	GList *type;
	GList *domain;
	GList *range;
	GList *superproperties;
	GList *subproperties;
	gchar *max_cardinality;
	gchar *description;
	gboolean deprecated;
        gboolean fulltextIndexed;
        gchar *weight;
} OntologyProperty;

typedef struct {
	gchar *title;
        gchar *description;
	GList *authors;
	GList *editors;
	GList *contributors;
	gchar *gitlog;
	gchar *upstream;
	gchar *copyright;
	gchar *baseUrl;
	gchar *localPrefix;
	gchar *relativePath;
} OntologyDescription;

typedef struct {
	GHashTable *classes;
	GHashTable *properties;
	GHashTable *prefixes;
} Ontology;


OntologyClass *       ttl_model_class_new        (const gchar *classname);
void                  ttl_model_class_free       (OntologyClass *klass);

OntologyDescription * ttl_model_description_new  (void);
void                  ttl_model_description_free (OntologyDescription *desc);

OntologyProperty *    ttl_model_property_new     (const gchar *propname);
void                  ttl_model_property_free    (OntologyProperty *property);

gchar *               ttl_model_name_to_shortname (Ontology    *ontology,
                                                   const gchar *name,
                                                   const gchar *separator);

gchar *               ttl_model_name_to_basename (Ontology    *ontology,
                                                  const gchar *name);
G_END_DECLS

#endif /* __TRACKER_TTL_MODEL_H__ */
