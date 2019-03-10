/*
 * Copyright (C) 2006, Jamie McCracken <jamiemcc@gnome.org>
 * Copyright (C) 2007, Jason Kivlighn <jkivlighn@gmail.com>
 * Copyright (C) 2007, Creative Commons <http://creativecommons.org>
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
#include <fcntl.h>
#include <zlib.h>
#include <inttypes.h>

#include <glib/gstdio.h>

#if HAVE_TRACKER_FTS
#include <libtracker-fts/tracker-fts.h>
#endif

#include <libtracker-common/tracker-locale.h>

#include "tracker-class.h"
#include "tracker-data-manager.h"
#include "tracker-data-update.h"
#include "tracker-db-interface-sqlite.h"
#include "tracker-db-manager.h"
#include "tracker-namespace.h"
#include "tracker-ontologies.h"
#include "tracker-ontology.h"
#include "tracker-property.h"
#include "tracker-sparql-query.h"
#include "tracker-data-query.h"
#include "tracker-sparql-parser.h"

#define RDF_PROPERTY                    TRACKER_PREFIX_RDF "Property"
#define RDF_TYPE                        TRACKER_PREFIX_RDF "type"

#define RDFS_CLASS                      TRACKER_PREFIX_RDFS "Class"
#define RDFS_DOMAIN                     TRACKER_PREFIX_RDFS "domain"
#define RDFS_RANGE                      TRACKER_PREFIX_RDFS "range"
#define RDFS_SUB_CLASS_OF               TRACKER_PREFIX_RDFS "subClassOf"
#define RDFS_SUB_PROPERTY_OF            TRACKER_PREFIX_RDFS "subPropertyOf"

#define NRL_INVERSE_FUNCTIONAL_PROPERTY TRACKER_PREFIX_NRL "InverseFunctionalProperty"
#define NRL_MAX_CARDINALITY             TRACKER_PREFIX_NRL "maxCardinality"

#define NAO_LAST_MODIFIED               TRACKER_PREFIX_NAO "lastModified"

#define ZLIBBUFSIZ 8192

struct _TrackerDataManager {
	GObject parent_instance;

	GFile *ontology_location;
	GFile *cache_location;
	GFile *data_location;
	guint initialized      : 1;
	guint restoring_backup : 1;
	guint first_time_index : 1;
	guint flags;

	gint select_cache_size;
	gint update_cache_size;

	TrackerDBManager *db_manager;
	TrackerOntologies *ontologies;
	TrackerData *data_update;

	gchar *status;
};

struct _TrackerDataManagerClass {
	GObjectClass parent_instance;
};

typedef struct {
	const gchar *from;
	const gchar *to;
} Conversion;

static Conversion allowed_boolean_conversions[] = {
	{ "false", "true" },
	{ "true", "false" },
	{ NULL, NULL }
};

static Conversion allowed_cardinality_conversions[] = {
	{ "1", NULL },
	{ NULL, NULL }
};

static Conversion allowed_range_conversions[] = {
	{ TRACKER_PREFIX_XSD "integer", TRACKER_PREFIX_XSD "string" },
	{ TRACKER_PREFIX_XSD "integer", TRACKER_PREFIX_XSD "double" },
	{ TRACKER_PREFIX_XSD "integer", TRACKER_PREFIX_XSD "boolean" },

	{ TRACKER_PREFIX_XSD "string", TRACKER_PREFIX_XSD "integer" },
	{ TRACKER_PREFIX_XSD "string", TRACKER_PREFIX_XSD "double" },
	{ TRACKER_PREFIX_XSD "string", TRACKER_PREFIX_XSD "boolean" },

	{ TRACKER_PREFIX_XSD "double", TRACKER_PREFIX_XSD "integer" },
	{ TRACKER_PREFIX_XSD "double", TRACKER_PREFIX_XSD "string" },
	{ TRACKER_PREFIX_XSD "double", TRACKER_PREFIX_XSD "boolean" },

	{ NULL, NULL }
};

enum {
	PROP_0,
	PROP_STATUS,
	N_PROPS
};

#if HAVE_TRACKER_FTS
static gboolean tracker_data_manager_fts_changed (TrackerDataManager *manager);
static void tracker_data_manager_update_fts (TrackerDataManager *manager,
                                             TrackerDBInterface *iface);
#endif

static void tracker_data_manager_initable_iface_init (GInitableIface *iface);

G_DEFINE_TYPE_WITH_CODE (TrackerDataManager, tracker_data_manager, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE, tracker_data_manager_initable_iface_init))

static void
tracker_data_manager_init (TrackerDataManager *manager)
{
}

GQuark
tracker_data_ontology_error_quark (void)
{
	return g_quark_from_static_string ("tracker-data-ontology-error-quark");
}

static void
handle_unsupported_ontology_change (TrackerDataManager  *manager,
                                    const gchar         *ontology_path,
                                    const gchar         *subject,
                                    const gchar         *change,
                                    const gchar         *old,
                                    const gchar         *attempted_new,
                                    GError             **error)
{
	g_set_error (error, TRACKER_DATA_ONTOLOGY_ERROR,
	             TRACKER_DATA_UNSUPPORTED_ONTOLOGY_CHANGE,
	             "%s: Unsupported ontology change for %s: can't change %s (old=%s, attempted new=%s)",
	             ontology_path != NULL ? ontology_path : "Unknown",
	             subject != NULL ? subject : "Unknown",
	             change != NULL ? change : "Unknown",
	             old != NULL ? old : "Unknown",
	             attempted_new != NULL ? attempted_new : "Unknown");
}

static void
set_secondary_index_for_single_value_property (TrackerDBInterface  *iface,
                                               const gchar         *service_name,
                                               const gchar         *field_name,
                                               const gchar         *second_field_name,
                                               gboolean             enabled,
                                               GError             **error)
{
	GError *internal_error = NULL;

	g_debug ("Dropping secondary index (single-value property):  "
	         "DROP INDEX IF EXISTS \"%s_%s\"",
	         service_name, field_name);

	tracker_db_interface_execute_query (iface, &internal_error,
	                                    "DROP INDEX IF EXISTS \"%s_%s\"",
	                                    service_name,
	                                    field_name);

	if (internal_error) {
		g_propagate_error (error, internal_error);
		return;
	}

	if (enabled) {
		g_debug ("Creating secondary index (single-value property): "
		         "CREATE INDEX \"%s_%s\" ON \"%s\" (\"%s\", \"%s\")",
		         service_name, field_name, service_name, field_name, second_field_name);

		tracker_db_interface_execute_query (iface, &internal_error,
		                                    "CREATE INDEX \"%s_%s\" ON \"%s\" (\"%s\", \"%s\")",
		                                    service_name,
		                                    field_name,
		                                    service_name,
		                                    field_name,
		                                    second_field_name);

		if (internal_error) {
			g_propagate_error (error, internal_error);
		}
	}
}

static void
set_index_for_single_value_property (TrackerDBInterface  *iface,
                                     const gchar         *service_name,
                                     const gchar         *field_name,
                                     gboolean             enabled,
                                     gboolean             datetime,
                                     GError             **error)
{
	GError *internal_error = NULL;

	g_debug ("Dropping index (single-value property): "
	         "DROP INDEX IF EXISTS \"%s_%s\"",
	         service_name, field_name);

	tracker_db_interface_execute_query (iface, &internal_error,
	                                    "DROP INDEX IF EXISTS \"%s_%s\"",
	                                    service_name,
	                                    field_name);

	if (internal_error) {
		g_propagate_error (error, internal_error);
		return;
	}

	if (enabled) {
		gchar *expr;

		if (datetime)
			expr = g_strdup_printf ("SparqlTimeSort(\"%s\")", field_name);
		else
			expr = g_strdup_printf ("\"%s\"", field_name);

		g_debug ("Creating index (single-value property): "
		         "CREATE INDEX \"%s_%s\" ON \"%s\" (%s)",
		         service_name, field_name, service_name, expr);

		tracker_db_interface_execute_query (iface, &internal_error,
		                                    "CREATE INDEX \"%s_%s\" ON \"%s\" (%s)",
		                                    service_name,
		                                    field_name,
		                                    service_name,
		                                    expr);
		g_free (expr);

		if (internal_error) {
			g_propagate_error (error, internal_error);
		}
	}
}

static void
set_index_for_multi_value_property (TrackerDBInterface  *iface,
                                    const gchar         *service_name,
                                    const gchar         *field_name,
                                    gboolean             enabled,
                                    gboolean             recreate,
                                    gboolean             datetime,
                                    GError             **error)
{
	GError *internal_error = NULL;
	gchar *expr;

	g_debug ("Dropping index (multi-value property): "
	         "DROP INDEX IF EXISTS \"%s_%s_ID_ID\"",
	         service_name, field_name);

	tracker_db_interface_execute_query (iface, &internal_error,
	                                    "DROP INDEX IF EXISTS \"%s_%s_ID_ID\"",
	                                    service_name,
	                                    field_name);

	if (internal_error) {
		g_propagate_error (error, internal_error);
		return;
	}

	/* Useful to have this here for the cases where we want to fully
	 * re-create the indexes even without an ontology change (when locale
	 * of the user changes) */
	g_debug ("Dropping index (multi-value property): "
	         "DROP INDEX IF EXISTS \"%s_%s_ID\"",
	         service_name,
	         field_name);
	tracker_db_interface_execute_query (iface, &internal_error,
	                                    "DROP INDEX IF EXISTS \"%s_%s_ID\"",
	                                    service_name,
	                                    field_name);

	if (internal_error) {
		g_propagate_error (error, internal_error);
		return;
	}

	if (!recreate) {
		return;
	}

	if (datetime)
		expr = g_strdup_printf ("SparqlTimeSort(\"%s\")", field_name);
	else
		expr = g_strdup_printf ("\"%s\"", field_name);

	if (enabled) {
		g_debug ("Creating index (multi-value property): "
		         "CREATE INDEX \"%s_%s_ID\" ON \"%s_%s\" (ID)",
		         service_name,
		         field_name,
		         service_name,
		         field_name);

		tracker_db_interface_execute_query (iface, &internal_error,
		                                    "CREATE INDEX \"%s_%s_ID\" ON \"%s_%s\" (ID)",
		                                    service_name,
		                                    field_name,
		                                    service_name,
		                                    field_name);

		if (internal_error) {
			g_propagate_error (error, internal_error);
			return;
		}

		g_debug ("Creating index (multi-value property): "
		         "CREATE UNIQUE INDEX \"%s_%s_ID_ID\" ON \"%s_%s\" (%s, ID)",
		         service_name,
		         field_name,
		         service_name,
		         field_name,
		         expr);

		tracker_db_interface_execute_query (iface, &internal_error,
		                                    "CREATE UNIQUE INDEX \"%s_%s_ID_ID\" ON \"%s_%s\" (%s, ID)",
		                                    service_name,
		                                    field_name,
		                                    service_name,
		                                    field_name,
		                                    expr);

		if (internal_error) {
			g_propagate_error (error, internal_error);
			return;
		}
	} else {
		g_debug ("Creating index (multi-value property): "
		         "CREATE UNIQUE INDEX \"%s_%s_ID_ID\" ON \"%s_%s\" (ID, %s)",
		         service_name,
		         field_name,
		         service_name,
		         field_name,
		         expr);

		tracker_db_interface_execute_query (iface, &internal_error,
		                                    "CREATE UNIQUE INDEX \"%s_%s_ID_ID\" ON \"%s_%s\" (ID, %s)",
		                                    service_name,
		                                    field_name,
		                                    service_name,
		                                    field_name,
		                                    expr);

		if (internal_error) {
			g_propagate_error (error, internal_error);
		}
	}

	g_free (expr);
}

static gboolean
is_allowed_conversion (const gchar *oldv,
                       const gchar *newv,
                       Conversion   allowed[])
{
	guint i;

	for (i = 0; allowed[i].from != NULL; i++) {
		if (g_strcmp0 (allowed[i].from, oldv) == 0) {
			if (g_strcmp0 (allowed[i].to, newv) == 0) {
				return TRUE;
			}
		}
	}

	return FALSE;
}

static gboolean
check_unsupported_property_value_change (TrackerDataManager *manager,
                                         const gchar        *ontology_path,
                                         const gchar        *kind,
                                         const gchar        *subject,
                                         const gchar        *predicate,
                                         const gchar        *object)
{
	GError *error = NULL;
	gboolean needed = TRUE;
	gchar *query = NULL;
	TrackerDBCursor *cursor;

	query = g_strdup_printf ("SELECT ?old_value WHERE { "
	                           "<%s> %s ?old_value "
	                         "}", subject, kind);

	cursor = tracker_data_query_sparql_cursor (manager, query, &error);

	if (cursor && tracker_db_cursor_iter_next (cursor, NULL, NULL)) {
		if (g_strcmp0 (object, tracker_db_cursor_get_string (cursor, 0, NULL)) == 0) {
			needed = FALSE;
		} else {
			needed = TRUE;
		}

	} else {
		if (object && (g_strcmp0 (object, "false") == 0)) {
			needed = FALSE;
		} else {
			needed = (object != NULL);
		}
	}

	g_free (query);
	if (cursor) {
		g_object_unref (cursor);
	}

	if (error) {
		g_critical ("Ontology change, %s", error->message);
		g_clear_error (&error);
	}

	return needed;
}

static gboolean
update_property_value (TrackerDataManager  *manager,
                       const gchar         *ontology_path,
                       const gchar         *kind,
                       const gchar         *subject,
                       const gchar         *predicate,
                       const gchar         *object,
                       Conversion           allowed[],
                       TrackerClass        *class,
                       TrackerProperty     *property,
                       GError             **error_in)
{
	GError *error = NULL;
	gboolean needed = TRUE;
	gboolean is_new = FALSE;

	if (class) {
		is_new = tracker_class_get_is_new (class);
	} else if (property) {
		is_new = tracker_property_get_is_new (property);
	}

	if (!is_new) {
		gchar *query = NULL;
		TrackerDBCursor *cursor;

		query = g_strdup_printf ("SELECT ?old_value WHERE { "
		                           "<%s> %s ?old_value "
		                         "}", subject, kind);

		cursor = tracker_data_query_sparql_cursor (manager, query, &error);

		if (cursor && tracker_db_cursor_iter_next (cursor, NULL, NULL)) {
			const gchar *str = NULL;

			str = tracker_db_cursor_get_string (cursor, 0, NULL);

			if (g_strcmp0 (object, str) == 0) {
				needed = FALSE;
			} else {
				gboolean unsup_onto_err = FALSE;

				if (allowed && !is_allowed_conversion (str, object, allowed)) {
					handle_unsupported_ontology_change (manager,
					                                    ontology_path,
					                                    subject,
					                                    kind,
					                                    str,
					                                    object,
					                                    error_in);
					needed = FALSE;
					unsup_onto_err = TRUE;
				}

				if (!unsup_onto_err) {
					tracker_data_delete_statement (manager->data_update, NULL, subject, predicate, str, &error);
					if (!error)
						tracker_data_update_buffer_flush (manager->data_update, &error);
				}
			}

		} else {
			if (object && (g_strcmp0 (object, "false") == 0)) {
				needed = FALSE;
			} else {
				needed = (object != NULL);
			}
		}
		g_free (query);
		if (cursor) {
			g_object_unref (cursor);
		}
	} else {
		needed = FALSE;
	}


	if (!error && needed && object) {
		tracker_data_insert_statement (manager->data_update, NULL, subject,
		                               predicate, object,
		                               &error);
		if (!error)
			tracker_data_update_buffer_flush (manager->data_update, &error);
	}

	if (error) {
		g_critical ("Ontology change, %s", error->message);
		g_clear_error (&error);
	}

	return needed;
}

static void
check_range_conversion_is_allowed (TrackerDataManager  *manager,
                                   const gchar         *ontology_path,
                                   const gchar         *subject,
                                   const gchar         *predicate,
                                   const gchar         *object,
                                   GError             **error)
{
	TrackerDBCursor *cursor;
	gchar *query;

	query = g_strdup_printf ("SELECT ?old_value WHERE { "
	                           "<%s> rdfs:range ?old_value "
	                         "}", subject);

	cursor = tracker_data_query_sparql_cursor (manager, query, NULL);

	g_free (query);

	if (cursor && tracker_db_cursor_iter_next (cursor, NULL, NULL)) {
		const gchar *str;

		str = tracker_db_cursor_get_string (cursor, 0, NULL);

		if (g_strcmp0 (object, str) != 0) {
			if (!is_allowed_conversion (str, object, allowed_range_conversions)) {
				handle_unsupported_ontology_change (manager,
				                                    ontology_path,
				                                    subject,
				                                    "rdfs:range",
				                                    str,
				                                    object,
				                                    error);
			}
		}
	}

	if (cursor) {
		g_object_unref (cursor);
	}
}

static void
fix_indexed (TrackerDataManager  *manager,
             TrackerProperty     *property,
             gboolean             recreate,
             GError             **error)
{
	GError *internal_error = NULL;
	TrackerDBInterface *iface;
	TrackerClass *class;
	const gchar *service_name;
	const gchar *field_name;
	gboolean datetime;

	iface = tracker_db_manager_get_writable_db_interface (manager->db_manager);

	class = tracker_property_get_domain (property);
	field_name = tracker_property_get_name (property);
	service_name = tracker_class_get_name (class);
	datetime = tracker_property_get_data_type (property) == TRACKER_PROPERTY_TYPE_DATETIME;

	if (tracker_property_get_multiple_values (property)) {
		set_index_for_multi_value_property (iface, service_name, field_name,
		                                    tracker_property_get_indexed (property),
		                                    recreate,
		                                    datetime,
		                                    &internal_error);
	} else {
		TrackerProperty *secondary_index;
		TrackerClass **domain_index_classes;

		secondary_index = tracker_property_get_secondary_index (property);
		if (secondary_index == NULL) {
			set_index_for_single_value_property (iface, service_name, field_name,
			                                     recreate && tracker_property_get_indexed (property),
			                                     datetime,
			                                     &internal_error);
		} else {
			set_secondary_index_for_single_value_property (iface, service_name, field_name,
			                                               tracker_property_get_name (secondary_index),
			                                               recreate && tracker_property_get_indexed (property),
			                                               &internal_error);
		}

		/* single-valued properties may also have domain-specific indexes */
		domain_index_classes = tracker_property_get_domain_indexes (property);
		while (!internal_error && domain_index_classes && *domain_index_classes) {
			set_index_for_single_value_property (iface,
			                                     tracker_class_get_name (*domain_index_classes),
			                                     field_name,
			                                     recreate,
			                                     datetime,
			                                     &internal_error);
			domain_index_classes++;
		}
	}

	if (internal_error) {
		g_propagate_error (error, internal_error);
	}
}


