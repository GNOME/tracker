/*
 * Copyright (C) 2009, Nokia <ivan.frade@nokia.com>
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

#include "config.h"

#include <string.h>
#include <stdlib.h>

#include <glib.h>

#include <libtracker-sparql/tracker-sparql.h>

#include "tracker-namespace.h"
#include "tracker-ontologies.h"
#include "tracker-property.h"

#define XSD_BOOLEAN  TRACKER_PREFIX_XSD "boolean"
#define XSD_DATE     TRACKER_PREFIX_XSD "date"
#define XSD_DATETIME TRACKER_PREFIX_XSD "dateTime"
#define XSD_DOUBLE   TRACKER_PREFIX_XSD "double"
#define XSD_INTEGER  TRACKER_PREFIX_XSD "integer"
#define XSD_STRING   TRACKER_PREFIX_XSD "string"
#define RDF_LANGSTRING TRACKER_PREFIX_RDF "langString"

typedef struct _TrackerPropertyPrivate TrackerPropertyPrivate;

struct _TrackerPropertyPrivate {
	gchar         *uri;
	gchar         *name;
	gchar         *table_name;

	gboolean       use_gvdb;

	TrackerPropertyType  data_type;
	TrackerClass   *domain;
	TrackerClass   *domain_index;
	TrackerClass   *range;
	gint           weight;
	gint           id;
	gboolean       indexed;
	TrackerProperty *secondary_index;
	gboolean       orig_fulltext_indexed;
	gboolean       fulltext_indexed;
	gboolean       multiple_values;
	gboolean       last_multiple_values;
	gboolean       is_inverse_functional_property;
	gboolean       is_new;
	gboolean       db_schema_changed;
	gboolean       writeback;
	gchar         *default_value;
	GPtrArray     *is_new_domain_index;
	gboolean       force_journal;

	GArray        *super_properties;
	GArray        *domain_indexes;
	GArray        *last_super_properties;
	gboolean       cardinality_changed;
	gboolean       orig_multiple_values;

	TrackerOntologies *ontologies;
};

static void property_finalize     (GObject      *object);

GType
tracker_property_type_get_type (void)
{
	static GType etype = 0;

	if (etype == 0) {
		static const GEnumValue values[] = {
			{ TRACKER_PROPERTY_TYPE_UNKNOWN,
			  "TRACKER_PROPERTY_TYPE_UNKNOWN",
			  "unknown" },
			{ TRACKER_PROPERTY_TYPE_STRING,
			  "TRACKER_PROPERTY_TYPE_STRING",
			  "string" },
			{ TRACKER_PROPERTY_TYPE_BOOLEAN,
			  "TRACKER_PROPERTY_TYPE_BOOLEAN",
			  "boolean" },
			{ TRACKER_PROPERTY_TYPE_INTEGER,
			  "TRACKER_PROPERTY_TYPE_INTEGER",
			  "integer" },
			{ TRACKER_PROPERTY_TYPE_DOUBLE,
			  "TRACKER_PROPERTY_TYPE_DOUBLE",
			  "double" },
			{ TRACKER_PROPERTY_TYPE_DATE,
			  "TRACKER_PROPERTY_TYPE_DATE",
			  "date" },
			{ TRACKER_PROPERTY_TYPE_DATETIME,
			  "TRACKER_PROPERTY_TYPE_DATETIME",
			  "datetime" },
			{ TRACKER_PROPERTY_TYPE_RESOURCE,
			  "TRACKER_PROPERTY_TYPE_RESOURCE",
			  "resource" },
			{ TRACKER_PROPERTY_TYPE_LANGSTRING,
			  "TRACKER_PROPERTY_TYPE_LANGSTRING",
			  "langString" },
			{ 0, NULL, NULL }
		};

		etype = g_enum_register_static ("TrackerPropertyType", values);
	}

	return etype;
}

G_DEFINE_TYPE_WITH_PRIVATE (TrackerProperty, tracker_property, G_TYPE_OBJECT);

static void
tracker_property_class_init (TrackerPropertyClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = property_finalize;
}

static void
tracker_property_init (TrackerProperty *property)
{
	TrackerPropertyPrivate *priv;

	priv = tracker_property_get_instance_private (property);

	priv->id = 0;
	priv->weight = 1;
	priv->multiple_values = TRUE;
	priv->force_journal = TRUE;
	priv->super_properties = g_array_new (TRUE, TRUE, sizeof (TrackerProperty *));
	priv->domain_indexes = g_array_new (TRUE, TRUE, sizeof (TrackerClass *));
	priv->last_super_properties = NULL;
	priv->cardinality_changed = FALSE;
}

static void
property_finalize (GObject *object)
{
	TrackerPropertyPrivate *priv;

	priv = tracker_property_get_instance_private (TRACKER_PROPERTY (object));

	g_free (priv->uri);
	g_free (priv->name);
	g_free (priv->table_name);

	if (priv->is_new_domain_index) {
		g_ptr_array_unref (priv->is_new_domain_index);
	}

	if (priv->domain) {
		g_object_unref (priv->domain);
	}

	if (priv->range) {
		g_object_unref (priv->range);
	}

	if (priv->secondary_index) {
		g_object_unref (priv->secondary_index);
	}

	if (priv->last_super_properties) {
		g_array_free (priv->last_super_properties, TRUE);
	}

	g_array_free (priv->super_properties, TRUE);
	g_array_free (priv->domain_indexes, TRUE);

	g_free (priv->default_value);

	(G_OBJECT_CLASS (tracker_property_parent_class)->finalize) (object);
}

/**
 * tracker_property_new:
 *
 * Creates a new #TrackerProperty instance.
 *
 * Returns: The newly created #TrackerProperty
 **/
