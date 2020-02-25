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

#ifndef __TTL_XML_H__
#define __TTL_XML_H__

#include <gio/gio.h>
#include "ttl_model.h"
#include <stdio.h>

G_BEGIN_DECLS

void ttl_xml_print (OntologyDescription *description,
                    Ontology            *ontology,
                    GFile               *file,
                    const gchar         *description_dir);

G_END_DECLS

#endif
