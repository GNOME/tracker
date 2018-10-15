/*
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

#ifndef __LIBTRACKER_DB_INTERFACE_H__
#define __LIBTRACKER_DB_INTERFACE_H__

#include <glib-object.h>
#include <gio/gio.h>

#include <libtracker-sparql/tracker-sparql.h>

#include "tracker-property.h"

G_BEGIN_DECLS

#if !defined (__LIBTRACKER_DATA_INSIDE__) && !defined (TRACKER_COMPILATION)
#error "only <libtracker-data/tracker-data.h> must be included directly."
#endif

#define TRACKER_TYPE_DB_INTERFACE           (tracker_db_interface_get_type ())
#define TRACKER_DB_INTERFACE(obj)           (G_TYPE_CHECK_INSTANCE_CAST ((obj), TRACKER_TYPE_DB_INTERFACE, TrackerDBInterface))
#define TRACKER_DB_INTERFACE_CLASS(c)       (G_TYPE_CHECK_CLASS_CAST ((c),      TRACKER_TYPE_DB_INTERFACE, TrackerDBInterfaceClass))
#define TRACKER_IS_DB_INTERFACE(obj)        (G_TYPE_CHECK_INSTANCE_TYPE ((obj), TRACKER_TYPE_DB_INTERFACE))
#define TRACKER_IS_DB_INTERFACE_CLASS(c)    (G_TYPE_CHECK_CLASS_TYPE ((o),      TRACKER_TYPE_DB_INTERFACE))
#define TRACKER_DB_INTERFACE_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), TRACKER_TYPE_DB_INTERFACE, TrackerDBInterfaceClass))

#define TRACKER_TYPE_DB_STATEMENT           (tracker_db_statement_get_type ())
#define TRACKER_DB_STATEMENT(obj)           (G_TYPE_CHECK_INSTANCE_CAST ((obj), TRACKER_TYPE_DB_STATEMENT, TrackerDBStatement))
#define TRACKER_DB_STATEMENT_CLASS(c)       (G_TYPE_CHECK_CLASS_CAST ((c),      TRACKER_TYPE_DB_STATEMENT, TrackerDBStatementClass))
#define TRACKER_IS_DB_STATEMENT(obj)        (G_TYPE_CHECK_INSTANCE_TYPE ((obj), TRACKER_TYPE_DB_STATEMENT))
#define TRACKER_IS_DB_STATEMENT_CLASS(c)    (G_TYPE_CHECK_CLASS_TYPE ((o),      TRACKER_TYPE_DB_STATEMENT))
#define TRACKER_DB_STATEMENT_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), TRACKER_TYPE_DB_STATEMENT, TrackerDBStatementClass))

#define TRACKER_TYPE_DB_CURSOR              (tracker_db_cursor_get_type ())
#define TRACKER_DB_CURSOR(obj)              (G_TYPE_CHECK_INSTANCE_CAST ((obj), TRACKER_TYPE_DB_CURSOR, TrackerDBCursor))
#define TRACKER_DB_CURSOR_CLASS(c)          (G_TYPE_CHECK_CLASS_CAST ((c),      TRACKER_TYPE_DB_CURSOR, TrackerDBCursorClass))
#define TRACKER_IS_DB_CURSOR(obj)           (G_TYPE_CHECK_INSTANCE_TYPE ((obj), TRACKER_TYPE_DB_CURSOR))
#define TRACKER_IS_DB_CURSOR_CLASS(c)       (G_TYPE_CHECK_CLASS_TYPE ((o),      TRACKER_TYPE_DB_CURSOR))
#define TRACKER_DB_CURSOR_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj),  TRACKER_TYPE_DB_CURSOR, TrackerDBCursorClass))

#define TRACKER_DB_INTERFACE_ERROR          (tracker_db_interface_error_quark ())

typedef void (*TrackerBusyCallback)      (const gchar *status,
                                          gdouble      progress,
                                          gpointer     user_data);

typedef enum {
	TRACKER_DB_QUERY_ERROR,
	TRACKER_DB_CORRUPT,
	TRACKER_DB_INTERRUPTED,
	TRACKER_DB_OPEN_ERROR,
	TRACKER_DB_NO_SPACE
} TrackerDBInterfaceError;

typedef enum {
	TRACKER_DB_STATEMENT_CACHE_TYPE_SELECT,
	TRACKER_DB_STATEMENT_CACHE_TYPE_UPDATE,
	TRACKER_DB_STATEMENT_CACHE_TYPE_NONE
} TrackerDBStatementCacheType;

typedef struct TrackerDBInterface      TrackerDBInterface;
typedef struct TrackerDBInterfaceClass TrackerDBInterfaceClass;
typedef struct TrackerDBStatement      TrackerDBStatement;
typedef struct TrackerDBStatementClass TrackerDBStatementClass;
typedef struct TrackerDBCursor         TrackerDBCursor;
typedef struct TrackerDBCursorClass    TrackerDBCursorClass;

GQuark                  tracker_db_interface_error_quark             (void);

GType                   tracker_db_interface_get_type                (void);
GType                   tracker_db_statement_get_type                (void);
GType                   tracker_db_cursor_get_type                   (void);

void                    tracker_db_interface_set_max_stmt_cache_size (TrackerDBInterface         *db_interface,
                                                                      TrackerDBStatementCacheType cache_type,
                                                                      guint                       max_size);

/* User data functions, mainly to attach the data manager */
void                    tracker_db_interface_set_user_data           (TrackerDBInterface         *interface,
                                                                      gpointer                    user_data,
	                                                              GDestroyNotify              destroy_notify);
