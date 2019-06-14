/*
 * Copyright (C) 2008-2010, Nokia
 * Copyright (C) 2018, Red Hat Inc.
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

#include "tracker-sparql-types.h"

enum {
	TOKEN_TYPE_NONE,
	TOKEN_TYPE_LITERAL,
	TOKEN_TYPE_VARIABLE,
	TOKEN_TYPE_PARAMETER,
	TOKEN_TYPE_PATH,
};

/* Helper structs */
static TrackerDataTable *
tracker_data_table_new (const gchar *tablename,
                        const gchar *graph,
                        gint         idx)
{
	TrackerDataTable *table;

	table = g_new0 (TrackerDataTable, 1);
	table->graph = g_strdup (graph);
	table->sql_db_tablename = g_strdup (tablename);
	table->sql_query_tablename = g_strdup_printf ("%s%d", tablename, idx);

	return table;
}

static void
tracker_data_table_free (TrackerDataTable *table)
{
	g_free (table->graph);
	g_free (table->sql_db_tablename);
	g_free (table->sql_query_tablename);
	g_free (table);
}

void
tracker_data_table_set_predicate_variable (TrackerDataTable *table,
                                           gboolean          is_variable)
{
	table->predicate_variable = is_variable;
}

static TrackerVariable *
tracker_variable_new (const gchar *sql_prefix,
		      const gchar *name)
{
	TrackerVariable *variable;

	variable = g_new0 (TrackerVariable, 1);
	variable->name = g_strdup (name);
	variable->sql_expression = g_strdup_printf ("\"%s_%s\"", sql_prefix, name);

	return variable;
}

static void
tracker_variable_free (TrackerVariable *variable)
{
	g_clear_object (&variable->binding);
	g_free (variable->sql_expression);
	g_free (variable->name);
	g_free (variable);
}

void
tracker_variable_set_sql_expression (TrackerVariable *variable,
                                     const gchar     *sql_expression)
{
	g_free (variable->sql_expression);
	variable->sql_expression = g_strdup (sql_expression);
}

const gchar *
tracker_variable_get_sql_expression (TrackerVariable *variable)
{
	return variable->sql_expression;
}

gchar *
tracker_variable_get_extra_sql_expression (TrackerVariable *variable,
					   const gchar     *suffix)
{
	return g_strdup_printf ("%s:%s", variable->sql_expression, suffix);
}

gboolean
tracker_variable_has_bindings (TrackerVariable *variable)
{
	return variable->binding != NULL;
}

void
tracker_variable_set_sample_binding (TrackerVariable        *variable,
                                     TrackerVariableBinding *binding)
{
	g_set_object (&variable->binding, binding);
}

TrackerVariableBinding *
tracker_variable_get_sample_binding (TrackerVariable *variable)
{
	return variable->binding;
}

guint
tracker_variable_hash (gconstpointer data)
{
	const TrackerVariable *variable = data;
	return g_str_hash (variable->name);
}

gboolean
tracker_variable_equal (gconstpointer data1,
                        gconstpointer data2)
{
	const TrackerVariable *var1 = data1, *var2 = data2;
	return g_str_equal (var1->name, var2->name);
}

void
tracker_token_literal_init (TrackerToken *token,
                            const gchar  *literal)
{
	token->type = TOKEN_TYPE_LITERAL;
	token->content.literal = g_strdup (literal);
}

void
tracker_token_variable_init (TrackerToken    *token,
                             TrackerVariable *variable)
{
	token->type = TOKEN_TYPE_VARIABLE;
	token->content.var = variable;
}

void
tracker_token_parameter_init (TrackerToken *token,
			      const gchar  *parameter)
{
	token->type = TOKEN_TYPE_PARAMETER;
	token->content.parameter = g_strdup (parameter);
}

void
tracker_token_path_init (TrackerToken       *token,
                         TrackerPathElement *path)
{
	token->type = TOKEN_TYPE_PATH;
	token->content.path = path;
}

