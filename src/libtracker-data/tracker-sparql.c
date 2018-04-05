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

#define TRACKER_NS "http://www.tracker-project.org/ontologies/tracker#"
#define RDF_NS "http://www.w3.org/1999/02/22-rdf-syntax-ns#"
#define RDFS_NS "http://www.w3.org/2000/01/rdf-schema#"
#define FTS_NS "http://www.tracker-project.org/ontologies/fts#"
#define XSD_NS "http://www.w3.org/2001/XMLSchema#"
#define FN_NS "http://www.w3.org/2005/xpath-functions#"

enum {
	TIME_FORMAT_SECONDS,
	TIME_FORMAT_MINUTES,
	TIME_FORMAT_HOURS
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
static inline TrackerVariable * _ensure_variable (TrackerSparql *sparql,
                                                  const gchar   *name);

#define _raise(v,s,sub)   \
	G_STMT_START { \
	g_set_error (error, TRACKER_SPARQL_ERROR, \
	             TRACKER_SPARQL_ERROR_##v, \
	             s " '%s'", sub); \
	return FALSE; \
	} G_STMT_END

#define _unimplemented(s) _raise(UNSUPPORTED, "Unsupported syntax", s)

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

struct _TrackerSparql
{
	GObject parent_instance;
	TrackerDataManager *data_manager;
	const gchar *sparql;

	TrackerNodeTree *tree;
	GError *parser_error;

	TrackerContext *context;
	TrackerStringBuilder *sql;

	GHashTable *prefix_map;
	GList *filter_clauses;

	GPtrArray *var_names;
	GArray *var_types;

	struct {
		TrackerContext *context;
		TrackerContext *select_context;
		TrackerStringBuilder *sql;
		TrackerParserNode *node;
		TrackerParserNode *prev_node;

		TrackerToken graph;
		TrackerToken subject;
		TrackerToken predicate;
		TrackerToken object;

		TrackerToken *token;

