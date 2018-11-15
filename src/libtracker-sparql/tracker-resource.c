/*
 * Copyright (C) 2016-2017, Sam Thursfield <sam@afuera.me.uk>
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

#include <glib.h>
#include <json-glib/json-glib.h>

#include <string.h>

#include <tracker-uri.h>
#include <tracker-resource.h>
#include <tracker-ontologies.h>

/* For tracker_sparql_escape_string */
#include "tracker-generated-no-checks.h"

typedef struct {
	char *identifier;
	GHashTable *properties;
	GHashTable *overwrite;
} TrackerResourcePrivate;

G_DEFINE_TYPE_WITH_PRIVATE (TrackerResource, tracker_resource, G_TYPE_OBJECT);
#define GET_PRIVATE(object)  (tracker_resource_get_instance_private (object))

/**
 * SECTION: tracker-resource
 * @short_description: Represents a single Tracker resource
 * @title: TrackerResource
 * @stability: Stable
 * @include: tracker-resource.h
 *
 * <para>
 * #TrackerResource keeps track of a set of properties for a given resource.
 * The resulting data can be serialized in several ways.
 * </para>
 */

static char *
generate_blank_node_identifier (void)
{
	static gint64 counter = 0;

	return g_strdup_printf("_:%" G_GINT64_FORMAT, counter++);
}

/**
 * TrackerResource:
 *
 * The <structname>TrackerResource</structname> object represents information
 * about a given resource.
 */

enum {
	PROP_0,

	PROP_IDENTIFIER,
};

static void constructed  (GObject *object);
static void finalize     (GObject *object);
static void get_property (GObject    *object,
                          guint       param_id,
                          GValue     *value,
                          GParamSpec *pspec);
static void set_property (GObject      *object,
                          guint         param_id,
                          const GValue *value,
                          GParamSpec   *pspec);


static void
tracker_resource_class_init (TrackerResourceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->constructed  = constructed;
	object_class->finalize     = finalize;
	object_class->get_property = get_property;
	object_class->set_property = set_property;

	/**
	 * TrackerResource:identifier
	 *
	 * The URI identifier for this class, or %NULL for a
	 * blank node.
	 */
	g_object_class_install_property (object_class,
	                                 PROP_IDENTIFIER,
	                                 g_param_spec_string ("identifier",
	                                                      "Identifier",
	                                                      "Identifier",
	                                                      NULL,
	                                                      G_PARAM_READWRITE));
}

/* Destroy-notify function for the values stored in the hash table. */
static void
free_value (GValue *value)
{
	g_value_unset (value);
	g_slice_free (GValue, value);
}

static void
tracker_resource_init (TrackerResource *resource)
{
	TrackerResourcePrivate *priv = GET_PRIVATE(resource);

	/* Values of properties */
	priv->properties = g_hash_table_new_full (
	        g_str_hash,
	        g_str_equal,
	        g_free,
	        (GDestroyNotify) free_value);

	/* TRUE for any property where we should delete any existing values. */
	priv->overwrite = g_hash_table_new_full (
	        g_str_hash,
	        g_str_equal,
	        g_free,
	        NULL);
}

static void
constructed (GObject *object)
{
	TrackerResourcePrivate *priv;

	priv = GET_PRIVATE (TRACKER_RESOURCE (object));

	if (! priv->identifier) {
		priv->identifier = generate_blank_node_identifier ();
	}

	G_OBJECT_CLASS (tracker_resource_parent_class)->constructed (object);
}

static void
finalize (GObject *object)
{
	TrackerResourcePrivate *priv;

	priv = GET_PRIVATE (TRACKER_RESOURCE (object));

	if (priv->identifier) {
		g_free (priv->identifier);
	}

	g_hash_table_unref (priv->overwrite);
	g_hash_table_unref (priv->properties);

	(G_OBJECT_CLASS (tracker_resource_parent_class)->finalize)(object);
}

static void
get_property (GObject    *object,
              guint       param_id,
              GValue     *value,
              GParamSpec *pspec)
{
	TrackerResourcePrivate *priv;

	priv = GET_PRIVATE (TRACKER_RESOURCE (object));

	switch (param_id) {
	case PROP_IDENTIFIER:
		g_value_set_string (value, priv->identifier);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
		break;
	}
}