void
tracker_token_unset (TrackerToken *token)
{
	if (token->type == TOKEN_TYPE_LITERAL)
		g_clear_pointer (&token->content.literal, g_free);
	else if (token->type == TOKEN_TYPE_PARAMETER)
		g_clear_pointer (&token->content.parameter, g_free);
	token->type = TOKEN_TYPE_NONE;
}

gboolean
tracker_token_is_empty (TrackerToken *token)
{
	return token->type == TOKEN_TYPE_NONE;
}

const gchar *
tracker_token_get_literal (TrackerToken *token)
{
	if (token->type == TOKEN_TYPE_LITERAL)
		return token->content.literal;
	return NULL;
}

TrackerVariable *
tracker_token_get_variable (TrackerToken *token)
{
	if (token->type == TOKEN_TYPE_VARIABLE)
		return token->content.var;
	return NULL;
}

const gchar *
tracker_token_get_parameter (TrackerToken *token)
{
	if (token->type == TOKEN_TYPE_PARAMETER)
		return token->content.parameter;
	return NULL;
}

TrackerPathElement *
tracker_token_get_path (TrackerToken *token)
{
	if (token->type == TOKEN_TYPE_PATH)
		return token->content.path;
	return NULL;
}

const gchar *
tracker_token_get_idstring (TrackerToken *token)
{
	if (token->type == TOKEN_TYPE_LITERAL)
		return token->content.literal;
	else if (token->type == TOKEN_TYPE_VARIABLE)
		return token->content.var->sql_expression;
	else if (token->type == TOKEN_TYPE_PATH)
		return token->content.path->name;
	else
		return NULL;
}

/* Solution */
TrackerSolution *
tracker_solution_new (guint n_cols)
{
	TrackerSolution *solution;

	solution = g_new0 (TrackerSolution, 1);
	solution->n_cols = n_cols;
	solution->columns = g_ptr_array_new_with_free_func (g_free);
	solution->values = g_ptr_array_new_with_free_func (g_free);
	solution->solution_index = -1;

	return solution;
}

void
tracker_solution_add_column_name (TrackerSolution *solution,
                                  const gchar     *name)
{
	g_ptr_array_add (solution->columns, g_strdup (name));
}

void
tracker_solution_add_value (TrackerSolution *solution,
                            const gchar     *str)
{
	g_ptr_array_add (solution->values, g_strdup (str));
}

gboolean
tracker_solution_next (TrackerSolution *solution)
{
	solution->solution_index++;
	return solution->solution_index * solution->n_cols < solution->values->len;
}

void
tracker_solution_rewind (TrackerSolution *solution)
{
	solution->solution_index = -1;
}

void
tracker_solution_free (TrackerSolution *solution)
{
	g_ptr_array_unref (solution->columns);
	g_ptr_array_unref (solution->values);
	g_free (solution);
}

GHashTable *
tracker_solution_get_bindings (TrackerSolution *solution)
{
	GHashTable *ht;
	gint i;

	ht = g_hash_table_new (g_str_hash, g_str_equal);

	for (i = 0; i < solution->columns->len; i++) {
		gint values_pos = solution->solution_index * solution->n_cols + i;
		gchar *name, *value;

		if (values_pos >= solution->values->len)
			break;

		name = g_ptr_array_index (solution->columns, i);
		value = g_ptr_array_index (solution->values, values_pos);
		g_hash_table_insert (ht, name, value);
	}

	return ht;
}

/* Data binding */
G_DEFINE_ABSTRACT_TYPE (TrackerBinding, tracker_binding, G_TYPE_OBJECT)

static void
tracker_binding_finalize (GObject *object)
{
	TrackerBinding *binding = TRACKER_BINDING (object);

	g_free (binding->sql_db_column_name);
	g_free (binding->sql_expression);

	G_OBJECT_CLASS (tracker_binding_parent_class)->finalize (object);
}