		const gchar *expression_list_separator;
		TrackerPropertyType expression_type;
	} current_state;
};

G_DEFINE_TYPE (TrackerSparql, tracker_sparql, G_TYPE_OBJECT)

static void
tracker_sparql_finalize (GObject *object)
{
	TrackerSparql *sparql = TRACKER_SPARQL (object);

	g_object_unref (sparql->data_manager);
	g_hash_table_destroy (sparql->prefix_map);

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

	g_ptr_array_unref (sparql->var_names);
	g_array_unref (sparql->var_types);

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

		if (!expanded_ns)
			return NULL;
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

static inline void
_append_literal_sql (TrackerSparql         *sparql,
                     TrackerLiteralBinding *binding)
{
	guint idx;

	idx = tracker_select_context_get_literal_binding_index (TRACKER_SELECT_CONTEXT (sparql->context),
	                                                        binding);

	if (TRACKER_BINDING (binding)->data_type == TRACKER_PROPERTY_TYPE_RESOURCE) {
		_append_string_printf (sparql,
		                       "COALESCE ((SELECT ID FROM Resource WHERE Uri = ?%d), 0) ",
		                       idx + 1);
	} else {
		_append_string_printf (sparql, "?%d ", idx + 1);
	}

	if (TRACKER_BINDING (binding)->data_type == TRACKER_PROPERTY_TYPE_STRING)
		_append_string (sparql, "COLLATE " TRACKER_COLLATION_NAME " ");
}

static void
_append_variable_sql (TrackerSparql   *sparql,
                      TrackerVariable *variable)
{
	TrackerBinding *binding;

	binding = TRACKER_BINDING (tracker_variable_get_sample_binding (variable));

	if (binding &&
	    binding->data_type == TRACKER_PROPERTY_TYPE_DATETIME) {
		TrackerVariable *local_time;
		gchar *name;

		name = g_strdup_printf ("%s:local", variable->name);
		local_time = _ensure_variable (sparql, name);
		g_free (name);

		_append_string_printf (sparql, "%s ",
				       tracker_variable_get_sql_expression (local_time));
	} else {
		_append_string_printf (sparql, "%s ",
				       tracker_variable_get_sql_expression (variable));
	}
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
		case TERMINAL_TYPE_IRIREF:
			add_start = subtract_end = 1;
			break;
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
	TrackerBinding *binding;
	gchar *str;

	str = _dup_last_string (sparql);
	g_assert (str != NULL);

	binding = tracker_literal_binding_new (str, NULL);
	tracker_binding_set_data_type (binding, sparql->current_state.expression_type);
	g_free (str);

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
_extract_node_variable (TrackerParserNode *node,
			TrackerSparql     *sparql)
{
	const TrackerGrammarRule *rule = tracker_parser_node_get_rule (node);
	TrackerVariable *variable;
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
		var = _ensure_variable (sparql, str);
		tracker_token_variable_init (token, var);
	} else {
		tracker_token_literal_init (token, str);
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
                        TrackerDataTable      *table,
                        TrackerTripleContext  *triple_context,
                        GError               **error)
{
	TrackerParserNode *node = tracker_node_tree_get_root (sparql->tree);

	for (node = tracker_sparql_parser_tree_find_first (node, TRUE);
	     node;
	     node = tracker_sparql_parser_tree_find_next (node, TRUE)) {
		gchar *match_start = NULL, *match_end = NULL, *ellipsis = NULL, *num_tokens = NULL;
		gchar *str, *var_name, *sql_expression;
		const TrackerGrammarRule *rule;
		TrackerBinding *binding;
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

		var_name = g_strdup_printf ("%s:ftsSnippet", subject->name);
		var = _ensure_variable (sparql, var_name);
		g_free (var_name);

		sql_expression = g_strdup_printf ("snippet(\"%s\".\"fts5\", -1, '%s', '%s', '%s', %s)",
		                                  table->sql_query_tablename,
		                                  match_start ? match_start : "",
		                                  match_end ? match_end : "",
		                                  ellipsis ? ellipsis : "…",
		                                  num_tokens ? num_tokens : "5");

		binding = tracker_variable_binding_new (var, NULL, NULL);
		tracker_binding_set_sql_expression (binding, sql_expression);
		_add_binding (sparql, binding);
		g_object_unref (binding);

		g_free (sql_expression);
		g_free (match_start);
		g_free (match_end);
		g_free (ellipsis);
		g_free (num_tokens);
		break;
	}

	return TRUE;
}

static gboolean
_add_quad (TrackerSparql  *sparql,
           TrackerToken   *graph,
	   TrackerToken   *subject,
	   TrackerToken   *predicate,
	   TrackerToken   *object,
	   GError        **error)
{
	TrackerSelectContext *select_context;
	TrackerTripleContext *triple_context;
	TrackerOntologies *ontologies;
	TrackerDataTable *table = NULL;
	TrackerVariable *variable;
	TrackerBinding *binding;
	TrackerProperty *property = NULL;
	TrackerClass *subject_type = NULL;
	gboolean new_table = FALSE, is_fts = FALSE, is_rdf_type = FALSE;

	select_context = TRACKER_SELECT_CONTEXT (sparql->current_state.select_context);
	triple_context = TRACKER_TRIPLE_CONTEXT (sparql->current_state.context);
	ontologies = tracker_data_manager_get_ontologies (sparql->data_manager);

	if (!tracker_token_get_variable (predicate)) {
		gboolean share_table = TRUE;
		const gchar *db_table;

		property = tracker_ontologies_get_property_by_uri (ontologies,
		                                                   tracker_token_get_literal (predicate));

		if (tracker_token_is_empty (graph) &&
		    !tracker_token_get_variable (object) &&
		    g_strcmp0 (tracker_token_get_literal (predicate), RDF_NS "type") == 0) {
			/* rdf:type query */
			subject_type = tracker_ontologies_get_class_by_uri (ontologies,
			                                                    tracker_token_get_literal (object));
			if (!subject_type) {
				g_set_error (error, TRACKER_SPARQL_ERROR,
					     TRACKER_SPARQL_ERROR_UNKNOWN_CLASS,
					     "Unknown class '%s'",
				             tracker_token_get_literal (object));
				return FALSE;
			}

			is_rdf_type = TRUE;
			db_table = tracker_class_get_name (subject_type);
		} else if (g_strcmp0 (tracker_token_get_literal (predicate), FTS_NS "match") == 0) {
			db_table = "fts5";
			share_table = FALSE;
			is_fts = TRUE;
		} else if (property != NULL) {
			db_table = tracker_property_get_table_name (property);

			if (tracker_token_get_variable (subject)) {
				GPtrArray *binding_list;

				variable = tracker_token_get_variable (subject);

				if (!tracker_token_get_variable (object) &&
				    g_strcmp0 (tracker_token_get_literal (predicate), RDFS_NS "domain") == 0) {
					TrackerPredicateVariable *pred_var;
					TrackerClass *domain;

					/* rdfs:domain */
					domain = tracker_ontologies_get_class_by_uri (ontologies,
					                                              tracker_token_get_literal (object));
					if (!domain) {
						g_set_error (error, TRACKER_SPARQL_ERROR,
							     TRACKER_SPARQL_ERROR_UNKNOWN_CLASS,
							     "Unknown class '%s'",
						             tracker_token_get_literal (object));
						return FALSE;
					}

					pred_var = tracker_select_context_lookup_predicate_variable (select_context,
												     variable);
					if (!pred_var) {
						pred_var = tracker_predicate_variable_new ();
						tracker_select_context_add_predicate_variable (select_context,
											       variable,
											       pred_var);
					}

					tracker_predicate_variable_set_domain (pred_var, domain);
				}

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

					if (domain_index)
						db_table = tracker_class_get_name (domain_index);
				}
			}

			/* We can never share the table with multiple triples for
			 * multi value properties as a property may consist of multiple rows.
			 */
			share_table = !tracker_property_get_multiple_values (property);

			subject_type = tracker_property_get_domain (property);
		} else if (property == NULL) {
			g_set_error (error, TRACKER_SPARQL_ERROR,
				     TRACKER_SPARQL_ERROR_UNKNOWN_PROPERTY,
				     "Unknown property '%s'",
			             tracker_token_get_literal (predicate));
			return FALSE;
		}

		if (share_table) {
			table = tracker_triple_context_lookup_table (triple_context,
								     tracker_token_get_idstring (subject),
								     db_table);
		}

		if (!table) {
			table = tracker_triple_context_add_table (triple_context,
								  tracker_token_get_idstring (subject),
								  db_table);
			new_table = TRUE;
		}
	} else {
		TrackerPredicateVariable *pred_var;

		/* Variable in predicate */
		variable = tracker_token_get_variable (predicate);
		table = tracker_triple_context_add_table (triple_context,
		                                          variable->name, variable->name);
		new_table = TRUE;

		pred_var = tracker_select_context_lookup_predicate_variable (select_context,
									     variable);
		if (!pred_var) {
			pred_var = tracker_predicate_variable_new ();
			tracker_predicate_variable_set_triple_details (pred_var,
			                                               tracker_token_get_literal (subject),
			                                               tracker_token_get_literal (object),
								       !tracker_token_is_empty (graph));

			tracker_select_context_add_predicate_variable (select_context,
								       variable,
								       pred_var);
		}

		tracker_data_table_set_predicate_variable (table, pred_var);

		/* Add to binding list */
		binding = tracker_variable_binding_new (variable, NULL, table);
		tracker_binding_set_data_type (binding, TRACKER_PROPERTY_TYPE_RESOURCE);
		tracker_binding_set_db_column_name (binding, "predicate");
		_add_binding (sparql, binding);
		g_object_unref (binding);
	}

	if (new_table) {
		if (tracker_token_get_variable (subject)) {
			variable = tracker_token_get_variable (subject);
			binding = tracker_variable_binding_new (variable, subject_type, table);
		} else {
			binding = tracker_literal_binding_new (tracker_token_get_literal (subject),
			                                       table);
		}

		tracker_binding_set_data_type (binding, TRACKER_PROPERTY_TYPE_RESOURCE);
		tracker_binding_set_db_column_name (binding, is_fts ? "ROWID" : "ID");
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

		if (tracker_token_get_variable (predicate)) {
			tracker_binding_set_data_type (binding, TRACKER_PROPERTY_TYPE_STRING);
			tracker_binding_set_db_column_name (binding, "object");
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

			if (tracker_property_get_data_type (property) == TRACKER_PROPERTY_TYPE_DATETIME) {
				gchar *date_var, *sql_expression, *local_date, *local_time;
				TrackerBinding *local_time_binding;

				/* Merge localDate/localTime into $var:local */
				date_var = g_strdup_printf ("%s:local", variable->name);
				variable = _ensure_variable (sparql, date_var);

				local_date = tracker_binding_get_extra_sql_expression (binding, "localDate");
				local_time = tracker_binding_get_extra_sql_expression (binding, "localTime");
				sql_expression = g_strdup_printf ("((%s * 24 * 3600) + %s)",
								  local_date, local_time);

				local_time_binding = tracker_variable_binding_new (variable, NULL, NULL);
				tracker_binding_set_sql_expression (local_time_binding,
				                                    sql_expression);
				_add_binding (sparql, local_time_binding);
				g_object_unref (local_time_binding);

				g_free (sql_expression);
				g_free (local_date);
				g_free (local_time);
				g_free (date_var);
			}
		}

		_add_binding (sparql, binding);
		g_object_unref (binding);
	} else if (is_fts) {
		binding = tracker_literal_binding_new (tracker_token_get_literal (object), table);
		tracker_binding_set_db_column_name (binding, "fts5");
		_add_binding (sparql, binding);
		g_object_unref (binding);

		if (tracker_token_get_variable (subject)) {
			gchar *var_name, *sql_expression;
			TrackerVariable *fts_var;

			variable = tracker_token_get_variable (subject);

			/* FTS rank */
			var_name = g_strdup_printf ("%s:ftsRank", variable->name);
			fts_var = _ensure_variable (sparql, var_name);
			g_free (var_name);

			binding = tracker_variable_binding_new (fts_var, NULL, table);
			tracker_binding_set_db_column_name (binding, "rank");
			_add_binding (sparql, binding);
			g_object_unref (binding);

			/* FTS offsets */
			var_name = g_strdup_printf ("%s:ftsOffsets", variable->name);
			fts_var = _ensure_variable (sparql, var_name);
			g_free (var_name);

			sql_expression = g_strdup_printf ("tracker_offsets(\"%s\".\"fts5\")",
			                                  table->sql_query_tablename);
			binding = tracker_variable_binding_new (fts_var, NULL, NULL);
			tracker_binding_set_sql_expression (binding, sql_expression);
			_add_binding (sparql, binding);
			g_object_unref (binding);
			g_free (sql_expression);

			/* FTS snippet */
			if (!introspect_fts_snippet (sparql, variable,
			                             table, triple_context, error)) {
				return FALSE;
			}
		}
	} else {
		binding = tracker_literal_binding_new (tracker_token_get_literal (object),
		                                       table);

		if (tracker_token_get_variable (predicate)) {
			tracker_binding_set_db_column_name (binding, "object");
		} else {
			g_assert (property != NULL);
			tracker_binding_set_data_type (binding, tracker_property_get_data_type (property));
			tracker_binding_set_db_column_name (binding, tracker_property_get_name (property));
		}

		_add_binding (sparql, binding);
		g_object_unref (binding);
	}

	if (!tracker_token_is_empty (graph)) {
		if (tracker_token_get_variable (graph)) {
			variable = tracker_token_get_variable (graph);
			binding = tracker_variable_binding_new (variable, NULL, table);
			tracker_variable_binding_set_nullable (TRACKER_VARIABLE_BINDING (binding), TRUE);
		} else {
			binding = tracker_literal_binding_new (tracker_token_get_literal (graph), table);
		}

		tracker_binding_set_data_type (binding, TRACKER_PROPERTY_TYPE_RESOURCE);

		if (tracker_token_get_variable (predicate)) {
			tracker_binding_set_db_column_name (binding, "graph");
		} else {
			gchar *column_name;

			g_assert (property != NULL);
			column_name = g_strdup_printf ("%s:graph", tracker_property_get_name (property));
			tracker_binding_set_db_column_name (binding, column_name);
			g_free (column_name);
		}

		_add_binding (sparql, binding);
		g_object_unref (binding);
	}

	return TRUE;
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

static void
convert_expression_to_string (TrackerSparql       *sparql,
                              TrackerPropertyType  type)
{
	switch (type) {
	case TRACKER_PROPERTY_TYPE_STRING:
	case TRACKER_PROPERTY_TYPE_INTEGER:
		/* Nothing to convert. Do not use CAST to convert integers to
		 * strings as this breaks use of index when sorting by variable
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
		_prepend_string (sparql, "strftime (\"%%Y-%%m-%%d\", ");
		_append_string (sparql, ", \"unixepoch\") ");
		break;
	case TRACKER_PROPERTY_TYPE_DATETIME:
		/* ISO 8601 format */
		_prepend_string (sparql, "SparqlFormatTime (");
		_append_string (sparql, ") ");
	default:
		/* Let sqlite convert the expression to string */
		_prepend_string (sparql, "CAST (");
		_append_string (sparql, " AS TEXT) ");
		break;
	}
}

static TrackerClass **
lookup_resource_types (TrackerSparql  *sparql,
		       const gchar    *resource,
		       gint           *n_types,
		       GError        **error)
{
	TrackerOntologies *ontologies;
	TrackerDBInterface *iface;
	TrackerDBStatement *stmt;
	TrackerDBCursor *cursor = NULL;
	GError *inner_error = NULL;
	GPtrArray *types;
	gint resource_id;

	if (n_types)
		*n_types = 0;

	ontologies = tracker_data_manager_get_ontologies (sparql->data_manager);
	iface = tracker_data_manager_get_writable_db_interface (sparql->data_manager);
	resource_id = tracker_data_query_resource_id (sparql->data_manager,
						      iface, resource);

	/* This is not an error condition, query might refer to an unknown resource */
	if (resource_id <= 0)
		return NULL;

	stmt = tracker_db_interface_create_statement (iface, TRACKER_DB_STATEMENT_CACHE_TYPE_SELECT, &inner_error,
						      "SELECT (SELECT Uri FROM Resource WHERE ID = \"rdf:type\") "
						      "FROM \"rdfs:Resource_rdf:type\" WHERE ID = ?");
	if (!stmt) {
		g_propagate_error (error, inner_error);
		return NULL;
	}

	tracker_db_statement_bind_int (stmt, 0, resource_id);
	cursor = tracker_db_statement_start_cursor (stmt, error);
	g_object_unref (stmt);

	if (!cursor) {
		g_propagate_error (error, inner_error);
		return NULL;
	}

	types = g_ptr_array_new ();

	while (tracker_db_cursor_iter_next (cursor, NULL, &inner_error)) {
		TrackerClass *type;

		type = tracker_ontologies_get_class_by_uri (ontologies,
							    tracker_db_cursor_get_string (cursor, 0, NULL));
		g_ptr_array_add (types, type);
	}

	g_object_unref (cursor);

	if (inner_error) {
		g_propagate_error (error, inner_error);
		g_ptr_array_unref (types);
		return NULL;
	}

	if (n_types)
		*n_types = types->len;

	return (TrackerClass **) g_ptr_array_free (types, FALSE);
}

static gboolean
append_predicate_variable_query (TrackerSparql             *sparql,
				 TrackerPredicateVariable  *pred_var,
				 GError                   **error)
{
	TrackerOntologies *ontologies;
	TrackerProperty **properties;
	TrackerStringBuilder *str, *old;
	TrackerClass **types;
	TrackerBinding *binding = NULL;
	gint n_properties, n_types, i, j;
	GError *inner_error = NULL;
	gboolean first = TRUE;

	ontologies = tracker_data_manager_get_ontologies (sparql->data_manager);
	properties = tracker_ontologies_get_properties (ontologies, &n_properties);

	if (pred_var->subject) {
		/* <s> ?p ?o */
		types = lookup_resource_types (sparql, pred_var->subject, &n_types, &inner_error);
		if (inner_error) {
			g_propagate_error (error, inner_error);
			return FALSE;
		}

		for (i = 0; i < n_types; i++) {
			for (j = 0; j < n_properties; j++) {
				if (types[i] != tracker_property_get_domain (properties[j]))
					continue;

				if (!first)
					_append_string (sparql, "UNION ALL ");

				first = FALSE;
				_append_string_printf (sparql,
						       "SELECT ID, (SELECT ID FROM Resource WHERE Uri = '%s') AS \"predicate\", ",
						       tracker_property_get_uri (properties[j]));

				str = _append_placeholder (sparql);
				old = tracker_sparql_swap_builder (sparql, str);
				_append_string_printf (sparql, "\"%s\" ", tracker_property_get_name (properties[j]));
				convert_expression_to_string (sparql, tracker_property_get_data_type (properties[j]));
				tracker_sparql_swap_builder (sparql, old);

				_append_string (sparql, " AS \"object\" ");

				if (pred_var->return_graph) {
					_append_string_printf (sparql, ", \"%s:graph\" AS \"graph\" ",
							       tracker_property_get_name (properties[j]));
				}

				_append_string_printf (sparql, "FROM \"%s\" WHERE ID = ",
						       tracker_property_get_table_name (properties[j]));

				if (!binding) {
					binding = tracker_literal_binding_new (pred_var->subject, NULL);
					tracker_binding_set_data_type (binding, TRACKER_PROPERTY_TYPE_RESOURCE);
					tracker_select_context_add_literal_binding (TRACKER_SELECT_CONTEXT (sparql->context),
										    TRACKER_LITERAL_BINDING (binding));
				}

				_append_literal_sql (sparql, TRACKER_LITERAL_BINDING (binding));
			}
		}

		if (first) {
			/* No match */
			_append_string (sparql,
					"SELECT NULL AS ID, NULL AS \"predicate\", NULL AS \"object\", NULL AS \"graph\"");
		}

		g_free (types);
	} else if (pred_var->object) {
		/* ?s ?p <o> */
		types = lookup_resource_types (sparql, pred_var->object, &n_types, &inner_error);
		if (inner_error) {
			g_propagate_error (error, inner_error);
			return FALSE;
		}

		for (i = 0; i < n_types; i++) {
			for (j = 0; j < n_properties; j++) {
				if (types[i] != tracker_property_get_range (properties[j]))
					continue;

				if (!first)
					_append_string (sparql, "UNION ALL ");

				first = FALSE;
				_append_string_printf (sparql,
						       "SELECT ID, (SELECT ID FROM Resource WHERE Uri = '%s') AS \"predicate\", ",
						       tracker_property_get_uri (properties[j]));

				str = _append_placeholder (sparql);
				old = tracker_sparql_swap_builder (sparql, str);
				_append_string_printf (sparql, "\"%s\" ", tracker_property_get_name (properties[j]));
				convert_expression_to_string (sparql, tracker_property_get_data_type (properties[j]));
				tracker_sparql_swap_builder (sparql, old);

				_append_string (sparql, " AS \"object\" ");

				if (pred_var->return_graph) {
					_append_string_printf (sparql,
							       ", \"%s:graph\" AS \"graph\" ",
							       tracker_property_get_name (properties[j]));
				}

				_append_string_printf (sparql, " FROM \"%s\" ",
						       tracker_property_get_table_name (properties[j]));
			}
		}

		if (first) {
			/* No match */
			_append_string (sparql,
					"SELECT NULL AS ID, NULL AS \"predicate\", NULL AS \"object\", NULL AS \"graph\" ");
		}

		g_free (types);
	} else if (pred_var->domain) {
		/* Any subject, predicates limited to a specific domain */

		for (j = 0; j < n_properties; j++) {
			if (pred_var->domain != tracker_property_get_domain (properties[j]))
				continue;

			if (!first)
				_append_string (sparql, "UNION ALL ");

			first = FALSE;
			_append_string_printf (sparql,
					       "SELECT ID, (SELECT ID FROM Resource WHERE Uri = '%s') AS \"predicate\", ",
					       tracker_property_get_uri (properties[j]));

			str = _append_placeholder (sparql);
			old = tracker_sparql_swap_builder (sparql, str);
			_append_string_printf (sparql, "\"%s\" ", tracker_property_get_name (properties[j]));
			convert_expression_to_string (sparql, tracker_property_get_data_type (properties[j]));
			tracker_sparql_swap_builder (sparql, old);

			_append_string (sparql, " AS \"object\" ");

			if (pred_var->return_graph) {
				_append_string_printf (sparql,
						       ", \"%s:graph\" AS \"graph\" ",
						       tracker_property_get_name (properties[j]));
			}

			_append_string_printf (sparql, "FROM \"%s\" ",
					       tracker_property_get_table_name (properties[j]));
		}
	} else {
		/* ?s ?p ?o */
		_unimplemented ("Unrestricted predicate variables are not supported");
	}

	return TRUE;
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
		TrackerBinding *binding;
		GPtrArray *binding_list;

		binding_list = tracker_triple_context_get_variable_binding_list (triple_context,
		                                                                 var);
		if (!binding_list)
			continue;

		if (!first)
			_append_string (sparql, ", ");

		first = FALSE;
		binding = g_ptr_array_index (binding_list, 0);
		_append_string_printf (sparql, "%s AS %s ",
				       tracker_binding_get_sql_expression (binding),
				       tracker_variable_get_sql_expression (var));
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
			_append_string (sparql, "(");

			if (!append_predicate_variable_query (sparql,
							      table->predicate_variable,
							      error))
				return FALSE;

			_append_string (sparql, ") ");
		} else {
			_append_string_printf (sparql, "\"%s\" ", table->sql_db_tablename);
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
		_append_string_printf (sparql, "%s = ", tracker_binding_get_sql_expression (binding));
		_append_literal_sql (sparql, TRACKER_LITERAL_BINDING (binding));
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

	return TRUE;
}

static gboolean
translate_Update (TrackerSparql  *sparql,
                  GError        **error)
{
	/* Update ::= Prologue ( Update1 ( ';' Update )? )?
	 */
	_call_rule (sparql, NAMED_RULE_Prologue, error);

	if (_check_in_rule (sparql, NAMED_RULE_Update1)) {
		_call_rule (sparql, NAMED_RULE_Update1, error);

		if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_SEMICOLON)) {
			_call_rule (sparql, NAMED_RULE_Update, error);
		}
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

		g_hash_table_iter_init (&iter, select_context->variables);

		while (g_hash_table_iter_next (&iter, NULL, (gpointer *) &var)) {
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

				select_context->type = binding->data_type;

				if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_AS)) {
					if (!handle_as (sparql, binding->data_type, error))
						return FALSE;
				} else if (sparql->current_state.select_context == sparql->context) {
					convert_expression_to_string (sparql, binding->data_type);
					tracker_sparql_add_select_var (sparql, var->name, binding->data_type);
				}

				tracker_sparql_swap_builder (sparql, old);
			} else if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_OPEN_PARENS) ||
			           _check_in_rule (sparql, NAMED_RULE_Expression)) {
				if (!first)
					_append_string (sparql, ", ");

				str = _append_placeholder (sparql);
				old = tracker_sparql_swap_builder (sparql, str);
				_call_rule (sparql, NAMED_RULE_Expression, error);

				if (sparql->current_state.select_context == sparql->context) {
					convert_expression_to_string (sparql, sparql->current_state.expression_type);
					sparql->current_state.expression_type = TRACKER_PROPERTY_TYPE_STRING;
				}

				select_context->type = sparql->current_state.expression_type;

				if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_AS)) {
					if (!handle_as (sparql, sparql->current_state.expression_type, error))
						return FALSE;
				} else {
					tracker_sparql_add_select_var (sparql, "", sparql->current_state.expression_type);
				}

				tracker_sparql_swap_builder (sparql, old);
				_accept (sparql, RULE_TYPE_LITERAL, LITERAL_CLOSE_PARENS);
			} else {
				break;
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

	/* Prologue ::= ( BaseDecl | PrefixDecl )*
	 */
	rule = _current_rule (sparql);

	while (rule == NAMED_RULE_BaseDecl || rule == NAMED_RULE_PrefixDecl) {
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

	/* FIXME: BASE is unimplemented, and we never raised an error */

	_expect (sparql, RULE_TYPE_TERMINAL, TERMINAL_TYPE_IRIREF);

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
	TrackerStringBuilder *str;

	/* SelectQuery ::= SelectClause DatasetClause* WhereClause SolutionModifier
	 */
	sparql->context = g_object_ref_sink (tracker_select_context_new ());
	sparql->current_state.select_context = sparql->context;
	tracker_sparql_push_context (sparql, sparql->context);

	/* Skip select clause here */
	str = _append_placeholder (sparql);
	select_clause = _skip_rule (sparql, NAMED_RULE_SelectClause);

	while (_check_in_rule (sparql, NAMED_RULE_DatasetClause)) {
		_call_rule (sparql, NAMED_RULE_DatasetClause, error);
	}

	_call_rule (sparql, NAMED_RULE_WhereClause, error);

	if (!_check_undefined_variables (sparql, TRACKER_SELECT_CONTEXT (sparql->context), error))
		return FALSE;

	/* Now that we have all variable/binding information available,
	 * process the select clause.
	 */
	if (!_postprocess_rule (sparql, select_clause, str, error))
		return FALSE;

	_call_rule (sparql, NAMED_RULE_SolutionModifier, error);

	tracker_sparql_pop_context (sparql, FALSE);

	return TRUE;
}

static gboolean
translate_SubSelect (TrackerSparql  *sparql,
                     GError        **error)
{
	TrackerContext *context, *prev;
	TrackerStringBuilder *str;
	TrackerParserNode *select_clause;

	/* SubSelect ::= SelectClause WhereClause SolutionModifier ValuesClause
	 */
	context = tracker_select_context_new ();
	prev = sparql->current_state.select_context;
	sparql->current_state.select_context = context;
	tracker_sparql_push_context (sparql, context);

	/* Skip select clause here */
	str = _append_placeholder (sparql);
	select_clause = _skip_rule (sparql, NAMED_RULE_SelectClause);

	_call_rule (sparql, NAMED_RULE_WhereClause, error);

	/* Now that we have all variable/binding information available,
	 * process the select clause.
	 */
	if (!_postprocess_rule (sparql, select_clause, str, error))
		return FALSE;

	_call_rule (sparql, NAMED_RULE_SolutionModifier, error);
	_call_rule (sparql, NAMED_RULE_ValuesClause, error);

	sparql->current_state.expression_type = TRACKER_SELECT_CONTEXT (context)->type;
	tracker_sparql_pop_context (sparql, FALSE);
	sparql->current_state.select_context = prev;

	return TRUE;
}

static gboolean
translate_ConstructQuery (TrackerSparql  *sparql,
                          GError        **error)
{
	_unimplemented ("CONSTRUCT");
}

static gboolean
translate_DescribeQuery (TrackerSparql  *sparql,
                         GError        **error)
{
	_unimplemented ("DESCRIBE");
}

static gboolean
translate_AskQuery (TrackerSparql  *sparql,
                    GError        **error)
{
	/* AskQuery ::= 'ASK' DatasetClause* WhereClause SolutionModifier
	 */
	_expect (sparql, RULE_TYPE_LITERAL, LITERAL_ASK);

	sparql->context = g_object_ref_sink (tracker_select_context_new ());
	sparql->current_state.select_context = sparql->context;
	tracker_sparql_push_context (sparql, sparql->context);

	_append_string (sparql, "SELECT CASE EXISTS (SELECT 1 ");

	while (_check_in_rule (sparql, NAMED_RULE_DatasetClause)) {
		_call_rule (sparql, NAMED_RULE_DatasetClause, error);
	}

	_call_rule (sparql, NAMED_RULE_WhereClause, error);
	_call_rule (sparql, NAMED_RULE_SolutionModifier, error);

	tracker_sparql_pop_context (sparql, FALSE);

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
	/* DefaultGraphClause ::= SourceSelector
	 */
	_call_rule (sparql, NAMED_RULE_SourceSelector, error);

	/* FIXME: FROM <graph> is unimplemented, and we never raised an error */

	return TRUE;
}

static gboolean
translate_NamedGraphClause (TrackerSparql  *sparql,
                            GError        **error)
{
	/* NamedGraphClause ::= 'NAMED' SourceSelector
	 */
	_expect (sparql, RULE_TYPE_LITERAL, LITERAL_NAMED);
	_call_rule (sparql, NAMED_RULE_SourceSelector, error);

	/* FIXME: FROM NAMED <graph> is unimplemented, and we never raised an error */

	return TRUE;
}

static gboolean
translate_SourceSelector (TrackerSparql  *sparql,
                          GError        **error)
{
	/* SourceSelector ::= iri
	 */
	_call_rule (sparql, NAMED_RULE_iri, error);
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
	_accept (sparql, RULE_TYPE_LITERAL, LITERAL_WHERE);
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
	/* GroupClause ::= 'GROUP' 'BY' GroupCondition+
	 */
	_expect (sparql, RULE_TYPE_LITERAL, LITERAL_GROUP);
	_expect (sparql, RULE_TYPE_LITERAL, LITERAL_BY);
	_append_string (sparql, "GROUP BY ");

	while (_check_in_rule (sparql, NAMED_RULE_GroupCondition)) {
		_call_rule (sparql, NAMED_RULE_GroupCondition, error);
	}

	return TRUE;
}

static gboolean
translate_GroupCondition (TrackerSparql  *sparql,
                          GError        **error)
{
	/* GroupCondition ::= BuiltInCall | FunctionCall | '(' Expression ( 'AS' Var )? ')' | Var
	 */
	if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_OPEN_PARENS)) {
		_call_rule (sparql, NAMED_RULE_Expression, error);

		if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_AS)) {
			_unimplemented ("AS in GROUP BY");
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
	 */
	if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_ASC)) {
		_call_rule (sparql, NAMED_RULE_BrackettedExpression, error);
		order_str = "ASC ";
	} else if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_DESC)) {
		_call_rule (sparql, NAMED_RULE_BrackettedExpression, error);
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
		sparql->current_state.expression_type = TRACKER_BINDING (binding)->data_type;
	} else {
		g_assert_not_reached ();
	}

	if (sparql->current_state.expression_type == TRACKER_PROPERTY_TYPE_STRING)
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
		_unimplemented ("VALUES");
	}

	return TRUE;
}