TrackerProperty *
tracker_property_new (gboolean use_gvdb)
{
	TrackerProperty *property;
	TrackerPropertyPrivate *priv;

	property = g_object_new (TRACKER_TYPE_PROPERTY, NULL);

	if (use_gvdb) {
		priv = tracker_property_get_instance_private (property);
		priv->use_gvdb = use_gvdb;
	}

	return property;
}

const gchar *
tracker_property_get_uri (TrackerProperty *property)
{
	TrackerPropertyPrivate *priv;

	g_return_val_if_fail (TRACKER_IS_PROPERTY (property), NULL);

	priv = tracker_property_get_instance_private (property);

	return priv->uri;
}

const gchar *
tracker_property_get_name (TrackerProperty *property)
{
	TrackerPropertyPrivate *priv;

	g_return_val_if_fail (TRACKER_IS_PROPERTY (property), NULL);

	priv = tracker_property_get_instance_private (property);

	return priv->name;
}

const gchar *
tracker_property_get_table_name (TrackerProperty *property)
{
	TrackerPropertyPrivate *priv;

	g_return_val_if_fail (TRACKER_IS_PROPERTY (property), NULL);

	priv = tracker_property_get_instance_private (property);

	if (!priv->table_name) {
		if (tracker_property_get_multiple_values (property)) {
			priv->table_name = g_strdup_printf ("%s_%s",
				tracker_class_get_name (tracker_property_get_domain (property)),
				tracker_property_get_name (property));
		} else {
			priv->table_name = g_strdup (tracker_class_get_name (tracker_property_get_domain (property)));
		}
	}

	return priv->table_name;
}

