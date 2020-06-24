/*
 * Copyright (C) 2006, Jamie McCracken <jamiemcc@gnome.org>
 * Copyright (C) 2008, Nokia <ivan.frade@nokia.com>
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

#include <gvdb/gvdb-builder.h>
#include <gvdb/gvdb-reader.h>

#include <libtracker-sparql/tracker-ontologies.h>

#include "tracker-ontologies.h"

typedef struct _TrackerOntologiesPrivate TrackerOntologiesPrivate;

struct _TrackerOntologiesPrivate {
	/* List of TrackerNamespace objects */
	GPtrArray  *namespaces;

	/* Namespace uris */
	GHashTable *namespace_uris;

	/* List of TrackerOntology objects */
	GPtrArray  *ontologies;

	/* Ontology uris */
	GHashTable *ontology_uris;

	/* List of TrackerClass objects */
	GPtrArray  *classes;

	/* Hash (gchar *class_uri, TrackerClass *service) */
	GHashTable *class_uris;

	/* List of TrackerProperty objects */
	GPtrArray  *properties;

	/* Field uris */
	GHashTable *property_uris;

	/* FieldType enum class */
	gpointer    property_type_enum_class;

	/* Hash (int id, const gchar *uri) */
	GHashTable *id_uri_pairs;

	/* rdf:type */
	TrackerProperty *rdf_type;

	GvdbTable *gvdb_table;
	GvdbTable *gvdb_namespaces_table;
	GvdbTable *gvdb_classes_table;
	GvdbTable *gvdb_properties_table;
};

G_DEFINE_TYPE_WITH_PRIVATE (TrackerOntologies, tracker_ontologies, G_TYPE_OBJECT)

static void
tracker_ontologies_init (TrackerOntologies *ontologies)
{
	TrackerOntologiesPrivate *priv = tracker_ontologies_get_instance_private (ontologies);

	priv->namespaces = g_ptr_array_new_with_free_func (g_object_unref);

	priv->ontologies = g_ptr_array_new_with_free_func (g_object_unref);

	priv->namespace_uris = g_hash_table_new_full (g_str_hash,
	                                              g_str_equal,
	                                              g_free,
	                                              g_object_unref);

	priv->ontology_uris = g_hash_table_new_full (g_str_hash,
	                                             g_str_equal,
	                                             g_free,
	                                             g_object_unref);

	priv->classes = g_ptr_array_new_with_free_func (g_object_unref);

	priv->class_uris = g_hash_table_new_full (g_str_hash,
	                                          g_str_equal,
	                                          g_free,
	                                          g_object_unref);

	priv->id_uri_pairs = g_hash_table_new_full (g_direct_hash, g_direct_equal,
	                                            NULL,
	                                            g_free);

	priv->properties = g_ptr_array_new_with_free_func (g_object_unref);

	priv->property_uris = g_hash_table_new_full (g_str_hash,
	                                             g_str_equal,
	                                             g_free,
	                                             g_object_unref);
}

static void
tracker_ontologies_finalize (GObject *object)
{
	TrackerOntologies *ontologies = TRACKER_ONTOLOGIES (object);
	TrackerOntologiesPrivate *priv = tracker_ontologies_get_instance_private (ontologies);

	g_ptr_array_free (priv->namespaces, TRUE);
	g_hash_table_unref (priv->namespace_uris);

	g_ptr_array_free (priv->ontologies, TRUE);
	g_hash_table_unref (priv->ontology_uris);

	g_ptr_array_free (priv->classes, TRUE);
	g_hash_table_unref (priv->class_uris);

	g_hash_table_unref (priv->id_uri_pairs);

	g_ptr_array_free (priv->properties, TRUE);
	g_hash_table_unref (priv->property_uris);

	if (priv->rdf_type) {
		g_object_unref (priv->rdf_type);
	}

	if (priv->gvdb_table) {
		gvdb_table_unref (priv->gvdb_properties_table);
		gvdb_table_unref (priv->gvdb_classes_table);
		gvdb_table_unref (priv->gvdb_namespaces_table);
		gvdb_table_unref (priv->gvdb_table);
	}

	G_OBJECT_CLASS (tracker_ontologies_parent_class)->finalize (object);
}