static void
tracker_data_ontology_load_statement (TrackerDataManager  *manager,
                                      const gchar         *ontology_path,
                                      gint                 subject_id,
                                      const gchar         *subject,
                                      const gchar         *predicate,
                                      const gchar         *object,
                                      gint                *max_id,
                                      gboolean             in_update,
                                      GHashTable          *classes,
                                      GHashTable          *properties,
                                      GPtrArray           *seen_classes,
                                      GPtrArray           *seen_properties,
                                      GError             **error)
{
	if (g_strcmp0 (predicate, RDF_TYPE) == 0) {
		if (g_strcmp0 (object, RDFS_CLASS) == 0) {
			TrackerClass *class;
			class = tracker_ontologies_get_class_by_uri (manager->ontologies, subject);

			if (class != NULL) {
				if (seen_classes)
					g_ptr_array_add (seen_classes, g_object_ref (class));
				if (!in_update) {
					g_critical ("%s: Duplicate definition of class %s", ontology_path, subject);
				} else {
					/* Reset for a correct post-check */
					tracker_class_reset_domain_indexes (class);
					tracker_class_reset_super_classes (class);
					tracker_class_set_notify (class, FALSE);
				}
				return;
			}

			if (subject_id == 0) {
				subject_id = ++(*max_id);
			}

			class = tracker_class_new (FALSE);
			tracker_class_set_ontologies (class, manager->ontologies);
			tracker_class_set_is_new (class, in_update);
			tracker_class_set_uri (class, subject);
			tracker_class_set_id (class, subject_id);
			tracker_ontologies_add_class (manager->ontologies, class);
			tracker_ontologies_add_id_uri_pair (manager->ontologies, subject_id, subject);

			if (seen_classes)
				g_ptr_array_add (seen_classes, g_object_ref (class));

			if (classes) {
				g_hash_table_insert (classes, GINT_TO_POINTER (subject_id), class);
			} else {
				g_object_unref (class);
			}

		} else if (g_strcmp0 (object, RDF_PROPERTY) == 0) {
			TrackerProperty *property;

			property = tracker_ontologies_get_property_by_uri (manager->ontologies, subject);
			if (property != NULL) {
				if (seen_properties)
					g_ptr_array_add (seen_properties, g_object_ref (property));
				if (!in_update) {
					g_critical ("%s: Duplicate definition of property %s", ontology_path, subject);
				} else {
					/* Reset for a correct post and pre-check */
					tracker_property_set_last_multiple_values (property, TRUE);
					tracker_property_reset_domain_indexes (property);
					tracker_property_reset_super_properties (property);
					tracker_property_set_indexed (property, FALSE);
					tracker_property_set_cardinality_changed (property, FALSE);
					tracker_property_set_secondary_index (property, NULL);
					tracker_property_set_writeback (property, FALSE);
					tracker_property_set_is_inverse_functional_property (property, FALSE);
					tracker_property_set_default_value (property, NULL);
					tracker_property_set_multiple_values (property, TRUE);
					tracker_property_set_fulltext_indexed (property, FALSE);
				}
				return;
			}

			if (subject_id == 0) {
				subject_id = ++(*max_id);
			}

			property = tracker_property_new (FALSE);
			tracker_property_set_ontologies (property, manager->ontologies);
			tracker_property_set_is_new (property, in_update);
			tracker_property_set_uri (property, subject);
			tracker_property_set_id (property, subject_id);
			tracker_property_set_multiple_values (property, TRUE);
			tracker_ontologies_add_property (manager->ontologies, property);
			tracker_ontologies_add_id_uri_pair (manager->ontologies, subject_id, subject);

			if (seen_properties)
				g_ptr_array_add (seen_properties, g_object_ref (property));

			if (properties) {
				g_hash_table_insert (properties, GINT_TO_POINTER (subject_id), property);
			} else {
				g_object_unref (property);
			}

		} else if (g_strcmp0 (object, NRL_INVERSE_FUNCTIONAL_PROPERTY) == 0) {
			TrackerProperty *property;

			property = tracker_ontologies_get_property_by_uri (manager->ontologies, subject);
			if (property == NULL) {
				g_critical ("%s: Unknown property %s", ontology_path, subject);
				return;
			}

			tracker_property_set_is_inverse_functional_property (property, TRUE);
		} else if (g_strcmp0 (object, TRACKER_PREFIX_TRACKER "Namespace") == 0) {
			TrackerNamespace *namespace;

			if (tracker_ontologies_get_namespace_by_uri (manager->ontologies, subject) != NULL) {
				if (!in_update)
					g_critical ("%s: Duplicate definition of namespace %s", ontology_path, subject);
				return;
			}

			namespace = tracker_namespace_new (FALSE);
			tracker_namespace_set_ontologies (namespace, manager->ontologies);
			tracker_namespace_set_is_new (namespace, in_update);
			tracker_namespace_set_uri (namespace, subject);
			tracker_ontologies_add_namespace (manager->ontologies, namespace);
			g_object_unref (namespace);

		} else if (g_strcmp0 (object, TRACKER_PREFIX_TRACKER "Ontology") == 0) {
			TrackerOntology *ontology;

			if (tracker_ontologies_get_ontology_by_uri (manager->ontologies, subject) != NULL) {
				if (!in_update)
					g_critical ("%s: Duplicate definition of ontology %s", ontology_path, subject);
				return;
			}

			ontology = tracker_ontology_new ();
			tracker_ontology_set_ontologies (ontology, manager->ontologies);
			tracker_ontology_set_is_new (ontology, in_update);
			tracker_ontology_set_uri (ontology, subject);
			tracker_ontologies_add_ontology (manager->ontologies, ontology);
			g_object_unref (ontology);

		}
	} else if (g_strcmp0 (predicate, RDFS_SUB_CLASS_OF) == 0) {
		TrackerClass *class, *super_class;
		gboolean is_new;

		class = tracker_ontologies_get_class_by_uri (manager->ontologies, subject);
		if (class == NULL) {
			g_critical ("%s: Unknown class %s", ontology_path, subject);
			return;
		}

		is_new = tracker_class_get_is_new (class);
		if (is_new != in_update) {
			gboolean ignore = FALSE;
			/* Detect unsupported ontology change */
			if (in_update == TRUE && is_new == FALSE && g_strcmp0 (object, TRACKER_PREFIX_RDFS "Resource") != 0) {
				TrackerClass **super_classes = tracker_class_get_super_classes (class);
				gboolean had = FALSE;

				super_class = tracker_ontologies_get_class_by_uri (manager->ontologies, object);
				if (super_class == NULL) {
					g_critical ("%s: Unknown class %s", ontology_path, object);
					return;
				}

				while (*super_classes) {
					if (*super_classes == super_class) {
						ignore = TRUE;
						g_debug ("%s: Class %s already has rdfs:subClassOf in %s",
						         ontology_path, object, subject);
						break;
					}
					super_classes++;
				}

				super_classes = tracker_class_get_last_super_classes (class);
				if (super_classes) {
					while (*super_classes) {
						if (super_class == *super_classes) {
							had = TRUE;
						}
						super_classes++;
					}
				}

				/* This doesn't detect removed rdfs:subClassOf situations, it
				 * only checks whether no new ones are being added. For
				 * detecting the removal of a rdfs:subClassOf, please check the
				 * tracker_data_ontology_process_changes_pre_db stuff */


				if (!ignore && !had) {
					handle_unsupported_ontology_change (manager,
					                                    ontology_path,
					                                    tracker_class_get_name (class),
					                                    "rdfs:subClassOf",
					                                    "-",
					                                    tracker_class_get_name (super_class),
					                                    error);
				}
			}

			if (!ignore) {
				super_class = tracker_ontologies_get_class_by_uri (manager->ontologies, object);
				tracker_class_add_super_class (class, super_class);
			}

			return;
		}

		super_class = tracker_ontologies_get_class_by_uri (manager->ontologies, object);
		if (super_class == NULL) {
			g_critical ("%s: Unknown class %s", ontology_path, object);
			return;
		}

		tracker_class_add_super_class (class, super_class);

	} else if (g_strcmp0 (predicate, TRACKER_PREFIX_TRACKER "notify") == 0) {
		TrackerClass *class;

		class = tracker_ontologies_get_class_by_uri (manager->ontologies, subject);

		if (class == NULL) {
			g_critical ("%s: Unknown class %s", ontology_path, subject);
			return;
		}

		tracker_class_set_notify (class, (strcmp (object, "true") == 0));
	} else if (g_strcmp0 (predicate, TRACKER_PREFIX_TRACKER "domainIndex") == 0) {
		TrackerClass *class;
		TrackerProperty *property;
		TrackerProperty **properties;
		gboolean ignore = FALSE;
		gboolean had = FALSE;
		guint n_props, i;

		class = tracker_ontologies_get_class_by_uri (manager->ontologies, subject);

		if (class == NULL) {
			g_critical ("%s: Unknown class %s", ontology_path, subject);
			return;
		}

		property = tracker_ontologies_get_property_by_uri (manager->ontologies, object);

		if (property == NULL) {

			/* In this case the import of the TTL will still make the introspection
			 * have the URI set as a tracker:domainIndex for class. The critical
			 * will have happened, but future operations might not cope with this
			 * situation. TODO: add error handling so that the entire ontology
			 * change operation is discarded, for example ignore the entire
			 * .ontology file and rollback all changes that happened. I would
			 * prefer a hard abort() here over a g_critical(), to be honest.
			 *
			 * Of course don't we yet allow just anybody to alter the ontology
			 * files. So very serious is this lack of thorough error handling
			 * not. Let's just not make mistakes when changing the .ontology
			 * files for now. */

			g_critical ("%s: Unknown property %s for tracker:domainIndex in %s."
			            "Don't release this .ontology change!",
			            ontology_path, object, subject);
			return;
		}

		if (tracker_property_get_multiple_values (property)) {
			g_critical ("%s: Property %s has multiple values while trying to add it as tracker:domainIndex in %s, this isn't supported",
			            ontology_path, object, subject);
			return;
		}

		properties = tracker_ontologies_get_properties (manager->ontologies, &n_props);
		for (i = 0; i < n_props; i++) {
			if (tracker_property_get_domain (properties[i]) == class &&
			    properties[i] == property) {
				g_critical ("%s: Property %s is already a first-class property of %s while trying to add it as tracker:domainIndex",
				            ontology_path, object, subject);
			}
		}

		properties = tracker_class_get_domain_indexes (class);
		while (*properties) {
			if (property == *properties) {
				g_debug ("%s: Property %s already a tracker:domainIndex in %s",
				         ontology_path, object, subject);
				ignore = TRUE;
			}
			properties++;
		}

		properties = tracker_class_get_last_domain_indexes (class);
		if (properties) {
			while (*properties) {
				if (property == *properties) {
					had = TRUE;
				}
				properties++;
			}
		}

		/* This doesn't detect removed tracker:domainIndex situations, it
		 * only checks whether no new ones are being added. For
		 * detecting the removal of a tracker:domainIndex, please check the
		 * tracker_data_ontology_process_changes_pre_db stuff */

		if (!ignore) {
			if (!had) {
				tracker_property_set_is_new_domain_index (property, class, in_update);
			}
			tracker_class_add_domain_index (class, property);
			tracker_property_add_domain_index (property, class);
		}

	} else if (g_strcmp0 (predicate, TRACKER_PREFIX_TRACKER "writeback") == 0) {
		TrackerProperty *property;

		property = tracker_ontologies_get_property_by_uri (manager->ontologies, subject);

		if (property == NULL) {
			g_critical ("%s: Unknown property %s", ontology_path, subject);
			return;
		}

		tracker_property_set_writeback (property, (strcmp (object, "true") == 0));
	} else if (g_strcmp0 (predicate, RDFS_SUB_PROPERTY_OF) == 0) {
		TrackerProperty *property, *super_property;
		gboolean is_new;

		property = tracker_ontologies_get_property_by_uri (manager->ontologies, subject);
		if (property == NULL) {
			g_critical ("%s: Unknown property %s", ontology_path, subject);
			return;
		}

		is_new = tracker_property_get_is_new (property);
		if (is_new != in_update) {
			gboolean ignore = FALSE;
			/* Detect unsupported ontology change */
			if (in_update == TRUE && is_new == FALSE) {
				TrackerProperty **super_properties = tracker_property_get_super_properties (property);
				gboolean had = FALSE;

				super_property = tracker_ontologies_get_property_by_uri (manager->ontologies, object);
				if (super_property == NULL) {
					g_critical ("%s: Unknown property %s", ontology_path, object);
					return;
				}

				while (*super_properties) {
					if (*super_properties == super_property) {
						ignore = TRUE;
						g_debug ("%s: Property %s already has rdfs:subPropertyOf in %s",
						         ontology_path, object, subject);
						break;
					}
					super_properties++;
				}

				super_properties = tracker_property_get_last_super_properties (property);
				if (super_properties) {
					while (*super_properties) {
						if (super_property == *super_properties) {
							had = TRUE;
						}
						super_properties++;
					}
				}

				/* This doesn't detect removed rdfs:subPropertyOf situations, it
				 * only checks whether no new ones are being added. For
				 * detecting the removal of a rdfs:subPropertyOf, please check the
				 * tracker_data_ontology_process_changes_pre_db stuff */

				if (!ignore && !had) {
					handle_unsupported_ontology_change (manager,
					                                    ontology_path,
					                                    tracker_property_get_name (property),
					                                    "rdfs:subPropertyOf",
					                                    "-",
					                                    tracker_property_get_name (super_property),
					                                    error);
				}
			}

			if (!ignore) {
				super_property = tracker_ontologies_get_property_by_uri (manager->ontologies, object);
				tracker_property_add_super_property (property, super_property);
			}

			return;
		}

		super_property = tracker_ontologies_get_property_by_uri (manager->ontologies, object);
		if (super_property == NULL) {
			g_critical ("%s: Unknown property %s", ontology_path, object);
			return;
		}

		tracker_property_add_super_property (property, super_property);
	} else if (g_strcmp0 (predicate, RDFS_DOMAIN) == 0) {
		TrackerProperty *property;
		TrackerClass *domain;
		gboolean is_new;

		property = tracker_ontologies_get_property_by_uri (manager->ontologies, subject);
		if (property == NULL) {
			g_critical ("%s: Unknown property %s", ontology_path, subject);
			return;
		}

		domain = tracker_ontologies_get_class_by_uri (manager->ontologies, object);
		if (domain == NULL) {
			g_critical ("%s: Unknown class %s", ontology_path, object);
			return;
		}

		is_new = tracker_property_get_is_new (property);
		if (is_new != in_update) {
			/* Detect unsupported ontology change */
			if (in_update == TRUE && is_new == FALSE) {
				TrackerClass *old_domain = tracker_property_get_domain (property);
				if (old_domain != domain) {
					handle_unsupported_ontology_change (manager,
					                                    ontology_path,
					                                    tracker_property_get_name (property),
					                                    "rdfs:domain",
					                                    tracker_class_get_name (old_domain),
					                                    tracker_class_get_name (domain),
					                                    error);
				}
			}
			return;
		}

		tracker_property_set_domain (property, domain);
	} else if (g_strcmp0 (predicate, RDFS_RANGE) == 0) {
		TrackerProperty *property;
		TrackerClass *range;

		property = tracker_ontologies_get_property_by_uri (manager->ontologies, subject);
		if (property == NULL) {
			g_critical ("%s: Unknown property %s", ontology_path, subject);
			return;
		}

		if (tracker_property_get_is_new (property) != in_update) {
			GError *err = NULL;
			check_range_conversion_is_allowed (manager,
			                                   ontology_path,
			                                   subject,
			                                   predicate,
			                                   object,
			                                   &err);
			if (err) {
				g_propagate_error (error, err);
				return;
			}
		}

		range = tracker_ontologies_get_class_by_uri (manager->ontologies, object);
		if (range == NULL) {
			g_critical ("%s: Unknown class %s", ontology_path, object);
			return;
		}

		tracker_property_set_range (property, range);
	} else if (g_strcmp0 (predicate, NRL_MAX_CARDINALITY) == 0) {
		TrackerProperty *property;

		property = tracker_ontologies_get_property_by_uri (manager->ontologies, subject);
		if (property == NULL) {
			g_critical ("%s: Unknown property %s", ontology_path, subject);
			return;
		}

		if (atoi (object) == 1) {
			tracker_property_set_multiple_values (property, FALSE);
			tracker_property_set_last_multiple_values (property, FALSE);
		} else {
			tracker_property_set_multiple_values (property, TRUE);
			tracker_property_set_last_multiple_values (property, TRUE);
		}

	} else if (g_strcmp0 (predicate, TRACKER_PREFIX_TRACKER "indexed") == 0) {
		TrackerProperty *property;

		property = tracker_ontologies_get_property_by_uri (manager->ontologies, subject);
		if (property == NULL) {
			g_critical ("%s: Unknown property %s", ontology_path, subject);
			return;
		}

		tracker_property_set_indexed (property, (strcmp (object, "true") == 0));
	} else if (g_strcmp0 (predicate, TRACKER_PREFIX_TRACKER "secondaryIndex") == 0) {
		TrackerProperty *property, *secondary_index;

		property = tracker_ontologies_get_property_by_uri (manager->ontologies, subject);
		if (property == NULL) {
			g_critical ("%s: Unknown property %s", ontology_path, subject);
			return;
		}

		secondary_index = tracker_ontologies_get_property_by_uri (manager->ontologies, object);
		if (secondary_index == NULL) {
			g_critical ("%s: Unknown property %s", ontology_path, object);
			return;
		}

		tracker_property_set_secondary_index (property, secondary_index);
	} else if (g_strcmp0 (predicate, TRACKER_PREFIX_TRACKER "transient") == 0) {
		TrackerProperty *property;
		gboolean is_new;

		property = tracker_ontologies_get_property_by_uri (manager->ontologies, subject);
		if (property == NULL) {
			g_critical ("%s: Unknown property %s", ontology_path, subject);
			return;
		}

		is_new = tracker_property_get_is_new (property);
		if (is_new != in_update) {
			/* Detect unsupported ontology change.
			 * Wouldn't be very hard to support this, just dropping the tabtle
			 * or creating the table in the-non memdisk db file, but afaik this
			 * isn't supported right now */
			if (in_update == TRUE && is_new == FALSE) {
				if (check_unsupported_property_value_change (manager,
				                                             ontology_path,
				                                             "tracker:transient",
				                                             subject,
				                                             predicate,
				                                             object)) {
					handle_unsupported_ontology_change (manager,
					                                    ontology_path,
					                                    tracker_property_get_name (property),
					                                    "tracker:transient",
					                                    tracker_property_get_transient (property) ? "true" : "false",
					                                    g_strcmp0 (object, "true") ==0 ? "true" : "false",
					                                    error);
				}
			}
			return;
		}

		if (g_strcmp0 (object, "true") == 0) {
			tracker_property_set_transient (property, TRUE);
		}
	} else if (g_strcmp0 (predicate, TRACKER_PREFIX_TRACKER "fulltextIndexed") == 0) {
		TrackerProperty *property;

		property = tracker_ontologies_get_property_by_uri (manager->ontologies, subject);
		if (property == NULL) {
			g_critical ("%s: Unknown property %s", ontology_path, subject);
			return;
		}

		tracker_property_set_fulltext_indexed (property,
		                                       strcmp (object, "true") == 0);
	} else if (g_strcmp0 (predicate, TRACKER_PREFIX_TRACKER "defaultValue") == 0) {
		TrackerProperty *property;

		property = tracker_ontologies_get_property_by_uri (manager->ontologies, subject);
		if (property == NULL) {
			g_critical ("%s: Unknown property %s", ontology_path, subject);
			return;
		}

		tracker_property_set_default_value (property, object);
	} else if (g_strcmp0 (predicate, TRACKER_PREFIX_TRACKER "prefix") == 0) {
		TrackerNamespace *namespace;

		namespace = tracker_ontologies_get_namespace_by_uri (manager->ontologies, subject);
		if (namespace == NULL) {
			g_critical ("%s: Unknown namespace %s", ontology_path, subject);
			return;
		}

		if (tracker_namespace_get_is_new (namespace) != in_update) {
			return;
		}

		tracker_namespace_set_prefix (namespace, object);
	} else if (g_strcmp0 (predicate, NAO_LAST_MODIFIED) == 0) {
		TrackerOntology *ontology;

		ontology = tracker_ontologies_get_ontology_by_uri (manager->ontologies, subject);
		if (ontology == NULL) {
			g_critical ("%s: Unknown ontology %s", ontology_path, subject);
			return;
		}

		if (tracker_ontology_get_is_new (ontology) != in_update) {
			return;
		}

		tracker_ontology_set_last_modified (ontology, tracker_string_to_date (object, NULL, NULL));
	}
}


