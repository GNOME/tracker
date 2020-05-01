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

#ifndef __LIBTRACKER_SPARQL_ONTOLOGIES_H__
#define __LIBTRACKER_SPARQL_ONTOLOGIES_H__

G_BEGIN_DECLS

#if !defined (__LIBTRACKER_SPARQL_INSIDE__) && !defined (TRACKER_COMPILATION)
#error "only <libtracker-sparql/tracker-sparql.h> must be included directly."
#endif

/* Core: resources, data types */
#define TRACKER_PREFIX_RDF      "http://www.w3.org/1999/02/22-rdf-syntax-ns#"
#define TRACKER_PREFIX_RDFS     "http://www.w3.org/2000/01/rdf-schema#"
#define TRACKER_PREFIX_XSD      "http://www.w3.org/2001/XMLSchema#"
#define TRACKER_PREFIX_TRACKER  "http://tracker.api.gnome.org/ontology/v3/tracker#"
#define TRACKER_PREFIX_DC       "http://purl.org/dc/elements/1.1/"

/* Our Nepomuk selection */
#define TRACKER_PREFIX_NRL      "http://tracker.api.gnome.org/ontology/v3/nrl#"
#define TRACKER_PREFIX_NIE      "http://tracker.api.gnome.org/ontology/v3/nie#"
#define TRACKER_PREFIX_NCO      "http://tracker.api.gnome.org/ontology/v3/nco#"
#define TRACKER_PREFIX_NAO      "http://tracker.api.gnome.org/ontology/v3/nao#"
#define TRACKER_PREFIX_NFO      "http://tracker.api.gnome.org/ontology/v3/nfo#"
#define TRACKER_PREFIX_NMM      "http://tracker.api.gnome.org/ontology/v3/nmm#"

/* Addtional ontologies used by tracker-miners */
#define TRACKER_PREFIX_MFO      "http://tracker.api.gnome.org/ontology/v3/mfo#"
#define TRACKER_PREFIX_SLO      "http://tracker.api.gnome.org/ontology/v3/slo#"
#define TRACKER_PREFIX_OSINFO   "http://tracker.api.gnome.org/ontology/v3/osinfo#"

G_END_DECLS

#endif /* __LIBTRACKER_SPARQL_ONTOLOGY_H__ */
