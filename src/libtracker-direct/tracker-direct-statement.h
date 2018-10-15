/*
 * Copyright (C) 2018, Red Hat, Inc.
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

#ifndef __TRACKER_DIRECT_STATEMENT_H__
#define __TRACKER_DIRECT_STATEMENT_H__

#include "tracker-direct.h"
#include <libtracker-sparql/tracker-sparql.h>

#define TRACKER_TYPE_DIRECT_STATEMENT         (tracker_direct_statement_get_type ())
#define TRACKER_DIRECT_STATEMENT(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), TRACKER_TYPE_DIRECT_STATEMENT, TrackerDirectStatement))
#define TRACKER_DIRECT_STATEMENT_CLASS(c)     (G_TYPE_CHECK_CLASS_CAST ((c), TRACKER_TYPE_DIRECT_STATEMENT, TrackerDirectStatementClass))
#define TRACKER_IS_DIRECT_STATEMENT(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), TRACKER_TYPE_DIRECT_STATEMENT))
#define TRACKER_IS_DIRECT_STATEMENT_CLASS(c)  (G_TYPE_CHECK_CLASS_TYPE ((c),  TRACKER_TYPE_DIRECT_STATEMENT))
#define TRACKER_DIRECT_STATEMENT_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), TRACKER_TYPE_DIRECT_STATEMENT, TrackerDirectStatementClass))

typedef struct _TrackerDirectStatement TrackerDirectStatement;
typedef struct _TrackerDirectStatementClass TrackerDirectStatementClass;

struct _TrackerDirectStatementClass
{
	TrackerSparqlStatementClass parent_class;
};

struct _TrackerDirectStatement
{
	TrackerSparqlStatement parent_instance;
};

GType tracker_direct_statement_get_type (void) G_GNUC_CONST;

TrackerDirectStatement * tracker_direct_statement_new (TrackerSparqlConnection  *conn,
                                                       const gchar              *sparql,
                                                       GError                  **error);

#endif /* __TRACKER_DIRECT_STATEMENT_H__ */