static void
check_for_deleted_domain_index (TrackerDataManager *manager,
                                TrackerClass       *class)
{
	TrackerProperty **last_domain_indexes;
	GSList *hfound = NULL, *deleted = NULL;

	last_domain_indexes = tracker_class_get_last_domain_indexes (class);

	if (!last_domain_indexes) {
		return;
	}

	while (*last_domain_indexes) {
		TrackerProperty *last_domain_index = *last_domain_indexes;
		gboolean found = FALSE;
		TrackerProperty **domain_indexes;

		domain_indexes = tracker_class_get_domain_indexes (class);

		while (*domain_indexes) {
			TrackerProperty *domain_index = *domain_indexes;
			if (last_domain_index == domain_index) {
				found = TRUE;
				hfound = g_slist_prepend (hfound, domain_index);
				break;
			}
			domain_indexes++;
		}

		if (!found) {
			deleted = g_slist_prepend (deleted, last_domain_index);
		}

		last_domain_indexes++;
	}


	if (deleted) {
		GSList *l;
		TrackerProperty **properties;
		guint n_props, i;

		tracker_class_set_db_schema_changed (class, TRUE);

		properties = tracker_ontologies_get_properties (manager->ontologies, &n_props);
		for (i = 0; i < n_props; i++) {
			if (tracker_property_get_domain (properties[i]) == class &&
			    !tracker_property_get_multiple_values (properties[i])) {

				/* These aren't domain-indexes, but it's just a flag for the
				 * functionality that'll recreate the table to know that the
				 * property must be involved in the recreation and copy */

				tracker_property_set_is_new_domain_index (properties[i], class, TRUE);
			}
		}

		for (l = hfound; l != NULL; l = l->next) {
			TrackerProperty *prop = l->data;
			g_debug ("Ontology change: keeping tracker:domainIndex: %s",
			         tracker_property_get_name (prop));
			tracker_property_set_is_new_domain_index (prop, class, TRUE);
		}

		for (l = deleted; l != NULL; l = l->next) {
			GError *error = NULL;
			TrackerProperty *prop = l->data;

			g_debug ("Ontology change: deleting tracker:domainIndex: %s",
			         tracker_property_get_name (prop));
			tracker_property_del_domain_index (prop, class);
			tracker_class_del_domain_index (class, prop);

			tracker_data_delete_statement (manager->data_update, NULL,
			                               tracker_class_get_uri (class),
			                               TRACKER_PREFIX_TRACKER "domainIndex",
			                               tracker_property_get_uri (prop),
			                               &error);

			if (error) {
				g_critical ("Ontology change, %s", error->message);
				g_clear_error (&error);
			} else {
				tracker_data_update_buffer_flush (manager->data_update, &error);
				if (error) {
					g_critical ("Ontology change, %s", error->message);
					g_clear_error (&error);
				}
			}
		}

		g_slist_free (deleted);
	}

	g_slist_free (hfound);
}

static void
check_for_deleted_super_classes (TrackerDataManager  *manager,
                                 TrackerClass        *class,
                                 GError             **error)
{
	TrackerClass **last_super_classes;

	last_super_classes = tracker_class_get_last_super_classes (class);

	if (!last_super_classes) {
		return;
	}

	while (*last_super_classes) {
		TrackerClass *last_super_class = *last_super_classes;
		gboolean found = FALSE;
		TrackerClass **super_classes;

		if (g_strcmp0 (tracker_class_get_uri (last_super_class), TRACKER_PREFIX_RDFS "Resource") == 0) {
			last_super_classes++;
			continue;
		}

		super_classes = tracker_class_get_super_classes (class);

		while (*super_classes) {
			TrackerClass *super_class = *super_classes;

			if (last_super_class == super_class) {
				found = TRUE;
				break;
			}
			super_classes++;
		}

		if (!found) {
			const gchar *ontology_path = "Unknown";
			const gchar *subject = tracker_class_get_uri (class);

			handle_unsupported_ontology_change (manager,
			                                    ontology_path,
			                                    subject,
			                                    "rdfs:subClassOf", "-", "-",
			                                    error);
			return;
		}

		last_super_classes++;
	}
}

static void
check_for_max_cardinality_change (TrackerDataManager  *manager,
                                  TrackerProperty     *property,
                                  GError             **error)
{
	gboolean orig_multiple_values = tracker_property_get_orig_multiple_values (property);
	gboolean new_multiple_values = tracker_property_get_multiple_values (property);
	GError *n_error = NULL;
	const gchar *ontology_path = "Unknown";

	if (tracker_property_get_is_new (property) == FALSE &&
	    (orig_multiple_values != new_multiple_values &&
		 orig_multiple_values == TRUE)) {
		const gchar *ontology_path = "Unknown";
		const gchar *subject = tracker_property_get_uri (property);

		handle_unsupported_ontology_change (manager,
		                                    ontology_path,
		                                    subject,
		                                    "nrl:maxCardinality", "none", "1",
		                                    &n_error);
		if (n_error) {
			g_propagate_error (error, n_error);
			return;
		}
	} else if (tracker_property_get_is_new (property) == FALSE &&
	           orig_multiple_values != new_multiple_values &&
	           orig_multiple_values == FALSE) {
		const gchar *subject = tracker_property_get_uri (property);

		if (update_property_value (manager, ontology_path,
		                           "nrl:maxCardinality",
		                           subject,
		                           TRACKER_PREFIX_NRL "maxCardinality",
		                           NULL, allowed_cardinality_conversions,
		                           NULL, property,
		                           &n_error)) {
			TrackerClass *class;
			class = tracker_property_get_domain(property);

			tracker_property_set_db_schema_changed (property, TRUE);
			tracker_property_set_cardinality_changed (property, TRUE);
			tracker_class_set_db_schema_changed (class, TRUE);
		}

		if (n_error) {
			g_propagate_error (error, n_error);
			return;
		}
	}
}

static void
check_for_deleted_super_properties (TrackerDataManager  *manager,
                                    TrackerProperty     *property,
                                    GError             **error)
{
	TrackerProperty **last_super_properties;
	GList *to_remove = NULL;

	last_super_properties = tracker_property_get_last_super_properties (property);

	if (!last_super_properties) {
		return;
	}

	while (*last_super_properties) {
		TrackerProperty *last_super_property = *last_super_properties;
		gboolean found = FALSE;
		TrackerProperty **super_properties;

		super_properties = tracker_property_get_super_properties (property);

		while (*super_properties) {
			TrackerProperty *super_property = *super_properties;

			if (last_super_property == super_property) {
				found = TRUE;
				break;
			}
			super_properties++;
		}

		if (!found) {
			to_remove = g_list_prepend (to_remove, last_super_property);
		}

		last_super_properties++;
	}

	if (to_remove) {
		GList *copy = to_remove;

		while (copy) {
			GError *n_error = NULL;
			TrackerProperty *prop_to_remove = copy->data;
			const gchar *object = tracker_property_get_uri (prop_to_remove);
			const gchar *subject = tracker_property_get_uri (property);

			tracker_property_del_super_property (property, prop_to_remove);

			tracker_data_delete_statement (manager->data_update, NULL, subject,
			                               TRACKER_PREFIX_RDFS "subPropertyOf",
			                               object, &n_error);

			if (!n_error) {
				tracker_data_update_buffer_flush (manager->data_update, &n_error);
			}

			if (n_error) {
				g_propagate_error (error, n_error);
				return;
			}

			copy = copy->next;
		}
		g_list_free (to_remove);
	}
}

static void
tracker_data_ontology_process_changes_pre_db (TrackerDataManager  *manager,
                                              GPtrArray           *seen_classes,
                                              GPtrArray           *seen_properties,
                                              GError             **error)
{
	gint i;
	if (seen_classes) {
		for (i = 0; i < seen_classes->len; i++) {
			GError *n_error = NULL;
			TrackerClass *class = g_ptr_array_index (seen_classes, i);

			check_for_deleted_domain_index (manager, class);
			check_for_deleted_super_classes (manager, class, &n_error);

			if (n_error) {
				g_propagate_error (error, n_error);
				return;
			}
		}
	}

	if (seen_properties) {
		for (i = 0; i < seen_properties->len; i++) {
			GError *n_error = NULL;
			TrackerProperty *property = g_ptr_array_index (seen_properties, i);

			check_for_max_cardinality_change (manager, property, &n_error);

			if (n_error) {
				g_propagate_error (error, n_error);
				return;
			}

			check_for_deleted_super_properties (manager, property, &n_error);

			if (n_error) {
				g_propagate_error (error, n_error);
				return;
			}
		}
	}
}

static void
tracker_data_ontology_process_changes_post_db (TrackerDataManager  *manager,
                                               GPtrArray           *seen_classes,
                                               GPtrArray           *seen_properties,
                                               GError             **error)
{
	gint i;
	/* TODO: Collect the ontology-paths of the seen events for proper error reporting */
	const gchar *ontology_path = "Unknown";

	/* This updates property-property changes and marks classes for necessity
	 * of having their tables recreated later. There's support for
	 * tracker:notify, tracker:writeback and tracker:indexed */

	if (seen_classes) {
		for (i = 0; i < seen_classes->len; i++) {
			TrackerClass *class = g_ptr_array_index (seen_classes, i);
			const gchar *subject;
			GError *n_error = NULL;

			subject = tracker_class_get_uri (class);

			if (tracker_class_get_notify (class)) {
				update_property_value (manager, ontology_path,
				                       "tracker:notify",
				                       subject,
				                       TRACKER_PREFIX_TRACKER "notify",
				                       "true", allowed_boolean_conversions,
				                       class, NULL, &n_error);
			} else {
				update_property_value (manager, ontology_path,
				                       "tracker:notify",
				                       subject,
				                       TRACKER_PREFIX_TRACKER "notify",
				                       "false", allowed_boolean_conversions,
				                       class, NULL, &n_error);
			}

			if (n_error) {
				g_propagate_error (error, n_error);
				return;
			}
		}
	}

	if (seen_properties) {
		for (i = 0; i < seen_properties->len; i++) {
			TrackerProperty *property = g_ptr_array_index (seen_properties, i);
			const gchar *subject;
			gchar *query;
			TrackerProperty *secondary_index;
			gboolean indexed_set = FALSE, in_onto;
			GError *n_error = NULL;
			TrackerSparqlCursor *cursor;

			subject = tracker_property_get_uri (property);

			/* Check for nrl:InverseFunctionalProperty changes (not supported) */
			in_onto = tracker_property_get_is_inverse_functional_property (property);

			query = g_strdup_printf ("ASK { <%s> a nrl:InverseFunctionalProperty }", subject);
			cursor = TRACKER_SPARQL_CURSOR (tracker_data_query_sparql_cursor (manager, query, &n_error));
			g_free (query);

			if (n_error) {
				g_propagate_error (error, n_error);
				return;
			}

			if (tracker_sparql_cursor_next (cursor, NULL, NULL)) {
				if (tracker_sparql_cursor_get_boolean (cursor, 0) != in_onto) {
					handle_unsupported_ontology_change (manager,
					                                    ontology_path,
					                                    subject,
					                                    "nrl:InverseFunctionalProperty", "-", "-",
					                                    &n_error);

					if (n_error) {
						g_object_unref (cursor);
						g_propagate_error (error, n_error);
						return;
					}
				}
			}

			if (cursor) {
				g_object_unref (cursor);
			}

			/* Check for possibly supported changes */
			if (tracker_property_get_writeback (property)) {
				update_property_value (manager, ontology_path,
				                       "tracker:writeback",
				                       subject,
				                       TRACKER_PREFIX_TRACKER "writeback",
				                       "true", allowed_boolean_conversions,
				                       NULL, property, &n_error);
			} else {
				update_property_value (manager, ontology_path,
				                       "tracker:writeback",
				                       subject,
				                       TRACKER_PREFIX_TRACKER "writeback",
				                       "false", allowed_boolean_conversions,
				                       NULL, property, &n_error);
			}

			if (n_error) {
				g_propagate_error (error, n_error);
				return;
			}

			if (tracker_property_get_indexed (property)) {
				if (update_property_value (manager, ontology_path,
				                           "tracker:indexed",
				                           subject,
				                           TRACKER_PREFIX_TRACKER "indexed",
				                           "true", allowed_boolean_conversions,
				                           NULL, property, &n_error)) {
					fix_indexed (manager, property, TRUE, &n_error);
					indexed_set = TRUE;
				}
			} else {
				if (update_property_value (manager, ontology_path,
				                           "tracker:indexed",
				                           subject,
				                           TRACKER_PREFIX_TRACKER "indexed",
				                           "false", allowed_boolean_conversions,
				                           NULL, property, &n_error)) {
					fix_indexed (manager, property, TRUE, &n_error);
					indexed_set = TRUE;
				}
			}

			if (n_error) {
				g_propagate_error (error, n_error);
				return;
			}

			secondary_index = tracker_property_get_secondary_index (property);

			if (secondary_index) {
				if (update_property_value (manager, ontology_path,
				                           "tracker:secondaryIndex",
				                           subject,
				                           TRACKER_PREFIX_TRACKER "secondaryIndex",
				                           tracker_property_get_uri (secondary_index), NULL,
				                           NULL, property, &n_error)) {
					if (!indexed_set) {
						fix_indexed (manager, property, TRUE, &n_error);
					}
				}
			} else {
				if (update_property_value (manager, ontology_path,
				                           "tracker:secondaryIndex",
				                           subject,
				                           TRACKER_PREFIX_TRACKER "secondaryIndex",
				                           NULL, NULL,
				                           NULL, property, &n_error)) {
					if (!indexed_set) {
						fix_indexed (manager, property, TRUE, &n_error);
					}
				}
			}

			if (n_error) {
				g_propagate_error (error, n_error);
				return;
			}

			if (update_property_value (manager, ontology_path,
			                           "rdfs:range", subject, TRACKER_PREFIX_RDFS "range",
			                           tracker_class_get_uri (tracker_property_get_range (property)),
			                           allowed_range_conversions,
			                           NULL, property, &n_error)) {
				TrackerClass *class;

				class = tracker_property_get_domain (property);
				tracker_class_set_db_schema_changed (class, TRUE);
				tracker_property_set_db_schema_changed (property, TRUE);
			}

			if (n_error) {
				g_propagate_error (error, n_error);
				return;
			}

			if (update_property_value (manager, ontology_path,
			                           "tracker:defaultValue", subject, TRACKER_PREFIX_TRACKER "defaultValue",
			                           tracker_property_get_default_value (property),
			                           NULL, NULL, property, &n_error)) {
				TrackerClass *class;

				class = tracker_property_get_domain (property);
				tracker_class_set_db_schema_changed (class, TRUE);
				tracker_property_set_db_schema_changed (property, TRUE);
			}

			if (n_error) {
				g_propagate_error (error, n_error);
			}
		}
	}
}

static void
tracker_data_ontology_process_changes_post_import (GPtrArray *seen_classes,
                                                   GPtrArray *seen_properties)
{
	return;
}

static void
tracker_data_ontology_free_seen (GPtrArray *seen)
{
	if (seen) {
		g_ptr_array_foreach (seen, (GFunc) g_object_unref, NULL);
		g_ptr_array_free (seen, TRUE);
	}
}

static void
load_ontology_file (TrackerDataManager  *manager,
                    GFile               *file,
                    gint                *max_id,
                    gboolean             in_update,
                    GPtrArray           *seen_classes,
                    GPtrArray           *seen_properties,
                    GHashTable          *uri_id_map,
                    GError             **error)
{
	TrackerTurtleReader *reader;
	GError              *ttl_error = NULL;
	gchar               *ontology_uri;

	reader = tracker_turtle_reader_new (file, &ttl_error);

	if (ttl_error) {
		g_propagate_error (error, ttl_error);
		return;
	}

	ontology_uri = g_file_get_uri (file);

	/* Post checks are only needed for ontology updates, not the initial
	 * ontology */

	while (ttl_error == NULL && tracker_turtle_reader_next (reader, &ttl_error)) {
		const gchar *subject, *predicate, *object;
		gint subject_id = 0;
		GError *ontology_error = NULL;

		subject = tracker_turtle_reader_get_subject (reader);
		predicate = tracker_turtle_reader_get_predicate (reader);
		object = tracker_turtle_reader_get_object (reader);

		if (uri_id_map) {
			subject_id = GPOINTER_TO_INT (g_hash_table_lookup (uri_id_map, subject));
		}

		tracker_data_ontology_load_statement (manager, ontology_uri,
		                                      subject_id, subject, predicate, object,
		                                      max_id, in_update, NULL, NULL,
		                                      seen_classes, seen_properties, &ontology_error);

		if (ontology_error) {
			g_propagate_error (error, ontology_error);
			break;
		}
	}

	g_free (ontology_uri);
	g_object_unref (reader);

	if (ttl_error) {
		g_propagate_error (error, ttl_error);
	}
}


