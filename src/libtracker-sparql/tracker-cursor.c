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
/**
 * SECTION: tracker-sparql-cursor
 * @short_description: Iteration of the query results
 * @title: TrackerSparqlCursor
 * @stability: Stable
 * @include: tracker-sparql.h
 *
 * <para>
 * #TrackerSparqlCursor is an object which provides methods to iterate the
 * results of a query to the Tracker Store.
 * </para>
 */
#include "config.h"

#include "tracker-cursor.h"
#include "tracker-private.h"

enum {
	PROP_0,
	PROP_CONNECTION,
	PROP_N_COLUMNS,
	N_PROPS
};

static GParamSpec *props[N_PROPS];

typedef struct {
	TrackerSparqlConnection *connection;
	gint n_columns;
} TrackerSparqlCursorPrivate;

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (TrackerSparqlCursor, tracker_sparql_cursor,
                                     G_TYPE_OBJECT)

static void
tracker_sparql_cursor_init (TrackerSparqlCursor *cursor)
{
}

static gint64
tracker_sparql_cursor_real_get_integer (TrackerSparqlCursor *cursor,
                                        gint                 column)
{
	const gchar *text;

	text = tracker_sparql_cursor_get_string (cursor, column, NULL);
	return g_ascii_strtoll (text, NULL, 10);
}

static gdouble
tracker_sparql_cursor_real_get_double (TrackerSparqlCursor *cursor,
                                       gint                 column)
{
	const gchar *text;

	text = tracker_sparql_cursor_get_string (cursor, column, NULL);
	return g_ascii_strtod (text, NULL);
}

static gboolean
tracker_sparql_cursor_real_get_boolean (TrackerSparqlCursor *cursor,
                                        gint                 column)
{
	const gchar *text;

	text = tracker_sparql_cursor_get_string (cursor, column, NULL);
	return g_ascii_strcasecmp (text, "true") == 0;
}

static gboolean
tracker_sparql_cursor_real_is_bound (TrackerSparqlCursor *cursor,
                                     gint                 column)
{
	return tracker_sparql_cursor_get_value_type (cursor, column) != TRACKER_SPARQL_VALUE_TYPE_UNBOUND;
}

static void
tracker_sparql_cursor_finalize (GObject *object)
{
	TrackerSparqlCursor *cursor = TRACKER_SPARQL_CURSOR (object);
	TrackerSparqlCursorPrivate *priv = tracker_sparql_cursor_get_instance_private (cursor);

	g_clear_object (&priv->connection);
	G_OBJECT_CLASS (tracker_sparql_cursor_parent_class)->finalize (object);
}

