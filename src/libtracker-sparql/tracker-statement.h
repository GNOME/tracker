/*
 * Copyright (C) 2018, Red Hat Ltd.
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
#ifndef __TRACKER_SPARQL_STATEMENT_H__
#define __TRACKER_SPARQL_STATEMENT_H__

#if !defined (__LIBTRACKER_SPARQL_INSIDE__) && !defined (TRACKER_COMPILATION)
#error "only <libtracker-sparql/tracker-sparql.h> must be included directly."
#endif

#include <gio/gio.h>
#include <libtracker-sparql/tracker-version.h>

#define TRACKER_TYPE_SPARQL_STATEMENT tracker_sparql_statement_get_type ()
#define TRACKER_SPARQL_TYPE_STATEMENT TRACKER_TYPE_SPARQL_STATEMENT
TRACKER_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (TrackerSparqlStatement,
                          tracker_sparql_statement,
                          TRACKER, SPARQL_STATEMENT,
                          GObject)

#include "tracker-connection.h"
#include "tracker-cursor.h"

struct _TrackerSparqlStatementClass
{
	GObjectClass parent_class;

        void (* bind_int) (TrackerSparqlStatement *stmt,
                           const gchar            *name,
                           gint64                  value);
        void (* bind_boolean) (TrackerSparqlStatement *stmt,
                               const gchar            *name,
                               gboolean                value);
        void (* bind_string) (TrackerSparqlStatement *stmt,
                              const gchar            *name,
                              const gchar            *value);
        void (* bind_double) (TrackerSparqlStatement *stmt,
                              const gchar            *name,
                              gdouble                 value);

        TrackerSparqlCursor * (* execute) (TrackerSparqlStatement  *stmt,
                                           GCancellable            *cancellable,
                                           GError                 **error);
        void (* execute_async) (TrackerSparqlStatement *stmt,
                                GCancellable           *cancellable,
                                GAsyncReadyCallback     callback,
                                gpointer                user_data);
        TrackerSparqlCursor * (* execute_finish) (TrackerSparqlStatement  *stmt,
                                                  GAsyncResult            *res,
                                                  GError                 **error);
	void (* clear_bindings) (TrackerSparqlStatement *stmt);
};

TRACKER_AVAILABLE_IN_ALL
TrackerSparqlConnection * tracker_sparql_statement_get_connection (TrackerSparqlStatement *stmt);

TRACKER_AVAILABLE_IN_ALL
const gchar * tracker_sparql_statement_get_sparql (TrackerSparqlStatement *stmt);

TRACKER_AVAILABLE_IN_ALL
void tracker_sparql_statement_bind_boolean (TrackerSparqlStatement *stmt,
                                            const gchar            *name,
                                            gboolean                value);

TRACKER_AVAILABLE_IN_ALL
void tracker_sparql_statement_bind_int (TrackerSparqlStatement *stmt,
                                        const gchar            *name,
                                        gint64                  value);

TRACKER_AVAILABLE_IN_ALL
void tracker_sparql_statement_bind_double (TrackerSparqlStatement *stmt,
                                           const gchar            *name,
                                           gdouble                 value);

TRACKER_AVAILABLE_IN_ALL
void tracker_sparql_statement_bind_string (TrackerSparqlStatement *stmt,
                                           const gchar            *name,
                                           const gchar            *value);

TRACKER_AVAILABLE_IN_ALL
TrackerSparqlCursor * tracker_sparql_statement_execute (TrackerSparqlStatement  *stmt,
                                                        GCancellable            *cancellable,
                                                        GError                 **error);

TRACKER_AVAILABLE_IN_ALL
void tracker_sparql_statement_execute_async (TrackerSparqlStatement *stmt,
                                             GCancellable           *cancellable,
                                             GAsyncReadyCallback     callback,
                                             gpointer                user_data);

TRACKER_AVAILABLE_IN_ALL
TrackerSparqlCursor * tracker_sparql_statement_execute_finish (TrackerSparqlStatement  *stmt,
                                                               GAsyncResult            *res,
                                                               GError                 **error);

TRACKER_AVAILABLE_IN_ALL
void tracker_sparql_statement_clear_bindings (TrackerSparqlStatement *stmt);

#endif /* __TRACKER_SPARQL_STATEMENT_H__ */