TrackerPropertyType
tracker_property_get_data_type (TrackerProperty *property)
{
	TrackerPropertyPrivate *priv;

	g_return_val_if_fail (TRACKER_IS_PROPERTY (property), TRACKER_PROPERTY_TYPE_STRING); //FIXME

	priv = tracker_property_get_instance_private (property);

	if (priv->use_gvdb) {
		const gchar *range_uri;

		range_uri = tracker_ontologies_get_property_string_gvdb (priv->ontologies, priv->uri, "range");
		if (strcmp (range_uri, XSD_STRING) == 0) {
			priv->data_type = TRACKER_PROPERTY_TYPE_STRING;
		} else if (strcmp (range_uri, RDF_LANGSTRING) == 0) {
			priv->data_type = TRACKER_PROPERTY_TYPE_LANGSTRING;
		} else if (strcmp (range_uri, XSD_BOOLEAN) == 0) {
			priv->data_type = TRACKER_PROPERTY_TYPE_BOOLEAN;
		} else if (strcmp (range_uri, XSD_INTEGER) == 0) {
			priv->data_type = TRACKER_PROPERTY_TYPE_INTEGER;
		} else if (strcmp (range_uri, XSD_DOUBLE) == 0) {
			priv->data_type = TRACKER_PROPERTY_TYPE_DOUBLE;
		} else if (strcmp (range_uri, XSD_DATE) == 0) {
			priv->data_type = TRACKER_PROPERTY_TYPE_DATE;
		} else if (strcmp (range_uri, XSD_DATETIME) == 0) {
			priv->data_type = TRACKER_PROPERTY_TYPE_DATETIME;
		} else {
			priv->data_type = TRACKER_PROPERTY_TYPE_RESOURCE;
		}
	}

	return priv->data_type;
}

TrackerClass *
tracker_property_get_domain (TrackerProperty *property)
{
	TrackerPropertyPrivate *priv;

	/* Removed for performance:
	 g_return_val_if_fail (TRACKER_IS_PROPERTY (property), NULL); */

	g_return_val_if_fail (property != NULL, NULL);

	priv = tracker_property_get_instance_private (property);

	if (!priv->domain && priv->use_gvdb) {
		const gchar *domain_uri;

		domain_uri = tracker_ontologies_get_property_string_gvdb (priv->ontologies, priv->uri, "domain");
		priv->domain = g_object_ref (tracker_ontologies_get_class_by_uri (priv->ontologies, domain_uri));
	}

	return priv->domain;
}

TrackerClass **
tracker_property_get_domain_indexes (TrackerProperty *property)
{
	TrackerPropertyPrivate *priv;

	/* Removed for performance:
	 g_return_val_if_fail (TRACKER_IS_PROPERTY (property), NULL); */

	g_return_val_if_fail (property != NULL, NULL);

	priv = tracker_property_get_instance_private (property);

	if (priv->use_gvdb) {
		TrackerClass *domain_index;
		GVariant *variant;

		tracker_property_reset_domain_indexes (property);

		variant = tracker_ontologies_get_property_value_gvdb (priv->ontologies, priv->uri, "domain-indexes");
		if (variant) {
			GVariantIter iter;
			const gchar *uri;

			g_variant_iter_init (&iter, variant);
			while (g_variant_iter_loop (&iter, "&s", &uri)) {
				domain_index = tracker_ontologies_get_class_by_uri (priv->ontologies, uri);

				tracker_property_add_domain_index (property, domain_index);
			}

			g_variant_unref (variant);
		}
	}

	return (TrackerClass ** ) priv->domain_indexes->data;
}

TrackerProperty **
tracker_property_get_last_super_properties (TrackerProperty *property)
{
	TrackerPropertyPrivate *priv;

	g_return_val_if_fail (TRACKER_IS_PROPERTY (property), NULL);
	g_return_val_if_fail (property != NULL, NULL);

	priv = tracker_property_get_instance_private (property);

	return (TrackerProperty **) (priv->last_super_properties ? priv->last_super_properties->data : NULL);
}

void
tracker_property_reset_super_properties (TrackerProperty *property)
{
	TrackerPropertyPrivate *priv;

	g_return_if_fail (TRACKER_IS_PROPERTY (property));

	priv = tracker_property_get_instance_private (property);

	if (priv->last_super_properties) {
		g_array_free (priv->last_super_properties, TRUE);
	}

	priv->last_super_properties = priv->super_properties;
	priv->super_properties = g_array_new (TRUE, TRUE, sizeof (TrackerProperty *));
}


