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

#ifndef __LIBTRACKER_SPARQL_VERSION_H__
#define __LIBTRACKER_SPARQL_VERSION_H__

#include <glib.h>

G_BEGIN_DECLS

#if !defined (__LIBTRACKER_SPARQL_INSIDE__) && !defined (TRACKER_COMPILATION)
#error "only <libtracker-sparql/tracker-sparql.h> must be included directly."
#endif

#ifndef _TRACKER_EXTERN
#define _TRACKER_EXTERN __attribute__((visibility("default"))) extern
#endif

#define TRACKER_AVAILABLE_IN_ALL _TRACKER_EXTERN

GLIB_VAR const guint tracker_major_version;
GLIB_VAR const guint tracker_minor_version;
GLIB_VAR const guint tracker_micro_version;
GLIB_VAR const guint tracker_interface_age;
GLIB_VAR const guint tracker_binary_age;

/**
 * TRACKER_CHECK_VERSION:
 * @major: the required major version.
 * @minor: the required minor version.
 * @micro: the required micro version.
 *
 * This macro essentially does the same thing as
 * tracker_check_version() but as a pre-processor operation rather
 * than a run-time operation. It will evaluate true or false based the
 * version passed in and the version available.
 *
 * <example>
 * <title>Simple version check example</title>
 * An example of how to make sure you have the version of Tracker
 * installed to run your code.
 * <programlisting>
 * if (!TRACKER_CHECK_VERSION (0, 10, 7)) {
 *         g_error ("Tracker version 0.10.7 or above is needed");
 * }
 * </programlisting>
 * </example>
 *
 * Since: 0.10
 **/
#define TRACKER_CHECK_VERSION(major,minor,micro)    \
    (TRACKER_MAJOR_VERSION > (major) || \
     (TRACKER_MAJOR_VERSION == (major) && TRACKER_MINOR_VERSION > (minor)) || \
     (TRACKER_MAJOR_VERSION == (major) && TRACKER_MINOR_VERSION == (minor) && \
      TRACKER_MICRO_VERSION >= (micro)))

TRACKER_AVAILABLE_IN_ALL
const gchar * tracker_check_version (guint required_major,
                                     guint required_minor,
                                     guint required_micro);

G_END_DECLS

#endif /* __LIBTRACKER_SPARQL_VERSION_H__ */
