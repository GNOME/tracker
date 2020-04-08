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

#include "ttl_model.h"
#include <string.h>

OntologyClass *
ttl_model_class_new (const gchar *classname)
{
	OntologyClass *def = NULL;

	def = g_new0 (OntologyClass, 1);

	def->classname = g_strdup (classname);
	def->superclasses = NULL;
	def->subclasses = NULL;
	def->in_domain_of = NULL;
	def->in_range_of = NULL;
	def->description = NULL;
	def->instances = NULL;
	def->notify = FALSE;
	def->deprecated = FALSE;

	return def;
}

void
ttl_model_class_free (OntologyClass *def)
{
	g_free (def->classname);

	g_list_free_full (def->superclasses, (GDestroyNotify) g_free);
	g_list_free_full (def->subclasses, (GDestroyNotify) g_free);
	g_list_free_full (def->in_domain_of, (GDestroyNotify) g_free);
	g_list_free_full (def->in_range_of, (GDestroyNotify) g_free);

	g_free (def->description);

	g_list_free_full (def->instances, (GDestroyNotify) g_free);

	g_free (def);
}

OntologyProperty *
ttl_model_property_new (const gchar *propname)
{
	OntologyProperty *prop;

	prop = g_new0 (OntologyProperty, 1);

	prop->propertyname = g_strdup (propname);
	prop->type = NULL;
	prop->domain = NULL;
	prop->range = NULL;
	prop->superproperties = NULL;
	prop->subproperties = NULL;
	prop->max_cardinality = NULL;
	prop->description = NULL;
	prop->deprecated = FALSE;
	prop->fulltextIndexed = FALSE ;
	prop->weight = NULL;

	return prop;
}

void
ttl_model_property_free (OntologyProperty *def)
{
	g_free (def->propertyname);

	g_list_free_full (def->type, (GDestroyNotify) g_free);
	g_list_free_full (def->domain, (GDestroyNotify) g_free);
	g_list_free_full (def->range, (GDestroyNotify) g_free);
	g_list_free_full (def->superproperties, (GDestroyNotify) g_free);
	g_list_free_full (def->subproperties, (GDestroyNotify) g_free);

	g_free (def->max_cardinality);
	g_free (def->description);
	g_free (def->weight);
	g_free (def);
}

OntologyDescription *
ttl_model_description_new (void)
{
	OntologyDescription *desc;

	desc = g_new0 (OntologyDescription, 1);
	desc->title = NULL;
	desc->authors = NULL;
	desc->editors = NULL;
	desc->contributors = NULL;
	desc->gitlog = NULL;
	desc->upstream = NULL;
	desc->copyright = NULL;
	desc->baseUrl = NULL;
	desc->localPrefix = NULL;
	desc->relativePath = NULL;
	return desc;
}

void
ttl_model_description_free (OntologyDescription *desc)
{
	g_free (desc->title);
	g_free (desc->description);

	g_list_free_full (desc->authors, (GDestroyNotify) g_free);
	g_list_free_full (desc->editors, (GDestroyNotify) g_free);
	g_list_free_full (desc->contributors, (GDestroyNotify) g_free);

	g_free (desc->gitlog);
	g_free (desc->upstream);
	g_free (desc->copyright);

	g_free (desc->baseUrl);
	g_free (desc->relativePath);
	g_free (desc->localPrefix);

	g_free (desc);
}

static gchar *
name_get_prefix (Ontology    *ontology,
                 const gchar *name)
{
	const gchar *delim;

	delim = g_strrstr (name, "#");

	if (!delim)
		delim = g_strrstr (name, "/");

	if (!delim)
		return NULL;

	delim++;

	return g_strndup (name, delim - name);
}

gchar *
ttl_model_name_to_shortname (Ontology    *ontology,
                             const gchar *name,
                             const gchar *separator)
{
	gchar *prefix, *short_prefix;
	const gchar *suffix;

	if (!separator)
		separator = ":";

	prefix = name_get_prefix (ontology, name);

	if (!prefix)
		return g_strdup (name);

	short_prefix = g_hash_table_lookup (ontology->prefixes, prefix);

	if (!short_prefix) {
		g_free (prefix);
		return g_strdup (name);
	}

	suffix = &name[strlen (prefix)];
	g_free (prefix);

	return g_strconcat (short_prefix, separator, suffix, NULL);
}

gchar *
ttl_model_name_to_basename (Ontology    *ontology,
                            const gchar *name)
{
	g_autofree gchar *prefix = NULL;
	const gchar *suffix;

	prefix = name_get_prefix (ontology, name);

	if (!prefix)
		return g_strdup (name);

	suffix = &name[strlen (prefix)];

	return g_strdup (suffix);
}