gpointer                tracker_db_interface_get_user_data           (TrackerDBInterface         *interface);

/* Functions to create queries/procedures */
TrackerDBStatement *    tracker_db_interface_create_statement        (TrackerDBInterface          *interface,
                                                                      TrackerDBStatementCacheType  cache_type,
                                                                      GError                     **error,
                                                                      const gchar                 *query,
                                                                      ...) G_GNUC_PRINTF (4, 5);
void                    tracker_db_interface_execute_vquery          (TrackerDBInterface          *interface,
                                                                      GError                     **error,
                                                                      const gchar                 *query,
                                                                      va_list                      args);
void                    tracker_db_interface_execute_query           (TrackerDBInterface          *interface,
                                                                      GError                     **error,
                                                                      const gchar                 *query,
                                                                       ...) G_GNUC_PRINTF (3, 4);

gboolean                tracker_db_interface_start_transaction       (TrackerDBInterface         *interface);
gboolean                tracker_db_interface_end_db_transaction      (TrackerDBInterface         *interface,
                                                                      GError                    **error);
gboolean                tracker_db_interface_get_is_used             (TrackerDBInterface         *interface);

void                    tracker_db_statement_bind_double             (TrackerDBStatement         *stmt,
                                                                      int                         index,
                                                                      double                      value);
void                    tracker_db_statement_bind_int                (TrackerDBStatement         *stmt,
                                                                      int                         index,
                                                                      gint64                      value);
void                    tracker_db_statement_bind_null               (TrackerDBStatement         *stmt,
                                                                      int                         index);
void                    tracker_db_statement_bind_text               (TrackerDBStatement         *stmt,
                                                                      int                         index,
                                                                      const gchar                *value);
void                    tracker_db_statement_bind_value              (TrackerDBStatement         *stmt,
                                                                      int                         index,
								      const GValue               *value);
void                    tracker_db_statement_execute                 (TrackerDBStatement         *stmt,
                                                                      GError                    **error);
TrackerDBCursor *       tracker_db_statement_start_cursor            (TrackerDBStatement         *stmt,
                                                                      GError                    **error);
TrackerDBCursor *       tracker_db_statement_start_sparql_cursor     (TrackerDBStatement         *stmt,
                                                                      TrackerPropertyType        *types,
                                                                      gint                        n_types,
                                                                      const gchar * const        *variable_names,
                                                                      gint                        n_variable_names,
                                                                      GError                    **error);

/* Functions to deal with a cursor */
void                    tracker_db_cursor_rewind                     (TrackerDBCursor            *cursor);
gboolean                tracker_db_cursor_iter_next                  (TrackerDBCursor            *cursor,
                                                                      GCancellable               *cancellable,
                                                                      GError                    **error);
guint                   tracker_db_cursor_get_n_columns              (TrackerDBCursor            *cursor);
const gchar*            tracker_db_cursor_get_variable_name          (TrackerDBCursor            *cursor,
                                                                      guint                       column);
TrackerSparqlValueType  tracker_db_cursor_get_value_type             (TrackerDBCursor            *cursor,
                                                                      guint                       column);
void                    tracker_db_cursor_get_value                  (TrackerDBCursor            *cursor,
                                                                      guint                       column,
                                                                      GValue                     *value);
const gchar*            tracker_db_cursor_get_string                 (TrackerDBCursor            *cursor,
                                                                      guint                       column,
                                                                      glong                      *length);
gint64                  tracker_db_cursor_get_int                    (TrackerDBCursor            *cursor,
                                                                      guint                       column);
gdouble                 tracker_db_cursor_get_double                 (TrackerDBCursor            *cursor,
                                                                      guint                       column);

G_END_DECLS

#endif /* __LIBTRACKER_DB_INTERFACE_H__ */
