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

#ifndef __LIBTRACKER_COMMON_PARSER_H__
#define __LIBTRACKER_COMMON_PARSER_H__

#include <glib.h>

#include <libtracker-common/tracker-language.h>

/* This version MUST be bumped on any change to any tracker-parser-*
 * file. Given the parser output gets both stored in disk and performed
 * at runtime, the former must be rebuilt for those to match perfectly
 * to avoid returning meaningless results on FTS searches.
 */
#define TRACKER_PARSER_VERSION 4

G_BEGIN_DECLS

typedef struct TrackerParser TrackerParser;

TrackerParser *tracker_parser_new             (TrackerLanguage *language);

void           tracker_parser_reset           (TrackerParser   *parser,
                                               const gchar     *txt,
                                               gint             txt_size,
                                               guint            max_word_length,
                                               gboolean         enable_stemmer,
                                               gboolean         enable_unaccent,
                                               gboolean         ignore_stop_words,
                                               gboolean         ignore_reserved_words,
                                               gboolean         ignore_numbers);

const gchar *  tracker_parser_next            (TrackerParser   *parser,
                                               gint            *position,
                                               gint            *byte_offset_start,
                                               gint            *byte_offset_end,
                                               gboolean        *stop_word,
                                               gint            *word_length);

void           tracker_parser_free            (TrackerParser   *parser);

/* Other helper methods */

gboolean       tracker_parser_unaccent_nfkd_string (gpointer  str,
                                                    gsize    *str_length);

G_END_DECLS

#endif /* __LIBTRACKER_COMMON_PARSER_H__ */
