/*
 * Copyright (C) 2010, Nokia <ivan.frade@nokia.com>
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
#ifndef __TRACKER_SPARQL_CONNECTION_H__
#define __TRACKER_SPARQL_CONNECTION_H__

#if !defined (__LIBTRACKER_SPARQL_INSIDE__) && !defined (TRACKER_COMPILATION)
#error "only <libtracker-sparql/tracker-sparql.h> must be included directly."
#endif

#include <libtracker-sparql/tracker-notifier.h>
#include <libtracker-sparql/tracker-version.h>
#include <gio/gio.h>

typedef enum {
	TRACKER_SPARQL_CONNECTION_FLAGS_NONE     = 0,
	TRACKER_SPARQL_CONNECTION_FLAGS_READONLY = 1 << 0,
} TrackerSparqlConnectionFlags;

/**
 * TrackerSparqlConnection:
 *
 * The <structname>TrackerSparqlConnection</structname> object represents a
 * SPARQL connection.
 */
#define TRACKER_TYPE_SPARQL_CONNECTION tracker_sparql_connection_get_type ()
#define TRACKER_SPARQL_TYPE_CONNECTION TRACKER_TYPE_SPARQL_CONNECTION

TRACKER_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (TrackerSparqlConnection,
                          tracker_sparql_connection,
                          TRACKER, SPARQL_CONNECTION,
                          GObject)

#include "tracker-cursor.h"
#include "tracker-statement.h"
#include "tracker-namespace-manager.h"

/**
 * TrackerSparqlError:
 * @TRACKER_SPARQL_ERROR_PARSE: Error parsing the SPARQL string.
 * @TRACKER_SPARQL_ERROR_UNKNOWN_CLASS: Unknown class.
 * @TRACKER_SPARQL_ERROR_UNKNOWN_PROPERTY: Unknown property.
 * @TRACKER_SPARQL_ERROR_TYPE: Wrong type.
 * @TRACKER_SPARQL_ERROR_CONSTRAINT: Subject is not in the domain of a property or
 *                             trying to set multiple values for a single valued
 *                             property.
 * @TRACKER_SPARQL_ERROR_NO_SPACE: There was no disk space available to perform the request.
 * @TRACKER_SPARQL_ERROR_INTERNAL: Internal error.
 * @TRACKER_SPARQL_ERROR_UNSUPPORTED: Unsupported feature or method.
 * @TRACKER_SPARQL_ERROR_UNKNOWN_GRAPH: Unknown graph.
 *
 * Error domain for Tracker Sparql. Errors in this domain will be from the
 * #TrackerSparqlError enumeration. See #GError for more information on error
 * domains.
 */
typedef enum {
	TRACKER_SPARQL_ERROR_PARSE,
	TRACKER_SPARQL_ERROR_UNKNOWN_CLASS,
	TRACKER_SPARQL_ERROR_UNKNOWN_PROPERTY,
	TRACKER_SPARQL_ERROR_TYPE,
	TRACKER_SPARQL_ERROR_CONSTRAINT,
	TRACKER_SPARQL_ERROR_NO_SPACE,
	TRACKER_SPARQL_ERROR_INTERNAL,
	TRACKER_SPARQL_ERROR_UNSUPPORTED,
	TRACKER_SPARQL_ERROR_UNKNOWN_GRAPH
} TrackerSparqlError;

#define TRACKER_SPARQL_ERROR tracker_sparql_error_quark ()

struct _TrackerSparqlConnectionClass
{
	GObjectClass parent_class;

        TrackerSparqlCursor * (* query) (TrackerSparqlConnection  *connection,
                                         const gchar              *sparql,
                                         GCancellable             *cancellable,
                                         GError                  **error);
	void (* query_async) (TrackerSparqlConnection *connection,
	                      const gchar             *sparql,
	                      GCancellable            *cancellable,
	                      GAsyncReadyCallback      callback,
	                      gpointer                 user_data);
        TrackerSparqlCursor * (* query_finish) (TrackerSparqlConnection  *connection,
                                                GAsyncResult             *res,
                                                GError                  **error);
        void (* update) (TrackerSparqlConnection  *connection,
                         const gchar              *sparql,
                         gint                      priority,
                         GCancellable             *cancellable,
                         GError                  **error);
        void (* update_async) (TrackerSparqlConnection *connection,
                               const gchar             *sparql,
                               gint                     priority,
                               GCancellable            *cancellable,
                               GAsyncReadyCallback      callback,
                               gpointer                 user_data);
        void (* update_finish) (TrackerSparqlConnection  *connection,
                                GAsyncResult             *res,
                                GError                  **error);
        void (* update_array_async) (TrackerSparqlConnection  *connection,
                                     gchar                   **sparql,
                                     gint                      sparql_length,
                                     gint                      priority,
                                     GCancellable             *cancellable,
                                     GAsyncReadyCallback       callback,
                                     gpointer                  user_data);
        gboolean (* update_array_finish) (TrackerSparqlConnection  *connection,
                                          GAsyncResult             *res,
                                          GError                  **error);
        GVariant* (* update_blank) (TrackerSparqlConnection  *connection,
                                    const gchar              *sparql,
                                    gint                      priority,
                                    GCancellable             *cancellable,
                                    GError                  **error);
        void (* update_blank_async) (TrackerSparqlConnection *connection,
                                     const gchar             *sparql,
                                     gint                     priority,
                                     GCancellable            *cancellable,
                                     GAsyncReadyCallback      callback,
                                     gpointer                 user_data);
        GVariant* (* update_blank_finish) (TrackerSparqlConnection  *connection,
                                           GAsyncResult             *res,
                                           GError                  **error);
        TrackerNamespaceManager * (* get_namespace_manager) (TrackerSparqlConnection *connection);
        TrackerSparqlStatement * (* query_statement) (TrackerSparqlConnection  *connection,
                                                      const gchar              *sparql,
                                                      GCancellable             *cancellable,
                                                      GError                  **error);
	TrackerNotifier * (* create_notifier) (TrackerSparqlConnection *connection,
	                                       TrackerNotifierFlags     flags);