TrackerClass *
tracker_property_get_range (TrackerProperty *property)
{
	TrackerPropertyPrivate *priv;

	g_return_val_if_fail (TRACKER_IS_PROPERTY (property), NULL);

	priv = tracker_property_get_instance_private (property);

	if (!priv->range && priv->use_gvdb) {
		const gchar *range_uri;

		range_uri = tracker_ontologies_get_property_string_gvdb (priv->ontologies, priv->uri, "range");
		priv->range = g_object_ref (tracker_ontologies_get_class_by_uri (priv->ontologies, range_uri));
	}

	return priv->range;
}

gint
tracker_property_get_weight (TrackerProperty *property)
{
	TrackerPropertyPrivate *priv;

	g_return_val_if_fail (TRACKER_IS_PROPERTY (property), -1);

	priv = tracker_property_get_instance_private (property);

	return priv->weight;
}

gint
tracker_property_get_id (TrackerProperty *property)
{
	TrackerPropertyPrivate *priv;

	g_return_val_if_fail (TRACKER_IS_PROPERTY (property), 0);

	priv = tracker_property_get_instance_private (property);

	return priv->id;
}

gboolean
tracker_property_get_indexed (TrackerProperty *property)
{
	TrackerPropertyPrivate *priv;

	g_return_val_if_fail (TRACKER_IS_PROPERTY (property), FALSE);

	priv = tracker_property_get_instance_private (property);

	return priv->indexed;
}

TrackerProperty *
tracker_property_get_secondary_index (TrackerProperty *property)
{
	TrackerPropertyPrivate *priv;

	g_return_val_if_fail (TRACKER_IS_PROPERTY (property), NULL);

	priv = tracker_property_get_instance_private (property);

	return priv->secondary_index;
}

gboolean
tracker_property_get_fulltext_indexed (TrackerProperty *property)
{
	TrackerPropertyPrivate *priv;

	/* Removed for performance:
	 g_return_val_if_fail (TRACKER_IS_PROPERTY (property), NULL); */

	g_return_val_if_fail (property != NULL, FALSE);

	priv = tracker_property_get_instance_private (property);

	if (priv->use_gvdb) {
		GVariant *value;
		gboolean result;

		value = tracker_ontologies_get_property_value_gvdb (priv->ontologies, priv->uri, "fulltext-indexed");
		if (value != NULL) {
			result = g_variant_get_boolean (value);
			g_variant_unref (value);
		} else {
			result = FALSE;
		}

		return result;
	}

	return priv->fulltext_indexed;
}

gboolean
tracker_property_get_orig_fulltext_indexed (TrackerProperty *property)
{
	TrackerPropertyPrivate *priv;

	g_return_val_if_fail (property != NULL, FALSE);

	priv = tracker_property_get_instance_private (property);

	return priv->orig_fulltext_indexed;
}

gboolean
tracker_property_get_is_new (TrackerProperty *property)
{
	TrackerPropertyPrivate *priv;

	g_return_val_if_fail (TRACKER_IS_PROPERTY (property), FALSE);

	priv = tracker_property_get_instance_private (property);

	return priv->is_new;
}

gboolean
tracker_property_get_is_new_domain_index (TrackerProperty *property,
                                          TrackerClass    *class)
{
	TrackerPropertyPrivate *priv;
	guint i;

	g_return_val_if_fail (TRACKER_IS_PROPERTY (property), FALSE);
	g_return_val_if_fail (TRACKER_IS_CLASS (class), FALSE);

	priv = tracker_property_get_instance_private (property);

	if (!priv->is_new_domain_index) {
		return FALSE;
	}

	for (i = 0; i < priv->is_new_domain_index->len; i++) {
		if (g_ptr_array_index (priv->is_new_domain_index, i) == class) {
			return TRUE;
		}
	}

	return FALSE;
}

gboolean
tracker_property_get_writeback (TrackerProperty *property)
{
	TrackerPropertyPrivate *priv;

	g_return_val_if_fail (TRACKER_IS_PROPERTY (property), FALSE);

	priv = tracker_property_get_instance_private (property);

	return priv->writeback;
}