static void
tracker_ontologies_class_init (TrackerOntologiesClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = tracker_ontologies_finalize;

	/* We will need the class later in order to match strings to enum values
	 * when inserting metadata types in the DB, so the enum class needs to be
	 * created beforehand.
	 */
	g_type_ensure (TRACKER_TYPE_PROPERTY_TYPE);
}

TrackerProperty *
tracker_ontologies_get_rdf_type (TrackerOntologies *ontologies)
{
	TrackerOntologiesPrivate *priv = tracker_ontologies_get_instance_private (ontologies);

	g_return_val_if_fail (priv->rdf_type != NULL, NULL);

	return priv->rdf_type;
}

const gchar*
tracker_ontologies_get_uri_by_id (TrackerOntologies *ontologies,
                                  gint               id)
{
	TrackerOntologiesPrivate *priv = tracker_ontologies_get_instance_private (ontologies);

	g_return_val_if_fail (id != -1, NULL);

	return g_hash_table_lookup (priv->id_uri_pairs, GINT_TO_POINTER (id));
}

void
tracker_ontologies_add_class (TrackerOntologies *ontologies,
                              TrackerClass      *service)
{
	TrackerOntologiesPrivate *priv = tracker_ontologies_get_instance_private (ontologies);
	const gchar *uri;

	g_return_if_fail (TRACKER_IS_CLASS (service));

	uri = tracker_class_get_uri (service);

	g_ptr_array_add (priv->classes, g_object_ref (service));
	tracker_class_set_ontologies (service, ontologies);

	if (uri) {
		g_hash_table_insert (priv->class_uris,
		                     g_strdup (uri),
		                     g_object_ref (service));
	}
}

TrackerClass *
tracker_ontologies_get_class_by_uri (TrackerOntologies *ontologies,
                                     const gchar       *class_uri)
{
	TrackerOntologiesPrivate *priv = tracker_ontologies_get_instance_private (ontologies);
	TrackerClass *class;

	g_return_val_if_fail (class_uri != NULL, NULL);

	class = g_hash_table_lookup (priv->class_uris, class_uri);

	if (!class && priv->gvdb_table) {
		if (tracker_ontologies_get_class_string_gvdb (ontologies, class_uri, "name") != NULL) {
			const gchar *id_str;

			class = tracker_class_new (TRUE);
			tracker_class_set_ontologies (class, ontologies);
			tracker_class_set_uri (class, class_uri);

			id_str = tracker_ontologies_get_class_string_gvdb (ontologies, class_uri, "id");
			if (id_str)
				tracker_class_set_id (class, g_ascii_strtoll (id_str, NULL, 10));

			g_hash_table_insert (priv->class_uris,
				             g_strdup (class_uri),
				             class);
		}
	}

	return class;
}

TrackerNamespace **
tracker_ontologies_get_namespaces (TrackerOntologies *ontologies,
                                   guint             *length)
{
	TrackerOntologiesPrivate *priv = tracker_ontologies_get_instance_private (ontologies);

	if (priv->namespaces->len == 0 && priv->gvdb_table) {
		gchar **namespace_uris;
		gint i;

		namespace_uris = gvdb_table_list (priv->gvdb_namespaces_table, "");

		for (i = 0; namespace_uris[i]; i++) {
			TrackerNamespace *namespace;

			namespace = tracker_ontologies_get_namespace_by_uri (ontologies,
			                                                     namespace_uris[i]);

			g_ptr_array_add (priv->namespaces, g_object_ref (namespace));
			tracker_namespace_set_ontologies (namespace, ontologies);
		}

		g_strfreev (namespace_uris);
	}

	*length = priv->namespaces->len;
	return (TrackerNamespace **) priv->namespaces->pdata;
}

TrackerOntology **
tracker_ontologies_get_ontologies (TrackerOntologies *ontologies,
                                   guint             *length)
{
	TrackerOntologiesPrivate *priv = tracker_ontologies_get_instance_private (ontologies);

	if (G_UNLIKELY (!priv->ontologies)) {
		*length = 0;
		return NULL;
	}

	*length = priv->ontologies->len;
	return (TrackerOntology **) priv->ontologies->pdata;
}