static void
tracker_binding_class_init (TrackerBindingClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = tracker_binding_finalize;
}

static void
tracker_binding_init (TrackerBinding *binding)
{
}

TrackerDataTable *
tracker_binding_get_table (TrackerBinding *binding)
{
	return binding->table;
}

void
tracker_binding_set_db_column_name (TrackerBinding *binding,
				    const gchar    *column_name)
{
	g_free (binding->sql_db_column_name);
	binding->sql_db_column_name = g_strdup (column_name);
}

void
tracker_binding_set_sql_expression (TrackerBinding *binding,
				    const gchar    *sql_expression)
{
	g_free (binding->sql_expression);
	binding->sql_expression = g_strdup (sql_expression);
}

const gchar *
tracker_binding_get_sql_expression (TrackerBinding *binding)
{
	if (!binding->sql_expression && binding->table) {
		binding->sql_expression =  g_strdup_printf ("\"%s\".\"%s\"",
							    binding->table->sql_query_tablename,
							    binding->sql_db_column_name);
	}

	return binding->sql_expression;
}

gchar *
tracker_binding_get_extra_sql_expression (TrackerBinding *binding,
                                          const gchar    *suffix)
{
	return g_strdup_printf ("\"%s\".\"%s:%s\"",
				binding->table->sql_query_tablename,
				binding->sql_db_column_name,
				suffix);
}

void
tracker_binding_set_data_type (TrackerBinding      *binding,
                               TrackerPropertyType  property_type)
{
	binding->data_type = property_type;
}

/* Literal binding */
G_DEFINE_TYPE (TrackerLiteralBinding, tracker_literal_binding, TRACKER_TYPE_BINDING)

static void
tracker_literal_binding_finalize (GObject *object)
{
	TrackerLiteralBinding *binding = TRACKER_LITERAL_BINDING (object);

	g_free (binding->literal);

	G_OBJECT_CLASS (tracker_literal_binding_parent_class)->finalize (object);
}

static void
tracker_literal_binding_class_init (TrackerLiteralBindingClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = tracker_literal_binding_finalize;
}

static void
tracker_literal_binding_init (TrackerLiteralBinding *binding)
{
}

TrackerBinding *
tracker_literal_binding_new (const gchar      *literal,
			     TrackerDataTable *table)
{
	TrackerBinding *binding;

	binding = g_object_new (TRACKER_TYPE_LITERAL_BINDING, NULL);
	binding->table = table;
	TRACKER_LITERAL_BINDING (binding)->literal = g_strdup (literal);

	return binding;
}

/* Parameter binding */
G_DEFINE_TYPE (TrackerParameterBinding, tracker_parameter_binding, TRACKER_TYPE_LITERAL_BINDING)

static void
tracker_parameter_binding_finalize (GObject *object)
{
	TrackerParameterBinding *binding = TRACKER_PARAMETER_BINDING (object);

	g_free (binding->name);

	G_OBJECT_CLASS (tracker_parameter_binding_parent_class)->finalize (object);
}

static void
tracker_parameter_binding_class_init (TrackerParameterBindingClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = tracker_parameter_binding_finalize;
}

static void
tracker_parameter_binding_init (TrackerParameterBinding *binding)
{
}

TrackerBinding *
tracker_parameter_binding_new (const gchar      *name,
			       TrackerDataTable *table)
{
	TrackerBinding *binding;

	binding = g_object_new (TRACKER_TYPE_PARAMETER_BINDING, NULL);
	binding->table = table;
	TRACKER_PARAMETER_BINDING (binding)->name = g_strdup (name);

	return binding;
}

/* Variable binding */
G_DEFINE_TYPE (TrackerVariableBinding, tracker_variable_binding, TRACKER_TYPE_BINDING)

