/*
 * Copyright (C) 2020, Sam Thursfield <sam@afuera.me.uk>
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

#ifndef __TRACKER_DEBUG_H__
#define __TRACKER_DEBUG_H__

#if !defined (__LIBTRACKER_COMMON_INSIDE__) && !defined (TRACKER_COMPILATION)
#error "only <libtracker-common/tracker-common.h> must be included directly."
#endif

#include <glib.h>

G_BEGIN_DECLS

typedef enum {
  TRACKER_DEBUG_COLLATION        = 1 <<  1,
  TRACKER_DEBUG_ONTOLOGY_CHANGES = 1 <<  2,
  TRACKER_DEBUG_SPARQL           = 1 <<  3,
  TRACKER_DEBUG_SQLITE           = 1 <<  4,
  TRACKER_DEBUG_SQL_STATEMENTS   = 1 <<  5,
} TrackerDebugFlag;

#ifdef G_ENABLE_DEBUG

#define TRACKER_DEBUG_CHECK(type) G_UNLIKELY (tracker_get_debug_flags () & TRACKER_DEBUG_##type)

#define TRACKER_NOTE(type,action)                G_STMT_START {     \
    if (TRACKER_DEBUG_CHECK (type))                                 \
       { action; };                              } G_STMT_END

#else /* !G_ENABLE_DEBUG */

#define TRACKER_DEBUG_CHECK(type) 0
#define TRACKER_NOTE(type, action)

#endif /* G_ENABLE_DEBUG */

guint tracker_get_debug_flags (void);

G_END_DECLS

#endif /* __TRACKER_DEBUG_H__ */
