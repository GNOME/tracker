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

#ifndef __TRACKER_SPARQL_TYPES_H__
#define __TRACKER_SPARQL_TYPES_H__

#include "tracker-ontologies.h"

#define TRACKER_TYPE_BINDING  (tracker_binding_get_type ())
#define TRACKER_BINDING(o)    (G_TYPE_CHECK_INSTANCE_CAST ((o), TRACKER_TYPE_BINDING, TrackerBinding))
#define TRACKER_IS_BINDING(o) (G_TYPE_CHECK_INSTANCE_TYPE ((o), TRACKER_TYPE_BINDING))

#define TRACKER_TYPE_LITERAL_BINDING  (tracker_literal_binding_get_type ())
#define TRACKER_LITERAL_BINDING(o)    (G_TYPE_CHECK_INSTANCE_CAST ((o), TRACKER_TYPE_LITERAL_BINDING, TrackerLiteralBinding))
#define TRACKER_IS_LITERAL_BINDING(o) (G_TYPE_CHECK_INSTANCE_TYPE ((o), TRACKER_TYPE_LITERAL_BINDING))

#define TRACKER_TYPE_PARAMETER_BINDING  (tracker_parameter_binding_get_type ())
#define TRACKER_PARAMETER_BINDING(o)    (G_TYPE_CHECK_INSTANCE_CAST ((o), TRACKER_TYPE_PARAMETER_BINDING, TrackerParameterBinding))
#define TRACKER_IS_PARAMETER_BINDING(o) (G_TYPE_CHECK_INSTANCE_TYPE ((o), TRACKER_TYPE_PARAMETER_BINDING))

#define TRACKER_TYPE_VARIABLE_BINDING  (tracker_variable_binding_get_type ())
#define TRACKER_VARIABLE_BINDING(o)    (G_TYPE_CHECK_INSTANCE_CAST ((o), TRACKER_TYPE_VARIABLE_BINDING, TrackerVariableBinding))
#define TRACKER_IS_VARIABLE_BINDING(o) (G_TYPE_CHECK_INSTANCE_TYPE ((o), TRACKER_TYPE_VARIABLE_BINDING))

#define TRACKER_TYPE_CONTEXT  (tracker_context_get_type ())
#define TRACKER_CONTEXT(o)    (G_TYPE_CHECK_INSTANCE_CAST ((o), TRACKER_TYPE_CONTEXT, TrackerContext))
#define TRACKER_IS_CONTEXT(o) (G_TYPE_CHECK_INSTANCE_TYPE ((o), TRACKER_TYPE_CONTEXT))

#define TRACKER_TYPE_SELECT_CONTEXT  (tracker_select_context_get_type ())
#define TRACKER_SELECT_CONTEXT(o)    (G_TYPE_CHECK_INSTANCE_CAST ((o), TRACKER_TYPE_SELECT_CONTEXT, TrackerSelectContext))
#define TRACKER_IS_SELECT_CONTEXT(o) (G_TYPE_CHECK_INSTANCE_TYPE ((o), TRACKER_TYPE_SELECT_CONTEXT))

#define TRACKER_TYPE_TRIPLE_CONTEXT (tracker_triple_context_get_type ())
#define TRACKER_TRIPLE_CONTEXT(o)    (G_TYPE_CHECK_INSTANCE_CAST ((o), TRACKER_TYPE_TRIPLE_CONTEXT, TrackerTripleContext))
#define TRACKER_IS_TRIPLE_CONTEXT(o) (G_TYPE_CHECK_INSTANCE_TYPE ((o), TRACKER_TYPE_TRIPLE_CONTEXT))