static gboolean
translate_Update1 (TrackerSparql  *sparql,
                   GError        **error)
{
	TrackerGrammarNamedRule rule;

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

	return TRUE;
}

static gboolean
translate_Load (TrackerSparql  *sparql,
                GError        **error)
{
	_unimplemented ("LOAD");
}

static gboolean
translate_Clear (TrackerSparql  *sparql,
                 GError        **error)
{
	_unimplemented ("CLEAR");
}

static gboolean
translate_Drop (TrackerSparql  *sparql,
                GError        **error)
{
	_unimplemented ("DROP");
}

static gboolean
translate_Create (TrackerSparql  *sparql,
                  GError        **error)
{
	_unimplemented ("CREATE");
}

static gboolean
translate_Add (TrackerSparql  *sparql,
               GError        **error)
{
	_unimplemented ("ADD");
}

static gboolean
translate_Move (TrackerSparql  *sparql,
                GError        **error)
{
	_unimplemented ("MOVE");
}

static gboolean
translate_Copy (TrackerSparql  *sparql,
                GError        **error)
{
	_unimplemented ("COPY");
}

static gboolean
translate_InsertData (TrackerSparql  *sparql,
                      GError        **error)
{
	/* InsertData ::= 'INSERT DATA' QuadData
	 */
	_expect (sparql, RULE_TYPE_LITERAL, LITERAL_INSERT);
	_expect (sparql, RULE_TYPE_LITERAL, LITERAL_DATA);

	_call_rule (sparql, NAMED_RULE_QuadData, error);

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

	_call_rule (sparql, NAMED_RULE_QuadData, error);

	return TRUE;
}