gboolean
tracker_property_get_db_schema_changed (TrackerProperty *property)
{
	TrackerPropertyPrivate *priv;

	g_return_val_if_fail (TRACKER_IS_PROPERTY (property), FALSE);

	priv = tracker_property_get_instance_private (property);

	return priv->db_schema_changed;
}

gboolean
tracker_property_get_cardinality_changed (TrackerProperty *property)
{
	TrackerPropertyPrivate *priv;

	g_return_val_if_fail (TRACKER_IS_PROPERTY (property), FALSE);

	priv = tracker_property_get_instance_private (property);

	return priv->cardinality_changed;
}

gboolean
tracker_property_get_multiple_values (TrackerProperty *property)
{
	TrackerPropertyPrivate *priv;

	g_return_val_if_fail (TRACKER_IS_PROPERTY (property), FALSE);

	priv = tracker_property_get_instance_private (property);

	if (priv->use_gvdb) {
		GVariant *value;
		gboolean result;

		value = tracker_ontologies_get_property_value_gvdb (priv->ontologies, priv->uri, "max-cardinality");
		if (value != NULL) {
			result = FALSE;
			g_variant_unref (value);
		} else {
			result = TRUE;
		}

		return result;
	}

	return priv->multiple_values;
}

gboolean
tracker_property_get_last_multiple_values (TrackerProperty *property)
{
	TrackerPropertyPrivate *priv;

	g_return_val_if_fail (TRACKER_IS_PROPERTY (property), FALSE);

	priv = tracker_property_get_instance_private (property);

	return priv->last_multiple_values;
}

gboolean
tracker_property_get_orig_multiple_values (TrackerProperty *property)
{
	TrackerPropertyPrivate *priv;

	g_return_val_if_fail (TRACKER_IS_PROPERTY (property), FALSE);

	priv = tracker_property_get_instance_private (property);

	return priv->orig_multiple_values;
}

gboolean
tracker_property_get_is_inverse_functional_property (TrackerProperty *property)
{
	TrackerPropertyPrivate *priv;

	g_return_val_if_fail (TRACKER_IS_PROPERTY (property), FALSE);

	priv = tracker_property_get_instance_private (property);

	if (priv->use_gvdb) {
		GVariant *value;
		gboolean result;

		value = tracker_ontologies_get_property_value_gvdb (priv->ontologies, priv->uri, "inverse-functional");
		if (value != NULL) {
			result = g_variant_get_boolean (value);
			g_variant_unref (value);
		} else {
			result = FALSE;
		}

		return result;
	}

	return priv->is_inverse_functional_property;
}

TrackerProperty **
tracker_property_get_super_properties (TrackerProperty *property)
{
	TrackerPropertyPrivate *priv;

	g_return_val_if_fail (TRACKER_IS_PROPERTY (property), NULL);

	priv = tracker_property_get_instance_private (property);

	return (TrackerProperty **) priv->super_properties->data;
}

const gchar *
tracker_property_get_default_value (TrackerProperty *property)
{
	TrackerPropertyPrivate *priv;

	g_return_val_if_fail (TRACKER_IS_PROPERTY (property), NULL);

	priv = tracker_property_get_instance_private (property);

	return priv->default_value;
}

