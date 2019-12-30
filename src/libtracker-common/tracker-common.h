/*
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

#ifndef __LIBTRACKER_COMMON_H__
#define __LIBTRACKER_COMMON_H__

#if !defined (__LIBTRACKER_COMMON_INSIDE__) && !defined (TRACKER_COMPILATION)
#error "only <libtracker-common/tracker-common.h> must be included directly."
#endif

#include <glib.h>

#define __LIBTRACKER_COMMON_INSIDE__

#include "tracker-date-time.h"
#include "tracker-file-utils.h"
#include "tracker-language.h"
#include "tracker-log.h"
#include "tracker-parser.h"
#include "tracker-type-utils.h"
#include "tracker-utils.h"
#include "tracker-locale.h"
#include "tracker-enum-types.h"

#undef __LIBTRACKER_COMMON_INSIDE__

#endif /* __LIBTRACKER_COMMON_H__ */
