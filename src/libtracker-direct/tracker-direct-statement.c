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

#include "config.h"
#include "tracker-direct-statement.h"
#include "tracker-data.h"

typedef struct _TrackerDirectStatementPrivate TrackerDirectStatementPrivate;

struct _TrackerDirectStatementPrivate
{
	TrackerSparql *sparql;
	GHashTable *values;
};

G_DEFINE_TYPE_WITH_PRIVATE (TrackerDirectStatement,
                            tracker_direct_statement,
                            TRACKER_SPARQL_TYPE_STATEMENT)

static void
tracker_direct_statement_finalize (GObject *object)
{
	TrackerDirectStatementPrivate *priv;

	priv = tracker_direct_statement_get_instance_private (TRACKER_DIRECT_STATEMENT (object));
	g_hash_table_destroy (priv->values);
	g_clear_object (&priv->sparql);

	G_OBJECT_CLASS (tracker_direct_statement_parent_class)->finalize (object);
}

static void
tracker_direct_statement_constructed (GObject *object)
{
	TrackerDirectStatementPrivate *priv;
	TrackerSparqlConnection *conn;
	gchar *sparql;

	priv = tracker_direct_statement_get_instance_private (TRACKER_DIRECT_STATEMENT (object));

	g_object_get (object,
	              "sparql", &sparql,
	              "connection", &conn,
	              NULL);

	priv->sparql = tracker_sparql_new (tracker_direct_connection_get_data_manager (TRACKER_DIRECT_CONNECTION (conn)),
	                                   sparql);
	g_object_unref (conn);
	g_free (sparql);

	G_OBJECT_CLASS (tracker_direct_statement_parent_class)->constructed (object);
}

static GValue *
insert_value (TrackerDirectStatement *stmt,
	      const gchar            *name,
	      GType                   type)
{
	TrackerDirectStatementPrivate *priv;
	GValue *value;

	priv = tracker_direct_statement_get_instance_private (stmt);
	value = g_new0 (GValue, 1);
	g_value_init (value, type);

	g_hash_table_insert (priv->values, g_strdup (name), value);

	return value;
}

static void
tracker_direct_statement_bind_int (TrackerSparqlStatement *stmt,
                                   const gchar            *name,
                                   gint64                  value)
{
	GValue *gvalue;

	gvalue = insert_value (TRACKER_DIRECT_STATEMENT (stmt), name, G_TYPE_INT64);
	g_value_set_int64 (gvalue, value);
}

static void
tracker_direct_statement_bind_double (TrackerSparqlStatement *stmt,
                                      const gchar            *name,
                                      double                  value)
{
	GValue *gvalue;

	gvalue = insert_value (TRACKER_DIRECT_STATEMENT (stmt), name, G_TYPE_DOUBLE);
	g_value_set_double (gvalue, value);
}

static void
tracker_direct_statement_bind_boolean (TrackerSparqlStatement *stmt,
                                       const gchar            *name,
                                       gboolean                value)
{
	GValue *gvalue;

	gvalue = insert_value (TRACKER_DIRECT_STATEMENT (stmt), name, G_TYPE_BOOLEAN);
	g_value_set_boolean (gvalue, value);
}

static void
tracker_direct_statement_bind_string (TrackerSparqlStatement *stmt,
                                      const gchar            *name,
                                      const gchar            *value)
{
	GValue *gvalue;

	gvalue = insert_value (TRACKER_DIRECT_STATEMENT (stmt), name, G_TYPE_STRING);
	g_value_set_string (gvalue, value);
}

static TrackerSparqlCursor *
tracker_direct_statement_execute (TrackerSparqlStatement  *stmt,
                                  GCancellable            *cancellable,
                                  GError                 **error)
{
	TrackerDirectStatementPrivate *priv;

	priv = tracker_direct_statement_get_instance_private (TRACKER_DIRECT_STATEMENT (stmt));

	return tracker_sparql_execute_cursor (priv->sparql, priv->values, error);
}

