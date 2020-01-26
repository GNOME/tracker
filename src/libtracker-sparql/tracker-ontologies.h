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
#define TRACKER_PREFIX_TRACKER  "http://www.tracker-project.org/ontologies/tracker#"
#define TRACKER_PREFIX_DC       "http://purl.org/dc/elements/1.1/"

/* Our Nepomuk selection */
#define TRACKER_PREFIX_NRL      "http://www.semanticdesktop.org/ontologies/2007/08/15/nrl#"
#define TRACKER_PREFIX_NMO      "http://www.semanticdesktop.org/ontologies/2007/03/22/nmo#"
#define TRACKER_PREFIX_NIE      "http://www.semanticdesktop.org/ontologies/2007/01/19/nie#"
#define TRACKER_PREFIX_NCO      "http://www.semanticdesktop.org/ontologies/2007/03/22/nco#"
#define TRACKER_PREFIX_NAO      "http://www.semanticdesktop.org/ontologies/2007/08/15/nao#"
#define TRACKER_PREFIX_NID3     "http://www.semanticdesktop.org/ontologies/2007/05/10/nid3#"
#define TRACKER_PREFIX_NFO      "http://www.semanticdesktop.org/ontologies/2007/03/22/nfo#"
#define TRACKER_PREFIX_OSINFO   "http://www.tracker-project.org/ontologies/osinfo#"

/* Temporary */
#define TRACKER_PREFIX_SLO      "http://www.tracker-project.org/temp/slo#"
#define TRACKER_PREFIX_NMM      "http://www.tracker-project.org/temp/nmm#"
#define TRACKER_PREFIX_MLO      "http://www.tracker-project.org/temp/mlo#"
#define TRACKER_PREFIX_MFO      "http://www.tracker-project.org/temp/mfo#"

G_END_DECLS

#endif /* __LIBTRACKER_SPARQL_ONTOLOGY_H__ */
