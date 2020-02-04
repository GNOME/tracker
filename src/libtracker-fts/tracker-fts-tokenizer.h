/*
 * Copyright (C) 2011 Nokia <ivan.frade@nokia.com>
 *
 * Author: Carlos Garnacho <carlos@lanedo.com>
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

#include <sqlite3.h>
#include <glib.h>
#include <libtracker-data/tracker-db-manager.h>

#ifndef __TRACKER_FTS_TOKENIZER_H__
#define __TRACKER_FTS_TOKENIZER_H__

gboolean tracker_tokenizer_initialize (sqlite3                *db,
                                       TrackerDBInterface     *interface,
                                       TrackerDBManagerFlags   flags,
                                       const gchar           **property_names);

#endif /* __TRACKER_FTS_TOKENIZER_H__ */