TrackerClass **
tracker_ontologies_get_classes (TrackerOntologies *ontologies,
                                guint             *length)
{
	TrackerOntologiesPrivate *priv = tracker_ontologies_get_instance_private (ontologies);

	if (priv->classes->len == 0 && priv->gvdb_table) {
		gchar **class_uris;
		gint i;

		class_uris = gvdb_table_list (priv->gvdb_classes_table, "");

		for (i = 0; class_uris[i]; i++) {
			TrackerClass *class;

			class = tracker_ontologies_get_class_by_uri (ontologies,class_uris[i]);

			g_ptr_array_add (priv->classes, g_object_ref (class));
			tracker_class_set_ontologies (class, ontologies);
		}

		g_strfreev (class_uris);
	}

	*length = priv->classes->len;
	return (TrackerClass **) priv->classes->pdata;
}

TrackerProperty **
tracker_ontologies_get_properties (TrackerOntologies *ontologies,
                                   guint             *length)
{
	TrackerOntologiesPrivate *priv = tracker_ontologies_get_instance_private (ontologies);

	if (priv->properties->len == 0 && priv->gvdb_table) {
		gchar **property_uris;
		gint i;

		property_uris = gvdb_table_list (priv->gvdb_properties_table, "");

		for (i = 0; property_uris[i]; i++) {
			TrackerProperty *property;

			property = tracker_ontologies_get_property_by_uri (ontologies, property_uris[i]);

			g_ptr_array_add (priv->properties, g_object_ref (property));
			tracker_property_set_ontologies (property, ontologies);
		}

		g_strfreev (property_uris);
	}

	*length = priv->properties->len;
	return (TrackerProperty **) priv->properties->pdata;
}

/* Field mechanics */
void
tracker_ontologies_add_property (TrackerOntologies *ontologies,
                                 TrackerProperty   *field)
{
	TrackerOntologiesPrivate *priv = tracker_ontologies_get_instance_private (ontologies);
	const gchar *uri;

	g_return_if_fail (TRACKER_IS_PROPERTY (field));

	uri = tracker_property_get_uri (field);

	if (g_strcmp0 (uri, TRACKER_PREFIX_RDF "type") == 0) {
		g_set_object (&priv->rdf_type, field);
	}

	g_ptr_array_add (priv->properties, g_object_ref (field));
	tracker_property_set_ontologies (field, ontologies);

	g_hash_table_insert (priv->property_uris,
	                     g_strdup (uri),
	                     g_object_ref (field));
}

void
tracker_ontologies_add_id_uri_pair (TrackerOntologies *ontologies,
                                    gint               id,
                                    const gchar       *uri)
{
	TrackerOntologiesPrivate *priv = tracker_ontologies_get_instance_private (ontologies);

	g_hash_table_insert (priv->id_uri_pairs,
	                     GINT_TO_POINTER (id),
	                     g_strdup (uri));
}

TrackerProperty *
tracker_ontologies_get_property_by_uri (TrackerOntologies *ontologies,
                                        const gchar       *uri)
{
	TrackerOntologiesPrivate *priv = tracker_ontologies_get_instance_private (ontologies);
	TrackerProperty *property;

	g_return_val_if_fail (uri != NULL, NULL);

	property = g_hash_table_lookup (priv->property_uris, uri);

	if (!property && priv->gvdb_table) {
		if (tracker_ontologies_get_property_string_gvdb (ontologies, uri, "name") != NULL) {
			const gchar *id_str;

			property = tracker_property_new (TRUE);
			tracker_property_set_ontologies (property, ontologies);
			tracker_property_set_uri (property, uri);

			id_str = tracker_ontologies_get_property_string_gvdb (ontologies, uri, "id");
			if (id_str)
				tracker_property_set_id (property, g_ascii_strtoll (id_str, NULL, 10));

			g_hash_table_insert (priv->property_uris,
				             g_strdup (uri),
				             property);
		}
	}

	return property;
}