static gboolean
translate_DeleteWhere (TrackerSparql  *sparql,
                       GError        **error)
{
	/* DeleteWhere ::= 'DELETE WHERE' QuadPattern
	 */
	_expect (sparql, RULE_TYPE_LITERAL, LITERAL_DELETE);
	_expect (sparql, RULE_TYPE_LITERAL, LITERAL_WHERE);

	_call_rule (sparql, NAMED_RULE_QuadPattern, error);

	return TRUE;
}

static gboolean
translate_Modify (TrackerSparql  *sparql,
                  GError        **error)
{
	/* Modify ::= ( 'WITH' iri )? ( DeleteClause InsertClause? | InsertClause ) UsingClause* 'WHERE' GroupGraphPattern
	 */
	if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_WITH)) {
		_call_rule (sparql, NAMED_RULE_iri, error);
	}

	if (_check_in_rule (sparql, NAMED_RULE_DeleteClause)) {
		_call_rule (sparql, NAMED_RULE_DeleteClause, error);
	}

	if (_check_in_rule (sparql, NAMED_RULE_InsertClause)) {
		_call_rule (sparql, NAMED_RULE_InsertClause, error);
	}

	while (_check_in_rule (sparql, NAMED_RULE_UsingClause)) {
		_call_rule (sparql, NAMED_RULE_UsingClause, error);
	}

	_expect (sparql, RULE_TYPE_LITERAL, LITERAL_WHERE);

	_call_rule (sparql, NAMED_RULE_GroupGraphPattern, error);

	return TRUE;
}

static gboolean
translate_DeleteClause (TrackerSparql  *sparql,
                        GError        **error)
{
	/* DeleteClause ::= 'DELETE' QuadPattern
	 */
	_expect (sparql, RULE_TYPE_LITERAL, LITERAL_DELETE);
	_call_rule (sparql, NAMED_RULE_QuadPattern, error);

	return TRUE;
}

static gboolean
translate_InsertClause (TrackerSparql  *sparql,
                        GError        **error)
{
	/* InsertClause ::= 'INSERT' QuadPattern
	 */
	_expect (sparql, RULE_TYPE_LITERAL, LITERAL_INSERT);
	_call_rule (sparql, NAMED_RULE_QuadPattern, error);

	return TRUE;
}

static gboolean
translate_UsingClause (TrackerSparql  *sparql,
                       GError        **error)
{
	/* UsingClause ::= 'USING' ( iri | 'NAMED' iri )
	 */
	_expect (sparql, RULE_TYPE_LITERAL, LITERAL_USING);

	if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_NAMED)) {
	}

	_call_rule (sparql, NAMED_RULE_iri, error);

	return TRUE;
}

static gboolean
translate_GraphOrDefault (TrackerSparql  *sparql,
                          GError        **error)
{
	/* GraphOrDefault ::= 'DEFAULT' | 'GRAPH'? iri
	 */
	if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_DEFAULT)) {

	} else {
		_accept (sparql, RULE_TYPE_LITERAL, LITERAL_GRAPH);
		_call_rule (sparql, NAMED_RULE_iri, error);
	}

	return TRUE;
}