void
tracker_property_set_uri (TrackerProperty *property,
                          const gchar     *value)
{
	TrackerPropertyPrivate *priv;

	g_return_if_fail (TRACKER_IS_PROPERTY (property));

	priv = tracker_property_get_instance_private (property);

	g_free (priv->uri);
	g_free (priv->name);
	priv->uri = NULL;
	priv->name = NULL;

	if (value) {
		TrackerNamespace *namespace;
		gchar *namespace_uri, *hash;

		priv->uri = g_strdup (value);

		hash = strrchr (priv->uri, '#');
		if (hash == NULL) {
			/* support ontologies whose namespace uri does not end in a hash, e.g. dc */
			hash = strrchr (priv->uri, '/');
		}
		if (hash == NULL) {
			g_critical ("Unknown namespace of property %s", priv->uri);
		} else {
			namespace_uri = g_strndup (priv->uri, hash - priv->uri + 1);
			namespace = tracker_ontologies_get_namespace_by_uri (priv->ontologies, namespace_uri);
			if (namespace == NULL) {
				g_critical ("Unknown namespace %s of property %s", namespace_uri, priv->uri);
			} else {
				priv->name = g_strdup_printf ("%s:%s", tracker_namespace_get_prefix (namespace), hash + 1);
			}
			g_free (namespace_uri);
		}
	}
}

void
tracker_property_set_domain (TrackerProperty *property,
                             TrackerClass    *value)
{
	TrackerPropertyPrivate *priv;

	g_return_if_fail (TRACKER_IS_PROPERTY (property));

	priv = tracker_property_get_instance_private (property);

	if (priv->domain) {
		g_object_unref (priv->domain);
		priv->domain = NULL;
	}

	if (value) {
		priv->domain = g_object_ref (value);
	}
}

void
tracker_property_add_domain_index (TrackerProperty *property,
                                   TrackerClass    *value)
{
	TrackerPropertyPrivate *priv;

	g_return_if_fail (TRACKER_IS_PROPERTY (property));
	g_return_if_fail (TRACKER_IS_CLASS (value));

	priv = tracker_property_get_instance_private (property);

	g_array_append_val (priv->domain_indexes, value);
}

void
tracker_property_del_domain_index (TrackerProperty *property,
                                   TrackerClass    *value)
{
	TrackerClass **classes;
	TrackerPropertyPrivate *priv;
	gint i = 0, found = -1;

	g_return_if_fail (TRACKER_IS_PROPERTY (property));
	g_return_if_fail (TRACKER_IS_CLASS (value));

	priv = tracker_property_get_instance_private (property);

	classes = (TrackerClass **) priv->domain_indexes->data;
	while (*classes) {
		if (*classes == value) {
			found = i;
			break;
		}
		i++;
		classes++;
	}

	if (found != -1) {
		g_array_remove_index (priv->domain_indexes, found);
	}
}

void
tracker_property_reset_domain_indexes (TrackerProperty *property)
{
	TrackerPropertyPrivate *priv;

	g_return_if_fail (TRACKER_IS_PROPERTY (property));

	priv = tracker_property_get_instance_private (property);
	g_array_free (priv->domain_indexes, TRUE);
	priv->domain_indexes = g_array_new (TRUE, TRUE, sizeof (TrackerClass *));
}

void
tracker_property_set_secondary_index (TrackerProperty *property,
                                      TrackerProperty *value)
{
	TrackerPropertyPrivate *priv;

	g_return_if_fail (TRACKER_IS_PROPERTY (property));

	priv = tracker_property_get_instance_private (property);

	if (priv->secondary_index) {
		g_object_unref (priv->secondary_index);
		priv->secondary_index = NULL;
	}

	if (value) {
		priv->secondary_index = g_object_ref (value);
	}
}