typedef struct _TrackerBinding TrackerBinding;
typedef struct _TrackerBindingClass TrackerBindingClass;
typedef struct _TrackerLiteralBinding TrackerLiteralBinding;
typedef struct _TrackerLiteralBindingClass TrackerLiteralBindingClass;
typedef struct _TrackerParameterBinding TrackerParameterBinding;
typedef struct _TrackerParameterBindingClass TrackerParameterBindingClass;
typedef struct _TrackerVariableBinding TrackerVariableBinding;
typedef struct _TrackerVariableBindingClass TrackerVariableBindingClass;
typedef struct _TrackerContext TrackerContext;
typedef struct _TrackerContextClass TrackerContextClass;
typedef struct _TrackerSelectContext TrackerSelectContext;
typedef struct _TrackerSelectContextClass TrackerSelectContextClass;
typedef struct _TrackerTripleContext TrackerTripleContext;
typedef struct _TrackerTripleContextClass TrackerTripleContextClass;

typedef struct _TrackerDataTable TrackerDataTable;
typedef struct _TrackerVariable TrackerVariable;
typedef struct _TrackerToken TrackerToken;
typedef struct _TrackerSolution TrackerSolution;
typedef struct _TrackerPathElement TrackerPathElement;

struct _TrackerDataTable {
	gchar *graph; /* Graph for this table, if specified */
	gchar *sql_db_tablename; /* as in db schema */
	gchar *sql_query_tablename; /* temp. name, generated */
	gboolean predicate_variable;
};

struct _TrackerBinding {
	GObject parent_instance;
	TrackerPropertyType data_type;
	TrackerDataTable *table;
	gchar *sql_db_column_name;
	gchar *sql_expression;
};

struct _TrackerBindingClass {
	GObjectClass parent_class;
};

/* Represents a mapping of a SPARQL literal to a SQL table and column */
struct _TrackerLiteralBinding {
	TrackerBinding parent_instance;
	GBytes *bytes;
	const gchar *literal;
};

struct _TrackerLiteralBindingClass {
	TrackerBindingClass parent_class;
};

/* Represents a mapping of a SPARQL parameter variable to a user-provided value */
struct _TrackerParameterBinding {
	TrackerLiteralBinding parent_instance;
	gchar *name;
};

struct _TrackerParameterBindingClass {
	TrackerLiteralBindingClass parent_class;
};

/* Represents a mapping of a SPARQL variable to a SQL table and column */
struct _TrackerVariableBinding {
	TrackerBinding parent_instance;
	TrackerVariable *variable;
	TrackerClass *type;
	guint nullable : 1;
};

struct _TrackerVariableBindingClass {
	TrackerBindingClass parent_class;
};

struct _TrackerVariable {
	gchar *name;
	gchar *sql_expression;
	TrackerVariableBinding *binding;
};

struct _TrackerToken {
	guint type;
	union {
		GBytes *literal;
		gchar *parameter;
		TrackerVariable *var;
		TrackerPathElement *path;
	} content;
};

struct _TrackerSolution {
	GPtrArray *columns;
	GPtrArray *values;
	int solution_index;
	int n_cols;
};

typedef enum {
	TRACKER_PATH_OPERATOR_NONE,
	TRACKER_PATH_OPERATOR_INVERSE,     /* ^ */
	TRACKER_PATH_OPERATOR_SEQUENCE,    /* / */
	TRACKER_PATH_OPERATOR_ALTERNATIVE, /* | */
	TRACKER_PATH_OPERATOR_ZEROORONE,   /* ? */
	TRACKER_PATH_OPERATOR_ONEORMORE,   /* + */
	TRACKER_PATH_OPERATOR_ZEROORMORE,  /* * */
	TRACKER_PATH_OPERATOR_NEGATED,     /* ! */
	TRACKER_PATH_OPERATOR_INTERSECTION, /* Used for negated paths */
} TrackerPathOperator;

struct _TrackerPathElement {
	TrackerPathOperator op;
	TrackerPropertyType type;
	gchar *name;

	union {
		TrackerProperty *property;
		struct {
			TrackerPathElement *child1;
			TrackerPathElement *child2;
		} composite;
	} data;
};

struct _TrackerContext {
	GInitiallyUnowned parent_instance;
	TrackerContext *parent;
	GList *children;

	/* Variables used in this context, these will be owned by the
	 * root select context.
	 */
	GHashTable *variable_set;
};

struct _TrackerContextClass {
	GInitiallyUnownedClass parent_class;
};

struct _TrackerSelectContext {
	TrackerContext parent_instance;