static gboolean
translate_GraphRefAll (TrackerSparql  *sparql,
                       GError        **error)
{
	/* GraphRefAll ::= GraphRef | 'DEFAULT' | 'NAMED' | 'ALL'
	 */
	if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_DEFAULT) ||
	    _accept (sparql, RULE_TYPE_LITERAL, LITERAL_NAMED) ||
	    _accept (sparql, RULE_TYPE_LITERAL, LITERAL_ALL)) {
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

		_accept (sparql, RULE_TYPE_LITERAL, LITERAL_DOT);

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
		_accept (sparql, RULE_TYPE_LITERAL, LITERAL_DOT);

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

static gboolean
translate_ServiceGraphPattern (TrackerSparql  *sparql,
                               GError        **error)
{
	/* ServiceGraphPattern ::= 'SERVICE' 'SILENT'? VarOrIri GroupGraphPattern
	 */
	_unimplemented ("SERVICE");
}

static gboolean
translate_Bind (TrackerSparql  *sparql,
                GError        **error)
{
	TrackerStringBuilder *str, *old;
	TrackerVariable *variable;
	TrackerBinding *binding;
	TrackerPropertyType type;

	/* Bind ::= 'BIND' '(' Expression 'AS' Var ')'
	 */
	_expect (sparql, RULE_TYPE_LITERAL, LITERAL_BIND);
	_expect (sparql, RULE_TYPE_LITERAL, LITERAL_OPEN_PARENS);

	str = _prepend_placeholder (sparql);
	old = tracker_sparql_swap_builder (sparql, str);

	_append_string (sparql, "SELECT *, ");
	_call_rule (sparql, NAMED_RULE_Expression, error);
	type = sparql->current_state.expression_type;

	_expect (sparql, RULE_TYPE_LITERAL, LITERAL_AS);
	_call_rule (sparql, NAMED_RULE_Var, error);

	variable = _last_node_variable (sparql);

	if (tracker_variable_has_bindings (variable))
		_raise (PARSE, "Expected undefined variable", "BIND");

	_append_string_printf (sparql, "AS %s FROM (",
			       tracker_variable_get_sql_expression (variable));

	binding = tracker_variable_binding_new (variable, NULL, NULL);
	tracker_binding_set_data_type (binding, type);
	tracker_variable_set_sample_binding (variable, TRACKER_VARIABLE_BINDING (binding));

	tracker_sparql_swap_builder (sparql, old);
	_append_string (sparql, ") ");

	_expect (sparql, RULE_TYPE_LITERAL, LITERAL_CLOSE_PARENS);

	return TRUE;
}

static gboolean
translate_InlineData (TrackerSparql  *sparql,
                      GError        **error)
{
	/* InlineData ::= 'VALUES' DataBlock
	 */
	_unimplemented ("VALUES");
}

static gboolean
translate_DataBlock (TrackerSparql  *sparql,
                     GError        **error)
{
	TrackerGrammarNamedRule rule;

	/* DataBlock ::= InlineDataOneVar | InlineDataFull
	 */
	rule = _current_rule (sparql);

	switch (rule) {
	case NAMED_RULE_InlineDataOneVar:
	case NAMED_RULE_InlineDataFull:
		_call_rule (sparql, rule, error);
		break;
	default:
		g_assert_not_reached ();
	}

	return TRUE;
}

static gboolean
translate_InlineDataOneVar (TrackerSparql  *sparql,
                            GError        **error)
{
	/* InlineDataOneVar ::= Var '{' DataBlockValue* '}'
	 */
	_call_rule (sparql, NAMED_RULE_Var, error);

	_expect (sparql, RULE_TYPE_LITERAL, LITERAL_OPEN_BRACE);

	while (_check_in_rule (sparql, NAMED_RULE_DataBlockValue)) {
		_call_rule (sparql, NAMED_RULE_DataBlockValue, error);
	}

	_expect (sparql, RULE_TYPE_LITERAL, LITERAL_CLOSE_BRACE);

	return TRUE;
}

static gboolean
translate_InlineDataFull (TrackerSparql  *sparql,
                          GError        **error)
{
	/* InlineDataFull ::= ( NIL | '(' Var* ')' ) '{' ( '(' DataBlockValue* ')' | NIL )* '}'
	 */
	if (_accept (sparql, RULE_TYPE_TERMINAL, TERMINAL_TYPE_NIL)) {

	} else if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_OPEN_PARENS)) {
		while (_check_in_rule (sparql, NAMED_RULE_Var)) {
			_call_rule (sparql, NAMED_RULE_Var, error);
		}

		_expect (sparql, RULE_TYPE_LITERAL, LITERAL_CLOSE_PARENS);
	} else {
		g_assert_not_reached ();
	}

	_expect (sparql, RULE_TYPE_LITERAL, LITERAL_OPEN_BRACE);

	do {
		if (_accept (sparql, RULE_TYPE_TERMINAL, TERMINAL_TYPE_NIL)) {
		} else if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_OPEN_PARENS)) {
			while (_check_in_rule (sparql, NAMED_RULE_DataBlockValue)) {
				_call_rule (sparql, NAMED_RULE_DataBlockValue, error);
			}

			_expect (sparql, RULE_TYPE_LITERAL, LITERAL_CLOSE_PARENS);
		} else {
			break;
		}
	} while (TRUE);

	return TRUE;
}

static gboolean
translate_DataBlockValue (TrackerSparql  *sparql,
                          GError        **error)
{
	TrackerGrammarNamedRule rule;

	/* DataBlockValue ::= iri | RDFLiteral | NumericLiteral | BooleanLiteral | 'UNDEF'
	 */
	if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_UNDEF)) {
		return TRUE;
	}

	rule = _current_rule (sparql);

	switch (rule) {
	case NAMED_RULE_iri:
	case NAMED_RULE_RDFLiteral:
	case NAMED_RULE_NumericLiteral:
	case NAMED_RULE_BooleanLiteral:
		_call_rule (sparql, rule, error);
		break;
	default:
		g_assert_not_reached ();
	}

	return TRUE;
}

static gboolean
translate_MinusGraphPattern (TrackerSparql  *sparql,
                             GError        **error)
{
	/* MinusGraphPattern ::= 'MINUS' GroupGraphPattern
	 */
	_unimplemented ("MINUS");
}

static void
append_union_select_vars (TrackerSparql  *sparql,
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

			append_union_select_vars (sparql, c->data, vars);
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
		if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_DISTINCT)) {
			_unimplemented ("DISTINCT in ArgList");
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

	if (rule == NAMED_RULE_VarOrTerm) {
		sparql->current_state.token = &sparql->current_state.subject;
		_call_rule (sparql, rule, error);
		g_assert (!tracker_token_is_empty (&sparql->current_state.subject));
		_call_rule (sparql, NAMED_RULE_PropertyListNotEmpty, error);
	} else if (rule == NAMED_RULE_TriplesNode) {
		sparql->current_state.token = &sparql->current_state.subject;
		_call_rule (sparql, rule, error);
		g_assert (!tracker_token_is_empty (&sparql->current_state.subject));
		_call_rule (sparql, NAMED_RULE_PropertyList, error);
	}

	tracker_token_unset (&sparql->current_state.subject);
	sparql->current_state.subject = old_subject;

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
	TrackerToken old_pred;

	old_pred = sparql->current_state.predicate;

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

	if (rule == NAMED_RULE_VarOrTerm) {
		sparql->current_state.token = &sparql->current_state.subject;
		_call_rule (sparql, rule, error);
		g_assert (!tracker_token_is_empty (&sparql->current_state.subject));

		_call_rule (sparql, NAMED_RULE_PropertyListPathNotEmpty, error);
	} else if (rule == NAMED_RULE_TriplesNodePath) {
		sparql->current_state.token = &sparql->current_state.subject;
		_call_rule (sparql, rule, error);
		g_assert (!tracker_token_is_empty (&sparql->current_state.subject));

		_call_rule (sparql, NAMED_RULE_PropertyListPath, error);
	}

	tracker_token_unset (&sparql->current_state.subject);
	sparql->current_state.subject = old_subject;

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
	TrackerToken old_predicate;

	/* PropertyListPathNotEmpty ::= ( VerbPath | VerbSimple ) ObjectListPath ( ';' ( ( VerbPath | VerbSimple ) ObjectList )? )*
	 */
	rule = _current_rule (sparql);
	old_predicate = sparql->current_state.predicate;

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

	return TRUE;
}

static gboolean
translate_VerbPath (TrackerSparql  *sparql,
                    GError        **error)
{
	/* VerbPath ::= Path
	 */
	_call_rule (sparql, NAMED_RULE_Path, error);

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
	return TRUE;
}

static gboolean
translate_PathAlternative (TrackerSparql  *sparql,
                           GError        **error)
{
	/* PathAlternative ::= PathSequence ( '|' PathSequence )*
	 */
	_call_rule (sparql, NAMED_RULE_PathSequence, error);

	if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_PATH_ALTERNATIVE)) {
		_unimplemented ("Property paths");
	}

	return TRUE;
}

static gboolean
translate_PathSequence (TrackerSparql  *sparql,
                        GError        **error)
{
	/* PathSequence ::= PathEltOrInverse ( '/' PathEltOrInverse )*
	 */
	_call_rule (sparql, NAMED_RULE_PathEltOrInverse, error);

	if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_PATH_SEQUENCE)) {
		_unimplemented ("Property paths");
	}

	return TRUE;
}

static gboolean
translate_PathEltOrInverse (TrackerSparql  *sparql,
                            GError        **error)
{
	/* PathEltOrInverse ::= PathElt | '^' PathElt
	 */
	if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_PATH_INVERSE)) {
		_unimplemented ("Property paths");
	}

	_call_rule (sparql, NAMED_RULE_PathElt, error);

	return TRUE;
}

