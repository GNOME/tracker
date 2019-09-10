/*
 * Copyright (C) 2006, Jamie McCracken <jamiemcc@gnome.org>
 * Copyright (C) 2008-2010, Nokia <ivan.frade@nokia.com>
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

#ifndef __LIBTRACKER_COMMON_DATE_TIME_H__
#define __LIBTRACKER_COMMON_DATE_TIME_H__

#include <glib.h>
#include <glib-object.h>

G_BEGIN_DECLS

#if !defined (__LIBTRACKER_COMMON_INSIDE__) && !defined (TRACKER_COMPILATION)
#error "only <libtracker-common/tracker-common.h> must be included directly."
#endif

typedef enum  {
	TRACKER_DATE_ERROR_OFFSET,
	TRACKER_DATE_ERROR_INVALID_ISO8601,
	TRACKER_DATE_ERROR_EMPTY
} TrackerDateError;

#define TRACKER_TYPE_DATE_TIME                 (tracker_date_time_get_type ())
#define TRACKER_DATE_ERROR                     tracker_date_error_quark ()

GQuark   tracker_date_error_quark              (void);

GType    tracker_date_time_get_type            (void);

void     tracker_date_time_set                 (GValue       *value,
                                                gdouble       time,
                                                gint          offset);
void     tracker_date_time_set_from_string     (GValue       *value,
                                                const gchar  *date_time_string,
                                                GError      **error);
gdouble  tracker_date_time_get_time            (const GValue *value);
gint     tracker_date_time_get_offset          (const GValue *value);
gint     tracker_date_time_get_local_date      (const GValue *value);
gint     tracker_date_time_get_local_time      (const GValue *value);

gdouble  tracker_string_to_date                (const gchar  *date_string,
                                                gint         *offset,
                                                GError      **error);
gchar *  tracker_date_to_string                (gdouble       date_time,
                                                gint          offset);

G_END_DECLS

#endif /* __LIBTRACKER_COMMON_DATE_TIME_H__ */