static void
tracker_variable_binding_class_init (TrackerVariableBindingClass *klass)
{
}

static void
tracker_variable_binding_init (TrackerVariableBinding *binding)
{
}

TrackerBinding *
tracker_variable_binding_new (TrackerVariable  *variable,
			      TrackerClass     *type,
			      TrackerDataTable *table)
{
	TrackerBinding *binding;

	binding = g_object_new (TRACKER_TYPE_VARIABLE_BINDING, NULL);
	binding->table = table;
	TRACKER_VARIABLE_BINDING (binding)->type = type;
	TRACKER_VARIABLE_BINDING (binding)->variable = variable;

	return binding;
}

void
tracker_variable_binding_set_nullable (TrackerVariableBinding *binding,
				       gboolean                nullable)
{
	binding->nullable = !!nullable;
}

gboolean
tracker_variable_binding_get_nullable (TrackerVariableBinding *binding)
{
	return binding->nullable;
}

TrackerVariable *
tracker_variable_binding_get_variable (TrackerVariableBinding *binding)
{
	return binding->variable;
}

TrackerClass *
tracker_variable_binding_get_class (TrackerVariableBinding *binding)
{
	return binding->type;
}

/* Path element */
static void
tracker_path_element_free (TrackerPathElement *elem)
{
	g_free (elem->name);
	g_free (elem);
}

TrackerPathElement *
tracker_path_element_property_new (TrackerPathOperator  op,
                                   TrackerProperty     *prop)
{
	TrackerPathElement *elem;

	g_return_val_if_fail (TRACKER_IS_PROPERTY (prop), NULL);
	g_return_val_if_fail (op == TRACKER_PATH_OPERATOR_NONE ||
	                      op == TRACKER_PATH_OPERATOR_NEGATED, NULL);

	elem = g_new0 (TrackerPathElement, 1);
	elem->op = op;
	elem->type = tracker_property_get_data_type (prop);
	elem->data.property = prop;

	return elem;
}

TrackerPathElement *
tracker_path_element_operator_new (TrackerPathOperator  op,
                                   TrackerPathElement  *child1,
                                   TrackerPathElement  *child2)
{
	TrackerPathElement *elem;

	g_return_val_if_fail (op != TRACKER_PATH_OPERATOR_NONE &&
	                      op != TRACKER_PATH_OPERATOR_NEGATED, NULL);
	g_return_val_if_fail (child1 != NULL, NULL);
	g_return_val_if_fail (child2 == NULL ||
	                      op == TRACKER_PATH_OPERATOR_SEQUENCE ||
	                      op == TRACKER_PATH_OPERATOR_ALTERNATIVE ||
	                      op == TRACKER_PATH_OPERATOR_INTERSECTION, NULL);

	elem = g_new0 (TrackerPathElement, 1);
	elem->op = op;
	elem->data.composite.child1 = child1;
	elem->data.composite.child2 = child2;
	elem->type = child2 ? child2->type : child1->type;

	return elem;
}

static void
tracker_path_element_set_unique_name (TrackerPathElement *elem,
                                      gint                id)
{
	const gchar *name = NULL;

	switch (elem->op) {
	case TRACKER_PATH_OPERATOR_NONE:
		name = tracker_property_get_name (elem->data.property);
		break;
	case TRACKER_PATH_OPERATOR_INVERSE:
		name = "inv";
		break;
	case TRACKER_PATH_OPERATOR_SEQUENCE:
		name = "seq";
		break;
	case TRACKER_PATH_OPERATOR_ALTERNATIVE:
		name = "alt";
		break;
	case TRACKER_PATH_OPERATOR_ZEROORONE:
		name = "zeroorone";
		break;
	case TRACKER_PATH_OPERATOR_ZEROORMORE:
		name = "zeroormore";
		break;
	case TRACKER_PATH_OPERATOR_ONEORMORE:
		name = "oneormore";
		break;
	case TRACKER_PATH_OPERATOR_NEGATED:
		name = "neg";
		break;
	case TRACKER_PATH_OPERATOR_INTERSECTION:
		name = "intersect";
		break;
	default:
		g_assert_not_reached ();
	}

	elem->name = g_strdup_printf ("p%d_%s", id, name);
}