static gboolean
translate_PathElt (TrackerSparql  *sparql,
                   GError        **error)
{
	/* PathElt ::= PathPrimary PathMod?
	 */
	_call_rule (sparql, NAMED_RULE_PathPrimary, error);
	_init_token (&sparql->current_state.predicate,
	             sparql->current_state.prev_node, sparql);

	if (_check_in_rule (sparql, NAMED_RULE_PathMod)) {
		_call_rule (sparql, NAMED_RULE_PathMod, error);
	}

	return TRUE;
}

static gboolean
translate_PathMod (TrackerSparql  *sparql,
                   GError        **error)
{
	/* PathMod ::= '?' | '*' | '+'
	 */
	_unimplemented ("Property paths");
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
	} else if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_A)) {

	} else if (_check_in_rule (sparql, NAMED_RULE_iri)) {
		_call_rule (sparql, NAMED_RULE_iri, error);
	} else {
		g_assert_not_reached ();
	}

	return TRUE;
}

static gboolean
translate_PathNegatedPropertySet (TrackerSparql  *sparql,
                                  GError        **error)
{
	/* PathNegatedPropertySet ::= PathOneInPropertySet | '(' ( PathOneInPropertySet ( '|' PathOneInPropertySet )* )? ')'
	 */
	_unimplemented ("Property paths");
}

static gboolean
translate_PathOneInPropertySet (TrackerSparql  *sparql,
                                GError        **error)
{
	/* PathOneInPropertySet ::= iri | 'a' | '^' ( iri | 'a' )
	 */
	_unimplemented ("Property paths");
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

	var = tracker_select_context_add_generated_variable (TRACKER_SELECT_CONTEXT (sparql->context));
	tracker_token_variable_init (&sparql->current_state.subject, var);
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
	/* Collection ::= '(' GraphNode+ ')'
	 */
	_unimplemented ("Collections are not supported");
}

static gboolean
translate_CollectionPath (TrackerSparql  *sparql,
                          GError        **error)
{
	/* CollectionPath ::= '(' GraphNodePath+ ')'
	 */
	_unimplemented ("Collections are not supported");
}