void
tracker_ontologies_add_namespace (TrackerOntologies *ontologies,
                                  TrackerNamespace  *namespace)
{
	TrackerOntologiesPrivate *priv = tracker_ontologies_get_instance_private (ontologies);
	const gchar *uri;

	g_return_if_fail (TRACKER_IS_NAMESPACE (namespace));

	uri = tracker_namespace_get_uri (namespace);

	g_ptr_array_add (priv->namespaces, g_object_ref (namespace));
	tracker_namespace_set_ontologies (namespace, ontologies);

	g_hash_table_insert (priv->namespace_uris,
	                     g_strdup (uri),
	                     g_object_ref (namespace));
}

void
tracker_ontologies_add_ontology (TrackerOntologies *ontologies,
                                 TrackerOntology   *ontology)
{
	TrackerOntologiesPrivate *priv = tracker_ontologies_get_instance_private (ontologies);
	const gchar *uri;

	g_return_if_fail (TRACKER_IS_ONTOLOGY (ontology));

	uri = tracker_ontology_get_uri (ontology);

	g_ptr_array_add (priv->ontologies, g_object_ref (ontology));
	tracker_ontology_set_ontologies (ontology, ontologies);

	g_hash_table_insert (priv->ontology_uris,
	                     g_strdup (uri),
	                     g_object_ref (ontology));
}

TrackerNamespace *
tracker_ontologies_get_namespace_by_uri (TrackerOntologies *ontologies,
                                         const gchar       *uri)
{
	TrackerOntologiesPrivate *priv = tracker_ontologies_get_instance_private (ontologies);
	TrackerNamespace *namespace;

	g_return_val_if_fail (uri != NULL, NULL);

	namespace = g_hash_table_lookup (priv->namespace_uris, uri);

	if (!namespace && priv->gvdb_table) {
		if (tracker_ontologies_get_namespace_string_gvdb (ontologies, uri, "prefix") != NULL) {
			namespace = tracker_namespace_new (TRUE);
			tracker_namespace_set_ontologies (namespace, ontologies);
			tracker_namespace_set_uri (namespace, uri);

			g_hash_table_insert (priv->namespace_uris,
					     g_strdup (uri),
					     namespace);
		}
	}

	return namespace;
}


TrackerOntology *
tracker_ontologies_get_ontology_by_uri (TrackerOntologies *ontologies,
                                        const gchar       *uri)
{
	TrackerOntologiesPrivate *priv = tracker_ontologies_get_instance_private (ontologies);

	g_return_val_if_fail (uri != NULL, NULL);

	return g_hash_table_lookup (priv->ontology_uris, uri);
}

static void
gvdb_hash_table_insert_variant (GHashTable  *table,
                                GvdbItem    *parent,
                                const gchar *uri,
                                const gchar *predicate,
                                GVariant    *value)
{
	gchar *key;
	GvdbItem *item;

	key = g_strdup_printf ("%s#%s", uri, predicate);
	item = gvdb_hash_table_insert (table, key);
	gvdb_item_set_parent (item, parent);
	gvdb_item_set_value (item, value);
	g_free (key);
}

static GvdbItem *
gvdb_hash_table_insert_item (GHashTable *table,
                             GvdbItem   *root,
                             const gchar *uri)
{
	GvdbItem *item;

	item = gvdb_hash_table_insert (table, uri);
	gvdb_item_set_parent (item, root);

	return item;
}

static void
gvdb_hash_table_insert_statement (GHashTable  *table,
                                  GvdbItem    *parent,
                                  const gchar *uri,
                                  const gchar *predicate,
                                  const gchar *value)
{
	gvdb_hash_table_insert_variant (table, parent, uri, predicate, g_variant_new_string (value));
}