/* Context */
G_DEFINE_TYPE (TrackerContext, tracker_context, G_TYPE_INITIALLY_UNOWNED)

static void
tracker_context_finalize (GObject *object)
{
	TrackerContext *context = (TrackerContext *) object;

	while (context->children) {
		g_object_unref (context->children->data);
		context->children = g_list_delete_link (context->children,
		                                        context->children);
	}

	if (context->variable_set)
		g_hash_table_unref (context->variable_set);

	G_OBJECT_CLASS (tracker_context_parent_class)->finalize (object);
}

static void
tracker_context_class_init (TrackerContextClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = tracker_context_finalize;
}

static void
tracker_context_init (TrackerContext *context)
{
	context->variable_set = g_hash_table_new (tracker_variable_hash,
	                                          tracker_variable_equal);
}

TrackerContext *
tracker_context_new (void)
{
	return g_object_new (TRACKER_TYPE_CONTEXT, NULL);
}

void
tracker_context_set_parent (TrackerContext *context,
                            TrackerContext *parent)
{
	g_assert (context->parent == NULL);

	context->parent = parent;
	parent->children = g_list_append (parent->children,
	                                  g_object_ref_sink (context));
}

TrackerContext *
tracker_context_get_parent (TrackerContext *context)
{
	return context->parent;
}

void
tracker_context_add_variable_ref (TrackerContext  *context,
				  TrackerVariable *variable)
{
	g_hash_table_add (context->variable_set, variable);
}

gboolean
tracker_context_lookup_variable_ref (TrackerContext  *context,
                                     TrackerVariable *variable)
{
	return g_hash_table_lookup (context->variable_set, variable) != NULL;
}

void
tracker_context_propagate_variables (TrackerContext *context)
{
	GHashTableIter iter;
	gpointer key;

	g_assert (context->parent != NULL);
	g_hash_table_iter_init (&iter, context->variable_set);

	while (g_hash_table_iter_next (&iter, &key, NULL))
		g_hash_table_add (context->parent->variable_set, key);
}

/* Select context */
G_DEFINE_TYPE (TrackerSelectContext, tracker_select_context, TRACKER_TYPE_CONTEXT)

static void
tracker_select_context_finalize (GObject *object)
{
	TrackerSelectContext *context = TRACKER_SELECT_CONTEXT (object);

	g_clear_pointer (&context->variables, g_hash_table_unref);
	g_clear_pointer (&context->generated_variables, g_ptr_array_unref);
	g_clear_pointer (&context->literal_bindings, g_ptr_array_unref);
	g_clear_pointer (&context->path_elements, g_ptr_array_unref);

	G_OBJECT_CLASS (tracker_select_context_parent_class)->finalize (object);
}

static void
tracker_select_context_class_init (TrackerSelectContextClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = tracker_select_context_finalize;
}

static void
tracker_select_context_init (TrackerSelectContext *context)
{
}

TrackerContext *
tracker_select_context_new (void)
{
	return g_object_new (TRACKER_TYPE_SELECT_CONTEXT, NULL);
}

TrackerVariable *
tracker_select_context_lookup_variable (TrackerSelectContext *context,
                                        const gchar          *name)
{
	if (!context->variables)
		return NULL;
	return g_hash_table_lookup (context->variables, name);
}