	/* All variables declared from this context. All these TrackerVariables
	 * are shared with children contexts. Only the root context has contents
	 * here.
	 */
	GHashTable *variables; /* string -> TrackerVariable */

	GPtrArray *generated_variables;

	/* SPARQL literals. Content is TrackerLiteralBinding */
	GPtrArray *literal_bindings;

	/* Counter for sqlite3_stmt query bindings */
	gint binding_counter;

	/* Type to propagate upwards */
	TrackerPropertyType type;

	/* Property path elements */
	GPtrArray *path_elements;
};

struct _TrackerSelectContextClass {
	TrackerContextClass parent_class;
};

struct _TrackerTripleContext {
	TrackerContext parent_instance;

	/* Data tables pulled by the bindings below */
	GPtrArray *sql_tables;

	/* SPARQL literals. Content is TrackerLiteralBinding */
	GPtrArray *literal_bindings;

	/* SPARQL variables. */
	GHashTable *variable_bindings; /* TrackerVariable -> GPtrArray(TrackerVariableBinding) */

	/* Counter for disambiguating table names in queries */
	gint table_counter;
};

struct _TrackerTripleContextClass {
	TrackerContextClass parent_class;
};

/* Data table */
void tracker_data_table_set_predicate_variable (TrackerDataTable *table,
                                                gboolean          is_variable);

/* Binding */
GType              tracker_binding_get_type (void) G_GNUC_CONST;
TrackerDataTable * tracker_binding_get_table (TrackerBinding   *binding);

void tracker_binding_set_db_column_name (TrackerBinding *binding,
					 const gchar    *column_name);

void tracker_binding_set_sql_expression (TrackerBinding *binding,
					 const gchar    *sql_expression);

void tracker_binding_set_data_type (TrackerBinding      *binding,
				    TrackerPropertyType  type);

const gchar * tracker_binding_get_sql_expression (TrackerBinding *binding);
gchar * tracker_binding_get_extra_sql_expression (TrackerBinding *binding,
						  const gchar    *suffix);

/* Literal binding */
GType            tracker_literal_binding_get_type (void) G_GNUC_CONST;
TrackerBinding * tracker_literal_binding_new (GBytes           *bytes,
					      TrackerDataTable *table);

/* Parameter binding */
GType            tracker_parameter_binding_get_type (void) G_GNUC_CONST;
TrackerBinding * tracker_parameter_binding_new (const gchar      *name,
						TrackerDataTable *table);

/* Variable binding */
GType            tracker_variable_binding_get_type (void) G_GNUC_CONST;
TrackerBinding * tracker_variable_binding_new (TrackerVariable  *variable,
					       TrackerClass     *class,
					       TrackerDataTable *table);
void tracker_variable_binding_set_nullable (TrackerVariableBinding *binding,
					    gboolean                nullable);
gboolean tracker_variable_binding_get_nullable (TrackerVariableBinding *binding);
TrackerVariable * tracker_variable_binding_get_variable (TrackerVariableBinding *binding);

/* Variable */
void    tracker_variable_set_sql_expression       (TrackerVariable *variable,
						   const gchar     *sql_expression);
const gchar * tracker_variable_get_sql_expression (TrackerVariable *variable);
gchar * tracker_variable_get_extra_sql_expression (TrackerVariable *variable,
						   const gchar     *suffix);

gboolean tracker_variable_has_bindings            (TrackerVariable        *variable);
void    tracker_variable_set_sample_binding       (TrackerVariable        *variable,
						   TrackerVariableBinding *binding);
TrackerVariableBinding * tracker_variable_get_sample_binding (TrackerVariable *variable);

/* Token */
void tracker_token_literal_init  (TrackerToken    *token,
                                  const gchar     *literal,
                                  gssize           len);
void tracker_token_variable_init (TrackerToken    *token,
                                  TrackerVariable *variable);
void tracker_token_parameter_init (TrackerToken   *token,
				   const gchar    *parameter);
void tracker_token_path_init      (TrackerToken       *token,
                                   TrackerPathElement *path_elem);