static gboolean
translate_GraphNode (TrackerSparql  *sparql,
                     GError        **error)
{
	/* GraphNode ::= VarOrTerm | TriplesNode
	 */
	if (_check_in_rule (sparql, NAMED_RULE_VarOrTerm)) {
		sparql->current_state.token = &sparql->current_state.object;
		_call_rule (sparql, NAMED_RULE_VarOrTerm, error);
		g_assert (!tracker_token_is_empty (&sparql->current_state.object));
	} else if (_check_in_rule (sparql, NAMED_RULE_TriplesNode)) {
		sparql->current_state.token = &sparql->current_state.object;
		_call_rule (sparql, NAMED_RULE_TriplesNode, error);
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
translate_GraphNodePath (TrackerSparql  *sparql,
                         GError        **error)
{
	/* GraphNodePath ::= VarOrTerm | TriplesNodePath
	 */
	if (_check_in_rule (sparql, NAMED_RULE_VarOrTerm)) {
		sparql->current_state.token = &sparql->current_state.object;
		_call_rule (sparql, NAMED_RULE_VarOrTerm, error);
		g_assert (!tracker_token_is_empty (&sparql->current_state.object));
	} else if (_check_in_rule (sparql, NAMED_RULE_TriplesNodePath)) {
		sparql->current_state.token = &sparql->current_state.object;
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
		TrackerVariableBinding *binding;
		TrackerVariable *var;

		/* Ensure the variable is referenced in the context */
		var = _extract_node_variable (sparql->current_state.prev_node,
		                              sparql);

		binding = tracker_variable_get_sample_binding (var);

		if (binding)
			sparql->current_state.expression_type = TRACKER_BINDING (binding)->data_type;
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
	case NAMED_RULE_iri:
	case NAMED_RULE_RDFLiteral:
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
	/* Expression ::= ConditionalOrExpression
	 */
	_call_rule (sparql, NAMED_RULE_ConditionalOrExpression, error);

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
	if (tracker_property_get_multiple_values (property)) {
		TrackerStringBuilder *str, *old;

		_append_string (sparql, "(SELECT GROUP_CONCAT (");
		str = _append_placeholder (sparql);
		old = tracker_sparql_swap_builder (sparql, str);
		_append_string_printf (sparql, "\"%s\"", tracker_property_get_name (property));
		convert_expression_to_string (sparql, tracker_property_get_data_type (property));
		tracker_sparql_swap_builder (sparql, old);

		_append_string_printf (sparql, ", ',') FROM \"%s\" WHERE ID = ",
				       tracker_property_get_table_name (property));

		_call_rule (sparql, NAMED_RULE_ArgList, error);
		sparql->current_state.expression_type = TRACKER_PROPERTY_TYPE_STRING;
	} else {
		_append_string_printf (sparql,
				       "(SELECT \"%s\" FROM \"%s\" WHERE ID = ",
				       tracker_property_get_name (property),
				       tracker_property_get_table_name (property));

		_call_rule (sparql, NAMED_RULE_ArgList, error);
		sparql->current_state.expression_type = tracker_property_get_data_type (property);
	}

	_append_string (sparql, ") ");

	return TRUE;
}

static gboolean
handle_type_cast (TrackerSparql  *sparql,
		  const gchar    *function,
		  GError        **error)
{
	if (g_str_equal (function, XSD_NS "string")) {
		_append_string (sparql, "CAST (");
		_call_rule (sparql, NAMED_RULE_ArgList, error);
		_append_string (sparql, "AS TEXT) ");
		sparql->current_state.expression_type = TRACKER_PROPERTY_TYPE_STRING;
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
		_call_rule (sparql, NAMED_RULE_ArgList, error);
		_append_string (sparql, ") ");
	} else if (g_str_equal (function, FN_NS "upper-case")) {
		_append_string (sparql, "SparqlUpperCase (");
		_call_rule (sparql, NAMED_RULE_ArgList, error);
		_append_string (sparql, ") ");
	} else if (g_str_equal (function, FN_NS "contains")) {
		/* contains('A','B') => 'A' GLOB '*B*' */
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
		_call_rule (sparql, NAMED_RULE_ArgList, error);
		_append_string (sparql, ") ");
		sparql->current_state.expression_type = TRACKER_PROPERTY_TYPE_STRING;
	} else if (g_str_equal (function, FN_NS "concat")) {
		const gchar *old_sep;

		old_sep = tracker_sparql_swap_current_expression_list_separator (sparql, " || ");
		_call_rule (sparql, NAMED_RULE_ArgList, error);
		tracker_sparql_swap_current_expression_list_separator (sparql, old_sep);
		sparql->current_state.expression_type = TRACKER_PROPERTY_TYPE_STRING;
	} else if (g_str_equal (function, FN_NS "string-join")) {
		_append_string (sparql, "SparqlStringJoin (");
		_call_rule (sparql, NAMED_RULE_ArgList, error);
		_append_string (sparql, ") ");
		sparql->current_state.expression_type = TRACKER_PROPERTY_TYPE_STRING;
	} else if (g_str_equal (function, FN_NS "replace")) {
		_append_string (sparql, "SparqlReplace (");
		_call_rule (sparql, NAMED_RULE_ArgList, error);
		_append_string (sparql, ") ");
		sparql->current_state.expression_type = TRACKER_PROPERTY_TYPE_STRING;
	} else if (g_str_equal (function, FN_NS "year-from-dateTime")) {
		_step (sparql);
		if (!helper_translate_date (sparql, "%%Y", error))
			return FALSE;
		sparql->current_state.expression_type = TRACKER_PROPERTY_TYPE_INTEGER;
	} else if (g_str_equal (function, FN_NS "month-from-dateTime")) {
		_step (sparql);
		if (!helper_translate_date (sparql, "%%m", error))
			return FALSE;
		sparql->current_state.expression_type = TRACKER_PROPERTY_TYPE_INTEGER;
	} else if (g_str_equal (function, FN_NS "day-from-dateTime")) {
		_step (sparql);
		if (!helper_translate_date (sparql, "%%d", error))
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
		TrackerVariable *variable;

		_step (sparql);
		_append_string (sparql, "( ");
		_expect (sparql, RULE_TYPE_LITERAL, LITERAL_OPEN_PARENS);

		_call_rule (sparql, NAMED_RULE_Expression, error);
		variable = _last_node_variable (sparql);

		if (!variable) {
			_raise (PARSE, "Expected variable", "fn:timezone-from-dateTime");
		} else {
			_append_string_printf (sparql, " - %s ",
					       tracker_variable_get_sql_expression (variable));
		}

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
		_append_string (sparql, "SparqlCaseFold (");
		_call_rule (sparql, NAMED_RULE_ArgList, error);
		_append_string (sparql, ") ");
	} else if (g_str_equal (function, TRACKER_NS "title-order")) {
		_call_rule (sparql, NAMED_RULE_ArgList, error);
		_append_string (sparql, "COLLATE " TRACKER_TITLE_COLLATION_NAME " ");
	} else if (g_str_equal (function, TRACKER_NS "ascii-lower-case")) {
		_append_string (sparql, "lower (");
		_call_rule (sparql, NAMED_RULE_ArgList, error);
		_append_string (sparql, ") ");
	} else if (g_str_equal (function, TRACKER_NS "normalize")) {
		_append_string (sparql, "SparqlNormalize (");
		_call_rule (sparql, NAMED_RULE_ArgList, error);
		_append_string (sparql, ") ");
	} else if (g_str_equal (function, TRACKER_NS "unaccent")) {
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
		_append_string (sparql, "COALESCE (");
		_call_rule (sparql, NAMED_RULE_ArgList, error);
		_append_string (sparql, ") ");
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
	gboolean handled;

	if (g_str_has_prefix (function, XSD_NS)) {
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
	_append_string_printf (sparql, "strftime (\"%s\", ", format);

	_call_rule (sparql, NAMED_RULE_Expression, error);

	_expect (sparql, RULE_TYPE_LITERAL, LITERAL_CLOSE_PARENS);
	_append_string (sparql, ", \"unixepoch\") ");

	return TRUE;
}

static gboolean
helper_translate_time (TrackerSparql  *sparql,
                       guint           format,
                       GError        **error)
{
	_expect (sparql, RULE_TYPE_LITERAL, LITERAL_OPEN_PARENS);
	_call_rule (sparql, NAMED_RULE_Expression, error);

	_expect (sparql, RULE_TYPE_LITERAL, LITERAL_CLOSE_PARENS);

	switch (format) {
	case TIME_FORMAT_SECONDS:
		_append_string (sparql, " % 60 ");
		break;
	case TIME_FORMAT_MINUTES:
		_append_string (sparql, " / 60 % 60 ");
		break;
	case TIME_FORMAT_HOURS:
		_append_string (sparql, " / 3600 % 24 ");
		break;
	default:
		g_assert_not_reached ();
	}

	return TRUE;
}

static gboolean
translate_BuiltInCall (TrackerSparql  *sparql,
                       GError        **error)
{
	const gchar *old_sep;

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
		_unimplemented ("DATATYPE");
	} else if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_IRI)) {
		_unimplemented ("IRI");
	} else if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_URI)) {
		_unimplemented ("URI");
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
		_expect (sparql, RULE_TYPE_LITERAL, LITERAL_OPEN_PARENS);
		_append_string (sparql, "LENGTH (");
		_call_rule (sparql, NAMED_RULE_Expression, error);
		_expect (sparql, RULE_TYPE_LITERAL, LITERAL_CLOSE_PARENS);
		_append_string (sparql, ") ");
		sparql->current_state.expression_type = TRACKER_PROPERTY_TYPE_INTEGER;
	} else if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_UCASE)) {
		_expect (sparql, RULE_TYPE_LITERAL, LITERAL_OPEN_PARENS);
		_append_string (sparql, "SparqlUpperCase (");
		_call_rule (sparql, NAMED_RULE_Expression, error);
		_expect (sparql, RULE_TYPE_LITERAL, LITERAL_CLOSE_PARENS);
		_append_string (sparql, ") ");
	} else if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_LCASE)) {
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
		if (!helper_translate_date (sparql, "%%Y", error))
			return FALSE;
		sparql->current_state.expression_type = TRACKER_PROPERTY_TYPE_INTEGER;
	} else if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_MONTH)) {
		if (!helper_translate_date (sparql, "%%m", error))
			return FALSE;
		sparql->current_state.expression_type = TRACKER_PROPERTY_TYPE_INTEGER;
	} else if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_DAY)) {
		if (!helper_translate_date (sparql, "%%d", error))
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
		_unimplemented ("TIMEZONE");
	} else if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_TZ)) {
		_unimplemented ("TZ");
	} else if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_MD5)) {
		_expect (sparql, RULE_TYPE_LITERAL, LITERAL_OPEN_PARENS);
		_append_string (sparql, "SparqlChecksum (");
		_call_rule (sparql, NAMED_RULE_Expression, error);
		_expect (sparql, RULE_TYPE_LITERAL, LITERAL_CLOSE_PARENS);
		_append_string (sparql, ", \"md5\") ");
		sparql->current_state.expression_type = TRACKER_PROPERTY_TYPE_STRING;
	} else if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_SHA1)) {
		_expect (sparql, RULE_TYPE_LITERAL, LITERAL_OPEN_PARENS);
		_append_string (sparql, "SparqlChecksum (");
		_call_rule (sparql, NAMED_RULE_Expression, error);
		_expect (sparql, RULE_TYPE_LITERAL, LITERAL_CLOSE_PARENS);
		_append_string (sparql, ", \"sha1\") ");
		sparql->current_state.expression_type = TRACKER_PROPERTY_TYPE_STRING;
	} else if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_SHA256)) {
		_expect (sparql, RULE_TYPE_LITERAL, LITERAL_OPEN_PARENS);
		_append_string (sparql, "SparqlChecksum (");
		_call_rule (sparql, NAMED_RULE_Expression, error);
		_expect (sparql, RULE_TYPE_LITERAL, LITERAL_CLOSE_PARENS);
		_append_string (sparql, ", \"sha256\") ");
		sparql->current_state.expression_type = TRACKER_PROPERTY_TYPE_STRING;
	} else if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_SHA384)) {
		_expect (sparql, RULE_TYPE_LITERAL, LITERAL_OPEN_PARENS);
		_append_string (sparql, "SparqlChecksum (");
		_call_rule (sparql, NAMED_RULE_Expression, error);
		_expect (sparql, RULE_TYPE_LITERAL, LITERAL_CLOSE_PARENS);
		_append_string (sparql, ", \"sha384\") ");
		sparql->current_state.expression_type = TRACKER_PROPERTY_TYPE_STRING;
	} else if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_SHA512)) {
		_expect (sparql, RULE_TYPE_LITERAL, LITERAL_OPEN_PARENS);
		_append_string (sparql, "SparqlChecksum (");
		_call_rule (sparql, NAMED_RULE_Expression, error);
		_expect (sparql, RULE_TYPE_LITERAL, LITERAL_CLOSE_PARENS);
		_append_string (sparql, ", \"sha512\") ");
		sparql->current_state.expression_type = TRACKER_PROPERTY_TYPE_STRING;
	} else if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_ISIRI) ||
		   _accept (sparql, RULE_TYPE_LITERAL, LITERAL_ISURI)) {
		TrackerBinding *binding;
		const gchar *str;

		_expect (sparql, RULE_TYPE_LITERAL, LITERAL_OPEN_PARENS);

		_call_rule (sparql, NAMED_RULE_Expression, error);

		str = (sparql->current_state.expression_type == TRACKER_PROPERTY_TYPE_RESOURCE) ? "1" : "0";

		binding = tracker_literal_binding_new (str, NULL);
		tracker_select_context_add_literal_binding (TRACKER_SELECT_CONTEXT (sparql->context),
		                                            TRACKER_LITERAL_BINDING (binding));
		_append_literal_sql (sparql, TRACKER_LITERAL_BINDING (binding));

		_expect (sparql, RULE_TYPE_LITERAL, LITERAL_CLOSE_PARENS);
		sparql->current_state.expression_type = TRACKER_PROPERTY_TYPE_BOOLEAN;
	} else if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_ISBLANK)) {
		_unimplemented ("ISBLANK");
	} else if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_ISLITERAL)) {
		_unimplemented ("ISLITERAL");
	} else if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_ISNUMERIC)) {
		_unimplemented ("ISNUMERIC");
	} else if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_LANGMATCHES)) {
		_unimplemented ("LANGMATCHES");
	} else if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_CONTAINS)) {
		/* contains('A','B') => 'A' GLOB '*B*' */
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
		_expect (sparql, RULE_TYPE_LITERAL, LITERAL_OPEN_PARENS);
		_append_string (sparql, "SparqlStringBefore (");
		_call_rule (sparql, NAMED_RULE_Expression, error);
		_expect (sparql, RULE_TYPE_LITERAL, LITERAL_COMMA);
		_append_string (sparql, ", ");
		_call_rule (sparql, NAMED_RULE_Expression, error);
		_expect (sparql, RULE_TYPE_LITERAL, LITERAL_CLOSE_PARENS);
		_append_string (sparql, ") ");
	} else if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_STRAFTER)) {
		_expect (sparql, RULE_TYPE_LITERAL, LITERAL_OPEN_PARENS);
		_append_string (sparql, "SparqlStringAfter (");
		_call_rule (sparql, NAMED_RULE_Expression, error);
		_expect (sparql, RULE_TYPE_LITERAL, LITERAL_COMMA);
		_append_string (sparql, ", ");
		_call_rule (sparql, NAMED_RULE_Expression, error);
		_expect (sparql, RULE_TYPE_LITERAL, LITERAL_CLOSE_PARENS);
		_append_string (sparql, ") ");
	} else if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_STRLANG)) {
		_unimplemented ("STRLANG");
	} else if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_STRDT)) {
		_unimplemented ("STRDT");
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
		_unimplemented ("BNODE");
	} else if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_RAND)) {
		_expect (sparql, RULE_TYPE_TERMINAL, TERMINAL_TYPE_NIL);
		_append_string (sparql, "SparqlRand() ");
		sparql->current_state.expression_type = TRACKER_PROPERTY_TYPE_DOUBLE;
	} else if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_NOW)) {
		_expect (sparql, RULE_TYPE_TERMINAL, TERMINAL_TYPE_NIL);
		_append_string (sparql, "strftime('%%s', 'now') ");
		sparql->current_state.expression_type = TRACKER_PROPERTY_TYPE_DATETIME;
	} else if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_UUID)) {
		_expect (sparql, RULE_TYPE_TERMINAL, TERMINAL_TYPE_NIL);
		_unimplemented ("UUID");
	} else if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_STRUUID)) {
		_expect (sparql, RULE_TYPE_TERMINAL, TERMINAL_TYPE_NIL);
		_unimplemented ("STRUUID");
	} else if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_CONCAT)) {
		old_sep = tracker_sparql_swap_current_expression_list_separator (sparql, " || ");
		_call_rule (sparql, NAMED_RULE_ExpressionList, error);
		tracker_sparql_swap_current_expression_list_separator (sparql, old_sep);
		sparql->current_state.expression_type = TRACKER_PROPERTY_TYPE_STRING;
	} else if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_COALESCE)) {
		_append_string (sparql, "COALESCE ");
		old_sep = tracker_sparql_swap_current_expression_list_separator (sparql, ", ");
		_call_rule (sparql, NAMED_RULE_ExpressionList, error);
		tracker_sparql_swap_current_expression_list_separator (sparql, old_sep);
	}

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
		_expect (sparql, RULE_TYPE_LITERAL, LITERAL_OPEN_PARENS);
		_append_string (sparql, "GROUP_CONCAT(");

		if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_DISTINCT))
			_append_string (sparql, "DISTINCT ");

		_call_rule (sparql, NAMED_RULE_Expression, error);

		if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_SEMICOLON)) {
			_expect (sparql, RULE_TYPE_LITERAL, LITERAL_SEPARATOR);
			_expect (sparql, RULE_TYPE_LITERAL, LITERAL_OP_EQ);
			_append_string (sparql, ", ");

			_call_rule (sparql, NAMED_RULE_String, error);
		}

		_expect (sparql, RULE_TYPE_LITERAL, LITERAL_CLOSE_PARENS);
		_append_string (sparql, ") ");
		sparql->current_state.expression_type = TRACKER_PROPERTY_TYPE_STRING;
	} else if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_SAMPLE)) {
		_unimplemented ("SAMPLE");
	} else {
		g_assert_not_reached ();
	}

	return TRUE;
}

