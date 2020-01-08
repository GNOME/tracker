/*
 * Copyright (C) 2010, Nokia <ivan.frade@nokia.com>
 * Copyright (C) 2019, Red Hat Inc.
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
#ifndef __TRACKER_PRIVATE_H__
#define __TRACKER_PRIVATE_H__

#include "tracker-cursor.h"

typedef struct _TrackerSparqlConnectionClass TrackerSparqlConnectionClass;

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

typedef struct _TrackerSparqlCursorClass TrackerSparqlCursorClass;

struct _TrackerSparqlCursorClass
{
	GObjectClass parent_class;

	TrackerSparqlValueType (* get_value_type) (TrackerSparqlCursor *cursor,
	                                           gint                 column);
        const gchar* (* get_variable_name) (TrackerSparqlCursor *cursor,
                                            gint                 column);
	const gchar* (* get_string) (TrackerSparqlCursor *cursor,
	                             gint                 column,
	                             glong               *length);
        gboolean (* next) (TrackerSparqlCursor  *cursor,
                           GCancellable         *cancellable,
                           GError              **error);
        void (* next_async) (TrackerSparqlCursor *cursor,
                             GCancellable        *cancellable,
                             GAsyncReadyCallback  callback,
                             gpointer             user_data);
        gboolean (* next_finish) (TrackerSparqlCursor  *cursor,
                                  GAsyncResult         *res,
                                  GError              **error);
        void (* rewind) (TrackerSparqlCursor* cursor);
        void (* close) (TrackerSparqlCursor* cursor);
        gint64 (* get_integer) (TrackerSparqlCursor *cursor,
                                gint                 column);
        gdouble (* get_double) (TrackerSparqlCursor *cursor,
                                gint                 column);
        gboolean (* get_boolean) (TrackerSparqlCursor *cursor,
                                  gint                 column);
        gboolean (* is_bound) (TrackerSparqlCursor *cursor,
                               gint                 column);
        gint (* get_n_columns) (TrackerSparqlCursor *cursor);
};

typedef struct _TrackerEndpointClass TrackerEndpointClass;

struct _TrackerEndpointClass {
	GObjectClass parent_class;
};

typedef struct _TrackerEndpointDBusClass TrackerEndpointDBusClass;

struct _TrackerEndpointDBusClass {
	struct _TrackerEndpointClass parent_class;
};

typedef struct _TrackerResourceClass TrackerResourceClass;

struct _TrackerResourceClass
{
	GObjectClass parent_class;
};

typedef struct _TrackerSparqlStatementClass TrackerSparqlStatementClass;

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

typedef struct _TrackerNotifierClass TrackerNotifierClass;

struct _TrackerNotifierClass {
	GObjectClass parent_class;

	void (* events) (TrackerNotifier *notifier,
	                 const GPtrArray *events);
};

void tracker_sparql_cursor_set_connection (TrackerSparqlCursor     *cursor,
                                           TrackerSparqlConnection *connection);

#endif /* __TRACKER_PRIVATE_H__ */