void tracker_token_unset (TrackerToken *token);

gboolean           tracker_token_is_empty     (TrackerToken *token);
GBytes           * tracker_token_get_literal  (TrackerToken *token);
TrackerVariable  * tracker_token_get_variable (TrackerToken *token);
const gchar      * tracker_token_get_idstring (TrackerToken *token);
const gchar      * tracker_token_get_parameter (TrackerToken *token);
TrackerPathElement * tracker_token_get_path   (TrackerToken *token);

/* Solution */
TrackerSolution * tracker_solution_new       (guint            n_cols);
void              tracker_solution_free      (TrackerSolution *solution);
gboolean          tracker_solution_next      (TrackerSolution *solution);
void              tracker_solution_rewind    (TrackerSolution *solution);

void              tracker_solution_add_column_name (TrackerSolution *solution,
                                                    const gchar     *str);
void              tracker_solution_add_value       (TrackerSolution *solution,
                                                    const gchar     *str);
GHashTable      * tracker_solution_get_bindings    (TrackerSolution *solution);

/* Property path element */
TrackerPathElement * tracker_path_element_property_new (TrackerPathOperator  op,
                                                        TrackerProperty     *prop);
TrackerPathElement * tracker_path_element_operator_new (TrackerPathOperator  op,
                                                        TrackerPathElement  *child1,
                                                        TrackerPathElement  *child2);

/* Context */
GType            tracker_context_get_type   (void) G_GNUC_CONST;
TrackerContext * tracker_context_new        (void);
void             tracker_context_set_parent (TrackerContext *context,
                                             TrackerContext *parent);
TrackerContext * tracker_context_get_parent (TrackerContext *context);

void tracker_context_propagate_variables (TrackerContext *context);
void tracker_context_add_variable_ref    (TrackerContext  *context,
					  TrackerVariable *variable);
gboolean tracker_context_lookup_variable_ref (TrackerContext  *context,
                                              TrackerVariable *variable);

/* Select context */
GType            tracker_select_context_get_type (void) G_GNUC_CONST;
TrackerContext * tracker_select_context_new (void);
TrackerVariable * tracker_select_context_lookup_variable (TrackerSelectContext *context,
                                                          const gchar          *name);
TrackerVariable * tracker_select_context_ensure_variable (TrackerSelectContext *context,
							  const gchar          *name);
TrackerVariable * tracker_select_context_add_generated_variable (TrackerSelectContext *context);

void tracker_select_context_add_literal_binding (TrackerSelectContext  *context,
                                                 TrackerLiteralBinding *binding);
guint tracker_select_context_get_literal_binding_index (TrackerSelectContext  *context,
                                                        TrackerLiteralBinding *binding);
void tracker_select_context_add_path_element (TrackerSelectContext *context,
                                              TrackerPathElement   *path_elem);
TrackerPathElement *
     tracker_select_context_lookup_path_element_for_property (TrackerSelectContext *context,
                                                              TrackerProperty      *property);

/* Triple context */
GType            tracker_triple_context_get_type (void) G_GNUC_CONST;
TrackerContext * tracker_triple_context_new (void);

TrackerDataTable * tracker_triple_context_lookup_table (TrackerTripleContext *context,
                                                        const gchar          *graph,
                                                        const gchar          *table);
TrackerDataTable * tracker_triple_context_add_table    (TrackerTripleContext *context,
                                                        const gchar          *graph,
                                                        const gchar          *table);
void tracker_triple_context_add_literal_binding  (TrackerTripleContext   *context,
						  TrackerLiteralBinding  *binding);
void tracker_triple_context_add_variable_binding (TrackerTripleContext   *context,
						  TrackerVariable        *variable,
						  TrackerVariableBinding *binding);
GPtrArray * tracker_triple_context_lookup_variable_binding_list (TrackerTripleContext *context,
								 TrackerVariable      *variable);
GPtrArray * tracker_triple_context_get_variable_binding_list (TrackerTripleContext *context,
							      TrackerVariable      *variable);

#endif /* __TRACKER_SPARQL_TYPES_H__ */