gboolean
tracker_ontologies_write_gvdb (TrackerOntologies  *ontologies,
                               const gchar        *filename,
                               GError            **error)
{
	TrackerOntologiesPrivate *priv = tracker_ontologies_get_instance_private (ontologies);
	GHashTable *root_table, *table;
	GvdbItem *root, *item;
	const gchar *uri;
	gboolean retval;
	gchar *str;
	gint i;

	root_table = gvdb_hash_table_new (NULL, NULL);

	table = gvdb_hash_table_new (root_table, "namespaces");
	root = gvdb_hash_table_insert (table, "");
	for (i = 0; i < priv->namespaces->len; i++) {
		TrackerNamespace *namespace;

		namespace = priv->namespaces->pdata[i];
		uri = tracker_namespace_get_uri (namespace);

		item = gvdb_hash_table_insert_item (table, root, uri);

		gvdb_hash_table_insert_statement (table, item, uri, "prefix", tracker_namespace_get_prefix (namespace));
	}
	g_hash_table_unref (table);

	table = gvdb_hash_table_new (root_table, "classes");
	root = gvdb_hash_table_insert (table, "");
	for (i = 0; i < priv->classes->len; i++) {
		TrackerClass *class;
		TrackerClass **super_classes;
		GVariantBuilder builder;

		class = priv->classes->pdata[i];
		uri = tracker_class_get_uri (class);

		item = gvdb_hash_table_insert_item (table, root, uri);

		str = g_strdup_printf ("%d", tracker_class_get_id (class));
		gvdb_hash_table_insert_statement (table, item, uri, "id", str);
		g_free (str);

		gvdb_hash_table_insert_statement (table, item, uri, "name", tracker_class_get_name (class));

		super_classes = tracker_class_get_super_classes (class);
		if (super_classes) {
			g_variant_builder_init (&builder, G_VARIANT_TYPE ("as"));

			while (*super_classes) {
				g_variant_builder_add (&builder, "s", tracker_class_get_uri (*super_classes));
				super_classes++;
			}

			gvdb_hash_table_insert_variant (table, item, uri, "super-classes", g_variant_builder_end (&builder));
		}
	}
	g_hash_table_unref (table);

	table = gvdb_hash_table_new (root_table, "properties");
	root = gvdb_hash_table_insert (table, "");
	for (i = 0; i < priv->properties->len; i++) {
		TrackerProperty *property;
		TrackerClass **domain_indexes;
		GVariantBuilder builder;

		property = priv->properties->pdata[i];
		uri = tracker_property_get_uri (property);

		item = gvdb_hash_table_insert_item (table, root, uri);

		str = g_strdup_printf ("%d", tracker_property_get_id (property));
		gvdb_hash_table_insert_statement (table, item, uri, "id", str);
		g_free (str);

		gvdb_hash_table_insert_statement (table, item, uri, "name", tracker_property_get_name (property));
		gvdb_hash_table_insert_statement (table, item, uri, "domain", tracker_class_get_uri (tracker_property_get_domain (property)));
		gvdb_hash_table_insert_statement (table, item, uri, "range", tracker_class_get_uri (tracker_property_get_range (property)));

		if (!tracker_property_get_multiple_values (property)) {
			gvdb_hash_table_insert_variant (table, item, uri, "max-cardinality", g_variant_new_int32 (1));
		}

		if (tracker_property_get_is_inverse_functional_property (property)) {
			gvdb_hash_table_insert_variant (table, item, uri, "inverse-functional", g_variant_new_boolean (TRUE));
		}

		if (tracker_property_get_fulltext_indexed (property)) {
			gvdb_hash_table_insert_variant (table, item, uri, "fulltext-indexed", g_variant_new_boolean (TRUE));
		}

		domain_indexes = tracker_property_get_domain_indexes (property);
		if (domain_indexes) {
			g_variant_builder_init (&builder, G_VARIANT_TYPE ("as"));

			while (*domain_indexes) {
				g_variant_builder_add (&builder, "s", tracker_class_get_uri (*domain_indexes));
				domain_indexes++;
			}

			gvdb_hash_table_insert_variant (table, item, uri, "domain-indexes", g_variant_builder_end (&builder));
		}
	}
	g_hash_table_unref (table);

	retval = gvdb_table_write_contents (root_table, filename, FALSE, error);

	g_hash_table_unref (root_table);

	return retval;
}

