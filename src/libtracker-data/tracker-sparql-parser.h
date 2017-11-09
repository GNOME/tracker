/*
 * Copyright (C) 2008-2010, Nokia
 * Copyright (C) 2018, Red Hat Inc.
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
#ifndef __TRACKER_SPARQL_PARSER_H__
#define __TRACKER_SPARQL_PARSER_H__

#include <glib.h>

typedef struct _TrackerParserNode TrackerParserNode;
typedef struct _TrackerGrammarRule TrackerGrammarRule;
typedef struct _TrackerNodeTree TrackerNodeTree;

TrackerNodeTree * tracker_sparql_parse_query  (const gchar  *query,
                                               gssize        len,
                                               gsize        *len_out,
                                               GError      **error);
TrackerNodeTree * tracker_sparql_parse_update (const gchar  *query,
                                               gssize        len,
                                               gsize        *len_out,
                                               GError      **error);

void   tracker_node_tree_free     (TrackerNodeTree *tree);
TrackerParserNode * tracker_node_tree_get_root (TrackerNodeTree *tree);

TrackerParserNode * tracker_sparql_parser_tree_find_first (TrackerParserNode *node,
                                                           gboolean           leaves_only);
TrackerParserNode * tracker_sparql_parser_tree_find_next  (TrackerParserNode *node,
                                                           gboolean           leaves_only);

const TrackerGrammarRule * tracker_parser_node_get_rule (TrackerParserNode *node);

gboolean tracker_parser_node_get_extents (TrackerParserNode *node,
                                          gssize            *start,
                                          gssize            *end);

#endif /* __TRACKER_SPARQL_PARSER_H__ */
