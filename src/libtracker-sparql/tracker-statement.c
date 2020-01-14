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
/**
 * SECTION: tracker-sparql-statement
 * @short_description: Prepared statements
 * @title: TrackerSparqlStatement
 * @stability: Stable
 * @include: tracker-sparql.h
 *
 * The <structname>TrackerSparqlStatement</structname> object represents
 * a SPARQL query. This query may contain parameterized variables
 * (expressed as ~var in the syntax), which may be mapped to arbitrary
 * values prior to execution. This statement may be reused for future
 * queries with different values.
 *
 * The argument bindings may be changed through tracker_sparql_statement_bind_int(),
 * tracker_sparql_statement_bind_boolean(), tracker_sparql_statement_bind_double()
 * and tracker_sparql_statement_bind_string(). Those functions receive
 * a @name argument corresponding for the variable name in the SPARQL query
 * (eg. "var" for ~var) and a @value to map the variable to.
 *
 * Once all arguments have a value, the query may be executed through
 * tracker_sparql_statement_execute() or tracker_sparql_statement_execute_async().
 *
 * This object was added in Tracker 2.2.
 */
#include "config.h"

#include "tracker-statement.h"
#include "tracker-private.h"

enum {
	PROP_0,
	PROP_CONNECTION,
	PROP_SPARQL,
	N_PROPS
};

static GParamSpec *props[N_PROPS];

typedef struct {
	TrackerSparqlConnection *connection;
	gchar *sparql;
} TrackerSparqlStatementPrivate;

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (TrackerSparqlStatement,
                                     tracker_sparql_statement,
                                     G_TYPE_OBJECT)

static void
tracker_sparql_statement_init (TrackerSparqlStatement *stmt)
{
}

static void
tracker_sparql_statement_finalize (GObject *object)
{
	TrackerSparqlStatement *stmt = TRACKER_SPARQL_STATEMENT (object);
	TrackerSparqlStatementPrivate *priv = tracker_sparql_statement_get_instance_private (stmt);

	g_clear_object (&priv->connection);
	G_OBJECT_CLASS (tracker_sparql_statement_parent_class)->finalize (object);
}