gboolean
tracker_ontologies_load_gvdb (TrackerOntologies  *ontologies,
			      const gchar        *filename,
                              GError            **error)
{
	TrackerOntologiesPrivate *priv;
	GvdbTable *gvdb_table;

	priv = tracker_ontologies_get_instance_private (ontologies);

	gvdb_table = gvdb_table_new (filename, TRUE, error);
	if (!gvdb_table)
		return FALSE;

	priv->gvdb_table = gvdb_table;
	priv->gvdb_namespaces_table = gvdb_table_get_table (priv->gvdb_table, "namespaces");
	priv->gvdb_classes_table = gvdb_table_get_table (priv->gvdb_table, "classes");
	priv->gvdb_properties_table = gvdb_table_get_table (priv->gvdb_table, "properties");
	return TRUE;
}

GVariant *
tracker_ontologies_get_namespace_value_gvdb (TrackerOntologies *ontologies,
                                             const gchar       *uri,
                                             const gchar       *predicate)
{
	TrackerOntologiesPrivate *priv = tracker_ontologies_get_instance_private (ontologies);
	gchar *key;
	GVariant *value;

	key = g_strdup_printf ("%s#%s", uri, predicate);
	value = gvdb_table_get_value (priv->gvdb_namespaces_table, key);
	g_free (key);

	return value;
}

const gchar *
tracker_ontologies_get_namespace_string_gvdb (TrackerOntologies *ontologies,
                                              const gchar       *uri,
                                              const gchar       *predicate)
{
	GVariant *value;
	const gchar *result;

	value = tracker_ontologies_get_namespace_value_gvdb (ontologies, uri, predicate);

	if (value == NULL) {
		return NULL;
	}

	result = g_variant_get_string (value, NULL);
	g_variant_unref (value);

	return result;
}

GVariant *
tracker_ontologies_get_class_value_gvdb (TrackerOntologies *ontologies,
                                         const gchar       *uri,
                                         const gchar       *predicate)
{
	TrackerOntologiesPrivate *priv = tracker_ontologies_get_instance_private (ontologies);
	gchar *key;
	GVariant *value;

	key = g_strdup_printf ("%s#%s", uri, predicate);
	value = gvdb_table_get_value (priv->gvdb_classes_table, key);
	g_free (key);

	return value;
}

const gchar *
tracker_ontologies_get_class_string_gvdb (TrackerOntologies *ontologies,
                                          const gchar       *uri,
                                          const gchar       *predicate)
{
	GVariant *value;
	const gchar *result;

	value = tracker_ontologies_get_class_value_gvdb (ontologies, uri, predicate);

	if (value == NULL) {
		return NULL;
	}

	result = g_variant_get_string (value, NULL);
	g_variant_unref (value);

	return result;
}

GVariant *
tracker_ontologies_get_property_value_gvdb (TrackerOntologies *ontologies,
                                            const gchar       *uri,
                                            const gchar       *predicate)
{
	TrackerOntologiesPrivate *priv = tracker_ontologies_get_instance_private (ontologies);
	gchar *key;
	GVariant *value;

	key = g_strdup_printf ("%s#%s", uri, predicate);
	value = gvdb_table_get_value (priv->gvdb_properties_table, key);
	g_free (key);

	return value;
}

const gchar *
tracker_ontologies_get_property_string_gvdb (TrackerOntologies *ontologies,
                                             const gchar       *uri,
                                             const gchar       *predicate)
{
	GVariant *value;
	const gchar *result;

	value = tracker_ontologies_get_property_value_gvdb (ontologies, uri, predicate);

	if (value == NULL) {
		return NULL;
	}

	result = g_variant_get_string (value, NULL);
	g_variant_unref (value);

	return result;
}

static gint
class_sort_func (gconstpointer a,
                 gconstpointer b)
{
	return g_strcmp0 (tracker_class_get_name (*((TrackerClass **) a)),
	                  tracker_class_get_name (*((TrackerClass **) b)));
}

void
tracker_ontologies_sort (TrackerOntologies *ontologies)
{
	TrackerOntologiesPrivate *priv = tracker_ontologies_get_instance_private (ontologies);

	/* Sort result so it is alphabetical */
	g_ptr_array_sort (priv->classes, class_sort_func);
}

TrackerOntologies *
tracker_ontologies_new (void)
{
	return g_object_new (TRACKER_TYPE_ONTOLOGIES, NULL);
}