static TrackerOntology*
get_ontology_from_file (TrackerDataManager *manager,
                        GFile              *file)
{
	TrackerTurtleReader *reader;
	GError *error = NULL;
	GHashTable *ontology_uris;
	TrackerOntology *ret = NULL;

	reader = tracker_turtle_reader_new (file, &error);

	if (error) {
		g_critical ("Turtle parse error: %s", error->message);
		g_error_free (error);
		return NULL;
	}

	ontology_uris = g_hash_table_new_full (g_str_hash,
	                                       g_str_equal,
	                                       g_free,
	                                       g_object_unref);

	while (error == NULL && tracker_turtle_reader_next (reader, &error)) {
		const gchar *subject, *predicate, *object;

		subject = tracker_turtle_reader_get_subject (reader);
		predicate = tracker_turtle_reader_get_predicate (reader);
		object = tracker_turtle_reader_get_object (reader);

		if (g_strcmp0 (predicate, RDF_TYPE) == 0) {
			if (g_strcmp0 (object, TRACKER_PREFIX_TRACKER "Ontology") == 0) {
				TrackerOntology *ontology;

				ontology = tracker_ontology_new ();
				tracker_ontology_set_ontologies (ontology, manager->ontologies);
				tracker_ontology_set_uri (ontology, subject);

				/* Passes ownership */
				g_hash_table_insert (ontology_uris,
				                     g_strdup (subject),
				                     ontology);
			}
		} else if (g_strcmp0 (predicate, NAO_LAST_MODIFIED) == 0) {
			TrackerOntology *ontology;

			ontology = g_hash_table_lookup (ontology_uris, subject);
			if (ontology == NULL) {
				gchar *uri = g_file_get_uri (file);
				g_critical ("%s: Unknown ontology %s", uri, subject);
				g_free (uri);
				return NULL;
			}

			tracker_ontology_set_last_modified (ontology, tracker_string_to_date (object, NULL, NULL));

			/* This one is here because lower ontology_uris is destroyed, and
			 * else would this one's reference also be destroyed with it */
			ret = g_object_ref (ontology);

			break;
		}
	}

	g_hash_table_unref (ontology_uris);
	g_object_unref (reader);

	if (error) {
		g_critical ("Turtle parse error: %s", error->message);
		g_error_free (error);
	}

	if (ret == NULL) {
		gchar *uri = g_file_get_uri (file);
		g_critical ("Ontology file has no nao:lastModified header: %s", uri);
		g_free (uri);
	}

	return ret;
}

static void
tracker_data_ontology_process_statement (TrackerDataManager *manager,
                                         const gchar        *subject,
                                         const gchar        *predicate,
                                         const gchar        *object,
                                         gboolean            is_uri,
                                         gboolean            in_update)
{
	GError *error = NULL;

	if (g_strcmp0 (predicate, RDF_TYPE) == 0) {
		if (g_strcmp0 (object, RDFS_CLASS) == 0) {
			TrackerClass *class;

			class = tracker_ontologies_get_class_by_uri (manager->ontologies, subject);

			if (class && tracker_class_get_is_new (class) != in_update) {
				return;
			}
		} else if (g_strcmp0 (object, RDF_PROPERTY) == 0) {
			TrackerProperty *prop;

			prop = tracker_ontologies_get_property_by_uri (manager->ontologies, subject);

			if (prop && tracker_property_get_is_new (prop) != in_update) {
				return;
			}
		} else if (g_strcmp0 (object, TRACKER_PREFIX_TRACKER "Namespace") == 0) {
			TrackerNamespace *namespace;

			namespace = tracker_ontologies_get_namespace_by_uri (manager->ontologies, subject);

			if (namespace && tracker_namespace_get_is_new (namespace) != in_update) {
				return;
			}
		} else if (g_strcmp0 (object, TRACKER_PREFIX_TRACKER "Ontology") == 0) {
			TrackerOntology *ontology;

			ontology = tracker_ontologies_get_ontology_by_uri (manager->ontologies, subject);

			if (ontology && tracker_ontology_get_is_new (ontology) != in_update) {
				return;
			}
		}
	} else if (g_strcmp0 (predicate, RDFS_SUB_CLASS_OF) == 0) {
		TrackerClass *class;

		class = tracker_ontologies_get_class_by_uri (manager->ontologies, subject);

		if (class && tracker_class_get_is_new (class) != in_update) {
			return;
		}
	} else if (g_strcmp0 (predicate, RDFS_SUB_PROPERTY_OF) == 0          ||
	           g_strcmp0 (predicate, RDFS_DOMAIN) == 0                   ||
	           g_strcmp0 (predicate, RDFS_RANGE) == 0                    ||
	           /* g_strcmp0 (predicate, NRL_MAX_CARDINALITY) == 0        || */
	           g_strcmp0 (predicate, TRACKER_PREFIX_TRACKER "indexed") == 0      ||
	           g_strcmp0 (predicate, TRACKER_PREFIX_TRACKER "transient") == 0    ||
	           g_strcmp0 (predicate, TRACKER_PREFIX_TRACKER "fulltextIndexed") == 0) {
		TrackerProperty *prop;

		prop = tracker_ontologies_get_property_by_uri (manager->ontologies, subject);

		if (prop && tracker_property_get_is_new (prop) != in_update) {
			return;
		}
	} else if (g_strcmp0 (predicate, TRACKER_PREFIX_TRACKER "prefix") == 0) {
		TrackerNamespace *namespace;

		namespace = tracker_ontologies_get_namespace_by_uri (manager->ontologies, subject);

		if (namespace && tracker_namespace_get_is_new (namespace) != in_update) {
			return;
		}
	} else if (g_strcmp0 (predicate, NAO_LAST_MODIFIED) == 0) {
		TrackerOntology *ontology;

		ontology = tracker_ontologies_get_ontology_by_uri (manager->ontologies, subject);

		if (ontology && tracker_ontology_get_is_new (ontology) != in_update) {
			return;
		}
	}

	if (is_uri) {
		tracker_data_insert_statement_with_uri (manager->data_update, NULL, subject,
		                                        predicate, object,
		                                        &error);

		if (error != NULL) {
			g_critical ("%s", error->message);
			g_error_free (error);
			return;
		}

	} else {
		tracker_data_insert_statement_with_string (manager->data_update, NULL, subject,
		                                           predicate, object,
		                                           &error);

		if (error != NULL) {
			g_critical ("%s", error->message);
			g_error_free (error);
			return;
		}
	}
}

static void
import_ontology_file (TrackerDataManager *manager,
                      GFile              *file,
                      gboolean            in_update)
{
	GError *error = NULL;
	TrackerTurtleReader* reader;

	reader = tracker_turtle_reader_new (file, &error);

	if (error != NULL) {
		g_critical ("%s", error->message);
		g_error_free (error);
		return;
	}

	while (tracker_turtle_reader_next (reader, &error)) {
		const gchar *subject = tracker_turtle_reader_get_subject (reader);
		const gchar *predicate = tracker_turtle_reader_get_predicate (reader);
		const gchar *object  = tracker_turtle_reader_get_object (reader);

		tracker_data_ontology_process_statement (manager,
		                                         subject, predicate, object,
		                                         tracker_turtle_reader_get_object_is_uri (reader),
		                                         in_update);
	}

	g_object_unref (reader);

	if (error) {
		g_critical ("%s", error->message);
		g_error_free (error);
	}
}

static void
class_add_super_classes_from_db (TrackerDBInterface *iface,
                                 TrackerDataManager *manager,
                                 TrackerClass       *class)
{
	TrackerDBStatement *stmt;
	TrackerDBCursor *cursor;
	GError *error = NULL;

	stmt = tracker_db_interface_create_statement (iface, TRACKER_DB_STATEMENT_CACHE_TYPE_SELECT, &error,
	                                              "SELECT (SELECT Uri FROM Resource WHERE ID = \"rdfs:subClassOf\") "
	                                              "FROM \"rdfs:Class_rdfs:subClassOf\" "
	                                              "WHERE ID = (SELECT ID FROM Resource WHERE Uri = ?)");

	if (!stmt) {
		g_critical ("%s", error->message);
		g_error_free (error);
		return;
	}

	tracker_db_statement_bind_text (stmt, 0, tracker_class_get_uri (class));
	cursor = tracker_db_statement_start_cursor (stmt, NULL);
	g_object_unref (stmt);

	if (cursor) {
		while (tracker_db_cursor_iter_next (cursor, NULL, NULL)) {
			TrackerClass *super_class;
			const gchar *super_class_uri;

			super_class_uri = tracker_db_cursor_get_string (cursor, 0, NULL);
			super_class = tracker_ontologies_get_class_by_uri (manager->ontologies, super_class_uri);
			tracker_class_add_super_class (class, super_class);
		}

		g_object_unref (cursor);
	}
}


static void
class_add_domain_indexes_from_db (TrackerDBInterface *iface,
                                  TrackerDataManager *manager,
                                  TrackerClass       *class)
{
	TrackerDBStatement *stmt;
	TrackerDBCursor *cursor;
	GError *error = NULL;

	stmt = tracker_db_interface_create_statement (iface, TRACKER_DB_STATEMENT_CACHE_TYPE_SELECT, &error,
	                                              "SELECT (SELECT Uri FROM Resource WHERE ID = \"tracker:domainIndex\") "
	                                              "FROM \"rdfs:Class_tracker:domainIndex\" "
	                                              "WHERE ID = (SELECT ID FROM Resource WHERE Uri = ?)");

	if (!stmt) {
		g_critical ("%s", error->message);
		g_error_free (error);
		return;
	}

	tracker_db_statement_bind_text (stmt, 0, tracker_class_get_uri (class));
	cursor = tracker_db_statement_start_cursor (stmt, NULL);
	g_object_unref (stmt);

	if (cursor) {
		while (tracker_db_cursor_iter_next (cursor, NULL, NULL)) {
			TrackerProperty *domain_index;
			const gchar *domain_index_uri;

			domain_index_uri = tracker_db_cursor_get_string (cursor, 0, NULL);
			domain_index = tracker_ontologies_get_property_by_uri (manager->ontologies, domain_index_uri);
			tracker_class_add_domain_index (class, domain_index);
			tracker_property_add_domain_index (domain_index, class);
		}

		g_object_unref (cursor);
	}
}

static void
property_add_super_properties_from_db (TrackerDBInterface *iface,
                                       TrackerDataManager *manager,
                                       TrackerProperty    *property)
{
	TrackerDBStatement *stmt;
	TrackerDBCursor *cursor;
	GError *error = NULL;

	stmt = tracker_db_interface_create_statement (iface, TRACKER_DB_STATEMENT_CACHE_TYPE_SELECT, &error,
	                                              "SELECT (SELECT Uri FROM Resource WHERE ID = \"rdfs:subPropertyOf\") "
	                                              "FROM \"rdf:Property_rdfs:subPropertyOf\" "
	                                              "WHERE ID = (SELECT ID FROM Resource WHERE Uri = ?)");

	if (!stmt) {
		g_critical ("%s", error->message);
		g_error_free (error);
		return;
	}

	tracker_db_statement_bind_text (stmt, 0, tracker_property_get_uri (property));
	cursor = tracker_db_statement_start_cursor (stmt, NULL);
	g_object_unref (stmt);

	if (cursor) {
		while (tracker_db_cursor_iter_next (cursor, NULL, NULL)) {
			TrackerProperty *super_property;
			const gchar *super_property_uri;

			super_property_uri = tracker_db_cursor_get_string (cursor, 0, NULL);
			super_property = tracker_ontologies_get_property_by_uri (manager->ontologies, super_property_uri);
			tracker_property_add_super_property (property, super_property);
		}

		g_object_unref (cursor);
	}
}

static void
db_get_static_data (TrackerDBInterface  *iface,
                    TrackerDataManager  *manager,
                    GError             **error)
{
	TrackerDBStatement *stmt;
	TrackerDBCursor *cursor = NULL;
	TrackerClass **classes;
	guint n_classes, i;
	GError *internal_error = NULL;

	stmt = tracker_db_interface_create_statement (iface, TRACKER_DB_STATEMENT_CACHE_TYPE_SELECT, &internal_error,
	                                              "SELECT (SELECT Uri FROM Resource WHERE ID = \"tracker:Ontology\".ID), "
	                                              "       (SELECT \"nao:lastModified\" FROM \"rdfs:Resource\" WHERE ID = \"tracker:Ontology\".ID) "
	                                              "FROM \"tracker:Ontology\"");

	if (stmt) {
		cursor = tracker_db_statement_start_cursor (stmt, &internal_error);
		g_object_unref (stmt);
	}

	if (cursor) {
		while (tracker_db_cursor_iter_next (cursor, NULL, &internal_error)) {
			TrackerOntology *ontology;
			const gchar     *uri;
			time_t           last_mod;

			ontology = tracker_ontology_new ();
			tracker_ontology_set_ontologies (ontology, manager->ontologies);

			uri = tracker_db_cursor_get_string (cursor, 0, NULL);
			last_mod = (time_t) tracker_db_cursor_get_int (cursor, 1);

			tracker_ontology_set_is_new (ontology, FALSE);
			tracker_ontology_set_uri (ontology, uri);
			tracker_ontology_set_last_modified (ontology, last_mod);
			tracker_ontologies_add_ontology (manager->ontologies, ontology);

			g_object_unref (ontology);
		}

		g_object_unref (cursor);
		cursor = NULL;
	}

	if (internal_error) {
		g_propagate_error (error, internal_error);
		return;
	}

	stmt = tracker_db_interface_create_statement (iface, TRACKER_DB_STATEMENT_CACHE_TYPE_SELECT, &internal_error,
	                                              "SELECT (SELECT Uri FROM Resource WHERE ID = \"tracker:Namespace\".ID), "
	                                              "\"tracker:prefix\" "
	                                              "FROM \"tracker:Namespace\"");

	if (stmt) {
		cursor = tracker_db_statement_start_cursor (stmt, &internal_error);
		g_object_unref (stmt);
	}

	if (cursor) {
		while (tracker_db_cursor_iter_next (cursor, NULL, &internal_error)) {
			TrackerNamespace *namespace;
			const gchar      *uri, *prefix;

			namespace = tracker_namespace_new (FALSE);

			uri = tracker_db_cursor_get_string (cursor, 0, NULL);
			prefix = tracker_db_cursor_get_string (cursor, 1, NULL);

			tracker_namespace_set_ontologies (namespace, manager->ontologies);
			tracker_namespace_set_is_new (namespace, FALSE);
			tracker_namespace_set_uri (namespace, uri);
			tracker_namespace_set_prefix (namespace, prefix);
			tracker_ontologies_add_namespace (manager->ontologies, namespace);

			g_object_unref (namespace);

		}

		g_object_unref (cursor);
		cursor = NULL;
	}

	if (internal_error) {
		g_propagate_error (error, internal_error);
		return;
	}

	stmt = tracker_db_interface_create_statement (iface, TRACKER_DB_STATEMENT_CACHE_TYPE_SELECT, &internal_error,
	                                              "SELECT \"rdfs:Class\".ID, "
	                                              "(SELECT Uri FROM Resource WHERE ID = \"rdfs:Class\".ID), "
	                                              "\"tracker:notify\" "
	                                              "FROM \"rdfs:Class\" ORDER BY ID");

	if (stmt) {
		cursor = tracker_db_statement_start_cursor (stmt, &internal_error);
		g_object_unref (stmt);
	}

	if (cursor) {
		while (tracker_db_cursor_iter_next (cursor, NULL, &internal_error)) {
			TrackerClass *class;
			const gchar  *uri;
			gint          id;
			GValue        value = { 0 };
			gboolean      notify;

			class = tracker_class_new (FALSE);

			id = tracker_db_cursor_get_int (cursor, 0);
			uri = tracker_db_cursor_get_string (cursor, 1, NULL);

			tracker_db_cursor_get_value (cursor, 2, &value);

			if (G_VALUE_TYPE (&value) != 0) {
				notify = (g_value_get_int64 (&value) == 1);
				g_value_unset (&value);
			} else {
				/* NULL */
				notify = FALSE;
			}

			tracker_class_set_ontologies (class, manager->ontologies);
			tracker_class_set_db_schema_changed (class, FALSE);
			tracker_class_set_is_new (class, FALSE);
			tracker_class_set_uri (class, uri);
			tracker_class_set_notify (class, notify);

			class_add_super_classes_from_db (iface, manager, class);

			/* We add domain indexes later , we first need to load the properties */

			tracker_ontologies_add_class (manager->ontologies, class);
			tracker_ontologies_add_id_uri_pair (manager->ontologies, id, uri);
			tracker_class_set_id (class, id);

			g_object_unref (class);
		}

		g_object_unref (cursor);
		cursor = NULL;
	}

	if (internal_error) {
		g_propagate_error (error, internal_error);
		return;
	}

	stmt = tracker_db_interface_create_statement (iface, TRACKER_DB_STATEMENT_CACHE_TYPE_SELECT, &internal_error,
	                                              "SELECT \"rdf:Property\".ID, (SELECT Uri FROM Resource WHERE ID = \"rdf:Property\".ID), "
	                                              "(SELECT Uri FROM Resource WHERE ID = \"rdfs:domain\"), "
	                                              "(SELECT Uri FROM Resource WHERE ID = \"rdfs:range\"), "
	                                              "\"nrl:maxCardinality\", "
	                                              "\"tracker:indexed\", "
	                                              "(SELECT Uri FROM Resource WHERE ID = \"tracker:secondaryIndex\"), "
	                                              "\"tracker:fulltextIndexed\", "
	                                              "\"tracker:transient\", "
	                                              "\"tracker:writeback\", "
	                                              "(SELECT 1 FROM \"rdfs:Resource_rdf:type\" WHERE ID = \"rdf:Property\".ID AND "
	                                              "\"rdf:type\" = (SELECT ID FROM Resource WHERE Uri = '" NRL_INVERSE_FUNCTIONAL_PROPERTY "')), "
	                                              "\"tracker:defaultValue\" "
	                                              "FROM \"rdf:Property\" ORDER BY ID");

	if (stmt) {
		cursor = tracker_db_statement_start_cursor (stmt, &internal_error);
		g_object_unref (stmt);
	}

	if (cursor) {
		while (tracker_db_cursor_iter_next (cursor, NULL, &internal_error)) {
			GValue value = { 0 };
			TrackerProperty *property;
			const gchar     *uri, *domain_uri, *range_uri, *secondary_index_uri, *default_value;
			gboolean         multi_valued, indexed, fulltext_indexed;
			gboolean         transient, is_inverse_functional_property;
			gboolean         writeback;
			gint             id;

			property = tracker_property_new (FALSE);

			id = tracker_db_cursor_get_int (cursor, 0);
			uri = tracker_db_cursor_get_string (cursor, 1, NULL);
			domain_uri = tracker_db_cursor_get_string (cursor, 2, NULL);
			range_uri = tracker_db_cursor_get_string (cursor, 3, NULL);

			tracker_db_cursor_get_value (cursor, 4, &value);

			if (G_VALUE_TYPE (&value) != 0) {
				multi_valued = (g_value_get_int64 (&value) > 1);
				g_value_unset (&value);
			} else {
				/* nrl:maxCardinality not set
				   not limited to single value */
				multi_valued = TRUE;
			}

			tracker_db_cursor_get_value (cursor, 5, &value);

			if (G_VALUE_TYPE (&value) != 0) {
				indexed = (g_value_get_int64 (&value) == 1);
				g_value_unset (&value);
			} else {
				/* NULL */
				indexed = FALSE;
			}

			secondary_index_uri = tracker_db_cursor_get_string (cursor, 6, NULL);

			tracker_db_cursor_get_value (cursor, 7, &value);

			if (G_VALUE_TYPE (&value) != 0) {
				fulltext_indexed = (g_value_get_int64 (&value) == 1);
				g_value_unset (&value);
			} else {
				/* NULL */
				fulltext_indexed = FALSE;
			}

			tracker_db_cursor_get_value (cursor, 8, &value);

			if (G_VALUE_TYPE (&value) != 0) {
				transient = (g_value_get_int64 (&value) == 1);
				g_value_unset (&value);
			} else {
				/* NULL */
				transient = FALSE;
			}

			/* tracker:writeback column */
			tracker_db_cursor_get_value (cursor, 9, &value);

			if (G_VALUE_TYPE (&value) != 0) {
				writeback = (g_value_get_int64 (&value) == 1);
				g_value_unset (&value);
			} else {
				/* NULL */
				writeback = FALSE;
			}

			/* NRL_INVERSE_FUNCTIONAL_PROPERTY column */
			tracker_db_cursor_get_value (cursor, 10, &value);

			if (G_VALUE_TYPE (&value) != 0) {
				is_inverse_functional_property = TRUE;
				g_value_unset (&value);
			} else {
				/* NULL */
				is_inverse_functional_property = FALSE;
			}

			default_value = tracker_db_cursor_get_string (cursor, 11, NULL);

			tracker_property_set_ontologies (property, manager->ontologies);
			tracker_property_set_is_new_domain_index (property, tracker_ontologies_get_class_by_uri (manager->ontologies, domain_uri), FALSE);
			tracker_property_set_is_new (property, FALSE);
			tracker_property_set_cardinality_changed (property, FALSE);
			tracker_property_set_transient (property, transient);
			tracker_property_set_uri (property, uri);
			tracker_property_set_id (property, id);
			tracker_property_set_domain (property, tracker_ontologies_get_class_by_uri (manager->ontologies, domain_uri));
			tracker_property_set_range (property, tracker_ontologies_get_class_by_uri (manager->ontologies, range_uri));
			tracker_property_set_multiple_values (property, multi_valued);
			tracker_property_set_orig_multiple_values (property, multi_valued);
			tracker_property_set_indexed (property, indexed);
			tracker_property_set_default_value (property, default_value);

			tracker_property_set_db_schema_changed (property, FALSE);
			tracker_property_set_writeback (property, writeback);

			if (secondary_index_uri) {
				tracker_property_set_secondary_index (property, tracker_ontologies_get_property_by_uri (manager->ontologies, secondary_index_uri));
			}

			tracker_property_set_orig_fulltext_indexed (property, fulltext_indexed);
			tracker_property_set_fulltext_indexed (property, fulltext_indexed);
			tracker_property_set_is_inverse_functional_property (property, is_inverse_functional_property);

			/* super properties are only used in updates, never for queries */
			if ((tracker_db_manager_get_flags (manager->db_manager, NULL, NULL) & TRACKER_DB_MANAGER_READONLY) == 0) {
				property_add_super_properties_from_db (iface, manager, property);
			}

			tracker_ontologies_add_property (manager->ontologies, property);
			tracker_ontologies_add_id_uri_pair (manager->ontologies, id, uri);

			g_object_unref (property);

		}

		g_object_unref (cursor);
		cursor = NULL;
	}

	/* Now that the properties are loaded we can do this foreach class */
	classes = tracker_ontologies_get_classes (manager->ontologies, &n_classes);
	for (i = 0; i < n_classes; i++) {
		class_add_domain_indexes_from_db (iface, manager, classes[i]);
	}

	if (internal_error) {
		g_propagate_error (error, internal_error);
		return;
	}
}