	void (* close) (TrackerSparqlConnection *connection);
};

TRACKER_AVAILABLE_IN_ALL
GQuark tracker_sparql_error_quark (void);

TRACKER_AVAILABLE_IN_ALL
TrackerSparqlConnection * tracker_sparql_connection_new (TrackerSparqlConnectionFlags   flags,
                                                         GFile                         *store,
                                                         GFile                         *ontology,
                                                         GCancellable                  *cancellable,
                                                         GError                       **error);
TRACKER_AVAILABLE_IN_ALL
void tracker_sparql_connection_new_async (TrackerSparqlConnectionFlags   flags,
                                          GFile                         *store,
                                          GFile                         *ontology,
                                          GCancellable                  *cancellable,
                                          GAsyncReadyCallback            callback,
                                          gpointer                       user_data);
TRACKER_AVAILABLE_IN_ALL
TrackerSparqlConnection * tracker_sparql_connection_new_finish (GAsyncResult  *result,
                                                                GError       **error);

TRACKER_AVAILABLE_IN_ALL
TrackerSparqlConnection * tracker_sparql_connection_bus_new (const gchar      *service_name,
                                                             const gchar      *object_path,
                                                             GDBusConnection  *dbus_connection,
                                                             GError          **error);
TRACKER_AVAILABLE_IN_ALL
TrackerSparqlConnection * tracker_sparql_connection_remote_new (const gchar *uri_base);

TRACKER_AVAILABLE_IN_ALL
TrackerSparqlCursor * tracker_sparql_connection_query (TrackerSparqlConnection  *connection,
                                                       const gchar              *sparql,
                                                       GCancellable             *cancellable,
                                                       GError                  **error);

TRACKER_AVAILABLE_IN_ALL
void tracker_sparql_connection_query_async (TrackerSparqlConnection *connection,
                                            const gchar             *sparql,
                                            GCancellable            *cancellable,
                                            GAsyncReadyCallback      callback,
                                            gpointer                 user_data);
TRACKER_AVAILABLE_IN_ALL
TrackerSparqlCursor * tracker_sparql_connection_query_finish (TrackerSparqlConnection  *connection,
                                                              GAsyncResult             *res,
                                                              GError                  **error);

TRACKER_AVAILABLE_IN_ALL
void tracker_sparql_connection_update (TrackerSparqlConnection  *connection,
                                       const gchar              *sparql,
                                       gint                      priority,
                                       GCancellable             *cancellable,
                                       GError                  **error);
TRACKER_AVAILABLE_IN_ALL
void tracker_sparql_connection_update_async (TrackerSparqlConnection *connection,
                                             const gchar             *sparql,
                                             gint                     priority,
                                             GCancellable            *cancellable,
                                             GAsyncReadyCallback      callback,
                                             gpointer                 user_data);
TRACKER_AVAILABLE_IN_ALL
void tracker_sparql_connection_update_finish (TrackerSparqlConnection  *connection,
                                              GAsyncResult             *res,
                                              GError                  **error);
TRACKER_AVAILABLE_IN_ALL
void tracker_sparql_connection_update_array_async (TrackerSparqlConnection  *connection,
                                                   gchar                   **sparql,
                                                   gint                      sparql_length,
                                                   gint                      priority,
                                                   GCancellable             *cancellable,
                                                   GAsyncReadyCallback       callback,
                                                   gpointer                  user_data);
TRACKER_AVAILABLE_IN_ALL
gboolean tracker_sparql_connection_update_array_finish (TrackerSparqlConnection  *connection,
                                                        GAsyncResult             *res,
                                                        GError                  **error);
TRACKER_AVAILABLE_IN_ALL
GVariant * tracker_sparql_connection_update_blank (TrackerSparqlConnection  *connection,
                                                   const gchar              *sparql,
                                                   gint                      priority,
                                                   GCancellable             *cancellable,
                                                   GError                  **error);
TRACKER_AVAILABLE_IN_ALL
void tracker_sparql_connection_update_blank_async (TrackerSparqlConnection *connection,
                                                   const gchar             *sparql,
                                                   gint                     priority,
                                                   GCancellable            *cancellable,
                                                   GAsyncReadyCallback      callback,
                                                   gpointer                 user_data);
TRACKER_AVAILABLE_IN_ALL
GVariant * tracker_sparql_connection_update_blank_finish (TrackerSparqlConnection  *connection,
                                                          GAsyncResult             *res,
                                                          GError                  **error);

TRACKER_AVAILABLE_IN_ALL
TrackerNamespaceManager * tracker_sparql_connection_get_namespace_manager (TrackerSparqlConnection *connection);

TRACKER_AVAILABLE_IN_ALL
TrackerSparqlStatement * tracker_sparql_connection_query_statement (TrackerSparqlConnection  *connection,
                                                                    const gchar              *sparql,
                                                                    GCancellable             *cancellable,
                                                                    GError                  **error);
TRACKER_AVAILABLE_IN_ALL
TrackerNotifier * tracker_sparql_connection_create_notifier (TrackerSparqlConnection *connection,
                                                             TrackerNotifierFlags     flags);

TRACKER_AVAILABLE_IN_ALL
void tracker_sparql_connection_close (TrackerSparqlConnection *connection);

#endif /* __TRACKER_SPARQL_CONNECTION_H__ */