static void
tracker_sparql_statement_set_property (GObject      *object,
                                       guint         prop_id,
                                       const GValue *value,
                                       GParamSpec   *pspec)
{
	TrackerSparqlStatement *stmt = TRACKER_SPARQL_STATEMENT (object);
	TrackerSparqlStatementPrivate *priv = tracker_sparql_statement_get_instance_private (stmt);

	switch (prop_id) {
	case PROP_CONNECTION:
		priv->connection = g_value_dup_object (value);
		break;
	case PROP_SPARQL:
		priv->sparql = g_value_dup_string (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
tracker_sparql_statement_get_property (GObject    *object,
                                       guint       prop_id,
                                       GValue     *value,
                                       GParamSpec *pspec)
{
	TrackerSparqlStatement *stmt = TRACKER_SPARQL_STATEMENT (object);
	TrackerSparqlStatementPrivate *priv = tracker_sparql_statement_get_instance_private (stmt);

	switch (prop_id) {
	case PROP_CONNECTION:
		g_value_set_object (value, priv->connection);
		break;
	case PROP_SPARQL:
		g_value_set_string (value, priv->sparql);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
tracker_sparql_statement_class_init (TrackerSparqlStatementClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = tracker_sparql_statement_finalize;
	object_class->set_property = tracker_sparql_statement_set_property;
	object_class->get_property = tracker_sparql_statement_get_property;

	/**
	 * TrackerSparqlStatement:connection:
	 *
	 * The #TrackerSparqlConnection used to perform the query.
	 */
	props[PROP_CONNECTION] =
		g_param_spec_object ("connection",
		                     "connection",
		                     "connection",
		                     TRACKER_TYPE_SPARQL_CONNECTION,
		                     G_PARAM_STATIC_STRINGS |
		                     G_PARAM_READABLE |
		                     G_PARAM_WRITABLE);
	/**
	 * TrackerSparqlStatement:sparql:
	 *
	 * SPARQL query stored in this statement.
	 */
	props[PROP_SPARQL] =
		g_param_spec_string ("sparql",
		                     "sparql",
		                     "sparql",
		                     NULL,
		                     G_PARAM_STATIC_STRINGS |
		                     G_PARAM_READABLE);

	g_object_class_install_properties (object_class, N_PROPS, props);
}

/**
 * tracker_sparql_statement_get_connection:
 * @stmt: a #TrackerSparqlStatement
 *
 * Returns the #TrackerSparqlConnection that this statement was created from.
 *
 * Returns: (transfer none): The SPARQL connection of this statement.
 **/
TrackerSparqlConnection *
tracker_sparql_statement_get_connection (TrackerSparqlStatement *stmt)
{
	TrackerSparqlStatementPrivate *priv = tracker_sparql_statement_get_instance_private (stmt);

	g_return_val_if_fail (TRACKER_IS_SPARQL_STATEMENT (stmt), NULL);

	return priv->connection;
}

/**
 * tracker_sparql_statement_get_sparql:
 * @stmt: a #TrackerSparqlStatement
 *
 * Returns the SPARQL string that this prepared statement holds.
 *
 * Returns: The contained SPARQL query
 **/
const gchar *
tracker_sparql_statement_get_sparql (TrackerSparqlStatement *stmt)
{
	TrackerSparqlStatementPrivate *priv = tracker_sparql_statement_get_instance_private (stmt);

	g_return_val_if_fail (TRACKER_IS_SPARQL_STATEMENT (stmt), NULL);

	return priv->sparql;
}

/**
 * tracker_sparql_statement_bind_boolean:
 * @stmt: a #TrackerSparqlStatement
 * @name: variable name
 * @value: value
 *
 * Binds the boolean @value to variable @name.
 */
void
tracker_sparql_statement_bind_boolean (TrackerSparqlStatement *stmt,
                                       const gchar            *name,
                                       gboolean                value)
{
	g_return_if_fail (TRACKER_IS_SPARQL_STATEMENT (stmt));
	g_return_if_fail (name != NULL);

	return TRACKER_SPARQL_STATEMENT_GET_CLASS (stmt)->bind_boolean (stmt,
	                                                                name,
	                                                                value);
}

/**
 * tracker_sparql_statement_bind_int:
 * @stmt: a #TrackerSparqlStatement
 * @name: variable name
 * @value: value
 *
 * Binds the integer @value to variable @name.
 */
void
tracker_sparql_statement_bind_int (TrackerSparqlStatement *stmt,
                                   const gchar            *name,
                                   gint64                  value)
{
	g_return_if_fail (TRACKER_IS_SPARQL_STATEMENT (stmt));
	g_return_if_fail (name != NULL);

	return TRACKER_SPARQL_STATEMENT_GET_CLASS (stmt)->bind_int (stmt,
	                                                            name,
	                                                            value);
}

/**
 * tracker_sparql_statement_bind_double:
 * @stmt: a #TrackerSparqlStatement
 * @name: variable name
 * @value: value
 *
 * Binds the double @value to variable @name.
 */
void
tracker_sparql_statement_bind_double (TrackerSparqlStatement *stmt,
                                      const gchar            *name,
                                      gdouble                 value)
{
	g_return_if_fail (TRACKER_IS_SPARQL_STATEMENT (stmt));
	g_return_if_fail (name != NULL);

	return TRACKER_SPARQL_STATEMENT_GET_CLASS (stmt)->bind_double (stmt,
	                                                               name,
	                                                               value);
}

/**
 * tracker_sparql_statement_bind_string:
 * @stmt: a #TrackerSparqlStatement
 * @name: variable name
 * @value: value
 *
 * Binds the string @value to variable @name.
 */
void
tracker_sparql_statement_bind_string (TrackerSparqlStatement *stmt,
                                      const gchar            *name,
                                      const gchar            *value)
{
	g_return_if_fail (TRACKER_IS_SPARQL_STATEMENT (stmt));
	g_return_if_fail (name != NULL);
	g_return_if_fail (value != NULL);

	return TRACKER_SPARQL_STATEMENT_GET_CLASS (stmt)->bind_string (stmt,
	                                                               name,
	                                                               value);
}

/**
 * tracker_sparql_statement_execute:
 * @stmt: a #TrackerSparqlStatement
 * @cancellable: a #GCancellable used to cancel the operation
 * @error: #GError for error reporting.
 *
 * Executes the SPARQL query with the currently bound values.
 *
 * Returns: (transfer full): A #TrackerSparqlCursor
 */
TrackerSparqlCursor *
tracker_sparql_statement_execute (TrackerSparqlStatement  *stmt,
                                  GCancellable            *cancellable,
                                  GError                 **error)
{
	g_return_val_if_fail (TRACKER_IS_SPARQL_STATEMENT (stmt), NULL);
	g_return_val_if_fail (!cancellable || G_IS_CANCELLABLE (cancellable), NULL);
	g_return_val_if_fail (!error || !*error, NULL);

	return TRACKER_SPARQL_STATEMENT_GET_CLASS (stmt)->execute (stmt,
	                                                           cancellable,
	                                                           error);
}

/**
 * tracker_sparql_statement_execute_async:
 * @stmt: a #TrackerSparqlStatement
 * @cancellable: a #GCancellable used to cancel the operation
 * @callback: user-defined #GAsyncReadyCallback to be called when
 *            asynchronous operation is finished.
 * @user_data: user-defined data to be passed to @callback
 *
 * Asynchronously executes the SPARQL query with the currently bound values.
 */
void
tracker_sparql_statement_execute_async (TrackerSparqlStatement *stmt,
                                        GCancellable           *cancellable,
                                        GAsyncReadyCallback     callback,
                                        gpointer                user_data)
{
	g_return_if_fail (TRACKER_IS_SPARQL_STATEMENT (stmt));
	g_return_if_fail (!cancellable || G_IS_CANCELLABLE (cancellable));

	return TRACKER_SPARQL_STATEMENT_GET_CLASS (stmt)->execute_async (stmt,
	                                                                 cancellable,
	                                                                 callback,
	                                                                 user_data);
}

/**
 * tracker_sparql_statement_execute_finish:
 * @stmt: a #TrackerSparqlStatement
 * @res: The #GAsyncResult from the callback used to return the #TrackerSparqlCursor
 * @error: The error which occurred or %NULL
 *
 * Finishes the asynchronous operation started through
 * tracker_sparql_statement_execute_async().
 *
 * Returns: (transfer full): A #TrackerSparqlCursor
 */
TrackerSparqlCursor *
tracker_sparql_statement_execute_finish (TrackerSparqlStatement  *stmt,
                                         GAsyncResult            *res,
                                         GError                 **error)
{
	g_return_val_if_fail (TRACKER_IS_SPARQL_STATEMENT (stmt), NULL);
	g_return_val_if_fail (G_IS_ASYNC_RESULT (res), NULL);
	g_return_val_if_fail (!error || !*error, NULL);

	return TRACKER_SPARQL_STATEMENT_GET_CLASS (stmt)->execute_finish (stmt,
	                                                                  res,
	                                                                  error);
}

/**
 * tracker_sparql_statement_clear_bindings:
 * @self: a #TrackerSparqlStatement
 *
 * Clears all boolean/string/integer/double bindings.
 *
 * Since: 3.0
 */
void
tracker_sparql_statement_clear_bindings (TrackerSparqlStatement *stmt)
{
	g_return_if_fail (TRACKER_IS_SPARQL_STATEMENT (stmt));

	TRACKER_SPARQL_STATEMENT_GET_CLASS (stmt)->clear_bindings (stmt);
}