TrackerVariable *
tracker_select_context_ensure_variable (TrackerSelectContext *context,
                                        const gchar          *name)
{
	TrackerVariable *variable;

	/* All variables are reserved to the root context */
	g_assert (TRACKER_CONTEXT (context)->parent == NULL);

	if (!context->variables) {
		context->variables =
			g_hash_table_new_full (g_str_hash, g_str_equal, NULL,
					       (GDestroyNotify) tracker_variable_free);
	}

	variable = g_hash_table_lookup (context->variables, name);

	if (!variable) {
		variable = tracker_variable_new ("v", name);
		g_hash_table_insert (context->variables, variable->name, variable);
	}

	return variable;
}

TrackerVariable *
tracker_select_context_add_generated_variable (TrackerSelectContext *context)
{
	TrackerVariable *variable;
	gchar *name;

	/* All variables are reserved to the root context */
	g_assert (TRACKER_CONTEXT (context)->parent == NULL);

	if (!context->generated_variables) {
		context->generated_variables =
			g_ptr_array_new_with_free_func ((GDestroyNotify) tracker_variable_free);
	}

	name = g_strdup_printf ("%d", context->generated_variables->len + 1);
	variable = tracker_variable_new ("g", name);
	g_free (name);

	g_ptr_array_add (context->generated_variables, variable);

	return variable;
}

void
tracker_select_context_add_literal_binding (TrackerSelectContext  *context,
                                            TrackerLiteralBinding *binding)
{
	gint i;

	/* Literal bindings are reserved to the root context */
	g_assert (TRACKER_CONTEXT (context)->parent == NULL);

	if (!context->literal_bindings)
		context->literal_bindings = g_ptr_array_new_with_free_func (g_object_unref);

	for (i = 0; i < context->literal_bindings->len; i++) {
		if (binding == g_ptr_array_index (context->literal_bindings, i))
			return;
	}

	g_ptr_array_add (context->literal_bindings, g_object_ref (binding));
}

guint
tracker_select_context_get_literal_binding_index (TrackerSelectContext  *context,
                                                  TrackerLiteralBinding *binding)
{
	guint i;

	for (i = 0; i < context->literal_bindings->len; i++) {
		if (binding == g_ptr_array_index (context->literal_bindings, i))
			return i;
	}

	g_assert_not_reached ();
	return -1;
}

void
tracker_select_context_add_path_element (TrackerSelectContext *context,
                                         TrackerPathElement   *path_elem)
{
	if (!context->path_elements) {
		context->path_elements =
			g_ptr_array_new_with_free_func ((GDestroyNotify) tracker_path_element_free);
	}

	g_ptr_array_add (context->path_elements, path_elem);
	tracker_path_element_set_unique_name (path_elem,
	                                      context->path_elements->len);
}

TrackerPathElement *
tracker_select_context_lookup_path_element_for_property (TrackerSelectContext *context,
                                                         TrackerProperty      *property)
{
	guint i;

	if (!context->path_elements)
		return NULL;

	for (i = 0; i < context->path_elements->len; i++) {
		TrackerPathElement *path_elem;

		path_elem = g_ptr_array_index (context->path_elements, i);

		if (path_elem->op == TRACKER_PATH_OPERATOR_NONE &&
		    path_elem->data.property == property)
			return path_elem;
	}

	return NULL;
}

/* Triple context */
G_DEFINE_TYPE (TrackerTripleContext, tracker_triple_context, TRACKER_TYPE_CONTEXT)

static void
tracker_triple_context_finalize (GObject *object)
{
	TrackerTripleContext *context = TRACKER_TRIPLE_CONTEXT (object);

	g_ptr_array_unref (context->sql_tables);
	g_ptr_array_unref (context->literal_bindings);
	g_hash_table_unref (context->variable_bindings);

	G_OBJECT_CLASS (tracker_triple_context_parent_class)->finalize (object);
}

static void
tracker_triple_context_class_init (TrackerTripleContextClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = tracker_triple_context_finalize;
}

