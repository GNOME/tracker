/*
 * Copyright (C) 2009, Nokia <ivan.frade@nokia.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

#ifndef __TTL_LOADER_H__
#define __TTL_LOADER_H__

#include <glib.h>
#include <gio/gio.h>
#include "ttl_model.h"

G_BEGIN_DECLS

Ontology            * ttl_loader_new_ontology     (void);

gboolean              ttl_loader_load_ontology    (Ontology    *ontology,
                                                   GFile       *filename,
                                                   GError     **error);
OntologyDescription * ttl_loader_load_description (GFile       *filename);

void                  ttl_loader_load_prefix_from_description (Ontology            *ontology,
                                                               OntologyDescription *description);

void                  ttl_loader_free_ontology    (Ontology *ontology);
void                  ttl_loader_free_description (OntologyDescription *desc);


G_END_DECLS

#endif /* __TTL_LOADER_H__ */
