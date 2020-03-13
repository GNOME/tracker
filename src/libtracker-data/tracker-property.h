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

#ifndef __LIBTRACKER_DATA_PROPERTY_H__
#define __LIBTRACKER_DATA_PROPERTY_H__

#include <glib-object.h>

#include "tracker-class.h"

G_BEGIN_DECLS

#if !defined (__LIBTRACKER_DATA_INSIDE__) && !defined (TRACKER_COMPILATION)
#error "only <libtracker-data/tracker-data.h> must be included directly."
#endif

/*
 * TrackerPropertyType
 */
#define TRACKER_TYPE_PROPERTY_TYPE (tracker_property_type_get_type ())

typedef enum {
	TRACKER_PROPERTY_TYPE_UNKNOWN,
	TRACKER_PROPERTY_TYPE_STRING,
	TRACKER_PROPERTY_TYPE_BOOLEAN,
	TRACKER_PROPERTY_TYPE_INTEGER,
	TRACKER_PROPERTY_TYPE_DOUBLE,
	TRACKER_PROPERTY_TYPE_DATE,
	TRACKER_PROPERTY_TYPE_DATETIME,
	TRACKER_PROPERTY_TYPE_RESOURCE,
	TRACKER_PROPERTY_TYPE_LANGSTRING,
} TrackerPropertyType;

GType        tracker_property_type_get_type  (void) G_GNUC_CONST;

/*
 * TrackerProperty
 */
#define TRACKER_TYPE_PROPERTY         (tracker_property_get_type ())
#define TRACKER_TYPE_PROPERTY_TYPE    (tracker_property_type_get_type ())
#define TRACKER_PROPERTY(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), TRACKER_TYPE_PROPERTY, TrackerProperty))
#define TRACKER_PROPERTY_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST ((k), TRACKER_TYPE_PROPERTY, TrackerPropertyClass))
#define TRACKER_IS_PROPERTY(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), TRACKER_TYPE_PROPERTY))
#define TRACKER_IS_PROPERTY_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), TRACKER_TYPE_PROPERTY))
#define TRACKER_PROPERTY_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), TRACKER_TYPE_PROPERTY, TrackerPropertyClass))

/* Forward typedef for TrackerProperty in tracker-class.h */
typedef struct _TrackerPropertyClass TrackerPropertyClass;
typedef struct _TrackerPropertyPrivate TrackerPropertyPrivate;

struct _TrackerProperty {
	GObject parent;
};

struct _TrackerPropertyClass {
	GObjectClass parent_class;
};

GType               tracker_property_get_type                (void) G_GNUC_CONST;
TrackerProperty *   tracker_property_new                     (gboolean              use_gvdb);
const gchar *       tracker_property_get_uri                 (TrackerProperty      *property);
const gchar *       tracker_property_get_name                (TrackerProperty      *property);
const gchar *       tracker_property_get_table_name          (TrackerProperty      *property);
TrackerPropertyType tracker_property_get_data_type           (TrackerProperty      *property);
TrackerClass *      tracker_property_get_domain              (TrackerProperty      *property);
TrackerClass *      tracker_property_get_range               (TrackerProperty      *property);
TrackerClass **     tracker_property_get_domain_indexes      (TrackerProperty      *property);
gint                tracker_property_get_weight              (TrackerProperty      *property);
gint                tracker_property_get_id                  (TrackerProperty      *property);
gboolean            tracker_property_get_indexed             (TrackerProperty      *property);
TrackerProperty *   tracker_property_get_secondary_index     (TrackerProperty      *property);
gboolean            tracker_property_get_orig_fulltext_indexed(TrackerProperty      *property);
gboolean            tracker_property_get_fulltext_indexed    (TrackerProperty      *property);
gboolean            tracker_property_get_multiple_values     (TrackerProperty      *property);
gboolean            tracker_property_get_last_multiple_values(TrackerProperty      *property);
gboolean            tracker_property_get_orig_multiple_values(TrackerProperty      *property);
gboolean            tracker_property_get_is_new              (TrackerProperty      *property);
gboolean            tracker_property_get_is_new_domain_index (TrackerProperty      *property,
                                                              TrackerClass         *class);
gboolean            tracker_property_get_writeback           (TrackerProperty      *property);
const gchar *       tracker_property_get_default_value       (TrackerProperty      *property);
gboolean            tracker_property_get_db_schema_changed   (TrackerProperty      *property);
gboolean            tracker_property_get_cardinality_changed (TrackerProperty      *property);
gboolean            tracker_property_get_is_inverse_functional_property
                                                             (TrackerProperty      *property);
TrackerProperty **  tracker_property_get_super_properties    (TrackerProperty      *property);
void                tracker_property_set_uri                 (TrackerProperty      *property,
                                                              const gchar          *value);
void                tracker_property_set_domain              (TrackerProperty      *property,
                                                              TrackerClass         *value);
void                tracker_property_add_domain_index        (TrackerProperty      *property,
                                                              TrackerClass         *value);
void                tracker_property_del_domain_index        (TrackerProperty      *property,
                                                              TrackerClass         *value);
void                tracker_property_reset_domain_indexes    (TrackerProperty      *property);
void                tracker_property_set_range               (TrackerProperty      *property,
                                                              TrackerClass         *range);
void                tracker_property_set_weight              (TrackerProperty      *property,
                                                              gint                  value);
void                tracker_property_set_id                  (TrackerProperty      *property,
                                                              gint                  value);
void                tracker_property_set_indexed             (TrackerProperty      *property,
                                                              gboolean              value);
void                tracker_property_set_secondary_index     (TrackerProperty      *property,
                                                              TrackerProperty      *value);
void                tracker_property_set_orig_fulltext_indexed(TrackerProperty      *property,
                                                               gboolean              value);
void                tracker_property_set_fulltext_indexed    (TrackerProperty      *property,
                                                              gboolean              value);
void                tracker_property_set_multiple_values     (TrackerProperty      *property,
                                                              gboolean              value);
void                tracker_property_set_last_multiple_values(TrackerProperty      *property,
                                                              gboolean              value);
void                tracker_property_set_orig_multiple_values(TrackerProperty      *property,
                                                              gboolean              value);
void                tracker_property_set_is_new              (TrackerProperty      *property,
                                                              gboolean              value);
void                tracker_property_set_is_new_domain_index (TrackerProperty      *property,
                                                              TrackerClass         *class,
                                                              gboolean              value);
void                tracker_property_set_writeback           (TrackerProperty      *property,
                                                               gboolean              value);
void                tracker_property_set_default_value       (TrackerProperty      *property,
                                                              const gchar          *value);
void                tracker_property_set_db_schema_changed   (TrackerProperty      *property,
                                                              gboolean              value);
void                tracker_property_set_cardinality_changed (TrackerProperty      *property,
                                                              gboolean              value);
void                tracker_property_set_is_inverse_functional_property
                                                             (TrackerProperty      *property,
                                                              gboolean              value);
void                tracker_property_add_super_property      (TrackerProperty      *property,
                                                              TrackerProperty      *value);
void                tracker_property_del_super_property      (TrackerProperty      *property,
                                                              TrackerProperty      *value);
TrackerProperty   **tracker_property_get_last_super_properties
                                                             (TrackerProperty      *property);
void                tracker_property_reset_super_properties  (TrackerProperty      *property);

void                tracker_property_set_ontologies          (TrackerProperty      *property,
                                                              TrackerOntologies    *ontologies);

G_END_DECLS

#endif /* __LIBTRACKER_DATA_PROPERTY_H__ */