static void
execute_in_thread (GTask        *task,
                   gpointer      object,
                   gpointer      task_data,
                   GCancellable *cancellable)
{
	TrackerDirectStatementPrivate *priv;
	TrackerSparqlCursor *cursor;
	GHashTable *values = task_data;
	GError *error = NULL;

	priv = tracker_direct_statement_get_instance_private (object);
	cursor = tracker_sparql_execute_cursor (priv->sparql, values, &error);

	if (error)
		g_task_return_error (task, error);
	else
		g_task_return_pointer (task, cursor, g_object_unref);

	g_object_unref (task);
}

static void
free_gvalue (gpointer data)
{
	g_value_unset (data);
	g_free (data);
}

static GHashTable *
create_values_ht (void)
{
	return g_hash_table_new_full (g_str_hash, g_str_equal,
				      g_free, free_gvalue);
}

static GHashTable *
copy_values_deep (GHashTable *values)
{
	GHashTable *copy;
	GHashTableIter iter;
	gpointer key, val;

	copy = create_values_ht ();
	g_hash_table_iter_init (&iter, values);

	while (g_hash_table_iter_next (&iter, &key, &val)) {
		GValue *copy_value;

		copy_value = g_new0 (GValue, 1);
		g_value_init (copy_value, G_VALUE_TYPE (val));
		g_value_copy (copy_value, val);

		g_hash_table_insert (copy, g_strdup (key), copy_value);
	}

	return copy;
}

static void
tracker_direct_statement_execute_async (TrackerSparqlStatement *stmt,
                                        GCancellable           *cancellable,
                                        GAsyncReadyCallback     callback,
                                        gpointer                user_data)
{
	TrackerDirectStatementPrivate *priv;
	GHashTable *values;
	GTask *task;

	priv = tracker_direct_statement_get_instance_private (TRACKER_DIRECT_STATEMENT (stmt));

	values = copy_values_deep (priv->values);

	task = g_task_new (stmt, cancellable, callback, user_data);
	g_task_set_task_data (task, values, (GDestroyNotify) g_hash_table_unref);
	g_task_run_in_thread (task, execute_in_thread);
}

static TrackerSparqlCursor *
tracker_direct_statement_execute_finish (TrackerSparqlStatement  *stmt,
                                         GAsyncResult            *res,
                                         GError                 **error)
{
	return g_task_propagate_pointer (G_TASK (res), error);
}

static void
tracker_direct_statement_class_init (TrackerDirectStatementClass *klass)
{
	TrackerSparqlStatementClass *stmt_class = (TrackerSparqlStatementClass *) klass;
	GObjectClass *object_class = (GObjectClass *) klass;

	object_class->finalize = tracker_direct_statement_finalize;
	object_class->constructed = tracker_direct_statement_constructed;

	stmt_class->bind_int = tracker_direct_statement_bind_int;
	stmt_class->bind_boolean = tracker_direct_statement_bind_boolean;
	stmt_class->bind_double = tracker_direct_statement_bind_double;
	stmt_class->bind_string = tracker_direct_statement_bind_string;
	stmt_class->execute = tracker_direct_statement_execute;
	stmt_class->execute_async = tracker_direct_statement_execute_async;
	stmt_class->execute_finish = tracker_direct_statement_execute_finish;
}

static void
tracker_direct_statement_init (TrackerDirectStatement *stmt)
{
	TrackerDirectStatementPrivate *priv;

	priv = tracker_direct_statement_get_instance_private (stmt);
	priv->values = create_values_ht ();
}

TrackerDirectStatement *
tracker_direct_statement_new (TrackerSparqlConnection  *conn,
                              const gchar              *sparql,
                              GError                  **error)
{
	return g_object_new (TRACKER_TYPE_DIRECT_STATEMENT,
	                     "sparql", sparql,
	                     "connection", conn,
	                     NULL);
}