static void
tracker_sparql_cursor_set_property (GObject      *object,
                                    guint         prop_id,
                                    const GValue *value,
                                    GParamSpec   *pspec)
{
	TrackerSparqlCursor *cursor = TRACKER_SPARQL_CURSOR (object);
	TrackerSparqlCursorPrivate *priv = tracker_sparql_cursor_get_instance_private (cursor);

	switch (prop_id) {
	case PROP_CONNECTION:
		priv->connection = g_value_dup_object (value);
		break;
	case PROP_N_COLUMNS:
		priv->n_columns = g_value_get_int (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
tracker_sparql_cursor_get_property (GObject    *object,
                                    guint       prop_id,
                                    GValue     *value,
                                    GParamSpec *pspec)
{
	TrackerSparqlCursor *cursor = TRACKER_SPARQL_CURSOR (object);
	TrackerSparqlCursorPrivate *priv = tracker_sparql_cursor_get_instance_private (cursor);

	switch (prop_id) {
	case PROP_CONNECTION:
		g_value_set_object (value, priv->connection);
		break;
	case PROP_N_COLUMNS:
		g_value_set_int (value, priv->n_columns);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
tracker_sparql_cursor_class_init (TrackerSparqlCursorClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = tracker_sparql_cursor_finalize;
	object_class->set_property = tracker_sparql_cursor_set_property;
	object_class->get_property = tracker_sparql_cursor_get_property;

	klass->get_integer = tracker_sparql_cursor_real_get_integer;
	klass->get_double = tracker_sparql_cursor_real_get_double;
	klass->get_boolean = tracker_sparql_cursor_real_get_boolean;
	klass->is_bound = tracker_sparql_cursor_real_is_bound;

	/**
	 * TrackerSparqlCursor:connection:
	 *
	 * The #TrackerSparqlConnection used to retrieve the results.
	 */
	props[PROP_CONNECTION] =
		g_param_spec_object ("connection",
		                     "connection",
		                     "connection",
		                     TRACKER_SPARQL_TYPE_CONNECTION,
		                     G_PARAM_CONSTRUCT_ONLY |
		                     G_PARAM_STATIC_STRINGS |
		                     G_PARAM_READABLE |
		                     G_PARAM_WRITABLE);
	/**
	 * TrackerSparqlCursor:n_columns:
	 *
	 * Number of columns available in the results to iterate.
	 */
	props[PROP_N_COLUMNS] =
		g_param_spec_int ("n-columns",
		                  "n-columns",
		                  "n-columns",
		                  G_MININT, G_MAXINT, 0,
		                  G_PARAM_STATIC_STRINGS |
		                  G_PARAM_READABLE);

	g_object_class_install_properties (object_class, N_PROPS, props);
}

/**
 * tracker_sparql_cursor_get_connection:
 * @cursor: a #TrackerSparqlCursor
 *
 * Returns: (transfer none): the #TrackerSparqlConnection associated with this
 * #TrackerSparqlCursor. The returned object must not be unreferenced
 * by the caller.
 */
TrackerSparqlConnection *
tracker_sparql_cursor_get_connection (TrackerSparqlCursor *cursor)
{
	TrackerSparqlCursorPrivate *priv = tracker_sparql_cursor_get_instance_private (cursor);

	g_return_val_if_fail (TRACKER_IS_SPARQL_CURSOR (cursor), NULL);

	return priv->connection;
}

void
tracker_sparql_cursor_set_connection (TrackerSparqlCursor     *cursor,
                                      TrackerSparqlConnection *connection)
{
	TrackerSparqlCursorPrivate *priv = tracker_sparql_cursor_get_instance_private (cursor);

	g_return_if_fail (TRACKER_IS_SPARQL_CURSOR (cursor));
	g_return_if_fail (TRACKER_IS_SPARQL_CONNECTION (connection));

	g_set_object (&priv->connection, connection);
}

/**
 * tracker_sparql_cursor_get_n_columns:
 * @cursor: a #TrackerSparqlCursor
 *
 * This method should only be called after a successful
 * tracker_sparql_cursor_next(); otherwise its return value
 * will be undefined.
 *
 * Returns: a #gint representing the number of columns available in the
 * results to iterate.
 */
gint
tracker_sparql_cursor_get_n_columns (TrackerSparqlCursor *cursor)
{
	g_return_val_if_fail (TRACKER_IS_SPARQL_CURSOR (cursor), 0);

	return TRACKER_SPARQL_CURSOR_GET_CLASS (cursor)->get_n_columns (cursor);
}

/**
 * tracker_sparql_cursor_get_string:
 * @cursor: a #TrackerSparqlCursor
 * @column: column number to retrieve (first one is 0)
 * @length: (out) (nullable): length of the returned string, or %NULL
 *
 * Retrieves a string representation of the data in the current
 * row in @column.
 *
 * Returns: a string which must not be freed. %NULL is returned if
 * the column is not in the [0,#n_columns] range.
 */
const gchar *
tracker_sparql_cursor_get_string (TrackerSparqlCursor *cursor,
                                  gint                 column,
                                  glong               *length)
{
	g_return_val_if_fail (TRACKER_IS_SPARQL_CURSOR (cursor), NULL);

	return TRACKER_SPARQL_CURSOR_GET_CLASS (cursor)->get_string (cursor,
	                                                             column,
	                                                             length);
}

/**
 * tracker_sparql_cursor_get_boolean:
 * @cursor: a #TrackerSparqlCursor
 * @column: column number to retrieve (first one is 0)
 *
 * Retrieve a boolean for the current row in @column.
 *
 * Returns: a #gboolean.
 */
gboolean
tracker_sparql_cursor_get_boolean (TrackerSparqlCursor *cursor,
                                   gint                 column)
{
	g_return_val_if_fail (TRACKER_IS_SPARQL_CURSOR (cursor), FALSE);

	return TRACKER_SPARQL_CURSOR_GET_CLASS (cursor)->get_boolean (cursor,
	                                                              column);
}

/**
 * tracker_sparql_cursor_get_double:
 * @cursor: a #TrackerSparqlCursor
 * @column: column number to retrieve (first one is 0)
 *
 * Retrieve a double for the current row in @column.
 *
 * Returns: a double.
 */
gdouble
tracker_sparql_cursor_get_double (TrackerSparqlCursor *cursor,
                                  gint                 column)
{
	g_return_val_if_fail (TRACKER_IS_SPARQL_CURSOR (cursor), -1);

	return TRACKER_SPARQL_CURSOR_GET_CLASS (cursor)->get_double (cursor,
	                                                             column);
}

/**
 * tracker_sparql_cursor_get_integer:
 * @cursor: a #TrackerSparqlCursor
 * @column: column number to retrieve (first one is 0)
 *
 * Retrieve an integer for the current row in @column.
 *
 * Returns: a #gint64.
 */
gint64
tracker_sparql_cursor_get_integer (TrackerSparqlCursor *cursor,
                                   gint                 column)
{
	g_return_val_if_fail (TRACKER_IS_SPARQL_CURSOR (cursor), -1);

	return TRACKER_SPARQL_CURSOR_GET_CLASS (cursor)->get_integer (cursor,
	                                                              column);
}

/**
 * tracker_sparql_cursor_get_value_type:
 * @cursor: a #TrackerSparqlCursor
 * @column: column number to retrieve (first one is 0)
 *
 * The data type bound to the current row in @column is returned.
 *
 * Returns: a #TrackerSparqlValueType.
 */
TrackerSparqlValueType
tracker_sparql_cursor_get_value_type (TrackerSparqlCursor *cursor,
                                      gint                 column)
{
	g_return_val_if_fail (TRACKER_IS_SPARQL_CURSOR (cursor),
	                      TRACKER_SPARQL_VALUE_TYPE_UNBOUND);

	return TRACKER_SPARQL_CURSOR_GET_CLASS (cursor)->get_value_type (cursor,
	                                                                 column);
}

/**
 * tracker_sparql_cursor_get_variable_name:
 * @cursor: a #TrackerSparqlCursor
 * @column: column number to retrieve (first one is 0)
 *
 * Retrieves the variable name for the current row in @column.
 *
 * Returns: a string which must not be freed.
 */
const gchar *
tracker_sparql_cursor_get_variable_name (TrackerSparqlCursor *cursor,
                                         gint                 column)
{
	g_return_val_if_fail (TRACKER_IS_SPARQL_CURSOR (cursor), NULL);

	return TRACKER_SPARQL_CURSOR_GET_CLASS (cursor)->get_variable_name (cursor,
	                                                                    column);
}

/**
 * tracker_sparql_cursor_close:
 * @cursor: a #TrackerSparqlCursor
 *
 * Closes the iterator, making it invalid.
 */
void
tracker_sparql_cursor_close (TrackerSparqlCursor *cursor)
{
	g_return_if_fail (TRACKER_IS_SPARQL_CURSOR (cursor));

	return TRACKER_SPARQL_CURSOR_GET_CLASS (cursor)->close (cursor);
}

/**
 * tracker_sparql_cursor_is_bound:
 * @cursor: a #TrackerSparqlCursor
 * @column: column number to retrieve (first one is 0)
 *
 * If the current row and @column are bound to a value, %TRUE is returned.
 *
 * Returns: a %TRUE or %FALSE.
 */
gboolean
tracker_sparql_cursor_is_bound (TrackerSparqlCursor *cursor,
                                gint                 column)
{
	g_return_val_if_fail (TRACKER_IS_SPARQL_CURSOR (cursor), FALSE);

	return TRACKER_SPARQL_CURSOR_GET_CLASS (cursor)->is_bound (cursor, column);
}

/**
 * tracker_sparql_cursor_next:
 * @cursor: a #TrackerSparqlCursor
 * @cancellable: a #GCancellable used to cancel the operation
 * @error: #GError for error reporting.
 *
 * Iterates to the next result. This is completely synchronous and
 * it may block.
 *
 * Returns: %FALSE if no more results found, otherwise %TRUE.
 */
gboolean
tracker_sparql_cursor_next (TrackerSparqlCursor  *cursor,
                            GCancellable         *cancellable,
                            GError              **error)
{
	g_return_val_if_fail (TRACKER_IS_SPARQL_CURSOR (cursor), FALSE);
	g_return_val_if_fail (!cancellable || G_IS_CANCELLABLE (cancellable), FALSE);
	g_return_val_if_fail (!error || !*error, FALSE);

	return TRACKER_SPARQL_CURSOR_GET_CLASS (cursor)->next (cursor,
	                                                       cancellable,
	                                                       error);
}

/**
 * tracker_sparql_cursor_next_async:
 * @cursor: a #TrackerSparqlCursor
 * @cancellable: a #GCancellable used to cancel the operation
 * @callback: user-defined #GAsyncReadyCallback to be called when
 *            asynchronous operation is finished.
 * @user_data: user-defined data to be passed to @callback
 *
 * Iterates, asynchronously, to the next result.
 */
void
tracker_sparql_cursor_next_async (TrackerSparqlCursor  *cursor,
                                  GCancellable         *cancellable,
                                  GAsyncReadyCallback   callback,
                                  gpointer              user_data)
{
	g_return_if_fail (TRACKER_IS_SPARQL_CURSOR (cursor));
	g_return_if_fail (!cancellable || G_IS_CANCELLABLE (cancellable));

	return TRACKER_SPARQL_CURSOR_GET_CLASS (cursor)->next_async (cursor,
	                                                             cancellable,
	                                                             callback,
	                                                             user_data);
}

/**
 * tracker_sparql_cursor_next_finish:
 * @cursor: a #TrackerSparqlCursor
 * @res: a #GAsyncResult with the result of the operation
 * @error: #GError for error reporting.
 *
 * Finishes the asynchronous iteration to the next result.
 *
 * Returns: %FALSE if no more results found, otherwise %TRUE.
 */
gboolean
tracker_sparql_cursor_next_finish (TrackerSparqlCursor  *cursor,
                                   GAsyncResult         *res,
                                   GError              **error)
{
	g_return_val_if_fail (TRACKER_IS_SPARQL_CURSOR (cursor), FALSE);
	g_return_val_if_fail (G_IS_ASYNC_RESULT (res), FALSE);
	g_return_val_if_fail (!error || !*error, FALSE);

	return TRACKER_SPARQL_CURSOR_GET_CLASS (cursor)->next_finish (cursor,
	                                                              res,
	                                                              error);
}

/**
 * tracker_sparql_cursor_rewind:
 * @cursor: a #TrackerSparqlCursor
 *
 * Resets the iterator to point back to the first result.
 */
void
tracker_sparql_cursor_rewind (TrackerSparqlCursor *cursor)
{
	g_return_if_fail (TRACKER_IS_SPARQL_CURSOR (cursor));

	return TRACKER_SPARQL_CURSOR_GET_CLASS (cursor)->rewind (cursor);
}