void
tracker_property_set_range (TrackerProperty *property,
                            TrackerClass     *value)
{
	TrackerPropertyPrivate *priv;
	const gchar *range_uri;

	g_return_if_fail (TRACKER_IS_PROPERTY (property));
	g_return_if_fail (TRACKER_IS_CLASS (value));

	priv = tracker_property_get_instance_private (property);

	if (priv->range) {
		g_object_unref (priv->range);
	}

	priv->range = g_object_ref (value);

	range_uri = tracker_class_get_uri (priv->range);
	if (strcmp (range_uri, XSD_STRING) == 0) {
		priv->data_type = TRACKER_PROPERTY_TYPE_STRING;
	} else if (strcmp (range_uri, RDF_LANGSTRING) == 0) {
		priv->data_type = TRACKER_PROPERTY_TYPE_LANGSTRING;
	} else if (strcmp (range_uri, XSD_BOOLEAN) == 0) {
		priv->data_type = TRACKER_PROPERTY_TYPE_BOOLEAN;
	} else if (strcmp (range_uri, XSD_INTEGER) == 0) {
		priv->data_type = TRACKER_PROPERTY_TYPE_INTEGER;
	} else if (strcmp (range_uri, XSD_DOUBLE) == 0) {
		priv->data_type = TRACKER_PROPERTY_TYPE_DOUBLE;
	} else if (strcmp (range_uri, XSD_DATE) == 0) {
		priv->data_type = TRACKER_PROPERTY_TYPE_DATE;
	} else if (strcmp (range_uri, XSD_DATETIME) == 0) {
		priv->data_type = TRACKER_PROPERTY_TYPE_DATETIME;
	} else {
		priv->data_type = TRACKER_PROPERTY_TYPE_RESOURCE;
	}
}

void
tracker_property_set_weight (TrackerProperty *property,
                             gint             value)
{
	TrackerPropertyPrivate *priv;
	g_return_if_fail (TRACKER_IS_PROPERTY (property));

	priv = tracker_property_get_instance_private (property);

	priv->weight = value;
}


void
tracker_property_set_id (TrackerProperty *property,
                         gint             value)
{
	TrackerPropertyPrivate *priv;
	g_return_if_fail (TRACKER_IS_PROPERTY (property));

	priv = tracker_property_get_instance_private (property);

	priv->id = value;
}

void
tracker_property_set_indexed (TrackerProperty *property,
                              gboolean         value)
{
	TrackerPropertyPrivate *priv;

	g_return_if_fail (TRACKER_IS_PROPERTY (property));

	priv = tracker_property_get_instance_private (property);

	priv->indexed = value;
}

void
tracker_property_set_is_new (TrackerProperty *property,
                             gboolean         value)
{
	TrackerPropertyPrivate *priv;

	g_return_if_fail (TRACKER_IS_PROPERTY (property));

	priv = tracker_property_get_instance_private (property);

	priv->is_new = value;
}

void
tracker_property_set_is_new_domain_index (TrackerProperty *property,
                                          TrackerClass    *class,
                                          gboolean         value)
{
	TrackerPropertyPrivate *priv;

	g_return_if_fail (TRACKER_IS_PROPERTY (property));

	if (class) {
		g_return_if_fail (TRACKER_IS_CLASS (class));
	}

	priv = tracker_property_get_instance_private (property);

	if (value) {
		if (!priv->is_new_domain_index) {
			priv->is_new_domain_index = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
		}
		g_ptr_array_add (priv->is_new_domain_index, g_object_ref (class));
	} else {
		guint i;
		gboolean found = FALSE;

		if (!priv->is_new_domain_index) {
			return;
		}

		if (!class) {
			g_ptr_array_unref (priv->is_new_domain_index);
			priv->is_new_domain_index = NULL;
			return;
		}

		for (i = 0; i < priv->is_new_domain_index->len; i++) {
			if (g_ptr_array_index (priv->is_new_domain_index, i) == class) {
				found = TRUE;
				break;
			}
		}

		if (found) {
			g_ptr_array_remove_index (priv->is_new_domain_index, i);
		}
	}
}

void
tracker_property_set_writeback (TrackerProperty *property,
                                gboolean         value)
{
	TrackerPropertyPrivate *priv;

	g_return_if_fail (TRACKER_IS_PROPERTY (property));

	priv = tracker_property_get_instance_private (property);

	priv->writeback = value;
}

void
tracker_property_set_db_schema_changed (TrackerProperty *property,
                                        gboolean         value)
{
	TrackerPropertyPrivate *priv;

	g_return_if_fail (TRACKER_IS_PROPERTY (property));

	priv = tracker_property_get_instance_private (property);

	priv->db_schema_changed = value;
}