static void
insert_uri_in_resource_table (TrackerDataManager  *manager,
                              TrackerDBInterface  *iface,
                              const gchar         *uri,
                              gint                 id,
                              GError             **error)
{
	TrackerDBStatement *stmt;
	GError *internal_error = NULL;

	stmt = tracker_db_interface_create_statement (iface, TRACKER_DB_STATEMENT_CACHE_TYPE_UPDATE,
	                                              &internal_error,
	                                              "INSERT OR IGNORE "
	                                              "INTO main.Resource "
	                                              "(ID, Uri) "
	                                              "VALUES (?, ?)");
	if (internal_error) {
		g_propagate_error (error, internal_error);
		return;
	}

	tracker_db_statement_bind_int (stmt, 0, id);
	tracker_db_statement_bind_text (stmt, 1, uri);
	tracker_db_statement_execute (stmt, &internal_error);

	if (internal_error) {
		g_object_unref (stmt);
		g_propagate_error (error, internal_error);
		return;
	}

	g_object_unref (stmt);

}

static void
range_change_for (TrackerProperty *property,
                  GString         *in_col_sql,
                  GString         *sel_col_sql,
                  const gchar     *field_name)
{
	/* TODO: TYPE_RESOURCE and TYPE_DATETIME are completely unhandled atm, we
	 * should forbid conversion from anything to resource or datetime in error
	 * handling earlier */

	g_string_append_printf (in_col_sql, ", \"%s\", \"%s:graph\"",
	                        field_name, field_name);

	if (tracker_property_get_data_type (property) == TRACKER_PROPERTY_TYPE_INTEGER ||
	    tracker_property_get_data_type (property) == TRACKER_PROPERTY_TYPE_DOUBLE) {
			g_string_append_printf (sel_col_sql, ", \"%s\" + 0, \"%s:graph\"",
			                        field_name, field_name);
	} else if (tracker_property_get_data_type (property) == TRACKER_PROPERTY_TYPE_DATETIME) {

		/* TODO (see above) */

		g_string_append_printf (sel_col_sql, ", \"%s\", \"%s:graph\"",
		                        field_name, field_name);
	} else if (tracker_property_get_data_type (property) == TRACKER_PROPERTY_TYPE_BOOLEAN) {
		g_string_append_printf (sel_col_sql, ", \"%s\" != 0, \"%s:graph\"",
		                        field_name, field_name);
	} else {
		g_string_append_printf (sel_col_sql, ", \"%s\", \"%s:graph\"",
		                        field_name, field_name);
	}
}

static void
create_decomposed_metadata_property_table (TrackerDBInterface *iface,
                                           TrackerProperty    *property,
                                           const gchar        *service_name,
                                           TrackerClass       *service,
                                           const gchar       **sql_type_for_single_value,
                                           gboolean            in_update,
                                           gboolean            in_change,
                                           GError            **error)
{
	GError *internal_error = NULL;
	const char *field_name;
	const char *sql_type;
	gboolean    not_single, datetime;

	field_name = tracker_property_get_name (property);

	not_single = !sql_type_for_single_value;

	switch (tracker_property_get_data_type (property)) {
	case TRACKER_PROPERTY_TYPE_STRING:
		sql_type = "TEXT";
		break;
	case TRACKER_PROPERTY_TYPE_INTEGER:
	case TRACKER_PROPERTY_TYPE_BOOLEAN:
	case TRACKER_PROPERTY_TYPE_DATE:
	case TRACKER_PROPERTY_TYPE_DATETIME:
	case TRACKER_PROPERTY_TYPE_RESOURCE:
		sql_type = "INTEGER";
		break;
	case TRACKER_PROPERTY_TYPE_DOUBLE:
		sql_type = "REAL";
		break;
	default:
		sql_type = "";
		break;
	}

	datetime = tracker_property_get_data_type (property) == TRACKER_PROPERTY_TYPE_DATETIME;

	if (!in_update || (in_update && (tracker_property_get_is_new (property) ||
	                                 tracker_property_get_is_new_domain_index (property, service) ||
	                                 tracker_property_get_cardinality_changed (property) ||
	                                 tracker_property_get_db_schema_changed (property)))) {
		if (not_single || tracker_property_get_multiple_values (property)) {
			GString *sql = NULL;
			GString *in_col_sql = NULL;
			GString *sel_col_sql = NULL;

			/* multiple values */

			if (in_update) {
				g_debug ("Altering database for class '%s' property '%s': multi value",
				         service_name, field_name);
			}

			if (in_change && !tracker_property_get_is_new (property) && !tracker_property_get_cardinality_changed (property)) {
				g_debug ("Drop index: DROP INDEX IF EXISTS \"%s_%s_ID\"\nRename: ALTER TABLE \"%s_%s\" RENAME TO \"%s_%s_TEMP\"",
				         service_name, field_name, service_name, field_name,
				         service_name, field_name);

				tracker_db_interface_execute_query (iface, &internal_error,
				                                    "DROP INDEX IF EXISTS \"%s_%s_ID\"",
				                                    service_name,
				                                    field_name);

				if (internal_error) {
					g_propagate_error (error, internal_error);
					goto error_out;
				}

				tracker_db_interface_execute_query (iface, &internal_error,
				                                    "ALTER TABLE \"%s_%s\" RENAME TO \"%s_%s_TEMP\"",
				                                    service_name, field_name, service_name, field_name);

				if (internal_error) {
					g_propagate_error (error, internal_error);
					goto error_out;
				}
			} else if (in_change && tracker_property_get_cardinality_changed (property)) {
				/* We should be dropping all indices colliding with the new table name */
				tracker_db_interface_execute_query (iface, &internal_error,
				                                    "DROP INDEX IF EXISTS \"%s_%s\"",
				                                    service_name,
				                                    field_name);
			}

			sql = g_string_new ("");
			g_string_append_printf (sql, "CREATE TABLE \"%s_%s\" ("
			                             "ID INTEGER NOT NULL, "
			                             "\"%s\" %s NOT NULL, "
			                             "\"%s:graph\" INTEGER",
			                             service_name,
			                             field_name,
			                             field_name,
			                             sql_type,
			                             field_name);

			if (in_change && !tracker_property_get_is_new (property)) {
				in_col_sql = g_string_new ("ID");
				sel_col_sql = g_string_new ("ID");

				range_change_for (property, in_col_sql, sel_col_sql, field_name);
			}

			tracker_db_interface_execute_query (iface, &internal_error,
			                                    "%s)", sql->str);

			if (internal_error) {
				g_propagate_error (error, internal_error);
				goto error_out;
			}

			/* multiple values */
			if (tracker_property_get_indexed (property)) {
				/* use different UNIQUE index for properties whose
				 * value should be indexed to minimize index size */
				set_index_for_multi_value_property (iface, service_name, field_name, TRUE, TRUE,
				                                    datetime,
				                                    &internal_error);
				if (internal_error) {
					g_propagate_error (error, internal_error);
					goto error_out;
				}
			} else {
				set_index_for_multi_value_property (iface, service_name, field_name, FALSE, TRUE,
				                                    datetime,
				                                    &internal_error);
				/* we still have to include the property value in
				 * the unique index for proper constraints */
				if (internal_error) {
					g_propagate_error (error, internal_error);
					goto error_out;
				}
			}

			if (in_change && !tracker_property_get_is_new (property) &&
			    !tracker_property_get_cardinality_changed (property) && in_col_sql && sel_col_sql) {
				gchar *query;

				query = g_strdup_printf ("INSERT INTO \"%s_%s\"(%s) "
				                         "SELECT %s FROM \"%s_%s_TEMP\"",
				                         service_name, field_name, in_col_sql->str,
				                         sel_col_sql->str, service_name, field_name);

				tracker_db_interface_execute_query (iface, &internal_error, "%s", query);

				if (internal_error) {
					g_free (query);
					g_propagate_error (error, internal_error);
					goto error_out;
				}

				g_free (query);
				tracker_db_interface_execute_query (iface, &internal_error, "DROP TABLE \"%s_%s_TEMP\"",
				                                    service_name, field_name);

				if (internal_error) {
					g_propagate_error (error, internal_error);
					goto error_out;
				}
			}

			/* multiple values */
			if (tracker_property_get_indexed (property)) {
				/* use different UNIQUE index for properties whose
				 * value should be indexed to minimize index size */
				set_index_for_multi_value_property (iface, service_name, field_name, TRUE, TRUE,
				                                    datetime,
				                                    &internal_error);
				if (internal_error) {
					g_propagate_error (error, internal_error);
					goto error_out;
				}
			} else {
				set_index_for_multi_value_property (iface, service_name, field_name, FALSE, TRUE,
				                                    datetime,
				                                    &internal_error);
				if (internal_error) {
					g_propagate_error (error, internal_error);
					goto error_out;
				}
				/* we still have to include the property value in
				 * the unique index for proper constraints */
			}

			error_out:

			if (sql) {
				g_string_free (sql, TRUE);
			}

			if (sel_col_sql) {
				g_string_free (sel_col_sql, TRUE);
			}

			if (in_col_sql) {
				g_string_free (in_col_sql, TRUE);
			}
		} else if (sql_type_for_single_value) {
			*sql_type_for_single_value = sql_type;
		}
	}
}

static gboolean
is_a_domain_index (TrackerProperty **domain_indexes, TrackerProperty *property)
{
	while (*domain_indexes) {

		if (*domain_indexes == property) {
			return TRUE;
		}

		domain_indexes++;
	}

	return FALSE;
}

static void
copy_from_domain_to_domain_index (TrackerDBInterface  *iface,
                                  TrackerProperty     *domain_index,
                                  const gchar         *column_name,
                                  const gchar         *column_suffix,
                                  TrackerClass        *dest_domain,
                                  GError             **error)
{
	GError *internal_error = NULL;
	TrackerClass *source_domain;
	const gchar *source_name, *dest_name;
	gchar *query;

	source_domain = tracker_property_get_domain (domain_index);
	source_name = tracker_class_get_name (source_domain);
	dest_name = tracker_class_get_name (dest_domain);

	query = g_strdup_printf ("UPDATE \"%s\" SET \"%s%s\"=("
	                         "SELECT \"%s%s\" FROM \"%s\" "
	                         "WHERE \"%s\".ID = \"%s\".ID)",
	                         dest_name,
	                         column_name,
	                         column_suffix ? column_suffix : "",
	                         column_name,
	                         column_suffix ? column_suffix : "",
	                         source_name,
	                         source_name,
	                         dest_name);

	g_debug ("Copying: '%s'", query);

	tracker_db_interface_execute_query (iface, &internal_error, "%s", query);

	if (internal_error) {
		g_propagate_error (error, internal_error);
	}

	g_free (query);
}

typedef struct {
	TrackerProperty *prop;
	const gchar *field_name;
	const gchar *suffix;
} ScheduleCopy;

static void
schedule_copy (GPtrArray *schedule,
               TrackerProperty *prop,
               const gchar *field_name,
               const gchar *suffix)
{
	ScheduleCopy *sched = g_new0 (ScheduleCopy, 1);
	sched->prop = prop;
	sched->field_name = field_name,
	sched->suffix = suffix;
	g_ptr_array_add (schedule, sched);
}

static void
create_insert_delete_triggers (TrackerDBInterface  *iface,
                               const gchar         *table_name,
                               const gchar * const *properties,
                               gint                 n_properties,
                               GError             **error)
{
	GError *internal_error = NULL;
	GString *trigger_query;
	gint i;

	/* Insert trigger */
	tracker_db_interface_execute_query (iface, &internal_error,
	                                    "DROP TRIGGER IF EXISTS \"trigger_insert_%s\" ",
	                                    table_name);
	if (internal_error) {
		g_propagate_error (error, internal_error);
		return;
	}

	trigger_query = g_string_new (NULL);
	g_string_append_printf (trigger_query,
	                        "CREATE TRIGGER \"trigger_insert_%s\" "
	                        "AFTER INSERT ON \"%s\" "
	                        "FOR EACH ROW BEGIN ",
	                        table_name, table_name);
	for (i = 0; i < n_properties; i++) {
		g_string_append_printf (trigger_query,
		                        "UPDATE Resource "
		                        "SET Refcount = Refcount + 1 "
		                        "WHERE Resource.rowid = NEW.\"%s\"; ",
		                        properties[i]);
	}

	g_string_append (trigger_query, "END; ");
	tracker_db_interface_execute_query (iface, &internal_error,
	                                    "%s", trigger_query->str);
	g_string_free (trigger_query, TRUE);

	if (internal_error) {
		g_propagate_error (error, internal_error);
		return;
	}

	/* Delete trigger */
	tracker_db_interface_execute_query (iface, &internal_error,
	                                    "DROP TRIGGER IF EXISTS \"trigger_delete_%s\" ",
	                                    table_name);
	if (internal_error) {
		g_propagate_error (error, internal_error);
		return;
	}

	trigger_query = g_string_new (NULL);
	g_string_append_printf (trigger_query,
	                        "CREATE TRIGGER \"trigger_delete_%s\" "
	                        "AFTER DELETE ON \"%s\" "
	                        "FOR EACH ROW BEGIN ",
	                        table_name, table_name);
	for (i = 0; i < n_properties; i++) {
		g_string_append_printf (trigger_query,
		                        "UPDATE Resource "
		                        "SET Refcount = Refcount - 1 "
		                        "WHERE Resource.rowid = OLD.\"%s\"; ",
		                        properties[i]);
	}

	g_string_append (trigger_query, "END; ");
	tracker_db_interface_execute_query (iface, &internal_error,
	                                    "%s", trigger_query->str);
	g_string_free (trigger_query, TRUE);

	if (internal_error) {
		g_propagate_error (error, internal_error);
		return;
	}
}