static void
tracker_triple_context_init (TrackerTripleContext *context)
{
	context->sql_tables = g_ptr_array_new_with_free_func ((GDestroyNotify) tracker_data_table_free);
	context->literal_bindings = g_ptr_array_new_with_free_func (g_object_unref);
	context->variable_bindings =
		g_hash_table_new_full (tracker_variable_hash,
		                       tracker_variable_equal, NULL,
				       (GDestroyNotify) g_ptr_array_unref);
}

TrackerContext *
tracker_triple_context_new (void)
{
	return g_object_new (TRACKER_TYPE_TRIPLE_CONTEXT, NULL);
}

TrackerDataTable *
tracker_triple_context_lookup_table (TrackerTripleContext *context,
                                     const gchar          *graph,
                                     const gchar          *tablename)
{
	TrackerDataTable *table = NULL;
	guint i;

	for (i = 0; i < context->sql_tables->len; i++) {
		TrackerDataTable *table;

		table = g_ptr_array_index (context->sql_tables, i);

		if (g_strcmp0 (table->graph, graph) == 0 &&
		    g_strcmp0 (table->sql_db_tablename, tablename) == 0)
			return table;
	}

	return table;
}

TrackerDataTable *
tracker_triple_context_add_table (TrackerTripleContext *context,
                                  const gchar          *graph,
                                  const gchar          *tablename)
{
	TrackerDataTable *table;

	table = tracker_data_table_new (tablename, graph, ++context->table_counter);
	g_ptr_array_add (context->sql_tables, table);

	return table;
}

void
tracker_triple_context_add_literal_binding (TrackerTripleContext  *context,
                                            TrackerLiteralBinding *binding)
{
	g_ptr_array_add (context->literal_bindings, g_object_ref (binding));
}

GPtrArray *
tracker_triple_context_lookup_variable_binding_list (TrackerTripleContext *context,
						     TrackerVariable      *variable)
{
	return g_hash_table_lookup (context->variable_bindings, variable);
}

GPtrArray *
tracker_triple_context_get_variable_binding_list (TrackerTripleContext *context,
                                                  TrackerVariable      *variable)
{
	GPtrArray *binding_list = NULL;

	binding_list = g_hash_table_lookup (context->variable_bindings, variable);

	if (!binding_list) {
		TrackerContext *current_context = (TrackerContext *) context;
		TrackerContext *parent_context;

		binding_list = g_ptr_array_new_with_free_func (g_object_unref);
		g_hash_table_insert (context->variable_bindings, variable, binding_list);

		if (tracker_variable_has_bindings (variable)) {
			/* might be in scalar subquery: check variables of outer queries */
			while (current_context) {
				parent_context = tracker_context_get_parent (current_context);

				/* only allow access to variables of immediate parent context of the subquery
				 * allowing access to other variables leads to invalid SQL or wrong results
				 */
				if (TRACKER_IS_SELECT_CONTEXT (current_context) &&
				    tracker_context_get_parent (current_context) &&
				    g_hash_table_lookup (parent_context->variable_set, variable)) {
					TrackerVariableBinding *sample;
					TrackerBinding *binding;

					sample = tracker_variable_get_sample_binding (variable);
					binding = tracker_variable_binding_new (variable, sample->type,
										tracker_binding_get_table (TRACKER_BINDING (sample)));
					tracker_binding_set_sql_expression (binding,
									    tracker_variable_get_sql_expression (variable));
					tracker_binding_set_data_type (binding,
								       TRACKER_BINDING (sample)->data_type);
					g_ptr_array_add (binding_list, binding);
					break;
				}

				current_context = parent_context;
			}
		}
	}

	return binding_list;
}

void
tracker_triple_context_add_variable_binding (TrackerTripleContext   *context,
                                             TrackerVariable        *variable,
                                             TrackerVariableBinding *binding)
{
	GPtrArray *binding_list;

	binding_list = tracker_triple_context_get_variable_binding_list (context,
	                                                                 variable);
	g_ptr_array_add (binding_list, g_object_ref (binding));
}