static void
set_property (GObject      *object,
              guint         param_id,
              const GValue *value,
              GParamSpec   *pspec)
{
	switch (param_id) {
	case PROP_IDENTIFIER:
		tracker_resource_set_identifier (TRACKER_RESOURCE (object), g_value_get_string (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
		break;
	}
}

/**
 * tracker_resource_new:
 * @identifier: A string containing a URI
 *
 * Creates a TrackerResource instance.
 *
 * Returns: a newly created #TrackerResource. Free with g_object_unref() when done
 *
 * Since: 1.10
 */
TrackerResource *
tracker_resource_new (const char *identifier)
{
	TrackerResource *resource;

	resource = g_object_new (TRACKER_TYPE_RESOURCE,
	                         "identifier", identifier,
	                         NULL);

	return resource;
}

/* Difference between 'set' and 'add': when generating a SPARQL update, the
 * setter will generate a corresponding DELETE, the adder will not. The setter
 * will also overwrite existing values in the Resource object, while the adder
 * will make a list.
 */

/**
 * tracker_resource_set_gvalue:
 * @self: the #TrackerResource
 * @property_uri: a string identifying the property to set
 * @value: an initialised #GValue
 *
 * State that the only value for the given property is 'value'. Any existing
 * values for 'property' will be removed.
 *
 * When serialising to SPARQL, any properties that were set with this function
 * will get a corresponding DELETE statement to remove any existing values in
 * the database.
 *
 * You can pass any kind of GValue for @value, but serialization functions will
 * normally only be able to serialize URIs/relationships and fundamental value
 * types (string, int, etc.).
 *
 * Since: 1.10
 */
void
tracker_resource_set_gvalue (TrackerResource *self,
                             const char      *property_uri,
                             const GValue    *value)
{
	TrackerResourcePrivate *priv;
	GValue *our_value;

	g_return_if_fail (TRACKER_IS_RESOURCE (self));
	g_return_if_fail (property_uri != NULL);
	g_return_if_fail (G_IS_VALUE (value));

	priv = GET_PRIVATE (self);

	our_value = g_slice_new0 (GValue);
	g_value_init (our_value, G_VALUE_TYPE (value));
	g_value_copy (value, our_value);

	g_hash_table_insert (priv->properties, g_strdup (property_uri), our_value);

	g_hash_table_insert (priv->overwrite, g_strdup (property_uri), GINT_TO_POINTER (TRUE));
};

static gboolean
validate_boolean (gboolean    value,
                  const char *func_name) {
	return TRUE;
}

static gboolean
validate_double (double      value,
                 const char *func_name) {
	return TRUE;
}

static gboolean
validate_int (int         value,
              const char *func_name) {
	return TRUE;
}

static gboolean
validate_int64 (gint64      value,
                const char *func_name) {
	return TRUE;
}

static gboolean
validate_pointer (const void *pointer,
                  const char *func_name)
{
	if (pointer == NULL) {
		g_warning ("%s: NULL is not a valid value.", func_name);
		return FALSE;
	}

	return TRUE;
}

#define SET_PROPERTY_FOR_GTYPE(name, ctype, gtype, set_function, validate_function) \
	void name (TrackerResource *self,                                           \
	           const char *property_uri,                                        \
	           ctype value)                                                     \
	{                                                                           \
		TrackerResourcePrivate *priv;                                       \
		GValue *our_value;                                                  \
                                                                                    \
		g_return_if_fail (TRACKER_IS_RESOURCE (self));                      \
		g_return_if_fail (property_uri != NULL);                            \
                                                                                    \
		priv = GET_PRIVATE (self);                                          \
                                                                                    \
		if (!validate_function (value, __func__)) {                         \
			return;                                                     \
		}                                                                   \
                                                                                    \
		our_value = g_slice_new0 (GValue);                                  \
		g_value_init (our_value, gtype);                                    \
		set_function (our_value, value);                                    \
                                                                                    \
		g_hash_table_insert (priv->properties,                              \
		                     g_strdup (property_uri),                       \
		                     our_value);                                    \
                                                                                    \
		g_hash_table_insert (priv->overwrite,                               \
		                     g_strdup (property_uri),                       \
		                     GINT_TO_POINTER (TRUE));                       \
	};

/**
 * tracker_resource_set_boolean:
 * @self: the #TrackerResource
 * @property_uri: a string identifying the property to modify
 * @value: the property object
 *
 * Sets a single-valued boolean object.
 *
 * Since: 1.10
 */
SET_PROPERTY_FOR_GTYPE (tracker_resource_set_boolean, gboolean, G_TYPE_BOOLEAN, g_value_set_boolean, validate_boolean);

/**
 * tracker_resource_set_double:
 * @self: the #TrackerResource
 * @property_uri: a string identifying the property to modify
 * @value: the property object
 *
 * Sets a single-valued double object.
 *
 * Since: 1.10
 */
SET_PROPERTY_FOR_GTYPE (tracker_resource_set_double, double, G_TYPE_DOUBLE, g_value_set_double, validate_double);

/**
 * tracker_resource_set_int:
 * @self: the #TrackerResource
 * @property_uri: a string identifying the property to modify
 * @value: the property object
 *
 * Sets a single-valued integer object.
 *
 * Since: 1.10
 */
SET_PROPERTY_FOR_GTYPE (tracker_resource_set_int, int, G_TYPE_INT, g_value_set_int, validate_int);

/**
 * tracker_resource_set_int64:
 * @self: the #TrackerResource
 * @property_uri: a string identifying the property to modify
 * @value: the property object
 *
 * Sets a single-valued integer object.
 *
 * Since: 1.10
 */
SET_PROPERTY_FOR_GTYPE (tracker_resource_set_int64, gint64, G_TYPE_INT64, g_value_set_int64, validate_int64);

/**
 * tracker_resource_set_relation:
 * @self: the #TrackerResource
 * @property_uri: a string identifying the property to modify
 * @resource: the property object
 *
 * Sets a single-valued resource object as a #TrackerResource. This
 * function produces similar RDF to tracker_resource_set_uri(),
 * although in this function the URI will depend on the identifier
 * set on @resource.
 *
 * Since: 1.10
 */
SET_PROPERTY_FOR_GTYPE (tracker_resource_set_relation, TrackerResource *, TRACKER_TYPE_RESOURCE, g_value_set_object, validate_pointer);

/**
 * tracker_resource_set_take_relation: (skip)
 * @self: the #TrackerResource
 * @property_uri: a string identifying the property to modify
 * @resource: the property object
 *
 * Sets a single-valued resource object as a #TrackerResource. This
 * function produces similar RDF to tracker_resource_set_uri(),
 * although in this function the URI will depend on the identifier
 * set on @resource. This function takes ownership of @resource.
 *
 * Since: 2.0
 */
SET_PROPERTY_FOR_GTYPE (tracker_resource_set_take_relation, TrackerResource *, TRACKER_TYPE_RESOURCE, g_value_take_object, validate_pointer);

/**
 * tracker_resource_set_string:
 * @self: the #TrackerResource
 * @property_uri: a string identifying the property to modify
 * @value: the property object
 *
 * Sets a single-valued string object.
 *
 * Since: 1.10
 */
SET_PROPERTY_FOR_GTYPE (tracker_resource_set_string, const char *, G_TYPE_STRING, g_value_set_string, validate_pointer);

/**
 * tracker_resource_set_uri:
 * @self: the #TrackerResource
 * @property_uri: a string identifying the property to modify
 * @value: the property object
 *
 * Sets a single-valued resource object as a string URI. This function
 * produces similar RDF to tracker_resource_set_relation(), although
 * it requires that the URI is previously known.
 *
 * Since: 1.10
 */
SET_PROPERTY_FOR_GTYPE (tracker_resource_set_uri, const char *, TRACKER_TYPE_URI, g_value_set_string, validate_pointer);

/**
 * tracker_resource_add_gvalue:
 * @self: the #TrackerResource
 * @property_uri: a string identifying the property to set
 * @value: an initialised #GValue
 *
 * Add 'value' to the list of values for given property.
 *
 * You can pass any kind of GValue for @value, but serialization functions will
 * normally only be able to serialize URIs/relationships and fundamental value
 * types (string, int, etc.).
 *
 * Since: 1.10
 */
void
tracker_resource_add_gvalue (TrackerResource *self,
                             const char      *property_uri,
                             const GValue    *value)
{
	TrackerResourcePrivate *priv;
	GValue *existing_value, *array_holder, *our_value;
	GPtrArray *array;

	g_return_if_fail (TRACKER_IS_RESOURCE (self));
	g_return_if_fail (property_uri != NULL);
	g_return_if_fail (G_IS_VALUE (value));

	priv = GET_PRIVATE (self);

	existing_value = g_hash_table_lookup (priv->properties, property_uri);

	if (existing_value && G_VALUE_HOLDS (existing_value, G_TYPE_PTR_ARRAY)) {
		array = g_value_get_boxed (existing_value);
		array_holder = existing_value;
	} else {
		array = g_ptr_array_new_with_free_func ((GDestroyNotify)free_value);
		array_holder = g_slice_new0 (GValue);
		g_value_init (array_holder, G_TYPE_PTR_ARRAY);
		g_value_take_boxed (array_holder, array);

		if (existing_value) {
			/* The existing_value is owned by the hash table and will be freed
			 * when we overwrite it with array_holder, so we need to allocate a
			 * new value and give it to the ptrarray.
			 */
			our_value = g_slice_new0 (GValue);
			g_value_init (our_value, G_VALUE_TYPE (existing_value));
			g_value_copy (existing_value, our_value);
			g_ptr_array_add (array, our_value);
		}
	}

	our_value = g_slice_new0 (GValue);
	g_value_init (our_value, G_VALUE_TYPE (value));
	g_value_copy (value, our_value);

	g_ptr_array_add (array, our_value);

	if (array_holder != existing_value) {
		g_hash_table_insert (priv->properties, g_strdup (property_uri), array_holder);
	}
};

#define ADD_PROPERTY_FOR_GTYPE(name, ctype, gtype, set_function, validate_function)     \
	void name (TrackerResource *self,                                               \
	           const char *property_uri,                                            \
	           ctype value)                                                         \
	{                                                                               \
		TrackerResourcePrivate *priv;                                           \
		GValue *existing_value, *array_holder, *our_value;                      \
		GPtrArray *array;                                                       \
                                                                                        \
		g_return_if_fail (TRACKER_IS_RESOURCE (self));                          \
		g_return_if_fail (property_uri != NULL);                                \
                                                                                        \
		priv = GET_PRIVATE (self);                                              \
                                                                                        \
		if (!validate_function (value, __func__)) {                             \
			return;                                                         \
		}                                                                       \
                                                                                        \
		existing_value = g_hash_table_lookup (priv->properties,                 \
		                                      property_uri);                    \
                                                                                        \
		if (existing_value && G_VALUE_HOLDS (existing_value,                    \
		                                     G_TYPE_PTR_ARRAY)) {               \
			array = g_value_get_boxed (existing_value);                     \
			array_holder = existing_value;                                  \
		} else {                                                                \
			array = g_ptr_array_new_with_free_func (                        \
			        (GDestroyNotify)free_value);                            \
			array_holder = g_slice_new0 (GValue);                           \
			g_value_init (array_holder, G_TYPE_PTR_ARRAY);                  \
			g_value_take_boxed (array_holder, array);                       \
                                                                                        \
			if (existing_value) {                                           \
				/* existing_value is owned by hash table */             \
				our_value = g_slice_new0 (GValue);                      \
				g_value_init (our_value, G_VALUE_TYPE(existing_value)); \
				g_value_copy (existing_value, our_value);               \
				g_ptr_array_add (array, our_value);                     \
			}                                                               \
		}                                                                       \
                                                                                        \
		our_value = g_slice_new0 (GValue);                                      \
		g_value_init (our_value, gtype);                                        \
		set_function (our_value, value);                                        \
                                                                                        \
		g_ptr_array_add (array, our_value);                                     \
                                                                                        \
		if (array_holder != existing_value) {                                   \
			g_hash_table_insert (priv->properties,                          \
			                     g_strdup (property_uri), array_holder);    \
		}                                                                       \
	};

/**
 * tracker_resource_add_boolean:
 * @self: the #TrackerResource
 * @property_uri: a string identifying the property to modify
 * @value: the property object
 *
 * Adds a boolean object to a multi-valued property.
 *
 * Since: 1.10
 */
ADD_PROPERTY_FOR_GTYPE (tracker_resource_add_boolean, gboolean, G_TYPE_BOOLEAN, g_value_set_boolean, validate_boolean);

/**
 * tracker_resource_add_double:
 * @self: the #TrackerResource
 * @property_uri: a string identifying the property to modify
 * @value: the property object
 *
 * Adds a double object to a multi-valued property.
 *
 * Since: 1.10
 */
ADD_PROPERTY_FOR_GTYPE (tracker_resource_add_double, double, G_TYPE_DOUBLE, g_value_set_double, validate_double);

/**
 * tracker_resource_add_int:
 * @self: the #TrackerResource
 * @property_uri: a string identifying the property to modify
 * @value: the property object
 *
 * Adds an integer object to a multi-valued property.
 *
 * Since: 1.10
 */
ADD_PROPERTY_FOR_GTYPE (tracker_resource_add_int, int, G_TYPE_INT, g_value_set_int, validate_int);

/**
 * tracker_resource_add_int64:
 * @self: the #TrackerResource
 * @property_uri: a string identifying the property to modify
 * @value: the property object
 *
 * Adds an integer object to a multi-valued property.
 *
 * Since: 1.10
 */
ADD_PROPERTY_FOR_GTYPE (tracker_resource_add_int64, gint64, G_TYPE_INT64, g_value_set_int64, validate_int64);

/**
 * tracker_resource_add_relation:
 * @self: the #TrackerResource
 * @property_uri: a string identifying the property to modify
 * @resource: the property object
 *
 * Adds a resource object to a multi-valued property. This
 * function produces similar RDF to tracker_resource_add_uri(),
 * although in this function the URI will depend on the identifier
 * set on @resource.
 *
 * Since: 1.10
 */
ADD_PROPERTY_FOR_GTYPE (tracker_resource_add_relation, TrackerResource *, TRACKER_TYPE_RESOURCE, g_value_set_object, validate_pointer);

/**
 * tracker_resource_add_take_relation: (skip)
 * @self: the #TrackerResource
 * @property_uri: a string identifying the property to modify
 * @resource: the property object
 *
 * Adds a resource object to a multi-valued property. This
 * function produces similar RDF to tracker_resource_add_uri(),
 * although in this function the URI will depend on the identifier
 * set on @resource. This function takes ownership of @resource.
 *
 * Since: 2.0
 */
ADD_PROPERTY_FOR_GTYPE (tracker_resource_add_take_relation, TrackerResource *, TRACKER_TYPE_RESOURCE, g_value_take_object, validate_pointer);


/**
 * tracker_resource_add_string:
 * @self: the #TrackerResource
 * @property_uri: a string identifying the property to modify
 * @value: the property object
 *
 * Adds a string object to a multi-valued property.
 *
 * Since: 1.10
 */
ADD_PROPERTY_FOR_GTYPE (tracker_resource_add_string, const char *, G_TYPE_STRING, g_value_set_string, validate_pointer);

/**
 * tracker_resource_add_uri:
 * @self: the #TrackerResource
 * @property_uri: a string identifying the property to modify
 * @value: the property object
 *
 * Adds a resource object to a multi-valued property. This function
 * produces similar RDF to tracker_resource_add_relation(), although
 * it requires that the URI is previously known.
 *
 * Since: 1.10
 */
ADD_PROPERTY_FOR_GTYPE (tracker_resource_add_uri, const char *, TRACKER_TYPE_URI, g_value_set_string, validate_pointer);


/**
 * tracker_resource_get_values:
 * @self: the #TrackerResource
 * @property_uri: a string identifying the property to look up
 *
 * Returns the list of all known values of the given property.
 *
 * Returns: (transfer full) (element-type GValue): a #GList of #GValue
 * instances, which must be freed by the caller.
 *
 * Since: 1.10
 */
GList *tracker_resource_get_values (TrackerResource *self,
                                    const char      *property_uri)
{
	TrackerResourcePrivate *priv;
	GValue *value;

	g_return_val_if_fail (TRACKER_IS_RESOURCE (self), NULL);
	g_return_val_if_fail (property_uri, NULL);

	priv = GET_PRIVATE (self);

	value = g_hash_table_lookup (priv->properties, property_uri);

	if (value == NULL) {
		return NULL;
	}

	if (G_VALUE_HOLDS (value, G_TYPE_PTR_ARRAY)) {
		GList *result = NULL;
		GPtrArray *array;
		int i;

		array = g_value_get_boxed (value);

		for (i = 0; i < array->len; i++) {
			value = g_ptr_array_index (array, i);
			result = g_list_prepend (result, value);
		};

		return g_list_reverse (result);
	} else {
		return g_list_append (NULL, value);
	}
}

#define GET_PROPERTY_FOR_GTYPE(name, ctype, gtype, get_function, no_value)    \
	ctype name (TrackerResource *self,                                    \
	            const char *property_uri)                                 \
	{                                                                     \
		TrackerResourcePrivate *priv;                                 \
		GValue *value;                                                \
                                                                              \
		g_return_val_if_fail (TRACKER_IS_RESOURCE (self), no_value);  \
		g_return_val_if_fail (property_uri, no_value);                \
                                                                              \
		priv = GET_PRIVATE (self);                                    \
                                                                              \
		value = g_hash_table_lookup (priv->properties, property_uri); \
                                                                              \
		if (value == NULL) {                                          \
			return no_value;                                      \
		};                                                            \
                                                                              \
		if (G_VALUE_HOLDS (value, G_TYPE_PTR_ARRAY)) {                \
			GPtrArray *array;                                     \
			array = g_value_get_boxed (value);                    \
			if (array->len == 0) {                                \
				return no_value;                              \
			} else {                                              \
				value = g_ptr_array_index (array, 0);         \
			}                                                     \
		}                                                             \
                                                                              \
		return get_function (value);                                  \
	};

/**
 * tracker_resource_get_first_boolean:
 * @self: A #TrackerResource
 * @property_uri: a string identifying the property to look up
 *
 * Returns the first boolean object previously assigned to a property.
 *
 * Returns: the first boolean object
 *
 * Since: 1.10
 */
GET_PROPERTY_FOR_GTYPE (tracker_resource_get_first_boolean, gboolean, G_TYPE_BOOLEAN, g_value_get_boolean, FALSE);

/**
 * tracker_resource_get_first_double:
 * @self: A #TrackerResource
 * @property_uri: a string identifying the property to look up
 *
 * Returns the first double object previously assigned to a property.
 *
 * Returns: the first double object
 *
 * Since: 1.10
 */
GET_PROPERTY_FOR_GTYPE (tracker_resource_get_first_double, double, G_TYPE_DOUBLE, g_value_get_double, 0.0);

/**
 * tracker_resource_get_first_int:
 * @self: A #TrackerResource
 * @property_uri: a string identifying the property to look up
 *
 * Returns the first integer object previously assigned to a property.
 *
 * Returns: the first integer object
 *
 * Since: 1.10
 */
GET_PROPERTY_FOR_GTYPE (tracker_resource_get_first_int, int, G_TYPE_INT, g_value_get_int, 0);

/**
 * tracker_resource_get_first_int64:
 * @self: A #TrackerResource
 * @property_uri: a string identifying the property to look up
 *
 * Returns the first integer object previously assigned to a property.
 *
 * Returns: the first integer object
 *
 * Since: 1.10
 */
GET_PROPERTY_FOR_GTYPE (tracker_resource_get_first_int64, gint64, G_TYPE_INT64, g_value_get_int64, 0);

/**
 * tracker_resource_get_first_relation:
 * @self: A #TrackerResource
 * @property_uri: a string identifying the property to look up
 *
 * Returns the first resource object previously assigned to a property.
 *
 * Returns: (transfer none): the first resource object
 *
 * Since: 1.10
 */
GET_PROPERTY_FOR_GTYPE (tracker_resource_get_first_relation, TrackerResource *, TRACKER_TYPE_RESOURCE, g_value_get_object, NULL);

/**
 * tracker_resource_get_first_string:
 * @self: A #TrackerResource
 * @property_uri: a string identifying the property to look up
 *
 * Returns the first string object previously assigned to a property.
 *
 * Returns: the first string object
 *
 * Since: 1.10
 */
GET_PROPERTY_FOR_GTYPE (tracker_resource_get_first_string, const char *, G_TYPE_STRING, g_value_get_string, NULL);

/**
 * tracker_resource_get_first_uri:
 * @self: A #TrackerResource
 * @property_uri: a string identifying the property to look up
 *
 * Returns the first resource object previously assigned to a property.
 *
 * Returns: the first resource object as an URI.
 *
 * Since: 1.10
 */
GET_PROPERTY_FOR_GTYPE (tracker_resource_get_first_uri, const char *, TRACKER_TYPE_URI, g_value_get_string, NULL);

/**
 * tracker_resource_get_identifier:
 * @self: A #TrackerResource
 *
 * Returns the identifier of a resource.
 *
 * If the identifier was set to NULL, the identifier returned will be a unique
 * SPARQL blank node identifier, such as "_:123".
 *
 * Returns: a string owned by the resource
 *
 * Since: 1.10
 */
const char *
tracker_resource_get_identifier (TrackerResource *self)
{
	TrackerResourcePrivate *priv;

	g_return_val_if_fail (TRACKER_IS_RESOURCE (self), NULL);

	priv = GET_PRIVATE (self);

	return priv->identifier;
}

/**
 * tracker_resource_set_identifier:
 * @self: a #TrackerResource
 * @identifier: (allow-none): a string identifying the resource
 *
 * Changes the identifier of a #TrackerResource. The identifier should be a
 * URI or compact URI, but this is not necessarily enforced. Invalid
 * identifiers may cause errors when serializing the resource or trying to
 * insert the results in a database.
 *
 * If the identifier is set to NULL, a SPARQL blank node identifier such as
 * "_:123" is assigned to the resource.
 *
 * Since: 1.10
 */
void
tracker_resource_set_identifier (TrackerResource *self,
                                 const char      *identifier)
{
	TrackerResourcePrivate *priv;

	g_return_if_fail (TRACKER_IS_RESOURCE (self));

	priv = GET_PRIVATE (self);

	g_free (priv->identifier);

	if (identifier == NULL) {
		/* We take NULL to mean "this is a blank node", and generate a
		 * unique blank node identifier right away. This is easier than
		 * leaving it NULL and trying to generate a blank node ID at
		 * serialization time, and it means that the serialization
		 * output is stable when called multiple times.
		 */
		priv->identifier = generate_blank_node_identifier ();
	} else {
		priv->identifier = g_strdup (identifier);
	}
}

gint
tracker_resource_identifier_compare_func (TrackerResource *resource,
                                          const char      *identifier)
{
	TrackerResourcePrivate *priv;

	g_return_val_if_fail (TRACKER_IS_RESOURCE (resource), 0);
	g_return_val_if_fail (identifier != NULL, 0);

	priv = GET_PRIVATE (resource);

	return strcmp (priv->identifier, identifier);
}

/**
 * tracker_resource_compare:
 * @self: A #TrackerResource
 *
 * Compare the identifiers of two TrackerResource instances. The resources
 * are considered identical if they have the same identifier.
 *
 * Note that there can be false negatives with this simplistic approach: two
 * resources may have different identifiers that actually refer to the same
 * thing.
 *
 * Returns: 0 if the identifiers are the same, -1 or +1 otherwise
 *
 * Since: 1.10
 */
gint
tracker_resource_compare (TrackerResource *a,
                          TrackerResource *b)
{
	TrackerResourcePrivate *a_priv, *b_priv;

	g_return_val_if_fail (TRACKER_IS_RESOURCE (a), 0);
	g_return_val_if_fail (TRACKER_IS_RESOURCE (b), 0);

	a_priv = GET_PRIVATE (a);
	b_priv = GET_PRIVATE (b);

	return strcmp (a_priv->identifier, b_priv->identifier);
};


/* Helper function for serialization code. This allows you to selectively
 * populate 'interned_namespaces' from 'all_namespaces' based on when a
 * particular prefix is actually used. This is quite inefficient compared
 * to just dumping all known namespaces, but it makes the serializated
 * output more readable.
 */
static void
maybe_intern_prefix_of_compact_uri (TrackerNamespaceManager *all_namespaces,
                                    TrackerNamespaceManager *interned_namespaces,
                                    const char              *uri)
{
	/* The TrackerResource API doesn't distinguish between compact URIs and full
	 * URIs. This is fine as long as users don't add prefixes that can be
	 * confused with URIs. Both URIs and CURIEs can have anything following
	 * the ':', so without hardcoding knowledge of well-known protocols here,
	 * we can't really tell if the user has done something dumb like defining a
	 * "urn" prefix.
	 */
	char *prefix = g_uri_parse_scheme (uri);

	if (prefix == NULL) {
		g_warning ("Invalid URI or compact URI: %s", uri);
		return;
	}

	if (tracker_namespace_manager_has_prefix (all_namespaces, prefix)) {
		if (!tracker_namespace_manager_has_prefix (interned_namespaces, prefix)) {
			const char *namespace = tracker_namespace_manager_lookup_prefix (all_namespaces, prefix);
			tracker_namespace_manager_add_prefix (interned_namespaces, prefix, namespace);
		}
	}

	g_free (prefix);
}


typedef struct {
	TrackerNamespaceManager *all_namespaces, *our_namespaces;
	GString *string;
	GList *done_list;
} GenerateTurtleData;

static void generate_turtle (TrackerResource *resource, GenerateTurtleData *data);

static gboolean
is_blank_node (const char *uri_or_curie_or_blank)
{
	return (strncmp(uri_or_curie_or_blank, "_:", 2) == 0);
}

static gboolean
is_builtin_class (const gchar             *uri_or_curie,
                  TrackerNamespaceManager *namespaces)
{
	gchar *prefix = NULL;
	gboolean has_prefix;

	prefix = g_uri_parse_scheme (uri_or_curie);

	if (!prefix)
		return TRUE;

	has_prefix = tracker_namespace_manager_has_prefix (namespaces, prefix);
	g_free (prefix);

	return has_prefix;
}

static void
generate_nested_turtle_resource (TrackerResource    *resource,
                                 GenerateTurtleData *data)
{
	/* We don't need to produce turtle for builtin classes */
	if (is_builtin_class (tracker_resource_get_identifier (resource),
	                      data->all_namespaces))
		return;

	if (g_list_find_custom (data->done_list, resource, (GCompareFunc) tracker_resource_compare) == NULL) {
		generate_turtle (resource, data);

		g_string_append (data->string, "\n");

		data->done_list = g_list_prepend (data->done_list, resource);
	}
}

static void
generate_turtle_resources_foreach (gpointer key,
                                   gpointer value_ptr,
                                   gpointer user_data)
{
	const GValue *value = value_ptr;
	GenerateTurtleData *data = user_data;
	TrackerResource *resource;
	int i;

	if (G_VALUE_HOLDS (value, TRACKER_TYPE_RESOURCE)) {
		resource = g_value_get_object (value);
		generate_nested_turtle_resource (resource, data);
	} else if (G_VALUE_HOLDS (value, G_TYPE_PTR_ARRAY)) {
		GPtrArray *array = g_value_get_boxed (value);
		const GValue *array_value;

		for (i = 0; i < array->len; i++) {
			array_value = g_ptr_array_index (array, i);

			if (G_VALUE_HOLDS (array_value, TRACKER_TYPE_RESOURCE)) {
				resource = g_value_get_object (array_value);
				generate_nested_turtle_resource (resource, data);
			}
		}
	}
}

static void
generate_turtle_uri_value (const char              *uri_or_curie_or_blank,
                           GString                 *string,
                           TrackerNamespaceManager *all_namespaces,
                           TrackerNamespaceManager *our_namespaces)
{
	/* The tracker_resource_set_uri() function accepts URIs
	 * (such as http://example.com/) and compact URIs (such as nie:DataObject),
	 * and blank node identifiers (_:0). The tracker_resource_set_identifier()
	 * function works the same.
	 *
	 * We could expand all CURIEs, but the generated Turtle or SPARQL will be
	 * clearer if we leave them be. We still need to attempt to expand them
	 * internally in order to know whether they need <> brackets around them.
	 */
	if (is_blank_node (uri_or_curie_or_blank)) {
		g_string_append (string, uri_or_curie_or_blank);
	} else {
		char *prefix = g_uri_parse_scheme (uri_or_curie_or_blank);

		if (prefix && tracker_namespace_manager_has_prefix (all_namespaces, prefix)) {
			/* It's a compact URI and we know the prefix */
			if (our_namespaces != NULL) {
				maybe_intern_prefix_of_compact_uri (all_namespaces, our_namespaces, uri_or_curie_or_blank);
			};

			g_string_append (string, uri_or_curie_or_blank);
		} else {
			/* It's a full URI (or something invalid, but we can't really tell that here) */
			g_string_append_printf (string, "<%s>", uri_or_curie_or_blank);
		}

		g_free (prefix);
	}
}

static void
generate_turtle_value (const GValue            *value,
                       GString                 *string,
                       TrackerNamespaceManager *all_namespaces,
                       TrackerNamespaceManager *our_namespaces)
{
	GType type = G_VALUE_TYPE (value);
	if (type == TRACKER_TYPE_URI) {
		generate_turtle_uri_value (g_value_get_string (value),
		                           string,
		                           all_namespaces,
		                           our_namespaces);
	} else if (type == TRACKER_TYPE_RESOURCE) {
		TrackerResource *relation = TRACKER_RESOURCE (g_value_get_object (value));
		generate_turtle_uri_value (tracker_resource_get_identifier (relation),
		                           string,
		                           all_namespaces,
		                           our_namespaces);
	} else if (type == G_TYPE_STRING) {
		char *escaped = tracker_sparql_escape_string (g_value_get_string (value));
		g_string_append_printf(string, "\"%s\"", escaped);
		g_free (escaped);
	} else if (type == G_TYPE_DATE) {
		char date_string[256];
		g_date_strftime (date_string, 256,
		                 "\"%Y-%m-%d%z\"^^<http://www.w3.org/2001/XMLSchema#date>",
		                 g_value_get_boxed (value));
		g_string_append (string, date_string);
	} else if (type == G_TYPE_DATE_TIME) {
		char *datetime_string;
		datetime_string = g_date_time_format (g_value_get_boxed (value),
		                                      "\"%Y-%m-%dT%H:%M:%S%z\"^^<http://www.w3.org/2001/XMLSchema#dateTime>");
		g_string_append (string, datetime_string);
		g_free (datetime_string);
	} else if (type == G_TYPE_DOUBLE || type == G_TYPE_FLOAT) {
		/* We can't use GValue transformations here; they're locale-dependent. */
		char buffer[256];
		g_ascii_dtostr (buffer, 255, g_value_get_double (value));
		g_string_append (string, buffer);
	} else {
		GValue str_value = G_VALUE_INIT;
		g_value_init (&str_value, G_TYPE_STRING);
		if (g_value_transform (value, &str_value)) {
			g_string_append (string, g_value_get_string (&str_value));
		} else {
			g_warning ("Cannot serialize value of type %s to Turtle/SPARQL",
			            G_VALUE_TYPE_NAME (value));
		}
		g_value_unset (&str_value);
	}
}

void
generate_turtle_property (const char              *property,
                          const GValue            *value,
                          GString                 *string,
                          TrackerNamespaceManager *all_namespaces,
                          TrackerNamespaceManager *our_namespaces)
{
	if (strcmp (property, TRACKER_PREFIX_RDF "type") == 0 || strcmp (property, "rdf:type") == 0) {
		g_string_append (string, "a");
	} else {
		g_string_append (string, property);
	}

	g_string_append (string, " ");
	if (G_VALUE_HOLDS (value, G_TYPE_PTR_ARRAY)) {
		int i;
		GPtrArray *array = g_value_get_boxed (value);
		if (array->len > 0) {
			generate_turtle_value (g_ptr_array_index (array, 0),
			                       string,
			                       all_namespaces,
			                       our_namespaces);
			for (i = 1; i < array->len; i++) {
				g_string_append (string, " , ");
				generate_turtle_value (g_ptr_array_index (array, i),
				                       string,
				                       all_namespaces,
				                       our_namespaces);
			}
		}
	} else {
		generate_turtle_value (value, string, all_namespaces, our_namespaces);
	}
}

static void
generate_turtle (TrackerResource    *resource,
                 GenerateTurtleData *data)
{
	TrackerResourcePrivate *priv = GET_PRIVATE (resource);
	GHashTableIter iter;
	const char *property;
	const GValue *value;

	/* First we recurse to any relations that aren't already in the done list */
	g_hash_table_foreach (priv->properties, generate_turtle_resources_foreach, data);

	generate_turtle_uri_value (tracker_resource_get_identifier(resource),
	        data->string, data->all_namespaces, data->our_namespaces);
	g_string_append (data->string, " ");

	g_hash_table_iter_init (&iter, priv->properties);
	if (g_hash_table_iter_next (&iter, (gpointer *)&property, (gpointer *)&value)) {
		while (TRUE) {
			generate_turtle_property (property, value, data->string, data->all_namespaces, data->our_namespaces);

			maybe_intern_prefix_of_compact_uri (data->all_namespaces, data->our_namespaces, property);

			if (g_hash_table_iter_next (&iter, (gpointer *)&property, (gpointer *)&value)) {
				g_string_append (data->string, " ;\n  ");
			} else {
				g_string_append (data->string, " .\n");
				break;
			}
		}
	}
}

/**
 * tracker_resource_print_turtle:
 * @self: a #TrackerResource
 * @namespaces: (allow-none): a set of prefixed URLs, or %NULL to use the
 *     default set
 *
 * Serialize all the information in @resource as a Turtle document.
 *
 * The generated Turtle should correspond to this standard:
 * <https://www.w3.org/TR/2014/REC-turtle-20140225/>
 *
 * The @namespaces object is used to expand any compact URI values. In most
 * cases you should pass %NULL, which means the set of namespaces returned by
 * tracker_namespace_manager_get_default() will be used. This defines the
 * usual prefixes for all of the ontologies that Tracker ships with by default.
 *
 * Returns: a newly-allocated string
 *
 * Since: 1.10
 */
char *
tracker_resource_print_turtle (TrackerResource         *self,
                               TrackerNamespaceManager *namespaces)
{
	TrackerResourcePrivate *priv;
	GenerateTurtleData context;
	char *prefixes;

	g_return_val_if_fail (TRACKER_IS_RESOURCE (self), "");

	priv = GET_PRIVATE (self);

	if (namespaces == NULL) {
		namespaces = tracker_namespace_manager_get_default ();
	}

	if (g_hash_table_size (priv->properties) == 0) {
		return g_strdup("");
	}

	context.all_namespaces = namespaces;
	context.our_namespaces = tracker_namespace_manager_new ();
	context.string = g_string_new ("");
	context.done_list = NULL;

	maybe_intern_prefix_of_compact_uri (context.all_namespaces, context.our_namespaces, tracker_resource_get_identifier(self));

	generate_turtle (self, &context);

	prefixes = tracker_namespace_manager_print_turtle (context.our_namespaces);
	g_string_prepend (context.string, "\n");
	g_string_prepend (context.string, prefixes);

	g_object_unref (context.our_namespaces);
	g_free (prefixes);

	g_list_free (context.done_list);

	return g_string_free (context.string, FALSE);
}

typedef struct {
	TrackerNamespaceManager *namespaces;
	GString *string;
	const char *graph_id;
	GList *done_list;
} GenerateSparqlData;

static void generate_sparql_deletes (TrackerResource *resource, GenerateSparqlData *data);
static void generate_sparql_insert_pattern (TrackerResource *resource, GenerateSparqlData *data);

static void
generate_sparql_relation_deletes_foreach (gpointer key,
                                          gpointer value_ptr,
                                          gpointer user_data)
{
	const GValue *value = value_ptr;
	GenerateSparqlData *data = user_data;

	if (G_VALUE_HOLDS (value, TRACKER_TYPE_RESOURCE)) {
		TrackerResource *relation = g_value_get_object (value);

		if (g_list_find_custom (data->done_list, relation, (GCompareFunc) tracker_resource_compare) == NULL) {
			generate_sparql_deletes (relation, data);
			data->done_list = g_list_prepend (data->done_list, relation);
		}
	}
}

static void
generate_sparql_relation_inserts_foreach (gpointer key,
                                          gpointer value_ptr,
                                          gpointer user_data)
{
	const GValue *value = value_ptr;
	GenerateSparqlData *data = user_data;

	if (G_VALUE_HOLDS (value, TRACKER_TYPE_RESOURCE)) {
		TrackerResource *relation = g_value_get_object (value);

		/* We don't need to produce inserts for builtin classes */
		if (is_builtin_class (tracker_resource_get_identifier (relation),
		                      data->namespaces))
			return;

		if (g_list_find_custom (data->done_list, relation, (GCompareFunc) tracker_resource_compare) == NULL) {
			generate_sparql_insert_pattern (relation, data);
			data->done_list = g_list_prepend (data->done_list, relation);
		}
	} else if (G_VALUE_HOLDS (value, G_TYPE_PTR_ARRAY)) {
		GPtrArray *array = g_value_get_boxed (value);
		const GValue *array_value;
		TrackerResource *relation;
		gint i;

		for (i = 0; i < array->len; i++) {
			array_value = g_ptr_array_index (array, i);

			if (!G_VALUE_HOLDS (array_value, TRACKER_TYPE_RESOURCE))
				continue;

			relation = g_value_get_object (array_value);

			/* We don't need to produce inserts for builtin classes */
			if (is_builtin_class (tracker_resource_get_identifier (relation),
					      data->namespaces))
				continue;

			if (g_list_find_custom (data->done_list, relation,
						(GCompareFunc) tracker_resource_compare) != NULL)
				continue;

			generate_sparql_insert_pattern (relation, data);
			data->done_list = g_list_prepend (data->done_list, relation);
		}
	}
}

static char *
variable_name_for_property (const char *property) {
	return g_strcanon (g_strdup (property),
	                   "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ1234567890",
	                   '_');
}

static void
generate_sparql_delete_queries (TrackerResource     *resource,
                                GHashTable          *overwrite_flags,
                                GenerateSparqlData  *data)
{
	TrackerResourcePrivate *priv = GET_PRIVATE (resource);
	GHashTableIter iter;
	const char *property;
	const GValue *value;

	g_hash_table_iter_init (&iter, priv->properties);
	while (g_hash_table_iter_next (&iter, (gpointer *)&property, (gpointer *)&value)) {
		/* Whether to generate the DELETE is based on whether set_value was ever
		* called for this property. That's tracked in the overwrite_flags hash table.
		*/
		if (g_hash_table_lookup (overwrite_flags, property)) {
			char *variable_name = variable_name_for_property (property);

			g_string_append (data->string, "DELETE WHERE {\n");

			if (data->graph_id) {
				g_string_append_printf (data->string, "GRAPH <%s> {\n", data->graph_id);
			}

			g_string_append (data->string, "  ");
			generate_turtle_uri_value (priv->identifier, data->string, data->namespaces, NULL);
			g_string_append_printf (data->string, " %s ?%s }", property, variable_name);
			g_free (variable_name);

			if (data->graph_id) {
				g_string_append (data->string, " }");
			}

			g_string_append (data->string, ";\n");
		}
	}
}

void
generate_sparql_deletes (TrackerResource    *resource,
                         GenerateSparqlData *data)
{
	TrackerResourcePrivate *priv = GET_PRIVATE (resource);

	if (! is_blank_node (priv->identifier) && g_hash_table_size (priv->overwrite) > 0) {
		generate_sparql_delete_queries (resource, priv->overwrite, data);
	}

	/* Now emit any sub-resources. */
	g_hash_table_foreach (priv->properties, generate_sparql_relation_deletes_foreach, data);
}

static void
generate_sparql_insert_pattern (TrackerResource    *resource,
                                GenerateSparqlData *data)
{
	TrackerResourcePrivate *priv = GET_PRIVATE (resource);
	GHashTableIter iter;
	const char *property;
	char *full_property;
	const GValue *value;
	gboolean had_property = FALSE;

	/* First, emit any sub-resources. */
	g_hash_table_foreach (priv->properties, generate_sparql_relation_inserts_foreach, data);

	generate_turtle_uri_value (priv->identifier, data->string, data->namespaces, NULL);
	g_string_append_printf (data->string, " ");

	/* rdf:type needs to be first, otherwise you'll see 'subject x is not in domain y'
	 * errors for the properties you try to set.
	 */
	value = g_hash_table_lookup (priv->properties, "rdf:type");
	if (value != NULL) {
		generate_turtle_property ("a", value, data->string, data->namespaces, NULL);
		had_property = TRUE;
	}

	g_hash_table_iter_init (&iter, priv->properties);
	while (g_hash_table_iter_next (&iter, (gpointer *)&property, (gpointer *)&value)) {
		full_property = tracker_namespace_manager_expand_uri (data->namespaces, property);

		if (strcmp (full_property, TRACKER_PREFIX_RDF "type") != 0 && strcmp (property, "rdf:type") != 0) {
			if (had_property) {
				g_string_append (data->string, " ; \n  ");
			}

			generate_turtle_property (property, value, data->string, data->namespaces, NULL);

			had_property = TRUE;
		}

		g_free (full_property);
	}

	g_string_append (data->string, " .\n");
}

/**
 * tracker_resource_print_sparql_update:
 * @self: a #TrackerResource
 * @namespaces: (allow-none): a set of prefixed URLs, or %NULL to use the
 *     default set
 * @graph_id: (allow-none): the URN of the graph the data should be added to,
 *     or %NULL
 *
 * Generates a SPARQL command to update a database with the information
 * stored in @resource.
 *
 * The @namespaces object is used to expand any compact URI values. In most
 * cases you should pass %NULL, which means the set of namespaces returned by
 * tracker_namespace_manager_get_default() will be used. This defines the
 * usual prefixes for all of the ontologies that Tracker ships with by default.
 *
 * Returns: a newly-allocated string containing a SPARQL update command.
 *
 * Since: 1.10
 */
char *
tracker_resource_print_sparql_update (TrackerResource         *resource,
                                      TrackerNamespaceManager *namespaces,
                                      const char              *graph_id)
{
	TrackerResourcePrivate *priv;
	GenerateSparqlData context;

	g_return_val_if_fail (TRACKER_IS_RESOURCE (resource), "");

	priv = GET_PRIVATE(resource);

	if (namespaces == NULL) {
		namespaces = tracker_namespace_manager_get_default ();
	}

	if (g_hash_table_size (priv->properties) == 0) {
		return g_strdup("");
	}

	context.namespaces = namespaces;
	context.string = g_string_new (NULL);
	context.graph_id = graph_id;

	/* Resources can be recursive, and may have repeated or even cyclic
	 * relationships. This list keeps track of what we already processed.
	 */
	context.done_list = NULL;

	/* Delete the existing data. If we don't do this, we may get constraint
	 * violations due to trying to add a second value to a single-valued
	 * property, and we may get old metadata hanging around.
	 */
	generate_sparql_deletes (resource, &context);

	g_list_free (context.done_list);
	context.done_list = NULL;

	/* Finally insert the data */
	g_string_append (context.string, "INSERT DATA {\n");
	if (graph_id) {
		g_string_append_printf (context.string, "GRAPH <%s> {\n", graph_id);
	}

	generate_sparql_insert_pattern (resource, &context);

	if (graph_id) {
		g_string_append (context.string, "}\n");
	}
	g_string_append (context.string, "};\n");

	g_list_free (context.done_list);
	context.done_list = NULL;

	return g_string_free (context.string, FALSE);
}

typedef struct {
	TrackerNamespaceManager *all_namespaces, *our_namespaces;
	JsonBuilder *builder;
	GList *done_list;
} GenerateJsonldData;

static void generate_jsonld_foreach (gpointer key, gpointer value_ptr, gpointer user_data);

static void
tracker_resource_generate_jsonld (TrackerResource    *self,
                                  GenerateJsonldData *data)
{
	TrackerResourcePrivate *priv = GET_PRIVATE (self);
	JsonBuilder *builder = data->builder;

	/* The JSON-LD spec says it is "important that nodes have an identifier", but
	 * doesn't mandate one. I think it's better to omit the ID for blank nodes
	 * (where the caller passed NULL as an identifier) than to emit something
	 * SPARQL-specific like '_:123'.
	 */
	if (strncmp (priv->identifier, "_:", 2) != 0) {
		json_builder_set_member_name (builder, "@id");
		json_builder_add_string_value (builder, priv->identifier);
	}

	g_hash_table_foreach (priv->properties, generate_jsonld_foreach, data);
};

static void
generate_jsonld_value (const GValue       *value,
                       GenerateJsonldData *data)
{
	JsonNode *node;

	if (G_VALUE_HOLDS (value, TRACKER_TYPE_RESOURCE)) {
		TrackerResource *resource;

		resource = TRACKER_RESOURCE (g_value_get_object (value));

		if (g_list_find_custom (data->done_list, resource, (GCompareFunc) tracker_resource_compare) == NULL) {
			json_builder_begin_object (data->builder);

			tracker_resource_generate_jsonld (resource, data);

			json_builder_end_object (data->builder);

			data->done_list = g_list_prepend (data->done_list, resource);
		} else {
			json_builder_add_string_value (data->builder, tracker_resource_get_identifier(resource));
		}
	} else if (G_VALUE_HOLDS (value, TRACKER_TYPE_URI)) {
		/* URIs can be treated the same as strings in JSON-LD provided the @context
		 * sets the type of that property correctly. However, json_node_set_value()
		 * will reject a GValue holding TRACKER_TYPE_URI, so we have to extract the
		 * string manually here.
		 */
		const char *uri = g_value_get_string (value);
		maybe_intern_prefix_of_compact_uri (data->all_namespaces, data->our_namespaces, uri);
		node = json_node_new (JSON_NODE_VALUE);
		json_node_set_string (node, uri);
		json_builder_add_value (data->builder, node);
	} else {
		node = json_node_new (JSON_NODE_VALUE);
		json_node_set_value (node, value);
		json_builder_add_value (data->builder, node);
	}
}

static void
generate_jsonld_foreach (gpointer key,
                         gpointer value_ptr,
                         gpointer user_data)
{
	const char *property = key;
	const GValue *value = value_ptr;
	GenerateJsonldData *data = user_data;
	JsonBuilder *builder = data->builder;

	if (strcmp (property, "rdf:type") == 0) {
		property = "@type";
	} else {
		maybe_intern_prefix_of_compact_uri (data->all_namespaces, data->our_namespaces, property);
	}

	json_builder_set_member_name (builder, property);

	if (G_VALUE_HOLDS (value, G_TYPE_PTR_ARRAY)) {
		json_builder_begin_array (builder);
		g_ptr_array_foreach (g_value_get_boxed (value), (GFunc) generate_jsonld_value, data);
		json_builder_end_array (builder);
	} else {
		generate_jsonld_value (value, data);
	}
}

static void
generate_jsonld_namespace_mapping_foreach (gpointer key,
                                           gpointer value,
                                           gpointer user_data)
{
	GenerateJsonldData *data = user_data;

	json_builder_set_member_name (data->builder, key);
	json_builder_add_string_value (data->builder, value);
}


/**
 * tracker_resource_print_jsonld:
 * @self: a #TrackerResource
 * @namespaces: (allow-none): a set of prefixed URLs, or %NULL to use the
 *     default set
 *
 * Serialize all the information in @resource as a JSON-LD document.
 *
 * See <http://www.jsonld.org/> for more information on the JSON-LD
 * serialization format.
 *
 * The @namespaces object is used to expand any compact URI values. In most
 * cases you should pass %NULL, which means the set of namespaces returned by
 * tracker_namespace_manager_get_default() will be used. This defines the
 * usual prefixes for all of the ontologies that Tracker ships with by default.
 *
 * Returns: a newly-allocated string containing JSON-LD data.
 *
 * Since: 2.0.5
 */
char *
tracker_resource_print_jsonld (TrackerResource         *self,
                               TrackerNamespaceManager *namespaces)
{
	GenerateJsonldData context;
	JsonNode *json_root_node;
	JsonGenerator *generator;
	char *result;

	if (namespaces == NULL) {
		namespaces = tracker_namespace_manager_get_default ();
	}

	context.all_namespaces = namespaces;
	context.our_namespaces = tracker_namespace_manager_new ();
	context.builder = json_builder_new ();
	context.done_list = NULL;

	maybe_intern_prefix_of_compact_uri (context.all_namespaces, context.our_namespaces, tracker_resource_get_identifier (self));

	json_builder_begin_object (context.builder);

	tracker_resource_generate_jsonld (self, &context);

	json_builder_set_member_name (context.builder, "@context");
	json_builder_begin_object (context.builder);
	tracker_namespace_manager_foreach (context.our_namespaces, generate_jsonld_namespace_mapping_foreach, &context);
	json_builder_end_object (context.builder);

	json_builder_end_object (context.builder);

	json_root_node = json_builder_get_root (context.builder);

	generator = json_generator_new ();
	json_generator_set_root (generator, json_root_node);
	json_generator_set_pretty (generator, TRUE);

	result = json_generator_to_data (generator, NULL);

	g_list_free (context.done_list);
	json_node_free (json_root_node);
	g_object_unref (context.builder);
	g_object_unref (generator);

	return result;
}