void
tracker_property_set_cardinality_changed (TrackerProperty *property,
                                          gboolean         value)
{
	TrackerPropertyPrivate *priv;

	g_return_if_fail (TRACKER_IS_PROPERTY (property));

	priv = tracker_property_get_instance_private (property);

	priv->cardinality_changed = value;
}

void
tracker_property_set_orig_fulltext_indexed (TrackerProperty *property,
                                            gboolean         value)
{
	TrackerPropertyPrivate *priv;

	g_return_if_fail (TRACKER_IS_PROPERTY (property));

	priv = tracker_property_get_instance_private (property);

	priv->orig_fulltext_indexed = value;
}

void
tracker_property_set_fulltext_indexed (TrackerProperty *property,
                                       gboolean         value)
{
	TrackerPropertyPrivate *priv;

	g_return_if_fail (TRACKER_IS_PROPERTY (property));

	priv = tracker_property_get_instance_private (property);

	priv->fulltext_indexed = value;
}

void
tracker_property_set_multiple_values (TrackerProperty *property,
                                      gboolean         value)
{
	TrackerPropertyPrivate *priv;

	g_return_if_fail (TRACKER_IS_PROPERTY (property));

	priv = tracker_property_get_instance_private (property);

	priv->multiple_values = value;
	g_clear_pointer (&priv->table_name, g_free);
}

void
tracker_property_set_last_multiple_values (TrackerProperty *property,
                                           gboolean         value)
{
	TrackerPropertyPrivate *priv;

	g_return_if_fail (TRACKER_IS_PROPERTY (property));

	priv = tracker_property_get_instance_private (property);

	priv->last_multiple_values = value;
}

void
tracker_property_set_orig_multiple_values (TrackerProperty *property,
                                           gboolean         value)
{
	TrackerPropertyPrivate *priv;

	g_return_if_fail (TRACKER_IS_PROPERTY (property));

	priv = tracker_property_get_instance_private (property);

	priv->orig_multiple_values = value;
}


void
tracker_property_set_is_inverse_functional_property (TrackerProperty *property,
                                                     gboolean         value)
{
	TrackerPropertyPrivate *priv;

	g_return_if_fail (TRACKER_IS_PROPERTY (property));

	priv = tracker_property_get_instance_private (property);

	priv->is_inverse_functional_property = value;
}

void
tracker_property_add_super_property (TrackerProperty *property,
                                     TrackerProperty *value)
{
	TrackerPropertyPrivate *priv;

	g_return_if_fail (TRACKER_IS_PROPERTY (property));
	g_return_if_fail (TRACKER_IS_PROPERTY (value));

	priv = tracker_property_get_instance_private (property);

	g_array_append_val (priv->super_properties, value);
}

void
tracker_property_del_super_property (TrackerProperty *property,
                                     TrackerProperty *value)
{
	TrackerPropertyPrivate *priv;
	guint i;

	g_return_if_fail (TRACKER_IS_PROPERTY (property));
	g_return_if_fail (TRACKER_IS_PROPERTY (value));

	priv = tracker_property_get_instance_private (property);

	for (i = 0; priv->super_properties->len; i++) {
		TrackerProperty *c_value = g_array_index (priv->super_properties, TrackerProperty*, i);

		if (c_value == value) {
			priv->super_properties = g_array_remove_index (priv->super_properties, i);
			return;
		}
	}
}

void
tracker_property_set_default_value (TrackerProperty *property,
                                    const gchar     *value)
{
	TrackerPropertyPrivate *priv;

	g_return_if_fail (TRACKER_IS_PROPERTY (property));

	priv = tracker_property_get_instance_private (property);

	g_free (priv->default_value);
	priv->default_value = g_strdup (value);
}

void
tracker_property_set_ontologies (TrackerProperty   *property,
                                 TrackerOntologies *ontologies)
{
	TrackerPropertyPrivate *priv;

	g_return_if_fail (TRACKER_IS_PROPERTY (property));
	g_return_if_fail (ontologies != NULL);
	priv = tracker_property_get_instance_private (property);

	priv->ontologies = ontologies;
}
