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

#ifndef __LIBTRACKER_COMMON_UTILS_H__
#define __LIBTRACKER_COMMON_UTILS_H__

#include <glib.h>

G_BEGIN_DECLS

#if !defined (__LIBTRACKER_COMMON_INSIDE__) && !defined (TRACKER_COMPILATION)
#error "only <libtracker-common/tracker-common.h> must be included directly."
#endif

gboolean tracker_is_empty_string            (const char   *str);
gboolean tracker_is_blank_string            (const char   *str);
guint    tracker_seconds_estimate           (gdouble       seconds_elapsed,
                                             guint         items_done,
                                             guint         items_remaining);
gchar *  tracker_seconds_estimate_to_string (gdouble       seconds_elapsed,
                                             gboolean      short_string,
                                             guint         items_done,
                                             guint         items_remaining);
gchar *  tracker_seconds_to_string          (gdouble       seconds,
                                             gboolean      short_string);
gchar *  tracker_strhex                     (const guint8 *data,
                                             gsize         size,
                                             gchar         delimiter);
gchar *  tracker_utf8_truncate              (const gchar  *str,
                                             gsize         max_size);
gchar *  tracker_unescape_unichars          (const gchar  *str,
					     gssize        len);

G_END_DECLS

#endif /* __LIBTRACKER_COMMON_UTILS_H__ */