static void
create_table_triggers (TrackerDataManager  *manager,
                       TrackerDBInterface  *iface,
                       TrackerClass        *klass,
                       GError             **error)
{
	const gchar *property_name;
	TrackerProperty **properties, *property;
	GError *internal_error = NULL;
	GPtrArray *trigger_properties;
	guint i, n_props;

	trigger_properties = g_ptr_array_new ();
	g_ptr_array_add (trigger_properties, "ROWID");

	properties = tracker_ontologies_get_properties (manager->ontologies, &n_props);

	for (i = 0; i < n_props; i++) {
		gboolean multivalued;
		gchar *table_name;

		property = properties[i];

		if (tracker_property_get_domain (property) != klass ||
		    tracker_property_get_data_type (property) != TRACKER_PROPERTY_TYPE_RESOURCE)
			continue;

		property_name = tracker_property_get_name (property);
		multivalued = tracker_property_get_multiple_values (property);

		if (multivalued) {
			const gchar * const properties[] = { "ID", property_name };

			table_name = g_strdup_printf ("%s_%s",
			                              tracker_class_get_name (klass),
			                              property_name);

			create_insert_delete_triggers (iface, table_name, properties,
			                               G_N_ELEMENTS (properties),
			                               &internal_error);
			if (internal_error) {
				g_propagate_error (error, internal_error);
				g_ptr_array_unref (trigger_properties);
				g_free (table_name);
				return;
			}
		} else {
			table_name = g_strdup (tracker_class_get_name (klass));
			g_ptr_array_add (trigger_properties, (gchar *) property_name);
		}

		tracker_db_interface_execute_query (iface, &internal_error,
		                                    "DROP TRIGGER IF EXISTS \"trigger_update_%s_%s\"",
		                                    tracker_class_get_name (klass),
		                                    property_name);
		if (internal_error) {
			g_propagate_error (error, internal_error);
			g_ptr_array_unref (trigger_properties);
			g_free (table_name);
			return;
		}

		tracker_db_interface_execute_query (iface, &internal_error,
		                                    "CREATE TRIGGER \"trigger_update_%s_%s\" "
		                                    "AFTER UPDATE OF \"%s\" ON \"%s\" "
		                                    "FOR EACH ROW BEGIN "
		                                    "UPDATE Resource SET Refcount = Refcount + 1 WHERE Resource.rowid = NEW.\"%s\";"
		                                    "UPDATE Resource SET Refcount = Refcount - 1 WHERE Resource.rowid = OLD.\"%s\";"
		                                    "END",
		                                    tracker_class_get_name (klass),
		                                    property_name,
		                                    property_name, table_name,
		                                    property_name, property_name);
		g_free (table_name);

		if (internal_error) {
			g_propagate_error (error, internal_error);
			g_ptr_array_unref (trigger_properties);
			return;
		}
	}

	create_insert_delete_triggers (iface,
	                               tracker_class_get_name (klass),
	                               (const gchar * const *) trigger_properties->pdata,
	                               trigger_properties->len,
	                               &internal_error);
	g_ptr_array_unref (trigger_properties);

	if (internal_error) {
		g_propagate_error (error, internal_error);
		return;
	}
}

static void
create_decomposed_metadata_tables (TrackerDataManager  *manager,
                                   TrackerDBInterface  *iface,
                                   TrackerClass        *service,
                                   gboolean             in_update,
                                   gboolean             in_change,
                                   GError             **error)
{
	const char       *service_name;
	GString          *create_sql = NULL;
	GString          *in_col_sql = NULL;
	GString          *sel_col_sql = NULL;
	TrackerProperty **properties, *property, **domain_indexes;
	GSList           *class_properties = NULL, *field_it;
	guint             i, n_props;
	gboolean          in_alter = in_update;
	GError           *internal_error = NULL;
	GPtrArray        *copy_schedule = NULL;

	g_return_if_fail (TRACKER_IS_CLASS (service));

	service_name = tracker_class_get_name (service);

	g_return_if_fail (service_name != NULL);

	if (g_str_has_prefix (service_name, "xsd:")) {
		/* xsd classes do not derive from rdfs:Resource and do not need separate tables */
		return;
	}

	if (in_change && !tracker_class_get_is_new (service)) {
		g_debug ("Rename: ALTER TABLE \"%s\" RENAME TO \"%s_TEMP\"", service_name, service_name);
		tracker_db_interface_execute_query (iface, &internal_error,
		                                    "ALTER TABLE \"%s\" RENAME TO \"%s_TEMP\"",
		                                    service_name, service_name);
		in_col_sql = g_string_new ("ID");
		sel_col_sql = g_string_new ("ID");
		if (internal_error) {
			g_propagate_error (error, internal_error);
			goto error_out;
		}
	}

	if (in_change || !in_update || (in_update && tracker_class_get_is_new (service))) {
		if (in_update)
			g_debug ("Altering database with new class '%s' (create)", service_name);
		in_alter = FALSE;
		create_sql = g_string_new ("");
		g_string_append_printf (create_sql, "CREATE TABLE \"%s\" (ID INTEGER NOT NULL PRIMARY KEY", service_name);
	}

	properties = tracker_ontologies_get_properties (manager->ontologies, &n_props);
	domain_indexes = tracker_class_get_domain_indexes (service);

	for (i = 0; i < n_props; i++) {
		gboolean is_domain_index, datetime;

		property = properties[i];
		is_domain_index = is_a_domain_index (domain_indexes, property);
		datetime = tracker_property_get_data_type (property) == TRACKER_PROPERTY_TYPE_DATETIME;

		if (tracker_property_get_domain (property) == service || is_domain_index) {
			gboolean put_change;
			const gchar *sql_type_for_single_value = NULL;
			const gchar *field_name;

			create_decomposed_metadata_property_table (iface, property,
			                                           service_name,
			                                           service,
			                                           &sql_type_for_single_value,
			                                           in_alter,
			                                           in_change,
			                                           &internal_error);

			if (internal_error) {
				g_propagate_error (error, internal_error);
				goto error_out;
			}

			field_name = tracker_property_get_name (property);

			if (sql_type_for_single_value) {
				const gchar *default_value;

				/* single value */

				default_value = tracker_property_get_default_value (property);

				if (in_update) {
					g_debug ("%sAltering database for class '%s' property '%s': single value (%s)",
					         in_alter ? "" : "  ",
					         service_name,
					         field_name,
					         in_alter ? "alter" : "create");
				}

				if (!in_alter) {
					put_change = TRUE;
					class_properties = g_slist_prepend (class_properties, property);

					g_string_append_printf (create_sql, ", \"%s\" %s",
					                        field_name,
					                        sql_type_for_single_value);

					if (!copy_schedule) {
						copy_schedule = g_ptr_array_new_with_free_func (g_free);
					}

					if (is_domain_index && tracker_property_get_is_new_domain_index (property, service)) {
						schedule_copy (copy_schedule, property, field_name, NULL);
					}

					if (g_ascii_strcasecmp (sql_type_for_single_value, "TEXT") == 0) {
						g_string_append (create_sql, " COLLATE " TRACKER_COLLATION_NAME);
					}

					/* add DEFAULT in case that the ontology specifies a default value,
					   assumes that default values never contain quotes */
					if (default_value != NULL) {
						g_string_append_printf (create_sql, " DEFAULT '%s'", default_value);
					}

					if (tracker_property_get_is_inverse_functional_property (property)) {
						g_string_append (create_sql, " UNIQUE");
					}

					g_string_append_printf (create_sql, ", \"%s:graph\" INTEGER",
					                        field_name);

					if (is_domain_index && tracker_property_get_is_new_domain_index (property, service)) {
						schedule_copy (copy_schedule, property, field_name, ":graph");
					}
				} else if ((!is_domain_index && tracker_property_get_is_new (property)) ||
				           (is_domain_index && tracker_property_get_is_new_domain_index (property, service))) {
					GString *alter_sql = NULL;

					put_change = FALSE;
					class_properties = g_slist_prepend (class_properties, property);

					alter_sql = g_string_new ("ALTER TABLE ");
					g_string_append_printf (alter_sql, "\"%s\" ADD COLUMN \"%s\" %s",
					                        service_name,
					                        field_name,
					                        sql_type_for_single_value);

					if (g_ascii_strcasecmp (sql_type_for_single_value, "TEXT") == 0) {
						g_string_append (alter_sql, " COLLATE " TRACKER_COLLATION_NAME);
					}

					/* add DEFAULT in case that the ontology specifies a default value,
					   assumes that default values never contain quotes */
					if (default_value != NULL) {
						g_string_append_printf (alter_sql, " DEFAULT '%s'", default_value);
					}

					if (tracker_property_get_is_inverse_functional_property (property)) {
						g_string_append (alter_sql, " UNIQUE");
					}

					g_debug ("Altering: '%s'", alter_sql->str);
					tracker_db_interface_execute_query (iface, &internal_error, "%s", alter_sql->str);
					if (internal_error) {
						g_string_free (alter_sql, TRUE);
						g_propagate_error (error, internal_error);
						goto error_out;
					} else if (is_domain_index) {
						copy_from_domain_to_domain_index (iface, property,
						                                  field_name, NULL,
						                                  service,
						                                  &internal_error);
						if (internal_error) {
							g_string_free (alter_sql, TRUE);
							g_propagate_error (error, internal_error);
							goto error_out;
						}

						/* This is implicit for all domain-specific-indices */
						set_index_for_single_value_property (iface, service_name,
						                                     field_name, TRUE,
						                                     datetime,
						                                     &internal_error);
						if (internal_error) {
							g_string_free (alter_sql, TRUE);
							g_propagate_error (error, internal_error);
							goto error_out;
						}
					}

					g_string_free (alter_sql, TRUE);

					alter_sql = g_string_new ("ALTER TABLE ");
					g_string_append_printf (alter_sql, "\"%s\" ADD COLUMN \"%s:graph\" INTEGER",
					                        service_name,
					                        field_name);
					g_debug ("Altering: '%s'", alter_sql->str);
					tracker_db_interface_execute_query (iface, &internal_error,
					                                    "%s", alter_sql->str);
					if (internal_error) {
						g_string_free (alter_sql, TRUE);
						g_propagate_error (error, internal_error);
						goto error_out;
					} else if (is_domain_index) {
						copy_from_domain_to_domain_index (iface, property,
						                                  field_name, ":graph",
						                                  service,
						                                  &internal_error);
						if (internal_error) {
							g_string_free (alter_sql, TRUE);
							g_propagate_error (error, internal_error);
							goto error_out;
						}
					}

					g_string_free (alter_sql, TRUE);
				} else {
					put_change = TRUE;
				}

				if (in_change && put_change) {
					range_change_for (property, in_col_sql, sel_col_sql, field_name);
				}
			}
		}
	}

	if (create_sql) {
		g_string_append (create_sql, ")");
		g_debug ("Creating: '%s'", create_sql->str);
		tracker_db_interface_execute_query (iface, &internal_error,
		                                    "%s", create_sql->str);
		if (internal_error) {
			g_propagate_error (error, internal_error);
			goto error_out;
		}
	}

	/* create index for single-valued fields */
	for (field_it = class_properties; field_it != NULL; field_it = field_it->next) {
		TrackerProperty *field, *secondary_index;
		const char *field_name;
		gboolean is_domain_index, datetime;

		field = field_it->data;

		/* This is implicit for all domain-specific-indices */
		is_domain_index = is_a_domain_index (domain_indexes, field);

		if (!tracker_property_get_multiple_values (field)
		    && (tracker_property_get_indexed (field) || is_domain_index)) {

			field_name = tracker_property_get_name (field);
			datetime = tracker_property_get_data_type (field) == TRACKER_PROPERTY_TYPE_DATETIME;

			secondary_index = tracker_property_get_secondary_index (field);
			if (secondary_index == NULL) {
				set_index_for_single_value_property (iface, service_name,
				                                     field_name, TRUE,
				                                     datetime,
				                                     &internal_error);
				if (internal_error) {
					g_propagate_error (error, internal_error);
					goto error_out;
				}
			} else {
				set_secondary_index_for_single_value_property (iface, service_name, field_name,
				                                               tracker_property_get_name (secondary_index),
				                                               TRUE, &internal_error);
				if (internal_error) {
					g_propagate_error (error, internal_error);
					goto error_out;
				}
			}
		}
	}

	if (!tracker_class_get_is_new (service) && in_change && sel_col_sql && in_col_sql) {
		guint i;
		gchar *query;

		query = g_strdup_printf ("INSERT INTO \"%s\"(%s) "
		                         "SELECT %s FROM \"%s_TEMP\"",
		                         service_name, in_col_sql->str,
		                         sel_col_sql->str, service_name);

		g_debug ("Copy: %s", query);

		tracker_db_interface_execute_query (iface, &internal_error, "%s", query);

		if (internal_error) {
			g_propagate_error (error, internal_error);
			goto error_out;
		}
		
		g_free (query);

		for (i = 0; i < n_props; i++) {
			property = properties[i];

			if (tracker_property_get_domain (property) == service && tracker_property_get_cardinality_changed (property)) {
				GString *n_sel_col_sql, *n_in_col_sql;
				const gchar *field_name = tracker_property_get_name (property);

				n_in_col_sql = g_string_new ("ID");
				n_sel_col_sql = g_string_new ("ID");

				/* Function does what it must do, so reusable atm */
				range_change_for (property, n_in_col_sql, n_sel_col_sql, field_name);

                                /* Columns happen to be the same for decomposed multi-value and single value atm */

				query = g_strdup_printf ("INSERT INTO \"%s_%s\"(%s) "
				                         "SELECT %s FROM \"%s_TEMP\" "
				                         "WHERE ID IS NOT NULL AND \"%s\" IS NOT NULL",
				                         service_name, field_name,
				                         n_in_col_sql->str, n_sel_col_sql->str,
				                         service_name, field_name);

				g_string_free (n_in_col_sql, TRUE);
				g_string_free (n_sel_col_sql, TRUE);

				g_debug ("Copy supported nlr:maxCardinality change: %s", query);

				tracker_db_interface_execute_query (iface, &internal_error, "%s", query);

				if (internal_error) {
					g_propagate_error (error, internal_error);
					goto error_out;
				}
		
				g_free (query);
			}
		}

		g_debug ("Rename (drop): DROP TABLE \"%s_TEMP\"", service_name);
		tracker_db_interface_execute_query (iface, &internal_error,
		                                    "DROP TABLE \"%s_TEMP\"", service_name);

		if (internal_error) {
			g_propagate_error (error, internal_error);
			goto error_out;
		}
	}

	if (!in_update || in_change || tracker_class_get_is_new (service)) {
		/* FIXME: We are trusting object refcount will stay intact across
		 * ontology changes. One situation where this is not true are
		 * removal or properties with rdfs:Resource range.
		 */
		create_table_triggers (manager, iface, service, &internal_error);

		if (internal_error) {
			g_propagate_error (error, internal_error);
			goto error_out;
		}
	}

	if (copy_schedule) {
		guint i;
		for (i = 0; i < copy_schedule->len; i++) {
			ScheduleCopy *sched = g_ptr_array_index (copy_schedule, i);
			copy_from_domain_to_domain_index (iface, sched->prop,
			                                  sched->field_name, sched->suffix,
			                                  service,
			                                  &internal_error);

			if (internal_error) {
				g_propagate_error (error, internal_error);
				break;
			}
		}
	}

error_out:

	if (copy_schedule) {
		g_ptr_array_free (copy_schedule, TRUE);
	}

	if (create_sql) {
		g_string_free (create_sql, TRUE);
	}

	g_slist_free (class_properties);

	if (in_col_sql) {
		g_string_free (in_col_sql, TRUE);
	}

	if (sel_col_sql) {
		g_string_free (sel_col_sql, TRUE);
	}
}

static void
clean_decomposed_transient_metadata (TrackerDataManager *manager,
                                     TrackerDBInterface *iface)
{
	TrackerProperty **properties;
	TrackerProperty *property;
	guint i, n_props;

	properties = tracker_ontologies_get_properties (manager->ontologies, &n_props);

	for (i = 0; i < n_props; i++) {
		property = properties[i];

		if (tracker_property_get_transient (property)) {
			TrackerClass *domain;
			const gchar *service_name;
			const gchar *prop_name;
			GError *error = NULL;

			domain = tracker_property_get_domain (property);
			service_name = tracker_class_get_name (domain);
			prop_name = tracker_property_get_name (property);

			if (tracker_property_get_multiple_values (property)) {
				/* create the disposable table */
				tracker_db_interface_execute_query (iface, &error, "DELETE FROM \"%s_%s\"",
				                                    service_name,
				                                    prop_name);
			} else {
				/* create the disposable table */
				tracker_db_interface_execute_query (iface, &error, "UPDATE \"%s\" SET \"%s\" = NULL",
				                                    service_name,
				                                    prop_name);
			}

			if (error) {
				g_critical ("Cleaning transient propery '%s:%s' failed: %s",
				            service_name,
				            prop_name,
				            error->message);
				g_error_free (error);
			}
		}
	}
}

static void
tracker_data_ontology_import_finished (TrackerDataManager *manager)
{
	TrackerClass **classes;
	TrackerProperty **properties;
	guint i, n_props, n_classes;

	classes = tracker_ontologies_get_classes (manager->ontologies, &n_classes);
	properties = tracker_ontologies_get_properties (manager->ontologies, &n_props);

	for (i = 0; i < n_classes; i++) {
		tracker_class_set_is_new (classes[i], FALSE);
		tracker_class_set_db_schema_changed (classes[i], FALSE);
	}

	for (i = 0; i < n_props; i++) {
		tracker_property_set_is_new_domain_index (properties[i], NULL, FALSE);
		tracker_property_set_is_new (properties[i], FALSE);
		tracker_property_set_db_schema_changed (properties[i], FALSE);
		tracker_property_set_cardinality_changed (properties[i], FALSE);
	}
}

static gboolean
query_table_exists (TrackerDBInterface  *iface,
                    const gchar         *table_name,
                    GError             **error)
{
	TrackerDBCursor *cursor = NULL;
	TrackerDBStatement *stmt;
	gboolean exists = FALSE;

	stmt = tracker_db_interface_create_statement (iface, TRACKER_DB_STATEMENT_CACHE_TYPE_SELECT, error,
	                                              "SELECT 1 FROM sqlite_master WHERE tbl_name=\"%s\" AND type=\"table\"",
	                                              table_name);
	if (stmt) {
		cursor = tracker_db_statement_start_cursor (stmt, error);
		g_object_unref (stmt);
	}

	if (cursor) {
		if (tracker_db_cursor_iter_next (cursor, NULL, error)) {
			exists = TRUE;
		}
		g_object_unref (cursor);
	}

	return exists;
}

static gboolean
create_base_tables (TrackerDataManager  *manager,
                    TrackerDBInterface  *iface,
                    GError             **error)
{
	GError *internal_error = NULL;

	tracker_db_interface_execute_query (iface, &internal_error,
	                                    "CREATE TABLE Resource (ID INTEGER NOT NULL PRIMARY KEY,"
	                                    " Uri TEXT NOT NULL, Refcount INTEGER DEFAULT 0, UNIQUE (Uri))");

	if (internal_error) {
		g_propagate_error (error, internal_error);
		return FALSE;
	}

	tracker_db_interface_execute_query (iface, &internal_error,
	                                    "CREATE TABLE Graph (ID INTEGER NOT NULL PRIMARY KEY)");

	if (internal_error) {
		g_propagate_error (error, internal_error);
		return FALSE;
	}

	return TRUE;
}

static void
tracker_data_ontology_setup_db (TrackerDataManager  *manager,
                                TrackerDBInterface  *iface,
                                gboolean             in_update,
                                GError             **error)
{

	TrackerClass **classes;
	guint i, n_classes;

	classes = tracker_ontologies_get_classes (manager->ontologies, &n_classes);

	/* create tables */
	for (i = 0; i < n_classes; i++) {
		GError *internal_error = NULL;

		/* Also !is_new classes are processed, they might have new properties */
		create_decomposed_metadata_tables (manager, iface, classes[i], in_update,
		                                   tracker_class_get_db_schema_changed (classes[i]),
		                                   &internal_error);

		if (internal_error) {
			g_propagate_error (error, internal_error);
			return;
		}
	}
}

