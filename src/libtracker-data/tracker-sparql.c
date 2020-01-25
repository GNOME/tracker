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

#include <glib-object.h>
#include "tracker-data-query.h"
#include "tracker-string-builder.h"
#include "tracker-sparql.h"
#include "tracker-sparql-types.h"
#include "tracker-sparql-parser.h"
#include "tracker-sparql-grammar.h"
#include "tracker-collation.h"
#include "tracker-db-interface-sqlite.h"
#include "tracker-utils.h"

#define TRACKER_NS "http://tracker.api.gnome.org/ontology/v3/tracker#"
#define RDF_NS "http://www.w3.org/1999/02/22-rdf-syntax-ns#"
#define RDFS_NS "http://www.w3.org/2000/01/rdf-schema#"
#define FTS_NS "http://tracker.api.gnome.org/ontology/v3/fts#"
#define XSD_NS "http://www.w3.org/2001/XMLSchema#"
#define FN_NS "http://www.w3.org/2005/xpath-functions#"

/* FIXME: This should be dependent on SQLITE_LIMIT_VARIABLE_NUMBER */
#define MAX_VARIABLES 999

enum {
	TIME_FORMAT_SECONDS,
	TIME_FORMAT_MINUTES,
	TIME_FORMAT_HOURS
};

enum {
	GRAPH_OP_DEFAULT,
	GRAPH_OP_NAMED,
	GRAPH_OP_ALL
};

static inline gboolean _call_rule_func (TrackerSparql            *sparql,
                                        TrackerGrammarNamedRule   rule,
                                        GError                  **error);
static gboolean handle_function_call (TrackerSparql  *sparql,
                                      GError        **error);
static gboolean helper_translate_date (TrackerSparql  *sparql,
                                       const gchar    *format,
                                       GError        **error);
static gboolean helper_translate_time (TrackerSparql  *sparql,
                                       guint           format,
                                       GError        **error);
static TrackerDBStatement * prepare_query (TrackerSparql         *sparql,
                                           TrackerDBInterface    *iface,
                                           TrackerStringBuilder  *str,
                                           GPtrArray             *literals,
                                           GHashTable            *parameters,
                                           gboolean               cached,
                                           GError               **error);
static inline TrackerVariable * _ensure_variable (TrackerSparql *sparql,
                                                  const gchar   *name);
static void convert_expression_to_string (TrackerSparql       *sparql,
                                          TrackerPropertyType  type);
static void _append_graph_checks (TrackerSparql *sparql,
                                  const gchar   *column_name,
                                  gboolean       add_unnamed_graph,
                                  GStrv          graphs,
                                  gint           len);

#define _raise(v,s,sub)   \
	G_STMT_START { \
	g_set_error (error, TRACKER_SPARQL_ERROR, \
	             TRACKER_SPARQL_ERROR_##v, \
	             s " '%s'", sub); \
	return FALSE; \
	} G_STMT_END

/* Added for control flow simplicity. All processing will be stopped
 * whenever any rule sets an error and returns FALSE.
 */
#define _call_rule(c,r,e) \
	G_STMT_START { \
	if (!_call_rule_func(c, r, e)) \
		return FALSE; \
	} G_STMT_END

typedef gboolean (* RuleTranslationFunc) (TrackerSparql  *sparql,
                                          GError        **error);

enum
{
	TRACKER_SPARQL_TYPE_SELECT,
	TRACKER_SPARQL_TYPE_DELETE,
	TRACKER_SPARQL_TYPE_INSERT,
	TRACKER_SPARQL_TYPE_UPDATE,
	TRACKER_SPARQL_TYPE_CONSTRUCT,
};

struct _TrackerSparql
{
	GObject parent_instance;
	TrackerDataManager *data_manager;
	gchar *sparql;

	TrackerNodeTree *tree;
	GError *parser_error;

	TrackerContext *context;
	TrackerStringBuilder *sql;

	GHashTable *prefix_map;
	GList *filter_clauses;

	GPtrArray *var_names;
	GArray *var_types;
	GHashTable *cached_bindings;

	GVariantBuilder *blank_nodes;
	GHashTable *solution_var_map;

	gboolean silent;
	gboolean cacheable;

	GHashTable *parameters;

	GPtrArray *anon_graphs;
	GPtrArray *named_graphs;
	gchar *base;

	struct {
		GPtrArray *graphs;
		GPtrArray *services;
		GHashTable *filtered_graphs;
		gboolean filter_unnamed_graph;
	} policy;

	struct {
		TrackerContext *context;
		TrackerContext *select_context;
		TrackerStringBuilder *sql;
		TrackerStringBuilder *with_clauses;
		TrackerStringBuilder *construct_query;
		TrackerParserNode *node;
		TrackerParserNode *prev_node;

		TrackerToken graph;
		TrackerToken subject;
		TrackerToken predicate;
		TrackerToken object;

		TrackerToken *token;

		TrackerPathElement *path;

		GHashTable *blank_node_map;
		GHashTable *update_blank_nodes;
		TrackerVariableBinding *as_in_group_by;

		GHashTable *union_views;

		const gchar *expression_list_separator;
		TrackerPropertyType expression_type;
		guint type;
		guint graph_op;
		gint values_idx;
		gint fts_match_idx;

		gboolean convert_to_string;
	} current_state;
};

G_DEFINE_TYPE (TrackerSparql, tracker_sparql, G_TYPE_OBJECT)

static void
tracker_sparql_finalize (GObject *object)
{
	TrackerSparql *sparql = TRACKER_SPARQL (object);

	g_object_unref (sparql->data_manager);
	g_hash_table_destroy (sparql->prefix_map);
	g_hash_table_destroy (sparql->parameters);
	g_hash_table_destroy (sparql->cached_bindings);


	if (sparql->sql)
		tracker_string_builder_free (sparql->sql);
	if (sparql->tree)
		tracker_node_tree_free (sparql->tree);

	g_clear_object (&sparql->context);

	/* Unset all possible current state (eg. after error) */
	tracker_token_unset (&sparql->current_state.graph);
	tracker_token_unset (&sparql->current_state.subject);
	tracker_token_unset (&sparql->current_state.predicate);
	tracker_token_unset (&sparql->current_state.object);
	g_clear_pointer (&sparql->current_state.blank_node_map,
	                 g_hash_table_unref);
	g_clear_pointer (&sparql->current_state.union_views, g_hash_table_unref);
	g_clear_pointer (&sparql->current_state.construct_query,
	                 tracker_string_builder_free);
	g_clear_object (&sparql->current_state.as_in_group_by);

	g_ptr_array_unref (sparql->named_graphs);
	g_ptr_array_unref (sparql->anon_graphs);
	g_ptr_array_unref (sparql->var_names);
	g_array_unref (sparql->var_types);
	g_free (sparql->base);

	g_clear_pointer (&sparql->policy.graphs, g_ptr_array_unref);
	g_clear_pointer (&sparql->policy.services, g_ptr_array_unref);
	g_clear_pointer (&sparql->policy.filtered_graphs, g_hash_table_unref);

	if (sparql->blank_nodes)
		g_variant_builder_unref (sparql->blank_nodes);

	g_free (sparql->sparql);

	G_OBJECT_CLASS (tracker_sparql_parent_class)->finalize (object);
}

static inline void
tracker_sparql_push_context (TrackerSparql  *sparql,
                             TrackerContext *context)
{
	if (sparql->current_state.context)
		tracker_context_set_parent (context, sparql->current_state.context);
	sparql->current_state.context = context;
}

static inline void
tracker_sparql_pop_context (TrackerSparql *sparql,
                            gboolean       propagate_variables)
{
	TrackerContext *parent;

	g_assert (sparql->current_state.context);

	parent = tracker_context_get_parent (sparql->current_state.context);

	if (parent && propagate_variables)
		tracker_context_propagate_variables (sparql->current_state.context);

	sparql->current_state.context = parent;
}

static inline TrackerStringBuilder *
tracker_sparql_swap_builder (TrackerSparql        *sparql,
                             TrackerStringBuilder *string)
{
	TrackerStringBuilder *old;

	old = sparql->current_state.sql;
	sparql->current_state.sql = string;

	return old;
}

static inline const gchar *
tracker_sparql_swap_current_expression_list_separator (TrackerSparql *sparql,
                                                       const gchar   *sep)
{
	const gchar *old;

	old = sparql->current_state.expression_list_separator;
	sparql->current_state.expression_list_separator = sep;

	return old;
}

static inline gchar *
tracker_sparql_expand_base (TrackerSparql *sparql,
                            const gchar   *term)
{
	if (sparql->base)
		return tracker_resolve_relative_uri (sparql->base, term);
	else
		return g_strdup (term);
}

static inline gchar *
tracker_sparql_expand_prefix (TrackerSparql *sparql,
                              const gchar   *term)
{
	const gchar *sep;
	gchar *ns, *expanded_ns;

	sep = strchr (term, ':');

	if (sep) {
		ns = g_strndup (term, sep - term);
		sep++;
	} else {
		ns = g_strdup (term);
	}

	expanded_ns = g_hash_table_lookup (sparql->prefix_map, ns);

	if (!expanded_ns && g_strcmp0 (ns, "fn") == 0)
		expanded_ns = FN_NS;

	if (!expanded_ns) {
		TrackerOntologies *ontologies;
		TrackerNamespace **namespaces;
		guint n_namespaces, i;

		ontologies = tracker_data_manager_get_ontologies (sparql->data_manager);
		namespaces = tracker_ontologies_get_namespaces (ontologies, &n_namespaces);

		for (i = 0; i < n_namespaces; i++) {
			if (!g_str_equal (ns, tracker_namespace_get_prefix (namespaces[i])))
				continue;

			expanded_ns = g_strdup (tracker_namespace_get_uri (namespaces[i]));
			g_hash_table_insert (sparql->prefix_map, g_strdup (ns), expanded_ns);
		}

		if (!expanded_ns) {
			g_free (ns);
			return NULL;
		}
	}

	g_free (ns);

	if (sep) {
		return g_strdup_printf ("%s%s", expanded_ns, sep);
	} else {
		return g_strdup (expanded_ns);
	}
}

static inline void
tracker_sparql_iter_next (TrackerSparql *sparql)
{
	sparql->current_state.prev_node = sparql->current_state.node;
	sparql->current_state.node =
		tracker_sparql_parser_tree_find_next (sparql->current_state.node, FALSE);
}

static inline gboolean
_check_in_rule (TrackerSparql           *sparql,
                TrackerGrammarNamedRule  named_rule)
{
	TrackerParserNode *node = sparql->current_state.node;
	const TrackerGrammarRule *rule;

	g_assert (named_rule < N_NAMED_RULES);

	if (!node)
		return FALSE;

	rule = tracker_parser_node_get_rule (node);

	return tracker_grammar_rule_is_a (rule, RULE_TYPE_RULE, named_rule);
}

static inline TrackerGrammarNamedRule
_current_rule (TrackerSparql *sparql)
{
	TrackerParserNode *parser_node = sparql->current_state.node;
	const TrackerGrammarRule *rule;

	if (!parser_node)
		return -1;
	rule = tracker_parser_node_get_rule (parser_node);
	if (rule->type != RULE_TYPE_RULE)
		return -1;

	return rule->data.rule;
}

static inline gboolean
_accept (TrackerSparql          *sparql,
         TrackerGrammarRuleType  type,
         guint                   value)
{
	TrackerParserNode *parser_node = sparql->current_state.node;
	const TrackerGrammarRule *rule;

	if (!parser_node)
		return FALSE;

	rule = tracker_parser_node_get_rule (parser_node);

	if (tracker_grammar_rule_is_a (rule, type, value)) {
		tracker_sparql_iter_next (sparql);
		return TRUE;
	}

	return FALSE;
}

static inline void
_expect (TrackerSparql          *sparql,
         TrackerGrammarRuleType  type,
         guint                   value)
{
	if (!_accept (sparql, type, value)) {
		TrackerParserNode *parser_node = sparql->current_state.node;
		const TrackerGrammarRule *rule = NULL;

		if (parser_node)
			rule = tracker_parser_node_get_rule (parser_node);

		if (type == RULE_TYPE_LITERAL) {
			if (rule) {
				g_error ("Parser expects literal '%s'. Got rule %d, value %d(%s)", literals[value],
				         rule->type, rule->data.literal, rule->string ? rule->string : "Unknown");
			} else {
				g_error ("Parser expects literal '%s'. Got EOF", literals[value]);
			}
		} else {
			if (rule) {
				g_error ("Parser expects rule %d (%d). Got rule %d, value %d(%s)", type, value,
				         rule->type, rule->data.literal, rule->string ? rule->string : "Unknown");
			} else {
				g_error ("Parser expects rule %d (%d). Got EOF", type, value);
			}
		}
	}
}

static inline void
_optional (TrackerSparql          *sparql,
           TrackerGrammarRuleType  type,
           guint                   value)
{
	(void) _accept (sparql, type, value);
}

static inline void
_step (TrackerSparql *sparql)
{
	tracker_sparql_iter_next (sparql);
}

static inline void
_prepend_string (TrackerSparql *sparql,
                 const gchar   *str)
{
	tracker_string_builder_prepend (sparql->current_state.sql, str, -1);
}

static inline TrackerStringBuilder *
_prepend_placeholder (TrackerSparql *sparql)
{
	return tracker_string_builder_prepend_placeholder (sparql->current_state.sql);
}

static inline void
_append_string (TrackerSparql *sparql,
                const gchar   *str)
{
	tracker_string_builder_append (sparql->current_state.sql, str, -1);
}

static inline void
_append_string_printf (TrackerSparql *sparql,
                       const gchar   *format,
                       ...)
{
	va_list varargs;

	va_start (varargs, format);
	tracker_string_builder_append_valist (sparql->current_state.sql, format, varargs);
	va_end (varargs);
}

static inline TrackerStringBuilder *
_append_placeholder (TrackerSparql *sparql)
{
	return tracker_string_builder_append_placeholder (sparql->current_state.sql);
}

static inline gchar *
_escape_sql_string (const gchar *str,
                    gchar        ch)
{
	int i, j, len;
	gchar *copy;

	len = strlen (str);
	copy = g_new (char, (len * 2) + 1);
	i = j = 0;

	while (i < len) {
		if (str[i] == ch) {
			copy[j] = ch;
			j++;
		}

		copy[j] = str[i];
		i++;
		j++;
	}

	copy[j] = '\0';

	return copy;
}

static inline void
_append_literal_sql (TrackerSparql         *sparql,
                     TrackerLiteralBinding *binding)
{
	guint idx;

	idx = tracker_select_context_get_literal_binding_index (TRACKER_SELECT_CONTEXT (sparql->context),
	                                                        binding);

	if (idx >= MAX_VARIABLES) {
		sparql->cacheable = FALSE;
	}

	if (TRACKER_BINDING (binding)->data_type == TRACKER_PROPERTY_TYPE_RESOURCE) {
		_append_string_printf (sparql,
		                       "COALESCE ((SELECT ID FROM Resource WHERE Uri = ");
	}

	if (!sparql->cacheable) {
		gchar *escaped, *full_str;

		_append_string (sparql, "\"");

		switch (TRACKER_BINDING (binding)->data_type) {
		case TRACKER_PROPERTY_TYPE_DATE:
			full_str = g_strdup_printf ("%sT00:00:00Z", binding->literal);
			escaped = _escape_sql_string (full_str, '"');
			_append_string (sparql, escaped);
			g_free (escaped);
			g_free (full_str);
			break;
		case TRACKER_PROPERTY_TYPE_DATETIME:
		case TRACKER_PROPERTY_TYPE_STRING:
		case TRACKER_PROPERTY_TYPE_LANGSTRING:
		case TRACKER_PROPERTY_TYPE_RESOURCE:
			escaped = _escape_sql_string (binding->literal, '"');
			_append_string (sparql, escaped);
			g_free (escaped);
			break;
		case TRACKER_PROPERTY_TYPE_BOOLEAN:
			if (g_str_equal (binding->literal, "1") ||
			    g_ascii_strcasecmp (binding->literal, "true") == 0) {
				_append_string (sparql, "1");
			} else {
				_append_string (sparql, "0");
			}
			break;
		case TRACKER_PROPERTY_TYPE_UNKNOWN:
		case TRACKER_PROPERTY_TYPE_INTEGER:
		case TRACKER_PROPERTY_TYPE_DOUBLE:
			_append_string (sparql, binding->literal);
			break;
		}

		_append_string (sparql, "\"");
	} else {
		_append_string_printf (sparql, "?%d ", idx + 1);
	}

	if (TRACKER_BINDING (binding)->data_type == TRACKER_PROPERTY_TYPE_RESOURCE)
		_append_string_printf (sparql, "), 0) ");
	if (TRACKER_BINDING (binding)->data_type == TRACKER_PROPERTY_TYPE_STRING ||
	    TRACKER_BINDING (binding)->data_type == TRACKER_PROPERTY_TYPE_LANGSTRING)
		_append_string (sparql, "COLLATE " TRACKER_COLLATION_NAME " ");
}

static void
_append_variable_sql (TrackerSparql   *sparql,
                      TrackerVariable *variable)
{
	_append_string_printf (sparql, "%s ",
	                       tracker_variable_get_sql_expression (variable));
}

static gchar *
build_properties_string_for_class (TrackerSparql *sparql,
                                   TrackerClass  *class,
                                   gint          *n_properties_ret)
{
	TrackerOntologies *ontologies;
	TrackerProperty **properties;
	guint n_properties, i, count = 0;
	GString *str;

	ontologies = tracker_data_manager_get_ontologies (sparql->data_manager);
	properties = tracker_ontologies_get_properties (ontologies, &n_properties);
	str = g_string_new (NULL);

	for (i = 0; i < n_properties; i++) {
		if (tracker_property_get_multiple_values (properties[i]))
			continue;

		if (tracker_property_get_domain (properties[i]) != class) {
			TrackerClass **domain_indexes;
			gboolean is_domain_index = FALSE;
			guint j;

			/* The property does not belong in this class, but could
			 * still be a domain index.
			 */
			domain_indexes = tracker_property_get_domain_indexes (properties[i]);
			for (j = 0; !is_domain_index && domain_indexes[j] != NULL; j++)
				is_domain_index = domain_indexes[j] == class;

			if (!is_domain_index)
				continue;
		}

		g_string_append_printf (str, "\"%s\",",
		                        tracker_property_get_name (properties[i]));
		count++;
	}

	*n_properties_ret = count;

	return g_string_free (str, FALSE);
}

static gchar *
build_properties_string (TrackerSparql   *sparql,
                         TrackerProperty *property,
                         gint            *n_properties)
{
	if (tracker_property_get_multiple_values (property)) {
		GString *str;

		str = g_string_new (NULL);
		g_string_append_printf (str, "\"%s\",",
		                        tracker_property_get_name (property));
		*n_properties = 1;
		return g_string_free (str, FALSE);
	} else {
		TrackerClass *class;

		class = tracker_property_get_domain (property);
		return build_properties_string_for_class (sparql, class, n_properties);
	}
}

static gboolean
tracker_sparql_graph_is_whitelisted (TrackerSparql *sparql,
                                     const gchar   *graph)
{
	gint i;

	if (!sparql->policy.graphs)
		return TRUE;

	for (i = 0; i < sparql->policy.graphs->len; i++) {
		const gchar *policy_graph;

		policy_graph = g_ptr_array_index (sparql->policy.graphs, i);

		if (g_strcmp0 (graph, policy_graph) == 0)
			return TRUE;
	}

	return FALSE;
}

static GHashTable *
tracker_sparql_get_effective_graphs (TrackerSparql *sparql)
{
	GHashTable *graphs;

	graphs = tracker_data_manager_get_graphs (sparql->data_manager);

	if (graphs && sparql->policy.graphs) {
		if (!sparql->policy.filtered_graphs) {
			gint i;

			sparql->policy.filtered_graphs =
				g_hash_table_new_full (g_str_hash,
				                       g_str_equal,
				                       g_free,
				                       NULL);

			for (i = 0; i < sparql->policy.graphs->len; i++) {
				gpointer key, value;

				if (g_hash_table_lookup_extended (graphs,
				                                  g_ptr_array_index (sparql->policy.graphs, i),
				                                  &key, &value)) {
					g_hash_table_insert (sparql->policy.filtered_graphs,
					                     g_strdup (key), value);
				}
			}
		}

		return sparql->policy.filtered_graphs;
	} else {
		return graphs;
	}
}

static void
_append_union_graph_with_clause (TrackerSparql *sparql,
                                 const gchar   *table_name,
                                 const gchar   *properties,
                                 gint           n_properties)
{
	gpointer graph_name, graph_id;
	GHashTable *graphs;
	GHashTableIter iter;

	graphs = tracker_sparql_get_effective_graphs (sparql);

	_append_string_printf (sparql, "\"unionGraph_%s\"(ID, %s graph) AS (",
	                       table_name, properties);

	if (!sparql->policy.filter_unnamed_graph) {
		_append_string_printf (sparql,
		                       "SELECT ID, %s 0 AS graph FROM \"main\".\"%s\" ",
		                       properties, table_name);
	} else {
		gint i;

		_append_string (sparql, "SELECT ");

		for (i = 0; i < n_properties + 2; i++) {
			if (i > 0)
				_append_string (sparql, ", ");
			_append_string (sparql, "NULL ");
		}

		_append_string (sparql, "WHERE 0 ");
	}

	g_hash_table_iter_init (&iter, graphs);
	while (g_hash_table_iter_next (&iter, &graph_name, &graph_id)) {
		_append_string_printf (sparql,
		                       "UNION ALL SELECT ID, %s %d AS graph FROM \"%s\".\"%s\" ",
		                       properties,
		                       GPOINTER_TO_INT (graph_id),
		                       (gchar *) graph_name,
		                       table_name);
	}

	_append_string (sparql, ") ");
}

static void
tracker_sparql_add_union_graph_subquery (TrackerSparql   *sparql,
                                         TrackerProperty *property)
{
	TrackerStringBuilder *old;
	const gchar *table_name;
	gchar *properties;
	gint n_properties;

	table_name = tracker_property_get_table_name (property);

	if (g_hash_table_lookup (sparql->current_state.union_views, table_name))
		return;

	g_hash_table_add (sparql->current_state.union_views, g_strdup (table_name));
	old = tracker_sparql_swap_builder (sparql, sparql->current_state.with_clauses);

	if (tracker_string_builder_is_empty (sparql->current_state.with_clauses))
		_append_string (sparql, "WITH ");
	else
		_append_string (sparql, ", ");

	properties = build_properties_string (sparql, property, &n_properties);
	_append_union_graph_with_clause (sparql, table_name, properties, n_properties);
	g_free (properties);

	tracker_sparql_swap_builder (sparql, old);
}

static void
tracker_sparql_add_union_graph_subquery_for_class (TrackerSparql *sparql,
                                                   TrackerClass  *class)
{
	TrackerStringBuilder *old;
	const gchar *table_name;
	gchar *properties;
	gint n_properties;

	table_name = tracker_class_get_name (class);

	if (g_hash_table_lookup (sparql->current_state.union_views, table_name))
		return;

	g_hash_table_add (sparql->current_state.union_views, g_strdup (table_name));
	old = tracker_sparql_swap_builder (sparql, sparql->current_state.with_clauses);

	if (tracker_string_builder_is_empty (sparql->current_state.with_clauses))
		_append_string (sparql, "WITH ");
	else
		_append_string (sparql, ", ");

	properties = build_properties_string_for_class (sparql, class, &n_properties);
	_append_union_graph_with_clause (sparql, table_name, properties, n_properties);
	g_free (properties);

	tracker_sparql_swap_builder (sparql, old);
}

static gint
tracker_sparql_find_graph (TrackerSparql *sparql,
                           const gchar   *name)
{
	GHashTable *effective_graphs;

	effective_graphs = tracker_sparql_get_effective_graphs (sparql);
	if (!effective_graphs ||
	    !g_hash_table_contains (effective_graphs, name)) {
		return 0;
	}

	return tracker_data_manager_find_graph (sparql->data_manager, name);
}

static void
_prepend_path_element (TrackerSparql      *sparql,
                       TrackerPathElement *path_elem)
{
	TrackerStringBuilder *old;
	gchar *table_name, *graph_column;

	if (path_elem->op == TRACKER_PATH_OPERATOR_NONE &&
	    tracker_token_is_empty (&sparql->current_state.graph)) {
		tracker_sparql_add_union_graph_subquery (sparql, path_elem->data.property);
	}

	old = tracker_sparql_swap_builder (sparql, sparql->current_state.with_clauses);

	if (tracker_string_builder_is_empty (sparql->current_state.with_clauses))
		_append_string (sparql, "WITH ");
	else
		_append_string (sparql, ", ");

	switch (path_elem->op) {
	case TRACKER_PATH_OPERATOR_NONE:
		/* A simple property */
		if (tracker_token_is_empty (&sparql->current_state.graph)) {
			table_name = g_strdup_printf ("\"unionGraph_%s\"",
			                              tracker_property_get_table_name (path_elem->data.property));
			graph_column = g_strdup ("graph");
		} else {
			const gchar *graph;

			graph = tracker_token_get_idstring (&sparql->current_state.graph);

			if (tracker_data_manager_find_graph (sparql->data_manager, graph)) {
				table_name = g_strdup_printf ("\"%s\".\"%s\"", graph,
				                              tracker_property_get_table_name (path_elem->data.property));
				graph_column = g_strdup_printf ("%d",
				                                tracker_sparql_find_graph (sparql, graph));
			} else {
				/* Graph does not exist, ensure to come back empty */
				table_name = g_strdup ("(SELECT 0 AS ID, NULL, NULL, 0, 0 LIMIT 0)");
				graph_column = g_strdup ("0");
			}
		}

		_append_string_printf (sparql,
		                       "\"%s\" (ID, value, graph, ID_type, value_type) AS "
		                       "(SELECT ID, \"%s\", %s, %d, %d FROM %s) ",
		                       path_elem->name,
		                       tracker_property_get_name (path_elem->data.property),
		                       graph_column,
		                       TRACKER_PROPERTY_TYPE_RESOURCE,
		                       tracker_property_get_data_type (path_elem->data.property),
		                       table_name);
		g_free (table_name);
		g_free (graph_column);
		break;
	case TRACKER_PATH_OPERATOR_INVERSE:
		_append_string_printf (sparql,
		                       "\"%s\" (ID, value, graph, ID_type, value_type) AS "
		                       "(SELECT value, ID, graph, value_type, ID_type FROM \"%s\" WHERE value IS NOT NULL) ",
		                       path_elem->name,
		                       path_elem->data.composite.child1->name);
		break;
	case TRACKER_PATH_OPERATOR_SEQUENCE:
		_append_string_printf (sparql,
		                       "\"%s\" (ID, value, graph, ID_type, value_type) AS "
		                       "(SELECT a.ID, b.value, b.graph, a.ID_type, b.value_type "
		                       "FROM \"%s\" AS a, \"%s\" AS b "
		                       "WHERE a.value = b.ID) ",
		                       path_elem->name,
		                       path_elem->data.composite.child1->name,
		                       path_elem->data.composite.child2->name);
		break;
	case TRACKER_PATH_OPERATOR_ALTERNATIVE:
		_append_string_printf (sparql,
		                       "\"%s\" (ID, value, graph, ID_type, value_type) AS "
		                       "(SELECT ID, value, graph, ID_type, value_type "
		                       "FROM \"%s\" "
		                       "UNION "
		                       "SELECT ID, value, graph, ID_type, value_type "
		                       "FROM \"%s\") ",
		                       path_elem->name,
		                       path_elem->data.composite.child1->name,
		                       path_elem->data.composite.child2->name);
		break;
	case TRACKER_PATH_OPERATOR_ZEROORMORE:
		_append_string_printf (sparql,
		                       "\"%s_helper\" (ID, value, graph, ID_type, value_type) AS "
		                       "(SELECT ID, value, graph, ID_type, value_type "
				       "FROM \"%s\" "
				       "UNION "
				       "SELECT a.ID, b.value, b.graph, a.ID_type, b.value_type "
				       "FROM \"%s\" AS a, \"%s_helper\" AS b "
				       "WHERE a.value = b.ID), ",
				       path_elem->name,
				       path_elem->data.composite.child1->name,
		                       path_elem->data.composite.child1->name,
				       path_elem->name);
		_append_string_printf (sparql,
		                       "\"%s\" (ID, value, graph, ID_type, value_type) AS "
		                       "(SELECT ID, value, graph, ID_type, value_type "
		                       "FROM \"%s_helper\" "
		                       "UNION "
		                       "SELECT ID, ID, graph, ID_type, ID_type "
		                       "FROM \"%s\" "
		                       "UNION "
		                       "SELECT value, value, graph, value_type, value_type "
		                       "FROM \"%s\") ",
		                       path_elem->name,
		                       path_elem->name,
		                       path_elem->data.composite.child1->name,
		                       path_elem->data.composite.child1->name);
		break;
	case TRACKER_PATH_OPERATOR_ONEORMORE:
		_append_string_printf (sparql,
		                       "\"%s\" (ID, value, graph, ID_type, value_type) AS "
		                       "(SELECT ID, value, graph, ID_type, value_type "
				       "FROM \"%s\" "
				       "UNION "
				       "SELECT a.ID, b.value, b.graph, a.ID_type, b.value_type "
				       "FROM \"%s\" AS a, \"%s\" AS b "
				       "WHERE b.ID = a.value) ",
				       path_elem->name,
				       path_elem->data.composite.child1->name,
		                       path_elem->data.composite.child1->name,
				       path_elem->name);
		break;
	case TRACKER_PATH_OPERATOR_ZEROORONE:
		_append_string_printf (sparql,
		                       "\"%s\" (ID, value, graph, ID_type, value_type) AS "
		                       "(SELECT ID, ID, graph, ID_type, ID_type "
				       "FROM \"%s\" "
				       "UNION "
				       "SELECT ID, value, graph, ID_type, value_type "
				       "FROM \"%s\" "
				       "UNION "
				       "SELECT value, value, graph, value_type, value_type "
				       "FROM \"%s\") ",
				       path_elem->name,
				       path_elem->data.composite.child1->name,
				       path_elem->data.composite.child1->name,
				       path_elem->data.composite.child1->name);
		break;
	case TRACKER_PATH_OPERATOR_NEGATED:
		_append_string_printf (sparql,
		                       "\"%s\" (ID, value, graph, ID_type, value_type) AS "
		                       "(SELECT subject AS ID, object AS value, graph, %d, object_type "
		                       "FROM \"tracker_triples\" ",
		                       path_elem->name,
		                       TRACKER_PROPERTY_TYPE_RESOURCE);

		if (!tracker_token_is_empty (&sparql->current_state.graph) &&
		    tracker_sparql_find_graph (sparql, tracker_token_get_idstring (&sparql->current_state.graph))) {
			const gchar *graph;

			graph = tracker_token_get_idstring (&sparql->current_state.graph);
			_append_graph_checks (sparql, "graph", FALSE, (GStrv) &graph, 1);
			_append_string (sparql, "AND ");
		} else if (sparql->policy.graphs) {
			_append_graph_checks (sparql, "graph",
			                      !sparql->policy.filter_unnamed_graph,
			                      (GStrv) sparql->policy.graphs->pdata,
			                      sparql->policy.graphs->len);
			_append_string (sparql, "AND ");
		} else {
			_append_string (sparql, "WHERE ");
		}

		_append_string_printf (sparql, "predicate != %d ",
		                       tracker_property_get_id (path_elem->data.property));
		_append_string (sparql, ") ");
		break;
	case TRACKER_PATH_OPERATOR_INTERSECTION:
		_append_string_printf (sparql,
		                       "\"%s\" (ID, value, graph, ID_type, value_type) AS "
		                       "(SELECT ID, value, graph, ID_type, value_type "
				       "FROM \"%s\" "
				       "INTERSECT "
				       "SELECT ID, value, graph, ID_type, value_type "
				       "FROM \"%s\") ",
				       path_elem->name,
				       path_elem->data.composite.child1->name,
				       path_elem->data.composite.child2->name);
		break;
	}

	tracker_sparql_swap_builder (sparql, old);
}

static inline gchar *
_extract_node_string (TrackerParserNode *node,
                      TrackerSparql     *sparql)
{
	const TrackerGrammarRule *rule;
	gchar *str = NULL;
	gssize start, end;

	if (!tracker_parser_node_get_extents (node, &start, &end))
		return NULL;

	rule = tracker_parser_node_get_rule (node);

	if (rule->type == RULE_TYPE_LITERAL) {
		switch (rule->data.literal) {
		case LITERAL_A:
			str = g_strdup (RDF_NS "type");
			break;
		default:
			str = g_strndup (&sparql->sparql[start], end - start);
			break;
		}
	} else if (rule->type == RULE_TYPE_TERMINAL) {
		const gchar *terminal_start, *terminal_end;
		gssize add_start = 0, subtract_end = 0;
		gboolean compress = FALSE;

		terminal_start = &sparql->sparql[start];
		terminal_end = &sparql->sparql[end];
		rule = tracker_parser_node_get_rule (node);

		switch (rule->data.terminal) {
		case TERMINAL_TYPE_VAR1:
		case TERMINAL_TYPE_VAR2:
		case TERMINAL_TYPE_PARAMETERIZED_VAR:
			add_start = 1;
			break;
		case TERMINAL_TYPE_STRING_LITERAL1:
		case TERMINAL_TYPE_STRING_LITERAL2:
			add_start = subtract_end = 1;
			compress = TRUE;
			break;
		case TERMINAL_TYPE_STRING_LITERAL_LONG1:
		case TERMINAL_TYPE_STRING_LITERAL_LONG2:
			add_start = subtract_end = 3;
			compress = TRUE;
			break;
		case TERMINAL_TYPE_IRIREF: {
			gchar *unexpanded;

			add_start = subtract_end = 1;
			unexpanded = g_strndup (terminal_start + add_start,
			                        terminal_end - terminal_start -
			                        add_start - subtract_end);
			str = tracker_sparql_expand_base (sparql, unexpanded);
			g_free (unexpanded);
			break;
		}
		case TERMINAL_TYPE_BLANK_NODE_LABEL:
			add_start = 2;
			break;
		case TERMINAL_TYPE_PNAME_NS:
			subtract_end = 1;
			/* Fall through */
		case TERMINAL_TYPE_PNAME_LN: {
			gchar *unexpanded;

			unexpanded = g_strndup (terminal_start + add_start,
						terminal_end - terminal_start - subtract_end);
			str = tracker_sparql_expand_prefix (sparql, unexpanded);
			g_free (unexpanded);
			break;
		}
		default:
			break;
		}

		terminal_start += add_start;
		terminal_end -= subtract_end;
		g_assert (terminal_end >= terminal_start);

		if (!str)
			str = g_strndup (terminal_start, terminal_end - terminal_start);

		if (compress) {
			gchar *tmp = str;

			str = g_strcompress (tmp);
			g_free (tmp);
		}
	} else {
		g_assert_not_reached ();
	}

	return str;
}

static inline gchar *
_dup_last_string (TrackerSparql *sparql)
{
	return _extract_node_string (sparql->current_state.prev_node, sparql);
}

static inline TrackerBinding *
_convert_terminal (TrackerSparql *sparql)
{
	const TrackerGrammarRule *rule;
	TrackerBinding *binding;
	gboolean is_parameter;
	GHashTable *ht;
	gchar *str;

	str = _dup_last_string (sparql);
	g_assert (str != NULL);

	rule = tracker_parser_node_get_rule (sparql->current_state.prev_node);
	is_parameter = tracker_grammar_rule_is_a (rule, RULE_TYPE_TERMINAL,
	                                          TERMINAL_TYPE_PARAMETERIZED_VAR);
	ht = is_parameter ? sparql->parameters : sparql->cached_bindings;

	binding = g_hash_table_lookup (ht, str);
	if (binding)
		return g_object_ref (binding);

	if (is_parameter) {
		binding = tracker_parameter_binding_new (str, NULL);
	} else {
		GBytes *bytes;

		bytes = g_bytes_new (str, strlen (str) + 1);
		binding = tracker_literal_binding_new (bytes, NULL);
		tracker_binding_set_data_type (binding, sparql->current_state.expression_type);
		g_bytes_unref (bytes);
	}

	g_hash_table_insert (ht, str, g_object_ref (binding));

	return binding;
}

static void
_add_binding (TrackerSparql  *sparql,
	      TrackerBinding *binding)
{
	TrackerTripleContext *context;

	context = TRACKER_TRIPLE_CONTEXT (sparql->current_state.context);

	if (TRACKER_IS_LITERAL_BINDING (binding)) {
		tracker_triple_context_add_literal_binding (context,
							    TRACKER_LITERAL_BINDING (binding));

		/* Also add on the root SelectContext right away */
		tracker_select_context_add_literal_binding (TRACKER_SELECT_CONTEXT (sparql->context),
							    TRACKER_LITERAL_BINDING (binding));
	} else if (TRACKER_IS_VARIABLE_BINDING (binding)) {
		TrackerVariableBinding *variable_binding = TRACKER_VARIABLE_BINDING (binding);
		TrackerVariable *variable;

		variable = tracker_variable_binding_get_variable (variable_binding);
		tracker_triple_context_add_variable_binding (context,
							     variable,
							     variable_binding);

		if (!tracker_variable_has_bindings (variable))
			tracker_variable_set_sample_binding (variable, variable_binding);
	} else {
		g_assert_not_reached ();
	}
}

static inline TrackerVariable *
_ensure_variable (TrackerSparql *sparql,
		  const gchar   *name)
{
	TrackerVariable *var;

	var = tracker_select_context_ensure_variable (TRACKER_SELECT_CONTEXT (sparql->context),
	                                              name);
	tracker_context_add_variable_ref (sparql->current_state.context, var);

	return var;
}

static inline TrackerVariable *
_extract_node_variable (TrackerParserNode  *node,
                        TrackerSparql      *sparql)
{
	const TrackerGrammarRule *rule = tracker_parser_node_get_rule (node);
	TrackerVariable *variable = NULL;
	gchar *str;

	if (!tracker_grammar_rule_is_a (rule, RULE_TYPE_TERMINAL, TERMINAL_TYPE_VAR1) &&
	    !tracker_grammar_rule_is_a (rule, RULE_TYPE_TERMINAL, TERMINAL_TYPE_VAR2))
		return NULL;

	str = _extract_node_string (node, sparql);
	variable = _ensure_variable (sparql, str);
	g_free (str);

	return variable;
}

static inline TrackerVariable *
_last_node_variable (TrackerSparql *sparql)
{
	return _extract_node_variable (sparql->current_state.prev_node, sparql);
}

static void
_init_token (TrackerToken      *token,
             TrackerParserNode *node,
             TrackerSparql     *sparql)
{
	const TrackerGrammarRule *rule = tracker_parser_node_get_rule (node);
	TrackerVariable *var;
	gchar *str;

	str = _extract_node_string (node, sparql);

	if (tracker_grammar_rule_is_a (rule, RULE_TYPE_TERMINAL, TERMINAL_TYPE_VAR1) ||
	    tracker_grammar_rule_is_a (rule, RULE_TYPE_TERMINAL, TERMINAL_TYPE_VAR2)) {
		if (sparql->current_state.type == TRACKER_SPARQL_TYPE_SELECT ||
		    sparql->current_state.type == TRACKER_SPARQL_TYPE_CONSTRUCT) {
			var = _ensure_variable (sparql, str);
			tracker_token_variable_init (token, var);
		} else {
			const gchar *value;

			value = g_hash_table_lookup (sparql->solution_var_map, str);
			if (value)
				tracker_token_literal_init (token, value, -1);
		}
	} else if (tracker_grammar_rule_is_a (rule, RULE_TYPE_TERMINAL, TERMINAL_TYPE_PARAMETERIZED_VAR)) {
		tracker_token_parameter_init (token, str);
	} else {
		tracker_token_literal_init (token, str, -1);
	}

	g_free (str);
}

static inline gboolean
_accept_token (TrackerParserNode      **node,
               TrackerGrammarRuleType   type,
               guint                    value,
               TrackerParserNode      **prev)
{
	const TrackerGrammarRule *rule;

	g_assert (node != NULL && *node != NULL);
	rule = tracker_parser_node_get_rule (*node);

	if (!tracker_grammar_rule_is_a (rule, type, value))
		return FALSE;

	if (prev)
		*prev = *node;

	*node = tracker_sparql_parser_tree_find_next (*node, TRUE);
	return TRUE;
}

static gboolean
extract_fts_snippet_parameters (TrackerSparql      *sparql,
                                TrackerParserNode  *node,
                                gchar             **match_start,
                                gchar             **match_end,
                                gchar             **ellipsis,
                                gchar             **num_tokens,
                                GError            **error)
{
	TrackerParserNode *val = NULL;

	if (_accept_token (&node, RULE_TYPE_LITERAL, LITERAL_COMMA, NULL)) {
		if (_accept_token (&node, RULE_TYPE_TERMINAL, TERMINAL_TYPE_STRING_LITERAL1, &val) ||
		    _accept_token (&node, RULE_TYPE_TERMINAL, TERMINAL_TYPE_STRING_LITERAL2, &val)) {
			*match_start = _extract_node_string (val, sparql);
		} else {
			_raise (PARSE, "«Match start» argument expects string", "fts:snippet");
		}

		if (!_accept_token (&node, RULE_TYPE_LITERAL, LITERAL_COMMA, NULL)) {
			_raise (PARSE, "Both «Match start» and «Match end» arguments expected", "fts:snippet");
		}

		if (_accept_token (&node, RULE_TYPE_TERMINAL, TERMINAL_TYPE_STRING_LITERAL1, &val) ||
		    _accept_token (&node, RULE_TYPE_TERMINAL, TERMINAL_TYPE_STRING_LITERAL2, &val)) {
			*match_end = _extract_node_string (val, sparql);
		} else {
			_raise (PARSE, "«Match end» argument expects string", "fts:snippet");
		}
	}

	if (_accept_token (&node, RULE_TYPE_LITERAL, LITERAL_COMMA, NULL)) {
		if (_accept_token (&node, RULE_TYPE_TERMINAL, TERMINAL_TYPE_STRING_LITERAL1, &val) ||
		    _accept_token (&node, RULE_TYPE_TERMINAL, TERMINAL_TYPE_STRING_LITERAL2, &val)) {
			*ellipsis = _extract_node_string (val, sparql);
		} else {
			_raise (PARSE, "«Ellipsis» argument expects string", "fts:snippet");
		}
	}

	if (_accept_token (&node, RULE_TYPE_LITERAL, LITERAL_COMMA, NULL)) {
		if (_accept_token (&node, RULE_TYPE_TERMINAL, TERMINAL_TYPE_INTEGER, &val) ||
		    _accept_token (&node, RULE_TYPE_TERMINAL, TERMINAL_TYPE_INTEGER_POSITIVE, &val)) {
			*num_tokens = _extract_node_string (val, sparql);
		} else {
			_raise (PARSE, "«Num. tokens» argument expects integer", "fts:snippet");
		}
	}

	if (!_accept_token (&node, RULE_TYPE_LITERAL, LITERAL_CLOSE_PARENS, NULL)) {
		_raise (PARSE, "Unexpected number of parameters", "fts:snippet");
	}

	return TRUE;
}

static gboolean
introspect_fts_snippet (TrackerSparql         *sparql,
                        TrackerVariable       *subject,
                        gchar                **expression,
                        GError               **error)
{
	TrackerParserNode *node = tracker_node_tree_get_root (sparql->tree);

	for (node = tracker_sparql_parser_tree_find_first (node, TRUE);
	     node;
	     node = tracker_sparql_parser_tree_find_next (node, TRUE)) {
		gchar *str, *match_start = NULL, *match_end = NULL, *ellipsis = NULL, *num_tokens = NULL;
		const TrackerGrammarRule *rule;
		TrackerVariable *var;

		rule = tracker_parser_node_get_rule (node);
		if (!tracker_grammar_rule_is_a (rule, RULE_TYPE_TERMINAL,
		                                TERMINAL_TYPE_PNAME_LN))
			continue;

		str = _extract_node_string (node, sparql);

		if (g_str_equal (str, FTS_NS "snippet")) {
			g_free (str);
			node = tracker_sparql_parser_tree_find_next (node, TRUE);
		} else {
			g_free (str);
			continue;
		}

		if (!_accept_token (&node, RULE_TYPE_LITERAL, LITERAL_OPEN_PARENS, NULL)) {
			_raise (PARSE, "Expected open parens", "fts:snippet");
		}

		var = _extract_node_variable (node, sparql);
		if (var != subject)
			continue;

		node = tracker_sparql_parser_tree_find_next (node, TRUE);

		if (!extract_fts_snippet_parameters (sparql, node,
		                                     &match_start,
		                                     &match_end,
		                                     &ellipsis,
		                                     &num_tokens,
		                                     error)) {
			g_free (match_start);
			g_free (match_end);
			g_free (ellipsis);
			g_free (num_tokens);
			return FALSE;
		}

		*expression = g_strdup_printf ("snippet(\"fts5\", -1, '%s', '%s', '%s', %s)",
		                               match_start ? match_start : "",
		                               match_end ? match_end : "",
		                               ellipsis ? ellipsis : "…",
		                               num_tokens ? num_tokens : "5");

		g_free (match_start);
		g_free (match_end);
		g_free (ellipsis);
		g_free (num_tokens);

		return TRUE;
	}

	return FALSE;
}

static gchar *
tracker_sparql_add_fts_subquery (TrackerSparql         *sparql,
                                 TrackerToken          *graph,
                                 TrackerToken          *subject,
                                 TrackerLiteralBinding *binding)
{
	TrackerStringBuilder *old;
	gchar *snippet_expression = NULL;
	GString *select_items;
	gchar *table_name;

	old = tracker_sparql_swap_builder (sparql, sparql->current_state.with_clauses);

	if (tracker_string_builder_is_empty (sparql->current_state.with_clauses))
		_append_string (sparql, "WITH ");
	else
		_append_string (sparql, ", ");

	table_name = g_strdup_printf ("ftsMatch%d",
	                              sparql->current_state.fts_match_idx++);
	_append_string_printf (sparql, "\"%s\"(ID ", table_name);
	select_items = g_string_new ("SELECT ROWID");

	if (tracker_token_get_variable (subject)) {
		_append_string (sparql, ",\"ftsRank\", \"ftsOffsets\" ");
		g_string_append (select_items, ",rank, tracker_offsets(fts5) ");

		_append_string (sparql, ",\"ftsSnippet\" ");

		if (introspect_fts_snippet (sparql, tracker_token_get_variable (subject),
		                            &snippet_expression, NULL)) {
			g_string_append_printf (select_items, ", %s ",
			                        snippet_expression ? snippet_expression : "");
			g_free (snippet_expression);
		} else {
			g_string_append (select_items, ", NULL ");
		}
	}

	if (!tracker_token_get_literal (graph))
		_append_string (sparql, ", graph");

	_append_string (sparql, ") AS (");

	if (tracker_token_get_literal (graph)) {
		_append_string_printf (sparql,
		                       "%s FROM \"%s\".\"fts5\" "
		                       "WHERE fts5 = ",
		                       select_items->str,
		                       tracker_token_get_idstring (graph));
		_append_literal_sql (sparql, binding);
	} else {
		gpointer graph_name, graph_id;
		GHashTable *graphs;
		GHashTableIter iter;

		_append_string_printf (sparql,
		                       "%s, 0 FROM \"main\".\"fts5\" "
		                       "WHERE fts5 = ",
		                       select_items->str);
		_append_literal_sql (sparql, binding);

		graphs = tracker_sparql_get_effective_graphs (sparql);
		g_hash_table_iter_init (&iter, graphs);

		while (g_hash_table_iter_next (&iter, &graph_name, &graph_id)) {
			_append_string_printf (sparql,
			                       "UNION ALL %s, %d AS graph "
			                       "FROM \"%s\".\"fts5\" "
			                       "WHERE fts5 = ",
			                       select_items->str,
			                       GPOINTER_TO_INT (graph_id),
			                       (gchar *) graph_name);
			_append_literal_sql (sparql, binding);
		}
	}

	_append_string (sparql, ") ");
	tracker_sparql_swap_builder (sparql, old);
	g_string_free (select_items, TRUE);

	return table_name;
}

static TrackerVariable *
reserve_subvariable (TrackerSparql   *sparql,
                     TrackerVariable *var,
                     const gchar     *suffix)
{
	TrackerVariable *subvar;
	gchar *name;

	name = g_strdup_printf ("%s:%s", var->name, suffix);
	subvar = _ensure_variable (sparql, name);
	g_free (name);

	return subvar;
}

static TrackerVariable *
lookup_subvariable (TrackerSparql   *sparql,
                    TrackerVariable *var,
                    const gchar     *suffix)
{
	TrackerVariable *subvar;
	gchar *name;

	name = g_strdup_printf ("%s:%s", var->name, suffix);
	subvar = tracker_select_context_lookup_variable (TRACKER_SELECT_CONTEXT (sparql->context),
	                                                 name);
	g_free (name);

	return subvar;
}

static gboolean
_add_quad (TrackerSparql  *sparql,
           TrackerToken   *graph,
	   TrackerToken   *subject,
	   TrackerToken   *predicate,
	   TrackerToken   *object,
	   GError        **error)
{
	TrackerTripleContext *triple_context;
	TrackerOntologies *ontologies;
	TrackerDataTable *table = NULL;
	TrackerVariable *variable;
	TrackerBinding *binding;
	TrackerProperty *property = NULL;
	TrackerClass *subject_type = NULL;
	gboolean new_table = FALSE, is_fts = FALSE, is_rdf_type = FALSE;
	const gchar *graph_db = NULL;

	triple_context = TRACKER_TRIPLE_CONTEXT (sparql->current_state.context);
	ontologies = tracker_data_manager_get_ontologies (sparql->data_manager);

	if (tracker_token_get_literal (graph))
		graph_db = tracker_token_get_idstring (graph);

	if (tracker_token_get_literal (predicate)) {
		gboolean share_table = TRUE;
		const gchar *db_table;
		gchar *fts_table = NULL;

		property = tracker_ontologies_get_property_by_uri (ontologies,
		                                                   tracker_token_get_idstring(predicate));

		if (tracker_token_get_literal (object) &&
		    g_strcmp0 (tracker_token_get_idstring (predicate), RDF_NS "type") == 0) {
			/* rdf:type query */
			subject_type = tracker_ontologies_get_class_by_uri (ontologies,
			                                                    tracker_token_get_idstring (object));
			if (!subject_type) {
				g_set_error (error, TRACKER_SPARQL_ERROR,
					     TRACKER_SPARQL_ERROR_UNKNOWN_CLASS,
					     "Unknown class '%s'",
				             tracker_token_get_idstring (object));
				return FALSE;
			}

			if (!graph_db || !tracker_sparql_find_graph (sparql, graph_db))
				tracker_sparql_add_union_graph_subquery_for_class (sparql, subject_type);

			is_rdf_type = TRUE;
			db_table = tracker_class_get_name (subject_type);
			share_table = !tracker_token_is_empty (graph);
		} else if (g_strcmp0 (tracker_token_get_idstring (predicate), FTS_NS "match") == 0) {
			if (tracker_token_get_literal (object)) {
				binding = tracker_literal_binding_new (tracker_token_get_literal (object), table);
			} else if (tracker_token_get_parameter (object)) {
				binding = tracker_parameter_binding_new (tracker_token_get_parameter (object), table);
			} else {
				g_assert_not_reached ();
			}

			tracker_binding_set_db_column_name (binding, "fts5");
			tracker_select_context_add_literal_binding (TRACKER_SELECT_CONTEXT (sparql->context),
			                                            TRACKER_LITERAL_BINDING (binding));
			g_object_unref (binding);

			fts_table = tracker_sparql_add_fts_subquery (sparql, graph, subject,
			                                             TRACKER_LITERAL_BINDING (binding));

			db_table = fts_table;
			share_table = FALSE;
			is_fts = TRUE;
		} else if (property != NULL) {
			db_table = tracker_property_get_table_name (property);

			if (tracker_token_get_variable (subject)) {
				GPtrArray *binding_list;

				variable = tracker_token_get_variable (subject);
				binding_list = tracker_triple_context_lookup_variable_binding_list (triple_context,
												    variable);
				/* Domain specific index might be a possibility, let's check */
				if (binding_list) {
					TrackerClass *domain_index = NULL;
					TrackerClass **classes;
					gint i = 0, j;

					classes = tracker_property_get_domain_indexes (property);

					while (!domain_index && classes[i]) {
						for (j = 0; j < binding_list->len; j++) {
							TrackerVariableBinding *list_binding;

							list_binding = g_ptr_array_index (binding_list, j);
							if (list_binding->type != classes[i])
								continue;

							domain_index = classes[i];
							break;
						}

						i++;
					}

					if (domain_index) {
						if (!graph_db || !tracker_sparql_find_graph (sparql, graph_db))
							tracker_sparql_add_union_graph_subquery_for_class (sparql, domain_index);
						db_table = tracker_class_get_name (domain_index);
					}
				}
			}

			if (!graph_db || !tracker_sparql_find_graph (sparql, graph_db))
				tracker_sparql_add_union_graph_subquery (sparql, property);

			/* We can never share the table with multiple triples for
			 * multi value properties as a property may consist of multiple rows.
			 * We can't either do that for graph unrestricted queries
			 * since we need the self-joins to query across all graphs.
			 */
			share_table = (!tracker_property_get_multiple_values (property) &&
			               !tracker_token_is_empty (graph));

			subject_type = tracker_property_get_domain (property);
		} else if (property == NULL) {
			g_set_error (error, TRACKER_SPARQL_ERROR,
				     TRACKER_SPARQL_ERROR_UNKNOWN_PROPERTY,
				     "Unknown property '%s'",
			             tracker_token_get_idstring (predicate));
			return FALSE;
		}

		if (share_table) {
			table = tracker_triple_context_lookup_table (triple_context,
			                                             graph_db,
								     db_table);
		}

		if (!table) {
			table = tracker_triple_context_add_table (triple_context,
			                                          graph_db,
								  db_table);
			new_table = TRUE;
		}

		table->fts = is_fts;
		g_free (fts_table);
	} else if (tracker_token_get_variable (predicate)) {
		/* Variable in predicate */
		variable = tracker_token_get_variable (predicate);
		table = tracker_triple_context_add_table (triple_context,
		                                          graph_db,
		                                          variable->name);
		tracker_data_table_set_predicate_variable (table, TRUE);
		new_table = TRUE;

		/* Add to binding list */
		binding = tracker_variable_binding_new (variable, NULL, table);
		tracker_binding_set_data_type (binding, TRACKER_PROPERTY_TYPE_RESOURCE);
		tracker_binding_set_db_column_name (binding, "predicate");
		_add_binding (sparql, binding);
		g_object_unref (binding);

		/* If object is variable, add another variable to hold its type */
		if (tracker_token_get_variable (object)) {
			TrackerVariable *type_var;

			type_var = reserve_subvariable (sparql, tracker_token_get_variable (object), "type");

			binding = tracker_variable_binding_new (type_var, NULL, table);
			tracker_binding_set_db_column_name (binding, "object_type");
			_add_binding (sparql, binding);
			g_object_unref (binding);
		}
	} else if (tracker_token_get_path (predicate)) {
		table = tracker_triple_context_add_table (triple_context,
		                                          graph_db,
		                                          tracker_token_get_idstring (predicate));
		tracker_data_table_set_predicate_path (table, TRUE);
		new_table = TRUE;

		/* If subject/object are variable, add variables to hold their type */
		if (tracker_token_get_variable (subject)) {
			TrackerVariable *type_var;

			type_var = reserve_subvariable (sparql, tracker_token_get_variable (subject), "type");

			binding = tracker_variable_binding_new (type_var, NULL, table);
			tracker_binding_set_db_column_name (binding, "ID_type");
			_add_binding (sparql, binding);
			g_object_unref (binding);
		}

		if (tracker_token_get_variable (object)) {
			TrackerVariable *type_var;

			type_var = reserve_subvariable (sparql, tracker_token_get_variable (object), "type");

			binding = tracker_variable_binding_new (type_var, NULL, table);
			tracker_binding_set_db_column_name (binding, "value_type");
			_add_binding (sparql, binding);
			g_object_unref (binding);
		}
	} else {
		/* The parser disallows parameter predicates */
		g_assert_not_reached ();
	}

	if (new_table) {
		if (tracker_token_get_variable (subject)) {
			variable = tracker_token_get_variable (subject);
			binding = tracker_variable_binding_new (variable, subject_type, table);
		} else if (tracker_token_get_literal (subject)) {
			binding = tracker_literal_binding_new (tracker_token_get_literal (subject),
			                                       table);
		} else if (tracker_token_get_parameter (subject)) {
			binding = tracker_parameter_binding_new (tracker_token_get_parameter (subject),
								 table);
		} else {
			g_assert_not_reached ();
		}

		tracker_binding_set_data_type (binding, TRACKER_PROPERTY_TYPE_RESOURCE);
		tracker_binding_set_db_column_name (binding, "ID");
		_add_binding (sparql, binding);
		g_object_unref (binding);
	}

	if (tracker_token_get_variable (graph)) {
		variable = tracker_token_get_variable (graph);
		binding = tracker_variable_binding_new (variable, NULL, table);
		tracker_binding_set_data_type (binding, TRACKER_PROPERTY_TYPE_RESOURCE);
		tracker_binding_set_db_column_name (binding, "graph");
		_add_binding (sparql, binding);
		g_object_unref (binding);
	}

	if (is_rdf_type) {
		/* The type binding is already implicit in the data table */
		return TRUE;
	}

	if (tracker_token_get_variable (object)) {
		variable = tracker_token_get_variable (object);
		binding = tracker_variable_binding_new (variable,
							property ? tracker_property_get_range (property) : NULL,
							table);
		if (!tracker_variable_has_bindings (variable))
			tracker_variable_set_sample_binding (variable, TRACKER_VARIABLE_BINDING (binding));

		if (tracker_token_get_variable (predicate)) {
			tracker_binding_set_db_column_name (binding, "object");
			tracker_variable_binding_set_nullable (TRACKER_VARIABLE_BINDING (binding), TRUE);
		} else if (tracker_token_get_path (predicate)) {
			TrackerPathElement *path;

			path = tracker_token_get_path (predicate);
			tracker_binding_set_data_type (binding, path->type);
			tracker_binding_set_db_column_name (binding, "value");
			tracker_variable_binding_set_nullable (TRACKER_VARIABLE_BINDING (binding), TRUE);
		} else {
			g_assert (property != NULL);
			tracker_binding_set_data_type (binding, tracker_property_get_data_type (property));
			tracker_binding_set_db_column_name (binding, tracker_property_get_name (property));

			if (!tracker_property_get_multiple_values (property)) {
				/* For single value properties, row may have NULL
				 * in any column except the ID column
				 */
				tracker_variable_binding_set_nullable (TRACKER_VARIABLE_BINDING (binding), TRUE);
			}
		}

		_add_binding (sparql, binding);
		g_object_unref (binding);
	} else if (is_fts) {
		if (tracker_token_get_variable (subject)) {
			gchar *var_name;
			TrackerVariable *fts_var;

			variable = tracker_token_get_variable (subject);

			/* FTS rank */
			var_name = g_strdup_printf ("%s:ftsRank", variable->name);
			fts_var = _ensure_variable (sparql, var_name);
			g_free (var_name);

			binding = tracker_variable_binding_new (fts_var, NULL, table);
			tracker_binding_set_db_column_name (binding, "ftsRank");
			_add_binding (sparql, binding);
			g_object_unref (binding);

			/* FTS offsets */
			var_name = g_strdup_printf ("%s:ftsOffsets", variable->name);
			fts_var = _ensure_variable (sparql, var_name);
			g_free (var_name);

			binding = tracker_variable_binding_new (fts_var, NULL, table);
			tracker_binding_set_db_column_name (binding, "ftsOffsets");
			_add_binding (sparql, binding);
			g_object_unref (binding);

			/* FTS snippet */
			var_name = g_strdup_printf ("%s:ftsSnippet", variable->name);
			fts_var = _ensure_variable (sparql, var_name);
			g_free (var_name);

			binding = tracker_variable_binding_new (fts_var, NULL, table);
			tracker_binding_set_db_column_name (binding, "ftsSnippet");
			_add_binding (sparql, binding);
			g_object_unref (binding);
		}
	} else {
		if (tracker_token_get_literal (object)) {
			binding = tracker_literal_binding_new (tracker_token_get_literal (object), table);
		} else if (tracker_token_get_parameter (object)) {
			binding = tracker_parameter_binding_new (tracker_token_get_parameter (object), table);
		} else {
			g_assert_not_reached ();
		}

		if (tracker_token_get_variable (predicate)) {
			tracker_binding_set_db_column_name (binding, "object");
		} else if (tracker_token_get_path (predicate)) {
			TrackerPathElement *path;

			path = tracker_token_get_path (predicate);
			tracker_binding_set_db_column_name (binding, "value");
			tracker_binding_set_data_type (binding, path->type);
		} else {
			g_assert (property != NULL);
			tracker_binding_set_data_type (binding, tracker_property_get_data_type (property));
			tracker_binding_set_db_column_name (binding, tracker_property_get_name (property));
		}

		_add_binding (sparql, binding);
		g_object_unref (binding);
	}

	return TRUE;
}

static void
add_construct_variable (TrackerSparql   *sparql,
                        TrackerVariable *var)
{
	TrackerPropertyType prop_type;
	TrackerStringBuilder *str, *old;

	str = _append_placeholder (sparql);
	old = tracker_sparql_swap_builder (sparql, str);

	_append_variable_sql (sparql, var);

	prop_type = TRACKER_BINDING (tracker_variable_get_sample_binding (var))->data_type;
	convert_expression_to_string (sparql, prop_type);

	tracker_sparql_swap_builder (sparql, old);
}

static gboolean
_construct_clause (TrackerSparql  *sparql,
                   TrackerToken   *graph,
                   TrackerToken   *subject,
                   TrackerToken   *predicate,
                   TrackerToken   *object,
                   GError        **error)
{
	gchar *construct_query;

	if (!tracker_string_builder_is_empty (sparql->current_state.sql))
		_append_string (sparql, "UNION ALL ");

	_append_string (sparql, "SELECT ");

	if (tracker_token_get_variable (subject) &&
	    tracker_variable_get_sample_binding (tracker_token_get_variable (subject)))
		add_construct_variable (sparql, tracker_token_get_variable (subject));
	else
		_append_string_printf (sparql, "\"%s\" ", tracker_token_get_idstring (subject));

	_append_string (sparql, "AS subject, ");

	if (tracker_token_get_variable (predicate) &&
	    tracker_variable_get_sample_binding (tracker_token_get_variable (predicate)))
		add_construct_variable (sparql, tracker_token_get_variable (predicate));
	else
		_append_string_printf (sparql, "\"%s\" ", tracker_token_get_idstring (predicate));

	_append_string (sparql, "AS predicate, ");

	if (tracker_token_get_variable (object) &&
	    tracker_variable_get_sample_binding (tracker_token_get_variable (object)))
		add_construct_variable (sparql, tracker_token_get_variable (object));
	else
		_append_string_printf (sparql, "\"%s\" ", tracker_token_get_idstring (object));

	_append_string (sparql, "AS object ");

	if (tracker_token_get_variable (subject) ||
	    tracker_token_get_variable (predicate) ||
	    tracker_token_get_variable (object)) {
		gboolean first = TRUE;
		_append_string (sparql, "FROM (SELECT DISTINCT ");

		if (tracker_token_get_variable (subject)) {
			_append_variable_sql (sparql, tracker_token_get_variable (subject));
			first = FALSE;
		}

		if (tracker_token_get_variable (predicate)) {
			if (!first)
				_append_string (sparql, ", ");
			_append_variable_sql (sparql, tracker_token_get_variable (predicate));
			first = FALSE;
		}

		if (tracker_token_get_variable (object)) {
			if (!first)
				_append_string (sparql, ", ");
			_append_variable_sql (sparql, tracker_token_get_variable (object));
		}

		_append_string (sparql, " FROM (");

		construct_query = tracker_string_builder_to_string (sparql->current_state.construct_query);
		_append_string_printf (sparql, "%s", construct_query);
		g_free (construct_query);

		_append_string (sparql, ")) ");
	}

	return TRUE;
}

static gboolean
tracker_sparql_apply_quad (TrackerSparql  *sparql,
                           GError        **error)
{
	GError *inner_error = NULL;

	if ((tracker_token_is_empty (&sparql->current_state.graph) &&
	     sparql->policy.filter_unnamed_graph) ||
	    (tracker_token_get_literal (&sparql->current_state.graph) &&
	     !tracker_sparql_graph_is_whitelisted (sparql, tracker_token_get_idstring (&sparql->current_state.graph)))) {
		_raise (CONSTRAINT, "Access to graph is disallowed",
		        tracker_token_is_empty (&sparql->current_state.graph) ? "DEFAULT" :
		        tracker_token_get_idstring (&sparql->current_state.graph));
	}

	switch (sparql->current_state.type) {
	case TRACKER_SPARQL_TYPE_SELECT:
		_add_quad (sparql,
		           &sparql->current_state.graph,
		           &sparql->current_state.subject,
		           &sparql->current_state.predicate,
		           &sparql->current_state.object,
		           &inner_error);
		break;
	case TRACKER_SPARQL_TYPE_CONSTRUCT:
		_construct_clause (sparql,
		                   &sparql->current_state.graph,
		                   &sparql->current_state.subject,
		                   &sparql->current_state.predicate,
		                   &sparql->current_state.object,
		                   &inner_error);
		break;
	case TRACKER_SPARQL_TYPE_INSERT:
		tracker_data_insert_statement (tracker_data_manager_get_data (sparql->data_manager),
		                               tracker_token_get_idstring (&sparql->current_state.graph),
		                               tracker_token_get_idstring (&sparql->current_state.subject),
		                               tracker_token_get_idstring (&sparql->current_state.predicate),
		                               tracker_token_get_literal (&sparql->current_state.object),
		                               &inner_error);
		break;
	case TRACKER_SPARQL_TYPE_DELETE:
		tracker_data_delete_statement (tracker_data_manager_get_data (sparql->data_manager),
		                               tracker_token_get_idstring (&sparql->current_state.graph),
		                               tracker_token_get_idstring (&sparql->current_state.subject),
		                               tracker_token_get_idstring (&sparql->current_state.predicate),
		                               tracker_token_get_literal (&sparql->current_state.object),
		                               &inner_error);
		break;
	case TRACKER_SPARQL_TYPE_UPDATE:
		tracker_data_update_statement (tracker_data_manager_get_data (sparql->data_manager),
		                               tracker_token_get_idstring (&sparql->current_state.graph),
		                               tracker_token_get_idstring (&sparql->current_state.subject),
		                               tracker_token_get_idstring (&sparql->current_state.predicate),
		                               tracker_token_get_literal (&sparql->current_state.object),
		                               &inner_error);
		break;
	default:
		g_assert_not_reached ();
	}

	if (inner_error) {
		g_propagate_error (error, inner_error);
		return FALSE;
	}

	return TRUE;
}

static void
tracker_sparql_init_string_builder (TrackerSparql *sparql)
{
	TrackerStringBuilder *str;

	g_clear_pointer (&sparql->sql, tracker_string_builder_free);
	sparql->sql = sparql->current_state.sql = tracker_string_builder_new ();
	sparql->current_state.with_clauses = _prepend_placeholder (sparql);

	/* Ensure the select clause goes to a different substring than the
	 * WITH clauses, so _prepend_string() works as expected.
	 */
	str = _append_placeholder (sparql);
	tracker_sparql_swap_builder (sparql, str);
}

static TrackerParserNode *
_skip_rule (TrackerSparql *sparql,
            guint          named_rule)
{
	TrackerParserNode *current, *iter, *next = NULL;

	g_assert (_check_in_rule (sparql, named_rule));
	current = iter = sparql->current_state.node;

	while (iter) {
		next = (TrackerParserNode *) g_node_next_sibling ((GNode *) iter);
		if (next) {
			next = tracker_sparql_parser_tree_find_first (next, FALSE);
			break;
		}

		iter = (TrackerParserNode *) ((GNode *) iter)->parent;
	}

	sparql->current_state.node = next;

	return current;
}

static TrackerPropertyType
rdf_type_to_property_type (const gchar *type)
{
	if (g_str_equal (type, XSD_NS "boolean")) {
		return TRACKER_PROPERTY_TYPE_BOOLEAN;
	} else if (g_str_equal (type, XSD_NS "integer") ||
	           g_str_equal (type, XSD_NS "nonPositiveInteger") ||
	           g_str_equal (type, XSD_NS "negativeInteger") ||
	           g_str_equal (type, XSD_NS "long") ||
	           g_str_equal (type, XSD_NS "int") ||
	           g_str_equal (type, XSD_NS "short") ||
	           g_str_equal (type, XSD_NS "byte") ||
	           g_str_equal (type, XSD_NS "nonNegativeInteger") ||
	           g_str_equal (type, XSD_NS "unsignedLong") ||
	           g_str_equal (type, XSD_NS "unsignedInt") ||
	           g_str_equal (type, XSD_NS "unsignedShort") ||
	           g_str_equal (type, XSD_NS "unsignedByte") ||
	           g_str_equal (type, XSD_NS "positiveInteger")) {
		return TRACKER_PROPERTY_TYPE_INTEGER;
	} else if (g_str_equal (type, XSD_NS "double")) {
		return TRACKER_PROPERTY_TYPE_DOUBLE;
	} else if (g_str_equal (type, XSD_NS "date")) {
		return TRACKER_PROPERTY_TYPE_DATE;
	} else if (g_str_equal (type, XSD_NS "dateTime")) {
		return TRACKER_PROPERTY_TYPE_DATETIME;
	} else if (g_str_equal (type, XSD_NS "string")) {
		return TRACKER_PROPERTY_TYPE_STRING;
	} else if (g_str_equal (type, RDF_NS "langString")) {
		return TRACKER_PROPERTY_TYPE_LANGSTRING;
	} else {
		return TRACKER_PROPERTY_TYPE_UNKNOWN;
	}
}

static void
convert_expression_to_string (TrackerSparql       *sparql,
                              TrackerPropertyType  type)
{
	switch (type) {
	case TRACKER_PROPERTY_TYPE_STRING:
	case TRACKER_PROPERTY_TYPE_INTEGER:
	case TRACKER_PROPERTY_TYPE_DOUBLE:
		/* Nothing to convert. Do not use CAST to convert integer/double to
		 * to string as this breaks use of index when sorting by variable
		 * introduced in select expression
		 */
		break;
	case TRACKER_PROPERTY_TYPE_RESOURCE:
		/* ID => Uri */
		_prepend_string (sparql, "(SELECT Uri FROM Resource WHERE ID = ");
		_append_string (sparql, ") ");
		break;
	case TRACKER_PROPERTY_TYPE_BOOLEAN:
		_prepend_string (sparql, "CASE ");
		_append_string (sparql, " WHEN 1 THEN 'true' WHEN 0 THEN 'false' ELSE NULL END ");
		break;
	case TRACKER_PROPERTY_TYPE_DATE:
		/* ISO 8601 format */
		_prepend_string (sparql, "strftime (\"%Y-%m-%d\", SparqlTimestamp (");
		_append_string (sparql, "), \"unixepoch\") ");
		break;
	case TRACKER_PROPERTY_TYPE_DATETIME:
		/* ISO 8601 format */
		_prepend_string (sparql, "SparqlFormatTime (");
		_append_string (sparql, ") ");
		break;
	case TRACKER_PROPERTY_TYPE_LANGSTRING:
	default:
		/* Let sqlite convert the expression to string */
		_prepend_string (sparql, "CAST (");
		_append_string (sparql, " AS TEXT) ");
		break;
	}
}

static void
_append_graph_checks (TrackerSparql *sparql,
                      const gchar   *column_name,
                      gboolean       add_unnamed_graph,
                      GStrv          graphs,
                      gint           len)
{
	gboolean first = TRUE;
	gint i;

	_append_string (sparql, "WHERE ");

	if (add_unnamed_graph) {
		_append_string_printf (sparql, "%s = %d OR ", column_name, 0);
	}

	_append_string_printf (sparql,
	                       "(SELECT Uri FROM Resource WHERE ID = %s) ",
	                       column_name);

	_append_string (sparql, "IN (");

	for (i = 0; i < len; i++) {
		if (!tracker_sparql_find_graph (sparql, graphs[i]))
			continue;

		if (!first)
			_append_string (sparql, ", ");

		_append_string_printf (sparql,
		                       "\"%s\" ",
		                       graphs[i]);
		first = FALSE;
	}

	_append_string (sparql, ")");
}

static TrackerContext *
_begin_triples_block (TrackerSparql *sparql)
{
	TrackerContext *context;

	context = tracker_triple_context_new ();
	tracker_sparql_push_context (sparql, context);

	return context;
}

static gboolean
_end_triples_block (TrackerSparql  *sparql,
		    GError        **error)
{
	TrackerTripleContext *triple_context;
	TrackerStringBuilder *where_placeholder;
	TrackerVariable *var;
	TrackerContext *context;
	GHashTableIter iter;
	gboolean first = TRUE;
	gint i;

	context = sparql->current_state.context;
	g_assert (TRACKER_IS_TRIPLE_CONTEXT (context));
	triple_context = (TrackerTripleContext *) context;

	/* Triple is empty */
	if (triple_context->sql_tables->len == 0) {
		tracker_sparql_pop_context (sparql, TRUE);
		return TRUE;
	}

	_append_string (sparql, "SELECT ");
	g_hash_table_iter_init (&iter, triple_context->variable_bindings);

	/* Add select variables */
	while (g_hash_table_iter_next (&iter, (gpointer *) &var, NULL)) {
		TrackerBinding *binding, *sample;
		GPtrArray *binding_list;

		binding_list = tracker_triple_context_get_variable_binding_list (triple_context,
		                                                                 var);
		if (!binding_list)
			continue;

		if (!first)
			_append_string (sparql, ", ");

		first = FALSE;
		binding = g_ptr_array_index (binding_list, 0);
		sample = TRACKER_BINDING (tracker_variable_get_sample_binding (var));

		if (sample &&
		    sample->data_type == TRACKER_PROPERTY_TYPE_STRING &&
		    binding->data_type == TRACKER_PROPERTY_TYPE_RESOURCE) {
			TrackerStringBuilder *str, *old;

			str = _append_placeholder (sparql);
			old = tracker_sparql_swap_builder (sparql, str);

			_append_string_printf (sparql, "%s ",
					       tracker_binding_get_sql_expression (binding));

			convert_expression_to_string (sparql, binding->data_type);
			_append_string_printf (sparql, "AS %s ", tracker_variable_get_sql_expression (var));

			tracker_sparql_swap_builder (sparql, old);
		} else {
			_append_string_printf (sparql, "%s AS %s ",
					       tracker_binding_get_sql_expression (binding),
					       tracker_variable_get_sql_expression (var));
		}
	}

	if (first)
		_append_string (sparql, "1 ");

	_append_string (sparql, "FROM ");
	first = TRUE;

	/* Add tables */
	for (i = 0; i < triple_context->sql_tables->len; i++) {
		TrackerDataTable *table = g_ptr_array_index (triple_context->sql_tables, i);

		if (!first)
			_append_string (sparql, ", ");

		if (table->predicate_variable) {
			_append_string (sparql,
			                "(SELECT subject AS ID, predicate, "
			                "object, object_type, graph FROM tracker_triples ");

			if (table->graph) {
				_append_graph_checks (sparql, "graph", FALSE,
						      &table->graph, 1);
			} else if (sparql->anon_graphs->len > 0 &&
				   tracker_token_is_empty (&sparql->current_state.graph)) {
				_append_graph_checks (sparql, "graph",
				                      !sparql->policy.filter_unnamed_graph,
						      (GStrv) sparql->anon_graphs->pdata,
						      sparql->anon_graphs->len);
			} else if (tracker_token_get_variable (&sparql->current_state.graph)) {
				if (sparql->named_graphs->len > 0) {
					_append_graph_checks (sparql, "graph", FALSE,
					                      (GStrv) sparql->named_graphs->pdata,
					                      sparql->named_graphs->len);
				} else if (sparql->policy.graphs) {
					_append_graph_checks (sparql, "graph", FALSE,
					                      (GStrv) sparql->policy.graphs->pdata,
					                      sparql->policy.graphs->len);
				} else {
					_append_string (sparql, "WHERE \"graph\" != 0 ");
				}
			} else if (sparql->policy.graphs) {
				_append_graph_checks (sparql, "graph",
				                      !sparql->policy.filter_unnamed_graph,
				                      (GStrv) sparql->policy.graphs->pdata,
				                      sparql->policy.graphs->len);
			}

			_append_string (sparql, ") ");
		} else if (table->predicate_path || table->fts) {
			_append_string_printf (sparql, "\"%s\" ", table->sql_db_tablename);
		} else {
			if (table->graph &&
			    tracker_sparql_find_graph (sparql, table->graph)) {
				_append_string_printf (sparql, "\"%s\".\"%s\" ",
				                       table->graph,
				                       table->sql_db_tablename);
			} else {
				_append_string_printf (sparql,
						       "(SELECT * FROM \"unionGraph_%s\" ",
						       table->sql_db_tablename);

				if (table->graph) {
					_append_graph_checks (sparql, "graph", FALSE,
					                      &table->graph, 1);
				} else if (sparql->anon_graphs->len > 0 &&
				           tracker_token_is_empty (&sparql->current_state.graph)) {
					_append_graph_checks (sparql, "graph",
					                      !sparql->policy.filter_unnamed_graph,
					                      (GStrv) sparql->anon_graphs->pdata,
					                      sparql->anon_graphs->len);
				} else if (tracker_token_get_variable (&sparql->current_state.graph)) {
					if (sparql->named_graphs->len > 0) {
						_append_graph_checks (sparql, "graph", FALSE,
						                      (GStrv) sparql->named_graphs->pdata,
						                      sparql->named_graphs->len);
					} else if (sparql->policy.graphs) {
						_append_graph_checks (sparql, "graph", FALSE,
						                      (GStrv) sparql->policy.graphs->pdata,
						                      sparql->policy.graphs->len);
					} else {
						_append_string (sparql, "WHERE \"graph\" != 0 ");
					}
				} else if (sparql->policy.graphs) {
					_append_graph_checks (sparql, "graph",
					                      !sparql->policy.filter_unnamed_graph,
					                      (GStrv) sparql->policy.graphs->pdata,
					                      sparql->policy.graphs->len);
				}

				_append_string (sparql, ") ");
			}
		}

		_append_string_printf (sparql, "AS \"%s\" ", table->sql_query_tablename);
		first = FALSE;
	}

	g_hash_table_iter_init (&iter, triple_context->variable_bindings);

	where_placeholder = _append_placeholder (sparql);
	first = TRUE;

	/* Add variable bindings */
	while (g_hash_table_iter_next (&iter, (gpointer *) &var, NULL)) {
		GPtrArray *binding_list;
		gboolean nullable = TRUE;
		guint i;

		binding_list = tracker_triple_context_lookup_variable_binding_list (triple_context,
										    var);
		if (!binding_list)
			continue;

		for (i = 0; i < binding_list->len; i++) {
			const gchar *expression1, *expression2;
			TrackerBinding *binding1, *binding2;

			binding1 = g_ptr_array_index (binding_list, i);
			if (!tracker_variable_binding_get_nullable (TRACKER_VARIABLE_BINDING (binding1)))
				nullable = FALSE;

			if (i + 1 >= binding_list->len)
				break;

			if (!first)
				_append_string (sparql, "AND ");

			/* Concatenate each binding with the next */
			binding2 = g_ptr_array_index (binding_list, i + 1);
			expression1 = tracker_binding_get_sql_expression (binding1);
			expression2 = tracker_binding_get_sql_expression (binding2);

			if (binding1->data_type == TRACKER_PROPERTY_TYPE_STRING &&
			    binding2->data_type == TRACKER_PROPERTY_TYPE_RESOURCE) {
				_append_string_printf (sparql,
				                       "(SELECT ID FROM Resource WHERE Uri = %s) ",
				                       expression1);
			} else {
				_append_string_printf (sparql, "%s ", expression1);
			}

			_append_string (sparql, "= ");

			if (binding1->data_type == TRACKER_PROPERTY_TYPE_RESOURCE &&
			    binding2->data_type == TRACKER_PROPERTY_TYPE_STRING) {
				_append_string_printf (sparql,
				                       "(SELECT ID FROM Resource WHERE Uri = %s) ",
				                       expression2);
			} else {
				_append_string_printf (sparql, "%s ", expression2);
			}

			if (!tracker_variable_binding_get_nullable (TRACKER_VARIABLE_BINDING (binding1)) ||
			    !tracker_variable_binding_get_nullable (TRACKER_VARIABLE_BINDING (binding2)))
				nullable = FALSE;

			first = FALSE;
		}

		if (nullable) {
			if (!first)
				_append_string (sparql, "AND ");
			_append_string_printf (sparql, "%s IS NOT NULL ",
			                       tracker_variable_get_sql_expression (var));
			first = FALSE;
		}
	}

	/* Add literal bindings */
	for (i = 0; i < triple_context->literal_bindings->len; i++) {
		TrackerBinding *binding;

		if (!first)
			_append_string (sparql, "AND ");

		first = FALSE;
		binding = g_ptr_array_index (triple_context->literal_bindings, i);
		if (binding->data_type == TRACKER_PROPERTY_TYPE_DATETIME) {
			_append_string_printf (sparql, "SparqlTimeSort (%s) = SparqlTimeSort (", tracker_binding_get_sql_expression (binding));
			_append_literal_sql (sparql, TRACKER_LITERAL_BINDING (binding));
			_append_string (sparql, ") ");
		} else {
			_append_string_printf (sparql, "%s = ", tracker_binding_get_sql_expression (binding));
			_append_literal_sql (sparql, TRACKER_LITERAL_BINDING (binding));
		}
	}

	/* If we had any where clauses, prepend the 'WHERE' literal */
	if (!first)
		tracker_string_builder_append (where_placeholder, "WHERE ", -1);

	tracker_sparql_pop_context (sparql, TRUE);

	return TRUE;
}

static gboolean
translate_Query (TrackerSparql  *sparql,
                 GError        **error)
{
	TrackerGrammarNamedRule rule;

	sparql->context = g_object_ref_sink (tracker_select_context_new ());
	sparql->current_state.select_context = sparql->context;
	tracker_sparql_push_context (sparql, sparql->context);

	sparql->current_state.union_views =
		g_hash_table_new_full (g_str_hash, g_str_equal,
		                       g_free, NULL);

	/* Query ::= Prologue
	 *           ( SelectQuery | ConstructQuery | DescribeQuery | AskQuery )
	 *           ValuesClause
	 */
	_call_rule (sparql, NAMED_RULE_Prologue, error);

	rule = _current_rule (sparql);

	switch (rule) {
	case NAMED_RULE_SelectQuery:
	case NAMED_RULE_AskQuery:
	case NAMED_RULE_ConstructQuery:
	case NAMED_RULE_DescribeQuery:
		_call_rule (sparql, rule, error);
		break;
	default:
		g_assert_not_reached ();
	}

	_call_rule (sparql, NAMED_RULE_ValuesClause, error);

	tracker_sparql_pop_context (sparql, FALSE);

	g_clear_pointer (&sparql->current_state.union_views,
	                 g_hash_table_unref);

	return TRUE;
}

static gboolean
translate_Update (TrackerSparql  *sparql,
                  GError        **error)
{
	/* Update ::= Prologue ( Update1 ( ';' Update )? )?
	 *
	 * TRACKER EXTENSION:
	 * ';' separator is made optional.
	 */
	_call_rule (sparql, NAMED_RULE_Prologue, error);

	if (!sparql->current_state.blank_node_map) {
		sparql->current_state.blank_node_map =
			g_hash_table_new_full (g_str_hash, g_str_equal,
			                       g_free, g_free);
	}

	if (_check_in_rule (sparql, NAMED_RULE_Update1)) {
		if (sparql->blank_nodes)
			g_variant_builder_open (sparql->blank_nodes, G_VARIANT_TYPE ("aa{ss}"));

		_call_rule (sparql, NAMED_RULE_Update1, error);

		if (sparql->blank_nodes)
			g_variant_builder_close (sparql->blank_nodes);

		_optional (sparql, RULE_TYPE_LITERAL, LITERAL_SEMICOLON);

		if (_check_in_rule (sparql, NAMED_RULE_Update))
			_call_rule (sparql, NAMED_RULE_Update, error);
	}

	return TRUE;
}

static void
tracker_sparql_add_select_var (TrackerSparql       *sparql,
			       const gchar         *name,
			       TrackerPropertyType  type)
{
	g_ptr_array_add (sparql->var_names, g_strdup (name));
	g_array_append_val (sparql->var_types, type);
}

static gboolean
handle_as (TrackerSparql        *sparql,
	   TrackerPropertyType   type,
	   GError              **error)
{
	TrackerBinding *binding;
	TrackerVariable *var;

	_call_rule (sparql, NAMED_RULE_Var, error);
	var = _last_node_variable (sparql);

	binding = tracker_variable_binding_new (var, NULL, NULL);
	tracker_binding_set_data_type (binding, type);
	tracker_variable_set_sample_binding (var, TRACKER_VARIABLE_BINDING (binding));
	_append_string_printf (sparql, "AS %s ",
			       tracker_variable_get_sql_expression (var));

	if (sparql->current_state.select_context == sparql->context)
		tracker_sparql_add_select_var (sparql, var->name, type);

	return TRUE;
}

static gboolean
translate_SelectClause (TrackerSparql  *sparql,
                        GError        **error)
{
	TrackerSelectContext *select_context;
	TrackerStringBuilder *str, *old;
	gboolean first = TRUE;

	/* SelectClause ::= 'SELECT' ( 'DISTINCT' | 'REDUCED' )? ( ( Var | ( '(' Expression 'AS' Var ')' ) )+ | '*' )
	 *
	 * TRACKER EXTENSION:
	 * Variable set also accepts the following syntax:
	 *   Expression ('AS' Var)?
	 */
	_expect (sparql, RULE_TYPE_LITERAL, LITERAL_SELECT);
	_append_string (sparql, "SELECT ");

	if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_DISTINCT)) {
		_append_string (sparql, "DISTINCT ");
	} else if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_REDUCED)) {
		/* REDUCED is allowed to return the same amount of elements, so... *shrug* */
	}

	select_context = TRACKER_SELECT_CONTEXT (sparql->current_state.select_context);

	if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_GLOB)) {
		TrackerVariable *var;
		GHashTableIter iter;

		if (!select_context->variables) {
			g_set_error (error, TRACKER_SPARQL_ERROR,
			             TRACKER_SPARQL_ERROR_TYPE,
			             "Glob used but no variables defined");
			return FALSE;
		}

		g_hash_table_iter_init (&iter, select_context->variables);

		while (g_hash_table_iter_next (&iter, NULL, (gpointer *) &var)) {
			/* Skip our own internal variables */
			if (strchr (var->name, ':'))
				continue;

			if (!first)
				_append_string (sparql, ", ");

			str = _append_placeholder (sparql);
			old = tracker_sparql_swap_builder (sparql, str);

			_append_string_printf (sparql, "%s ",
			                       tracker_variable_get_sql_expression (var));

			if (sparql->current_state.select_context == sparql->context) {
				TrackerPropertyType prop_type;

				prop_type = TRACKER_BINDING (tracker_variable_get_sample_binding (var))->data_type;
				convert_expression_to_string (sparql, prop_type);
			}

			if (sparql->current_state.select_context == sparql->context)
				_append_string_printf (sparql, "AS \"%s\" ", var->name);

			tracker_sparql_swap_builder (sparql, old);
			first = FALSE;
		}
	} else {
		do {
			TrackerVariable *var;
			TrackerBinding *binding;

			if (_check_in_rule (sparql, NAMED_RULE_Var)) {
				if (!first)
					_append_string (sparql, ", ");

				_call_rule (sparql, NAMED_RULE_Var, error);
				var = _last_node_variable (sparql);

				if (!tracker_variable_has_bindings (var)) {
					_raise (PARSE, "Undefined variable", var->name);
				}

				binding = TRACKER_BINDING (tracker_variable_get_sample_binding (var));

				str = _append_placeholder (sparql);
				old = tracker_sparql_swap_builder (sparql, str);

				_append_string_printf (sparql, "%s ",
				                       tracker_variable_get_sql_expression (var));

				if (sparql->current_state.select_context == sparql->context)
					convert_expression_to_string (sparql, binding->data_type);

				select_context->type = binding->data_type;

				if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_AS)) {
					if (!handle_as (sparql, binding->data_type, error))
						return FALSE;
				} else if (sparql->current_state.select_context == sparql->context) {
					tracker_sparql_add_select_var (sparql, var->name, binding->data_type);
				}

				tracker_sparql_swap_builder (sparql, old);
			} else {
				gboolean parens = FALSE;

				if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_OPEN_PARENS))
					parens = TRUE;
				else if (!_check_in_rule (sparql, NAMED_RULE_Expression))
					break;

				if (!first)
					_append_string (sparql, ", ");

				str = _append_placeholder (sparql);
				old = tracker_sparql_swap_builder (sparql, str);
				_call_rule (sparql, NAMED_RULE_Expression, error);

				if (sparql->current_state.select_context == sparql->context)
					convert_expression_to_string (sparql, sparql->current_state.expression_type);

				select_context->type = sparql->current_state.expression_type;

				if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_AS)) {
					if (!handle_as (sparql, sparql->current_state.expression_type, error))
						return FALSE;
				} else {
					tracker_sparql_add_select_var (sparql, "", sparql->current_state.expression_type);
				}

				tracker_sparql_swap_builder (sparql, old);

				if (parens)
					_expect (sparql, RULE_TYPE_LITERAL, LITERAL_CLOSE_PARENS);
			}

			first = FALSE;
		} while (TRUE);
	}

	return TRUE;
}

static gboolean
translate_Prologue (TrackerSparql  *sparql,
                    GError        **error)
{
	TrackerGrammarNamedRule rule;

	/* Prologue ::= ( BaseDecl | PrefixDecl | ConstraintDecl )*
	 *
	 * TRACKER EXTENSION:
	 * ConstraintDecl entirely.
	 */
	rule = _current_rule (sparql);

	while (rule == NAMED_RULE_BaseDecl ||
	       rule == NAMED_RULE_PrefixDecl ||
	       rule == NAMED_RULE_ConstraintDecl) {
		_call_rule (sparql, rule, error);
		rule = _current_rule (sparql);
	}

	return TRUE;
}

static gboolean
translate_BaseDecl (TrackerSparql  *sparql,
                    GError        **error)
{
	/* BaseDecl ::= 'BASE' IRIREF
	 */
	_expect (sparql, RULE_TYPE_LITERAL, LITERAL_BASE);
	_expect (sparql, RULE_TYPE_TERMINAL, TERMINAL_TYPE_IRIREF);

	/* Sparql syntax allows for multiple BaseDecl, but it only makes
	 * sense to keep one. Given that the sparql1.1-query recommendation
	 * does not define the behavior, just pick the first one.
	 */
	if (!sparql->base)
		sparql->base = _dup_last_string (sparql);

	return TRUE;
}

static gboolean
translate_PrefixDecl (TrackerSparql  *sparql,
		      GError        **error)
{
	gchar *ns, *uri;

	/* PrefixDecl ::= 'PREFIX' PNAME_NS IRIREF
	 */
	_expect (sparql, RULE_TYPE_LITERAL, LITERAL_PREFIX);

	_expect (sparql, RULE_TYPE_TERMINAL, TERMINAL_TYPE_PNAME_NS);
	ns = _dup_last_string (sparql);

	_expect (sparql, RULE_TYPE_TERMINAL, TERMINAL_TYPE_IRIREF);
	uri = _dup_last_string (sparql);

	g_hash_table_insert (sparql->prefix_map, ns, uri);

	return TRUE;
}

static void
intersect_set (GPtrArray *array,
               GPtrArray *set)
{
	const gchar *set_graph, *graph;
	gint i = 0, j;
	gboolean found;

	while (i < array->len) {
		graph = g_ptr_array_index (array, i);
		found = FALSE;

		for (j = 0; j < set->len; j++) {
			set_graph = g_ptr_array_index (set, j);

			if (g_strcmp0 (set_graph, graph) == 0) {
				found = TRUE;
				break;
			}
		}

		if (found) {
			i++;
		} else {
			g_ptr_array_remove_index_fast (array, i);
		}
	}
}

static gboolean
translate_ConstraintDecl (TrackerSparql  *sparql,
                          GError        **error)
{
	GPtrArray **previous_set, *set;
	gboolean graph = FALSE;
	gboolean filter_unnamed_graph = TRUE;

	/* ConstraintDecl ::= 'CONSTRAINT' ( 'GRAPH' | 'SERVICE' ) ( ( IRIREF | 'DEFAULT' | 'ALL' ) ( ',' ( IRIREF | 'DEFAULT' | 'ALL' ) )* )?
	 *
	 * TRACKER EXTENSION
	 */
	_expect (sparql, RULE_TYPE_LITERAL, LITERAL_CONSTRAINT);

	if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_GRAPH)) {
		previous_set = &sparql->policy.graphs;
		graph = TRUE;
	} else if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_SERVICE)) {
		previous_set = &sparql->policy.services;
	} else {
		g_assert_not_reached ();
	}

	set = g_ptr_array_new_with_free_func (g_free);

	do {
		gchar *elem;

		if (_accept (sparql, RULE_TYPE_TERMINAL, TERMINAL_TYPE_IRIREF)) {
			if (set) {
				elem = _dup_last_string (sparql);
				g_ptr_array_add (set, elem);
			}
		} else if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_DEFAULT)) {
			if (graph)
				filter_unnamed_graph = FALSE;
		} else if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_ALL)) {
			if (graph)
				filter_unnamed_graph = FALSE;
			g_clear_pointer (&set, g_ptr_array_unref);
		} else {
			break;
		}
	} while (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_COMMA));

	if (*previous_set) {
		if (set) {
			intersect_set (*previous_set, set);
			g_ptr_array_unref (set);
		}
	} else {
		*previous_set = set;
	}

	if (graph) {
		sparql->policy.filter_unnamed_graph |= filter_unnamed_graph;
		g_clear_pointer (&sparql->policy.filtered_graphs,
		                 g_hash_table_unref);
	}

	return TRUE;
}

static gboolean
_check_undefined_variables (TrackerSparql         *sparql,
                            TrackerSelectContext  *context,
                            GError               **error)
{
	TrackerVariable *variable;
	GHashTableIter iter;

	if (!context->variables)
		return TRUE;

	g_hash_table_iter_init (&iter, context->variables);

	while (g_hash_table_iter_next (&iter, NULL, (gpointer*) &variable)) {
		if (!tracker_variable_has_bindings (variable)) {
			_raise (PARSE, "Use of undefined variable", variable->name);
		}
	}

	return TRUE;
}

static gboolean
_postprocess_rule (TrackerSparql         *sparql,
                   TrackerParserNode     *node,
                   TrackerStringBuilder  *str,
                   GError               **error)
{
	TrackerStringBuilder *old_str;
	TrackerParserNode *old_node;
	const TrackerGrammarRule *rule;

	old_node = sparql->current_state.node;
	sparql->current_state.node = node;
	if (str)
		old_str = tracker_sparql_swap_builder (sparql, str);

	rule = tracker_parser_node_get_rule (node);
	g_assert (rule->type == RULE_TYPE_RULE);
	_call_rule (sparql, rule->data.rule, error);
	sparql->current_state.node = old_node;

	if (str)
		tracker_sparql_swap_builder (sparql, old_str);

	return TRUE;
}

static gboolean
translate_SelectQuery (TrackerSparql  *sparql,
                       GError        **error)
{
	TrackerParserNode *select_clause;
	TrackerStringBuilder *select, *str, *old;

	/* SelectQuery ::= SelectClause DatasetClause* WhereClause SolutionModifier
	 */

	/* Skip select clause here */
	select = _append_placeholder (sparql);
	str = _append_placeholder (sparql);
	old = tracker_sparql_swap_builder (sparql, str);
	select_clause = _skip_rule (sparql, NAMED_RULE_SelectClause);

	while (_check_in_rule (sparql, NAMED_RULE_DatasetClause)) {
		_call_rule (sparql, NAMED_RULE_DatasetClause, error);
	}

	/* Optimize single graph case, so it always resorts to querying
	 * the specific database.
	 */
	if (sparql->named_graphs->len + sparql->anon_graphs->len == 1) {
		const gchar *graph = NULL;

		if (sparql->named_graphs->len > 0)
			graph = g_ptr_array_index (sparql->named_graphs, 0);
		else if (sparql->anon_graphs->len > 0)
			graph = g_ptr_array_index (sparql->anon_graphs, 0);

		if (graph)
			tracker_token_literal_init (&sparql->current_state.graph, graph, -1);
	}

	_call_rule (sparql, NAMED_RULE_WhereClause, error);

	if (!_check_undefined_variables (sparql, TRACKER_SELECT_CONTEXT (sparql->context), error))
		return FALSE;

	_call_rule (sparql, NAMED_RULE_SolutionModifier, error);

	tracker_sparql_swap_builder (sparql, old);

	/* Now that we have all variable/binding information available,
	 * process the select clause.
	 */
	if (!_postprocess_rule (sparql, select_clause, select, error))
		return FALSE;

	return TRUE;
}

static gboolean
translate_SubSelect (TrackerSparql  *sparql,
                     GError        **error)
{
	TrackerContext *context, *prev;
	TrackerStringBuilder *select, *str, *old;
	TrackerParserNode *select_clause;

	/* SubSelect ::= SelectClause WhereClause SolutionModifier ValuesClause
	 */
	context = tracker_select_context_new ();
	prev = sparql->current_state.select_context;
	sparql->current_state.select_context = context;
	tracker_sparql_push_context (sparql, context);

	/* Skip select clause here */
	select = _append_placeholder (sparql);
	str = _append_placeholder (sparql);
	old = tracker_sparql_swap_builder (sparql, str);

	select_clause = _skip_rule (sparql, NAMED_RULE_SelectClause);

	_call_rule (sparql, NAMED_RULE_WhereClause, error);

	_call_rule (sparql, NAMED_RULE_SolutionModifier, error);
	_call_rule (sparql, NAMED_RULE_ValuesClause, error);

	tracker_sparql_swap_builder (sparql, old);

	/* Now that we have all variable/binding information available,
	 * process the select clause.
	 */
	if (!_postprocess_rule (sparql, select_clause, select, error))
		return FALSE;

	sparql->current_state.expression_type = TRACKER_SELECT_CONTEXT (context)->type;
	tracker_sparql_pop_context (sparql, FALSE);
	sparql->current_state.select_context = prev;

	return TRUE;
}

static gboolean
translate_ConstructQuery (TrackerSparql  *sparql,
                          GError        **error)
{
	TrackerParserNode *node = NULL;
	TrackerStringBuilder *old;

	/* ConstructQuery ::= 'CONSTRUCT' ( ConstructTemplate DatasetClause* WhereClause SolutionModifier |
	 *                                  DatasetClause* 'WHERE' '{' TriplesTemplate? '}' SolutionModifier )
	 */
	_expect (sparql, RULE_TYPE_LITERAL, LITERAL_CONSTRUCT);
	sparql->current_state.construct_query = tracker_string_builder_new ();

	if (_current_rule (sparql) == NAMED_RULE_ConstructTemplate) {
		node = _skip_rule (sparql, NAMED_RULE_ConstructTemplate);

		old = tracker_sparql_swap_builder (sparql, sparql->current_state.construct_query);

		_append_string (sparql, "SELECT * ");

		while (_current_rule (sparql) == NAMED_RULE_DatasetClause)
			_call_rule (sparql, NAMED_RULE_DatasetClause, error);

		_call_rule (sparql, NAMED_RULE_WhereClause, error);
		_call_rule (sparql, NAMED_RULE_SolutionModifier, error);
		tracker_sparql_swap_builder (sparql, old);

		sparql->current_state.type = TRACKER_SPARQL_TYPE_CONSTRUCT;
		if (!_postprocess_rule (sparql, node, NULL, error))
			return FALSE;
	} else {
		while (_current_rule (sparql) == NAMED_RULE_DatasetClause)
			_call_rule (sparql, NAMED_RULE_DatasetClause, error);

		_expect (sparql, RULE_TYPE_LITERAL, LITERAL_WHERE);
		_expect (sparql, RULE_TYPE_LITERAL, LITERAL_OPEN_BRACE);

		if (_current_rule (sparql) == NAMED_RULE_TriplesTemplate) {
			node = _skip_rule (sparql, NAMED_RULE_TriplesTemplate);

			old = tracker_sparql_swap_builder (sparql, sparql->current_state.construct_query);

			_begin_triples_block (sparql);
			if (!_postprocess_rule (sparql, node, NULL, error))
				return FALSE;
			if (!_end_triples_block (sparql, error))
				return FALSE;

			_expect (sparql, RULE_TYPE_LITERAL, LITERAL_CLOSE_BRACE);
			_call_rule (sparql, NAMED_RULE_SolutionModifier, error);
			tracker_sparql_swap_builder (sparql, old);

			/* Switch to construct mode, and rerun again the triples template */
			sparql->current_state.type = TRACKER_SPARQL_TYPE_CONSTRUCT;
			if (!_postprocess_rule (sparql, node, NULL, error))
				return FALSE;
		} else {
			_append_string (sparql, "SELECT NULL ");
			_expect (sparql, RULE_TYPE_LITERAL, LITERAL_CLOSE_BRACE);
			_call_rule (sparql, NAMED_RULE_SolutionModifier, error);
		}
	}

	return TRUE;
}

static gboolean
translate_DescribeQuery (TrackerSparql  *sparql,
                         GError        **error)
{
	TrackerStringBuilder *where_str = NULL;
	TrackerVariable *variable;
	TrackerBinding *binding;
	GList *resources = NULL, *l;
	gboolean glob = FALSE;

	/* DescribeQuery ::= 'DESCRIBE' ( VarOrIri+ | '*' ) DatasetClause* WhereClause? SolutionModifier
	 */
	_expect (sparql, RULE_TYPE_LITERAL, LITERAL_DESCRIBE);
	_append_string (sparql,
	                "SELECT "
	                "  (SELECT Uri FROM Resource WHERE ID = subject),"
	                "  (SELECT Uri FROM Resource WHERE ID = predicate),"
	                "  object "
	                "FROM tracker_triples ");

	if (sparql->policy.graphs) {
		_append_graph_checks (sparql, "graph",
		                      !sparql->policy.filter_unnamed_graph,
		                      (GStrv) sparql->policy.graphs->pdata,
		                      sparql->policy.graphs->len);
		_append_string (sparql, "AND ");
	} else {
		_append_string (sparql, "WHERE ");
	}
	_append_string (sparql,
	                "object IS NOT NULL AND subject IN (");

	if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_GLOB)) {
		glob = TRUE;
	} else {
		TrackerContext *context;

		context = tracker_triple_context_new ();
		tracker_sparql_push_context (sparql, context);

		while (_check_in_rule (sparql, NAMED_RULE_VarOrIri)) {
			TrackerBinding *binding;
			TrackerToken resource;

			_call_rule (sparql, NAMED_RULE_VarOrIri, error);
			_init_token (&resource, sparql->current_state.prev_node, sparql);

			if (tracker_token_get_literal (&resource)) {
				binding = tracker_literal_binding_new (tracker_token_get_literal (&resource),
				                                       NULL);
			} else {
				TrackerVariable *variable;

				variable = tracker_token_get_variable (&resource);
				binding = tracker_variable_binding_new (variable, NULL, NULL);
			}

			tracker_binding_set_data_type (binding, TRACKER_PROPERTY_TYPE_RESOURCE);
			resources = g_list_prepend (resources, binding);
			tracker_token_unset (&resource);
		}

		tracker_sparql_pop_context (sparql, FALSE);
	}

	while (_check_in_rule (sparql, NAMED_RULE_DatasetClause))
		_call_rule (sparql, NAMED_RULE_DatasetClause, error);

	if (_check_in_rule (sparql, NAMED_RULE_WhereClause)) {
		TrackerParserNode *where_clause;

		where_str = tracker_string_builder_new ();
		where_clause = _skip_rule (sparql, NAMED_RULE_WhereClause);

		if (!_postprocess_rule (sparql, where_clause, where_str, error)) {
			g_list_free_full (resources, g_object_unref);
			tracker_string_builder_free (where_str);
			return FALSE;
		}
	}

	if (glob && TRACKER_SELECT_CONTEXT (sparql->context)->variables) {
		GHashTableIter iter;

		g_hash_table_iter_init (&iter, TRACKER_SELECT_CONTEXT (sparql->context)->variables);

		while (g_hash_table_iter_next (&iter, NULL, (gpointer *) &variable)) {
			binding = tracker_variable_binding_new (variable, NULL, NULL);
			resources = g_list_prepend (resources, binding);
		}
	}

	if (resources == NULL) {
		g_clear_pointer (&where_str, tracker_string_builder_free);
		_raise (PARSE, "Use of unprojected variables", "DescribeQuery");
	}

	for (l = resources; l; l = l->next) {
		binding = l->data;

		if (l != resources)
			_append_string (sparql, "UNION ALL ");

		if (TRACKER_IS_LITERAL_BINDING (binding)) {
			tracker_select_context_add_literal_binding (TRACKER_SELECT_CONTEXT (sparql->context),
			                                            TRACKER_LITERAL_BINDING (binding));
			_append_string (sparql, "SELECT ");
			_append_literal_sql (sparql, TRACKER_LITERAL_BINDING (binding));
		} else if (TRACKER_IS_VARIABLE_BINDING (binding)) {
			gchar *str;

			variable = TRACKER_VARIABLE_BINDING (binding)->variable;

			if (!where_str) {
				g_list_free_full (resources, g_object_unref);
				_raise (PARSE, "Use of unprojected variable in Describe", variable->name);
			}

			_append_string (sparql, "SELECT ");
			_append_variable_sql (sparql, variable);

			str = tracker_string_builder_to_string (where_str);
			_append_string_printf (sparql, "%s", str);
			g_free (str);
		}
	}

	_call_rule (sparql, NAMED_RULE_SolutionModifier, error);
	_append_string (sparql, ") ");
	g_list_free_full (resources, g_object_unref);
	g_clear_pointer (&where_str, tracker_string_builder_free);

	return TRUE;
}

static gboolean
translate_AskQuery (TrackerSparql  *sparql,
                    GError        **error)
{
	TrackerStringBuilder *str, *old;

	/* AskQuery ::= 'ASK' DatasetClause* WhereClause SolutionModifier
	 */
	_expect (sparql, RULE_TYPE_LITERAL, LITERAL_ASK);

	_append_string (sparql, "SELECT CASE EXISTS (SELECT 1 ");

	while (_check_in_rule (sparql, NAMED_RULE_DatasetClause)) {
		_call_rule (sparql, NAMED_RULE_DatasetClause, error);
	}

	str = _append_placeholder (sparql);
	old = tracker_sparql_swap_builder (sparql, str);
	_call_rule (sparql, NAMED_RULE_WhereClause, error);
	_call_rule (sparql, NAMED_RULE_SolutionModifier, error);

	tracker_sparql_swap_builder (sparql, old);

	_append_string (sparql, ") WHEN 1 THEN 'true' WHEN 0 THEN 'false' ELSE NULL END");

	return TRUE;
}

static gboolean
translate_DatasetClause (TrackerSparql  *sparql,
                         GError        **error)
{
	TrackerGrammarNamedRule rule;

	/* DatasetClause ::= 'FROM' ( DefaultGraphClause | NamedGraphClause )
	 */
	_expect (sparql, RULE_TYPE_LITERAL, LITERAL_FROM);

	rule = _current_rule (sparql);

	switch (rule) {
	case NAMED_RULE_DefaultGraphClause:
	case NAMED_RULE_NamedGraphClause:
		_call_rule (sparql, rule, error);
		break;
	default:
		g_assert_not_reached ();
	}

	return TRUE;
}

static gboolean
translate_DefaultGraphClause (TrackerSparql  *sparql,
                              GError        **error)
{
	gchar *graph;

	/* DefaultGraphClause ::= SourceSelector
	 */
	_call_rule (sparql, NAMED_RULE_SourceSelector, error);

	graph = g_strdup (tracker_token_get_idstring (&sparql->current_state.graph));
	g_ptr_array_add (sparql->anon_graphs, graph);
	tracker_token_unset (&sparql->current_state.graph);

	return TRUE;
}

static gboolean
translate_NamedGraphClause (TrackerSparql  *sparql,
                            GError        **error)
{
	gchar *graph;

	/* NamedGraphClause ::= 'NAMED' SourceSelector
	 */
	_expect (sparql, RULE_TYPE_LITERAL, LITERAL_NAMED);
	_call_rule (sparql, NAMED_RULE_SourceSelector, error);

	graph = g_strdup (tracker_token_get_idstring (&sparql->current_state.graph));
	g_ptr_array_add (sparql->named_graphs, graph);
	tracker_token_unset (&sparql->current_state.graph);

	return TRUE;
}

static gboolean
translate_SourceSelector (TrackerSparql  *sparql,
                          GError        **error)
{
	/* SourceSelector ::= iri
	 */
	_call_rule (sparql, NAMED_RULE_iri, error);
	_init_token (&sparql->current_state.graph,
	             sparql->current_state.prev_node, sparql);
	return TRUE;
}

static gboolean
translate_WhereClause (TrackerSparql  *sparql,
                       GError        **error)
{
	TrackerStringBuilder *child, *old;

	/* WhereClause ::= 'WHERE'? GroupGraphPattern
	 */
	child = _append_placeholder (sparql);
	old = tracker_sparql_swap_builder (sparql, child);
	_optional (sparql, RULE_TYPE_LITERAL, LITERAL_WHERE);
	_call_rule (sparql, NAMED_RULE_GroupGraphPattern, error);

	if (!tracker_string_builder_is_empty (child)) {
		_prepend_string (sparql, "FROM (");
		_append_string (sparql, ") ");
	}

	tracker_sparql_swap_builder (sparql, old);

	return TRUE;
}

static gboolean
translate_SolutionModifier (TrackerSparql  *sparql,
                            GError        **error)
{
	/* SolutionModifier ::= GroupClause? HavingClause? OrderClause? LimitOffsetClauses?
	 */
	if (_check_in_rule (sparql, NAMED_RULE_GroupClause)) {
		_call_rule (sparql, NAMED_RULE_GroupClause, error);
	}

	if (_check_in_rule (sparql, NAMED_RULE_HavingClause)) {
		_call_rule (sparql, NAMED_RULE_HavingClause, error);
	}

	if (_check_in_rule (sparql, NAMED_RULE_OrderClause)) {
		_call_rule (sparql, NAMED_RULE_OrderClause, error);
	}

	if (_check_in_rule (sparql, NAMED_RULE_LimitOffsetClauses)) {
		_call_rule (sparql, NAMED_RULE_LimitOffsetClauses, error);
	}

	return TRUE;
}

static gboolean
translate_GroupClause (TrackerSparql  *sparql,
                       GError        **error)
{
	GList *conditions = NULL, *expressions = NULL, *l;
	gboolean variables_projected = FALSE;
	TrackerStringBuilder *select, *old;
	gchar *str;

	/* GroupClause ::= 'GROUP' 'BY' GroupCondition+
	 */
	_expect (sparql, RULE_TYPE_LITERAL, LITERAL_GROUP);
	_expect (sparql, RULE_TYPE_LITERAL, LITERAL_BY);

	/* As we still may get variables projected in the GroupCondition
	 * clauses, we need to preprocess those so we can do a final
	 * SELECT *, $variables FROM (...) surrounding the resulting
	 * query.
	 */
	while (_check_in_rule (sparql, NAMED_RULE_GroupCondition)) {
		TrackerParserNode *node;

		node = _skip_rule (sparql, NAMED_RULE_GroupCondition);
		conditions = g_list_prepend (conditions, node);
	}

	for (l = conditions; l; l = l->next) {
		TrackerStringBuilder *expr;

		expr = tracker_string_builder_new ();

		if (!_postprocess_rule (sparql, l->data, expr, error)) {
			g_object_unref (expr);
			g_list_free_full (expressions, g_object_unref);
			g_list_free (conditions);
			return FALSE;
		}

		if (sparql->current_state.as_in_group_by) {
			TrackerVariableBinding *binding = sparql->current_state.as_in_group_by;
			TrackerVariable *var = tracker_variable_binding_get_variable (binding);

			if (!variables_projected) {
				select = _prepend_placeholder (sparql);
				old = tracker_sparql_swap_builder (sparql, select);
				variables_projected = TRUE;
				_append_string (sparql, "FROM (SELECT * ");
			}

			_append_string (sparql, ", ");

			str = tracker_string_builder_to_string (expr);
			tracker_string_builder_append (select, str, -1);
			g_free (str);

			_append_string (sparql, "AS ");
			_append_variable_sql (sparql, var);
			expressions = g_list_prepend (expressions,
			                              g_strdup (tracker_variable_get_sql_expression (var)));
			g_clear_object (&sparql->current_state.as_in_group_by);
		} else {
			str = tracker_string_builder_to_string (expr);
			expressions = g_list_prepend (expressions, str);
		}

		tracker_string_builder_free (expr);
	}

	if (variables_projected) {
		tracker_sparql_swap_builder (sparql, old);
		_append_string (sparql, ") ");
	}

	_append_string (sparql, "GROUP BY ");

	for (l = expressions; l; l = l->next) {
		if (l != expressions)
			_append_string (sparql, ", ");

		_append_string_printf (sparql, "%s ", l->data);
	}

	g_list_free_full (expressions, g_free);
	g_list_free (conditions);

	return TRUE;
}

static gboolean
translate_GroupCondition (TrackerSparql  *sparql,
                          GError        **error)
{
	/* GroupCondition ::= BuiltInCall | FunctionCall | '(' Expression ( 'AS' Var )? ')' | Var
	 */
	sparql->current_state.as_in_group_by = NULL;

	if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_OPEN_PARENS)) {
		TrackerPropertyType expr_type;
		_call_rule (sparql, NAMED_RULE_Expression, error);
		expr_type = sparql->current_state.expression_type;

		if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_AS)) {
			TrackerVariable *var;
			TrackerBinding *binding;

			_call_rule (sparql, NAMED_RULE_Var, error);
			var = _last_node_variable (sparql);

			binding = tracker_variable_binding_new (var, NULL, NULL);
			tracker_binding_set_data_type (binding, expr_type);
			tracker_variable_set_sample_binding (var, TRACKER_VARIABLE_BINDING (binding));

			sparql->current_state.as_in_group_by = TRACKER_VARIABLE_BINDING (binding);
		}

		_expect (sparql, RULE_TYPE_LITERAL, LITERAL_CLOSE_PARENS);
	} else {
		TrackerGrammarNamedRule rule;
		TrackerVariable *variable;

		rule = _current_rule (sparql);

		switch (rule) {
		case NAMED_RULE_Var:
			_call_rule (sparql, rule, error);
			variable = _last_node_variable (sparql);
			_append_variable_sql (sparql, variable);
			break;
		case NAMED_RULE_BuiltInCall:
		case NAMED_RULE_FunctionCall:
			_call_rule (sparql, rule, error);
			break;
		default:
			g_assert_not_reached ();
		}
	}

	return TRUE;
}

static gboolean
translate_HavingClause (TrackerSparql  *sparql,
                        GError        **error)
{
	/* HavingClause ::= 'HAVING' HavingCondition+
	 */
	_expect (sparql, RULE_TYPE_LITERAL, LITERAL_HAVING);
	_append_string (sparql, "HAVING ");

	while (_check_in_rule (sparql, NAMED_RULE_HavingCondition)) {
		_call_rule (sparql, NAMED_RULE_HavingCondition, error);
	}

	return TRUE;
}

static gboolean
translate_HavingCondition (TrackerSparql  *sparql,
                           GError        **error)
{
	/* HavingCondition ::= Constraint
	 */
	_call_rule (sparql, NAMED_RULE_Constraint, error);
	return TRUE;
}

static gboolean
translate_OrderClause (TrackerSparql  *sparql,
                       GError        **error)
{
	gboolean first = TRUE;

	/* OrderClause ::= 'ORDER' 'BY' OrderCondition+
	 */
	_expect (sparql, RULE_TYPE_LITERAL, LITERAL_ORDER);
	_expect (sparql, RULE_TYPE_LITERAL, LITERAL_BY);
	_append_string (sparql, "ORDER BY ");

	while (_check_in_rule (sparql, NAMED_RULE_OrderCondition)) {
		if (!first)
			_append_string (sparql, ", ");
		_call_rule (sparql, NAMED_RULE_OrderCondition, error);
		first = FALSE;
	}

	return TRUE;
}

static gboolean
translate_OrderCondition (TrackerSparql  *sparql,
                          GError        **error)
{
	TrackerStringBuilder *str, *old;
	const gchar *order_str = NULL;

	str = _append_placeholder (sparql);
	old = tracker_sparql_swap_builder (sparql, str);

	/* OrderCondition ::= ( ( 'ASC' | 'DESC' ) BrackettedExpression )
	 *                    | ( Constraint | Var )
	 *
	 * TRACKER EXTENSION:
	 * plain Expression is also accepted, the last group is:
	 * ( Constraint | Var | Expression )
	 */
	if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_ASC)) {
		_call_rule (sparql, NAMED_RULE_Expression, error);
		order_str = "ASC ";
	} else if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_DESC)) {
		_call_rule (sparql, NAMED_RULE_Expression, error);
		order_str = "DESC ";
	} else if (_check_in_rule (sparql, NAMED_RULE_Constraint)) {
		_call_rule (sparql, NAMED_RULE_Constraint, error);
	} else if (_check_in_rule (sparql, NAMED_RULE_Var)) {
		TrackerVariableBinding *binding;
		TrackerVariable *variable;

		_call_rule (sparql, NAMED_RULE_Var, error);

		variable = _last_node_variable (sparql);
		_append_variable_sql (sparql, variable);

		binding = tracker_variable_get_sample_binding (variable);
		if (binding)
			sparql->current_state.expression_type = TRACKER_BINDING (binding)->data_type;
	} else {
		g_assert_not_reached ();
	}

	if (sparql->current_state.expression_type == TRACKER_PROPERTY_TYPE_STRING ||
	    sparql->current_state.expression_type == TRACKER_PROPERTY_TYPE_LANGSTRING)
		_append_string (sparql, "COLLATE " TRACKER_COLLATION_NAME " ");
	else if (sparql->current_state.expression_type == TRACKER_PROPERTY_TYPE_RESOURCE)
		convert_expression_to_string (sparql, sparql->current_state.expression_type);

	tracker_sparql_swap_builder (sparql, old);

	if (order_str)
		_append_string (sparql, order_str);

	return TRUE;
}

static gboolean
translate_LimitOffsetClauses (TrackerSparql  *sparql,
                              GError        **error)
{
	TrackerBinding *limit = NULL, *offset = NULL;

	/* LimitOffsetClauses ::= LimitClause OffsetClause? | OffsetClause LimitClause?
	 */
	if (_check_in_rule (sparql, NAMED_RULE_LimitClause)) {
		_call_rule (sparql, NAMED_RULE_LimitClause, error);
		limit = _convert_terminal (sparql);

		if (_check_in_rule (sparql, NAMED_RULE_OffsetClause)) {
			_call_rule (sparql, NAMED_RULE_OffsetClause, error);
			offset = _convert_terminal (sparql);
		}
	} else if (_check_in_rule (sparql, NAMED_RULE_OffsetClause)) {
		_call_rule (sparql, NAMED_RULE_OffsetClause, error);
		offset = _convert_terminal (sparql);

		if (_check_in_rule (sparql, NAMED_RULE_LimitClause)) {
			_call_rule (sparql, NAMED_RULE_LimitClause, error);
			limit = _convert_terminal (sparql);
		}
	} else {
		g_assert_not_reached ();
	}

	if (limit) {
		_append_string (sparql, "LIMIT ");
		tracker_select_context_add_literal_binding (TRACKER_SELECT_CONTEXT (sparql->context),
		                                            TRACKER_LITERAL_BINDING (limit));
		_append_literal_sql (sparql, TRACKER_LITERAL_BINDING (limit));
		g_object_unref (limit);
	}

	if (offset) {
		_append_string (sparql, "OFFSET ");
		tracker_select_context_add_literal_binding (TRACKER_SELECT_CONTEXT (sparql->context),
		                                            TRACKER_LITERAL_BINDING (offset));
		_append_literal_sql (sparql, TRACKER_LITERAL_BINDING (offset));
		g_object_unref (offset);
	}

	return TRUE;
}

static gboolean
translate_LimitClause (TrackerSparql  *sparql,
                       GError        **error)
{
	/* LimitClause ::= 'LIMIT' INTEGER
	 */
	_expect (sparql, RULE_TYPE_LITERAL, LITERAL_LIMIT);
	_expect (sparql, RULE_TYPE_TERMINAL, TERMINAL_TYPE_INTEGER);
	sparql->current_state.expression_type = TRACKER_PROPERTY_TYPE_INTEGER;

	return TRUE;
}

static gboolean
translate_OffsetClause (TrackerSparql  *sparql,
                        GError        **error)
{
	/* OffsetClause ::= 'OFFSET' INTEGER
	 */
	_expect (sparql, RULE_TYPE_LITERAL, LITERAL_OFFSET);
	_expect (sparql, RULE_TYPE_TERMINAL, TERMINAL_TYPE_INTEGER);
	sparql->current_state.expression_type = TRACKER_PROPERTY_TYPE_INTEGER;

	return TRUE;
}

static gboolean
translate_ValuesClause (TrackerSparql  *sparql,
                        GError        **error)
{
	/* ValuesClause ::= ( 'VALUES' DataBlock )?
	 */
	if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_VALUES)) {
		_prepend_string (sparql, "SELECT * FROM (");
		_append_string (sparql, ") NATURAL INNER JOIN (");
		_call_rule (sparql, NAMED_RULE_DataBlock, error);
		_append_string (sparql, ") ");
	}

	return TRUE;
}

static gboolean
translate_Update1 (TrackerSparql  *sparql,
                   GError        **error)
{
	TrackerGrammarNamedRule rule;
	GError *inner_error = NULL;

	sparql->current_state.union_views =
		g_hash_table_new_full (g_str_hash, g_str_equal,
		                       g_free, NULL);

	/* Update1 ::= Load | Clear | Drop | Add | Move | Copy | Create | InsertData | DeleteData | DeleteWhere | Modify
	 */
	rule = _current_rule (sparql);

	switch (rule) {
	case NAMED_RULE_Load:
	case NAMED_RULE_Clear:
	case NAMED_RULE_Drop:
	case NAMED_RULE_Add:
	case NAMED_RULE_Move:
	case NAMED_RULE_Copy:
	case NAMED_RULE_Create:
	case NAMED_RULE_InsertData:
	case NAMED_RULE_DeleteData:
	case NAMED_RULE_DeleteWhere:
	case NAMED_RULE_Modify:
		_call_rule (sparql, rule, error);
		break;
	default:
		g_assert_not_reached ();
	}

	g_clear_pointer (&sparql->current_state.union_views, g_hash_table_unref);

	tracker_data_update_buffer_flush (tracker_data_manager_get_data (sparql->data_manager),
	                                  &inner_error);
	if (inner_error) {
		g_propagate_error (error, inner_error);
		return FALSE;
	} else {
		return TRUE;
	}
}

static gboolean
translate_Load (TrackerSparql  *sparql,
                GError        **error)
{
	TrackerToken resource;
	gboolean silent = FALSE;
	GError *inner_error = NULL;
	const gchar *graph = NULL;
	GFile *file;

	_expect (sparql, RULE_TYPE_LITERAL, LITERAL_LOAD);

	if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_SILENT))
		silent = TRUE;

	_call_rule (sparql, NAMED_RULE_iri, error);
	_init_token (&resource, sparql->current_state.prev_node, sparql);

	if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_INTO)) {
		_call_rule (sparql, NAMED_RULE_GraphRefAll, error);

		if (!tracker_token_is_empty (&sparql->current_state.graph))
			graph = tracker_token_get_idstring (&sparql->current_state.graph);
	}

	file = g_file_new_for_uri (tracker_token_get_idstring (&resource));
	tracker_token_unset (&resource);

	tracker_data_load_turtle_file (tracker_data_manager_get_data (sparql->data_manager),
	                               file, graph, &inner_error);

	if (inner_error) {
		g_clear_object (&file);

		if (!silent) {
			g_propagate_error (error, inner_error);
			return FALSE;
		}
	}

	return TRUE;
}

static gboolean
handle_silent (gboolean   silent,
               GError    *error_in,
               GError   **error)
{
	if (!error_in) {
		return TRUE;
	} else if (silent) {
		g_error_free (error_in);
		return TRUE;
	} else {
		g_propagate_error (error, error_in);
		return FALSE;
	}
}

static gboolean
translate_Clear (TrackerSparql  *sparql,
                 GError        **error)
{
	gboolean silent = FALSE;
	GError *inner_error = NULL;
	GList *graphs = NULL, *l;
	const gchar *graph;

	/* Clear ::= 'CLEAR' 'SILENT'? GraphRefAll
	 */
	_expect (sparql, RULE_TYPE_LITERAL, LITERAL_CLEAR);

	if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_SILENT))
		silent = TRUE;

	_call_rule (sparql, NAMED_RULE_GraphRefAll, error);

	if (tracker_token_is_empty (&sparql->current_state.graph)) {
		if (sparql->current_state.graph_op == GRAPH_OP_DEFAULT ||
		    sparql->current_state.graph_op == GRAPH_OP_ALL) {
			graphs = g_list_prepend (graphs, "main");
		}

		if (sparql->current_state.graph_op == GRAPH_OP_ALL ||
		    sparql->current_state.graph_op == GRAPH_OP_NAMED) {
			GHashTable *ht;
			GHashTableIter iter;

			ht = tracker_sparql_get_effective_graphs (sparql);
			g_hash_table_iter_init (&iter, ht);

			while (g_hash_table_iter_next (&iter, (gpointer *) &graph, NULL))
				graphs = g_list_prepend (graphs, (gpointer) graph);
		}
	} else {
		graph = tracker_token_get_idstring (&sparql->current_state.graph);

		if (tracker_sparql_find_graph (sparql, graph) == 0)
			_raise (UNKNOWN_GRAPH, "Unknown graph", graph);

		graphs = g_list_prepend (graphs, (gpointer) graph);
	}

	for (l = graphs; l; l = l->next) {
		if (!tracker_data_manager_clear_graph (sparql->data_manager,
		                                       l->data, &inner_error))
			break;
	}

	g_list_free (graphs);

	return handle_silent (silent, inner_error, error);
}

static gboolean
translate_Drop (TrackerSparql  *sparql,
                GError        **error)
{
	gboolean silent = FALSE;
	GError *inner_error = NULL;
	GList *graphs = NULL, *l;
	const gchar *graph;

	/* Drop ::= 'DROP' 'SILENT'? GraphRefAll
	 */
	_expect (sparql, RULE_TYPE_LITERAL, LITERAL_DROP);

	if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_SILENT))
		silent = TRUE;

	_call_rule (sparql, NAMED_RULE_GraphRefAll, error);

	if (tracker_token_is_empty (&sparql->current_state.graph)) {
		if (sparql->current_state.graph_op == GRAPH_OP_DEFAULT ||
		    sparql->current_state.graph_op == GRAPH_OP_ALL) {
			graphs = g_list_prepend (graphs, NULL);
		}

		if (sparql->current_state.graph_op == GRAPH_OP_ALL ||
		    sparql->current_state.graph_op == GRAPH_OP_NAMED) {
			GHashTable *ht;
			GHashTableIter iter;

			ht = tracker_sparql_get_effective_graphs (sparql);
			g_hash_table_iter_init (&iter, ht);

			while (g_hash_table_iter_next (&iter, (gpointer *) &graph, NULL))
				graphs = g_list_prepend (graphs, (gpointer) graph);
		}
	} else {
		graph = tracker_token_get_idstring (&sparql->current_state.graph);
		graphs = g_list_prepend (graphs, (gpointer) graph);
	}

	for (l = graphs; l; l = l->next) {
		if (!tracker_data_manager_drop_graph (sparql->data_manager,
		                                      l->data, &inner_error))
			break;
	}

	g_list_free (graphs);

	return handle_silent (silent, inner_error, error);
}

static gboolean
translate_Create (TrackerSparql  *sparql,
                  GError        **error)
{
	gboolean silent = FALSE;
	GError *inner_error = NULL;
	const gchar *graph_name;

	/* Create ::= 'CREATE' 'SILENT'? GraphRef
	 */
	_expect (sparql, RULE_TYPE_LITERAL, LITERAL_CREATE);

	if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_SILENT))
		silent = TRUE;

	_call_rule (sparql, NAMED_RULE_GraphRef, error);
	g_assert (!tracker_token_is_empty (&sparql->current_state.graph));

	graph_name = tracker_token_get_idstring (&sparql->current_state.graph);

	if (tracker_sparql_find_graph (sparql, graph_name) != 0) {
		inner_error = g_error_new (TRACKER_SPARQL_ERROR,
		                           TRACKER_SPARQL_ERROR_CONSTRAINT,
		                           "Graph '%s' already exists",
		                           graph_name);
		goto error;
	}

	if (!tracker_data_manager_create_graph (sparql->data_manager,
	                                        graph_name,
	                                        &inner_error))
		goto error;

	return TRUE;

error:
	return handle_silent (silent, inner_error, error);
}

static gboolean
translate_Add (TrackerSparql  *sparql,
               GError        **error)
{
	gboolean silent = FALSE;
	gchar *source, *destination;
	GError *inner_error = NULL;

	/* Add ::= 'ADD' 'SILENT'? GraphOrDefault 'TO' GraphOrDefault
	 */
	_expect (sparql, RULE_TYPE_LITERAL, LITERAL_ADD);

	if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_SILENT))
		silent = TRUE;

	_call_rule (sparql, NAMED_RULE_GraphOrDefault, error);
	source = g_strdup (tracker_token_get_idstring (&sparql->current_state.graph));

	_expect (sparql, RULE_TYPE_LITERAL, LITERAL_TO);

	_call_rule (sparql, NAMED_RULE_GraphOrDefault, error);
	destination = g_strdup (tracker_token_get_idstring (&sparql->current_state.graph));

	if (g_strcmp0 (source, destination) == 0) {
		g_free (source);
		g_free (destination);
		return TRUE;
	}

	if (source &&
	    !tracker_sparql_find_graph (sparql, source)) {
		g_set_error (&inner_error, TRACKER_SPARQL_ERROR,
			     TRACKER_SPARQL_ERROR_UNKNOWN_GRAPH,
			     "Unknown graph '%s'", source);
		goto error;
	}

	if (destination &&
	    !tracker_sparql_find_graph (sparql, destination)) {
		if (!tracker_data_manager_create_graph (sparql->data_manager,
							destination, &inner_error))
			goto error;
	}

	if (!tracker_data_manager_copy_graph (sparql->data_manager,
					      source, destination,
					      &inner_error))
		goto error;

	g_free (source);
	g_free (destination);

	return TRUE;

error:

	g_free (source);
	g_free (destination);

	return handle_silent (silent, inner_error, error);
}

static gboolean
translate_Move (TrackerSparql  *sparql,
                GError        **error)
{
	gboolean silent = FALSE;
	gchar *source, *destination;
	GError *inner_error = NULL;

	/* Move ::= 'MOVE' 'SILENT'? GraphOrDefault 'TO' GraphOrDefault
	 */
	_expect (sparql, RULE_TYPE_LITERAL, LITERAL_MOVE);

	if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_SILENT))
		silent = TRUE;

	_call_rule (sparql, NAMED_RULE_GraphOrDefault, error);
	source = g_strdup (tracker_token_get_idstring (&sparql->current_state.graph));

	_expect (sparql, RULE_TYPE_LITERAL, LITERAL_TO);

	_call_rule (sparql, NAMED_RULE_GraphOrDefault, error);
	destination = g_strdup (tracker_token_get_idstring (&sparql->current_state.graph));

	if (g_strcmp0 (source, destination) == 0) {
		g_free (source);
		g_free (destination);
		return TRUE;
	}

	if (source &&
	    !tracker_sparql_find_graph (sparql, source)) {
		g_set_error (&inner_error, TRACKER_SPARQL_ERROR,
			     TRACKER_SPARQL_ERROR_UNKNOWN_GRAPH,
			     "Unknown graph '%s'", source);
		goto error;
	}

	if (destination &&
	    !tracker_sparql_find_graph (sparql, destination)) {
		if (!tracker_data_manager_create_graph (sparql->data_manager,
							destination, &inner_error))
			goto error;
	} else {
		if (!tracker_data_manager_clear_graph (sparql->data_manager,
						       destination, &inner_error))
			goto error;
	}

	if (!tracker_data_manager_copy_graph (sparql->data_manager,
	                                      source, destination,
	                                      &inner_error))
		goto error;

	if (!tracker_data_manager_drop_graph (sparql->data_manager,
	                                      source,
	                                      &inner_error))
		goto error;

	g_free (source);
	g_free (destination);

	return TRUE;

error:
	g_free (source);
	g_free (destination);

	return handle_silent (silent, inner_error, error);
}

static gboolean
translate_Copy (TrackerSparql  *sparql,
                GError        **error)
{
	gboolean silent = FALSE;
	gchar *source, *destination;
	GError *inner_error = NULL;

	/* Copy ::= 'COPY' 'SILENT'? GraphOrDefault 'TO' GraphOrDefault
	 */
	_expect (sparql, RULE_TYPE_LITERAL, LITERAL_COPY);

	if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_SILENT))
		silent = TRUE;

	_call_rule (sparql, NAMED_RULE_GraphOrDefault, error);
	g_assert (!tracker_token_is_empty (&sparql->current_state.graph) ||
		  sparql->current_state.graph_op == GRAPH_OP_DEFAULT);
	source = g_strdup (tracker_token_get_idstring (&sparql->current_state.graph));

	_expect (sparql, RULE_TYPE_LITERAL, LITERAL_TO);

	_call_rule (sparql, NAMED_RULE_GraphOrDefault, error);
	g_assert (!tracker_token_is_empty (&sparql->current_state.graph) ||
		  sparql->current_state.graph_op == GRAPH_OP_DEFAULT);
	destination = g_strdup (tracker_token_get_idstring (&sparql->current_state.graph));

	if (g_strcmp0 (source, destination) == 0) {
		g_free (source);
		g_free (destination);
		return TRUE;
	}

	if (source &&
	    !tracker_sparql_find_graph (sparql, source)) {
		g_set_error (&inner_error, TRACKER_SPARQL_ERROR,
			     TRACKER_SPARQL_ERROR_UNKNOWN_GRAPH,
			     "Unknown graph '%s'", source);
		goto error;
	}

	if (destination &&
	    !tracker_sparql_find_graph (sparql, destination)) {
		if (!tracker_data_manager_create_graph (sparql->data_manager,
							destination, &inner_error))
			goto error;
	} else {
		if (!tracker_data_manager_clear_graph (sparql->data_manager,
						       destination, &inner_error))
			goto error;
	}

	if (!tracker_data_manager_copy_graph (sparql->data_manager,
	                                      source, destination,
	                                      &inner_error))
		goto error;

	g_free (source);
	g_free (destination);

	return TRUE;

error:
	g_free (source);
	g_free (destination);

	return handle_silent (silent, inner_error, error);
}

static gboolean
translate_InsertData (TrackerSparql  *sparql,
                      GError        **error)
{
	/* InsertData ::= 'INSERT DATA' QuadData
	 */
	_expect (sparql, RULE_TYPE_LITERAL, LITERAL_INSERT);
	_expect (sparql, RULE_TYPE_LITERAL, LITERAL_DATA);

	if (sparql->blank_nodes) {
		sparql->current_state.update_blank_nodes =
			g_hash_table_new (g_str_hash, g_str_equal);
		g_variant_builder_open (sparql->blank_nodes, G_VARIANT_TYPE ("a{ss}"));
	}

	sparql->current_state.type = TRACKER_SPARQL_TYPE_INSERT;
	_call_rule (sparql, NAMED_RULE_QuadData, error);

	if (sparql->blank_nodes) {
		g_clear_pointer (&sparql->current_state.update_blank_nodes,
		                 g_hash_table_unref);
		g_variant_builder_close (sparql->blank_nodes);
	}

	return TRUE;
}

static gboolean
translate_DeleteData (TrackerSparql  *sparql,
                      GError        **error)
{
	/* DeleteData ::= 'DELETE DATA' QuadData
	 */
	_expect (sparql, RULE_TYPE_LITERAL, LITERAL_DELETE);
	_expect (sparql, RULE_TYPE_LITERAL, LITERAL_DATA);

	sparql->current_state.type = TRACKER_SPARQL_TYPE_DELETE;
	_call_rule (sparql, NAMED_RULE_QuadData, error);

	return TRUE;
}

static gboolean
prepare_solution_select (TrackerSparql      *sparql,
                         TrackerParserNode  *pattern,
                         GError            **error)
{
	TrackerSelectContext *select_context;
	TrackerVariable *var;
	GHashTableIter iter;
	TrackerStringBuilder *outer_select;

	_begin_triples_block (sparql);

	if (!_postprocess_rule (sparql, pattern, NULL, error))
		return FALSE;

	if (!_end_triples_block (sparql, error))
		return FALSE;

	/* Surround by select to casts all variables to text */
	_append_string (sparql, ")");

	select_context = TRACKER_SELECT_CONTEXT (sparql->context);

	outer_select = _prepend_placeholder (sparql);
	tracker_sparql_swap_builder (sparql, outer_select);
	_append_string (sparql, "SELECT ");

	if (select_context->variables) {
		gboolean first = TRUE;

		g_hash_table_iter_init (&iter, select_context->variables);

		while (g_hash_table_iter_next (&iter, NULL, (gpointer *) &var)) {
			TrackerStringBuilder *str, *old;
			TrackerPropertyType prop_type;

			if (!first)
				_append_string (sparql, ", ");

			str = _append_placeholder (sparql);
			old = tracker_sparql_swap_builder (sparql, str);

			_append_string_printf (sparql, "%s ",
			                       tracker_variable_get_sql_expression (var));
			prop_type = TRACKER_BINDING (tracker_variable_get_sample_binding (var))->data_type;
			convert_expression_to_string (sparql, prop_type);
			tracker_sparql_swap_builder (sparql, old);

			_append_string_printf (sparql, "AS \"%s\" ", var->name);
			first = FALSE;
		}
	} else {
		_append_string (sparql, "1 ");
	}

	_append_string (sparql, "FROM (");
	return TRUE;
}

static TrackerSolution *
get_solution_for_pattern (TrackerSparql      *sparql,
                          TrackerParserNode  *pattern,
                          GError            **error)
{
	TrackerDBStatement *stmt;
	TrackerDBInterface *iface;
	TrackerDBCursor *cursor;
	TrackerSolution *solution;
	gint i, n_cols;
	gboolean retval;

	sparql->current_state.type = TRACKER_SPARQL_TYPE_SELECT;
	sparql->context = g_object_ref_sink (tracker_select_context_new ());
	sparql->current_state.select_context = sparql->context;
	tracker_sparql_push_context (sparql, sparql->context);

	tracker_sparql_init_string_builder (sparql);

	retval = prepare_solution_select (sparql, pattern, error);
	tracker_sparql_pop_context (sparql, FALSE);

	if (!retval) {
		g_clear_object (&sparql->context);
		return NULL;
	}

	iface = tracker_data_manager_get_writable_db_interface (sparql->data_manager);
	stmt = prepare_query (sparql, iface, sparql->sql,
	                      TRACKER_SELECT_CONTEXT (sparql->context)->literal_bindings,
	                      NULL, TRUE,
	                      error);
	g_clear_object (&sparql->context);

	if (!stmt)
		return NULL;

	cursor = tracker_db_statement_start_sparql_cursor (stmt,
	                                                   NULL, 0,
	                                                   NULL, 0,
							   error);
	g_object_unref (stmt);

	if (!cursor)
		return NULL;

	n_cols = tracker_db_cursor_get_n_columns (cursor);
	solution = tracker_solution_new (n_cols);

	for (i = 0; i < n_cols; i++) {
		const gchar *name = tracker_db_cursor_get_variable_name (cursor, i);
		tracker_solution_add_column_name (solution, name);
	}

	while (tracker_db_cursor_iter_next (cursor, NULL, NULL)) {
		for (i = 0; i < n_cols; i++) {
			GValue value = G_VALUE_INIT;

			tracker_db_cursor_get_value (cursor, i, &value);

			if (G_VALUE_TYPE (&value) == G_TYPE_STRING) {
				tracker_solution_add_value (solution,
				                            g_value_get_string (&value));
			} else if (G_VALUE_TYPE (&value) == G_TYPE_INT64) {
				gchar *str;
				str = g_strdup_printf ("%" G_GINT64_FORMAT,
				                       g_value_get_int64 (&value));
				tracker_solution_add_value (solution, str);
				g_free (str);
			} else if (G_VALUE_TYPE (&value) == G_TYPE_DOUBLE) {
				gchar buf[G_ASCII_DTOSTR_BUF_SIZE];
				g_ascii_dtostr (buf, sizeof (buf),
				                g_value_get_double (&value));
				tracker_solution_add_value (solution, buf);
			} else if (G_VALUE_TYPE (&value) == G_TYPE_INVALID) {
				tracker_solution_add_value (solution, NULL);
			} else {
				g_assert_not_reached ();
			}

			g_value_unset (&value);
		}
	}

	g_object_unref (cursor);

	return solution;
}

static gboolean
iterate_solution (TrackerSparql      *sparql,
                  TrackerSolution    *solution,
                  TrackerParserNode  *node,
                  GError            **error)
{
	gboolean retval = TRUE;

	tracker_solution_rewind (solution);

	while (retval && tracker_solution_next (solution)) {
		GError *flush_error = NULL;

		sparql->solution_var_map = tracker_solution_get_bindings (solution);
		retval = _postprocess_rule (sparql, node, NULL, error);
		g_clear_pointer (&sparql->solution_var_map, g_hash_table_unref);

		tracker_data_update_buffer_might_flush (tracker_data_manager_get_data (sparql->data_manager),
		                                        &flush_error);
		if (flush_error) {
			g_propagate_error (error, flush_error);
			retval = FALSE;
		}
	}

	return retval;
}

static gboolean
translate_DeleteWhere (TrackerSparql  *sparql,
                       GError        **error)
{
	TrackerParserNode *quad_pattern;
	TrackerSolution *solution;
	gboolean retval;

	/* DeleteWhere ::= 'DELETE WHERE' QuadPattern
	 */
	_expect (sparql, RULE_TYPE_LITERAL, LITERAL_DELETE);
	_expect (sparql, RULE_TYPE_LITERAL, LITERAL_WHERE);

	quad_pattern = _skip_rule (sparql, NAMED_RULE_QuadPattern);

	/* 'DELETE WHERE' uses the same pattern for both query and update */
	solution = get_solution_for_pattern (sparql, quad_pattern, error);
	if (!solution)
		return FALSE;

	sparql->current_state.type = TRACKER_SPARQL_TYPE_DELETE;
	retval = iterate_solution (sparql, solution, quad_pattern, error);
	tracker_solution_free (solution);

	return retval;
}

static gboolean
translate_Modify (TrackerSparql  *sparql,
                  GError        **error)
{
	TrackerParserNode *delete = NULL, *insert = NULL, *where = NULL;
	TrackerSolution *solution;
	gboolean retval = TRUE;

	/* Modify ::= ( 'WITH' iri )? ( DeleteClause InsertClause? | InsertClause ) UsingClause* 'WHERE' GroupGraphPattern
	 *
	 * TRACKER EXTENSION:
	 * Last part of the clause is:
	 * ('WHERE' GroupGraphPattern)?
	 */
	if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_WITH)) {
		_call_rule (sparql, NAMED_RULE_iri, error);
		_init_token (&sparql->current_state.graph,
		             sparql->current_state.prev_node, sparql);
	}

	if (_check_in_rule (sparql, NAMED_RULE_DeleteClause)) {
		delete = _skip_rule (sparql, NAMED_RULE_DeleteClause);
	}

	if (_check_in_rule (sparql, NAMED_RULE_InsertClause)) {
		insert = _skip_rule (sparql, NAMED_RULE_InsertClause);
	}

	while (_check_in_rule (sparql, NAMED_RULE_UsingClause)) {
		_call_rule (sparql, NAMED_RULE_UsingClause, error);
	}

	if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_WHERE)) {
		where = _skip_rule (sparql, NAMED_RULE_GroupGraphPattern);
		solution = get_solution_for_pattern (sparql, where, error);
		if (!solution)
			return FALSE;
	} else {
		solution = tracker_solution_new (1);
		tracker_solution_add_value (solution, "");
	}

	if (delete) {
		retval = iterate_solution (sparql, solution, delete, error);
	}

	/* Flush in between */
	if (retval && delete && insert) {
		GError *flush_error = NULL;

		tracker_data_update_buffer_flush (tracker_data_manager_get_data (sparql->data_manager),
		                                  &flush_error);
		if (flush_error) {
			g_propagate_error (error, flush_error);
			retval = FALSE;
		}
	}

	if (insert && retval) {
		retval = iterate_solution (sparql, solution, insert, error);
	}

	tracker_solution_free (solution);

	return retval;
}

static gboolean
translate_DeleteClause (TrackerSparql  *sparql,
                        GError        **error)
{
	/* DeleteClause ::= 'DELETE' QuadPattern
	 *
	 * TRACKER EXTENSION:
	 * Clause may start too with:
	 * 'DELETE' 'SILENT'
	 */
	sparql->current_state.type = TRACKER_SPARQL_TYPE_DELETE;
	_expect (sparql, RULE_TYPE_LITERAL, LITERAL_DELETE);
	sparql->silent = _accept (sparql, RULE_TYPE_LITERAL, LITERAL_SILENT);

	_call_rule (sparql, NAMED_RULE_QuadPattern, error);

	return TRUE;
}

static gboolean
translate_InsertClause (TrackerSparql  *sparql,
                        GError        **error)
{
	TrackerToken old_graph;
	gboolean into = FALSE;

	/* InsertClause ::= 'INSERT' QuadPattern
	 *
	 * TRACKER EXTENSION:
	 * Clause may start with:
	 * 'INSERT' ('OR' 'REPLACE')? ('SILENT')? ('INTO' iri)?
	 */
	if (sparql->blank_nodes) {
		sparql->current_state.update_blank_nodes =
			g_hash_table_new (g_str_hash, g_str_equal);
		g_variant_builder_open (sparql->blank_nodes, G_VARIANT_TYPE ("a{ss}"));
	}

	sparql->current_state.type = TRACKER_SPARQL_TYPE_INSERT;
	_expect (sparql, RULE_TYPE_LITERAL, LITERAL_INSERT);

	if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_OR)) {
		_expect (sparql, RULE_TYPE_LITERAL, LITERAL_REPLACE);
		sparql->current_state.type = TRACKER_SPARQL_TYPE_UPDATE;
	} else {
		sparql->current_state.type = TRACKER_SPARQL_TYPE_INSERT;
	}

	sparql->silent = _accept (sparql, RULE_TYPE_LITERAL, LITERAL_SILENT);

	if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_INTO)) {
		old_graph = sparql->current_state.graph;
		_call_rule (sparql, NAMED_RULE_iri, error);
		_init_token (&sparql->current_state.graph,
		             sparql->current_state.prev_node, sparql);
		into = TRUE;
	}

	_call_rule (sparql, NAMED_RULE_QuadPattern, error);

	if (into) {
		tracker_token_unset (&sparql->current_state.graph);
		sparql->current_state.graph = old_graph;
	}

	if (sparql->blank_nodes) {
		g_clear_pointer (&sparql->current_state.update_blank_nodes,
		                 g_hash_table_unref);
		g_variant_builder_close (sparql->blank_nodes);
	}

	return TRUE;
}

static gboolean
translate_UsingClause (TrackerSparql  *sparql,
                       GError        **error)
{
	gboolean named = FALSE;
	gchar *graph;

	/* UsingClause ::= 'USING' ( iri | 'NAMED' iri )
	 */
	_expect (sparql, RULE_TYPE_LITERAL, LITERAL_USING);

	if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_NAMED))
		named = TRUE;

	_call_rule (sparql, NAMED_RULE_iri, error);
	_init_token (&sparql->current_state.graph,
		     sparql->current_state.prev_node, sparql);
	graph = g_strdup (tracker_token_get_idstring (&sparql->current_state.graph));

	if (named)
		g_ptr_array_add (sparql->named_graphs, graph);
	else
		g_ptr_array_add (sparql->anon_graphs, graph);

	tracker_token_unset (&sparql->current_state.graph);
	g_free (graph);

	return TRUE;
}

static gboolean
translate_GraphOrDefault (TrackerSparql  *sparql,
                          GError        **error)
{
	/* GraphOrDefault ::= 'DEFAULT' | 'GRAPH'? iri
	 */
	if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_DEFAULT)) {
		tracker_token_unset (&sparql->current_state.graph);
		sparql->current_state.graph_op = GRAPH_OP_DEFAULT;
	} else {
		_optional (sparql, RULE_TYPE_LITERAL, LITERAL_GRAPH);
		_call_rule (sparql, NAMED_RULE_iri, error);
		_init_token (&sparql->current_state.graph,
		             sparql->current_state.prev_node, sparql);
	}

	return TRUE;
}

static gboolean
translate_GraphRefAll (TrackerSparql  *sparql,
                       GError        **error)
{
	/* GraphRefAll ::= GraphRef | 'DEFAULT' | 'NAMED' | 'ALL'
	 */
	if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_DEFAULT)) {
		sparql->current_state.graph_op = GRAPH_OP_DEFAULT;
	} else if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_NAMED)) {
		sparql->current_state.graph_op = GRAPH_OP_NAMED;
	} else if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_ALL)) {
		sparql->current_state.graph_op = GRAPH_OP_ALL;
	} else {
		_call_rule (sparql, NAMED_RULE_GraphRef, error);
	}

	return TRUE;
}

static gboolean
translate_GraphRef (TrackerSparql  *sparql,
                    GError        **error)
{
	/* GraphRef ::= 'GRAPH' iri
	 */
	_expect (sparql, RULE_TYPE_LITERAL, LITERAL_GRAPH);
	_call_rule (sparql, NAMED_RULE_iri, error);
	_init_token (&sparql->current_state.graph,
	             sparql->current_state.prev_node, sparql);

	return TRUE;
}

static gboolean
translate_QuadPattern (TrackerSparql  *sparql,
                       GError        **error)
{
	/* QuadPattern ::= '{' Quads '}'
	 */
	_expect (sparql, RULE_TYPE_LITERAL, LITERAL_OPEN_BRACE);
	_call_rule (sparql, NAMED_RULE_Quads, error);
	_expect (sparql, RULE_TYPE_LITERAL, LITERAL_CLOSE_BRACE);

	return TRUE;
}

static gboolean
translate_QuadData (TrackerSparql  *sparql,
                    GError        **error)
{
	/* QuadData ::= '{' Quads '}'
	 */
	_expect (sparql, RULE_TYPE_LITERAL, LITERAL_OPEN_BRACE);
	_call_rule (sparql, NAMED_RULE_Quads, error);
	_expect (sparql, RULE_TYPE_LITERAL, LITERAL_CLOSE_BRACE);

	return TRUE;
}

static gboolean
translate_Quads (TrackerSparql  *sparql,
                 GError        **error)
{
	/* Quads ::= TriplesTemplate? ( QuadsNotTriples '.'? TriplesTemplate? )*
	 */
	if (_check_in_rule (sparql, NAMED_RULE_TriplesTemplate)) {
		_call_rule (sparql, NAMED_RULE_TriplesTemplate, error);
	}

	while (_check_in_rule (sparql, NAMED_RULE_QuadsNotTriples)) {
		_call_rule (sparql, NAMED_RULE_QuadsNotTriples, error);

		_optional (sparql, RULE_TYPE_LITERAL, LITERAL_DOT);

		if (_check_in_rule (sparql, NAMED_RULE_TriplesTemplate)) {
			_call_rule (sparql, NAMED_RULE_TriplesTemplate, error);
		}
	}

	return TRUE;
}

static gboolean
translate_QuadsNotTriples (TrackerSparql  *sparql,
                           GError        **error)
{
	TrackerToken old_graph;

	/* QuadsNotTriples ::= 'GRAPH' VarOrIri '{' TriplesTemplate? '}'
	 */
	old_graph = sparql->current_state.graph;

	_expect (sparql, RULE_TYPE_LITERAL, LITERAL_GRAPH);

	_call_rule (sparql, NAMED_RULE_VarOrIri, error);
	_init_token (&sparql->current_state.graph,
	             sparql->current_state.prev_node, sparql);

	_expect (sparql, RULE_TYPE_LITERAL, LITERAL_OPEN_BRACE);

	if (_check_in_rule (sparql, NAMED_RULE_TriplesTemplate)) {
		_call_rule (sparql, NAMED_RULE_TriplesTemplate, error);
	}

	_expect (sparql, RULE_TYPE_LITERAL, LITERAL_CLOSE_BRACE);
	tracker_token_unset (&sparql->current_state.graph);
	sparql->current_state.graph = old_graph;

	return TRUE;
}

static gboolean
translate_TriplesTemplate (TrackerSparql  *sparql,
                           GError        **error)
{
	/* TriplesTemplate ::= TriplesSameSubject ( '.' TriplesTemplate? )?
	 */
	_call_rule (sparql, NAMED_RULE_TriplesSameSubject, error);

	if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_DOT)) {
		if (_check_in_rule (sparql, NAMED_RULE_TriplesTemplate)) {
			_call_rule (sparql, NAMED_RULE_TriplesTemplate, error);
		}
	}

	return TRUE;
}

static gboolean
translate_GroupGraphPatternSub (TrackerSparql  *sparql,
                                GError        **error)
{
	TrackerStringBuilder *child, *old;
	TrackerParserNode *root;

	/* GroupGraphPatternSub ::= TriplesBlock? ( GraphPatternNotTriples '.'? TriplesBlock? )*
	 */
	root = (TrackerParserNode *) ((GNode *) sparql->current_state.node)->parent;
	child = _append_placeholder (sparql);
	old = tracker_sparql_swap_builder (sparql, child);

	if (_check_in_rule (sparql, NAMED_RULE_TriplesBlock)) {
		_begin_triples_block (sparql);
		_call_rule (sparql, NAMED_RULE_TriplesBlock, error);
		if (!_end_triples_block (sparql, error))
			return FALSE;
	}

	while (_check_in_rule (sparql, NAMED_RULE_GraphPatternNotTriples)) {
		/* XXX: In the older code there was a minor optimization for
		 * simple OPTIONAL {} clauses. Two cases where handled where the
		 * optional is added inside the triples block:
		 *
		 * 1) OPTIONAL { ?u <p> ?o }, where ?u would be already bound
		 *    in the non optional part, <p> is a single-valued property,
		 *    and ?o is unbound. The binding representing pred/obj would
		 *    be folded into the previous triples block, simply without:
		 *
		 *    [AND] "var" IS NOT NULL
		 *
		 * 2) OPTIONAL { ?u <p> ?o }, where ?o is bound in the non optional
		 *    part, <p> is an InverseFunctionalProperty and ?u is unbound.
		 *    The previous triples block select clause would contain:
		 *
		 *    SELECT ...,
		 *           (SELECT ID FROM "$prop_table" WHERE "$prop" = "$table_in_from_clause"."$prop") AS ...,
		 *           ...
		 *
		 *    i.e. the resource ID is obtained in a subquery.
		 *
		 *    The first one could be useful way more frequently than the
		 *    second, and both involved substantial complications to SQL
		 *    query preparation, so they have been left out at the moment.
		 */
		_call_rule (sparql, NAMED_RULE_GraphPatternNotTriples, error);
		_optional (sparql, RULE_TYPE_LITERAL, LITERAL_DOT);

		if (_check_in_rule (sparql, NAMED_RULE_TriplesBlock)) {
			gboolean do_join;

			do_join = !tracker_string_builder_is_empty (sparql->current_state.sql);

			if (do_join) {
				_prepend_string (sparql, "SELECT * FROM (");
				_append_string (sparql, ") NATURAL INNER JOIN (");
			}

			_begin_triples_block (sparql);
			_call_rule (sparql, NAMED_RULE_TriplesBlock, error);
			if (!_end_triples_block (sparql, error))
				return FALSE;

			if (do_join)
				_append_string (sparql, ") ");
		}
	}

	/* Handle filters last, they apply to the pattern as a whole */
	if (sparql->filter_clauses) {
		GList *filters = sparql->filter_clauses;
		gboolean first = TRUE;

		while (filters) {
			TrackerParserNode *filter_node = filters->data;
			GList *elem = filters;

			filters = filters->next;

			if (!g_node_is_ancestor ((GNode *) root, (GNode *) filter_node))
				continue;

			if (first) {
				if (tracker_string_builder_is_empty (sparql->current_state.sql)) {
					_prepend_string (sparql, "SELECT 1 ");
					_append_string (sparql, "WHERE ");
				} else {
					_prepend_string (sparql, "SELECT * FROM (");
					_append_string (sparql, ") WHERE ");
				}
				first = FALSE;
			} else {
				_append_string (sparql, "AND ");
			}

			if (!_postprocess_rule (sparql, filter_node,
			                        NULL, error))
				return FALSE;

			sparql->filter_clauses =
				g_list_delete_link (sparql->filter_clauses, elem);
		}
	}

	tracker_sparql_swap_builder (sparql, old);

	return TRUE;
}

static gboolean
translate_TriplesBlock (TrackerSparql  *sparql,
                        GError        **error)
{
	/* TriplesBlock ::= TriplesSameSubjectPath ( '.' TriplesBlock? )?
	 */
	_call_rule (sparql, NAMED_RULE_TriplesSameSubjectPath, error);

	if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_DOT)) {
		if (_check_in_rule (sparql, NAMED_RULE_TriplesBlock)) {
			_call_rule (sparql, NAMED_RULE_TriplesBlock, error);
		}
	}

	return TRUE;
}

static gboolean
translate_GraphPatternNotTriples (TrackerSparql  *sparql,
                                  GError        **error)
{
	TrackerGrammarNamedRule rule;

	/* GraphPatternNotTriples ::= GroupOrUnionGraphPattern | OptionalGraphPattern | MinusGraphPattern | GraphGraphPattern | ServiceGraphPattern | Filter | Bind | InlineData
	 */
	rule = _current_rule (sparql);

	switch (rule) {
	case NAMED_RULE_GroupOrUnionGraphPattern:
	case NAMED_RULE_OptionalGraphPattern:
	case NAMED_RULE_MinusGraphPattern:
	case NAMED_RULE_GraphGraphPattern:
	case NAMED_RULE_ServiceGraphPattern:
	case NAMED_RULE_Filter:
	case NAMED_RULE_Bind:
	case NAMED_RULE_InlineData:
		_call_rule (sparql, rule, error);
		break;
	default:
		g_assert_not_reached ();
	}

	return TRUE;
}

static gboolean
translate_OptionalGraphPattern (TrackerSparql  *sparql,
                                GError        **error)
{
	gboolean do_join;

	/* OptionalGraphPattern ::= 'OPTIONAL' GroupGraphPattern
	 */
	do_join = !tracker_string_builder_is_empty (sparql->current_state.sql);

	_expect (sparql, RULE_TYPE_LITERAL, LITERAL_OPTIONAL);

	if (do_join) {
		_prepend_string (sparql, "SELECT * FROM (");
		_append_string (sparql, ") NATURAL LEFT JOIN (");
	}

	_call_rule (sparql, NAMED_RULE_GroupGraphPattern, error);

	if (do_join)
		_append_string (sparql, ") ");

	return TRUE;
}

static gboolean
translate_GraphGraphPattern (TrackerSparql  *sparql,
                             GError        **error)
{
	TrackerToken old_graph;
	gboolean do_join;

	/* GraphGraphPattern ::= 'GRAPH' VarOrIri GroupGraphPattern
	 */

	do_join = !tracker_string_builder_is_empty (sparql->current_state.sql);

	if (do_join) {
		_prepend_string (sparql, "SELECT * FROM (");
		_append_string (sparql, ") NATURAL INNER JOIN (");
	}

	old_graph = sparql->current_state.graph;

	_expect (sparql, RULE_TYPE_LITERAL, LITERAL_GRAPH);
	_call_rule (sparql, NAMED_RULE_VarOrIri, error);
	_init_token (&sparql->current_state.graph,
	             sparql->current_state.prev_node, sparql);
	_call_rule (sparql, NAMED_RULE_GroupGraphPattern, error);

	tracker_token_unset (&sparql->current_state.graph);
	sparql->current_state.graph = old_graph;

	if (do_join)
		_append_string (sparql, ") ");

	return TRUE;
}

static GList *
extract_variables (TrackerSparql     *sparql,
                   TrackerParserNode *pattern)
{
	TrackerParserNode *node;
	GList *variables = NULL;

	for (node = tracker_sparql_parser_tree_find_first (pattern, TRUE);
	     node;
	     node = tracker_sparql_parser_tree_find_next (node, TRUE)) {
		const TrackerGrammarRule *rule;

		if (!g_node_is_ancestor ((GNode *) pattern, (GNode *) node))
			break;

		rule = tracker_parser_node_get_rule (node);

		if (!tracker_grammar_rule_is_a (rule, RULE_TYPE_TERMINAL,
		                                TERMINAL_TYPE_VAR1) &&
		    !tracker_grammar_rule_is_a (rule, RULE_TYPE_TERMINAL,
						TERMINAL_TYPE_VAR2) &&
		    !tracker_grammar_rule_is_a (rule, RULE_TYPE_TERMINAL,
		                                TERMINAL_TYPE_PARAMETERIZED_VAR))
			continue;

		variables = g_list_prepend (variables, node);
	}

	return variables;
}

static gboolean
translate_ServiceGraphPattern (TrackerSparql  *sparql,
                               GError        **error)
{
	gssize pattern_start, pattern_end;
	TrackerParserNode *pattern;
	gchar *pattern_str, *escaped_str, *var_str;
	TrackerContext *context;
	GList *variables = NULL;
	GList *variable_rules = NULL, *l;
	GList *join_vars = NULL;
	TrackerToken service;
	GString *service_sparql;
	gboolean silent = FALSE, do_join;
	gint i = 0;

	/* ServiceGraphPattern ::= 'SERVICE' 'SILENT'? VarOrIri GroupGraphPattern
	 */
	do_join = !tracker_string_builder_is_empty (sparql->current_state.sql);

	if (do_join) {
		_prepend_string (sparql, "SELECT * FROM (");
		_append_string (sparql, ") AS Left INNER JOIN (");
	}

	context = tracker_triple_context_new ();
	tracker_sparql_push_context (sparql, context);

	_expect (sparql, RULE_TYPE_LITERAL, LITERAL_SERVICE);

	if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_SILENT))
		silent = TRUE;

	_call_rule (sparql, NAMED_RULE_VarOrIri, error);
	_init_token (&service, sparql->current_state.prev_node, sparql);

	if (sparql->policy.services &&
	    tracker_token_get_literal (&service)) {
		gboolean found = FALSE;
		gint i;

		for (i = 0; i < sparql->policy.services->len; i++) {
			if (g_strcmp0 (g_ptr_array_index (sparql->policy.services, i),
			               tracker_token_get_idstring (&service)) == 0) {
				found = TRUE;
				break;
			}
		}

		if (!found) {
			_raise (PARSE, "Access to service is disallowed", "SERVICE");
		}
	}

	pattern = _skip_rule (sparql, NAMED_RULE_GroupGraphPattern);
	_append_string (sparql, "SELECT ");
	service_sparql = g_string_new ("SELECT ");

	variable_rules = extract_variables (sparql, pattern);

	for (l = variable_rules; l; l = l->next) {
		TrackerParserNode *node = l->data;
		const TrackerGrammarRule *rule;
		TrackerBinding *binding;
		TrackerVariable *var;

		rule = tracker_parser_node_get_rule (node);

		if (tracker_grammar_rule_is_a (rule, RULE_TYPE_TERMINAL,
		                               TERMINAL_TYPE_PARAMETERIZED_VAR))
			continue;

		var_str = _extract_node_string (node, sparql);
		var = _extract_node_variable (node, sparql);

		if (g_list_find (variables, var))
			continue;

		if (i > 0)
			_append_string (sparql, ", ");

		/* Variable was used before in the graph pattern, preserve
		* for later so we join on it properly.
		*/
		if (do_join && tracker_variable_get_sample_binding (var))
			join_vars = g_list_prepend (join_vars, var);

		variables = g_list_prepend (variables, var);
		binding = tracker_variable_binding_new (var, NULL, NULL);
		tracker_binding_set_data_type (binding, TRACKER_PROPERTY_TYPE_STRING);
		_add_binding (sparql, binding);

		_append_string_printf (sparql, "col%d AS %s ",
				       i, tracker_variable_get_sql_expression (var));
		g_string_append_printf (service_sparql, "?%s ", var_str);
		g_free (var_str);
		i++;
	}

	tracker_parser_node_get_extents (pattern, &pattern_start, &pattern_end);
	pattern_str = g_strndup (&sparql->sparql[pattern_start], pattern_end - pattern_start);
	escaped_str = _escape_sql_string (pattern_str, '"');
	g_string_append (service_sparql, escaped_str);
	g_list_free (variables);
	g_free (pattern_str);
	g_free (escaped_str);

	_append_string_printf (sparql, "FROM tracker_service WHERE service=\"%s\" AND query=\"%s\" AND silent=%d ",
			       tracker_token_get_idstring (&service),
			       service_sparql->str,
			       silent);

	i = 0;

	/* Proxy parameters to the virtual table */
	for (l = variable_rules; l; l = l->next) {
		TrackerParserNode *node = l->data;
		const TrackerGrammarRule *rule;
		TrackerBinding *binding;
		gchar *name;

		rule = tracker_parser_node_get_rule (node);

		if (!tracker_grammar_rule_is_a (rule, RULE_TYPE_TERMINAL,
		                                TERMINAL_TYPE_PARAMETERIZED_VAR))
			continue;

		name = _extract_node_string (node, sparql);
		binding = tracker_parameter_binding_new (name, NULL);
		_add_binding (sparql, binding);

		_append_string_printf (sparql,
		                       "AND valuename%d = \"%s\" AND value%d = ",
		                       i, name, i);
		_append_literal_sql (sparql, TRACKER_LITERAL_BINDING (binding));
		g_free (name);
		i++;
	}

	tracker_token_unset (&service);
	tracker_sparql_pop_context (sparql, TRUE);
	g_string_free (service_sparql, TRUE);
	g_list_free (variable_rules);

	if (do_join) {
		_append_string (sparql, ") AS Right ");

		for (l = join_vars; l; l = l->next) {
			TrackerBinding *sample;

			if (l == join_vars)
				_append_string (sparql, "ON ");
			else
				_append_string (sparql, "AND ");

			sample = TRACKER_BINDING (tracker_variable_get_sample_binding (l->data));

			if (sample && sample->data_type == TRACKER_PROPERTY_TYPE_RESOURCE) {
				_append_string_printf (sparql, "(SELECT Uri FROM Resource WHERE ID = Left.%s) ",
						       tracker_variable_get_sql_expression (l->data));
			} else {
				_append_string_printf (sparql, "Left.%s ",
						       tracker_variable_get_sql_expression (l->data));
			}

			_append_string_printf (sparql, "= Right.%s ",
					       tracker_variable_get_sql_expression (l->data));
		}
	}

	g_list_free (join_vars);

	return TRUE;
}

static gboolean
translate_Bind (TrackerSparql  *sparql,
                GError        **error)
{
	TrackerStringBuilder *str, *old;
	TrackerVariable *variable;
	TrackerBinding *binding;
	TrackerPropertyType type;
	gboolean is_empty;

	/* Bind ::= 'BIND' '(' Expression 'AS' Var ')'
	 */
	_expect (sparql, RULE_TYPE_LITERAL, LITERAL_BIND);
	_expect (sparql, RULE_TYPE_LITERAL, LITERAL_OPEN_PARENS);

	is_empty = tracker_string_builder_is_empty (sparql->current_state.sql);

	if (!is_empty) {
		str = _prepend_placeholder (sparql);
		old = tracker_sparql_swap_builder (sparql, str);
	}

	_append_string (sparql, "SELECT ");

	if (!is_empty)
		_append_string (sparql, "*, ");

	_call_rule (sparql, NAMED_RULE_Expression, error);
	type = sparql->current_state.expression_type;

	_expect (sparql, RULE_TYPE_LITERAL, LITERAL_AS);
	_call_rule (sparql, NAMED_RULE_Var, error);

	variable = _last_node_variable (sparql);

	if (tracker_variable_has_bindings (variable))
		_raise (PARSE, "Expected undefined variable", "BIND");

	_append_string_printf (sparql, "AS %s ",
			       tracker_variable_get_sql_expression (variable));

	binding = tracker_variable_binding_new (variable, NULL, NULL);
	tracker_binding_set_data_type (binding, type);
	tracker_variable_set_sample_binding (variable, TRACKER_VARIABLE_BINDING (binding));

	if (!is_empty) {
		_append_string (sparql, "FROM (");
		tracker_sparql_swap_builder (sparql, old);
		_append_string (sparql, ") ");
	}

	_expect (sparql, RULE_TYPE_LITERAL, LITERAL_CLOSE_PARENS);

	return TRUE;
}

static gboolean
translate_InlineData (TrackerSparql  *sparql,
                      GError        **error)
{
	gboolean do_join;

	/* InlineData ::= 'VALUES' DataBlock
	 */
	do_join = !tracker_string_builder_is_empty (sparql->current_state.sql);

	if (do_join) {
		_prepend_string (sparql, "SELECT * FROM (");
		_append_string (sparql, ") NATURAL INNER JOIN (");
	}

	_expect (sparql, RULE_TYPE_LITERAL, LITERAL_VALUES);
	_call_rule (sparql, NAMED_RULE_DataBlock, error);

	if (do_join)
		_append_string (sparql, ")");

	return TRUE;
}

static gboolean
translate_DataBlock (TrackerSparql  *sparql,
                     GError        **error)
{
	TrackerGrammarNamedRule rule;
	TrackerStringBuilder *old;

	/* DataBlock ::= InlineDataOneVar | InlineDataFull
	 */
	old = tracker_sparql_swap_builder (sparql, sparql->current_state.with_clauses);

	if (tracker_string_builder_is_empty (sparql->current_state.with_clauses))
		_append_string (sparql, "WITH ");
	else
		_append_string (sparql, ", ");

	sparql->current_state.values_idx++;
	_append_string_printf (sparql, "\"dataBlock%d\"",
	                       sparql->current_state.values_idx);
	rule = _current_rule (sparql);

	switch (rule) {
	case NAMED_RULE_InlineDataOneVar:
	case NAMED_RULE_InlineDataFull:
		_call_rule (sparql, rule, error);
		break;
	default:
		g_assert_not_reached ();
	}

	tracker_sparql_swap_builder (sparql, old);

	_append_string_printf (sparql, "SELECT * FROM \"dataBlock%d\"",
	                       sparql->current_state.values_idx);

	return TRUE;
}

static gboolean
translate_InlineDataOneVar (TrackerSparql  *sparql,
                            GError        **error)
{
	TrackerVariable *var;
	TrackerBinding *binding;
	gint n_values = 0;

	/* InlineDataOneVar ::= Var '{' DataBlockValue* '}'
	 */
	_call_rule (sparql, NAMED_RULE_Var, error);

	var = _last_node_variable (sparql);
	binding = tracker_variable_binding_new (var, NULL, NULL);
	tracker_variable_set_sample_binding (var, TRACKER_VARIABLE_BINDING (binding));

	_append_string (sparql, "(");
	_append_variable_sql (sparql, var);
	_append_string (sparql, ") AS ( ");

	_expect (sparql, RULE_TYPE_LITERAL, LITERAL_OPEN_BRACE);

	while (_check_in_rule (sparql, NAMED_RULE_DataBlockValue)) {
		if (n_values == 0)
			_append_string (sparql, "VALUES ");
		else
			_append_string (sparql, ", ");

		_append_string (sparql, "(");
		_call_rule (sparql, NAMED_RULE_DataBlockValue, error);
		_append_string (sparql, ") ");
		n_values++;
	}

	if (n_values == 0)
		_append_string (sparql, "SELECT NULL WHERE FALSE");
	else
		tracker_binding_set_data_type (binding, sparql->current_state.expression_type);

	_expect (sparql, RULE_TYPE_LITERAL, LITERAL_CLOSE_BRACE);
	_append_string (sparql, ") ");

	return TRUE;
}

static gboolean
translate_InlineDataFull (TrackerSparql  *sparql,
                          GError        **error)
{
	TrackerVariable *var;
	TrackerBinding *binding;
	gint n_params, n_args = 0, n_values = 0;

	/* InlineDataFull ::= ( NIL | '(' Var* ')' ) '{' ( '(' DataBlockValue* ')' | NIL )* '}'
	 */
	_append_string (sparql, "(");

	if (_accept (sparql, RULE_TYPE_TERMINAL, TERMINAL_TYPE_NIL)) {
		_append_string (sparql, "NONE");
		n_args = 0;
	} else if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_OPEN_PARENS)) {
		while (_check_in_rule (sparql, NAMED_RULE_Var)) {
			if (n_args != 0)
				_append_string (sparql, ", ");

			_call_rule (sparql, NAMED_RULE_Var, error);

			var = _last_node_variable (sparql);
			binding = tracker_variable_binding_new (var, NULL, NULL);
			tracker_variable_set_sample_binding (var, TRACKER_VARIABLE_BINDING (binding));
			n_args++;

			_append_variable_sql (sparql, var);
		}

		_expect (sparql, RULE_TYPE_LITERAL, LITERAL_CLOSE_PARENS);
	} else {
		g_assert_not_reached ();
	}

	_append_string (sparql, ") AS ( ");
	_expect (sparql, RULE_TYPE_LITERAL, LITERAL_OPEN_BRACE);

	do {
		gboolean is_nil = FALSE;

		if (_accept (sparql, RULE_TYPE_TERMINAL, TERMINAL_TYPE_NIL))
			is_nil = TRUE;
		else if (!_accept (sparql, RULE_TYPE_LITERAL, LITERAL_OPEN_PARENS))
			break;

		if (n_values == 0)
			_append_string (sparql, "VALUES ");
		else
			_append_string (sparql, ", ");

		if (is_nil) {
			_append_string (sparql, "(NULL)");
			n_params = 0;
		} else {
			n_params = 0;

			_append_string (sparql, "(");

			while (_check_in_rule (sparql, NAMED_RULE_DataBlockValue)) {
				if (n_params != 0)
					_append_string (sparql, ", ");

				_call_rule (sparql, NAMED_RULE_DataBlockValue, error);
				n_params++;
			}

			_expect (sparql, RULE_TYPE_LITERAL, LITERAL_CLOSE_PARENS);
			_append_string (sparql, ") ");
		}

		if (n_params != n_args) {
			g_set_error (error, TRACKER_SPARQL_ERROR,
			             TRACKER_SPARQL_ERROR_PARSE,
			             "VALUES defined %d arguments but set has %d parameters",
			             n_args, n_params);
			return FALSE;
		}

		n_values++;
	} while (TRUE);

	if (n_values == 0)
		_append_string (sparql, "SELECT NULL WHERE FALSE");

	_append_string (sparql, ") ");

	return TRUE;
}

static gboolean
translate_DataBlockValue (TrackerSparql  *sparql,
                          GError        **error)
{
	TrackerSelectContext *select_context;
	TrackerGrammarNamedRule rule;
	TrackerBinding *binding;

	/* DataBlockValue ::= iri | RDFLiteral | NumericLiteral | BooleanLiteral | 'UNDEF'
	 */
	if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_UNDEF)) {
		_append_string (sparql, "NULL ");
		return TRUE;
	}

	select_context = TRACKER_SELECT_CONTEXT (sparql->current_state.select_context);
	rule = _current_rule (sparql);

	switch (rule) {
	case NAMED_RULE_RDFLiteral:
		_call_rule (sparql, rule, error);
		binding = g_ptr_array_index (select_context->literal_bindings,
		                             select_context->literal_bindings->len - 1);
		_append_literal_sql (sparql, TRACKER_LITERAL_BINDING (binding));
		break;
	case NAMED_RULE_iri:
	case NAMED_RULE_NumericLiteral:
	case NAMED_RULE_BooleanLiteral:
		_call_rule (sparql, rule, error);
		binding = _convert_terminal (sparql);
		tracker_select_context_add_literal_binding (select_context,
		                                            TRACKER_LITERAL_BINDING (binding));
		_append_literal_sql (sparql, TRACKER_LITERAL_BINDING (binding));
		g_object_unref (binding);
		break;
	default:
		g_assert_not_reached ();
	}

	return TRUE;
}

static void
append_subquery_select_vars (TrackerSparql  *sparql,
                             TrackerContext *context,
                             GList          *vars)
{
	GList *l;

	_append_string (sparql, "SELECT ");

	if (vars == NULL)
		_append_string (sparql, "* ");

	for (l = vars; l; l = l->next) {
		TrackerVariable *variable = l->data;

		if (l != vars)
			_append_string (sparql, ", ");

		if (!tracker_context_lookup_variable_ref (context, variable))
			_append_string (sparql, "NULL AS ");

		_append_string_printf (sparql, "%s ",
				       tracker_variable_get_sql_expression (variable));
	}

	_append_string (sparql, "FROM (");
}

static GList *
intersect_var_set (GHashTable *ht1,
		   GHashTable *ht2)
{
	GHashTableIter iter;
	GList *intersection = NULL;
	gpointer key;

	g_hash_table_iter_init (&iter, ht1);

	while (g_hash_table_iter_next (&iter, &key, NULL)) {
		if (g_hash_table_contains (ht2, key))
			intersection = g_list_prepend (intersection, key);
	}

	return intersection;
}


static gboolean
translate_MinusGraphPattern (TrackerSparql  *sparql,
                             GError        **error)
{
	TrackerStringBuilder *pre, *post, *cur;
	TrackerContext *cur_context, *context;
	GList *intersection, *l, *vars;

	cur_context = sparql->current_state.context;

	/* MinusGraphPattern ::= 'MINUS' GroupGraphPattern
	 */
	_expect (sparql, RULE_TYPE_LITERAL, LITERAL_MINUS);

	pre = _prepend_placeholder (sparql);
	post = _append_placeholder (sparql);

	context = tracker_context_new ();
	tracker_sparql_push_context (sparql, context);
	_call_rule (sparql, NAMED_RULE_GroupGraphPattern, error);
	tracker_sparql_pop_context (sparql, FALSE);

	intersection = intersect_var_set (cur_context->variable_set, context->variable_set);

	vars = g_hash_table_get_keys (cur_context->variable_set);
	cur = tracker_sparql_swap_builder (sparql, pre);
	append_subquery_select_vars (sparql, cur_context, vars);
	tracker_sparql_swap_builder (sparql, cur);

	if (intersection) {
		cur = tracker_sparql_swap_builder (sparql, post);
		_append_string (sparql, ") WHERE (");
		for (l = intersection; l; l = l->next) {
			if (l != intersection)
				_append_string (sparql, ", ");
			_append_string_printf (sparql, "%s ",
					       tracker_variable_get_sql_expression (l->data));
		}

		_append_string (sparql, ") NOT IN (");
		append_subquery_select_vars (sparql, context, intersection);

		tracker_sparql_swap_builder (sparql, cur);
		_append_string (sparql, ")) ");
		g_list_free (intersection);
	} else {
		cur = tracker_sparql_swap_builder (sparql, post);
		_append_string (sparql, ") EXCEPT ");
		append_subquery_select_vars (sparql, context, vars);

		tracker_sparql_swap_builder (sparql, cur);
		_append_string (sparql, ") ");
	}

	g_list_free (vars);

	return TRUE;
}

static gboolean
translate_GroupOrUnionGraphPattern (TrackerSparql  *sparql,
                                    GError        **error)
{
	TrackerContext *context;
	GPtrArray *placeholders;
	GList *vars, *c;
	gint idx = 0;
	gboolean do_join;

	/* GroupOrUnionGraphPattern ::= GroupGraphPattern ( 'UNION' GroupGraphPattern )*
	 */
	do_join = !tracker_string_builder_is_empty (sparql->current_state.sql);

	if (do_join) {
		_prepend_string (sparql, "SELECT * FROM (");
		_append_string (sparql, ") NATURAL INNER JOIN (");
	}

	placeholders = g_ptr_array_new ();
	context = tracker_context_new ();
	tracker_sparql_push_context (sparql, context);

	do {
		g_ptr_array_add (placeholders, _append_placeholder (sparql));

		if (!_call_rule_func (sparql, NAMED_RULE_GroupGraphPattern, error)) {
			g_ptr_array_unref (placeholders);
			return FALSE;
		}
	} while (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_UNION));

	vars = g_hash_table_get_keys (context->variable_set);

	if (placeholders->len > 1) {
		/* We are performing an union of multiple GroupGraphPattern,
		 * we must fix up the junction between all nested selects so we
		 * do UNION ALL on a common set of variables in a fixed
		 * order.
		 *
		 * If a variable is unused in the current subcontext
		 * it is defined as NULL.
		 */
		for (c = context->children; c; c = c->next) {
			TrackerStringBuilder *str, *old;

			g_assert (idx < placeholders->len);
			str = g_ptr_array_index (placeholders, idx);
			old = tracker_sparql_swap_builder (sparql, str);

			if (c != context->children)
				_append_string (sparql, ") UNION ALL ");

			append_subquery_select_vars (sparql, c->data, vars);
			tracker_sparql_swap_builder (sparql, old);
			idx++;
		}

		_append_string (sparql, ") ");
	}

	tracker_sparql_pop_context (sparql, TRUE);
	g_ptr_array_unref (placeholders);
	g_list_free (vars);

	if (do_join)
		_append_string (sparql, ") ");

	return TRUE;
}

static gboolean
translate_Filter (TrackerSparql  *sparql,
                  GError        **error)
{
	TrackerParserNode *node;

	/* Filter ::= 'FILTER' Constraint
	 */
	_expect (sparql, RULE_TYPE_LITERAL, LITERAL_FILTER);
	node = _skip_rule (sparql, NAMED_RULE_Constraint);
	/* Add constraints to list for later processing */
	sparql->filter_clauses = g_list_prepend (sparql->filter_clauses, node);

	return TRUE;
}

static gboolean
translate_Constraint (TrackerSparql  *sparql,
                      GError        **error)
{
	TrackerGrammarNamedRule rule;

	/* Constraint ::= BrackettedExpression | BuiltInCall | FunctionCall
	 */
	rule = _current_rule (sparql);

	switch (rule) {
	case NAMED_RULE_BrackettedExpression:
	case NAMED_RULE_BuiltInCall:
	case NAMED_RULE_FunctionCall:
		_call_rule (sparql, rule, error);
		break;
	default:
		g_assert_not_reached ();
	}

	return TRUE;
}

static gboolean
translate_FunctionCall (TrackerSparql  *sparql,
                        GError        **error)
{
	/* FunctionCall ::= iri ArgList
	 */
	_call_rule (sparql, NAMED_RULE_iri, error);
	return handle_function_call (sparql, error);
}

static gboolean
translate_ArgList (TrackerSparql  *sparql,
                   GError        **error)
{
	/* ArgList ::= NIL | '(' 'DISTINCT'? Expression ( ',' Expression )* ')'
	 */
	if (_accept (sparql, RULE_TYPE_TERMINAL, TERMINAL_TYPE_NIL)) {
	} else if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_OPEN_PARENS)) {
		if (_check_in_rule (sparql, NAMED_RULE_ArgList))
			_raise (PARSE, "Recursive ArgList is not allowed", "ArgList");

		if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_DISTINCT)) {
			/* This path is only for custom aggregate function, as per
			 * the SPARQL recommendation, note 15 in grammar section:
			 * "Only custom aggregate functions use the DISTINCT keyword in a function call."
			 *
			 * But we have none, so it's fine to bail out here.
			 */
			_raise (PARSE, "DISTINCT is not allowed in non-aggregate function", "ArgList");
		}

		_call_rule (sparql, NAMED_RULE_Expression, error);

		while (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_COMMA)) {
			const gchar *separator = ", ";

			if (sparql->current_state.expression_list_separator)
				separator = sparql->current_state.expression_list_separator;

			_append_string (sparql, separator);
			_call_rule (sparql, NAMED_RULE_Expression, error);
		}

		_expect (sparql, RULE_TYPE_LITERAL, LITERAL_CLOSE_PARENS);
	} else {
		g_assert_not_reached ();
	}

	return TRUE;
}

static gboolean
translate_ExpressionList (TrackerSparql  *sparql,
                          GError        **error)
{
	/* ExpressionList ::= NIL | '(' Expression ( ',' Expression )* ')'
	 */
	if (_accept (sparql, RULE_TYPE_TERMINAL, TERMINAL_TYPE_NIL)) {
		_append_string (sparql, "() ");
	} else if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_OPEN_PARENS)) {
		_append_string (sparql, "(");
		_call_rule (sparql, NAMED_RULE_Expression, error);

		while (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_COMMA)) {
			_append_string (sparql,
					sparql->current_state.expression_list_separator);
			_call_rule (sparql, NAMED_RULE_Expression, error);
		}

		_expect (sparql, RULE_TYPE_LITERAL, LITERAL_CLOSE_PARENS);
		_append_string (sparql, ") ");
	} else {
		g_assert_not_reached ();
	}

	return TRUE;
}

static gboolean
translate_ConstructTemplate (TrackerSparql  *sparql,
                             GError        **error)
{
	/* ConstructTemplate ::= '{' ConstructTriples? '}'
	 */
	_expect (sparql, RULE_TYPE_LITERAL, LITERAL_OPEN_BRACE);

	if (_check_in_rule (sparql, NAMED_RULE_ConstructTriples)) {
		_call_rule (sparql, NAMED_RULE_ConstructTriples, error);
	}

	_expect (sparql, RULE_TYPE_LITERAL, LITERAL_CLOSE_BRACE);

	return TRUE;
}

static gboolean
translate_ConstructTriples (TrackerSparql  *sparql,
                            GError        **error)
{
	/* ConstructTriples ::= TriplesSameSubject ( '.' ConstructTriples? )?
	 */
	_call_rule (sparql, NAMED_RULE_TriplesSameSubject, error);

	if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_DOT)) {
		if (_check_in_rule (sparql, NAMED_RULE_ConstructTriples)) {
			_call_rule (sparql, NAMED_RULE_ConstructTriples, error);
		}
	}

	return TRUE;
}

static gboolean
translate_TriplesSameSubject (TrackerSparql  *sparql,
                              GError        **error)
{
	TrackerToken old_subject = sparql->current_state.subject;
	TrackerGrammarNamedRule rule;

	/* TriplesSameSubject ::= VarOrTerm PropertyListNotEmpty | TriplesNode PropertyList
	 */
	rule = _current_rule (sparql);
	sparql->current_state.token = &sparql->current_state.subject;

	if (rule == NAMED_RULE_VarOrTerm) {
		_call_rule (sparql, rule, error);
		sparql->current_state.token = &sparql->current_state.object;
		_call_rule (sparql, NAMED_RULE_PropertyListNotEmpty, error);
	} else if (rule == NAMED_RULE_TriplesNode) {
		_call_rule (sparql, rule, error);
		sparql->current_state.token = &sparql->current_state.object;
		_call_rule (sparql, NAMED_RULE_PropertyList, error);
	}

	tracker_token_unset (&sparql->current_state.subject);
	sparql->current_state.subject = old_subject;
	sparql->current_state.token = NULL;

	return TRUE;
}

static gboolean
translate_GroupGraphPattern (TrackerSparql  *sparql,
                             GError        **error)
{
	TrackerGrammarNamedRule rule;
	TrackerContext *context;

	/* GroupGraphPattern ::= '{' ( SubSelect | GroupGraphPatternSub ) '}'
	 */
	_expect (sparql, RULE_TYPE_LITERAL, LITERAL_OPEN_BRACE);
	context = tracker_context_new ();
	tracker_sparql_push_context (sparql, context);

	rule = _current_rule (sparql);

	if (rule == NAMED_RULE_SubSelect) {
		_append_string (sparql, "(");
		_call_rule (sparql, rule, error);
		_append_string (sparql, ") ");
	} else if (rule == NAMED_RULE_GroupGraphPatternSub) {
		_call_rule (sparql, rule, error);
	}

	tracker_sparql_pop_context (sparql, TRUE);
	_expect (sparql, RULE_TYPE_LITERAL, LITERAL_CLOSE_BRACE);

	return TRUE;
}

static gboolean
translate_PropertyList (TrackerSparql  *sparql,
                        GError        **error)
{
	/* PropertyList ::= PropertyListNotEmpty?
	 */
	if (_check_in_rule (sparql, NAMED_RULE_PropertyListNotEmpty)) {
		_call_rule (sparql, NAMED_RULE_PropertyListNotEmpty, error);
	}

	return TRUE;
}

static gboolean
translate_PropertyListNotEmpty (TrackerSparql  *sparql,
                                GError        **error)
{
	TrackerToken old_pred, *prev_token;

	old_pred = sparql->current_state.predicate;
	prev_token = sparql->current_state.token;
	sparql->current_state.token = &sparql->current_state.object;

	/* PropertyListNotEmpty ::= Verb ObjectList ( ';' ( Verb ObjectList )? )*
	 */
	_call_rule (sparql, NAMED_RULE_Verb, error);
	_init_token (&sparql->current_state.predicate,
	             sparql->current_state.prev_node, sparql);

	_call_rule (sparql, NAMED_RULE_ObjectList, error);
	tracker_token_unset (&sparql->current_state.predicate);

	while (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_SEMICOLON)) {
		if (!_check_in_rule (sparql, NAMED_RULE_Verb))
			break;

		_call_rule (sparql, NAMED_RULE_Verb, error);
		_init_token (&sparql->current_state.predicate,
		             sparql->current_state.prev_node, sparql);

		_call_rule (sparql, NAMED_RULE_ObjectList, error);

		tracker_token_unset (&sparql->current_state.predicate);
	}

	sparql->current_state.predicate = old_pred;
	sparql->current_state.token = prev_token;

	return TRUE;
}

static gboolean
translate_Verb (TrackerSparql  *sparql,
                GError        **error)
{
	/* Verb ::= VarOrIri | 'a'
	 */
	if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_A)) {
	} else {
		_call_rule (sparql, NAMED_RULE_VarOrIri, error);
	}

	return TRUE;
}

static gboolean
translate_ObjectList (TrackerSparql  *sparql,
                      GError        **error)
{
	/* ObjectList ::= Object ( ',' Object )*
	 */
	_call_rule (sparql, NAMED_RULE_Object, error);

	while (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_COMMA)) {
		_call_rule (sparql, NAMED_RULE_Object, error);
	}

	return TRUE;
}

static gboolean
translate_Object (TrackerSparql  *sparql,
                  GError        **error)
{
	/* Object ::= GraphNode
	 */
	_call_rule (sparql, NAMED_RULE_GraphNode, error);
	return TRUE;
}

static gboolean
translate_TriplesSameSubjectPath (TrackerSparql  *sparql,
                                  GError        **error)
{
	TrackerToken old_subject = sparql->current_state.subject;
	TrackerGrammarNamedRule rule;

	/* TriplesSameSubjectPath ::= VarOrTerm PropertyListPathNotEmpty | TriplesNodePath PropertyListPath
	 */
	rule = _current_rule (sparql);
	sparql->current_state.token = &sparql->current_state.subject;

	if (rule == NAMED_RULE_VarOrTerm) {
		_call_rule (sparql, rule, error);
		g_assert (!tracker_token_is_empty (&sparql->current_state.subject));
		sparql->current_state.token = &sparql->current_state.object;
		_call_rule (sparql, NAMED_RULE_PropertyListPathNotEmpty, error);
	} else if (rule == NAMED_RULE_TriplesNodePath) {
		_call_rule (sparql, rule, error);
		g_assert (!tracker_token_is_empty (&sparql->current_state.subject));
		sparql->current_state.token = &sparql->current_state.object;
		_call_rule (sparql, NAMED_RULE_PropertyListPath, error);
	}

	tracker_token_unset (&sparql->current_state.subject);
	sparql->current_state.subject = old_subject;
	sparql->current_state.token = NULL;

	return TRUE;
}

static gboolean
translate_PropertyListPath (TrackerSparql  *sparql,
                            GError        **error)
{
	/* PropertyListPath ::= PropertyListPathNotEmpty?
	 */
	if (_check_in_rule (sparql, NAMED_RULE_PropertyListPathNotEmpty)) {
		_call_rule (sparql, NAMED_RULE_PropertyListPathNotEmpty, error);
	}

	return TRUE;
}

static gboolean
translate_PropertyListPathNotEmpty (TrackerSparql  *sparql,
                                    GError        **error)
{
	TrackerGrammarNamedRule rule;
	TrackerToken old_predicate, *prev_token;

	/* PropertyListPathNotEmpty ::= ( VerbPath | VerbSimple ) ObjectListPath ( ';' ( ( VerbPath | VerbSimple ) ObjectList )? )*
	 */
	rule = _current_rule (sparql);
	old_predicate = sparql->current_state.predicate;
	prev_token = sparql->current_state.token;
	sparql->current_state.token = &sparql->current_state.object;

	if (rule == NAMED_RULE_VerbPath || rule == NAMED_RULE_VerbSimple) {
		_call_rule (sparql, rule, error);
	} else {
		g_assert_not_reached ();
	}

	_call_rule (sparql, NAMED_RULE_ObjectListPath, error);
	tracker_token_unset (&sparql->current_state.predicate);

	while (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_SEMICOLON)) {
		rule = _current_rule (sparql);

		if (rule == NAMED_RULE_VerbPath || rule == NAMED_RULE_VerbSimple) {
			_call_rule (sparql, rule, error);
		} else {
			break;
		}

		_call_rule (sparql, NAMED_RULE_ObjectList, error);
		tracker_token_unset (&sparql->current_state.predicate);
	}

	sparql->current_state.predicate = old_predicate;
	sparql->current_state.token = prev_token;

	return TRUE;
}

static gboolean
translate_VerbPath (TrackerSparql  *sparql,
                    GError        **error)
{
	/* VerbPath ::= Path
	 */

	/* If this path consists of a single element, do not set
	 * up a property path. Just set the property token to
	 * be the only property literal and let _add_quad()
	 * apply its optimizations.
	 */
	if (g_node_n_nodes ((GNode *) sparql->current_state.node,
	                    G_TRAVERSE_LEAVES) == 1) {
		TrackerParserNode *prop;
		gchar *str;

		prop = tracker_sparql_parser_tree_find_first (sparql->current_state.node, TRUE);
		str = _extract_node_string (prop, sparql);
		tracker_token_literal_init (&sparql->current_state.predicate, str, -1);
		g_free (str);

		_skip_rule (sparql, NAMED_RULE_Path);
	} else {
		_call_rule (sparql, NAMED_RULE_Path, error);
		sparql->current_state.path = NULL;
	}

	return TRUE;
}

static gboolean
translate_VerbSimple (TrackerSparql  *sparql,
                      GError        **error)
{
	/* VerbSimple ::= Var
	 */
	_call_rule (sparql, NAMED_RULE_Var, error);
	_init_token (&sparql->current_state.predicate,
	             sparql->current_state.prev_node, sparql);
	return TRUE;
}

static gboolean
translate_ObjectListPath (TrackerSparql  *sparql,
                          GError        **error)
{
	/* ObjectListPath ::= ObjectPath ( ',' ObjectPath )*
	 */
	_call_rule (sparql, NAMED_RULE_ObjectPath, error);

	while (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_COMMA)) {
		_call_rule (sparql, NAMED_RULE_ObjectPath, error);
	}

	return TRUE;
}

static gboolean
translate_ObjectPath (TrackerSparql  *sparql,
                      GError        **error)
{
	/* ObjectPath ::= GraphNodePath
	 */
	_call_rule (sparql, NAMED_RULE_GraphNodePath, error);

	return TRUE;
}

static gboolean
translate_Path (TrackerSparql  *sparql,
                GError        **error)
{
	/* Path ::= PathAlternative
	 */
	_call_rule (sparql, NAMED_RULE_PathAlternative, error);
	tracker_token_path_init (&sparql->current_state.predicate,
	                         sparql->current_state.path);
	return TRUE;
}

static gboolean
translate_PathAlternative (TrackerSparql  *sparql,
                           GError        **error)
{
	GPtrArray *path_elems;

	path_elems = g_ptr_array_new ();

	/* PathAlternative ::= PathSequence ( '|' PathSequence )*
	 */
	_call_rule (sparql, NAMED_RULE_PathSequence, error);
	g_ptr_array_add (path_elems, sparql->current_state.path);

	while (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_PATH_ALTERNATIVE)) {
		_call_rule (sparql, NAMED_RULE_PathSequence, error);
		g_ptr_array_add (path_elems, sparql->current_state.path);
	}

	if (path_elems->len > 1) {
		TrackerPathElement *path_elem;
		gint i;

		path_elem = tracker_path_element_operator_new (TRACKER_PATH_OPERATOR_ALTERNATIVE,
		                                               g_ptr_array_index (path_elems, 0),
		                                               g_ptr_array_index (path_elems, 1));
		tracker_select_context_add_path_element (TRACKER_SELECT_CONTEXT (sparql->context),
		                                         path_elem);
		_prepend_path_element (sparql, path_elem);

		for (i = 2; i < path_elems->len; i++) {
			TrackerPathElement *child;

			child = g_ptr_array_index (path_elems, i);
			path_elem = tracker_path_element_operator_new (TRACKER_PATH_OPERATOR_ALTERNATIVE,
			                                               child, path_elem);
			tracker_select_context_add_path_element (TRACKER_SELECT_CONTEXT (sparql->context),
			                                         path_elem);
			_prepend_path_element (sparql, path_elem);
		}

		sparql->current_state.path = path_elem;
	}

	g_ptr_array_unref (path_elems);

	return TRUE;
}

static gboolean
translate_PathSequence (TrackerSparql  *sparql,
                        GError        **error)
{
	GPtrArray *path_elems;

	path_elems = g_ptr_array_new ();

	/* PathSequence ::= PathEltOrInverse ( '/' PathEltOrInverse )*
	 */
	_call_rule (sparql, NAMED_RULE_PathEltOrInverse, error);
	g_ptr_array_add (path_elems, sparql->current_state.path);

	while (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_PATH_SEQUENCE)) {
		_call_rule (sparql, NAMED_RULE_PathEltOrInverse, error);
		g_ptr_array_add (path_elems, sparql->current_state.path);
	}

	if (path_elems->len > 1) {
		TrackerPathElement *path_elem;
		gint i;

		/* We must handle path elements in inverse order, paired to
		 * the path element created in the previous step.
		 */
		path_elem = tracker_path_element_operator_new (TRACKER_PATH_OPERATOR_SEQUENCE,
		                                               g_ptr_array_index (path_elems, path_elems->len - 2),
		                                               g_ptr_array_index (path_elems, path_elems->len - 1));
		tracker_select_context_add_path_element (TRACKER_SELECT_CONTEXT (sparql->context),
		                                         path_elem);
		_prepend_path_element (sparql, path_elem);

		for (i = ((gint) path_elems->len) - 3; i >= 0; i--) {
			TrackerPathElement *child;

			child = g_ptr_array_index (path_elems, i);
			path_elem = tracker_path_element_operator_new (TRACKER_PATH_OPERATOR_SEQUENCE,
			                                               child, path_elem);
			tracker_select_context_add_path_element (TRACKER_SELECT_CONTEXT (sparql->context),
			                                         path_elem);
			_prepend_path_element (sparql, path_elem);
		}

		sparql->current_state.path = path_elem;
	}

	g_ptr_array_unref (path_elems);

	return TRUE;
}

static gboolean
translate_PathEltOrInverse (TrackerSparql  *sparql,
                            GError        **error)
{
	gboolean inverse = FALSE;

	/* PathEltOrInverse ::= PathElt | '^' PathElt
	 */
	if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_PATH_INVERSE))
		inverse = TRUE;

	_call_rule (sparql, NAMED_RULE_PathElt, error);

	if (inverse) {
		TrackerPathElement *path_elem;

		path_elem = tracker_path_element_operator_new (TRACKER_PATH_OPERATOR_INVERSE,
		                                               sparql->current_state.path,
		                                               NULL);
		tracker_select_context_add_path_element (TRACKER_SELECT_CONTEXT (sparql->context),
		                                         path_elem);
		_prepend_path_element (sparql, path_elem);
		sparql->current_state.path = path_elem;
	}

	return TRUE;
}

static gboolean
translate_PathElt (TrackerSparql  *sparql,
                   GError        **error)
{
	/* PathElt ::= PathPrimary PathMod?
	 */
	_call_rule (sparql, NAMED_RULE_PathPrimary, error);

	if (_check_in_rule (sparql, NAMED_RULE_PathMod)) {
		_call_rule (sparql, NAMED_RULE_PathMod, error);
	}

	return TRUE;
}

static gboolean
translate_PathMod (TrackerSparql  *sparql,
                   GError        **error)
{
	TrackerPathElement *path_elem;
	TrackerPathOperator op;

	/* PathMod ::= '?' | '*' | '+'
	 */
	if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_PATH_STAR)) {
		op = TRACKER_PATH_OPERATOR_ZEROORMORE;
	} else if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_PATH_PLUS)) {
		op = TRACKER_PATH_OPERATOR_ONEORMORE;
	} else if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_PATH_OPTIONAL)) {
		op = TRACKER_PATH_OPERATOR_ZEROORONE;
	} else {
		return TRUE;
	}

	path_elem = tracker_path_element_operator_new (op, sparql->current_state.path, NULL);
	tracker_select_context_add_path_element (TRACKER_SELECT_CONTEXT (sparql->context),
						 path_elem);
	_prepend_path_element (sparql, path_elem);
	sparql->current_state.path = path_elem;
	return TRUE;
}

static gboolean
translate_PathPrimary (TrackerSparql  *sparql,
                       GError        **error)
{
	/* PathPrimary ::= iri | 'a' | '!' PathNegatedPropertySet | '(' Path ')'
	 */
	if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_OP_NEG)) {
		_call_rule (sparql, NAMED_RULE_PathNegatedPropertySet, error);
	} else if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_OPEN_PARENS)) {
		_call_rule (sparql, NAMED_RULE_Path, error);

		_expect (sparql, RULE_TYPE_LITERAL, LITERAL_CLOSE_PARENS);
	} else if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_A) ||
	           _check_in_rule (sparql, NAMED_RULE_iri)) {
		TrackerOntologies *ontologies;
		TrackerProperty *prop;
		TrackerPathElement *path_elem;
		gchar *str;

		if (_check_in_rule (sparql, NAMED_RULE_iri))
			_call_rule (sparql, NAMED_RULE_iri, error);

		str = _dup_last_string (sparql);
		ontologies = tracker_data_manager_get_ontologies (sparql->data_manager);
		prop = tracker_ontologies_get_property_by_uri (ontologies, str);

		if (!prop) {
			g_set_error (error, TRACKER_SPARQL_ERROR,
			             TRACKER_SPARQL_ERROR_UNKNOWN_PROPERTY,
			             "Unknown property '%s'", str);
			g_free (str);
			return FALSE;
		}

		path_elem =
			tracker_select_context_lookup_path_element_for_property (TRACKER_SELECT_CONTEXT (sparql->context),
			                                                         prop);

		if (!path_elem) {
			path_elem = tracker_path_element_property_new (TRACKER_PATH_OPERATOR_NONE, prop);
			tracker_select_context_add_path_element (TRACKER_SELECT_CONTEXT (sparql->context),
			                                         path_elem);
			_prepend_path_element (sparql, path_elem);
		}

		sparql->current_state.path = path_elem;
		g_free (str);
	} else {
		g_assert_not_reached ();
	}

	return TRUE;
}

static gboolean
translate_PathNegatedPropertySet (TrackerSparql  *sparql,
                                  GError        **error)
{
	TrackerPathElement *path_elem;

	/* PathNegatedPropertySet ::= PathOneInPropertySet | '(' ( PathOneInPropertySet ( '|' PathOneInPropertySet )* )? ')'
	 */
	if (_check_in_rule (sparql, NAMED_RULE_PathOneInPropertySet))
		_call_rule (sparql, NAMED_RULE_PathOneInPropertySet, error);
	else if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_OPEN_PARENS)) {
		GPtrArray *path_elems;

		path_elems = g_ptr_array_new ();

		_call_rule (sparql, NAMED_RULE_PathEltOrInverse, error);
		g_ptr_array_add (path_elems, sparql->current_state.path);

		while (_check_in_rule (sparql, NAMED_RULE_PathOneInPropertySet)) {
			_call_rule (sparql, NAMED_RULE_PathOneInPropertySet, error);
			g_ptr_array_add (path_elems, sparql->current_state.path);
		}

		if (path_elems->len > 1) {
			gint i;

			path_elem = tracker_path_element_operator_new (TRACKER_PATH_OPERATOR_INTERSECTION,
			                                               g_ptr_array_index (path_elems, 0),
			                                               g_ptr_array_index (path_elems, 1));
			tracker_select_context_add_path_element (TRACKER_SELECT_CONTEXT (sparql->context),
			                                         path_elem);
			_prepend_path_element (sparql, path_elem);

			for (i = 2; i < path_elems->len; i++) {
				TrackerPathElement *child;

				child = g_ptr_array_index (path_elems, i);
				path_elem = tracker_path_element_operator_new (TRACKER_PATH_OPERATOR_INTERSECTION,
				                                               child, path_elem);
				tracker_select_context_add_path_element (TRACKER_SELECT_CONTEXT (sparql->context),
				                                         path_elem);
				_prepend_path_element (sparql, path_elem);
			}

			sparql->current_state.path = path_elem;
		}

		_expect (sparql, RULE_TYPE_LITERAL, LITERAL_CLOSE_PARENS);
	} else {
		g_assert_not_reached ();
	}

	return TRUE;
}

static gboolean
translate_PathOneInPropertySet (TrackerSparql  *sparql,
                                GError        **error)
{
	TrackerPathElement *path_elem;
	gboolean inverse = FALSE;

	/* PathOneInPropertySet ::= iri | 'a' | '^' ( iri | 'a' )
	 */
	if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_PATH_INVERSE))
		inverse = TRUE;

	if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_A) ||
	    _check_in_rule (sparql, NAMED_RULE_iri)) {
		TrackerOntologies *ontologies;
		TrackerProperty *prop;
		gchar *str;

		if (_check_in_rule (sparql, NAMED_RULE_iri))
			_call_rule (sparql, NAMED_RULE_iri, error);

		str = _dup_last_string (sparql);
		ontologies = tracker_data_manager_get_ontologies (sparql->data_manager);
		prop = tracker_ontologies_get_property_by_uri (ontologies, str);

		if (!prop) {
			g_set_error (error, TRACKER_SPARQL_ERROR,
			             TRACKER_SPARQL_ERROR_UNKNOWN_PROPERTY,
			             "Unknown property '%s'", str);
			g_free (str);
			return FALSE;
		}

		path_elem =
			tracker_select_context_lookup_path_element_for_property (TRACKER_SELECT_CONTEXT (sparql->context),
			                                                         prop);

		if (!path_elem) {
			path_elem = tracker_path_element_property_new (TRACKER_PATH_OPERATOR_NEGATED, prop);
			tracker_select_context_add_path_element (TRACKER_SELECT_CONTEXT (sparql->context),
			                                         path_elem);
			_prepend_path_element (sparql, path_elem);
		}

		sparql->current_state.path = path_elem;
		g_free (str);
	} else {
		g_assert_not_reached ();
	}

	if (inverse) {
		path_elem = tracker_path_element_operator_new (TRACKER_PATH_OPERATOR_INVERSE,
		                                               sparql->current_state.path,
		                                               NULL);
		tracker_select_context_add_path_element (TRACKER_SELECT_CONTEXT (sparql->context),
		                                         path_elem);
		_prepend_path_element (sparql, path_elem);
		sparql->current_state.path = path_elem;
	}

	return TRUE;
}

static gboolean
translate_Integer (TrackerSparql  *sparql,
                   GError        **error)
{
	/* Integer ::= INTEGER
	 */
	_expect (sparql, RULE_TYPE_TERMINAL, TERMINAL_TYPE_INTEGER);
	sparql->current_state.expression_type = TRACKER_PROPERTY_TYPE_INTEGER;

	return TRUE;
}

static gboolean
translate_TriplesNode (TrackerSparql  *sparql,
                       GError        **error)
{
	TrackerGrammarNamedRule rule;

	/* TriplesNode ::= Collection |	BlankNodePropertyList
	 */
	rule = _current_rule (sparql);

	switch (rule) {
	case NAMED_RULE_Collection:
	case NAMED_RULE_BlankNodePropertyList:
		_call_rule (sparql, rule, error);
		break;
	default:
		g_assert_not_reached ();
	}

	return TRUE;
}

static gboolean
translate_BlankNodePropertyList (TrackerSparql  *sparql,
                                 GError        **error)
{
	TrackerToken old_subject = sparql->current_state.subject;
	TrackerVariable *var;

	/* BlankNodePropertyList ::= '[' PropertyListNotEmpty ']'
	 */
	_expect (sparql, RULE_TYPE_LITERAL, LITERAL_OPEN_BRACKET);

        if (sparql->current_state.type == TRACKER_SPARQL_TYPE_SELECT) {
		var = tracker_select_context_add_generated_variable (TRACKER_SELECT_CONTEXT (sparql->context));
		tracker_token_variable_init (&sparql->current_state.subject, var);
	} else {
		TrackerDBInterface *iface;
		gchar *bnode_id;

		iface = tracker_data_manager_get_writable_db_interface (sparql->data_manager);
		bnode_id = tracker_data_query_unused_uuid (sparql->data_manager, iface);
		tracker_token_literal_init (&sparql->current_state.subject, bnode_id, -1);
		g_free (bnode_id);
	}

	_call_rule (sparql, NAMED_RULE_PropertyListNotEmpty, error);

	_expect (sparql, RULE_TYPE_LITERAL, LITERAL_CLOSE_BRACKET);

	/* Return the blank node subject through the token, if token is already
	 * the subject, doesn't need changing.
	 */
	g_assert (sparql->current_state.token != NULL);

	if (sparql->current_state.token != &sparql->current_state.subject) {
		*sparql->current_state.token = sparql->current_state.subject;
		sparql->current_state.subject = old_subject;
	}

	return TRUE;
}

static gboolean
translate_TriplesNodePath (TrackerSparql  *sparql,
                           GError        **error)
{
	TrackerGrammarNamedRule rule;

	/* TriplesNodePath ::= CollectionPath | BlankNodePropertyListPath
	 */
	rule = _current_rule (sparql);

	if (rule == NAMED_RULE_CollectionPath) {
		_call_rule (sparql, rule, error);
	} else if (rule == NAMED_RULE_BlankNodePropertyListPath) {
		_call_rule (sparql, rule, error);
	}

	return TRUE;
}

static gboolean
translate_BlankNodePropertyListPath (TrackerSparql  *sparql,
                                     GError        **error)
{
	TrackerToken old_subject = sparql->current_state.subject;
	TrackerToken *token_location = sparql->current_state.token;
	TrackerVariable *var;

	/* BlankNodePropertyListPath ::= '[' PropertyListPathNotEmpty ']'
	 */
	_expect (sparql, RULE_TYPE_LITERAL, LITERAL_OPEN_BRACKET);

	var = tracker_select_context_add_generated_variable (TRACKER_SELECT_CONTEXT (sparql->context));
	tracker_token_variable_init (&sparql->current_state.subject, var);
	_call_rule (sparql, NAMED_RULE_PropertyListPathNotEmpty, error);

	_expect (sparql, RULE_TYPE_LITERAL, LITERAL_CLOSE_BRACKET);

	tracker_token_unset (&sparql->current_state.subject);
	sparql->current_state.subject = old_subject;

	/* Return the blank node subject through the token */
	g_assert (sparql->current_state.token != NULL);
	tracker_token_unset (token_location);
	tracker_token_variable_init (token_location, var);

	return TRUE;
}

static gboolean
translate_Collection (TrackerSparql  *sparql,
                      GError        **error)
{
	TrackerToken old_subject, old_predicate, old_object, *old_token, *cur;
	GArray *elems;
	gint i;

	/* Collection ::= '(' GraphNode+ ')'
	 */
	_expect (sparql, RULE_TYPE_LITERAL, LITERAL_OPEN_PARENS);

	old_subject = sparql->current_state.subject;
	old_predicate = sparql->current_state.predicate;
	old_object = sparql->current_state.object;
	old_token = sparql->current_state.token;

	elems = g_array_new (FALSE, TRUE, sizeof (TrackerToken));

	while (_check_in_rule (sparql, NAMED_RULE_GraphNode)) {
		if (elems->len > 0) {
			cur = &g_array_index (elems, TrackerToken, elems->len - 1);
		} else {
			g_array_set_size (elems, elems->len + 1);
			cur = &g_array_index (elems, TrackerToken, 0);

			if (sparql->current_state.type == TRACKER_SPARQL_TYPE_SELECT) {
				TrackerVariable *var;
				var = tracker_select_context_add_generated_variable (TRACKER_SELECT_CONTEXT (sparql->context));
				tracker_token_variable_init (cur, var);
			} else {
				TrackerDBInterface *iface;
				gchar *bnode_id;

				iface = tracker_data_manager_get_writable_db_interface (sparql->data_manager);
				bnode_id = tracker_data_query_unused_uuid (sparql->data_manager, iface);
				tracker_token_literal_init (cur, bnode_id, -1);
				g_free (bnode_id);
			}
		}

		sparql->current_state.subject = *cur;

		/* Add "_:elem a rdf:List" first */
		tracker_token_literal_init (&sparql->current_state.predicate,
		                            RDF_NS "type", -1);
		tracker_token_literal_init (&sparql->current_state.object,
		                            RDF_NS "List", -1);

		if (!tracker_sparql_apply_quad (sparql, error)) {
			tracker_token_unset (&sparql->current_state.predicate);
			tracker_token_unset (&sparql->current_state.object);
			goto error;
		}

		/* rdf:first */
		tracker_token_literal_init (&sparql->current_state.predicate,
		                            RDF_NS "first", -1);
		sparql->current_state.token = &sparql->current_state.object;
		_call_rule (sparql, NAMED_RULE_GraphNode, error);
		sparql->current_state.token = NULL;
		tracker_token_unset (&sparql->current_state.predicate);

		/* rdf:rest */
		tracker_token_literal_init (&sparql->current_state.predicate,
		                            RDF_NS "rest", -1);

		if (_check_in_rule (sparql, NAMED_RULE_GraphNode)) {
			/* Generate variable for next element */
			g_array_set_size (elems, elems->len + 1);
			cur = &g_array_index (elems, TrackerToken, elems->len - 1);

			if (sparql->current_state.type == TRACKER_SPARQL_TYPE_SELECT) {
				TrackerVariable *var;
				var = tracker_select_context_add_generated_variable (TRACKER_SELECT_CONTEXT (sparql->context));
				tracker_token_variable_init (cur, var);
			} else {
				TrackerDBInterface *iface;
				gchar *bnode_id;

				iface = tracker_data_manager_get_writable_db_interface (sparql->data_manager);
				bnode_id = tracker_data_query_unused_uuid (sparql->data_manager, iface);
				tracker_token_literal_init (cur, bnode_id, -1);
				g_free (bnode_id);
			}

			sparql->current_state.object = *cur;

			if (!tracker_sparql_apply_quad (sparql, error)) {
				tracker_token_unset (&sparql->current_state.predicate);
				goto error;
			}
		} else {
			/* Make last element point to rdf:nil */
			tracker_token_literal_init (&sparql->current_state.object,
			                            RDF_NS "nil", -1);

			if (!tracker_sparql_apply_quad (sparql, error)) {
				tracker_token_unset (&sparql->current_state.predicate);
				tracker_token_unset (&sparql->current_state.object);
				goto error;
			}
		}

		tracker_token_unset (&sparql->current_state.predicate);
	}

	sparql->current_state.subject = old_subject;
	sparql->current_state.predicate = old_predicate;
	sparql->current_state.object = old_object;
	sparql->current_state.token = old_token;

	*sparql->current_state.token = g_array_index (elems, TrackerToken, 0);

	for (i = 1; i < elems->len; i++) {
		cur = &g_array_index (elems, TrackerToken, i);
		tracker_token_unset (cur);
	}

	g_array_unref (elems);
	_expect (sparql, RULE_TYPE_LITERAL, LITERAL_CLOSE_PARENS);

	return TRUE;

error:
	sparql->current_state.subject = old_subject;
	sparql->current_state.predicate = old_predicate;
	sparql->current_state.object = old_object;
	sparql->current_state.token = old_token;

	for (i = 0; i < elems->len; i++) {
		cur = &g_array_index (elems, TrackerToken, i);
		tracker_token_unset (cur);
	}

	g_array_unref (elems);

	return FALSE;
}

static gboolean
translate_CollectionPath (TrackerSparql  *sparql,
                          GError        **error)
{
	TrackerToken old_subject, old_predicate, old_object, *old_token;
	TrackerVariable *cur, *first = NULL, *rest = NULL;

	old_subject = sparql->current_state.subject;
	old_predicate = sparql->current_state.predicate;
	old_object = sparql->current_state.object;
	old_token = sparql->current_state.token;

	/* CollectionPath ::= '(' GraphNodePath+ ')'
	 */
	_expect (sparql, RULE_TYPE_LITERAL, LITERAL_OPEN_PARENS);
	while (_check_in_rule (sparql, NAMED_RULE_GraphNodePath)) {
		if (rest) {
			cur = rest;
			rest = NULL;
		} else {
			cur = tracker_select_context_add_generated_variable (TRACKER_SELECT_CONTEXT (sparql->context));
			first = cur;
		}

		tracker_token_variable_init (&sparql->current_state.subject, cur);

		/* rdf:first */
		tracker_token_literal_init (&sparql->current_state.predicate,
		                            RDF_NS "first", -1);
		sparql->current_state.token = &sparql->current_state.object;
		_call_rule (sparql, NAMED_RULE_GraphNodePath, error);
		sparql->current_state.token = NULL;
		tracker_token_unset (&sparql->current_state.predicate);

		/* rdf:rest */
		tracker_token_literal_init (&sparql->current_state.predicate,
		                            RDF_NS "rest", -1);

		if (_check_in_rule (sparql, NAMED_RULE_GraphNodePath)) {
			/* Generate variable for next element */
			rest = tracker_select_context_add_generated_variable (TRACKER_SELECT_CONTEXT (sparql->context));
			tracker_token_variable_init (&sparql->current_state.object, rest);
		} else {
			/* Make last element point to rdf:nil */
			tracker_token_literal_init (&sparql->current_state.object,
			                            RDF_NS "nil", -1);
		}

		if (!_add_quad (sparql,
		                &sparql->current_state.graph,
		                &sparql->current_state.subject,
		                &sparql->current_state.predicate,
		                &sparql->current_state.object,
		                error))
			return FALSE;

		tracker_token_unset (&sparql->current_state.object);
		tracker_token_unset (&sparql->current_state.predicate);

		tracker_token_unset (&sparql->current_state.subject);
	}

	_expect (sparql, RULE_TYPE_LITERAL, LITERAL_CLOSE_PARENS);

	sparql->current_state.subject = old_subject;
	sparql->current_state.predicate = old_predicate;
	sparql->current_state.object = old_object;
	sparql->current_state.token = old_token;
	tracker_token_variable_init (sparql->current_state.token, first);

	return TRUE;
}

static gboolean
translate_GraphNode (TrackerSparql  *sparql,
                     GError        **error)
{
	GError *inner_error = NULL;

	/* GraphNode ::= VarOrTerm | TriplesNode
	 *
	 * TRACKER EXTENSION:
	 * Literal 'NULL' is also accepted, rule is effectively:
	 *   VarOrTerm | TriplesNode | 'NULL'
	 */
	if (_check_in_rule (sparql, NAMED_RULE_VarOrTerm)) {
		_call_rule (sparql, NAMED_RULE_VarOrTerm, error);
	} else if (_check_in_rule (sparql, NAMED_RULE_TriplesNode)) {
		_call_rule (sparql, NAMED_RULE_TriplesNode, error);
	} else if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_NULL)) {
		if (sparql->current_state.type != TRACKER_SPARQL_TYPE_UPDATE)
			_raise (PARSE, "«NULL» literal is not allowed in this mode", "NULL");
		/* Object token is left unset on purpose */
	} else {
		g_assert_not_reached ();
	}

	/* Quoting sparql11-update:
	 * If any solution produces a triple containing an unbound variable
	 * or an illegal RDF construct, such as a literal in a subject or
	 * predicate position, then that triple is not included when processing
	 * the operation: INSERT will not instantiate new data in the output
	 * graph, and DELETE will not remove anything.
	 *
	 * Updates are a Tracker extension and object may be explicitly NULL.
	 */
	if (tracker_token_is_empty (&sparql->current_state.subject) ||
	    tracker_token_is_empty (&sparql->current_state.predicate) ||
	    (tracker_token_is_empty (&sparql->current_state.object) &&
	     sparql->current_state.type != TRACKER_SPARQL_TYPE_UPDATE))
		return TRUE;

	if (!tracker_sparql_apply_quad (sparql, &inner_error)) {
		if (inner_error && !sparql->silent) {
			g_propagate_error (error, inner_error);
			return FALSE;
		}
	}

	tracker_token_unset (&sparql->current_state.object);

	return TRUE;
}

static gboolean
translate_GraphNodePath (TrackerSparql  *sparql,
                         GError        **error)
{
	/* GraphNodePath ::= VarOrTerm | TriplesNodePath
	 */
	if (_check_in_rule (sparql, NAMED_RULE_VarOrTerm)) {
		_call_rule (sparql, NAMED_RULE_VarOrTerm, error);
		g_assert (!tracker_token_is_empty (&sparql->current_state.object));
	} else if (_check_in_rule (sparql, NAMED_RULE_TriplesNodePath)) {
		_call_rule (sparql, NAMED_RULE_TriplesNodePath, error);
		g_assert (!tracker_token_is_empty (&sparql->current_state.object));
	} else {
		g_assert_not_reached ();
	}

	if (!_add_quad (sparql,
			&sparql->current_state.graph,
			&sparql->current_state.subject,
			&sparql->current_state.predicate,
			&sparql->current_state.object,
			error))
		return FALSE;

	tracker_token_unset (&sparql->current_state.object);

	return TRUE;
}

static gboolean
translate_VarOrTerm (TrackerSparql  *sparql,
                     GError        **error)
{
	TrackerGrammarNamedRule rule;

	/* VarOrTerm ::= Var | GraphTerm
	 */
	rule = _current_rule (sparql);

	switch (rule) {
	case NAMED_RULE_Var:
		if (sparql->current_state.type != TRACKER_SPARQL_TYPE_SELECT &&
		    sparql->current_state.type != TRACKER_SPARQL_TYPE_CONSTRUCT &&
		    !sparql->solution_var_map) {
			TrackerParserNode *node = sparql->current_state.node;
			const gchar *str = "Unknown";

			/* Find the insert/delete clause, a child of Update1 */
			while (node) {
				TrackerParserNode *parent;
				const TrackerGrammarRule *rule;

				parent = (TrackerParserNode *) ((GNode *)node)->parent;
				rule = tracker_parser_node_get_rule (parent);

				if (tracker_grammar_rule_is_a (rule, RULE_TYPE_RULE, NAMED_RULE_Update1)) {
					rule = tracker_parser_node_get_rule (node);
					str = rule->string;
					break;
				}

				node = parent;
			}

			_raise (PARSE, "Variables are not allowed in update clause", str);
		}

		_call_rule (sparql, rule, error);
		g_assert (sparql->current_state.token != NULL);
		_init_token (sparql->current_state.token,
			     sparql->current_state.prev_node, sparql);
		break;
	case NAMED_RULE_GraphTerm:
		_call_rule (sparql, rule, error);
		break;
	default:
		g_assert_not_reached ();
	}

	return TRUE;
}

static gboolean
translate_VarOrIri (TrackerSparql  *sparql,
                    GError        **error)
{
	TrackerGrammarNamedRule rule;

	/* VarOrIri ::= Var | iri
	 */
	rule = _current_rule (sparql);

	if (rule == NAMED_RULE_Var) {
		_call_rule (sparql, rule, error);
	} else if (rule == NAMED_RULE_iri) {
		_call_rule (sparql, rule, error);
	} else {
		g_assert_not_reached ();
	}

	return TRUE;
}

static gboolean
translate_Var (TrackerSparql  *sparql,
               GError        **error)
{
	/* Var ::= VAR1 | VAR2
	 */
	sparql->current_state.expression_type = TRACKER_PROPERTY_TYPE_UNKNOWN;

	if (_accept (sparql, RULE_TYPE_TERMINAL, TERMINAL_TYPE_VAR1) ||
	    _accept (sparql, RULE_TYPE_TERMINAL, TERMINAL_TYPE_VAR2)) {
		if (sparql->current_state.type == TRACKER_SPARQL_TYPE_SELECT ||
		    sparql->current_state.type == TRACKER_SPARQL_TYPE_CONSTRUCT) {
			TrackerVariableBinding *binding;
			TrackerVariable *var;

			/* Ensure the variable is referenced in the context */
			var = _extract_node_variable (sparql->current_state.prev_node,
			                              sparql);

			binding = tracker_variable_get_sample_binding (var);

			if (binding)
				sparql->current_state.expression_type = TRACKER_BINDING (binding)->data_type;
		}
	} else {
		g_assert_not_reached ();
	}

	return TRUE;
}

static gboolean
translate_GraphTerm (TrackerSparql  *sparql,
                     GError        **error)
{
	TrackerGrammarNamedRule rule;

	/* GraphTerm ::= iri | RDFLiteral | NumericLiteral | BooleanLiteral | BlankNode | NIL
	 */
	if (_accept (sparql, RULE_TYPE_TERMINAL, TERMINAL_TYPE_NIL)) {
		return TRUE;
	}

	rule = _current_rule (sparql);

	switch (rule) {
	case NAMED_RULE_RDFLiteral:
		_call_rule (sparql, rule, error);
		break;
	case NAMED_RULE_iri:
	case NAMED_RULE_NumericLiteral:
	case NAMED_RULE_BooleanLiteral:
		_call_rule (sparql, rule, error);
		g_assert (sparql->current_state.token != NULL);
		_init_token (sparql->current_state.token,
			     sparql->current_state.prev_node, sparql);
		break;
	case NAMED_RULE_BlankNode:
		_call_rule (sparql, rule, error);
		break;
	default:
		g_assert_not_reached ();
	}

	return TRUE;
}

static gboolean
translate_Expression (TrackerSparql  *sparql,
                      GError        **error)
{
	TrackerStringBuilder *str, *old;
	gboolean convert_to_string;

	/* Expression ::= ConditionalOrExpression
	 */
	convert_to_string = sparql->current_state.convert_to_string;
	sparql->current_state.convert_to_string = FALSE;

	if (convert_to_string) {
		str = _append_placeholder (sparql);
		old = tracker_sparql_swap_builder (sparql, str);
	}

	_call_rule (sparql, NAMED_RULE_ConditionalOrExpression, error);

	if (convert_to_string) {
		convert_expression_to_string (sparql, sparql->current_state.expression_type);
		tracker_sparql_swap_builder (sparql, old);
	}

	sparql->current_state.convert_to_string = convert_to_string;

	return TRUE;

}

static gboolean
translate_ConditionalOrExpression (TrackerSparql  *sparql,
                                   GError        **error)
{
	/* ConditionalOrExpression ::= ConditionalAndExpression ( '||' ConditionalAndExpression )*
	 */
	_call_rule (sparql, NAMED_RULE_ConditionalAndExpression, error);

	while (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_OP_OR)) {
		if (sparql->current_state.expression_type != TRACKER_PROPERTY_TYPE_BOOLEAN)
			_raise (PARSE, "Expected boolean expression", "||");

		_append_string (sparql, " OR ");
		_call_rule (sparql, NAMED_RULE_ConditionalAndExpression, error);

		if (sparql->current_state.expression_type != TRACKER_PROPERTY_TYPE_BOOLEAN)
			_raise (PARSE, "Expected boolean expression", "||");
	}

	return TRUE;
}

static gboolean
translate_ConditionalAndExpression (TrackerSparql  *sparql,
                                    GError        **error)
{
	/* ConditionalAndExpression ::= ValueLogical ( '&&' ValueLogical )*
	 */
	_call_rule (sparql, NAMED_RULE_ValueLogical, error);

	while (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_OP_AND)) {
		if (sparql->current_state.expression_type != TRACKER_PROPERTY_TYPE_BOOLEAN)
			_raise (PARSE, "Expected boolean expression", "&&");

		_append_string (sparql, " AND ");
		_call_rule (sparql, NAMED_RULE_ValueLogical, error);

		if (sparql->current_state.expression_type != TRACKER_PROPERTY_TYPE_BOOLEAN)
			_raise (PARSE, "Expected boolean expression", "&&");
	}

	return TRUE;
}

static gboolean
translate_ValueLogical (TrackerSparql  *sparql,
                        GError        **error)
{
	/* ValueLogical ::= RelationalExpression
	 */
	_call_rule (sparql, NAMED_RULE_RelationalExpression, error);

	return TRUE;
}

static gboolean
translate_RelationalExpression (TrackerSparql  *sparql,
                                GError        **error)
{
	const gchar *old_sep;

	/* RelationalExpression ::= NumericExpression ( '=' NumericExpression | '!=' NumericExpression | '<' NumericExpression | '>' NumericExpression | '<=' NumericExpression | '>=' NumericExpression | 'IN' ExpressionList | 'NOT' 'IN' ExpressionList )?
	 */
	_call_rule (sparql, NAMED_RULE_NumericExpression, error);

	if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_OP_IN)) {
		_append_string (sparql, "IN ");
		old_sep = tracker_sparql_swap_current_expression_list_separator (sparql, ", ");
		_call_rule (sparql, NAMED_RULE_ExpressionList, error);
		tracker_sparql_swap_current_expression_list_separator (sparql, old_sep);
		sparql->current_state.expression_type = TRACKER_PROPERTY_TYPE_BOOLEAN;
	} else if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_NOT)) {
		_expect (sparql, RULE_TYPE_LITERAL, LITERAL_OP_IN);
		_append_string (sparql, "NOT IN ");
		old_sep = tracker_sparql_swap_current_expression_list_separator (sparql, ", ");
		_call_rule (sparql, NAMED_RULE_ExpressionList, error);
		tracker_sparql_swap_current_expression_list_separator (sparql, old_sep);
		sparql->current_state.expression_type = TRACKER_PROPERTY_TYPE_BOOLEAN;
	} else if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_OP_EQ)) {
		_append_string (sparql, " = ");
		_call_rule (sparql, NAMED_RULE_NumericExpression, error);
		sparql->current_state.expression_type = TRACKER_PROPERTY_TYPE_BOOLEAN;
	} else if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_OP_NE)) {
		_append_string (sparql, " != ");
		_call_rule (sparql, NAMED_RULE_NumericExpression, error);
		sparql->current_state.expression_type = TRACKER_PROPERTY_TYPE_BOOLEAN;
	} else if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_OP_LT)) {
		_append_string (sparql, " < ");
		_call_rule (sparql, NAMED_RULE_NumericExpression, error);
		sparql->current_state.expression_type = TRACKER_PROPERTY_TYPE_BOOLEAN;
	} else if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_OP_GT)) {
		_append_string (sparql, " > ");
		_call_rule (sparql, NAMED_RULE_NumericExpression, error);
		sparql->current_state.expression_type = TRACKER_PROPERTY_TYPE_BOOLEAN;
	} else if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_OP_LE)) {
		_append_string (sparql, " <= ");
		_call_rule (sparql, NAMED_RULE_NumericExpression, error);
		sparql->current_state.expression_type = TRACKER_PROPERTY_TYPE_BOOLEAN;
	} else if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_OP_GE)) {
		_append_string (sparql, " >= ");
		_call_rule (sparql, NAMED_RULE_NumericExpression, error);
		sparql->current_state.expression_type = TRACKER_PROPERTY_TYPE_BOOLEAN;
	}

	return TRUE;
}

static gboolean
translate_NumericExpression (TrackerSparql  *sparql,
                             GError        **error)
{
	/* NumericExpression ::= AdditiveExpression
	 */
	_call_rule (sparql, NAMED_RULE_AdditiveExpression, error);

	return TRUE;
}

static gboolean
maybe_numeric (TrackerPropertyType prop_type)
{
	return (prop_type == TRACKER_PROPERTY_TYPE_INTEGER ||
	        prop_type == TRACKER_PROPERTY_TYPE_DOUBLE ||
	        prop_type == TRACKER_PROPERTY_TYPE_DATE ||
	        prop_type == TRACKER_PROPERTY_TYPE_DATETIME ||
	        prop_type == TRACKER_PROPERTY_TYPE_UNKNOWN);
}

static gboolean
translate_AdditiveExpression (TrackerSparql  *sparql,
                              GError        **error)
{
	/* AdditiveExpression ::= MultiplicativeExpression ( '+' MultiplicativeExpression | '-' MultiplicativeExpression | ( NumericLiteralPositive | NumericLiteralNegative ) ( ( '*' UnaryExpression ) | ( '/' UnaryExpression ) )* )*
	 */
	_call_rule (sparql, NAMED_RULE_MultiplicativeExpression, error);

	do {
		if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_ARITH_PLUS)) {
			if (!maybe_numeric (sparql->current_state.expression_type))
				_raise (PARSE, "Expected numeric operand", "+");

			_append_string (sparql, " + ");
			_call_rule (sparql, NAMED_RULE_MultiplicativeExpression, error);

			if (!maybe_numeric (sparql->current_state.expression_type))
				_raise (PARSE, "Expected numeric operand", "+");
		} else if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_ARITH_MINUS)) {
			if (!maybe_numeric (sparql->current_state.expression_type))
				_raise (PARSE, "Expected numeric operand", "-");
			_append_string (sparql, " - ");
			_call_rule (sparql, NAMED_RULE_MultiplicativeExpression, error);

			if (!maybe_numeric (sparql->current_state.expression_type))
				_raise (PARSE, "Expected numeric operand", "+");
		} else if (_check_in_rule (sparql, NAMED_RULE_NumericLiteralPositive) ||
		           _check_in_rule (sparql, NAMED_RULE_NumericLiteralNegative)) {
			if (!maybe_numeric (sparql->current_state.expression_type))
				_raise (PARSE, "Expected numeric operand", "multiplication/division");

			_call_rule (sparql, _current_rule (sparql), error);

			do {
				if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_ARITH_MULT)) {
					_append_string (sparql, " * ");
				} else if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_ARITH_DIV)) {
					_append_string (sparql, " / ");
				} else {
					break;
				}

				_call_rule (sparql, NAMED_RULE_UnaryExpression, error);
				if (!maybe_numeric (sparql->current_state.expression_type))
					_raise (PARSE, "Expected numeric operand", "multiplication/division");
			} while (TRUE);
		} else {
			break;
		}
	} while (TRUE);

	return TRUE;
}

static gboolean
translate_MultiplicativeExpression (TrackerSparql  *sparql,
                                    GError        **error)
{
	/* MultiplicativeExpression ::= UnaryExpression ( '*' UnaryExpression | '/' UnaryExpression )*
	 */
	_call_rule (sparql, NAMED_RULE_UnaryExpression, error);

	do {
		if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_ARITH_MULT)) {
			_append_string (sparql, " * ");
		} else if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_ARITH_DIV)) {
			_append_string (sparql, " / ");
		} else {
			break;
		}

		_call_rule (sparql, NAMED_RULE_UnaryExpression, error);
	} while (TRUE);

	return TRUE;
}

static gboolean
translate_UnaryExpression (TrackerSparql  *sparql,
                           GError        **error)
{
	/* UnaryExpression ::= '!' PrimaryExpression
	 *                     | '+' PrimaryExpression
	 *                     | '-' PrimaryExpression
	 *                     | PrimaryExpression
	 */
	if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_OP_NEG)) {
		_append_string (sparql, "NOT (");
		_call_rule (sparql, NAMED_RULE_PrimaryExpression, error);
		_append_string (sparql, ") ");

		if (sparql->current_state.expression_type != TRACKER_PROPERTY_TYPE_BOOLEAN) {
			_raise (PARSE, "Expected boolean expression", "UnaryExpression");
		}
	} else if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_ARITH_PLUS)) {
		_call_rule (sparql, NAMED_RULE_PrimaryExpression, error);
	} else if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_ARITH_MINUS)) {
		_append_string (sparql, "-(");
		_call_rule (sparql, NAMED_RULE_PrimaryExpression, error);
		_append_string (sparql, ") ");
	} else {
		_call_rule (sparql, NAMED_RULE_PrimaryExpression, error);
	}

	return TRUE;
}

static gboolean
translate_PrimaryExpression (TrackerSparql  *sparql,
                             GError        **error)
{
	TrackerSelectContext *select_context;
	TrackerGrammarNamedRule rule;
	TrackerBinding *binding;
	TrackerVariable *variable;

	/* PrimaryExpression ::= BrackettedExpression | BuiltInCall | iriOrFunction | RDFLiteral | NumericLiteral | BooleanLiteral | Var
	 */
	rule = _current_rule (sparql);
	select_context = TRACKER_SELECT_CONTEXT (sparql->context);

	switch (rule) {
	case NAMED_RULE_NumericLiteral:
	case NAMED_RULE_BooleanLiteral:
		_call_rule (sparql, rule, error);
		binding = _convert_terminal (sparql);
		tracker_select_context_add_literal_binding (select_context,
		                                            TRACKER_LITERAL_BINDING (binding));
		_append_literal_sql (sparql, TRACKER_LITERAL_BINDING (binding));
		g_object_unref (binding);
		break;
	case NAMED_RULE_Var:
		_call_rule (sparql, rule, error);
		variable = _last_node_variable (sparql);
		_append_variable_sql (sparql, variable);

		/* If the variable is bound, propagate the binding data type */
		if (tracker_variable_has_bindings (variable)) {
			binding = TRACKER_BINDING (tracker_variable_get_sample_binding (variable));
			sparql->current_state.expression_type = binding->data_type;
		}
		break;
	case NAMED_RULE_RDFLiteral:
		_call_rule (sparql, rule, error);
		binding = g_ptr_array_index (select_context->literal_bindings,
		                             select_context->literal_bindings->len - 1);
		_append_literal_sql (sparql, TRACKER_LITERAL_BINDING (binding));
		break;
	case NAMED_RULE_BrackettedExpression:
	case NAMED_RULE_BuiltInCall:
	case NAMED_RULE_iriOrFunction:
		_call_rule (sparql, rule, error);
		break;
	default:
		g_assert_not_reached ();
	}

	return TRUE;
}

static gboolean
handle_property_function (TrackerSparql    *sparql,
			  TrackerProperty  *property,
			  GError          **error)
{
	TrackerPropertyType type;

	if (tracker_property_get_multiple_values (property)) {
		TrackerStringBuilder *str, *old;

		_append_string (sparql, "(SELECT GROUP_CONCAT (");
		str = _append_placeholder (sparql);
		old = tracker_sparql_swap_builder (sparql, str);
		_append_string_printf (sparql, "\"%s\"", tracker_property_get_name (property));
		convert_expression_to_string (sparql, tracker_property_get_data_type (property));
		tracker_sparql_swap_builder (sparql, old);

		_append_string (sparql, ", ',') ");

		type = TRACKER_PROPERTY_TYPE_STRING;
	} else {
		_append_string_printf (sparql,
				       "(SELECT \"%s\" ",
				       tracker_property_get_name (property));

		type = tracker_property_get_data_type (property);
	}

	if (tracker_token_is_empty (&sparql->current_state.graph)) {
		tracker_sparql_add_union_graph_subquery (sparql, property);

		_append_string_printf (sparql, "FROM \"unionGraph_%s\" ",
		                       tracker_property_get_table_name (property));
	} else {
		_append_string_printf (sparql, "FROM \"%s\".\"%s\" ",
		                       tracker_token_get_idstring (&sparql->current_state.graph),
		                       tracker_property_get_table_name (property));
	}

	_append_string (sparql, "WHERE ID = ");
	_call_rule (sparql, NAMED_RULE_ArgList, error);
	_append_string_printf (sparql, "AND \"%s\" IS NOT NULL",
	                       tracker_property_get_name (property));
	_append_string (sparql, ") ");

	sparql->current_state.expression_type = type;

	return TRUE;
}

static gboolean
handle_type_cast (TrackerSparql  *sparql,
		  const gchar    *function,
		  GError        **error)
{
	sparql->current_state.convert_to_string = TRUE;

	if (g_str_equal (function, XSD_NS "string")) {
		_append_string (sparql, "CAST (");
		_call_rule (sparql, NAMED_RULE_ArgList, error);
		_append_string (sparql, "AS TEXT) ");
		sparql->current_state.expression_type = TRACKER_PROPERTY_TYPE_STRING;
	} else if (g_str_equal (function, RDF_NS "langString")) {
		_append_string (sparql, "CAST (");
		_call_rule (sparql, NAMED_RULE_ArgList, error);
		_append_string (sparql, "AS BLOB) ");
		sparql->current_state.expression_type = TRACKER_PROPERTY_TYPE_LANGSTRING;
	} else if (g_str_equal (function, XSD_NS "integer")) {
		_append_string (sparql, "CAST (");
		_call_rule (sparql, NAMED_RULE_ArgList, error);
		_append_string (sparql, "AS INTEGER) ");
		sparql->current_state.expression_type = TRACKER_PROPERTY_TYPE_INTEGER;
	} else if (g_str_equal (function, XSD_NS "double")) {
		_append_string (sparql, "CAST (");
		_call_rule (sparql, NAMED_RULE_ArgList, error);
		_append_string (sparql, "AS REAL) ");
		sparql->current_state.expression_type = TRACKER_PROPERTY_TYPE_DOUBLE;
	} else {
		_raise (PARSE, "Unhandled cast conversion", function);
	}

	return TRUE;
}

static gboolean
handle_xpath_function (TrackerSparql  *sparql,
                       const gchar    *function,
                       GError        **error)
{
	if (g_str_equal (function, FN_NS "lower-case")) {
		_append_string (sparql, "SparqlLowerCase (");
		sparql->current_state.convert_to_string = TRUE;
		_call_rule (sparql, NAMED_RULE_ArgList, error);
		_append_string (sparql, ") ");
	} else if (g_str_equal (function, FN_NS "upper-case")) {
		_append_string (sparql, "SparqlUpperCase (");
		sparql->current_state.convert_to_string = TRUE;
		_call_rule (sparql, NAMED_RULE_ArgList, error);
		_append_string (sparql, ") ");
	} else if (g_str_equal (function, FN_NS "contains")) {
		/* contains('A','B') => 'A' GLOB '*B*' */
		sparql->current_state.convert_to_string = TRUE;
		_step (sparql);
		_expect (sparql, RULE_TYPE_LITERAL, LITERAL_OPEN_PARENS);
		_append_string (sparql, "(");
		_call_rule (sparql, NAMED_RULE_Expression, error);
		_expect (sparql, RULE_TYPE_LITERAL, LITERAL_COMMA);
		_append_string (sparql, " GLOB '*' || ");
		_call_rule (sparql, NAMED_RULE_Expression, error);
		_append_string (sparql, " || '*') ");
		_expect (sparql, RULE_TYPE_LITERAL, LITERAL_CLOSE_PARENS);
		sparql->current_state.expression_type = TRACKER_PROPERTY_TYPE_BOOLEAN;
	} else if (g_str_equal (function, FN_NS "starts-with")) {
		gchar buf[6] = { 0 };
		TrackerParserNode *node;

		/* strstarts('A','B') => 'A' BETWEEN 'B' AND 'B\u0010fffd'
		 * 0010fffd always sorts last.
		 */

		sparql->current_state.convert_to_string = TRUE;
		_step (sparql);
		_append_string (sparql, "( ");
		_expect (sparql, RULE_TYPE_LITERAL, LITERAL_OPEN_PARENS);
		_call_rule (sparql, NAMED_RULE_Expression, error);
		_expect (sparql, RULE_TYPE_LITERAL, LITERAL_COMMA);
		_append_string (sparql, "BETWEEN ");

		node = sparql->current_state.node;
		_call_rule (sparql, NAMED_RULE_Expression, error);
		_append_string (sparql, "AND ");

		/* Evaluate the same expression node again */
		sparql->current_state.node = node;
		_call_rule (sparql, NAMED_RULE_Expression, error);

		g_unichar_to_utf8 (TRACKER_COLLATION_LAST_CHAR, buf);
		_append_string_printf (sparql, "|| '%s') ", buf);
		_expect (sparql, RULE_TYPE_LITERAL, LITERAL_CLOSE_PARENS);
		sparql->current_state.expression_type = TRACKER_PROPERTY_TYPE_BOOLEAN;
	} else if (g_str_equal (function, FN_NS "ends-with")) {
		/* strends('A','B') => 'A' GLOB '*B' */
		sparql->current_state.convert_to_string = TRUE;
		_step (sparql);
		_expect (sparql, RULE_TYPE_LITERAL, LITERAL_OPEN_PARENS);
		_append_string (sparql, "(");
		_call_rule (sparql, NAMED_RULE_Expression, error);
		_expect (sparql, RULE_TYPE_LITERAL, LITERAL_COMMA);
		_append_string (sparql, " GLOB '*' || ");
		_call_rule (sparql, NAMED_RULE_Expression, error);
		_expect (sparql, RULE_TYPE_LITERAL, LITERAL_CLOSE_PARENS);
		_append_string (sparql, ") ");
		sparql->current_state.expression_type = TRACKER_PROPERTY_TYPE_BOOLEAN;
	} else if (g_str_equal (function, FN_NS "substring")) {
		_append_string (sparql, "SUBSTR (");
		sparql->current_state.convert_to_string = TRUE;
		_call_rule (sparql, NAMED_RULE_ArgList, error);
		_append_string (sparql, ") ");
		sparql->current_state.expression_type = TRACKER_PROPERTY_TYPE_STRING;
	} else if (g_str_equal (function, FN_NS "concat")) {
		const gchar *old_sep;

		old_sep = tracker_sparql_swap_current_expression_list_separator (sparql, " || ");
		sparql->current_state.convert_to_string = TRUE;
		_call_rule (sparql, NAMED_RULE_ArgList, error);
		tracker_sparql_swap_current_expression_list_separator (sparql, old_sep);
		sparql->current_state.expression_type = TRACKER_PROPERTY_TYPE_STRING;
	} else if (g_str_equal (function, FN_NS "string-join")) {
		sparql->current_state.convert_to_string = TRUE;
		_append_string (sparql, "SparqlStringJoin (");
		_step (sparql);
		_expect (sparql, RULE_TYPE_LITERAL, LITERAL_OPEN_PARENS);

		if (!_check_in_rule (sparql, NAMED_RULE_ArgList))
			_raise (PARSE, "List of strings to join must be surrounded by parentheses", "fn:string-join");

		_call_rule (sparql, NAMED_RULE_ArgList, error);

		while (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_COMMA)) {
			_append_string (sparql, ", ");
			_call_rule (sparql, NAMED_RULE_Expression, error);
		}

		_expect (sparql, RULE_TYPE_LITERAL, LITERAL_CLOSE_PARENS);
		_append_string (sparql, ") ");
		sparql->current_state.expression_type = TRACKER_PROPERTY_TYPE_STRING;
	} else if (g_str_equal (function, FN_NS "replace")) {
		sparql->current_state.convert_to_string = TRUE;
		_append_string (sparql, "SparqlReplace (");
		_call_rule (sparql, NAMED_RULE_ArgList, error);
		_append_string (sparql, ") ");
		sparql->current_state.expression_type = TRACKER_PROPERTY_TYPE_STRING;
	} else if (g_str_equal (function, FN_NS "year-from-dateTime")) {
		_step (sparql);
		if (!helper_translate_date (sparql, "%Y", error))
			return FALSE;
		sparql->current_state.expression_type = TRACKER_PROPERTY_TYPE_INTEGER;
	} else if (g_str_equal (function, FN_NS "month-from-dateTime")) {
		_step (sparql);
		if (!helper_translate_date (sparql, "%m", error))
			return FALSE;
		sparql->current_state.expression_type = TRACKER_PROPERTY_TYPE_INTEGER;
	} else if (g_str_equal (function, FN_NS "day-from-dateTime")) {
		_step (sparql);
		if (!helper_translate_date (sparql, "%d", error))
			return FALSE;
		sparql->current_state.expression_type = TRACKER_PROPERTY_TYPE_INTEGER;
	} else if (g_str_equal (function, FN_NS "hours-from-dateTime")) {
		_step (sparql);
		if (!helper_translate_time (sparql, TIME_FORMAT_HOURS, error))
			return FALSE;
		sparql->current_state.expression_type = TRACKER_PROPERTY_TYPE_INTEGER;
	} else if (g_str_equal (function, FN_NS "minutes-from-dateTime")) {
		_step (sparql);
		if (!helper_translate_time (sparql, TIME_FORMAT_MINUTES, error))
			return FALSE;
		sparql->current_state.expression_type = TRACKER_PROPERTY_TYPE_INTEGER;
	} else if (g_str_equal (function, FN_NS "seconds-from-dateTime")) {
		_step (sparql);
		if (!helper_translate_time (sparql, TIME_FORMAT_SECONDS, error))
			return FALSE;
		sparql->current_state.expression_type = TRACKER_PROPERTY_TYPE_INTEGER;
	} else if (g_str_equal (function, FN_NS "timezone-from-dateTime")) {
		_step (sparql);
		_append_string (sparql, "SparqlTimezoneDuration( ");
		_expect (sparql, RULE_TYPE_LITERAL, LITERAL_OPEN_PARENS);

		_call_rule (sparql, NAMED_RULE_Expression, error);

		_expect (sparql, RULE_TYPE_LITERAL, LITERAL_CLOSE_PARENS);
		_append_string (sparql, ") ");

		sparql->current_state.expression_type = TRACKER_PROPERTY_TYPE_INTEGER;
	} else {
		_raise (PARSE, "Unknown XPath function", function);
	}

	return TRUE;
}

static TrackerVariable *
find_fts_variable (TrackerSparql     *sparql,
                   TrackerParserNode *node,
		   const gchar       *suffix)
{
	TrackerParserNode *var = NULL;

	node = tracker_sparql_parser_tree_find_next (node, TRUE);

	if (!_accept_token (&node, RULE_TYPE_LITERAL, LITERAL_OPEN_PARENS, NULL))
		return NULL;

	if (_accept_token (&node, RULE_TYPE_TERMINAL, TERMINAL_TYPE_VAR1, &var) ||
	    _accept_token (&node, RULE_TYPE_TERMINAL, TERMINAL_TYPE_VAR2, &var)) {
		TrackerVariable *variable;
		gchar *node_var, *full;

		node_var = _extract_node_string (var, sparql);
		full = g_strdup_printf ("%s:%s", node_var, suffix);
		variable = _ensure_variable (sparql, full);
		g_free (full);
		g_free (node_var);

		return variable;
	}

	return NULL;
}

static gboolean
handle_custom_function (TrackerSparql  *sparql,
			const gchar    *function,
			GError        **error)
{
	TrackerVariable *variable;
	TrackerParserNode *node;

	if (g_str_equal (function, TRACKER_NS "case-fold")) {
		sparql->current_state.convert_to_string = TRUE;
		_append_string (sparql, "SparqlCaseFold (");
		_call_rule (sparql, NAMED_RULE_ArgList, error);
		_append_string (sparql, ") ");
	} else if (g_str_equal (function, TRACKER_NS "title-order")) {
		_call_rule (sparql, NAMED_RULE_ArgList, error);
		_append_string (sparql, "COLLATE " TRACKER_TITLE_COLLATION_NAME " ");
	} else if (g_str_equal (function, TRACKER_NS "ascii-lower-case")) {
		sparql->current_state.convert_to_string = TRUE;
		_append_string (sparql, "lower (");
		_call_rule (sparql, NAMED_RULE_ArgList, error);
		_append_string (sparql, ") ");
	} else if (g_str_equal (function, TRACKER_NS "normalize")) {
		sparql->current_state.convert_to_string = TRUE;
		_append_string (sparql, "SparqlNormalize (");
		_call_rule (sparql, NAMED_RULE_ArgList, error);
		_append_string (sparql, ") ");
	} else if (g_str_equal (function, TRACKER_NS "unaccent")) {
		sparql->current_state.convert_to_string = TRUE;
		_append_string (sparql, "SparqlUnaccent (");
		_call_rule (sparql, NAMED_RULE_ArgList, error);
		_append_string (sparql, ") ");
	} else if (g_str_equal (function, TRACKER_NS "id")) {
		_call_rule (sparql, NAMED_RULE_ArgList, error);

		if (sparql->current_state.expression_type != TRACKER_PROPERTY_TYPE_RESOURCE)
			_raise (PARSE, "Expected resource", "tracker:id");

		sparql->current_state.expression_type = TRACKER_PROPERTY_TYPE_INTEGER;
	} else if (g_str_equal (function, TRACKER_NS "uri")) {
		_call_rule (sparql, NAMED_RULE_ArgList, error);

		if (sparql->current_state.expression_type != TRACKER_PROPERTY_TYPE_INTEGER)
			_raise (PARSE, "Expected integer ID", "tracker:uri");

		sparql->current_state.expression_type = TRACKER_PROPERTY_TYPE_RESOURCE;
	} else if (g_str_equal (function, TRACKER_NS "cartesian-distance")) {
		_append_string (sparql, "SparqlCartesianDistance (");
		_call_rule (sparql, NAMED_RULE_ArgList, error);
		_append_string (sparql, ") ");
		sparql->current_state.expression_type = TRACKER_PROPERTY_TYPE_DOUBLE;
	} else if (g_str_equal (function, TRACKER_NS "haversine-distance")) {
		_append_string (sparql, "SparqlHaversineDistance (");
		_call_rule (sparql, NAMED_RULE_ArgList, error);
		_append_string (sparql, ") ");
		sparql->current_state.expression_type = TRACKER_PROPERTY_TYPE_DOUBLE;
	} else if (g_str_equal (function, TRACKER_NS "uri-is-parent")) {
		_append_string (sparql, "SparqlUriIsParent (");
		_call_rule (sparql, NAMED_RULE_ArgList, error);
		_append_string (sparql, ") ");
		sparql->current_state.expression_type = TRACKER_PROPERTY_TYPE_BOOLEAN;
	} else if (g_str_equal (function, TRACKER_NS "uri-is-descendant")) {
		_append_string (sparql, "SparqlUriIsDescendant (");
		_call_rule (sparql, NAMED_RULE_ArgList, error);
		_append_string (sparql, ") ");
		sparql->current_state.expression_type = TRACKER_PROPERTY_TYPE_BOOLEAN;
	} else if (g_str_equal (function, TRACKER_NS "string-from-filename")) {
		_append_string (sparql, "SparqlStringFromFilename (");
		_call_rule (sparql, NAMED_RULE_ArgList, error);
		_append_string (sparql, ") ");
		sparql->current_state.expression_type = TRACKER_PROPERTY_TYPE_STRING;
	} else if (g_str_equal (function, TRACKER_NS "coalesce")) {
		sparql->current_state.convert_to_string = TRUE;
		_append_string (sparql, "COALESCE (");
		_call_rule (sparql, NAMED_RULE_ArgList, error);
		_append_string (sparql, ") ");
		sparql->current_state.expression_type = TRACKER_PROPERTY_TYPE_STRING;
	} else if (g_str_equal (function, FTS_NS "rank")) {
		node = _skip_rule (sparql, NAMED_RULE_ArgList);
		variable = find_fts_variable (sparql, node, "ftsRank");
		if (!variable)
			_raise (PARSE, "Function expects single variable argument", "fts:rank");

		_append_variable_sql (sparql, variable);
		sparql->current_state.expression_type = TRACKER_PROPERTY_TYPE_INTEGER;
	} else if (g_str_equal (function, FTS_NS "offsets")) {
		node = _skip_rule (sparql, NAMED_RULE_ArgList);
		variable = find_fts_variable (sparql, node, "ftsOffsets");
		if (!variable || !tracker_variable_has_bindings (variable))
			_raise (PARSE, "Function expects single variable argument", "fts:offsets");

		_append_variable_sql (sparql, variable);
		sparql->current_state.expression_type = TRACKER_PROPERTY_TYPE_STRING;
	} else if (g_str_equal (function, FTS_NS "snippet")) {
		node = _skip_rule (sparql, NAMED_RULE_ArgList);
		variable = find_fts_variable (sparql, node, "ftsSnippet");
		if (!variable || !tracker_variable_has_bindings (variable))
			_raise (PARSE, "Function expects variable argument", "fts:snippet");

		_append_variable_sql (sparql, variable);
		sparql->current_state.expression_type = TRACKER_PROPERTY_TYPE_STRING;
	} else {
		_raise (PARSE, "Unknown function", function);
	}

	return TRUE;
}

static gboolean
handle_function_call (TrackerSparql  *sparql,
                      GError        **error)
{
	gchar *function = _dup_last_string (sparql);
	gboolean handled, convert_to_string;

	convert_to_string = sparql->current_state.convert_to_string;
	sparql->current_state.convert_to_string = FALSE;

	if (g_str_has_prefix (function, XSD_NS) ||
	    strcmp (function, RDF_NS "langString") == 0) {
		handled = handle_type_cast (sparql, function, error);
	} else if (g_str_has_prefix (function, FN_NS)) {
		handled = handle_xpath_function (sparql, function, error);
	} else {
		TrackerOntologies *ontologies;
		TrackerProperty *property;

		ontologies = tracker_data_manager_get_ontologies (sparql->data_manager);
		property = tracker_ontologies_get_property_by_uri (ontologies, function);

		if (property) {
			handled = handle_property_function (sparql, property, error);
		} else {
			handled = handle_custom_function (sparql, function, error);
		}
	}

	sparql->current_state.convert_to_string = convert_to_string;
	g_free (function);

	return handled;
}

static gboolean
translate_iriOrFunction (TrackerSparql  *sparql,
                         GError        **error)
{
	gboolean handled = TRUE;

	/* iriOrFunction ::= iri ArgList?
	 */
	_call_rule (sparql, NAMED_RULE_iri, error);

	if (_check_in_rule (sparql, NAMED_RULE_ArgList)) {
		handled = handle_function_call (sparql, error);
	} else {
		TrackerBinding *binding;

		binding = _convert_terminal (sparql);
		tracker_select_context_add_literal_binding (TRACKER_SELECT_CONTEXT (sparql->context),
		                                            TRACKER_LITERAL_BINDING (binding));
		_append_literal_sql (sparql, TRACKER_LITERAL_BINDING (binding));
		g_object_unref (binding);
	}

	return handled;
}

static gboolean
translate_BrackettedExpression (TrackerSparql  *sparql,
                                GError        **error)
{
	TrackerGrammarNamedRule rule;

	/* BrackettedExpression ::= '(' Expression ')'
	 *
	 * TRACKER EXTENSION:
	 * SubSelect is accepted too, thus the grammar results in:
	 * '(' ( Expression | SubSelect) ')'
	 */
	_expect (sparql, RULE_TYPE_LITERAL, LITERAL_OPEN_PARENS);
	_append_string (sparql, "(");
	rule = _current_rule (sparql);

	switch (rule) {
	case NAMED_RULE_Expression:
	case NAMED_RULE_SubSelect:
		_call_rule (sparql, rule, error);
		break;
	default:
		g_assert_not_reached ();
	}

	_expect (sparql, RULE_TYPE_LITERAL, LITERAL_CLOSE_PARENS);
	_append_string (sparql, ") ");

	return TRUE;
}

static gboolean
helper_translate_date (TrackerSparql  *sparql,
                       const gchar    *format,
                       GError        **error)
{
	_expect (sparql, RULE_TYPE_LITERAL, LITERAL_OPEN_PARENS);
	_append_string_printf (sparql, "strftime (\"%s\", SparqlTimestamp (", format);

	_call_rule (sparql, NAMED_RULE_Expression, error);

	_expect (sparql, RULE_TYPE_LITERAL, LITERAL_CLOSE_PARENS);
	_append_string (sparql, "), \"unixepoch\") ");

	return TRUE;
}

static gboolean
helper_translate_time (TrackerSparql  *sparql,
                       guint           format,
                       GError        **error)
{
	_append_string (sparql, "CAST (SparqlTimestamp (");
	_expect (sparql, RULE_TYPE_LITERAL, LITERAL_OPEN_PARENS);
	_call_rule (sparql, NAMED_RULE_Expression, error);
	_expect (sparql, RULE_TYPE_LITERAL, LITERAL_CLOSE_PARENS);

	switch (format) {
	case TIME_FORMAT_SECONDS:
		_append_string (sparql, ") AS INTEGER) % 60 ");
		break;
	case TIME_FORMAT_MINUTES:
		_append_string (sparql, ") AS INTEGER) / 60 % 60 ");
		break;
	case TIME_FORMAT_HOURS:
		_append_string (sparql, ") AS INTEGER) / 3600 % 24 ");
		break;
	default:
		g_assert_not_reached ();
	}

	return TRUE;
}

static gboolean
helper_datatype (TrackerSparql      *sparql,
                 TrackerParserNode  *node,
                 GError            **error)
{
	TrackerStringBuilder *dummy;
	gboolean retval;

	_append_string (sparql, "SparqlDataType (");

	if (g_node_n_nodes ((GNode *) node, G_TRAVERSE_LEAVES) == 1) {
		TrackerVariable *var, *type_var;
		TrackerParserNode *arg;

		arg = tracker_sparql_parser_tree_find_next (node, TRUE);
		var = _extract_node_variable (arg, sparql);

		if (var) {
			/* This is a simple is*(?u) statement, check if the variable type is known */
			type_var = lookup_subvariable (sparql, var, "type");

			if (type_var && tracker_variable_has_bindings (type_var)) {
				/* Exists and is known, use it */
				_append_variable_sql (sparql, type_var);
				_append_string (sparql, ") ");

				return TRUE;
			}
		}
	}

	/* Redirect output to a dummy string, we just care of the parsed expression type */
	dummy = tracker_string_builder_new ();
	retval = _postprocess_rule (sparql, node, dummy, error);
	tracker_string_builder_free (dummy);

	if (!retval)
		return retval;

	_append_string_printf (sparql, "%d) ", sparql->current_state.expression_type);

	return TRUE;
}

static gboolean
translate_BuiltInCall (TrackerSparql  *sparql,
                       GError        **error)
{
	TrackerParserNode *node;
	gboolean convert_to_string;
	const gchar *old_sep;

	convert_to_string = sparql->current_state.convert_to_string;
	sparql->current_state.convert_to_string = FALSE;

	if (_check_in_rule (sparql, NAMED_RULE_Aggregate)) {
		_call_rule (sparql, NAMED_RULE_Aggregate, error);
	} else if (_check_in_rule (sparql, NAMED_RULE_RegexExpression)) {
		_call_rule (sparql, NAMED_RULE_RegexExpression, error);
	} else if (_check_in_rule (sparql, NAMED_RULE_ExistsFunc)) {
		_call_rule (sparql, NAMED_RULE_ExistsFunc, error);
	} else if (_check_in_rule (sparql, NAMED_RULE_NotExistsFunc)) {
		_call_rule (sparql, NAMED_RULE_NotExistsFunc, error);
	} else if (_check_in_rule (sparql, NAMED_RULE_SubstringExpression)) {
		_call_rule (sparql, NAMED_RULE_SubstringExpression, error);
	} else if (_check_in_rule (sparql, NAMED_RULE_StrReplaceExpression)) {
		_call_rule (sparql, NAMED_RULE_StrReplaceExpression, error);
	} else if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_STR)) {
		TrackerStringBuilder *str, *old;

		str = _append_placeholder (sparql);
		old = tracker_sparql_swap_builder (sparql, str);
		_expect (sparql, RULE_TYPE_LITERAL, LITERAL_OPEN_PARENS);
		_call_rule (sparql, NAMED_RULE_Expression, error);
		_expect (sparql, RULE_TYPE_LITERAL, LITERAL_CLOSE_PARENS);

		convert_expression_to_string (sparql, sparql->current_state.expression_type);
		sparql->current_state.expression_type = TRACKER_PROPERTY_TYPE_STRING;
		tracker_sparql_swap_builder (sparql, old);
	} else if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_DATATYPE)) {
		_expect (sparql, RULE_TYPE_LITERAL, LITERAL_OPEN_PARENS);
		node = _skip_rule (sparql, NAMED_RULE_Expression);
		_expect (sparql, RULE_TYPE_LITERAL, LITERAL_CLOSE_PARENS);

		_append_string (sparql, "NULLIF (");
		if (!helper_datatype (sparql, node, error))
			return FALSE;

		_append_string (sparql, ", \"" RDFS_NS "Resource\") ");
		sparql->current_state.expression_type = TRACKER_PROPERTY_TYPE_STRING;
	} else if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_URI) ||
	           _accept (sparql, RULE_TYPE_LITERAL, LITERAL_IRI)) {
		sparql->current_state.convert_to_string = TRUE;
		_expect (sparql, RULE_TYPE_LITERAL, LITERAL_OPEN_PARENS);
		_append_string (sparql, "SparqlUri (");
		_call_rule (sparql, NAMED_RULE_Expression, error);
		_expect (sparql, RULE_TYPE_LITERAL, LITERAL_CLOSE_PARENS);
		_append_string (sparql, ") ");
		sparql->current_state.expression_type = TRACKER_PROPERTY_TYPE_STRING;
	} else if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_ABS)) {
		_expect (sparql, RULE_TYPE_LITERAL, LITERAL_OPEN_PARENS);
		_append_string (sparql, "ABS (");
		_call_rule (sparql, NAMED_RULE_Expression, error);
		_expect (sparql, RULE_TYPE_LITERAL, LITERAL_CLOSE_PARENS);
		_append_string (sparql, ") ");
	} else if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_CEIL)) {
		_expect (sparql, RULE_TYPE_LITERAL, LITERAL_OPEN_PARENS);
		_append_string (sparql, "SparqlCeil (");
		_call_rule (sparql, NAMED_RULE_Expression, error);
		_expect (sparql, RULE_TYPE_LITERAL, LITERAL_CLOSE_PARENS);
		_append_string (sparql, ") ");
	} else if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_FLOOR)) {
		_expect (sparql, RULE_TYPE_LITERAL, LITERAL_OPEN_PARENS);
		_append_string (sparql, "SparqlFloor (");
		_call_rule (sparql, NAMED_RULE_Expression, error);
		_expect (sparql, RULE_TYPE_LITERAL, LITERAL_CLOSE_PARENS);
		_append_string (sparql, ") ");
	} else if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_ROUND)) {
		_expect (sparql, RULE_TYPE_LITERAL, LITERAL_OPEN_PARENS);
		_append_string (sparql, "ROUND (");
		_call_rule (sparql, NAMED_RULE_Expression, error);
		_expect (sparql, RULE_TYPE_LITERAL, LITERAL_CLOSE_PARENS);
		_append_string (sparql, ") ");
	} else if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_STRLEN)) {
		sparql->current_state.convert_to_string = TRUE;
		_expect (sparql, RULE_TYPE_LITERAL, LITERAL_OPEN_PARENS);
		_append_string (sparql, "LENGTH (");
		_call_rule (sparql, NAMED_RULE_Expression, error);
		_expect (sparql, RULE_TYPE_LITERAL, LITERAL_CLOSE_PARENS);
		_append_string (sparql, ") ");
		sparql->current_state.expression_type = TRACKER_PROPERTY_TYPE_INTEGER;
	} else if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_UCASE)) {
		sparql->current_state.convert_to_string = TRUE;
		_expect (sparql, RULE_TYPE_LITERAL, LITERAL_OPEN_PARENS);
		_append_string (sparql, "SparqlUpperCase (");
		_call_rule (sparql, NAMED_RULE_Expression, error);
		_expect (sparql, RULE_TYPE_LITERAL, LITERAL_CLOSE_PARENS);
		_append_string (sparql, ") ");
	} else if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_LCASE)) {
		sparql->current_state.convert_to_string = TRUE;
		_expect (sparql, RULE_TYPE_LITERAL, LITERAL_OPEN_PARENS);
		_append_string (sparql, "SparqlLowerCase (");
		_call_rule (sparql, NAMED_RULE_Expression, error);
		_expect (sparql, RULE_TYPE_LITERAL, LITERAL_CLOSE_PARENS);
		_append_string (sparql, ") ");
	} else if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_ENCODE_FOR_URI)) {
		_expect (sparql, RULE_TYPE_LITERAL, LITERAL_OPEN_PARENS);
		_append_string (sparql, "SparqlEncodeForUri (");
		_call_rule (sparql, NAMED_RULE_Expression, error);
		_expect (sparql, RULE_TYPE_LITERAL, LITERAL_CLOSE_PARENS);
		_append_string (sparql, ") ");
		sparql->current_state.expression_type = TRACKER_PROPERTY_TYPE_STRING;
	} else if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_YEAR)) {
		if (!helper_translate_date (sparql, "%Y", error))
			return FALSE;
		sparql->current_state.expression_type = TRACKER_PROPERTY_TYPE_INTEGER;
	} else if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_MONTH)) {
		if (!helper_translate_date (sparql, "%m", error))
			return FALSE;
		sparql->current_state.expression_type = TRACKER_PROPERTY_TYPE_INTEGER;
	} else if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_DAY)) {
		if (!helper_translate_date (sparql, "%d", error))
			return FALSE;
		sparql->current_state.expression_type = TRACKER_PROPERTY_TYPE_INTEGER;
	} else if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_HOURS)) {
		if (!helper_translate_time (sparql, TIME_FORMAT_HOURS, error))
			return FALSE;
		sparql->current_state.expression_type = TRACKER_PROPERTY_TYPE_INTEGER;
	} else if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_MINUTES)) {
		if (!helper_translate_time (sparql, TIME_FORMAT_MINUTES, error))
			return FALSE;
		sparql->current_state.expression_type = TRACKER_PROPERTY_TYPE_INTEGER;
	} else if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_SECONDS)) {
		if (!helper_translate_time (sparql, TIME_FORMAT_SECONDS, error))
			return FALSE;
		sparql->current_state.expression_type = TRACKER_PROPERTY_TYPE_DOUBLE;
	} else if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_TIMEZONE)) {
		sparql->current_state.convert_to_string = TRUE;
		_expect (sparql, RULE_TYPE_LITERAL, LITERAL_OPEN_PARENS);
		_append_string (sparql, "SparqlTimezone (");
		_call_rule (sparql, NAMED_RULE_Expression, error);
		_expect (sparql, RULE_TYPE_LITERAL, LITERAL_CLOSE_PARENS);
		_append_string (sparql, ") ");
		sparql->current_state.expression_type = TRACKER_PROPERTY_TYPE_STRING;
	} else if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_TZ)) {
		sparql->current_state.convert_to_string = TRUE;
		_expect (sparql, RULE_TYPE_LITERAL, LITERAL_OPEN_PARENS);
		_append_string (sparql, "SparqlTimezoneString (");
		_call_rule (sparql, NAMED_RULE_Expression, error);
		_expect (sparql, RULE_TYPE_LITERAL, LITERAL_CLOSE_PARENS);
		_append_string (sparql, ") ");
		sparql->current_state.expression_type = TRACKER_PROPERTY_TYPE_STRING;
	} else if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_MD5)) {
		sparql->current_state.convert_to_string = TRUE;
		_expect (sparql, RULE_TYPE_LITERAL, LITERAL_OPEN_PARENS);
		_append_string (sparql, "SparqlChecksum (");
		_call_rule (sparql, NAMED_RULE_Expression, error);
		_expect (sparql, RULE_TYPE_LITERAL, LITERAL_CLOSE_PARENS);
		_append_string (sparql, ", \"md5\") ");
		sparql->current_state.expression_type = TRACKER_PROPERTY_TYPE_STRING;
	} else if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_SHA1)) {
		sparql->current_state.convert_to_string = TRUE;
		_expect (sparql, RULE_TYPE_LITERAL, LITERAL_OPEN_PARENS);
		_append_string (sparql, "SparqlChecksum (");
		_call_rule (sparql, NAMED_RULE_Expression, error);
		_expect (sparql, RULE_TYPE_LITERAL, LITERAL_CLOSE_PARENS);
		_append_string (sparql, ", \"sha1\") ");
		sparql->current_state.expression_type = TRACKER_PROPERTY_TYPE_STRING;
	} else if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_SHA256)) {
		sparql->current_state.convert_to_string = TRUE;
		_expect (sparql, RULE_TYPE_LITERAL, LITERAL_OPEN_PARENS);
		_append_string (sparql, "SparqlChecksum (");
		_call_rule (sparql, NAMED_RULE_Expression, error);
		_expect (sparql, RULE_TYPE_LITERAL, LITERAL_CLOSE_PARENS);
		_append_string (sparql, ", \"sha256\") ");
		sparql->current_state.expression_type = TRACKER_PROPERTY_TYPE_STRING;
	} else if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_SHA384)) {
		sparql->current_state.convert_to_string = TRUE;
		_expect (sparql, RULE_TYPE_LITERAL, LITERAL_OPEN_PARENS);
		_append_string (sparql, "SparqlChecksum (");
		_call_rule (sparql, NAMED_RULE_Expression, error);
		_expect (sparql, RULE_TYPE_LITERAL, LITERAL_CLOSE_PARENS);
		_append_string (sparql, ", \"sha384\") ");
		sparql->current_state.expression_type = TRACKER_PROPERTY_TYPE_STRING;
	} else if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_SHA512)) {
		sparql->current_state.convert_to_string = TRUE;
		_expect (sparql, RULE_TYPE_LITERAL, LITERAL_OPEN_PARENS);
		_append_string (sparql, "SparqlChecksum (");
		_call_rule (sparql, NAMED_RULE_Expression, error);
		_expect (sparql, RULE_TYPE_LITERAL, LITERAL_CLOSE_PARENS);
		_append_string (sparql, ", \"sha512\") ");
		sparql->current_state.expression_type = TRACKER_PROPERTY_TYPE_STRING;
	} else if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_ISIRI) ||
		   _accept (sparql, RULE_TYPE_LITERAL, LITERAL_ISURI)) {
		_expect (sparql, RULE_TYPE_LITERAL, LITERAL_OPEN_PARENS);
		node = _skip_rule (sparql, NAMED_RULE_Expression);
		_expect (sparql, RULE_TYPE_LITERAL, LITERAL_CLOSE_PARENS);

		if (!helper_datatype (sparql, node, error))
			return FALSE;

		_append_string (sparql, "== \"" RDFS_NS "Resource\" ");
		sparql->current_state.expression_type = TRACKER_PROPERTY_TYPE_BOOLEAN;
	} else if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_ISBLANK)) {
		_expect (sparql, RULE_TYPE_LITERAL, LITERAL_OPEN_PARENS);
		node = _skip_rule (sparql, NAMED_RULE_Expression);
		_expect (sparql, RULE_TYPE_LITERAL, LITERAL_CLOSE_PARENS);

		_append_string (sparql, "CASE ");

		if (!helper_datatype (sparql, node, error))
			return FALSE;

		_append_string (sparql, "== \"" RDFS_NS "Resource\" ");
		_append_string (sparql, "WHEN 1 THEN (SELECT BlankNode FROM Resource WHERE ID = ");

		if (!_postprocess_rule (sparql, node, NULL, error))
			return FALSE;

		_append_string (sparql, ") ELSE NULL END ");
		sparql->current_state.expression_type = TRACKER_PROPERTY_TYPE_BOOLEAN;
	} else if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_ISLITERAL)) {
		_expect (sparql, RULE_TYPE_LITERAL, LITERAL_OPEN_PARENS);
		node = _skip_rule (sparql, NAMED_RULE_Expression);
		_expect (sparql, RULE_TYPE_LITERAL, LITERAL_CLOSE_PARENS);

		if (!helper_datatype (sparql, node, error))
			return FALSE;

		_append_string (sparql, "!= \"" RDFS_NS "Resource\" ");
		sparql->current_state.expression_type = TRACKER_PROPERTY_TYPE_BOOLEAN;
	} else if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_ISNUMERIC)) {
		_expect (sparql, RULE_TYPE_LITERAL, LITERAL_OPEN_PARENS);
		node = _skip_rule (sparql, NAMED_RULE_Expression);
		_expect (sparql, RULE_TYPE_LITERAL, LITERAL_CLOSE_PARENS);

		if (!helper_datatype (sparql, node, error))
			return FALSE;

		_append_string (sparql, "IN (\"" XSD_NS "integer\", \"" XSD_NS "double\")");
		sparql->current_state.expression_type = TRACKER_PROPERTY_TYPE_BOOLEAN;
	} else if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_LANGMATCHES)) {
		_append_string (sparql, "SparqlLangMatches (");
		_expect (sparql, RULE_TYPE_LITERAL, LITERAL_OPEN_PARENS);
		_call_rule (sparql, NAMED_RULE_Expression, error);
		_expect (sparql, RULE_TYPE_LITERAL, LITERAL_COMMA);
		_append_string (sparql, ", ");
		_call_rule (sparql, NAMED_RULE_Expression, error);
		_expect (sparql, RULE_TYPE_LITERAL, LITERAL_CLOSE_PARENS);
		_append_string (sparql, ") ");
		sparql->current_state.expression_type = TRACKER_PROPERTY_TYPE_BOOLEAN;
	} else if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_CONTAINS)) {
		/* contains('A','B') => 'A' GLOB '*B*' */
		sparql->current_state.convert_to_string = TRUE;
		_expect (sparql, RULE_TYPE_LITERAL, LITERAL_OPEN_PARENS);
		_append_string (sparql, "(");
		_call_rule (sparql, NAMED_RULE_Expression, error);
		_expect (sparql, RULE_TYPE_LITERAL, LITERAL_COMMA);
		_append_string (sparql, " GLOB '*' || ");
		_call_rule (sparql, NAMED_RULE_Expression, error);
		_append_string (sparql, " || '*') ");
		_expect (sparql, RULE_TYPE_LITERAL, LITERAL_CLOSE_PARENS);
		sparql->current_state.expression_type = TRACKER_PROPERTY_TYPE_BOOLEAN;
	} else if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_STRSTARTS)) {
		gchar buf[6] = { 0 };
		TrackerParserNode *node;

		/* strstarts('A','B') => 'A' BETWEEN 'B' AND 'B\u0010fffd'
		 * 0010fffd always sorts last.
		 */
		sparql->current_state.convert_to_string = TRUE;
		_append_string (sparql, "( ");
		_expect (sparql, RULE_TYPE_LITERAL, LITERAL_OPEN_PARENS);
		_call_rule (sparql, NAMED_RULE_Expression, error);
		_expect (sparql, RULE_TYPE_LITERAL, LITERAL_COMMA);
		_append_string (sparql, "BETWEEN ");

		node = sparql->current_state.node;
		_call_rule (sparql, NAMED_RULE_Expression, error);
		_append_string (sparql, "AND ");

		/* Evaluate the same expression node again */
		sparql->current_state.node = node;
		_call_rule (sparql, NAMED_RULE_Expression, error);

		g_unichar_to_utf8 (TRACKER_COLLATION_LAST_CHAR, buf);
		_append_string_printf (sparql, "|| '%s') ", buf);
		_expect (sparql, RULE_TYPE_LITERAL, LITERAL_CLOSE_PARENS);
		sparql->current_state.expression_type = TRACKER_PROPERTY_TYPE_BOOLEAN;
	} else if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_STRENDS)) {
		/* strends('A','B') => 'A' GLOB '*B' */
		sparql->current_state.convert_to_string = TRUE;
		_expect (sparql, RULE_TYPE_LITERAL, LITERAL_OPEN_PARENS);
		_append_string (sparql, "(");
		_call_rule (sparql, NAMED_RULE_Expression, error);
		_expect (sparql, RULE_TYPE_LITERAL, LITERAL_COMMA);
		_append_string (sparql, " GLOB '*' || ");
		_call_rule (sparql, NAMED_RULE_Expression, error);
		_expect (sparql, RULE_TYPE_LITERAL, LITERAL_CLOSE_PARENS);
		_append_string (sparql, ") ");
		sparql->current_state.expression_type = TRACKER_PROPERTY_TYPE_BOOLEAN;
	} else if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_STRBEFORE)) {
		sparql->current_state.convert_to_string = TRUE;
		_expect (sparql, RULE_TYPE_LITERAL, LITERAL_OPEN_PARENS);
		_append_string (sparql, "SparqlStringBefore (");
		_call_rule (sparql, NAMED_RULE_Expression, error);
		_expect (sparql, RULE_TYPE_LITERAL, LITERAL_COMMA);
		_append_string (sparql, ", ");
		_call_rule (sparql, NAMED_RULE_Expression, error);
		_expect (sparql, RULE_TYPE_LITERAL, LITERAL_CLOSE_PARENS);
		_append_string (sparql, ") ");
	} else if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_STRAFTER)) {
		sparql->current_state.convert_to_string = TRUE;
		_expect (sparql, RULE_TYPE_LITERAL, LITERAL_OPEN_PARENS);
		_append_string (sparql, "SparqlStringAfter (");
		_call_rule (sparql, NAMED_RULE_Expression, error);
		_expect (sparql, RULE_TYPE_LITERAL, LITERAL_COMMA);
		_append_string (sparql, ", ");
		_call_rule (sparql, NAMED_RULE_Expression, error);
		_expect (sparql, RULE_TYPE_LITERAL, LITERAL_CLOSE_PARENS);
		_append_string (sparql, ") ");
	} else if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_STRLANG)) {
		_append_string (sparql, "SparqlStrLang (");
		_expect (sparql, RULE_TYPE_LITERAL, LITERAL_OPEN_PARENS);
		_call_rule (sparql, NAMED_RULE_Expression, error);
		_expect (sparql, RULE_TYPE_LITERAL, LITERAL_COMMA);
		_append_string (sparql, ", ");
		_call_rule (sparql, NAMED_RULE_Expression, error);
		_expect (sparql, RULE_TYPE_LITERAL, LITERAL_CLOSE_PARENS);
		_append_string (sparql, ") ");
		sparql->current_state.expression_type = TRACKER_PROPERTY_TYPE_LANGSTRING;
	} else if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_STRDT)) {
		TrackerParserNode *expr, *node, *iri_node = NULL;
		TrackerPropertyType type;
		gchar *type_iri;
		gboolean retval = TRUE;

		_expect (sparql, RULE_TYPE_LITERAL, LITERAL_OPEN_PARENS);
		expr = _skip_rule (sparql, NAMED_RULE_Expression);
		_expect (sparql, RULE_TYPE_LITERAL, LITERAL_COMMA);
		node = _skip_rule (sparql, NAMED_RULE_Expression);
		_expect (sparql, RULE_TYPE_LITERAL, LITERAL_CLOSE_PARENS);

		if (g_node_n_nodes ((GNode *) node, G_TRAVERSE_LEAVES) == 1)
			iri_node = tracker_sparql_parser_tree_find_first (node, TRUE);

		if (!iri_node)
			_raise (PARSE, "Second argument must be IRI", "STRDT");

		type_iri = _extract_node_string (iri_node, sparql);
		type = rdf_type_to_property_type (type_iri);
		g_free (type_iri);

		switch (type) {
		case TRACKER_PROPERTY_TYPE_UNKNOWN:
		case TRACKER_PROPERTY_TYPE_STRING:
		case TRACKER_PROPERTY_TYPE_LANGSTRING:
		case TRACKER_PROPERTY_TYPE_RESOURCE:
			retval = _postprocess_rule (sparql, expr, NULL, error);
			break;
		case TRACKER_PROPERTY_TYPE_BOOLEAN:
			retval = _postprocess_rule (sparql, expr, NULL, error);
			break;
		case TRACKER_PROPERTY_TYPE_INTEGER:
			_append_string (sparql, "CAST (");
			retval = _postprocess_rule (sparql, expr, NULL, error);
			_append_string (sparql, "AS INTEGER) ");
			break;
		case TRACKER_PROPERTY_TYPE_DOUBLE:
			_append_string (sparql, "CAST (");
			retval = _postprocess_rule (sparql, expr, NULL, error);
			_append_string (sparql, "AS REAL) ");
			break;
		case TRACKER_PROPERTY_TYPE_DATE:
		case TRACKER_PROPERTY_TYPE_DATETIME:
			retval = _postprocess_rule (sparql, expr, NULL, error);
			break;
		}

		if (!retval)
			return FALSE;

		sparql->current_state.expression_type = type;
	} else if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_SAMETERM)) {
		_expect (sparql, RULE_TYPE_LITERAL, LITERAL_OPEN_PARENS);
		_append_string (sparql, " ( ");
		_call_rule (sparql, NAMED_RULE_Expression, error);
		_expect (sparql, RULE_TYPE_LITERAL, LITERAL_COMMA);
		_append_string (sparql, " = ");
		_call_rule (sparql, NAMED_RULE_Expression, error);
		_expect (sparql, RULE_TYPE_LITERAL, LITERAL_CLOSE_PARENS);
		_append_string (sparql, " ) ");
		sparql->current_state.expression_type = TRACKER_PROPERTY_TYPE_BOOLEAN;
	} else if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_IF)) {
		_expect (sparql, RULE_TYPE_LITERAL, LITERAL_OPEN_PARENS);
		_append_string (sparql, "CASE ");

		_call_rule (sparql, NAMED_RULE_Expression, error);
		_expect (sparql, RULE_TYPE_LITERAL, LITERAL_COMMA);
		_append_string (sparql, "WHEN 1 THEN ");

		_call_rule (sparql, NAMED_RULE_Expression, error);
		_expect (sparql, RULE_TYPE_LITERAL, LITERAL_COMMA);
		_append_string (sparql, "WHEN 0 THEN ");

		_call_rule (sparql, NAMED_RULE_Expression, error);
		_expect (sparql, RULE_TYPE_LITERAL, LITERAL_CLOSE_PARENS);
		_append_string (sparql, "ELSE NULL END ");
	} else if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_BOUND)) {
		_expect (sparql, RULE_TYPE_LITERAL, LITERAL_OPEN_PARENS);
		_append_string (sparql, "(");
		_call_rule (sparql, NAMED_RULE_Expression, error);
		_expect (sparql, RULE_TYPE_LITERAL, LITERAL_CLOSE_PARENS);
		_append_string (sparql, "IS NOT NULL) ");
		sparql->current_state.expression_type = TRACKER_PROPERTY_TYPE_BOOLEAN;
	} else if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_BNODE)) {
		if (_accept (sparql, RULE_TYPE_TERMINAL, TERMINAL_TYPE_NIL)) {
			_append_string (sparql, "SparqlUUID(\"urn:bnode\") ");
		} else {
			_append_string (sparql, "SparqlBNODE(");
			_expect (sparql, RULE_TYPE_LITERAL, LITERAL_OPEN_PARENS);
			_call_rule (sparql, NAMED_RULE_Expression, error);
			_expect (sparql, RULE_TYPE_LITERAL, LITERAL_CLOSE_PARENS);
			_append_string (sparql, ") ");
		}

		sparql->current_state.expression_type = TRACKER_PROPERTY_TYPE_STRING;
	} else if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_RAND)) {
		_expect (sparql, RULE_TYPE_TERMINAL, TERMINAL_TYPE_NIL);
		_append_string (sparql, "SparqlRand() ");
		sparql->current_state.expression_type = TRACKER_PROPERTY_TYPE_DOUBLE;
	} else if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_NOW)) {
		_expect (sparql, RULE_TYPE_TERMINAL, TERMINAL_TYPE_NIL);
		_append_string (sparql, "strftime('%s', 'now') ");
		sparql->current_state.expression_type = TRACKER_PROPERTY_TYPE_DATETIME;
	} else if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_UUID)) {
		_expect (sparql, RULE_TYPE_TERMINAL, TERMINAL_TYPE_NIL);
		_append_string (sparql, "SparqlUUID(\"urn:uuid\") ");
		sparql->current_state.expression_type = TRACKER_PROPERTY_TYPE_STRING;
	} else if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_STRUUID)) {
		_expect (sparql, RULE_TYPE_TERMINAL, TERMINAL_TYPE_NIL);
		_append_string (sparql, "SparqlUUID() ");
		sparql->current_state.expression_type = TRACKER_PROPERTY_TYPE_STRING;
	} else if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_CONCAT)) {
		sparql->current_state.convert_to_string = TRUE;
		old_sep = tracker_sparql_swap_current_expression_list_separator (sparql, " || ");
		_call_rule (sparql, NAMED_RULE_ExpressionList, error);
		tracker_sparql_swap_current_expression_list_separator (sparql, old_sep);
		sparql->current_state.expression_type = TRACKER_PROPERTY_TYPE_STRING;
	} else if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_COALESCE)) {
		sparql->current_state.convert_to_string = TRUE;
		_append_string (sparql, "COALESCE ");
		old_sep = tracker_sparql_swap_current_expression_list_separator (sparql, ", ");
		_call_rule (sparql, NAMED_RULE_ExpressionList, error);
		tracker_sparql_swap_current_expression_list_separator (sparql, old_sep);
		sparql->current_state.expression_type = TRACKER_PROPERTY_TYPE_STRING;
	}

	sparql->current_state.convert_to_string = convert_to_string;

	return TRUE;
}

static gboolean
translate_RegexExpression (TrackerSparql  *sparql,
                           GError        **error)
{
	TrackerStringBuilder *str, *old;

	/* RegexExpression ::= 'REGEX' '(' Expression ',' Expression ( ',' Expression )? ')'
	 */
	_expect (sparql, RULE_TYPE_LITERAL, LITERAL_REGEX);
	_expect (sparql, RULE_TYPE_LITERAL, LITERAL_OPEN_PARENS);
	_append_string (sparql, "SparqlRegex (");

	str = _append_placeholder (sparql);
	old = tracker_sparql_swap_builder (sparql, str);
	_call_rule (sparql, NAMED_RULE_Expression, error);
	convert_expression_to_string (sparql, sparql->current_state.expression_type);
	tracker_sparql_swap_builder (sparql, old);

	_expect (sparql, RULE_TYPE_LITERAL, LITERAL_COMMA);
	_append_string (sparql, ", ");

	_call_rule (sparql, NAMED_RULE_Expression, error);

	if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_COMMA)) {
		_append_string (sparql, ", ");
		_call_rule (sparql, NAMED_RULE_Expression, error);
	}

	_expect (sparql, RULE_TYPE_LITERAL, LITERAL_CLOSE_PARENS);
	_append_string (sparql, ") ");

	sparql->current_state.expression_type = TRACKER_PROPERTY_TYPE_BOOLEAN;

	return TRUE;
}

static gboolean
translate_SubstringExpression (TrackerSparql  *sparql,
                               GError        **error)
{
	/* SubstringExpression ::= 'SUBSTR' '(' Expression ',' Expression ( ',' Expression )? ')'
	 */
	_expect (sparql, RULE_TYPE_LITERAL, LITERAL_SUBSTR);
	_expect (sparql, RULE_TYPE_LITERAL, LITERAL_OPEN_PARENS);
	_append_string (sparql, "SUBSTR (");

	_call_rule (sparql, NAMED_RULE_Expression, error);

	_expect (sparql, RULE_TYPE_LITERAL, LITERAL_COMMA);
	_append_string (sparql, ", ");

	_call_rule (sparql, NAMED_RULE_Expression, error);

	if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_COMMA)) {
		_append_string (sparql, ", ");
		_call_rule (sparql, NAMED_RULE_Expression, error);
	}

	_expect (sparql, RULE_TYPE_LITERAL, LITERAL_CLOSE_PARENS);
	_append_string (sparql, ") ");
	sparql->current_state.expression_type = TRACKER_PROPERTY_TYPE_STRING;

	return TRUE;
}

static gboolean
translate_StrReplaceExpression (TrackerSparql  *sparql,
                                GError        **error)
{
	/* StrReplaceExpression ::= 'REPLACE' '(' Expression ',' Expression ',' Expression ( ',' Expression )? ')'
	 */
	_expect (sparql, RULE_TYPE_LITERAL, LITERAL_REPLACE);
	_expect (sparql, RULE_TYPE_LITERAL, LITERAL_OPEN_PARENS);
	_append_string (sparql, "SparqlReplace (");

	_call_rule (sparql, NAMED_RULE_Expression, error);

	_expect (sparql, RULE_TYPE_LITERAL, LITERAL_COMMA);
	_append_string (sparql, ", ");

	_call_rule (sparql, NAMED_RULE_Expression, error);

	_expect (sparql, RULE_TYPE_LITERAL, LITERAL_COMMA);
	_append_string (sparql, ", ");

	_call_rule (sparql, NAMED_RULE_Expression, error);

	if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_COMMA)) {
		_append_string (sparql, ", ");
		_call_rule (sparql, NAMED_RULE_Expression, error);
	}

	_expect (sparql, RULE_TYPE_LITERAL, LITERAL_CLOSE_PARENS);
	_append_string (sparql, ") ");
	sparql->current_state.expression_type = TRACKER_PROPERTY_TYPE_STRING;

	return TRUE;
}

static gboolean
translate_ExistsFunc (TrackerSparql  *sparql,
		      GError        **error)
{
	TrackerContext *context;

	/* ExistsFunc ::= 'EXISTS' GroupGraphPattern
	 */
	_expect (sparql, RULE_TYPE_LITERAL, LITERAL_EXISTS);
	_append_string (sparql, "EXISTS (");

	context = tracker_select_context_new ();
	tracker_sparql_push_context (sparql, context);

	_call_rule (sparql, NAMED_RULE_GroupGraphPattern, error);

	tracker_sparql_pop_context (sparql, FALSE);

	if (!_check_undefined_variables (sparql, TRACKER_SELECT_CONTEXT (context), error))
		return FALSE;

	_append_string (sparql, ") ");

	sparql->current_state.expression_type = TRACKER_PROPERTY_TYPE_BOOLEAN;

	return TRUE;
}

static gboolean
translate_NotExistsFunc (TrackerSparql  *sparql,
                         GError        **error)
{
	/* NotExistsFunc ::= 'NOT' 'EXISTS' GroupGraphPattern
	 */
	_expect (sparql, RULE_TYPE_LITERAL, LITERAL_NOT);
	_append_string (sparql, "NOT ");

	return translate_ExistsFunc (sparql, error);
}

static gboolean
translate_Aggregate (TrackerSparql  *sparql,
                     GError        **error)
{
	/* Aggregate ::= 'COUNT' '(' 'DISTINCT'? ( '*' | Expression ) ')'
	 *               | 'SUM' '(' 'DISTINCT'? Expression ')'
	 *               | 'MIN' '(' 'DISTINCT'? Expression ')'
	 *               | 'MAX' '(' 'DISTINCT'? Expression ')'
	 *               | 'AVG' '(' 'DISTINCT'? Expression ')'
	 *               | 'SAMPLE' '(' 'DISTINCT'? Expression ')'
	 *               | 'GROUP_CONCAT' '(' 'DISTINCT'? Expression ( ';' 'SEPARATOR' '=' String )? ')'
	 */
	if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_COUNT) ||
	    _accept (sparql, RULE_TYPE_LITERAL, LITERAL_SUM) ||
	    _accept (sparql, RULE_TYPE_LITERAL, LITERAL_MIN) ||
	    _accept (sparql, RULE_TYPE_LITERAL, LITERAL_MAX) ||
	    _accept (sparql, RULE_TYPE_LITERAL, LITERAL_AVG)) {
		gchar *last_string = _dup_last_string (sparql);

		_expect (sparql, RULE_TYPE_LITERAL, LITERAL_OPEN_PARENS);
		/* Luckily the SQL literals are the same than Sparql's */
		_append_string (sparql, last_string);
		_append_string (sparql, "(");
		g_free (last_string);

		if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_DISTINCT))
			_append_string (sparql, "DISTINCT ");

		if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_GLOB)) {
			_append_string (sparql, "* ");
		} else if (_check_in_rule (sparql, NAMED_RULE_Expression)) {
			_call_rule (sparql, NAMED_RULE_Expression, error);
		}

		_expect (sparql, RULE_TYPE_LITERAL, LITERAL_CLOSE_PARENS);
		_append_string (sparql, ") ");

		sparql->current_state.expression_type = TRACKER_PROPERTY_TYPE_INTEGER;
	} else if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_GROUP_CONCAT)) {
		TrackerStringBuilder *str, *old;
		gboolean separator = FALSE;

		_expect (sparql, RULE_TYPE_LITERAL, LITERAL_OPEN_PARENS);
		_append_string (sparql, "GROUP_CONCAT(");

		if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_DISTINCT))
			_append_string (sparql, "DISTINCT ");

		str = _append_placeholder (sparql);
		old = tracker_sparql_swap_builder (sparql, str);

		_call_rule (sparql, NAMED_RULE_Expression, error);

		if (sparql->current_state.expression_type == TRACKER_PROPERTY_TYPE_RESOURCE)
			convert_expression_to_string (sparql, sparql->current_state.expression_type);

		tracker_sparql_swap_builder (sparql, old);

		if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_SEMICOLON)) {
			_expect (sparql, RULE_TYPE_LITERAL, LITERAL_SEPARATOR);
			_expect (sparql, RULE_TYPE_LITERAL, LITERAL_OP_EQ);
			separator = TRUE;
		} else if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_COMMA)) {
			separator = TRUE;
		}

		if (separator) {
			TrackerBinding *binding;

			_append_string (sparql, ", ");
			_call_rule (sparql, NAMED_RULE_String, error);

			binding = _convert_terminal (sparql);
			tracker_select_context_add_literal_binding (TRACKER_SELECT_CONTEXT (sparql->context),
								    TRACKER_LITERAL_BINDING (binding));
			_append_literal_sql (sparql, TRACKER_LITERAL_BINDING (binding));
			g_object_unref (binding);
		}

		_expect (sparql, RULE_TYPE_LITERAL, LITERAL_CLOSE_PARENS);
		_append_string (sparql, ") ");
		sparql->current_state.expression_type = TRACKER_PROPERTY_TYPE_STRING;
	} else if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_SAMPLE)) {
		_expect (sparql, RULE_TYPE_LITERAL, LITERAL_OPEN_PARENS);

		if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_DISTINCT))
			_append_string (sparql, "DISTINCT ");

		_call_rule (sparql, NAMED_RULE_Expression, error);
		_expect (sparql, RULE_TYPE_LITERAL, LITERAL_CLOSE_PARENS);
	} else {
		g_assert_not_reached ();
	}

	return TRUE;
}

static gboolean
translate_RDFLiteral (TrackerSparql  *sparql,
                      GError        **error)
{
	TrackerParserNode *node;
	TrackerBinding *binding;
	gchar *str, *langtag = NULL, *cast = NULL;
	gboolean is_parameter;
	const TrackerGrammarRule *rule;
	TrackerPropertyType type;

	/* RDFLiteral ::= String ( LANGTAG | ( '^^' iri ) )?
	 */
	_call_rule (sparql, NAMED_RULE_String, error);
	node = sparql->current_state.prev_node;
	str = _extract_node_string (node, sparql);
	rule = tracker_parser_node_get_rule (node);
	is_parameter = tracker_grammar_rule_is_a (rule, RULE_TYPE_TERMINAL,
	                                          TERMINAL_TYPE_PARAMETERIZED_VAR);

	if (_accept (sparql, RULE_TYPE_TERMINAL, TERMINAL_TYPE_LANGTAG)) {
		langtag = _dup_last_string (sparql);
		sparql->current_state.expression_type = TRACKER_PROPERTY_TYPE_LANGSTRING;
	} else if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_DOUBLE_CIRCUMFLEX)) {
		_call_rule (sparql, NAMED_RULE_iri, error);
		cast = _dup_last_string (sparql);
	}

	if (is_parameter && (langtag || cast)) {
		g_free (str);
		g_free (langtag);
		g_free (cast);
		_raise (PARSE, "Parameter cannot have LANGTAG/^^ modifiers", "RDFLiteral");
	}

	if (is_parameter) {
		binding = tracker_parameter_binding_new (str, NULL);
	} else {
		GString *langstr;
		GBytes *bytes;

		langstr = g_string_new (str);

		if (langtag) {
			g_string_append_c (langstr, '\0');
			g_string_append_printf (langstr, "%s", &langtag[1]);
		}

		bytes = g_bytes_new_take (langstr->str,
		                          langstr->len + 1);
		g_string_free (langstr, FALSE);

		binding = tracker_literal_binding_new (bytes, NULL);
		g_bytes_unref (bytes);
	}

	if (cast) {
		type = rdf_type_to_property_type (cast);
	} else if (langtag) {
		type = TRACKER_PROPERTY_TYPE_LANGSTRING;
	} else {
		type = TRACKER_PROPERTY_TYPE_STRING;
	}

	sparql->current_state.expression_type = type;
	tracker_binding_set_data_type (binding, type);

	if (sparql->current_state.type == TRACKER_SPARQL_TYPE_SELECT ||
	    sparql->current_state.type == TRACKER_SPARQL_TYPE_CONSTRUCT) {
		tracker_select_context_add_literal_binding (TRACKER_SELECT_CONTEXT (sparql->context),
		                                            TRACKER_LITERAL_BINDING (binding));
	}

	if (sparql->current_state.token) {
		if (is_parameter) {
			tracker_token_parameter_init (sparql->current_state.token,
			                              TRACKER_PARAMETER_BINDING (binding)->name);
		} else {
			gconstpointer data;
			gsize len;

			data = g_bytes_get_data (TRACKER_LITERAL_BINDING (binding)->bytes, &len);
			tracker_token_literal_init (sparql->current_state.token,
			                            data, len);
		}
	}

	g_object_unref (binding);
	g_free (langtag);
	g_free (cast);
	g_free (str);

	return TRUE;
}

static gboolean
translate_NumericLiteral (TrackerSparql  *sparql,
                          GError        **error)
{
	TrackerGrammarNamedRule rule;

	/* NumericLiteral ::= NumericLiteralUnsigned | NumericLiteralPositive | NumericLiteralNegative
	 */
	rule = _current_rule (sparql);

	switch (rule) {
	case NAMED_RULE_NumericLiteralUnsigned:
	case NAMED_RULE_NumericLiteralPositive:
	case NAMED_RULE_NumericLiteralNegative:
		_call_rule (sparql, rule, error);
		break;
	default:
		g_assert_not_reached ();
	}

	return TRUE;
}

static gboolean
translate_NumericLiteralUnsigned (TrackerSparql  *sparql,
                                  GError        **error)
{
	/* NumericLiteralUnsigned ::= INTEGER | DECIMAL | DOUBLE
	 *
	 * TRACKER EXTENSION:
	 * The terminal PARAMETERIZED_VAR is additionally accepted
	 */
	if (_accept (sparql, RULE_TYPE_TERMINAL, TERMINAL_TYPE_INTEGER)) {
		sparql->current_state.expression_type = TRACKER_PROPERTY_TYPE_INTEGER;
	} else if (_accept (sparql, RULE_TYPE_TERMINAL, TERMINAL_TYPE_DOUBLE) ||
	           _accept (sparql, RULE_TYPE_TERMINAL, TERMINAL_TYPE_DECIMAL)) {
		sparql->current_state.expression_type = TRACKER_PROPERTY_TYPE_DOUBLE;
	} else if (_accept (sparql, RULE_TYPE_TERMINAL, TERMINAL_TYPE_PARAMETERIZED_VAR)) {
		sparql->current_state.expression_type = TRACKER_PROPERTY_TYPE_UNKNOWN;
	} else {
		g_assert_not_reached ();
	}

	return TRUE;
}

static gboolean
translate_NumericLiteralPositive (TrackerSparql  *sparql,
                                  GError        **error)
{
	/* NumericLiteralPositive ::= INTEGER_POSITIVE | DECIMAL_POSITIVE | DOUBLE_POSITIVE
	 *
	 * TRACKER EXTENSION:
	 * The terminal PARAMETERIZED_VAR is additionally accepted
	 */
	if (_accept (sparql, RULE_TYPE_TERMINAL, TERMINAL_TYPE_INTEGER_POSITIVE)) {
		sparql->current_state.expression_type = TRACKER_PROPERTY_TYPE_INTEGER;
	} else if (_accept (sparql, RULE_TYPE_TERMINAL, TERMINAL_TYPE_DECIMAL_POSITIVE) ||
	           _accept (sparql, RULE_TYPE_TERMINAL, TERMINAL_TYPE_DOUBLE_POSITIVE)) {
		sparql->current_state.expression_type = TRACKER_PROPERTY_TYPE_DOUBLE;
	} else if (_accept (sparql, RULE_TYPE_TERMINAL, TERMINAL_TYPE_PARAMETERIZED_VAR)) {
		sparql->current_state.expression_type = TRACKER_PROPERTY_TYPE_UNKNOWN;
	} else {
		g_assert_not_reached ();
	}

	return TRUE;
}

static gboolean
translate_NumericLiteralNegative (TrackerSparql  *sparql,
                                  GError        **error)
{
	/* NumericLiteralNegative ::= INTEGER_NEGATIVE | DECIMAL_NEGATIVE | DOUBLE_NEGATIVE
	 *
	 * TRACKER EXTENSION:
	 * The terminal PARAMETERIZED_VAR is additionally accepted
	 */
	if (_accept (sparql, RULE_TYPE_TERMINAL, TERMINAL_TYPE_INTEGER_NEGATIVE)) {
		sparql->current_state.expression_type = TRACKER_PROPERTY_TYPE_INTEGER;
	} else if (_accept (sparql, RULE_TYPE_TERMINAL, TERMINAL_TYPE_DECIMAL_NEGATIVE) ||
	           _accept (sparql, RULE_TYPE_TERMINAL, TERMINAL_TYPE_DOUBLE_NEGATIVE)) {
		sparql->current_state.expression_type = TRACKER_PROPERTY_TYPE_DOUBLE;
	} else if (_accept (sparql, RULE_TYPE_TERMINAL, TERMINAL_TYPE_PARAMETERIZED_VAR)) {
		sparql->current_state.expression_type = TRACKER_PROPERTY_TYPE_UNKNOWN;
	} else {
		g_assert_not_reached ();
	}

	return TRUE;
}

static gboolean
translate_BooleanLiteral (TrackerSparql  *sparql,
                          GError        **error)
{
	/* BooleanLiteral ::= 'true' | 'false'
	 *
	 * TRACKER EXTENSION:
	 * The terminal PARAMETERIZED_VAR is additionally accepted
	 */
	if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_TRUE) ||
	    _accept (sparql, RULE_TYPE_LITERAL, LITERAL_FALSE)) {
		sparql->current_state.expression_type = TRACKER_PROPERTY_TYPE_BOOLEAN;
		return TRUE;
	} else if (_accept (sparql, RULE_TYPE_TERMINAL, TERMINAL_TYPE_PARAMETERIZED_VAR)) {
		sparql->current_state.expression_type = TRACKER_PROPERTY_TYPE_UNKNOWN;
	} else {
		g_assert_not_reached ();
	}

	return TRUE;
}

static gboolean
translate_String (TrackerSparql  *sparql,
                  GError        **error)
{
	/* String ::= STRING_LITERAL1 | STRING_LITERAL2 | STRING_LITERAL_LONG1 | STRING_LITERAL_LONG2
	 *
	 * TRACKER EXTENSION:
	 * The terminal PARAMETERIZED_VAR is additionally accepted
	 */
	if (_accept (sparql, RULE_TYPE_TERMINAL, TERMINAL_TYPE_STRING_LITERAL1) ||
	    _accept (sparql, RULE_TYPE_TERMINAL, TERMINAL_TYPE_STRING_LITERAL2) ||
	    _accept (sparql, RULE_TYPE_TERMINAL, TERMINAL_TYPE_STRING_LITERAL_LONG1) ||
	    _accept (sparql, RULE_TYPE_TERMINAL, TERMINAL_TYPE_STRING_LITERAL_LONG2)) {
		sparql->current_state.expression_type = TRACKER_PROPERTY_TYPE_STRING;
		return TRUE;
	} else if (_accept (sparql, RULE_TYPE_TERMINAL, TERMINAL_TYPE_PARAMETERIZED_VAR)) {
		sparql->current_state.expression_type = TRACKER_PROPERTY_TYPE_UNKNOWN;
	} else {
		g_assert_not_reached ();
	}

	return TRUE;
}

static gboolean
translate_iri (TrackerSparql  *sparql,
               GError        **error)
{
	/* iri ::= IRIREF | PrefixedName
	 */
	if (_check_in_rule (sparql, NAMED_RULE_PrefixedName)) {
		_call_rule (sparql, NAMED_RULE_PrefixedName, error);
	} else {
		_expect (sparql, RULE_TYPE_TERMINAL, TERMINAL_TYPE_IRIREF);
	}

	sparql->current_state.expression_type = TRACKER_PROPERTY_TYPE_RESOURCE;

	return TRUE;
}

static gboolean
translate_PrefixedName (TrackerSparql  *sparql,
                        GError        **error)
{
	/* PrefixedName ::= PNAME_LN | PNAME_NS
	 */
	if (_accept (sparql, RULE_TYPE_TERMINAL, TERMINAL_TYPE_PNAME_LN) ||
	    _accept (sparql, RULE_TYPE_TERMINAL, TERMINAL_TYPE_PNAME_NS)) {
		return TRUE;
	} else {
		g_assert_not_reached ();
	}

	return TRUE;
}

static gboolean
translate_BlankNode (TrackerSparql  *sparql,
                     GError        **error)
{
	TrackerDBInterface *iface;
	gchar *bnode_id;
	TrackerVariable *var;

	/* BlankNode ::= BLANK_NODE_LABEL | ANON
	 */
	g_assert (sparql->current_state.token != NULL);

	iface = tracker_data_manager_get_writable_db_interface (sparql->data_manager);

        if (sparql->current_state.type != TRACKER_SPARQL_TYPE_SELECT &&
	    sparql->current_state.type != TRACKER_SPARQL_TYPE_CONSTRUCT) {
	        if (_accept (sparql, RULE_TYPE_TERMINAL, TERMINAL_TYPE_ANON)) {
		        bnode_id = tracker_data_query_unused_uuid (sparql->data_manager, iface);
		        tracker_token_literal_init (sparql->current_state.token, bnode_id, -1);
		        g_free (bnode_id);
	        } else if (_accept (sparql, RULE_TYPE_TERMINAL, TERMINAL_TYPE_BLANK_NODE_LABEL)) {
		        gchar *str;

		        str = _dup_last_string (sparql);

		        if (sparql->current_state.blank_node_map) {
			        bnode_id = g_hash_table_lookup (sparql->current_state.blank_node_map, str);

			        if (!bnode_id) {
				        bnode_id = tracker_data_query_unused_uuid (sparql->data_manager, iface);
				        g_hash_table_insert (sparql->current_state.blank_node_map,
				                             g_strdup (str), bnode_id);
			        }

			        if (sparql->blank_nodes &&
			            sparql->current_state.update_blank_nodes &&
			            !g_hash_table_contains (sparql->current_state.update_blank_nodes, str)) {
				        g_hash_table_add (sparql->current_state.update_blank_nodes, str);
				        g_variant_builder_add (sparql->blank_nodes, "{ss}", str, bnode_id);
			        }

			        tracker_token_literal_init (sparql->current_state.token, bnode_id, -1);
		        } else {
			        tracker_token_literal_init (sparql->current_state.token, str, -1);
		        }

		        g_free (str);
	        } else {
		        g_assert_not_reached ();
	        }
        } else {
	        if (_accept (sparql, RULE_TYPE_TERMINAL, TERMINAL_TYPE_ANON)) {
		        var = tracker_select_context_add_generated_variable (TRACKER_SELECT_CONTEXT (sparql->context));
	        } else if (_accept (sparql, RULE_TYPE_TERMINAL, TERMINAL_TYPE_BLANK_NODE_LABEL)) {
		        gchar *str, *var_name;

		        str = _dup_last_string (sparql);
		        var_name = g_strdup_printf ("BlankNode:%s", str);
		        var = _ensure_variable (sparql, var_name);
		        g_free (var_name);
		        g_free (str);
	        } else {
		        g_assert_not_reached ();
	        }

	        tracker_token_variable_init (sparql->current_state.token, var);
        }

	return TRUE;
}

const RuleTranslationFunc rule_translation_funcs[N_NAMED_RULES] = {
	NULL, /* Grammar parser entry points */
	NULL,
	translate_Query,
	translate_Update,
	translate_SelectClause,
	translate_Prologue,
	translate_BaseDecl,
	translate_PrefixDecl,
	translate_ConstraintDecl,
	translate_SelectQuery,
	translate_SubSelect,
	translate_ConstructQuery,
	translate_DescribeQuery,
	translate_AskQuery,
	translate_DatasetClause,
	translate_DefaultGraphClause,
	translate_NamedGraphClause,
	translate_SourceSelector,
	translate_WhereClause,
	translate_SolutionModifier,
	translate_GroupClause,
	translate_GroupCondition,
	translate_HavingClause,
	translate_HavingCondition,
	translate_OrderClause,
	translate_OrderCondition,
	translate_LimitOffsetClauses,
	translate_LimitClause,
	translate_OffsetClause,
	translate_ValuesClause,
	translate_Update1,
	translate_Load,
	translate_Clear,
	translate_Drop,
	translate_Create,
	translate_Add,
	translate_Move,
	translate_Copy,
	translate_InsertData,
	translate_DeleteData,
	translate_DeleteWhere,
	translate_Modify,
	translate_DeleteClause,
	translate_InsertClause,
	translate_UsingClause,
	translate_GraphOrDefault,
	translate_GraphRefAll,
	translate_GraphRef,
	translate_QuadPattern,
	translate_QuadData,
	translate_Quads,
	translate_QuadsNotTriples,
	translate_TriplesTemplate,
	translate_GroupGraphPatternSub,
	translate_TriplesBlock,
	translate_GraphPatternNotTriples,
	translate_OptionalGraphPattern,
	translate_GraphGraphPattern,
	translate_ServiceGraphPattern,
	translate_Bind,
	translate_InlineData,
	translate_DataBlock,
	translate_InlineDataOneVar,
	translate_InlineDataFull,
	translate_DataBlockValue,
	translate_MinusGraphPattern,
	translate_GroupOrUnionGraphPattern,
	translate_Filter,
	translate_Constraint,
	translate_FunctionCall,
	translate_ArgList,
	translate_ExpressionList,
	translate_ConstructTemplate,
	translate_ConstructTriples,
	translate_TriplesSameSubject,
	translate_GroupGraphPattern,
	translate_PropertyList,
	translate_PropertyListNotEmpty,
	translate_Verb,
	translate_ObjectList,
	translate_Object,
	translate_TriplesSameSubjectPath,
	translate_PropertyListPath,
	translate_PropertyListPathNotEmpty,
	translate_VerbPath,
	translate_VerbSimple,
	translate_ObjectListPath,
	translate_ObjectPath,
	translate_Path,
	translate_PathAlternative,
	translate_PathSequence,
	translate_PathEltOrInverse,
	translate_PathElt,
	translate_PathMod,
	translate_PathPrimary,
	translate_PathNegatedPropertySet,
	translate_PathOneInPropertySet,
	translate_Integer,
	translate_TriplesNode,
	translate_BlankNodePropertyList,
	translate_TriplesNodePath,
	translate_BlankNodePropertyListPath,
	translate_Collection,
	translate_CollectionPath,
	translate_GraphNode,
	translate_GraphNodePath,
	translate_VarOrTerm,
	translate_VarOrIri,
	translate_Var,
	translate_GraphTerm,
	translate_Expression,
	translate_ConditionalOrExpression,
	translate_ConditionalAndExpression,
	translate_ValueLogical,
	translate_RelationalExpression,
	translate_NumericExpression,
	translate_AdditiveExpression,
	translate_MultiplicativeExpression,
	translate_UnaryExpression,
	translate_PrimaryExpression,
	translate_iriOrFunction,
	translate_BrackettedExpression,
	translate_BuiltInCall,
	translate_RegexExpression,
	translate_SubstringExpression,
	translate_StrReplaceExpression,
	translate_ExistsFunc,
	translate_NotExistsFunc,
	translate_Aggregate,
	translate_RDFLiteral,
	translate_NumericLiteral,
	translate_NumericLiteralUnsigned,
	translate_NumericLiteralPositive,
	translate_NumericLiteralNegative,
	translate_BooleanLiteral,
	translate_String,
	translate_iri,
	translate_PrefixedName,
	translate_BlankNode,
};

static inline gboolean
_call_rule_func (TrackerSparql            *sparql,
                 TrackerGrammarNamedRule   named_rule,
                 GError                  **error)
{
	TrackerParserNode *parser_node = sparql->current_state.node;
	const TrackerGrammarRule *rule;
	GError *inner_error = NULL;
	gboolean retval;

	g_assert (named_rule < N_NAMED_RULES);
	g_assert (rule_translation_funcs[named_rule]);

	/* Empty rules pass */
	if (!parser_node ||
	    !tracker_parser_node_get_extents (parser_node, NULL, NULL))
		return TRUE;

	rule = tracker_parser_node_get_rule (parser_node);

	if (!tracker_grammar_rule_is_a (rule, RULE_TYPE_RULE, named_rule))
		return TRUE;

	tracker_sparql_iter_next (sparql);

	retval = rule_translation_funcs[named_rule] (sparql, &inner_error);

	if (!retval) {
		if (!inner_error) {
			g_error ("Translation rule '%s' returns FALSE, but no error",
			         rule->string);
		}

		g_assert (inner_error != NULL);
		g_propagate_error (error, inner_error);
	}

	return retval;
}

static void
tracker_sparql_class_init (TrackerSparqlClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = tracker_sparql_finalize;
}

static void
tracker_sparql_init (TrackerSparql *sparql)
{
	sparql->prefix_map = g_hash_table_new_full (g_str_hash, g_str_equal,
	                                            g_free, g_free);
	sparql->cached_bindings = g_hash_table_new_full (g_str_hash, g_str_equal,
							 g_free, g_object_unref);
	sparql->parameters = g_hash_table_new_full (g_str_hash, g_str_equal,
	                                            g_free, g_object_unref);
	sparql->var_names = g_ptr_array_new_with_free_func (g_free);
	sparql->var_types = g_array_new (FALSE, FALSE, sizeof (TrackerPropertyType));
	sparql->anon_graphs = g_ptr_array_new_with_free_func (g_free);
	sparql->named_graphs = g_ptr_array_new_with_free_func (g_free);
	sparql->cacheable = TRUE;
}

TrackerSparql*
tracker_sparql_new (TrackerDataManager *manager,
                    const gchar        *query)
{
	TrackerNodeTree *tree;
	TrackerSparql *sparql;

	g_return_val_if_fail (TRACKER_IS_DATA_MANAGER (manager), NULL);
	g_return_val_if_fail (query != NULL, NULL);

	sparql = g_object_new (TRACKER_TYPE_SPARQL, NULL);
	sparql->data_manager = g_object_ref (manager);
	sparql->sparql = tracker_unescape_unichars (query, -1);

	tree = tracker_sparql_parse_query (sparql->sparql, -1, NULL,
					   &sparql->parser_error);
	if (tree) {
		sparql->tree = tree;
		sparql->current_state.node = tracker_node_tree_get_root (sparql->tree);
		tracker_sparql_init_string_builder (sparql);
	}

	return sparql;
}

static TrackerDBStatement *
prepare_query (TrackerSparql         *sparql,
               TrackerDBInterface    *iface,
               TrackerStringBuilder  *str,
               GPtrArray             *literals,
	       GHashTable            *parameters,
               gboolean               cached,
               GError               **error)
{
	TrackerDBStatement *stmt;
	gchar *query;
	guint i;

	query = tracker_string_builder_to_string (str);
	stmt = tracker_db_interface_create_statement (iface,
	                                              cached ?
	                                              TRACKER_DB_STATEMENT_CACHE_TYPE_SELECT :
	                                              TRACKER_DB_STATEMENT_CACHE_TYPE_NONE,
	                                              error, "%s", query);
	g_free (query);

	if (!stmt || !literals)
		return stmt;

	for (i = 0; i < literals->len; i++) {
		TrackerLiteralBinding *binding;
		TrackerPropertyType prop_type;

		binding = g_ptr_array_index (literals, i);
		prop_type = TRACKER_BINDING (binding)->data_type;

		if (TRACKER_IS_PARAMETER_BINDING (binding)) {
			const gchar *name;
			GValue *value = NULL;

			name = TRACKER_PARAMETER_BINDING (binding)->name;

			if (parameters)
				value = g_hash_table_lookup (parameters, name);

			if (value) {
				tracker_db_statement_bind_value (stmt, i, value);
			} else {
				g_set_error (error, TRACKER_SPARQL_ERROR,
					     TRACKER_SPARQL_ERROR_TYPE,
					     "Parameter '%s' has no given value", name);
				g_object_unref (stmt);
				return NULL;
			}
		} else if (prop_type == TRACKER_PROPERTY_TYPE_BOOLEAN) {
			if (g_str_equal (binding->literal, "1") ||
			    g_ascii_strcasecmp (binding->literal, "true") == 0) {
				tracker_db_statement_bind_int (stmt, i, 1);
			} else if (g_str_equal (binding->literal, "0") ||
			           g_ascii_strcasecmp (binding->literal, "false") == 0) {
				tracker_db_statement_bind_int (stmt, i, 0);
			} else {
				g_set_error (error, TRACKER_SPARQL_ERROR,
					     TRACKER_SPARQL_ERROR_TYPE,
					     "'%s' is not a valid boolean",
				             binding->literal);
				g_object_unref (stmt);
				return NULL;
			}
		} else if (prop_type == TRACKER_PROPERTY_TYPE_DATE) {
			gchar *full_str;
			gdouble datetime;

			full_str = g_strdup_printf ("%sT00:00:00Z", binding->literal);
			datetime = tracker_string_to_date (full_str, NULL, error);
			g_free (full_str);

			if (datetime < 0) {
				g_object_unref (stmt);
				return NULL;
			}

			tracker_db_statement_bind_int (stmt, i, (int) datetime);
		} else if (prop_type == TRACKER_PROPERTY_TYPE_DATETIME) {
			gdouble datetime;

			datetime = tracker_string_to_date (binding->literal, NULL, error);
			if (datetime < 0) {
				g_object_unref (stmt);
				return NULL;
			}

			tracker_db_statement_bind_double (stmt, i, datetime);
		} else if (prop_type == TRACKER_PROPERTY_TYPE_INTEGER) {
			tracker_db_statement_bind_int (stmt, i, atoi (binding->literal));
		} else if (prop_type == TRACKER_PROPERTY_TYPE_LANGSTRING &&
			   g_bytes_get_size (binding->bytes) > strlen (binding->literal) + 1) {
			tracker_db_statement_bind_bytes (stmt, i, binding->bytes);
		} else {
			tracker_db_statement_bind_text (stmt, i, binding->literal);
		}
	}

	return stmt;
}

TrackerSparqlCursor *
tracker_sparql_execute_cursor (TrackerSparql  *sparql,
                               GHashTable     *parameters,
                               GError        **error)
{
	TrackerDBStatement *stmt;
	TrackerDBInterface *iface;
	TrackerDBCursor *cursor;
	TrackerPropertyType *types;
	const gchar * const *names;
	guint n_types, n_names;

#ifdef G_ENABLE_DEBUG
	if (TRACKER_DEBUG_CHECK (SPARQL)) {
		gchar *query_to_print;

		query_to_print = g_strdup (sparql->sparql);
		g_strdelimit (query_to_print, "\n", ' ');
		g_message ("[SPARQL] %s", query_to_print);
		g_free (query_to_print);
	}
#endif

	if (sparql->parser_error) {
		g_propagate_error (error, sparql->parser_error);
		return NULL;
	}

	if (!_call_rule_func (sparql, NAMED_RULE_Query, error))
		return NULL;

	iface = tracker_data_manager_get_db_interface (sparql->data_manager);
	stmt = prepare_query (sparql, iface, sparql->sql,
	                      TRACKER_SELECT_CONTEXT (sparql->context)->literal_bindings,
			      parameters,
	                      sparql->cacheable,
	                      error);
	if (!stmt)
		return NULL;

	types = (TrackerPropertyType *) sparql->var_types->data;
	n_types = sparql->var_types->len;
	names = (const gchar * const *) sparql->var_names->pdata;
	n_names = sparql->var_names->len;

	cursor = tracker_db_statement_start_sparql_cursor (stmt,
							   types, n_types,
							   names, n_names,
							   error);
	g_object_unref (stmt);

	return TRACKER_SPARQL_CURSOR (cursor);
}

TrackerSparql *
tracker_sparql_new_update (TrackerDataManager *manager,
                           const gchar        *query)
{
	TrackerNodeTree *tree;
	TrackerSparql *sparql;
	gsize len;

	g_return_val_if_fail (TRACKER_IS_DATA_MANAGER (manager), NULL);
	g_return_val_if_fail (query != NULL, NULL);

	sparql = g_object_new (TRACKER_TYPE_SPARQL, NULL);
	sparql->data_manager = g_object_ref (manager);
	sparql->sparql = tracker_unescape_unichars (query, -1);

	tree = tracker_sparql_parse_update (sparql->sparql, -1, &len,
	                                    &sparql->parser_error);

	if (tree && !sparql->parser_error && query[len] != '\0') {
		tracker_node_tree_free (tree);
		tree = NULL;
		g_set_error (&sparql->parser_error,
			     TRACKER_SPARQL_ERROR,
			     TRACKER_SPARQL_ERROR_PARSE,
			     "Parser error at byte %ld: Expected NIL character",
			     len);
	}

	if (tree) {
		sparql->tree = tree;
		sparql->current_state.node = tracker_node_tree_get_root (sparql->tree);
		tracker_sparql_init_string_builder (sparql);
	}

	return sparql;
}

GVariant *
tracker_sparql_execute_update (TrackerSparql  *sparql,
                               gboolean        blank,
                               GError        **error)
{
	if (sparql->parser_error) {
		g_propagate_error (error, sparql->parser_error);
		return NULL;
	}

	if (blank)
		sparql->blank_nodes = g_variant_builder_new (G_VARIANT_TYPE ("aaa{ss}"));

	if (!_call_rule_func (sparql, NAMED_RULE_Update, error))
		return NULL;

	if (sparql->blank_nodes) {
		GVariant *blank_nodes;

		blank_nodes = g_variant_builder_end (sparql->blank_nodes);
		return g_variant_ref_sink (blank_nodes);
	}

	return NULL;
}
