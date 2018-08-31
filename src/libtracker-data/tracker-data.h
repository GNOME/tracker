/*
 * Copyright (C) 2009, Nokia <ivan.frade@nokia.com>
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

#ifndef __LIBTRACKER_DATA_H__
#define __LIBTRACKER_DATA_H__

#if !defined (TRACKER_ENABLE_INTERNALS) && !defined (TRACKER_COMPILATION)
#error "TRACKER_ENABLE_INTERNALS not defined, this must be defined to use tracker's internal functions"
#endif

#define __LIBTRACKER_DATA_INSIDE__

#include "tracker-class.h"
#include "tracker-data-backup.h"
#include "tracker-data-manager.h"
#include "tracker-data-query.h"
#include "tracker-data-update.h"
#include "tracker-db-config.h"
#include "tracker-db-interface.h"
#include "tracker-db-interface-sqlite.h"
#include "tracker-db-journal.h"
#include "tracker-db-manager.h"
#include "tracker-namespace.h"
#include "tracker-ontology.h"
#include "tracker-ontologies.h"
#include "tracker-property.h"
#include "tracker-sparql-query.h"
#include "tracker-sparql.h"
#include "tracker-uuid.h"

#undef __LIBTRACKER_DATA_INSIDE__

#endif /* __LIBTRACKER_DATA_H__ */