static void
tracker_data_ontology_import_into_db (TrackerDataManager  *manager,
                                      TrackerDBInterface  *iface,
                                      gboolean             in_update,
                                      GError             **error)
{
	TrackerClass **classes;
	TrackerProperty **properties;
	guint i, n_props, n_classes;

	classes = tracker_ontologies_get_classes (manager->ontologies, &n_classes);
	properties = tracker_ontologies_get_properties (manager->ontologies, &n_props);

	/* insert classes into rdfs:Resource table */
	for (i = 0; i < n_classes; i++) {
		if (tracker_class_get_is_new (classes[i]) == in_update) {
			GError *internal_error = NULL;

			insert_uri_in_resource_table (manager, iface,
			                              tracker_class_get_uri (classes[i]),
			                              tracker_class_get_id (classes[i]),
			                              &internal_error);

			if (internal_error) {
				g_propagate_error (error, internal_error);
				return;
			}
		}
	}

	/* insert properties into rdfs:Resource table */
	for (i = 0; i < n_props; i++) {
		if (tracker_property_get_is_new (properties[i]) == in_update) {
			GError *internal_error = NULL;

			insert_uri_in_resource_table (manager, iface,
			                              tracker_property_get_uri (properties[i]),
			                              tracker_property_get_id (properties[i]),
			                              &internal_error);

			if (internal_error) {
				g_propagate_error (error, internal_error);
				return;
			}
		}
	}

}

static gint
compare_file_names (GFile *file_a,
                    GFile *file_b)
{
	gchar *name_a, *name_b;
	gint return_val;

	name_a = g_file_get_basename (file_a);
	name_b = g_file_get_basename (file_b);
	return_val = strcmp (name_a, name_b);

	g_free (name_a);
	g_free (name_b);

	return return_val;
}

static GList*
get_ontologies (TrackerDataManager  *manager,
                GFile               *ontologies,
                GError             **error)
{
	GFileEnumerator *enumerator;
	GList *sorted = NULL;

	enumerator = g_file_enumerate_children (ontologies,
	                                        G_FILE_ATTRIBUTE_STANDARD_NAME,
	                                        G_FILE_QUERY_INFO_NONE,
	                                        NULL, error);
	if (!enumerator)
		return NULL;

	while (TRUE) {
		GFileInfo *info;
		GFile *child;
		const gchar *name;

		if (!g_file_enumerator_iterate (enumerator, &info, &child, NULL, error)) {
			g_list_free_full (sorted, g_object_unref);
			g_object_unref (enumerator);
			return NULL;
		}

		if (!info)
			break;

		name = g_file_info_get_name (info);
		if (g_str_has_suffix (name, ".ontology"))
			sorted = g_list_prepend (sorted, g_object_ref (child));
	}

	sorted = g_list_sort (sorted, (GCompareFunc) compare_file_names);

	/* Add our builtin ontologies so they are loaded first */
	sorted = g_list_prepend (sorted, g_file_new_for_uri ("resource://org/freedesktop/tracker/ontology/31-nao.ontology"));
	sorted = g_list_prepend (sorted, g_file_new_for_uri ("resource://org/freedesktop/tracker/ontology/20-dc.ontology"));
	sorted = g_list_prepend (sorted, g_file_new_for_uri ("resource://org/freedesktop/tracker/ontology/12-nrl.ontology"));
	sorted = g_list_prepend (sorted, g_file_new_for_uri ("resource://org/freedesktop/tracker/ontology/11-rdf.ontology"));
	sorted = g_list_prepend (sorted, g_file_new_for_uri ("resource://org/freedesktop/tracker/ontology/10-xsd.ontology"));

	g_object_unref (enumerator);

	return sorted;
}


static gint
get_new_service_id (TrackerDBInterface *iface)
{
	TrackerDBCursor    *cursor = NULL;
	TrackerDBStatement *stmt;
	gint max_service_id = 0;
	GError *error = NULL;

	/* Don't intermix this thing with tracker_data_update_get_new_service_id,
	 * if you use this, know what you are doing! */
	stmt = tracker_db_interface_create_statement (iface, TRACKER_DB_STATEMENT_CACHE_TYPE_SELECT, &error,
	                                              "SELECT MAX(ID) AS A FROM Resource WHERE ID <= %d", TRACKER_ONTOLOGIES_MAX_ID);

	if (stmt) {
		cursor = tracker_db_statement_start_cursor (stmt, &error);
		g_object_unref (stmt);
	}

	if (cursor) {
		if (tracker_db_cursor_iter_next (cursor, NULL, &error)) {
			max_service_id = tracker_db_cursor_get_int (cursor, 0);
		}
		g_object_unref (cursor);
	}

	if (error) {
		g_error ("Unable to get max ID, aborting: %s", error->message);
	}

	return ++max_service_id;
}

static void
tracker_data_manager_update_status (TrackerDataManager *manager,
                                    const gchar        *status)
{
	g_free (manager->status);
	manager->status = g_strdup (status);
	g_object_notify (G_OBJECT (manager), "status");
}

static void
busy_callback (const gchar *status,
               gdouble      progress,
               gpointer     user_data)
{
	tracker_data_manager_update_status (user_data, status);
}


static void
tracker_data_manager_recreate_indexes (TrackerDataManager  *manager,
                                       GError             **error)
{
	GError *internal_error = NULL;
	TrackerProperty **properties;
	guint n_properties;
	guint i;

	properties = tracker_ontologies_get_properties (manager->ontologies, &n_properties);
	if (!properties) {
		g_critical ("Couldn't get all properties to recreate indexes");
		return;
	}

	g_debug ("Dropping all indexes...");
	for (i = 0; i < n_properties; i++) {
		fix_indexed (manager, properties [i], FALSE, &internal_error);

		if (internal_error) {
			g_propagate_error (error, internal_error);
			return;
		}
	}

	g_debug ("Starting index re-creation...");
	for (i = 0; i < n_properties; i++) {
		fix_indexed (manager, properties [i], TRUE, &internal_error);

		if (internal_error) {
			g_critical ("Unable to create index for %s: %s",
			            tracker_property_get_name (properties[i]),
			            internal_error->message);
			g_clear_error (&internal_error);
		}

		busy_callback ("Recreating indexes",
		               (gdouble) ((gdouble) i / (gdouble) n_properties),
		               manager);
	}
	g_debug ("  Finished index re-creation...");
}

static gboolean
write_ontologies_gvdb (TrackerDataManager  *manager,
                       gboolean             overwrite,
                       GError             **error)
{
	gboolean retval = TRUE;
	gchar *filename;
	GFile *child;

	child = g_file_get_child (manager->cache_location, "ontologies.gvdb");
	filename = g_file_get_path (child);
	g_object_unref (child);

	if (overwrite || !g_file_test (filename, G_FILE_TEST_EXISTS)) {
		retval = tracker_ontologies_write_gvdb (manager->ontologies, filename, error);
	}

	g_free (filename);

	return retval;
}

static void
load_ontologies_gvdb (TrackerDataManager  *manager,
                      GError             **error)
{
	gchar *filename;
	GFile *child;

	child = g_file_get_child (manager->cache_location, "ontologies.gvdb");
	filename = g_file_get_path (child);
	g_object_unref (child);

	tracker_ontologies_load_gvdb (manager->ontologies, filename, error);
	g_free (filename);
}

#if HAVE_TRACKER_FTS
static gboolean
tracker_data_manager_fts_changed (TrackerDataManager *manager)
{
	TrackerProperty **properties;
	gboolean has_changed = FALSE;
	guint i, len;

	properties = tracker_ontologies_get_properties (manager->ontologies, &len);

	for (i = 0; i < len; i++) {
		TrackerClass *class;

		if (tracker_property_get_fulltext_indexed (properties[i]) !=
		    tracker_property_get_orig_fulltext_indexed (properties[i])) {
			has_changed |= TRUE;
		}

		if (!tracker_property_get_fulltext_indexed (properties[i])) {
			continue;
		}

		has_changed |= tracker_property_get_is_new (properties[i]);

		/* We must also regenerate FTS if any table in the view
		 * updated its schema.
		 */
		class = tracker_property_get_domain (properties[i]);
		has_changed |= tracker_class_get_db_schema_changed (class);
	}

	return has_changed;
}

static void
ontology_get_fts_properties (TrackerDataManager  *manager,
                             GHashTable         **fts_properties,
                             GHashTable         **multivalued)
{
	TrackerProperty **properties;
	guint i, len;

	properties = tracker_ontologies_get_properties (manager->ontologies, &len);
	*multivalued = g_hash_table_new (g_str_hash, g_str_equal);
	*fts_properties = g_hash_table_new_full (g_str_hash, g_str_equal,
	                                         NULL, (GDestroyNotify) g_list_free);

	for (i = 0; i < len; i++) {
		const gchar *name, *table_name;
		GList *list;

		if (!tracker_property_get_fulltext_indexed (properties[i])) {
			continue;
		}

		table_name = tracker_property_get_table_name (properties[i]);
		name = tracker_property_get_name (properties[i]);
		list = g_hash_table_lookup (*fts_properties, table_name);

		if (tracker_property_get_multiple_values (properties[i])) {
			g_hash_table_insert (*multivalued, (gpointer) table_name,
			                     GUINT_TO_POINTER (TRUE));
		}

		if (!list) {
			list = g_list_prepend (NULL, (gpointer) name);
			g_hash_table_insert (*fts_properties, (gpointer) table_name, list);
		} else {
			list = g_list_append (list, (gpointer) name);
		}
	}
}

static void
rebuild_fts_tokens (TrackerDataManager *manager,
                    TrackerDBInterface *iface)
{
	g_debug ("Rebuilding FTS tokens, this may take a moment...");
	tracker_db_interface_sqlite_fts_rebuild_tokens (iface);
	g_debug ("FTS tokens rebuilt");

	/* Update the stamp file */
	tracker_db_manager_tokenizer_update (manager->db_manager);
}

gboolean
tracker_data_manager_init_fts (TrackerDBInterface *iface,
                               gboolean            create)
{
	GHashTable *fts_props, *multivalued;
	TrackerDataManager *manager;

	manager = tracker_db_interface_get_user_data (iface);
	ontology_get_fts_properties (manager, &fts_props, &multivalued);
	tracker_db_interface_sqlite_fts_init (iface, fts_props,
	                                      multivalued, create);
	g_hash_table_unref (fts_props);
	g_hash_table_unref (multivalued);
	return TRUE;
}

static void
tracker_data_manager_update_fts (TrackerDataManager *manager,
                                 TrackerDBInterface *iface)
{
	GHashTable *fts_properties, *multivalued;

	ontology_get_fts_properties (manager, &fts_properties, &multivalued);
	tracker_db_interface_sqlite_fts_alter_table (iface, fts_properties, multivalued);
	g_hash_table_unref (fts_properties);
	g_hash_table_unref (multivalued);
}
#endif

GFile *
tracker_data_manager_get_cache_location (TrackerDataManager *manager)
{
	return manager->cache_location ? g_object_ref (manager->cache_location) : NULL;
}

GFile *
tracker_data_manager_get_data_location (TrackerDataManager *manager)
{
	return manager->data_location ? g_object_ref (manager->data_location) : NULL;
}

TrackerDataManager *
tracker_data_manager_new (TrackerDBManagerFlags   flags,
                          GFile                  *cache_location,
                          GFile                  *data_location,
                          GFile                  *ontology_location,
                          gboolean                restoring_backup,
                          guint                   select_cache_size,
                          guint                   update_cache_size)
{
	TrackerDataManager *manager;

	if (!cache_location || !data_location || !ontology_location) {
		g_warning ("All data storage and ontology locations must be provided");
		return NULL;
	}

	manager = g_object_new (TRACKER_TYPE_DATA_MANAGER, NULL);

	/* TODO: Make these properties */
	g_set_object (&manager->cache_location, cache_location);
	g_set_object (&manager->ontology_location, ontology_location);
	g_set_object (&manager->data_location, data_location);
	manager->flags = flags;
	manager->restoring_backup = restoring_backup;
	manager->select_cache_size = select_cache_size;
	manager->update_cache_size = update_cache_size;

	return manager;
}

static void
update_ontology_last_modified (TrackerDataManager  *manager,
                               TrackerDBInterface  *iface,
                               TrackerOntology     *ontology,
                               GError             **error)
{
	TrackerDBStatement *stmt;
	const gchar *ontology_uri;
	time_t last_mod;

	ontology_uri = tracker_ontology_get_uri (ontology);
	last_mod = tracker_ontology_get_last_modified (ontology);
	stmt = tracker_db_interface_create_statement (iface, TRACKER_DB_STATEMENT_CACHE_TYPE_UPDATE, error,
	                                              "UPDATE \"rdfs:Resource\" SET \"nao:lastModified\"= ? "
	                                              "WHERE \"rdfs:Resource\".ID = "
	                                              "(SELECT Resource.ID FROM Resource INNER JOIN \"rdfs:Resource\" "
	                                              "ON \"rdfs:Resource\".ID = Resource.ID WHERE "
	                                              "Resource.Uri = ?)");
	if (stmt) {
		tracker_db_statement_bind_int (stmt, 0, last_mod);
		tracker_db_statement_bind_text (stmt, 1, ontology_uri);
		tracker_db_statement_execute (stmt, error);
		g_object_unref (stmt);
	}
}

