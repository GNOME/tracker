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
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301  USA
 */

#ifndef __LIBTRACKER_DATA_CLASS_H__
#define __LIBTRACKER_DATA_CLASS_H__

#include <glib-object.h>

G_BEGIN_DECLS

#if !defined (__LIBTRACKER_DATA_INSIDE__) && !defined (TRACKER_COMPILATION)
#error "only <libtracker-data/tracker-data.h> must be included directly."
#endif

#define TRACKER_TYPE_CLASS         (tracker_class_get_type ())
#define TRACKER_CLASS(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), TRACKER_TYPE_CLASS, TrackerClass))
#define TRACKER_CLASS_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST ((k), TRACKER_TYPE_CLASS, TrackerClassClass))
#define TRACKER_IS_CLASS(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), TRACKER_TYPE_CLASS))
#define TRACKER_IS_CLASS_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), TRACKER_TYPE_CLASS))
#define TRACKER_CLASS_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), TRACKER_TYPE_CLASS, TrackerClassClass))

typedef struct _TrackerOntologies TrackerOntologies;
typedef struct _TrackerProperty TrackerProperty;
typedef struct _TrackerClass TrackerClass;
typedef struct _TrackerClassClass TrackerClassClass;
typedef struct _TrackerClassPrivate TrackerClassPrivate;

struct _TrackerClass {
	GObject parent;
	TrackerClassPrivate *priv;
};

struct _TrackerClassClass {
	GObjectClass parent_class;
};

GType             tracker_class_get_type               (void) G_GNUC_CONST;
TrackerClass *    tracker_class_new                    (gboolean             use_gvdb);
const gchar *     tracker_class_get_uri                (TrackerClass        *service);
const gchar *     tracker_class_get_name               (TrackerClass        *service);
gint              tracker_class_get_count              (TrackerClass        *service);
gint              tracker_class_get_id                 (TrackerClass        *service);
gboolean          tracker_class_get_is_new             (TrackerClass        *service);
gboolean          tracker_class_get_db_schema_changed  (TrackerClass        *service);
gboolean          tracker_class_get_notify             (TrackerClass        *service);

TrackerClass    **tracker_class_get_super_classes      (TrackerClass        *service);
TrackerProperty **tracker_class_get_domain_indexes     (TrackerClass        *service);
TrackerProperty **tracker_class_get_last_domain_indexes(TrackerClass        *service);
TrackerClass    **tracker_class_get_last_super_classes (TrackerClass        *service);

void              tracker_class_set_uri                (TrackerClass        *service,
                                                        const gchar         *value);
void              tracker_class_set_count              (TrackerClass        *service,
                                                        gint                 value);
void              tracker_class_add_super_class        (TrackerClass        *service,
                                                        TrackerClass        *value);
void              tracker_class_add_domain_index       (TrackerClass        *service,
                                                        TrackerProperty     *value);
void              tracker_class_del_domain_index       (TrackerClass        *service,
                                                        TrackerProperty     *value);
void              tracker_class_reset_domain_indexes   (TrackerClass        *service);
void              tracker_class_reset_super_classes   (TrackerClass        *service);
void              tracker_class_set_id                 (TrackerClass        *service,
                                                        gint                 id);
void              tracker_class_set_is_new             (TrackerClass        *service,
                                                        gboolean             value);
void              tracker_class_set_db_schema_changed  (TrackerClass        *service,
                                                        gboolean             value);
void              tracker_class_set_notify             (TrackerClass        *service,
                                                        gboolean             value);

void              tracker_class_set_ontologies         (TrackerClass        *class,
                                                        TrackerOntologies   *ontologies);

G_END_DECLS

#endif /* __LIBTRACKER_DATA_CLASS_H__ */