static gboolean
translate_RDFLiteral (TrackerSparql  *sparql,
                      GError        **error)
{
	TrackerBinding *binding;

	/* RDFLiteral ::= String ( LANGTAG | ( '^^' iri ) )?
	 */
	_call_rule (sparql, NAMED_RULE_String, error);
	binding = _convert_terminal (sparql);

	if (_accept (sparql, RULE_TYPE_TERMINAL, TERMINAL_TYPE_LANGTAG)) {
		g_object_unref (binding);
		_unimplemented ("LANGTAG");
	} else if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_DOUBLE_CIRCUMFLEX)) {
		gchar *cast;

		_call_rule (sparql, NAMED_RULE_iri, error);
		cast = _dup_last_string (sparql);

		if (g_str_equal (cast, XSD_NS "boolean")) {
			sparql->current_state.expression_type = TRACKER_PROPERTY_TYPE_BOOLEAN;
		} else if (g_str_equal (cast, XSD_NS "integer") ||
		           g_str_equal (cast, XSD_NS "nonPositiveInteger") ||
		           g_str_equal (cast, XSD_NS "negativeInteger") ||
		           g_str_equal (cast, XSD_NS "long") ||
		           g_str_equal (cast, XSD_NS "int") ||
		           g_str_equal (cast, XSD_NS "short") ||
		           g_str_equal (cast, XSD_NS "byte") ||
		           g_str_equal (cast, XSD_NS "nonNegativeInteger") ||
		           g_str_equal (cast, XSD_NS "unsignedLong") ||
		           g_str_equal (cast, XSD_NS "unsignedInt") ||
		           g_str_equal (cast, XSD_NS "unsignedShort") ||
		           g_str_equal (cast, XSD_NS "unsignedByte") ||
		           g_str_equal (cast, XSD_NS "positiveInteger")) {
			sparql->current_state.expression_type = TRACKER_PROPERTY_TYPE_INTEGER;
		} else if (g_str_equal (cast, XSD_NS "double")) {
			sparql->current_state.expression_type = TRACKER_PROPERTY_TYPE_DOUBLE;
		} else if (g_str_equal (cast, XSD_NS "date")) {
			sparql->current_state.expression_type = TRACKER_PROPERTY_TYPE_DATE;
		} else if (g_str_equal (cast, XSD_NS "dateTime")) {
			sparql->current_state.expression_type = TRACKER_PROPERTY_TYPE_DATETIME;
		} else {
			sparql->current_state.expression_type = TRACKER_PROPERTY_TYPE_STRING;
		}

		g_free (cast);
	}

	tracker_binding_set_data_type (binding, sparql->current_state.expression_type);
	tracker_select_context_add_literal_binding (TRACKER_SELECT_CONTEXT (sparql->context),
	                                            TRACKER_LITERAL_BINDING (binding));

	g_object_unref (binding);

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
	 */
	if (_accept (sparql, RULE_TYPE_TERMINAL, TERMINAL_TYPE_INTEGER)) {
		sparql->current_state.expression_type = TRACKER_PROPERTY_TYPE_INTEGER;
	} else if (_accept (sparql, RULE_TYPE_TERMINAL, TERMINAL_TYPE_DOUBLE) ||
	           _accept (sparql, RULE_TYPE_TERMINAL, TERMINAL_TYPE_DECIMAL)) {
		sparql->current_state.expression_type = TRACKER_PROPERTY_TYPE_DOUBLE;
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
	 */
	if (_accept (sparql, RULE_TYPE_TERMINAL, TERMINAL_TYPE_INTEGER_POSITIVE)) {
		sparql->current_state.expression_type = TRACKER_PROPERTY_TYPE_INTEGER;
	} else if (_accept (sparql, RULE_TYPE_TERMINAL, TERMINAL_TYPE_DECIMAL_POSITIVE) ||
	           _accept (sparql, RULE_TYPE_TERMINAL, TERMINAL_TYPE_DOUBLE_POSITIVE)) {
		sparql->current_state.expression_type = TRACKER_PROPERTY_TYPE_DOUBLE;
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
	 */
	if (_accept (sparql, RULE_TYPE_TERMINAL, TERMINAL_TYPE_INTEGER_NEGATIVE)) {
		sparql->current_state.expression_type = TRACKER_PROPERTY_TYPE_INTEGER;
	} else if (_accept (sparql, RULE_TYPE_TERMINAL, TERMINAL_TYPE_DECIMAL_NEGATIVE) ||
	           _accept (sparql, RULE_TYPE_TERMINAL, TERMINAL_TYPE_DOUBLE_NEGATIVE)) {
		sparql->current_state.expression_type = TRACKER_PROPERTY_TYPE_DOUBLE;
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
	 */
	if (_accept (sparql, RULE_TYPE_LITERAL, LITERAL_TRUE) ||
	    _accept (sparql, RULE_TYPE_LITERAL, LITERAL_FALSE)) {
		sparql->current_state.expression_type = TRACKER_PROPERTY_TYPE_BOOLEAN;
		return TRUE;
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
	 */
	if (_accept (sparql, RULE_TYPE_TERMINAL, TERMINAL_TYPE_STRING_LITERAL1) ||
	    _accept (sparql, RULE_TYPE_TERMINAL, TERMINAL_TYPE_STRING_LITERAL2) ||
	    _accept (sparql, RULE_TYPE_TERMINAL, TERMINAL_TYPE_STRING_LITERAL_LONG1) ||
	    _accept (sparql, RULE_TYPE_TERMINAL, TERMINAL_TYPE_STRING_LITERAL_LONG2)) {
		sparql->current_state.expression_type = TRACKER_PROPERTY_TYPE_STRING;
		return TRUE;
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
	TrackerVariable *var;

	/* BlankNode ::= BLANK_NODE_LABEL | ANON
	 */
	if (_accept (sparql, RULE_TYPE_TERMINAL, TERMINAL_TYPE_ANON)) {
		g_assert (sparql->current_state.token != NULL);

		var = tracker_select_context_add_generated_variable (TRACKER_SELECT_CONTEXT (sparql->context));
		tracker_token_variable_init (sparql->current_state.token, var);
	} else if (_accept (sparql, RULE_TYPE_TERMINAL, TERMINAL_TYPE_BLANK_NODE_LABEL)) {
		/* FIXME */
		return TRUE;
	} else {
		g_assert_not_reached ();
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
	sparql->var_names = g_ptr_array_new_with_free_func (g_free);
	sparql->var_types = g_array_new (FALSE, FALSE, sizeof (TrackerPropertyType));
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
	sparql->sparql = query;

	tree = tracker_sparql_parse_query (sparql->sparql, -1, NULL,
					   &sparql->parser_error);
	if (tree) {
		sparql->tree = tree;
		sparql->sql = tracker_string_builder_new ();

		sparql->current_state.node = tracker_node_tree_get_root (sparql->tree);
		sparql->current_state.sql = sparql->sql;
	}

	return sparql;
}

static TrackerDBStatement *
prepare_query (TrackerDBInterface    *iface,
               TrackerStringBuilder  *str,
               GPtrArray             *literals,
               GError               **error)
{
	TrackerDBStatement *stmt;
	gchar *query;
	guint i;

	query = tracker_string_builder_to_string (str);
	stmt = tracker_db_interface_create_statement (iface,
	                                              TRACKER_DB_STATEMENT_CACHE_TYPE_SELECT,
	                                              error, query);
	g_free (query);

	if (!stmt || !literals)
		return stmt;

	for (i = 0; i < literals->len; i++) {
		TrackerLiteralBinding *binding;
		TrackerPropertyType prop_type;

		binding = g_ptr_array_index (literals, i);
		prop_type = TRACKER_BINDING (binding)->data_type;

		if (prop_type == TRACKER_PROPERTY_TYPE_BOOLEAN) {
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
		} else {
			tracker_db_statement_bind_text (stmt, i, binding->literal);
		}
	}

	return stmt;
}

TrackerSparqlCursor *
tracker_sparql_execute_cursor (TrackerSparql  *sparql,
                               GError        **error)
{
	TrackerDBStatement *stmt;
	TrackerDBInterface *iface;
	TrackerDBCursor *cursor;
	TrackerPropertyType *types;
	const gchar * const *names;
	guint n_types, n_names;

	if (sparql->parser_error) {
		g_propagate_error (error, sparql->parser_error);
		return NULL;
	}

	if (!_call_rule_func (sparql, NAMED_RULE_Query, error))
		return NULL;

	iface = tracker_data_manager_get_db_interface (sparql->data_manager);
	stmt = prepare_query (iface, sparql->sql,
	                      TRACKER_SELECT_CONTEXT (sparql->context)->literal_bindings,
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