static gboolean
tracker_data_manager_initable_init (GInitable     *initable,
                                    GCancellable  *cancellable,
                                    GError       **error)
{
	TrackerDataManager *manager = TRACKER_DATA_MANAGER (initable);
	TrackerDBInterface *iface;
	gboolean is_first_time_index, check_ontology, has_graph_table = FALSE;
	TrackerDBCursor *cursor;
	TrackerDBStatement *stmt;
	GHashTable *ontos_table;
	GList *sorted = NULL, *l;
	gint max_id = 0;
	gboolean read_only;
	GHashTable *uri_id_map = NULL;
	GError *internal_error = NULL;

	if (manager->initialized) {
		return TRUE;
	}

	if (!g_file_is_native (manager->cache_location) ||
	    !g_file_is_native (manager->data_location)) {
		g_set_error (error,
		             TRACKER_DATA_ONTOLOGY_ERROR,
		             TRACKER_DATA_UNSUPPORTED_LOCATION,
		             "Cache and data locations must be local");
		return FALSE;
	}

	read_only = (manager->flags & TRACKER_DB_MANAGER_READONLY) ? TRUE : FALSE;

	/* Make sure we initialize all other modules we depend on */
	manager->data_update = tracker_data_new (manager);
	manager->ontologies = tracker_ontologies_new ();

	manager->db_manager = tracker_db_manager_new (manager->flags,
	                                              manager->cache_location,
	                                              manager->data_location,
	                                              &is_first_time_index,
	                                              manager->restoring_backup,
	                                              FALSE,
	                                              manager->select_cache_size,
	                                              manager->update_cache_size,
	                                              busy_callback, manager, "",
	                                              G_OBJECT (manager),
						      manager->ontologies,
	                                              &internal_error);
	if (!manager->db_manager) {
		g_propagate_error (error, internal_error);
		return FALSE;
	}

	manager->first_time_index = is_first_time_index;

	tracker_data_manager_update_status (manager, "Initializing data manager");

	iface = tracker_db_manager_get_writable_db_interface (manager->db_manager);


	if (g_file_query_file_type (manager->ontology_location, G_FILE_QUERY_INFO_NONE, NULL) != G_FILE_TYPE_DIRECTORY) {
		gchar *uri;

		uri = g_file_get_uri (manager->ontology_location);
		g_set_error (error, TRACKER_DATA_ONTOLOGY_ERROR,
		             TRACKER_DATA_ONTOLOGY_NOT_FOUND,
		             "'%s' is not a ontology location", uri);
		g_free (uri);
		return FALSE;
	}

	if (is_first_time_index && !read_only) {
		sorted = get_ontologies (manager, manager->ontology_location, &internal_error);

		if (internal_error) {
			g_propagate_error (error, internal_error);
			return FALSE;
		}

		/* load ontology from files into memory (max_id starts at zero: first-time) */

		for (l = sorted; l; l = l->next) {
			GError *ontology_error = NULL;
			GFile *ontology_file = l->data;
			gchar *uri = g_file_get_uri (ontology_file);

			g_debug ("Loading ontology %s", uri);

			load_ontology_file (manager, ontology_file,
			                    &max_id,
			                    FALSE,
			                    NULL,
			                    NULL,
			                    uri_id_map,
			                    &ontology_error);
			if (ontology_error) {
				g_error ("Error loading ontology (%s): %s",
				         uri, ontology_error->message);
			}

			g_free (uri);
		}

		tracker_data_begin_ontology_transaction (manager->data_update, &internal_error);
		if (internal_error) {
			g_propagate_error (error, internal_error);
			return FALSE;
		}

		if (!create_base_tables (manager, iface, error)) {
			return FALSE;
		}

		tracker_data_ontology_setup_db (manager, iface, FALSE,
		                                &internal_error);

		if (!internal_error) {
			tracker_data_ontology_import_into_db (manager, iface,
							      FALSE,
			                                      &internal_error);
		}

		if (internal_error) {
			g_propagate_error (error, internal_error);
			return FALSE;
		}

#if HAVE_TRACKER_FTS
		tracker_data_manager_init_fts (iface, TRUE);
#endif

		/* store ontology in database */
		for (l = sorted; l; l = l->next) {
			import_ontology_file (manager, l->data, FALSE);
		}

		tracker_data_commit_transaction (manager->data_update, &internal_error);

		if (internal_error) {
			g_propagate_error (error, internal_error);
			return FALSE;
		}

		write_ontologies_gvdb (manager, TRUE /* overwrite */, NULL);

		g_list_free_full (sorted, g_object_unref);
		sorted = NULL;

		/* First time, no need to check ontology */
		check_ontology = FALSE;
	} else {
		if (!read_only) {
			/* Load ontology from database into memory */
			db_get_static_data (iface, manager, &internal_error);
			check_ontology = (manager->flags & TRACKER_DB_MANAGER_DO_NOT_CHECK_ONTOLOGY) == 0;

			if (internal_error) {
				g_propagate_error (error, internal_error);
				return FALSE;
			}

			write_ontologies_gvdb (manager, FALSE /* overwrite */, NULL);

			/* Skipped in the read-only case as it can't work with direct access and
			   it reduces initialization time */
			clean_decomposed_transient_metadata (manager, iface);
		} else {
			GError *gvdb_error = NULL;

			load_ontologies_gvdb (manager, &gvdb_error);
			check_ontology = FALSE;

			if (gvdb_error) {
				g_clear_error (&gvdb_error);

				/* fall back to loading ontology from database into memory */
				db_get_static_data (iface, manager, &internal_error);
				if (internal_error) {
					g_propagate_error (error, internal_error);
					return FALSE;
				}
			}
		}

#if HAVE_TRACKER_FTS
		tracker_data_manager_init_fts (iface, FALSE);
#endif
	}

	if (!read_only) {
		has_graph_table = query_table_exists (iface, "Graph", &internal_error);
		if (internal_error) {
			g_propagate_error (error, internal_error);
			return FALSE;
		}

		check_ontology |= !has_graph_table;
	}

	if (check_ontology) {
		GList *to_reload = NULL;
		GList *ontos = NULL;
		GPtrArray *seen_classes;
		GPtrArray *seen_properties;
		GError *n_error = NULL;
		gboolean transaction_started = FALSE;

		seen_classes = g_ptr_array_new ();
		seen_properties = g_ptr_array_new ();

		/* Get all the ontology files from ontology_location */
		ontos = get_ontologies (manager, manager->ontology_location, &internal_error);

		if (internal_error) {
			g_propagate_error (error, internal_error);
			return FALSE;
		}

		/* check ontology against database */

		/* Get a map of tracker:Ontology v. nao:lastModified so that we can test
		 * for all the ontology files in ontology_location whether the last-modified
		 * has changed since we dealt with the file last time. */

		stmt = tracker_db_interface_create_statement (iface, TRACKER_DB_STATEMENT_CACHE_TYPE_SELECT, &n_error,
		        "SELECT Resource.Uri, \"rdfs:Resource\".\"nao:lastModified\" FROM \"tracker:Ontology\" "
		        "INNER JOIN Resource ON Resource.ID = \"tracker:Ontology\".ID "
		        "INNER JOIN \"rdfs:Resource\" ON \"tracker:Ontology\".ID = \"rdfs:Resource\".ID");

		if (stmt) {
			cursor = tracker_db_statement_start_cursor (stmt, &n_error);
			g_object_unref (stmt);
		} else {
			cursor = NULL;
		}

		ontos_table = g_hash_table_new_full (g_str_hash,
		                                     g_str_equal,
		                                     g_free,
		                                     NULL);

		if (cursor) {
			while (tracker_db_cursor_iter_next (cursor, NULL, &n_error)) {
				const gchar *onto_uri = tracker_db_cursor_get_string (cursor, 0, NULL);
				/* It's stored as an int in the db anyway. This is caused by
				 * string_to_gvalue in tracker-data-update.c */
				gint value = tracker_db_cursor_get_int (cursor, 1);

				g_hash_table_insert (ontos_table, g_strdup (onto_uri),
				                     GINT_TO_POINTER (value));
			}

			g_object_unref (cursor);
		}

		if (n_error) {
			g_warning ("%s", n_error->message);
			g_clear_error (&n_error);
		}

		for (l = ontos; l; l = l->next) {
			TrackerOntology *ontology;
			GFile *ontology_file = l->data;
			const gchar *ontology_uri;
			gboolean found, update_nao = FALSE;
			gpointer value;
			gint last_mod;

			/* Parse a TrackerOntology from ontology_file */
			ontology = get_ontology_from_file (manager, ontology_file);

			if (!ontology) {
				/* TODO: cope with full custom .ontology files: deal with this
				 * error gracefully. App devs might install wrong ontology files
				 * and we shouldn't critical() due to this. */
				gchar *uri = g_file_get_uri (ontology_file);
				g_critical ("Can't get ontology from file: %s", uri);
				g_free (uri);
				continue;
			}

			ontology_uri = tracker_ontology_get_uri (ontology);
			/* We can't do better than this cast, it's stored as an int in the
			 * db. See above comment for more info. */
			last_mod = (gint) tracker_ontology_get_last_modified (ontology);

			found = g_hash_table_lookup_extended (ontos_table,
			                                      ontology_uri,
			                                      NULL, &value);

			if (found) {
				GError *ontology_error = NULL;
				gint val = GPOINTER_TO_INT (value);

				if (!has_graph_table) {
					/* No graph table and no resource triggers,
					 * the table must be recreated.
					 */
					if (!transaction_started) {
						tracker_data_begin_ontology_transaction (manager->data_update, &internal_error);
						if (internal_error) {
							g_propagate_error (error, internal_error);
							return FALSE;
						}
						transaction_started = TRUE;
					}

					to_reload = g_list_prepend (to_reload, ontology_file);
					continue;
				}

				/* When the last-modified in our database isn't the same as the last
				 * modified in the latest version of the file, deal with changes. */
				if (val != last_mod) {
					gchar *uri = g_file_get_uri (ontology_file);

					g_debug ("Ontology file '%s' needs update", uri);
					g_free (uri);

					if (!transaction_started) {
						tracker_data_begin_ontology_transaction (manager->data_update, &internal_error);
						if (internal_error) {
							g_propagate_error (error, internal_error);
							return FALSE;
						}
						transaction_started = TRUE;
					}

					if (max_id == 0) {
						/* In case of first-time, this wont start at zero */
						max_id = get_new_service_id (iface);
					}
					/* load ontology from files into memory, set all new's
					 * is_new to TRUE */
					load_ontology_file (manager, ontology_file,
					                    &max_id,
					                    TRUE,
					                    seen_classes,
					                    seen_properties,
					                    uri_id_map,
					                    &ontology_error);

					if (g_error_matches (ontology_error,
					                     TRACKER_DATA_ONTOLOGY_ERROR,
					                     TRACKER_DATA_UNSUPPORTED_ONTOLOGY_CHANGE)) {
						g_warning ("%s", ontology_error->message);
						g_error_free (ontology_error);

						tracker_data_ontology_free_seen (seen_classes);
						tracker_data_ontology_free_seen (seen_properties);
						tracker_data_ontology_import_finished (manager);

						/* as we're processing an ontology change,
						   transaction is guaranteed to be started */
						tracker_data_rollback_transaction (manager->data_update);

						if (ontos_table) {
							g_hash_table_unref (ontos_table);
						}
						if (ontos) {
							g_list_free_full (ontos, g_object_unref);
						}
						g_object_unref (manager->ontology_location);
						if (uri_id_map) {
							g_hash_table_unref (uri_id_map);
						}

						goto skip_ontology_check;
					}

					if (ontology_error) {
						g_critical ("Fatal error dealing with ontology changes: %s", ontology_error->message);
						g_error_free (ontology_error);
					}

					to_reload = g_list_prepend (to_reload, l->data);
					update_nao = TRUE;
				}
			} else {
				GError *ontology_error = NULL;
				gchar *uri = g_file_get_uri (ontology_file);

				g_debug ("Ontology file '%s' got added", uri);
				g_free (uri);

				if (!transaction_started) {
					tracker_data_begin_ontology_transaction (manager->data_update, &internal_error);
					if (internal_error) {
						g_propagate_error (error, internal_error);
						return FALSE;
					}
					transaction_started = TRUE;
				}

				if (max_id == 0) {
					/* In case of first-time, this wont start at zero */
					max_id = get_new_service_id (iface);
				}
				/* load ontology from files into memory, set all new's
				 * is_new to TRUE */
				load_ontology_file (manager, ontology_file,
				                    &max_id,
				                    TRUE,
				                    seen_classes,
				                    seen_properties,
				                    uri_id_map,
				                    &ontology_error);

				if (g_error_matches (ontology_error,
				                     TRACKER_DATA_ONTOLOGY_ERROR,
				                     TRACKER_DATA_UNSUPPORTED_ONTOLOGY_CHANGE)) {
					g_warning ("%s", ontology_error->message);
					g_error_free (ontology_error);

					tracker_data_ontology_free_seen (seen_classes);
					tracker_data_ontology_free_seen (seen_properties);
					tracker_data_ontology_import_finished (manager);

					/* as we're processing an ontology change,
					   transaction is guaranteed to be started */
					tracker_data_rollback_transaction (manager->data_update);

					if (ontos_table) {
						g_hash_table_unref (ontos_table);
					}
					if (ontos) {
						g_list_free_full (ontos, g_object_unref);
					}
					if (uri_id_map) {
						g_hash_table_unref (uri_id_map);
					}

					goto skip_ontology_check;
				}

				if (ontology_error) {
					g_critical ("Fatal error dealing with ontology changes: %s", ontology_error->message);
					g_error_free (ontology_error);
				}

				to_reload = g_list_prepend (to_reload, l->data);
				update_nao = TRUE;
			}

			if (update_nao) {
				update_ontology_last_modified (manager, iface, ontology, &n_error);

				if (n_error) {
					g_critical ("%s", n_error->message);
					g_clear_error (&n_error);
				}
			}

			g_object_unref (ontology);
		}

		if (to_reload) {
			GError *ontology_error = NULL;

			tracker_data_ontology_process_changes_pre_db (manager,
			                                              seen_classes,
			                                              seen_properties,
			                                              &ontology_error);

			if (!ontology_error) {
				/* Perform ALTER-TABLE and CREATE-TABLE calls for all that are is_new */
#if HAVE_TRACKER_FTS
				gboolean update_fts;

				update_fts = tracker_data_manager_fts_changed (manager);

				if (update_fts)
					tracker_db_interface_sqlite_fts_delete_table (iface);
#endif

				tracker_data_ontology_setup_db (manager, iface, TRUE,
				                                &ontology_error);

				if (!ontology_error) {
					tracker_data_ontology_import_into_db (manager, iface, TRUE,
					                                      &ontology_error);
				}

				if (!ontology_error) {
#if HAVE_TRACKER_FTS
					if (update_fts) {
						tracker_data_manager_update_fts (manager, iface);
					} else {
						tracker_data_manager_init_fts (iface, FALSE);
					}
#endif
					tracker_data_ontology_process_changes_post_db (manager,
					                                               seen_classes,
					                                               seen_properties,
					                                               &ontology_error);
				}
			}

			if (g_error_matches (ontology_error,
			                     TRACKER_DATA_ONTOLOGY_ERROR,
			                     TRACKER_DATA_UNSUPPORTED_ONTOLOGY_CHANGE)) {
				g_warning ("%s", ontology_error->message);
				g_error_free (ontology_error);

				tracker_data_ontology_free_seen (seen_classes);
				tracker_data_ontology_free_seen (seen_properties);
				tracker_data_ontology_import_finished (manager);

				/* as we're processing an ontology change,
				   transaction is guaranteed to be started */
				tracker_data_rollback_transaction (manager->data_update);

				if (ontos_table) {
					g_hash_table_unref (ontos_table);
				}
				if (ontos) {
					g_list_free_full (ontos, g_object_unref);
				}
				if (uri_id_map) {
					g_hash_table_unref (uri_id_map);
				}

				goto skip_ontology_check;
			}

			if (ontology_error) {
				g_propagate_error (error, ontology_error);
				return FALSE;
			}

			for (l = to_reload; l; l = l->next) {
				GFile *ontology_file = l->data;
				/* store ontology in database */
				import_ontology_file (manager, ontology_file, TRUE);
			}
			g_list_free (to_reload);

			tracker_data_ontology_process_changes_post_import (seen_classes, seen_properties);

			write_ontologies_gvdb (manager, TRUE /* overwrite */, NULL);
		}

		tracker_data_ontology_free_seen (seen_classes);
		tracker_data_ontology_free_seen (seen_properties);

		/* Reset the is_new flag for all classes and properties */
		tracker_data_ontology_import_finished (manager);

		if (transaction_started) {
			tracker_data_commit_transaction (manager->data_update, &internal_error);
			if (internal_error) {
				g_propagate_error (error, internal_error);
				return FALSE;
			}
		}

		g_hash_table_unref (ontos_table);
		g_list_free_full (ontos, g_object_unref);
	} else if (is_first_time_index && !read_only) {
		TrackerOntology **ontologies;
		guint n_ontologies, i;

		ontologies = tracker_ontologies_get_ontologies (manager->ontologies, &n_ontologies);

		for (i = 0; i < n_ontologies; i++) {
			GError *n_error = NULL;

			update_ontology_last_modified (manager, iface, ontologies[i], &n_error);

			if (n_error) {
				g_critical ("%s", n_error->message);
				g_clear_error (&n_error);
			}
		}
	}

skip_ontology_check:
	/* If locale changed, re-create indexes */
	if (!read_only && tracker_db_manager_locale_changed (manager->db_manager, NULL)) {
		/* No need to reset the collator in the db interface,
		 * as this is only executed during startup, which should
		 * already have the proper locale set in the collator */
		tracker_data_manager_recreate_indexes (manager, &internal_error);

		if (internal_error) {
			g_propagate_error (error, internal_error);
			return FALSE;
		}

		tracker_db_manager_set_current_locale (manager->db_manager);

#if HAVE_TRACKER_FTS
		rebuild_fts_tokens (manager, iface);
	} else if (!read_only && tracker_db_manager_get_tokenizer_changed (manager->db_manager)) {
		rebuild_fts_tokens (manager, iface);
#endif
	}

	if (!read_only) {
		tracker_ontologies_sort (manager->ontologies);
	}

	manager->initialized = TRUE;

	/* This is the only one which doesn't show the 'OPERATION' part */
	tracker_data_manager_update_status (manager, "Idle");

	return TRUE;
}

static gboolean
data_manager_check_perform_cleanup (TrackerDataManager *manager)
{
	TrackerDBStatement *stmt;
	TrackerDBInterface *iface;
	TrackerDBCursor *cursor = NULL;
	guint count = 0;

	iface = tracker_db_manager_get_writable_db_interface (manager->db_manager);
	stmt = tracker_db_interface_create_statement (iface, TRACKER_DB_STATEMENT_CACHE_TYPE_NONE,
	                                              NULL, "SELECT COUNT(*) FROM Graph");
	if (stmt) {
		cursor = tracker_db_statement_start_cursor (stmt, NULL);
		g_object_unref (stmt);
	}

	if (cursor && tracker_db_cursor_iter_next (cursor, NULL, NULL))
		count = tracker_db_cursor_get_int (cursor, 0);

	g_clear_object (&cursor);

	/* We need to be sure the data is coherent, so we'll refrain from
	 * doing any clean ups till there are elements in the Graph table.
	 *
	 * A database that's been freshly updated to the refcounted
	 * resources will have an empty Graph table, so we might
	 * unintentionally delete graph URNs if we clean up in this state.
	 */
	if (count == 0)
		return FALSE;

	count = 0;
	stmt = tracker_db_interface_create_statement (iface, TRACKER_DB_STATEMENT_CACHE_TYPE_NONE, NULL,
	                                              "SELECT COUNT(*) FROM Resource WHERE Refcount <= 0 "
	                                              "AND Resource.ID > %d AND Resource.ID NOT IN (SELECT ID FROM Graph)",
	                                              TRACKER_ONTOLOGIES_MAX_ID);
	if (stmt) {
		cursor = tracker_db_statement_start_cursor (stmt, NULL);
		g_object_unref (stmt);
	}

	if (cursor && tracker_db_cursor_iter_next (cursor, NULL, NULL))
		count = tracker_db_cursor_get_int (cursor, 0);

	g_clear_object (&cursor);

	return count > 0;
}

void
tracker_data_manager_dispose (GObject *object)
{
	TrackerDataManager *manager = TRACKER_DATA_MANAGER (object);
	TrackerDBStatement *stmt;
	TrackerDBInterface *iface;
	GError *error = NULL;
	gboolean readonly = TRUE;

	if (manager->db_manager) {
		readonly = (tracker_db_manager_get_flags (manager->db_manager, NULL, NULL) & TRACKER_DB_MANAGER_READONLY) != 0;

		if (!readonly && data_manager_check_perform_cleanup (manager)) {
			/* Delete stale URIs in the Resource table */
			g_debug ("Cleaning up stale resource URIs");

			iface = tracker_db_manager_get_writable_db_interface (manager->db_manager);
			stmt = tracker_db_interface_create_statement (iface, TRACKER_DB_STATEMENT_CACHE_TYPE_UPDATE,
			                                              &error,
			                                              "DELETE FROM Resource WHERE Refcount <= 0 "
			                                              "AND Resource.ID > %d AND Resource.ID NOT IN (SELECT ID FROM Graph)",
			                                              TRACKER_ONTOLOGIES_MAX_ID);

			if (stmt) {
				tracker_db_statement_execute (stmt, &error);
				g_object_unref (stmt);
			}

			if (error) {
				g_warning ("Could not clean up stale resource URIs: %s\n",
				           error->message);
				g_clear_error (&error);
			}

			tracker_db_manager_check_perform_vacuum (manager->db_manager);
		}

		g_clear_pointer (&manager->db_manager, tracker_db_manager_free);
	}

	G_OBJECT_CLASS (tracker_data_manager_parent_class)->dispose (object);
}

void
tracker_data_manager_finalize (GObject *object)
{
	TrackerDataManager *manager = TRACKER_DATA_MANAGER (object);

	g_clear_object (&manager->ontologies);
	g_clear_object (&manager->data_update);
	g_free (manager->status);

	G_OBJECT_CLASS (tracker_data_manager_parent_class)->finalize (object);
}

static void
tracker_data_manager_initable_iface_init (GInitableIface *iface)
{
	iface->init = tracker_data_manager_initable_init;
}

static void
tracker_data_manager_get_property (GObject    *object,
                                   guint       prop_id,
                                   GValue     *value,
                                   GParamSpec *pspec)
{
	TrackerDataManager *manager = TRACKER_DATA_MANAGER (object);

	switch (prop_id) {
	case PROP_STATUS:
		g_value_set_string (value, manager->status);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
tracker_data_manager_class_init (TrackerDataManagerClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->get_property = tracker_data_manager_get_property;
	object_class->dispose = tracker_data_manager_dispose;
	object_class->finalize = tracker_data_manager_finalize;

	g_object_class_install_property (object_class,
	                                 PROP_STATUS,
	                                 g_param_spec_string ("status",
	                                                      "Status",
	                                                      "Status",
	                                                      NULL,
	                                                      G_PARAM_READABLE));
}

TrackerOntologies *
tracker_data_manager_get_ontologies (TrackerDataManager *manager)
{
	return manager->ontologies;
}

TrackerDBManager *
tracker_data_manager_get_db_manager (TrackerDataManager *manager)
{
	return manager->db_manager;
}

TrackerDBInterface *
tracker_data_manager_get_db_interface (TrackerDataManager *manager)
{
	return tracker_db_manager_get_db_interface (manager->db_manager);
}

TrackerDBInterface *
tracker_data_manager_get_writable_db_interface (TrackerDataManager *manager)
{
	return tracker_db_manager_get_writable_db_interface (manager->db_manager);
}

TrackerData *
tracker_data_manager_get_data (TrackerDataManager *manager)
{
	return manager->data_update;
}

void
tracker_data_manager_shutdown (TrackerDataManager *manager)
{
	g_object_run_dispose (G_OBJECT (manager));
}

GHashTable *
tracker_data_manager_get_namespaces (TrackerDataManager *manager)
{
	TrackerNamespace **namespaces;
	guint i, n_namespaces;
	GHashTable *ht;

	ht = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
	namespaces = tracker_ontologies_get_namespaces (manager->ontologies,
	                                                &n_namespaces);
	for (i = 0; i < n_namespaces; i++) {
		g_hash_table_insert (ht,
		                     g_strdup (tracker_namespace_get_prefix (namespaces[i])),
		                     g_strdup (tracker_namespace_get_uri (namespaces[i])));
	}

	return ht;
}
