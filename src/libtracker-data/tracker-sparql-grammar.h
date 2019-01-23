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
#ifndef __TRACKER_GRAMMAR_H__
#define __TRACKER_GRAMMAR_H__

#include "string.h"

typedef enum {
	NAMED_RULE_QueryUnit,
	NAMED_RULE_UpdateUnit,
	NAMED_RULE_Query,
	NAMED_RULE_Update,
	NAMED_RULE_SelectClause,
	NAMED_RULE_Prologue,
	NAMED_RULE_BaseDecl,
	NAMED_RULE_PrefixDecl,
	NAMED_RULE_SelectQuery,
	NAMED_RULE_SubSelect,
	NAMED_RULE_ConstructQuery,
	NAMED_RULE_DescribeQuery,
	NAMED_RULE_AskQuery,
	NAMED_RULE_DatasetClause,
	NAMED_RULE_DefaultGraphClause,
	NAMED_RULE_NamedGraphClause,
	NAMED_RULE_SourceSelector,
	NAMED_RULE_WhereClause,
	NAMED_RULE_SolutionModifier,
	NAMED_RULE_GroupClause,
	NAMED_RULE_GroupCondition,
	NAMED_RULE_HavingClause,
	NAMED_RULE_HavingCondition,
	NAMED_RULE_OrderClause,
	NAMED_RULE_OrderCondition,
	NAMED_RULE_LimitOffsetClauses,
	NAMED_RULE_LimitClause,
	NAMED_RULE_OffsetClause,
	NAMED_RULE_ValuesClause,
	NAMED_RULE_Update1,
	NAMED_RULE_Load,
	NAMED_RULE_Clear,
	NAMED_RULE_Drop,
	NAMED_RULE_Create,
	NAMED_RULE_Add,
	NAMED_RULE_Move,
	NAMED_RULE_Copy,
	NAMED_RULE_InsertData,
	NAMED_RULE_DeleteData,
	NAMED_RULE_DeleteWhere,
	NAMED_RULE_Modify,
	NAMED_RULE_DeleteClause,
	NAMED_RULE_InsertClause,
	NAMED_RULE_UsingClause,
	NAMED_RULE_GraphOrDefault,
	NAMED_RULE_GraphRefAll,
	NAMED_RULE_GraphRef,
	NAMED_RULE_QuadPattern,
	NAMED_RULE_QuadData,
	NAMED_RULE_Quads,
	NAMED_RULE_QuadsNotTriples,
	NAMED_RULE_TriplesTemplate,
	NAMED_RULE_GroupGraphPatternSub,
	NAMED_RULE_TriplesBlock,
	NAMED_RULE_GraphPatternNotTriples,
	NAMED_RULE_OptionalGraphPattern,
	NAMED_RULE_GraphGraphPattern,
	NAMED_RULE_ServiceGraphPattern,
	NAMED_RULE_Bind,
	NAMED_RULE_InlineData,
	NAMED_RULE_DataBlock,
	NAMED_RULE_InlineDataOneVar,
	NAMED_RULE_InlineDataFull,
	NAMED_RULE_DataBlockValue,
	NAMED_RULE_MinusGraphPattern,
	NAMED_RULE_GroupOrUnionGraphPattern,
	NAMED_RULE_Filter,
	NAMED_RULE_Constraint,
	NAMED_RULE_FunctionCall,
	NAMED_RULE_ArgList,
	NAMED_RULE_ExpressionList,
	NAMED_RULE_ConstructTemplate,
	NAMED_RULE_ConstructTriples,
	NAMED_RULE_TriplesSameSubject,
	NAMED_RULE_GroupGraphPattern,
	NAMED_RULE_PropertyList,
	NAMED_RULE_PropertyListNotEmpty,
	NAMED_RULE_Verb,
	NAMED_RULE_ObjectList,
	NAMED_RULE_Object,
	NAMED_RULE_TriplesSameSubjectPath,
	NAMED_RULE_PropertyListPath,
	NAMED_RULE_PropertyListPathNotEmpty,
	NAMED_RULE_VerbPath,
	NAMED_RULE_VerbSimple,
	NAMED_RULE_ObjectListPath,
	NAMED_RULE_ObjectPath,
	NAMED_RULE_Path,
	NAMED_RULE_PathAlternative,
	NAMED_RULE_PathSequence,
	NAMED_RULE_PathEltOrInverse,
	NAMED_RULE_PathElt,
	NAMED_RULE_PathMod,
	NAMED_RULE_PathPrimary,
	NAMED_RULE_PathNegatedPropertySet,
	NAMED_RULE_PathOneInPropertySet,
	NAMED_RULE_Integer,
	NAMED_RULE_TriplesNode,
	NAMED_RULE_BlankNodePropertyList,
	NAMED_RULE_TriplesNodePath,
	NAMED_RULE_BlankNodePropertyListPath,
	NAMED_RULE_Collection,
	NAMED_RULE_CollectionPath,
	NAMED_RULE_GraphNode,
	NAMED_RULE_GraphNodePath,
	NAMED_RULE_VarOrTerm,
	NAMED_RULE_VarOrIri,
	NAMED_RULE_Var,
	NAMED_RULE_GraphTerm,
	NAMED_RULE_Expression,
	NAMED_RULE_ConditionalOrExpression,
	NAMED_RULE_ConditionalAndExpression,
	NAMED_RULE_ValueLogical,
	NAMED_RULE_RelationalExpression,
	NAMED_RULE_NumericExpression,
	NAMED_RULE_AdditiveExpression,
	NAMED_RULE_MultiplicativeExpression,
	NAMED_RULE_UnaryExpression,
	NAMED_RULE_PrimaryExpression,
	NAMED_RULE_iriOrFunction,
	NAMED_RULE_BrackettedExpression,
	NAMED_RULE_BuiltInCall,
	NAMED_RULE_RegexExpression,
	NAMED_RULE_SubstringExpression,
	NAMED_RULE_StrReplaceExpression,
	NAMED_RULE_ExistsFunc,
	NAMED_RULE_NotExistsFunc,
	NAMED_RULE_Aggregate,
	NAMED_RULE_RDFLiteral,
	NAMED_RULE_NumericLiteral,
	NAMED_RULE_NumericLiteralUnsigned,
	NAMED_RULE_NumericLiteralPositive,
	NAMED_RULE_NumericLiteralNegative,
	NAMED_RULE_BooleanLiteral,
	NAMED_RULE_String,
	NAMED_RULE_iri,
	NAMED_RULE_PrefixedName,
	NAMED_RULE_BlankNode,
	N_NAMED_RULES
} TrackerGrammarNamedRule;

typedef enum {
	LITERAL_A,
	LITERAL_ABS,
	LITERAL_ADD,
	LITERAL_ALL,
	LITERAL_ARITH_MULT,
	LITERAL_ARITH_DIV,
	LITERAL_ARITH_PLUS,
	LITERAL_ARITH_MINUS,
	LITERAL_AS,
	LITERAL_ASC,
	LITERAL_ASK,
	LITERAL_AVG,
	LITERAL_BASE,
	LITERAL_BIND,
	LITERAL_BNODE,
	LITERAL_BOUND,
	LITERAL_BY,
	LITERAL_CEIL,
	LITERAL_CLEAR,
	LITERAL_CLOSE_BRACE,
	LITERAL_CLOSE_BRACKET,
	LITERAL_CLOSE_PARENS,
	LITERAL_COALESCE,
	LITERAL_COLON,
	LITERAL_CONCAT,
	LITERAL_CONTAINS,
	LITERAL_COMMA,
	LITERAL_CONSTRUCT,
	LITERAL_COPY,
	LITERAL_COUNT,
	LITERAL_CREATE,
	LITERAL_DATA,
	LITERAL_DATATYPE,
	LITERAL_DAY,
	LITERAL_DEFAULT,
	LITERAL_DELETE,
	LITERAL_DESC,
	LITERAL_DESCRIBE,
	LITERAL_DISTINCT,
	LITERAL_DOT,
	LITERAL_DOUBLE_CIRCUMFLEX,
	LITERAL_DROP,
	LITERAL_ENCODE_FOR_URI,
	LITERAL_EXISTS,
	LITERAL_FALSE,
	LITERAL_FILTER,
	LITERAL_FLOOR,
	LITERAL_FROM,
	LITERAL_GLOB,
	LITERAL_GRAPH,
	LITERAL_GROUP,
	LITERAL_GROUP_CONCAT,
	LITERAL_HAVING,
	LITERAL_HOURS,
	LITERAL_IF,
	LITERAL_INSERT,
	LITERAL_INTO,
	LITERAL_IRI,
	LITERAL_ISBLANK,
	LITERAL_ISIRI,
	LITERAL_ISLITERAL,
	LITERAL_ISNUMERIC,
	LITERAL_ISURI,
	LITERAL_LANG,
	LITERAL_LANGMATCHES,
	LITERAL_LCASE,
	LITERAL_LIMIT,
	LITERAL_LOAD,
	LITERAL_MAX,
	LITERAL_MD5,
	LITERAL_MIN,
	LITERAL_MINUS,
	LITERAL_MINUTES,
	LITERAL_MONTH,
	LITERAL_MOVE,
	LITERAL_NAMED,
	LITERAL_NOT,
	LITERAL_NOW,
	LITERAL_NULL, /* TRACKER EXTENSION */
	LITERAL_OFFSET,
	LITERAL_OP_AND,
	LITERAL_OP_EQ,
	LITERAL_OP_GE,
	LITERAL_OP_GT,
	LITERAL_OP_LE,
	LITERAL_OP_LT,
	LITERAL_OP_NE,
	LITERAL_OP_NEG,
	LITERAL_OP_OR,
	LITERAL_OP_IN,
	LITERAL_OPEN_BRACE,
	LITERAL_OPEN_BRACKET,
	LITERAL_OPEN_PARENS,
	LITERAL_OPTIONAL,
	LITERAL_OR,
	LITERAL_ORDER,
	LITERAL_PATH_SEQUENCE,
	LITERAL_PATH_ALTERNATIVE,
	LITERAL_PATH_INVERSE,
	LITERAL_PATH_OPTIONAL,
	LITERAL_PATH_STAR,
	LITERAL_PATH_PLUS,
	LITERAL_PREFIX,
	LITERAL_RAND,
	LITERAL_REDUCED,
	LITERAL_REGEX,
	LITERAL_REPLACE,
	LITERAL_ROUND,
	LITERAL_SAMETERM,
	LITERAL_SAMPLE,
	LITERAL_SECONDS,
	LITERAL_SELECT,
	LITERAL_SEMICOLON,
	LITERAL_SEPARATOR,
	LITERAL_SERVICE,
	LITERAL_SHA1,
	LITERAL_SHA256,
	LITERAL_SHA384,
	LITERAL_SHA512,
	LITERAL_SILENT,
	LITERAL_STR,
	LITERAL_STRAFTER,
	LITERAL_STRBEFORE,
	LITERAL_STRDT,
	LITERAL_STRENDS,
	LITERAL_STRLANG,
	LITERAL_STRLEN,
	LITERAL_STRSTARTS,
	LITERAL_STRUUID,
	LITERAL_SUBSTR,
	LITERAL_SUM,
	LITERAL_TIMEZONE,
	LITERAL_TO,
	LITERAL_TRUE,
	LITERAL_TZ,
	LITERAL_UCASE,
	LITERAL_UNDEF,
	LITERAL_UNION,
	LITERAL_URI,
	LITERAL_USING,
	LITERAL_UUID,
	LITERAL_VALUES,
	LITERAL_VAR,
	LITERAL_WHERE,
	LITERAL_WITH,
	LITERAL_YEAR,
	N_LITERALS
} TrackerGrammarLiteral;

static const gchar literals[][N_LITERALS] = {
	"a", /* LITERAL_A */
	"abs",  /* LITERAL_ABS */
	"add",  /* LITERAL_ADD */
	"all",  /* LITERAL_ALL */
	"*", /* LITERAL_ARITH_MULT */
	"/",  /* LITERAL_ARITH_DIV */
	"+",  /* LITERAL_ARITH_PLUS */
	"-", /* LITERAL_ARITH_MINUS */
	"as",  /* LITERAL_AS */
	"asc",  /* LITERAL_ASC */
	"ask",  /* LITERAL_ASK */
	"avg",  /* LITERAL_AVG */
	"base", /* LITERAL_BASE */
	"bind", /* LITERAL_BIND */
	"bnode", /* LITERAL_BNODE */
	"bound", /* LITERAL_BOUND */
	"by", /* LITERAL_BY */
	"ceil", /* LITERAL_CEIL */
	"clear", /* LITERAL_CLEAR */
	"}", /* LITERAL_CLOSE_BRACE */
	"]", /* LITERAL_CLOSE_BRACKET */
	")", /* LITERAL_CLOSE_PARENS */
	"coalesce", /* LITERAL_COALESCE */
	":", /* LITERAL_COLON */
	"concat", /* LITERAL_CONCAT */
	"contains", /* LITERAL_CONTAINS */
	",", /* LITERAL_COMMA */
	"construct", /* LITERAL_CONSTRUCT */
	"copy", /* LITERAL_COPY */
	"count", /* LITERAL_COUNT */
	"create", /* LITERAL_CREATE */
	"data", /* LITERAL_DATA */
	"datatype", /* LITERAL_DATATYPE */
	"day", /* LITERAL_DAY */
	"default", /* LITERAL_DEFAULT */
	"delete", /* LITERAL_DELETE */
	"desc", /* LITERAL_DESC */
	"describe", /* LITERAL_DESCRIBE */
	"distinct", /* LITERAL_DISTINCT */
	".", /* LITERAL_DOT */
	"^^", /* LITERAL_DOUBLE_CIRCUMFLEX */
	"drop", /* LITERAL_DROP */
	"encode_for_uri", /* LITERAL_ENCODE_FOR_URI */
	"exists", /* LITERAL_EXISTS */
	"false", /* LITERAL_FALSE */
	"filter", /* LITERAL_FILTER */
	"floor", /* LITERAL_FLOOR */
	"from", /* LITERAL_FROM */
	"*", /* LITERAL_GLOB */
	"graph", /* LITERAL_GRAPH */
	"group", /* LITERAL_GROUP */
	"group_concat", /* LITERAL_GROUP_CONCAT */
	"having", /* LITERAL_HAVING */
	"hours", /* LITERAL_HOURS */
	"if", /* LITERAL_IF */
	"insert", /* LITERAL_INSERT */
	"into", /* LITERAL_INTO */
	"iri", /* LITERAL_IRI */
	"isblank", /* LITERAL_ISBLANK */
	"isiri", /* LITERAL_ISIRI */
	"isliteral", /* LITERAL_ISLITERAL */
	"isnumeric", /* LITERAL_ISNUMERIC */
	"isuri", /* LITERAL_ISURI */
	"lang", /* LITERAL_LANG */
	"langmatches", /* LITERAL_LANGMATCHES */
	"lcase", /* LITERAL_LCASE */
	"limit", /* LITERAL_LIMIT */
	"load", /* LITERAL_LOAD */
	"max", /* LITERAL_MAX */
	"md5", /* LITERAL_MD5 */
	"min", /* LITERAL_MIN */
	"minus", /* LITERAL_MINUS */
	"minutes", /* LITERAL_MINUTES */
	"month", /* LITERAL_MONTH */
	"move", /* LITERAL_MOVE */
	"named", /* LITERAL_NAMED */
	"not", /* LITERAL_NOT */
	"now", /* LITERAL_NOW */
	"null", /* LITERAL_NULL (TRACKER EXTENSION) */
	"offset", /* LITERAL_OFFSET */
	"&&", /* LITERAL_OP_AND */
	"=", /* LITERAL_OP_EQ */
	">=", /* LITERAL_OP_GE */
	">", /* LITERAL_OP_GT */
	"<=", /* LITERAL_OP_LE */
	"<", /* LITERAL_OP_LT */
	"!=", /* LITERAL_OP_NE */
	"!", /* LITERAL_OP_NEG */
	"||", /* LITERAL_OP_OR */
	"in", /* LITERAL_OP_IN */
	"{", /* LITERAL_OPEN_BRACE */
	"[", /* LITERAL_OPEN_BRACKET */
	"(", /* LITERAL_OPEN_PARENS */
	"optional", /* LITERAL_OPTIONAL */
	"or", /* LITERAL_OR */
	"order", /* LITERAL_ORDER */
	"/", /* LITERAL_PATH_SEQUENCE */
	"|", /* LITERAL_PATH_ALTERNATIVE */
	"^", /* LITERAL_PATH_INVERSE */
	"?", /* LITERAL_PATH_OPTIONAL */
	"*", /* LITERAL_PATH_STAR */
	"+", /* LITERAL_PATH_PLUS */
	"prefix", /* LITERAL_PREFIX */
	"rand", /* LITERAL_RAND */
	"reduced", /* LITERAL_REDUCED */
	"regex", /* LITERAL_REGEX */
	"replace", /* LITERAL_REPLACE */
	"round", /* LITERAL_ROUND */
	"sameterm", /* LITERAL_SAMETERM */
	"sample", /* LITERAL_SAMPLE */
	"seconds", /* LITERAL_SECONDS */
	"select", /* LITERAL_SELECT */
	";", /* LITERAL_SEMICOLON */
	"separator", /* LITERAL_SEPARATOR */
	"service", /* LITERAL_SERVICE */
	"sha1", /* LITERAL_SHA1 */
	"sha256", /* LITERAL_SHA256 */
	"sha384", /* LITERAL_SHA384 */
	"sha512", /* LITERAL_SHA512 */
	"silent", /* LITERAL_SILENT */
	"str", /* LITERAL_STR */
	"strafter", /* LITERAL_STRAFTER */
	"strbefore", /* LITERAL_STRBEFORE */
	"strdt", /* LITERAL_STRDT */
	"strends", /* LITERAL_STRENDS */
	"strlang", /* LITERAL_STRLANG */
	"strlen", /* LITERAL_STRLEN */
	"strstarts", /* LITERAL_STRSTARTS */
	"struuid", /* LITERAL_STRUUID */
	"substr", /* LITERAL_SUBSTR */
	"sum", /* LITERAL_SUM */
	"timezone", /* LITERAL_TIMEZONE */
	"to", /* LITERAL_TO */
	"true", /* LITERAL_TRUE */
	"tz", /* LITERAL_TZ */
	"ucase", /* LITERAL_UCASE */
	"undef", /* LITERAL_UNDEF */
	"union", /* LITERAL_UNION */
	"uri", /* LITERAL_URI */
	"using", /* LITERAL_USING */
	"uuid", /* LITERAL_UUID */
	"values", /* LITERAL_VALUES */
	"var", /* LITERAL_VAR */
	"where", /* LITERAL_WHERE */
	"with", /* LITERAL_WITH */
	"year", /* LITERAL_YEAR */
};

typedef enum {
	TERMINAL_TYPE_IRIREF,
	TERMINAL_TYPE_PNAME_NS,
	TERMINAL_TYPE_PNAME_LN,
	TERMINAL_TYPE_BLANK_NODE_LABEL,
	TERMINAL_TYPE_VAR1,
	TERMINAL_TYPE_VAR2,
	TERMINAL_TYPE_LANGTAG,
	TERMINAL_TYPE_INTEGER,
	TERMINAL_TYPE_DECIMAL,
	TERMINAL_TYPE_DOUBLE,
	TERMINAL_TYPE_INTEGER_POSITIVE,
	TERMINAL_TYPE_DECIMAL_POSITIVE,
	TERMINAL_TYPE_DOUBLE_POSITIVE,
	TERMINAL_TYPE_INTEGER_NEGATIVE,
	TERMINAL_TYPE_DECIMAL_NEGATIVE,
	TERMINAL_TYPE_DOUBLE_NEGATIVE,
	TERMINAL_TYPE_STRING_LITERAL1,
	TERMINAL_TYPE_STRING_LITERAL2,
	TERMINAL_TYPE_STRING_LITERAL_LONG1,
	TERMINAL_TYPE_STRING_LITERAL_LONG2,
	TERMINAL_TYPE_NIL,
	TERMINAL_TYPE_ANON,
	TERMINAL_TYPE_PARAMETERIZED_VAR,
	N_TERMINAL_TYPES
} TrackerGrammarTerminalType;

typedef struct _TrackerGrammarRule TrackerGrammarRule;

typedef enum {
	RULE_TYPE_NIL,
	RULE_TYPE_RULE,
	RULE_TYPE_TERMINAL,
	RULE_TYPE_LITERAL,
	RULE_TYPE_SEQUENCE,
	RULE_TYPE_OR,
	RULE_TYPE_GT0,
	RULE_TYPE_GTE0,
	RULE_TYPE_OPTIONAL,
} TrackerGrammarRuleType;

struct _TrackerGrammarRule {
	TrackerGrammarRuleType type;
	const gchar *string;
	union {
		TrackerGrammarLiteral literal;
		TrackerGrammarNamedRule rule;
		TrackerGrammarTerminalType terminal;
		const TrackerGrammarRule *children;
	} data;
};

typedef gboolean (*TrackerTerminalFunc) (const gchar  *str,
					 const gchar  *end,
					 const gchar **str_out);

/* R=Rule, L=Literal, S=Sequence, T=Terminal, OR=Or, GTE0=">=0"(*) GT0=">0"(+) OPT=Optional(?), NIL=Closing rule */
#define R(target) { RULE_TYPE_RULE, #target, .data = { .rule = NAMED_RULE_##target } }
#define L(lit) { RULE_TYPE_LITERAL, literals[LITERAL_##lit], .data = { .literal = LITERAL_##lit } }
#define T(name) { RULE_TYPE_TERMINAL, #name, .data = { .terminal = TERMINAL_TYPE_##name } }

#define OR(rules) { RULE_TYPE_OR, NULL, .data = { .children = (rules) } }
#define S(rules) { RULE_TYPE_SEQUENCE, NULL, .data = { .children = (rules) } }
#define GT0(rules) { RULE_TYPE_GT0, NULL, .data = { .children = (rules) } }
#define GTE0(rules) { RULE_TYPE_GTE0, NULL, .data = { .children = (rules) } }
#define OPT(rules) { RULE_TYPE_OPTIONAL, NULL, .data = { .children = (rules) } }
#define NIL { RULE_TYPE_NIL }

/* Rules to parse SPARQL, as per https://www.w3.org/TR/sparql11-query/#sparqlGrammar */

/* BlankNode ::= BLANK_NODE_LABEL | ANON
 */
static const TrackerGrammarRule helper_BlankNode_or[] = { T(BLANK_NODE_LABEL), T(ANON), NIL };
static const TrackerGrammarRule rule_BlankNode[] = { OR(helper_BlankNode_or), NIL };

/* PrefixedName ::= PNAME_LN | PNAME_NS
 */
static const TrackerGrammarRule helper_PrefixedName_or[] = { T(PNAME_LN), T(PNAME_NS), NIL };
static const TrackerGrammarRule rule_PrefixedName[] = { OR(helper_PrefixedName_or), NIL };

/* iri ::= IRIREF | PrefixedName
 */
static const TrackerGrammarRule helper_iri_or[] = { T(IRIREF), R(PrefixedName), NIL };
static const TrackerGrammarRule rule_iri[] = { OR(helper_iri_or), NIL };

/* String ::= STRING_LITERAL1 | STRING_LITERAL2 | STRING_LITERAL_LONG1 | STRING_LITERAL_LONG2
 *
 * TRACKER EXTENSION:
 * The terminal PARAMETERIZED_VAR is additionally accepted
 */
static const TrackerGrammarRule helper_String_or[] = { T(STRING_LITERAL1), T(STRING_LITERAL2), T(STRING_LITERAL_LONG1), T(STRING_LITERAL_LONG2), T(PARAMETERIZED_VAR), NIL };
static const TrackerGrammarRule rule_String[] = { OR(helper_String_or), NIL };

/* BooleanLiteral ::= 'true' | 'false'
 *
 * TRACKER EXTENSION:
 * The terminal PARAMETERIZED_VAR is additionally accepted
 */
static const TrackerGrammarRule helper_BooleanLiteral_or[] = { L(TRUE), L(FALSE), T(PARAMETERIZED_VAR), NIL };
static const TrackerGrammarRule rule_BooleanLiteral[] = { OR(helper_BooleanLiteral_or), NIL };

/* NumericLiteralNegative ::= INTEGER_NEGATIVE | DECIMAL_NEGATIVE | DOUBLE_NEGATIVE
 *
 * TRACKER EXTENSION:
 * The terminal PARAMETERIZED_VAR is additionally accepted
 */
static const TrackerGrammarRule helper_NumericLiteralNegative_or[] = { T(DECIMAL_NEGATIVE), T(DOUBLE_NEGATIVE), T(INTEGER_NEGATIVE), T(PARAMETERIZED_VAR), NIL };
static const TrackerGrammarRule rule_NumericLiteralNegative[] = { OR(helper_NumericLiteralNegative_or), NIL };

/* NumericLiteralPositive ::= INTEGER_POSITIVE | DECIMAL_POSITIVE | DOUBLE_POSITIVE
 *
 * TRACKER EXTENSION:
 * The terminal PARAMETERIZED_VAR is additionally accepted
 */
static const TrackerGrammarRule helper_NumericLiteralPositive_or[] = { T(DECIMAL_POSITIVE), T(DOUBLE_POSITIVE), T(INTEGER_POSITIVE), T(PARAMETERIZED_VAR), NIL };
static const TrackerGrammarRule rule_NumericLiteralPositive[] = { OR(helper_NumericLiteralPositive_or), NIL };

/* NumericLiteralUnsigned ::= INTEGER | DECIMAL | DOUBLE
 *
 * TRACKER EXTENSION:
 * The terminal PARAMETERIZED_VAR is additionally accepted
 */
static const TrackerGrammarRule helper_NumericLiteralUnsigned_or[] = { T(DECIMAL), T(DOUBLE), T(INTEGER), T(PARAMETERIZED_VAR), NIL };
static const TrackerGrammarRule rule_NumericLiteralUnsigned[] = { OR(helper_NumericLiteralUnsigned_or), NIL };

/* NumericLiteral ::= NumericLiteralUnsigned | NumericLiteralPositive | NumericLiteralNegative
 */
static const TrackerGrammarRule helper_NumericLiteral_or[] = { R(NumericLiteralUnsigned), R(NumericLiteralPositive), R(NumericLiteralNegative), NIL };
static const TrackerGrammarRule rule_NumericLiteral[] = { OR(helper_NumericLiteral_or), NIL };

/* RDFLiteral ::= String ( LANGTAG | ( '^^' iri ) )?
 */
static const TrackerGrammarRule helper_RDFLiteral_seq[] = { L(DOUBLE_CIRCUMFLEX), R(iri), NIL };
static const TrackerGrammarRule helper_RDFLiteral_or[] = { T(LANGTAG), S(helper_RDFLiteral_seq), NIL };
static const TrackerGrammarRule helper_RDFLiteral_opt[] = { OR(helper_RDFLiteral_or), NIL };
static const TrackerGrammarRule rule_RDFLiteral[] = { R(String), OPT(helper_RDFLiteral_opt), NIL };

/* Aggregate ::= 'COUNT' '(' 'DISTINCT'? ( '*' | Expression ) ')'
 *               | 'SUM' '(' 'DISTINCT'? Expression ')'
 *               | 'MIN' '(' 'DISTINCT'? Expression ')'
 *               | 'MAX' '(' 'DISTINCT'? Expression ')'
 *               | 'AVG' '(' 'DISTINCT'? Expression ')'
 *               | 'SAMPLE' '(' 'DISTINCT'? Expression ')'
 *               | 'GROUP_CONCAT' '(' 'DISTINCT'? Expression ( ';' 'SEPARATOR' '=' String )? ')'
 *
 * TRACKER EXTENSION:
 *
 * GROUP_CONCAT accepts a comma separator, so effectively:
 * 'GROUP_CONCAT' '(' 'DISTINCT'? Expression ( ( ';' 'SEPARATOR' '=' | ',') String )? ')'
 */
static const TrackerGrammarRule helper_Aggregate_opt_1[] = { L(DISTINCT), NIL };
static const TrackerGrammarRule helper_Aggregate_or_1[] = { L(GLOB), R(Expression), NIL };
static const TrackerGrammarRule helper_Aggregate_seq_1[] = { L(COUNT), L(OPEN_PARENS), OPT(helper_Aggregate_opt_1), OR(helper_Aggregate_or_1), L(CLOSE_PARENS), NIL };
static const TrackerGrammarRule helper_Aggregate_seq_2[] = { L(SUM), L(OPEN_PARENS), OPT(helper_Aggregate_opt_1), R(Expression), L(CLOSE_PARENS), NIL };
static const TrackerGrammarRule helper_Aggregate_seq_3[] = { L(MIN), L(OPEN_PARENS), OPT(helper_Aggregate_opt_1), R(Expression), L(CLOSE_PARENS), NIL };
static const TrackerGrammarRule helper_Aggregate_seq_4[] = { L(MAX), L(OPEN_PARENS), OPT(helper_Aggregate_opt_1), R(Expression), L(CLOSE_PARENS), NIL };
static const TrackerGrammarRule helper_Aggregate_seq_5[] = { L(AVG), L(OPEN_PARENS), OPT(helper_Aggregate_opt_1), R(Expression), L(CLOSE_PARENS), NIL };
static const TrackerGrammarRule helper_Aggregate_seq_6[] = { L(SAMPLE), L(OPEN_PARENS), OPT(helper_Aggregate_opt_1), R(Expression), L(CLOSE_PARENS), NIL };
static const TrackerGrammarRule helper_Aggregate_seq_8[] = { L(SEMICOLON), L(SEPARATOR), L(OP_EQ), NIL };
static const TrackerGrammarRule helper_Aggregate_or_3[] = { S(helper_Aggregate_seq_8), L(COMMA), NIL };
static const TrackerGrammarRule helper_Aggregate_seq_in_opt[] = { OR(helper_Aggregate_or_3), R(String), NIL };
static const TrackerGrammarRule helper_Aggregate_opt_2[] = { S(helper_Aggregate_seq_in_opt), NIL };
static const TrackerGrammarRule helper_Aggregate_seq_7[] = { L(GROUP_CONCAT), L(OPEN_PARENS), OPT(helper_Aggregate_opt_1), R(Expression), OPT(helper_Aggregate_opt_2), L(CLOSE_PARENS), NIL };
static const TrackerGrammarRule helper_Aggregate_or_2[] = { S(helper_Aggregate_seq_1), S(helper_Aggregate_seq_2), S(helper_Aggregate_seq_3), S(helper_Aggregate_seq_4), S(helper_Aggregate_seq_5), S(helper_Aggregate_seq_6), S(helper_Aggregate_seq_7), NIL };
static const TrackerGrammarRule rule_Aggregate[] = { OR(helper_Aggregate_or_2), NIL };

/* NotExistsFunc ::= 'NOT' 'EXISTS' GroupGraphPattern
 */
static const TrackerGrammarRule rule_NotExistsFunc[] = { L(NOT), L(EXISTS), R(GroupGraphPattern), NIL };

/* ExistsFunc ::= 'EXISTS' GroupGraphPattern
 */
static const TrackerGrammarRule rule_ExistsFunc[] = { L(EXISTS), R(GroupGraphPattern), NIL };

/* StrReplaceExpression ::= 'REPLACE' '(' Expression ',' Expression ',' Expression ( ',' Expression )? ')'
 */
static const TrackerGrammarRule helper_StrReplaceExpression_seq[] = { L(COMMA), R(Expression), NIL };
static const TrackerGrammarRule helper_StrReplaceExpression_opt[] = { S(helper_StrReplaceExpression_seq), NIL };
static const TrackerGrammarRule rule_StrReplaceExpression[] = { L(REPLACE), L(OPEN_PARENS), R(Expression), L(COMMA), R( Expression), L(COMMA), R( Expression), OPT(helper_StrReplaceExpression_opt), L(CLOSE_PARENS), NIL };

/* SubstringExpression ::= 'SUBSTR' '(' Expression ',' Expression ( ',' Expression )? ')'
 */
static const TrackerGrammarRule helper_SubstringExpression_seq[] = { L(COMMA), R( Expression), NIL };
static const TrackerGrammarRule helper_SubstringExpression_opt[] = { S(helper_SubstringExpression_seq), NIL };
static const TrackerGrammarRule rule_SubstringExpression[] = { L(SUBSTR), L(OPEN_PARENS), R( Expression), L(COMMA), R(Expression), OPT(helper_SubstringExpression_opt), L(CLOSE_PARENS), NIL };

/* RegexExpression ::= 'REGEX' '(' Expression ',' Expression ( ',' Expression )? ')'
 */
static const TrackerGrammarRule helper_RegexExpression_seq[] = { L(COMMA), R(Expression), NIL };
static const TrackerGrammarRule helper_RegexExpression_opt[] = { S(helper_RegexExpression_seq), NIL };
static const TrackerGrammarRule rule_RegexExpression[] = { L(REGEX), L(OPEN_PARENS), R(Expression), L(COMMA), R(Expression), OPT(helper_RegexExpression_opt), L(CLOSE_PARENS), NIL };

/* BuiltInCall ::= Aggregate
 *                 | 'STR' '(' Expression ')'
 *                 | 'LANG' '(' Expression ')'
 *                 | 'LANGMATCHES' '(' Expression ',' Expression ')'
 *                 | 'DATATYPE' '(' Expression ')'
 *                 | 'BOUND' '(' Var ')'
 *                 | 'IRI' '(' Expression ')'
 *                 | 'URI' '(' Expression ')'
 *                 | 'BNODE' ( '(' Expression ')' | NIL )
 *                 | 'RAND' NIL
 *                 | 'ABS' '(' Expression ')'
 *                 | 'CEIL' '(' Expression ')'
 *                 | 'FLOOR' '(' Expression ')'
 *                 | 'ROUND' '(' Expression ')'
 *                 | 'CONCAT' ExpressionList
 *                 | SubstringExpression
 *                 | 'STRLEN' '(' Expression ')'
 *                 | StrReplaceExpression
 *                 | 'UCASE' '(' Expression ')'
 *                 | 'LCASE' '(' Expression ')'
 *                 | 'ENCODE_FOR_URI' '(' Expression ')'
 *                 | 'CONTAINS' '(' Expression ',' Expression ')'
 *                 | 'STRSTARTS' '(' Expression ',' Expression ')'
 *                 | 'STRENDS' '(' Expression ',' Expression ')'
 *                 | 'STRBEFORE' '(' Expression ',' Expression ')'
 *                 | 'STRAFTER' '(' Expression ',' Expression ')'
 *                 | 'YEAR' '(' Expression ')'
 *                 | 'MONTH' '(' Expression ')'
 *                 | 'DAY' '(' Expression ')'
 *                 | 'HOURS' '(' Expression ')'
 *                 | 'MINUTES' '(' Expression ')'
 *                 | 'SECONDS' '(' Expression ')'
 *                 | 'TIMEZONE' '(' Expression ')'
 *                 | 'TZ' '(' Expression ')'
 *                 | 'NOW' NIL
 *                 | 'UUID' NIL
 *                 | 'STRUUID' NIL
 *                 | 'MD5' '(' Expression ')'
 *                 | 'SHA1' '(' Expression ')'
 *                 | 'SHA256' '(' Expression ')'
 *                 | 'SHA384' '(' Expression ')'
 *                 | 'SHA512' '(' Expression ')'
 *                 | 'COALESCE' ExpressionList
 *                 | 'IF' '(' Expression ',' Expression ',' Expression ')'
 *                 | 'STRLANG' '(' Expression ',' Expression ')'
 *                 | 'STRDT' '(' Expression ',' Expression ')'
 *                 | 'sameTerm' '(' Expression ',' Expression ')'
 *                 | 'isIRI' '(' Expression ')'
 *                 | 'isURI' '(' Expression ')'
 *                 | 'isBLANK' '(' Expression ')'
 *                 | 'isLITERAL' '(' Expression ')'
 *                 | 'isNUMERIC' '(' Expression ')'
 *                 | RegexExpression
 *                 | ExistsFunc
 *                 | NotExistsFunc
 *
 * TRACKER EXTENSION:
 * BOUND accepts the more generic Expression rule, resulting in:
 *    'BOUND' '(' Expression ')'
 */
static const TrackerGrammarRule helper_BuiltInCall_seq_1[] = { L(OPEN_PARENS), R(Expression), L(CLOSE_PARENS), NIL };
static const TrackerGrammarRule helper_BuiltInCall_or_1[] = { T(NIL), S(helper_BuiltInCall_seq_1), NIL };
static const TrackerGrammarRule helper_BuiltInCall_seq_2[] = { L(STR), S(helper_BuiltInCall_seq_1), NIL };
static const TrackerGrammarRule helper_BuiltInCall_seq_3[] = { L(LANG), S(helper_BuiltInCall_seq_1), NIL };
static const TrackerGrammarRule helper_BuiltInCall_seq_4[] = { L(LANGMATCHES), L(OPEN_PARENS), R(Expression), L(COMMA), R(Expression), L(CLOSE_PARENS), NIL };
static const TrackerGrammarRule helper_BuiltInCall_seq_5[] = { L(DATATYPE), S(helper_BuiltInCall_seq_1), NIL };
static const TrackerGrammarRule helper_BuiltInCall_seq_6[] = { L(BOUND), L(OPEN_PARENS), R(Expression), L(CLOSE_PARENS), NIL };
static const TrackerGrammarRule helper_BuiltInCall_seq_7[] = { L(IRI), S(helper_BuiltInCall_seq_1), NIL };
static const TrackerGrammarRule helper_BuiltInCall_seq_8[] = { L(URI), S(helper_BuiltInCall_seq_1), NIL };
static const TrackerGrammarRule helper_BuiltInCall_seq_9[] = { L(BNODE), OR(helper_BuiltInCall_or_1), NIL };
static const TrackerGrammarRule helper_BuiltInCall_seq_10[] = { L(RAND), T(NIL), NIL };
static const TrackerGrammarRule helper_BuiltInCall_seq_11[] = { L(ABS), S(helper_BuiltInCall_seq_1), NIL };
static const TrackerGrammarRule helper_BuiltInCall_seq_12[] = { L(CEIL), S(helper_BuiltInCall_seq_1), NIL };
static const TrackerGrammarRule helper_BuiltInCall_seq_13[] = { L(FLOOR), S(helper_BuiltInCall_seq_1), NIL };
static const TrackerGrammarRule helper_BuiltInCall_seq_14[] = { L(ROUND), S(helper_BuiltInCall_seq_1), NIL };
static const TrackerGrammarRule helper_BuiltInCall_seq_15[] = { L(CONCAT), R(ExpressionList), NIL };
static const TrackerGrammarRule helper_BuiltInCall_seq_16[] = { R(SubstringExpression), NIL };
static const TrackerGrammarRule helper_BuiltInCall_seq_17[] = { L(STRLEN), S(helper_BuiltInCall_seq_1), NIL };
static const TrackerGrammarRule helper_BuiltInCall_seq_18[] = { R(StrReplaceExpression), NIL };
static const TrackerGrammarRule helper_BuiltInCall_seq_19[] = { L(UCASE), S(helper_BuiltInCall_seq_1), NIL };
static const TrackerGrammarRule helper_BuiltInCall_seq_20[] = { L(LCASE), S(helper_BuiltInCall_seq_1), NIL };
static const TrackerGrammarRule helper_BuiltInCall_seq_21[] = { L(ENCODE_FOR_URI), S(helper_BuiltInCall_seq_1), NIL };
static const TrackerGrammarRule helper_BuiltInCall_seq_22[] = { L(CONTAINS), L(OPEN_PARENS), R(Expression), L(COMMA), R(Expression), L(CLOSE_PARENS), NIL };
static const TrackerGrammarRule helper_BuiltInCall_seq_23[] = { L(STRSTARTS), L(OPEN_PARENS), R(Expression), L(COMMA), R(Expression), L(CLOSE_PARENS), NIL };
static const TrackerGrammarRule helper_BuiltInCall_seq_24[] = { L(STRENDS), L(OPEN_PARENS), R(Expression), L(COMMA), R(Expression), L(CLOSE_PARENS), NIL };
static const TrackerGrammarRule helper_BuiltInCall_seq_25[] = { L(STRBEFORE), L(OPEN_PARENS), R(Expression), L(COMMA), R(Expression), L(CLOSE_PARENS), NIL };
static const TrackerGrammarRule helper_BuiltInCall_seq_26[] = { L(STRAFTER), L(OPEN_PARENS), R(Expression), L(COMMA), R(Expression), L(CLOSE_PARENS), NIL };
static const TrackerGrammarRule helper_BuiltInCall_seq_27[] = { L(YEAR), S(helper_BuiltInCall_seq_1), NIL };
static const TrackerGrammarRule helper_BuiltInCall_seq_28[] = { L(MONTH), S(helper_BuiltInCall_seq_1), NIL };
static const TrackerGrammarRule helper_BuiltInCall_seq_29[] = { L(DAY), S(helper_BuiltInCall_seq_1), NIL };
static const TrackerGrammarRule helper_BuiltInCall_seq_30[] = { L(HOURS), S(helper_BuiltInCall_seq_1), NIL };
static const TrackerGrammarRule helper_BuiltInCall_seq_31[] = { L(MINUTES), S(helper_BuiltInCall_seq_1), NIL };
static const TrackerGrammarRule helper_BuiltInCall_seq_32[] = { L(SECONDS), S(helper_BuiltInCall_seq_1), NIL };
static const TrackerGrammarRule helper_BuiltInCall_seq_33[] = { L(TIMEZONE), S(helper_BuiltInCall_seq_1), NIL };
static const TrackerGrammarRule helper_BuiltInCall_seq_34[] = { L(TZ), S(helper_BuiltInCall_seq_1), NIL };
static const TrackerGrammarRule helper_BuiltInCall_seq_35[] = { L(NOW), T(NIL), NIL };
static const TrackerGrammarRule helper_BuiltInCall_seq_36[] = { L(UUID), T(NIL), NIL };
static const TrackerGrammarRule helper_BuiltInCall_seq_37[] = { L(STRUUID), T(NIL), NIL };
static const TrackerGrammarRule helper_BuiltInCall_seq_38[] = { L(MD5), S(helper_BuiltInCall_seq_1), NIL };
static const TrackerGrammarRule helper_BuiltInCall_seq_39[] = { L(SHA1), S(helper_BuiltInCall_seq_1), NIL };
static const TrackerGrammarRule helper_BuiltInCall_seq_40[] = { L(SHA256), S(helper_BuiltInCall_seq_1), NIL };
static const TrackerGrammarRule helper_BuiltInCall_seq_41[] = { L(SHA384), S(helper_BuiltInCall_seq_1), NIL };
static const TrackerGrammarRule helper_BuiltInCall_seq_42[] = { L(SHA512), S(helper_BuiltInCall_seq_1), NIL };
static const TrackerGrammarRule helper_BuiltInCall_seq_43[] = { L(COALESCE), R(ExpressionList), NIL };
static const TrackerGrammarRule helper_BuiltInCall_seq_44[] = { L(IF), L(OPEN_PARENS), R(Expression), L(COMMA), R(Expression), L(COMMA), R(Expression), L(CLOSE_PARENS), NIL };
static const TrackerGrammarRule helper_BuiltInCall_seq_45[] = { L(STRLANG), L(OPEN_PARENS), R(Expression), L(COMMA), R(Expression), L(CLOSE_PARENS), NIL };
static const TrackerGrammarRule helper_BuiltInCall_seq_46[] = { L(STRDT), L(OPEN_PARENS), R(Expression), L(COMMA), R(Expression), L(CLOSE_PARENS), NIL };
static const TrackerGrammarRule helper_BuiltInCall_seq_47[] = { L(SAMETERM), L(OPEN_PARENS), R(Expression), L(COMMA), R(Expression), L(CLOSE_PARENS), NIL };
static const TrackerGrammarRule helper_BuiltInCall_seq_48[] = { L(ISIRI), S(helper_BuiltInCall_seq_1), NIL };
static const TrackerGrammarRule helper_BuiltInCall_seq_49[] = { L(ISURI), S(helper_BuiltInCall_seq_1), NIL };
static const TrackerGrammarRule helper_BuiltInCall_seq_50[] = { L(ISBLANK), S(helper_BuiltInCall_seq_1), NIL };
static const TrackerGrammarRule helper_BuiltInCall_seq_51[] = { L(ISLITERAL), S(helper_BuiltInCall_seq_1), NIL };
static const TrackerGrammarRule helper_BuiltInCall_seq_52[] = { L(ISNUMERIC), S(helper_BuiltInCall_seq_1), NIL };
static const TrackerGrammarRule helper_BuiltInCall_seq_53[] = { R(RegexExpression), NIL };
static const TrackerGrammarRule helper_BuiltInCall_seq_54[] = { R(ExistsFunc), NIL };
static const TrackerGrammarRule helper_BuiltInCall_seq_55[] = { R(NotExistsFunc), NIL };
static const TrackerGrammarRule helper_BuiltInCall_seq_56[] = { R(Aggregate), NIL };
static const TrackerGrammarRule helper_BuiltInCall_or_2[] = { S(helper_BuiltInCall_seq_2), S(helper_BuiltInCall_seq_3), S(helper_BuiltInCall_seq_4),
						       S(helper_BuiltInCall_seq_5), S(helper_BuiltInCall_seq_6), S(helper_BuiltInCall_seq_7),
						       S(helper_BuiltInCall_seq_8), S(helper_BuiltInCall_seq_9), S(helper_BuiltInCall_seq_10),
						       S(helper_BuiltInCall_seq_11), S(helper_BuiltInCall_seq_12), S(helper_BuiltInCall_seq_13),
						       S(helper_BuiltInCall_seq_14), S(helper_BuiltInCall_seq_15), S(helper_BuiltInCall_seq_16),
						       S(helper_BuiltInCall_seq_17), S(helper_BuiltInCall_seq_18), S(helper_BuiltInCall_seq_19),
						       S(helper_BuiltInCall_seq_20), S(helper_BuiltInCall_seq_21), S(helper_BuiltInCall_seq_22),
						       S(helper_BuiltInCall_seq_23), S(helper_BuiltInCall_seq_24), S(helper_BuiltInCall_seq_25),
						       S(helper_BuiltInCall_seq_26), S(helper_BuiltInCall_seq_27), S(helper_BuiltInCall_seq_28),
						       S(helper_BuiltInCall_seq_29), S(helper_BuiltInCall_seq_30), S(helper_BuiltInCall_seq_31),
						       S(helper_BuiltInCall_seq_32), S(helper_BuiltInCall_seq_33), S(helper_BuiltInCall_seq_34),
						       S(helper_BuiltInCall_seq_35), S(helper_BuiltInCall_seq_36), S(helper_BuiltInCall_seq_37),
						       S(helper_BuiltInCall_seq_38), S(helper_BuiltInCall_seq_39), S(helper_BuiltInCall_seq_40),
						       S(helper_BuiltInCall_seq_41), S(helper_BuiltInCall_seq_42), S(helper_BuiltInCall_seq_43),
						       S(helper_BuiltInCall_seq_44), S(helper_BuiltInCall_seq_45), S(helper_BuiltInCall_seq_46),
						       S(helper_BuiltInCall_seq_47), S(helper_BuiltInCall_seq_48), S(helper_BuiltInCall_seq_49),
						       S(helper_BuiltInCall_seq_50), S(helper_BuiltInCall_seq_51), S(helper_BuiltInCall_seq_52),
                                                       S(helper_BuiltInCall_seq_53), S(helper_BuiltInCall_seq_54), S(helper_BuiltInCall_seq_55),
                                                       S(helper_BuiltInCall_seq_56), NIL };
static const TrackerGrammarRule rule_BuiltInCall[] = { OR(helper_BuiltInCall_or_2), NIL };

/* BrackettedExpression ::= '(' Expression ')'
 *
 * TRACKER EXTENSION:
 * SubSelect is accepted too, thus the grammar results in:
 * '(' ( Expression | SubSelect) ')'
 */
static const TrackerGrammarRule ext_BrackettedExpression_or[] = { R(Expression), R(SubSelect), NIL };
static const TrackerGrammarRule rule_BrackettedExpression[] = { L(OPEN_PARENS), OR(ext_BrackettedExpression_or), L(CLOSE_PARENS), NIL };

/* iriOrFunction ::= iri ArgList?
 */
static const TrackerGrammarRule helper_iriOrFunction_opt[] = { R(ArgList), NIL };
static const TrackerGrammarRule rule_iriOrFunction[] = { R(iri), OPT(helper_iriOrFunction_opt), NIL };

/* PrimaryExpression ::= BrackettedExpression | BuiltInCall | iriOrFunction | RDFLiteral | NumericLiteral | BooleanLiteral | Var
 */
static const TrackerGrammarRule helper_PrimaryExpression_or[] = { R(BrackettedExpression), R(BuiltInCall), R(iriOrFunction), R(RDFLiteral), R(NumericLiteral), R(BooleanLiteral), R(Var), NIL };
static const TrackerGrammarRule rule_PrimaryExpression[] = { OR(helper_PrimaryExpression_or), NIL };

/* UnaryExpression ::= '!' PrimaryExpression
 *                     | '+' PrimaryExpression
 *                     | '-' PrimaryExpression
 *                     | PrimaryExpression
 */
static const TrackerGrammarRule helper_UnaryExpression_seq_1[] = { L(OP_NEG), R(PrimaryExpression), NIL };
static const TrackerGrammarRule helper_UnaryExpression_seq_2[] = { L(ARITH_PLUS), R(PrimaryExpression), NIL };
static const TrackerGrammarRule helper_UnaryExpression_seq_3[] = { L(ARITH_MINUS), R(PrimaryExpression), NIL };
static const TrackerGrammarRule helper_UnaryExpression_or[] = { S(helper_UnaryExpression_seq_1), S(helper_UnaryExpression_seq_2), S(helper_UnaryExpression_seq_3), R(PrimaryExpression), NIL };
static const TrackerGrammarRule rule_UnaryExpression[] = { OR(helper_UnaryExpression_or), NIL };

/* MultiplicativeExpression ::= UnaryExpression ( '*' UnaryExpression | '/' UnaryExpression )*
 */
static const TrackerGrammarRule helper_MultiplicativeExpression_seq_1[] = { L(ARITH_MULT), R(UnaryExpression), NIL };
static const TrackerGrammarRule helper_MultiplicativeExpression_seq_2[] = { L(ARITH_DIV), R(UnaryExpression), NIL };
static const TrackerGrammarRule helper_MultiplicativeExpression_or[] = { S(helper_MultiplicativeExpression_seq_1), S(helper_MultiplicativeExpression_seq_2), NIL };
static const TrackerGrammarRule helper_MultiplicativeExpression_gte0[] = { OR(helper_MultiplicativeExpression_or), NIL };
static const TrackerGrammarRule rule_MultiplicativeExpression[] = { R(UnaryExpression), GTE0(helper_MultiplicativeExpression_gte0), NIL };

/* AdditiveExpression ::= MultiplicativeExpression ( '+' MultiplicativeExpression | '-' MultiplicativeExpression | ( NumericLiteralPositive | NumericLiteralNegative ) ( ( '*' UnaryExpression ) | ( '/' UnaryExpression ) )* )*
 */
static const TrackerGrammarRule helper_AdditiveExpression_seq_1[] = { L(ARITH_PLUS), R(MultiplicativeExpression), NIL };
static const TrackerGrammarRule helper_AdditiveExpression_seq_2[] = { L(ARITH_MINUS), R(MultiplicativeExpression), NIL };
static const TrackerGrammarRule helper_AdditiveExpression_or_1[] = { R(NumericLiteralPositive), R(NumericLiteralNegative), NIL };
static const TrackerGrammarRule helper_AdditiveExpression_seq_3[] = { L(ARITH_MULT), R(UnaryExpression), NIL };
static const TrackerGrammarRule helper_AdditiveExpression_seq_4[] = { L(ARITH_DIV), R(UnaryExpression), NIL };
static const TrackerGrammarRule helper_AdditiveExpression_or_2[] = { S(helper_AdditiveExpression_seq_3), S(helper_AdditiveExpression_seq_4), NIL };
static const TrackerGrammarRule helper_AdditiveExpression_gte0_1[] = { OR(helper_AdditiveExpression_or_2), NIL };
static const TrackerGrammarRule helper_AdditiveExpression_seq_5[] = { OR(helper_AdditiveExpression_or_1), GTE0(helper_AdditiveExpression_gte0_1), NIL };
static const TrackerGrammarRule helper_AdditiveExpression_or_3[] = { S(helper_AdditiveExpression_seq_1), S(helper_AdditiveExpression_seq_2), S(helper_AdditiveExpression_seq_5), NIL };
static const TrackerGrammarRule helper_AdditiveExpression_gte0_2[] = { OR(helper_AdditiveExpression_or_3), NIL };
static const TrackerGrammarRule rule_AdditiveExpression[] = { R(MultiplicativeExpression), GTE0(helper_AdditiveExpression_gte0_2), NIL };

/* NumericExpression ::= AdditiveExpression
 */
static const TrackerGrammarRule rule_NumericExpression[] = { R(AdditiveExpression), NIL };

/* RelationalExpression ::= NumericExpression ( '=' NumericExpression | '!=' NumericExpression | '<' NumericExpression | '>' NumericExpression | '<=' NumericExpression | '>=' NumericExpression | 'IN' ExpressionList | 'NOT' 'IN' ExpressionList )?
 */
static const TrackerGrammarRule helper_RelationalExpression_seq_1[] = { L(OP_EQ), R(NumericExpression), NIL };
static const TrackerGrammarRule helper_RelationalExpression_seq_2[] = { L(OP_NE), R(NumericExpression), NIL };
static const TrackerGrammarRule helper_RelationalExpression_seq_3[] = { L(OP_LT), R(NumericExpression), NIL };
static const TrackerGrammarRule helper_RelationalExpression_seq_4[] = { L(OP_GT), R(NumericExpression), NIL };
static const TrackerGrammarRule helper_RelationalExpression_seq_5[] = { L(OP_LE), R(NumericExpression), NIL };
static const TrackerGrammarRule helper_RelationalExpression_seq_6[] = { L(OP_GE), R(NumericExpression), NIL };
static const TrackerGrammarRule helper_RelationalExpression_seq_7[] = { L(OP_IN), R(ExpressionList), NIL };
static const TrackerGrammarRule helper_RelationalExpression_seq_8[] = { L(NOT), L(OP_IN), R(ExpressionList), NIL };
static const TrackerGrammarRule helper_RelationalExpression_or[] = { S(helper_RelationalExpression_seq_1), S(helper_RelationalExpression_seq_2), S(helper_RelationalExpression_seq_5), S(helper_RelationalExpression_seq_6), S(helper_RelationalExpression_seq_3), S(helper_RelationalExpression_seq_4), S(helper_RelationalExpression_seq_7), S(helper_RelationalExpression_seq_8), NIL };
static const TrackerGrammarRule helper_RelationalExpression_opt[] = { OR(helper_RelationalExpression_or), NIL };
static const TrackerGrammarRule rule_RelationalExpression[] = { R(NumericExpression), OPT(helper_RelationalExpression_opt), NIL };

/* ValueLogical ::= RelationalExpression
 */
static const TrackerGrammarRule rule_ValueLogical[] = { R(RelationalExpression), NIL };

/* ConditionalAndExpression ::= ValueLogical ( '&&' ValueLogical )*
 */
static const TrackerGrammarRule helper_ConditionalAndExpression_seq[] = { L(OP_AND), R(ValueLogical), NIL };
static const TrackerGrammarRule helper_ConditionalAndExpression_gte0[] = { S(helper_ConditionalAndExpression_seq), NIL };
static const TrackerGrammarRule rule_ConditionalAndExpression[] = { R(ValueLogical), GTE0(helper_ConditionalAndExpression_gte0), NIL };

/* ConditionalOrExpression ::= ConditionalAndExpression ( '||' ConditionalAndExpression )*
 */
static const TrackerGrammarRule helper_ConditionalOrExpression_seq[] = { L(OP_OR), R(ConditionalAndExpression), NIL };
static const TrackerGrammarRule helper_ConditionalOrExpression_gte0[] = { S(helper_ConditionalOrExpression_seq), NIL };
static const TrackerGrammarRule rule_ConditionalOrExpression[] = { R(ConditionalAndExpression), GTE0(helper_ConditionalOrExpression_gte0), NIL };

/* Expression ::= ConditionalOrExpression
 */
static const TrackerGrammarRule rule_Expression[] = { R(ConditionalOrExpression), NIL };

/* GraphTerm ::= iri | RDFLiteral | NumericLiteral | BooleanLiteral | BlankNode | NIL
 */
static const TrackerGrammarRule helper_GraphTerm_or[] = { R(iri), R(RDFLiteral), R(NumericLiteral), R(BooleanLiteral), R(BlankNode), T(NIL), NIL };
static const TrackerGrammarRule rule_GraphTerm[] = { OR(helper_GraphTerm_or), NIL };

/* Var ::= VAR1 | VAR2
 */
static const TrackerGrammarRule helper_Var_or[] = { T(VAR1), T(VAR2), NIL };
static const TrackerGrammarRule rule_Var[] = { OR(helper_Var_or), NIL };

/* VarOrIri ::= Var | iri
 */
static const TrackerGrammarRule helper_VarOrIri_or[] = { R(Var), R(iri), NIL };
static const TrackerGrammarRule rule_VarOrIri[] = { OR(helper_VarOrIri_or), NIL };

/* VarOrTerm ::= Var | GraphTerm
 */
static const TrackerGrammarRule helper_VarOrTerm_or[] = { R(Var), R(GraphTerm), NIL };
static const TrackerGrammarRule rule_VarOrTerm[] = { OR(helper_VarOrTerm_or), NIL };

/* GraphNodePath ::= VarOrTerm | TriplesNodePath
 */
static const TrackerGrammarRule helper_GraphNodePath_or[] = { R(VarOrTerm), R(TriplesNodePath), NIL };
static const TrackerGrammarRule rule_GraphNodePath[] = { OR(helper_GraphNodePath_or), NIL };

/* GraphNode ::= VarOrTerm | TriplesNode
 *
 * TRACKER EXTENSION:
 * Literal 'NULL' is also accepted, rule is effectively:
 *   VarOrTerm | TriplesNode | 'NULL'
 */
static const TrackerGrammarRule helper_GraphNode_or[] = { R(VarOrTerm), R(TriplesNode), L(NULL), NIL };
static const TrackerGrammarRule rule_GraphNode[] = { OR(helper_GraphNode_or), NIL };

/* CollectionPath ::= '(' GraphNodePath+ ')'
 */
static const TrackerGrammarRule helper_CollectionPath_gt0[] = { R(GraphNodePath), NIL };
static const TrackerGrammarRule rule_CollectionPath[] = { L(OPEN_PARENS), GT0(helper_CollectionPath_gt0), L(CLOSE_PARENS), NIL };

/* Collection ::= '(' GraphNode+ ')'
 */
static const TrackerGrammarRule helper_Collection_gt0[] = { R(GraphNode), NIL };
static const TrackerGrammarRule rule_Collection[] = { L(OPEN_PARENS), GT0(helper_Collection_gt0), L(CLOSE_PARENS), NIL };

/* BlankNodePropertyListPath ::= '[' PropertyListPathNotEmpty ']'
 */
static const TrackerGrammarRule rule_BlankNodePropertyListPath[] = { L(OPEN_BRACKET), R(PropertyListPathNotEmpty), L(CLOSE_BRACKET), NIL };

/* TriplesNodePath ::= CollectionPath | BlankNodePropertyListPath
 */
static const TrackerGrammarRule helper_TriplesNodePath_or[] = { R(CollectionPath), R(BlankNodePropertyListPath), NIL };
static const TrackerGrammarRule rule_TriplesNodePath[] = { OR(helper_TriplesNodePath_or ), NIL };

/* BlankNodePropertyList ::= '[' PropertyListNotEmpty ']'
 */
static const TrackerGrammarRule rule_BlankNodePropertyList[] = { L(OPEN_BRACKET), R(PropertyListNotEmpty), L(CLOSE_BRACKET), NIL };

/* TriplesNode ::= Collection |	BlankNodePropertyList
 */
static const TrackerGrammarRule helper_TriplesNode_or[] = { R(Collection), R(BlankNodePropertyList), NIL };
static const TrackerGrammarRule rule_TriplesNode[] = { OR(helper_TriplesNode_or), NIL };

/* Integer ::= INTEGER
 */
static const TrackerGrammarRule rule_Integer[] = { T(INTEGER), NIL };

/* PathOneInPropertySet ::= iri | 'a' | '^' ( iri | 'a' )
 */
static const TrackerGrammarRule helper_PathOneInPropertySet_or_1[] = { R(iri), L(A), NIL };
static const TrackerGrammarRule helper_PathOneInPropertySet_seq[] = { L(PATH_INVERSE), OR(helper_PathOneInPropertySet_or_1), NIL };
static const TrackerGrammarRule helper_PathOneInPropertySet_or_2[] = { R(iri), L(A), S(helper_PathOneInPropertySet_seq), NIL };
static const TrackerGrammarRule rule_PathOneInPropertySet[] = { OR(helper_PathOneInPropertySet_or_2), NIL };

/* PathNegatedPropertySet ::= PathOneInPropertySet | '(' ( PathOneInPropertySet ( '|' PathOneInPropertySet )* )? ')'
 */
static const TrackerGrammarRule helper_PathNegatedPropertySet_seq_1[] = { L(PATH_ALTERNATIVE), R(PathOneInPropertySet), NIL };
static const TrackerGrammarRule helper_PathNegatedPropertySet_gte0[] = { S(helper_PathNegatedPropertySet_seq_1), NIL };
static const TrackerGrammarRule helper_PathNegatedPropertySet_seq_2[] = { R(PathOneInPropertySet), GTE0(helper_PathNegatedPropertySet_gte0), NIL };
static const TrackerGrammarRule helper_PathNegatedPropertySet_opt[] = { S(helper_PathNegatedPropertySet_seq_2), NIL };
static const TrackerGrammarRule helper_PathNegatedPropertySet_seq_3[] = { L(OPEN_PARENS), OPT(helper_PathNegatedPropertySet_opt), L(CLOSE_PARENS), NIL };
static const TrackerGrammarRule helper_PathNegatedPropertySet_or[] = { R(PathOneInPropertySet), S(helper_PathNegatedPropertySet_seq_3), NIL };
static const TrackerGrammarRule rule_PathNegatedPropertySet[] = { OR(helper_PathNegatedPropertySet_or), NIL };

/* PathPrimary ::= iri | 'a' | '!' PathNegatedPropertySet | '(' Path ')'
 */
static const TrackerGrammarRule helper_PathPrimary_seq_1[] = { L(OP_NEG), R(PathNegatedPropertySet), NIL };
static const TrackerGrammarRule helper_PathPrimary_seq_2[] = { L(OPEN_PARENS), R(Path), L(CLOSE_PARENS), NIL };
static const TrackerGrammarRule helper_PathPrimary_or[] = { R(iri), L(A), S(helper_PathPrimary_seq_1), S(helper_PathPrimary_seq_2), NIL };
static const TrackerGrammarRule rule_PathPrimary[] = { OR(helper_PathPrimary_or), NIL };

/* PathMod ::= '?' | '*' | '+'
 */
static const TrackerGrammarRule helper_PathMod_or[] = { L(PATH_OPTIONAL), L(PATH_STAR), L(PATH_PLUS), NIL };
static const TrackerGrammarRule rule_PathMod[] = { OR(helper_PathMod_or), NIL };

/* PathElt ::= PathPrimary PathMod?
 */
static const TrackerGrammarRule helper_PathElt_opt[] = { R(PathMod), NIL };
static const TrackerGrammarRule rule_PathElt[] = { R(PathPrimary), OPT(helper_PathElt_opt), NIL };

/* PathEltOrInverse ::= PathElt | '^' PathElt
 */
static const TrackerGrammarRule helper_PathEltOrInverse_seq[] = { L(PATH_INVERSE), R(PathElt), NIL };
static const TrackerGrammarRule helper_PathEltOrInverse_or[] = { R(PathElt), S(helper_PathEltOrInverse_seq), NIL };
static const TrackerGrammarRule rule_PathEltOrInverse[] = { OR(helper_PathEltOrInverse_or), NIL };

/* PathSequence ::= PathEltOrInverse ( '/' PathEltOrInverse )*
 */
static const TrackerGrammarRule helper_PathSequence_seq[] = { L(PATH_SEQUENCE), R(PathEltOrInverse), NIL };
static const TrackerGrammarRule helper_PathSequence_gte0[] = { S(helper_PathSequence_seq), NIL };
static const TrackerGrammarRule rule_PathSequence[] = { R(PathEltOrInverse), GTE0(helper_PathSequence_gte0), NIL };

/* PathAlternative ::= PathSequence ( '|' PathSequence )*
 */
static const TrackerGrammarRule helper_PathAlternative_seq[] = { L(PATH_ALTERNATIVE), R(PathSequence), NIL };
static const TrackerGrammarRule helper_PathAlternative_gte0[] = { S(helper_PathAlternative_seq), NIL };
static const TrackerGrammarRule rule_PathAlternative[] =  { R(PathSequence), GTE0(helper_PathAlternative_gte0), NIL };

/* Path ::= PathAlternative
 */
static const TrackerGrammarRule rule_Path[] = { R(PathAlternative), NIL };

/* ObjectPath ::= GraphNodePath
 */
static const TrackerGrammarRule rule_ObjectPath[] = { R(GraphNodePath), NIL };

/* ObjectListPath ::= ObjectPath ( ',' ObjectPath )*
 */
static const TrackerGrammarRule helper_ObjectListPath_seq[] = { L(COMMA), R(ObjectPath), NIL };
static const TrackerGrammarRule helper_ObjectListPath_gte0[] = { S(helper_ObjectListPath_seq), NIL };
static const TrackerGrammarRule rule_ObjectListPath[] = { R(ObjectPath), GTE0(helper_ObjectListPath_gte0), NIL };

/* VerbSimple ::= Var
 */
static const TrackerGrammarRule rule_VerbSimple[] = { R(Var), NIL };

/* VerbPath ::= Path
 */
static const TrackerGrammarRule rule_VerbPath[] = { R(Path), NIL };

/* PropertyListPathNotEmpty ::= ( VerbPath | VerbSimple ) ObjectListPath ( ';' ( ( VerbPath | VerbSimple ) ObjectList )? )*
 */
static const TrackerGrammarRule helper_PropertyListPathNotEmpty_or_1[] = { R(VerbPath), R(VerbSimple), NIL };
static const TrackerGrammarRule helper_PropertyListPathNotEmpty_seq_1[] = { OR(helper_PropertyListPathNotEmpty_or_1), R(ObjectList), NIL };
static const TrackerGrammarRule helper_PropertyListPathNotEmpty_opt[] = { S(helper_PropertyListPathNotEmpty_seq_1), NIL };
static const TrackerGrammarRule helper_PropertyListPathNotEmpty_seq_2[] = { L(SEMICOLON), OPT(helper_PropertyListPathNotEmpty_opt), NIL };
static const TrackerGrammarRule helper_PropertyListPathNotEmpty_gte0[] = { S(helper_PropertyListPathNotEmpty_seq_2), NIL };
static const TrackerGrammarRule rule_PropertyListPathNotEmpty[] = { OR(helper_PropertyListPathNotEmpty_or_1), R(ObjectListPath), GTE0(helper_PropertyListPathNotEmpty_gte0), NIL };

/* PropertyListPath ::= PropertyListPathNotEmpty?
 */
static const TrackerGrammarRule helper_PropertyListPath_opt[] = { R(PropertyListPathNotEmpty), NIL };
static const TrackerGrammarRule rule_PropertyListPath[] = { OPT(helper_PropertyListPath_opt), NIL };

/* TriplesSameSubjectPath ::= VarOrTerm PropertyListPathNotEmpty | TriplesNodePath PropertyListPath
 */
static const TrackerGrammarRule helper_TriplesSameSubjectPath_seq_1[] = { R(VarOrTerm), R(PropertyListPathNotEmpty), NIL };
static const TrackerGrammarRule helper_TriplesSameSubjectPath_seq_2[] = { R(TriplesNodePath), R(PropertyListPath), NIL };
static const TrackerGrammarRule helper_TriplesSameSubjectPath_or[] = { S(helper_TriplesSameSubjectPath_seq_1), S(helper_TriplesSameSubjectPath_seq_2), NIL };
static const TrackerGrammarRule rule_TriplesSameSubjectPath[] = { OR(helper_TriplesSameSubjectPath_or), NIL };

/* Object ::= GraphNode
 */
static const TrackerGrammarRule rule_Object[] = { R(GraphNode), NIL };

/* ObjectList ::= Object ( ',' Object )*
 */
static const TrackerGrammarRule helper_ObjectList_seq[] = { L(COMMA), R(Object), NIL };
static const TrackerGrammarRule helper_ObjectList_gte0[] = { S(helper_ObjectList_seq), NIL };
static const TrackerGrammarRule rule_ObjectList[] = { R(Object), GTE0(helper_ObjectList_gte0), NIL };

/* Verb ::= VarOrIri | 'a'
 */
static const TrackerGrammarRule helper_Verb_or[] = { R(VarOrIri), L(A), NIL };
static const TrackerGrammarRule rule_Verb[] = { OR(helper_Verb_or), NIL };

/* PropertyListNotEmpty ::= Verb ObjectList ( ';' ( Verb ObjectList )? )*
 */
static const TrackerGrammarRule helper_PropertyListNotEmpty_seq_1[] = { R(Verb), R(ObjectList), NIL };
static const TrackerGrammarRule helper_PropertyListNotEmpty_opt[] = { S(helper_PropertyListNotEmpty_seq_1), NIL };
static const TrackerGrammarRule helper_PropertyListNotEmpty_seq_2[] = { L(SEMICOLON), OPT(helper_PropertyListNotEmpty_opt), NIL };
static const TrackerGrammarRule helper_PropertyListNotEmpty_gte0[] = { S(helper_PropertyListNotEmpty_seq_2), NIL };
static const TrackerGrammarRule rule_PropertyListNotEmpty[] = { R(Verb), R(ObjectList), GTE0(helper_PropertyListNotEmpty_gte0), NIL };

/* PropertyList ::= PropertyListNotEmpty?
 */
static const TrackerGrammarRule helper_PropertyList_opt[] = { R(PropertyListNotEmpty), NIL };
static const TrackerGrammarRule rule_PropertyList[] = { OPT(helper_PropertyList_opt), NIL };

/* TriplesSameSubject ::= VarOrTerm PropertyListNotEmpty | TriplesNode PropertyList
 */
static const TrackerGrammarRule helper_TriplesSameSubject_seq_1[] = { R(VarOrTerm), R(PropertyListNotEmpty), NIL };
static const TrackerGrammarRule helper_TriplesSameSubject_seq_2[] = { R(TriplesNode), R(PropertyList), NIL };
static const TrackerGrammarRule helper_TriplesSameSubject_or[] = { S(helper_TriplesSameSubject_seq_1), S(helper_TriplesSameSubject_seq_2), NIL };
static const TrackerGrammarRule rule_TriplesSameSubject[] = { OR(helper_TriplesSameSubject_or), NIL };

/* ConstructTriples ::= TriplesSameSubject ( '.' ConstructTriples? )?
 */
static const TrackerGrammarRule helper_ConstructTriples_opt_1[] = { R(ConstructTriples), NIL };
static const TrackerGrammarRule helper_ConstructTriples_seq[] = { L(DOT), OPT(helper_ConstructTriples_opt_1), NIL };
static const TrackerGrammarRule helper_ConstructTriples_opt_2[] = { S(helper_ConstructTriples_seq), NIL };
static const TrackerGrammarRule rule_ConstructTriples[] = { R(TriplesSameSubject), OPT(helper_ConstructTriples_opt_2), NIL };

/* ConstructTemplate ::= '{' ConstructTriples? '}'
 */
static const TrackerGrammarRule helper_ConstructTemplate_opt[] = { R(ConstructTriples), NIL };
static const TrackerGrammarRule rule_ConstructTemplate[] = { L(OPEN_BRACE), OPT(helper_ConstructTemplate_opt), L(CLOSE_BRACE), NIL };

/* ExpressionList ::= NIL | '(' Expression ( ',' Expression )* ')'
 */
static const TrackerGrammarRule helper_ExpressionList_seq_1[] = { L(COMMA), R(Expression), NIL };
static const TrackerGrammarRule helper_ExpressionList_gte0[] = { S(helper_ExpressionList_seq_1), NIL };
static const TrackerGrammarRule helper_ExpressionList_seq_2[] = {  L(OPEN_PARENS), R(Expression), GTE0(helper_ExpressionList_gte0), L(CLOSE_PARENS), NIL };
static const TrackerGrammarRule helper_ExpressionList_or[] = { T(NIL), S(helper_ExpressionList_seq_2), NIL };
static const TrackerGrammarRule rule_ExpressionList[] = { OR(helper_ExpressionList_or), NIL };

/* ArgList ::= NIL | '(' 'DISTINCT'? Expression ( ',' Expression )* ')'
 *
 * TRACKER EXTENSION:
 * First argument may be an ArgList in itself for fn:string-join, resulting in:
 * ( ArgList | 'DISTINCT'? Expression )
 */
static const TrackerGrammarRule helper_ArgList_seq_1[] = { L(COMMA), R(Expression), NIL };
static const TrackerGrammarRule helper_ArgList_gte0[] = { S(helper_ArgList_seq_1), NIL };
static const TrackerGrammarRule helper_ArgList_opt[] = { L(DISTINCT), NIL };
static const TrackerGrammarRule helper_ArgList_seq_3[] = { OPT(helper_ArgList_opt), R(Expression), NIL };
static const TrackerGrammarRule helper_ArgList_or_2[] = { S(helper_ArgList_seq_3), R(ArgList), NIL };
static const TrackerGrammarRule helper_ArgList_seq_2[] = {  L(OPEN_PARENS), OR(helper_ArgList_or_2), GTE0(helper_ArgList_gte0), L(CLOSE_PARENS), NIL };
static const TrackerGrammarRule helper_ArgList_or[] = { T(NIL), S(helper_ArgList_seq_2), NIL };
static const TrackerGrammarRule rule_ArgList[] = { OR(helper_ArgList_or), NIL };

/* FunctionCall ::= iri ArgList
 */
static const TrackerGrammarRule rule_FunctionCall[] = { R(iri), R(ArgList), NIL };

/* Constraint ::= BrackettedExpression | BuiltInCall | FunctionCall
 */
static const TrackerGrammarRule helper_Constraint_or[] = { R(BrackettedExpression), R(BuiltInCall), R(FunctionCall), NIL };
static const TrackerGrammarRule rule_Constraint[] = { OR(helper_Constraint_or), NIL };

/* Filter ::= 'FILTER' Constraint
 */
static const TrackerGrammarRule rule_Filter[] = { L(FILTER), R(Constraint), NIL };

/* GroupOrUnionGraphPattern ::= GroupGraphPattern ( 'UNION' GroupGraphPattern )*
 */
static const TrackerGrammarRule helper_GroupOrUnionGraphPattern_seq[] = { L(UNION), R(GroupGraphPattern), NIL };
static const TrackerGrammarRule helper_GroupOrUnionGraphPattern_gte0[] = { S(helper_GroupOrUnionGraphPattern_seq), NIL };
static const TrackerGrammarRule rule_GroupOrUnionGraphPattern[] = { R(GroupGraphPattern), GTE0(helper_GroupOrUnionGraphPattern_gte0), NIL };

/* MinusGraphPattern ::= 'MINUS' GroupGraphPattern
 */
static const TrackerGrammarRule rule_MinusGraphPattern[] = { L(MINUS), R(GroupGraphPattern), NIL };

/* DataBlockValue ::= iri | RDFLiteral | NumericLiteral | BooleanLiteral | 'UNDEF'
 */
static const TrackerGrammarRule helper_DataBlockValue_or[] = { R(iri), R(RDFLiteral), R(NumericLiteral), R(BooleanLiteral), L(UNDEF), NIL };
static const TrackerGrammarRule rule_DataBlockValue[] = { OR(helper_DataBlockValue_or), NIL };

/* InlineDataFull ::= ( NIL | '(' Var* ')' ) '{' ( '(' DataBlockValue* ')' | NIL )* '}'
 */
static const TrackerGrammarRule helper_InlineDataFull_gte0_1[] = { R(Var), NIL };
static const TrackerGrammarRule helper_InlineDataFull_seq_1[] = { L(OPEN_PARENS), GTE0(helper_InlineDataFull_gte0_1), L(CLOSE_PARENS), NIL };
static const TrackerGrammarRule helper_InlineDataFull_or_1[] = { T(NIL), S(helper_InlineDataFull_seq_1), NIL };
static const TrackerGrammarRule helper_InlineDataFull_gte0_2[] = { R(DataBlockValue), NIL };
static const TrackerGrammarRule helper_InlineDataFull_seq_2[] = { L(OPEN_PARENS), GTE0(helper_InlineDataFull_gte0_2), L(CLOSE_PARENS), NIL };
static const TrackerGrammarRule helper_InlineDataFull_or_2[] = { S(helper_InlineDataFull_seq_2), T(NIL), NIL };
static const TrackerGrammarRule helper_InlineDataFull_gte0_3[] = { OR(helper_InlineDataFull_or_2), NIL };
static const TrackerGrammarRule rule_InlineDataFull[] = { OR(helper_InlineDataFull_or_1), L(OPEN_BRACE), GTE0(helper_InlineDataFull_gte0_3), L(CLOSE_BRACE), NIL };

/* InlineDataOneVar ::= Var '{' DataBlockValue* '}'
 */
static const TrackerGrammarRule helper_InlineDataOneVar_gte0[] = { R(DataBlockValue), NIL };
static const TrackerGrammarRule rule_InlineDataOneVar[] = { R(Var), L(OPEN_BRACE), GTE0(helper_InlineDataOneVar_gte0), L(CLOSE_BRACE), NIL };

/* DataBlock ::= InlineDataOneVar | InlineDataFull
 */
static const TrackerGrammarRule helper_DataBlock_or[] = { R(InlineDataOneVar), R(InlineDataFull), NIL };
static const TrackerGrammarRule rule_DataBlock[] = { OR(helper_DataBlock_or), NIL };

/* InlineData ::= 'VALUES' DataBlock
 */
static const TrackerGrammarRule rule_InlineData[] = { L(VALUES), R(DataBlock), NIL };

/* Bind ::= 'BIND' '(' Expression 'AS' Var ')'
 */
static const TrackerGrammarRule rule_Bind[] = { L(BIND), L(OPEN_PARENS), R(Expression), L(AS), R(Var), L(CLOSE_PARENS), NIL };

/* ServiceGraphPattern ::= 'SERVICE' 'SILENT'? VarOrIri GroupGraphPattern
 */
static const TrackerGrammarRule helper_ServiceGraphPattern_opt[] = { L(SILENT), NIL };
static const TrackerGrammarRule rule_ServiceGraphPattern[] = { L(SERVICE), OPT(helper_ServiceGraphPattern_opt), R(VarOrIri), R(GroupGraphPattern), NIL };

/* GraphGraphPattern ::= 'GRAPH' VarOrIri GroupGraphPattern
 */
static const TrackerGrammarRule rule_GraphGraphPattern[] = { L(GRAPH), R(VarOrIri), R(GroupGraphPattern), NIL };

/* OptionalGraphPattern ::= 'OPTIONAL' GroupGraphPattern
 */
static const TrackerGrammarRule rule_OptionalGraphPattern[] = { L(OPTIONAL), R(GroupGraphPattern), NIL };

/* GraphPatternNotTriples ::= GroupOrUnionGraphPattern | OptionalGraphPattern | MinusGraphPattern | GraphGraphPattern | ServiceGraphPattern | Filter | Bind | InlineData
 */
static const TrackerGrammarRule helper_GraphPatternNotTriples_or[] = { R(GroupOrUnionGraphPattern), R(OptionalGraphPattern), R(MinusGraphPattern), R(GraphGraphPattern), R(ServiceGraphPattern), R(Filter), R(Bind), R(InlineData), NIL };
static const TrackerGrammarRule rule_GraphPatternNotTriples[] = { OR(helper_GraphPatternNotTriples_or), NIL };

/* TriplesBlock ::= TriplesSameSubjectPath ( '.' TriplesBlock? )?
 */
static const TrackerGrammarRule helper_TriplesBlock_opt_1[] = { R(TriplesBlock), NIL };
static const TrackerGrammarRule helper_TriplesBlock_seq[] = { L(DOT), OPT(helper_TriplesBlock_opt_1), NIL };
static const TrackerGrammarRule helper_TriplesBlock_opt_2[] = { S(helper_TriplesBlock_seq), NIL };
static const TrackerGrammarRule rule_TriplesBlock[] = { R(TriplesSameSubjectPath), OPT(helper_TriplesBlock_opt_2), NIL };

/* GroupGraphPatternSub ::= TriplesBlock? ( GraphPatternNotTriples '.'? TriplesBlock? )*
 */
static const TrackerGrammarRule helper_GroupGraphPatternSub_opt_1[] = { R(TriplesBlock), NIL };
static const TrackerGrammarRule helper_GroupGraphPatternSub_opt_2[] = { L(DOT), NIL };
static const TrackerGrammarRule helper_GroupGraphPatternSub_seq[] = { R(GraphPatternNotTriples), OPT(helper_GroupGraphPatternSub_opt_2), OPT(helper_GroupGraphPatternSub_opt_1), NIL };
static const TrackerGrammarRule helper_GroupGraphPatternSub_gte0[] = { S(helper_GroupGraphPatternSub_seq), NIL };
static const TrackerGrammarRule rule_GroupGraphPatternSub[] =  { OPT(helper_GroupGraphPatternSub_opt_1), GTE0(helper_GroupGraphPatternSub_gte0), NIL };

/* GroupGraphPattern ::= '{' ( SubSelect | GroupGraphPatternSub ) '}'
 */
static const TrackerGrammarRule helper_GroupGraphPattern_or[] = { R(SubSelect), R(GroupGraphPatternSub), NIL };
static const TrackerGrammarRule rule_GroupGraphPattern[] = { L(OPEN_BRACE), OR(helper_GroupGraphPattern_or), L(CLOSE_BRACE), NIL };

/* TriplesTemplate ::= TriplesSameSubject ( '.' TriplesTemplate? )?
 */
static const TrackerGrammarRule helper_TriplesTemplate_opt_2[] = { R(TriplesTemplate), NIL };
static const TrackerGrammarRule helper_TriplesTemplate_seq[] = { L(DOT), OPT(helper_TriplesTemplate_opt_2), NIL };
static const TrackerGrammarRule helper_TriplesTemplate_opt_1[] = { S(helper_TriplesTemplate_seq), NIL };
static const TrackerGrammarRule rule_TriplesTemplate[] =  { R(TriplesSameSubject), OPT(helper_TriplesTemplate_opt_1), NIL };

/* QuadsNotTriples ::= 'GRAPH' VarOrIri '{' TriplesTemplate? '}'
 */
static const TrackerGrammarRule helper_QuadsNotTriples_opt[] = { R(TriplesTemplate), NIL };
static const TrackerGrammarRule rule_QuadsNotTriples[] = { L(GRAPH), R(VarOrIri), L(OPEN_BRACE), OPT(helper_QuadsNotTriples_opt), L(CLOSE_BRACE), NIL };

/* Quads ::= TriplesTemplate? ( QuadsNotTriples '.'? TriplesTemplate? )*
 */
static const TrackerGrammarRule helper_Quads_opt_3[] = { R(TriplesTemplate), NIL };
static const TrackerGrammarRule helper_Quads_opt_1[] = { L(DOT), NIL };
static const TrackerGrammarRule helper_Quads_opt_2[] = { R(TriplesTemplate), NIL };
static const TrackerGrammarRule helper_Quads_seq[] = { R(QuadsNotTriples), OPT(helper_Quads_opt_1), OPT(helper_Quads_opt_2), NIL };
static const TrackerGrammarRule helper_Quads_gte0[] = { S(helper_Quads_seq), NIL };
static const TrackerGrammarRule rule_Quads[] = { OPT(helper_Quads_opt_3), GTE0(helper_Quads_gte0), NIL };

/* QuadData ::= '{' Quads '}'
 */
static const TrackerGrammarRule rule_QuadData[] = { L(OPEN_BRACE), R(Quads), L(CLOSE_BRACE), NIL };

/* QuadPattern ::= '{' Quads '}'
 */
static const TrackerGrammarRule rule_QuadPattern[] = { L(OPEN_BRACE), R(Quads), L(CLOSE_BRACE), NIL };

/* GraphRef ::= 'GRAPH' iri
 */
static const TrackerGrammarRule rule_GraphRef[] = { L(GRAPH), R(iri), NIL };

/* GraphRefAll ::= GraphRef | 'DEFAULT' | 'NAMED' | 'ALL'
 */
static const TrackerGrammarRule helper_GraphRefAll_or[] = { R(GraphRef), L(DEFAULT), L(NAMED), L(ALL), NIL };
static const TrackerGrammarRule rule_GraphRefAll[] = { OR(helper_GraphRefAll_or), NIL };

/* GraphOrDefault ::= 'DEFAULT' | 'GRAPH'? iri
 */
static const TrackerGrammarRule helper_GraphOrDefault_seq[] = { L(NAMED), R(iri), NIL };
static const TrackerGrammarRule helper_GraphOrDefault_or[] = { L(DEFAULT), S(helper_GraphOrDefault_seq), NIL };
static const TrackerGrammarRule rule_GraphOrDefault[] = { OR(helper_GraphOrDefault_or), NIL };

/* UsingClause ::= 'USING' ( iri | 'NAMED' iri )
 */
static const TrackerGrammarRule helper_UsingClause_seq[] = { L(NAMED), R(iri), NIL };
static const TrackerGrammarRule helper_UsingClause_or[] = { R(iri), S(helper_UsingClause_seq), NIL };
static const TrackerGrammarRule rule_UsingClause[] = { L(USING), OR(helper_UsingClause_or), NIL };

/* InsertClause ::= 'INSERT' QuadPattern
 *
 * TRACKER EXTENSION:
 * Clause may start with:
 * 'INSERT' ('OR' 'REPLACE')? ('SILENT')? ('INTO' iri)?
 */
static const TrackerGrammarRule helper_InsertClause_seq_1[] = { L(OR), L(REPLACE), NIL };
static const TrackerGrammarRule helper_InsertClause_opt_1[] = { S(helper_InsertClause_seq_1), NIL };
static const TrackerGrammarRule helper_InsertClause_opt_2[] = { L(SILENT), NIL };
static const TrackerGrammarRule helper_InsertClause_seq_2[] = { L(INTO), R(iri), NIL };
static const TrackerGrammarRule helper_InsertClause_opt_3[] = { S(helper_InsertClause_seq_2), NIL };
static const TrackerGrammarRule rule_InsertClause[] = { L(INSERT), OPT(helper_InsertClause_opt_1), OPT(helper_InsertClause_opt_2), OPT(helper_InsertClause_opt_3), R(QuadPattern), NIL };

/* DeleteClause ::= 'DELETE' QuadPattern
 *
 * TRACKER EXTENSION:
 * Clause may start too with:
 * 'DELETE' 'SILENT'
 */
static const TrackerGrammarRule helper_DeleteClause_opt_1[] = { L(SILENT), NIL };
static const TrackerGrammarRule rule_DeleteClause[] = { L(DELETE), OPT(helper_DeleteClause_opt_1), R(QuadPattern), NIL };

/* Modify ::= ( 'WITH' iri )? ( DeleteClause InsertClause? | InsertClause ) UsingClause* 'WHERE' GroupGraphPattern
 *
 * TRACKER EXTENSION:
 * Last part of the clause is:
 * ('WHERE' GroupGraphPattern)?
 */
static const TrackerGrammarRule helper_Modify_seq_1[] = { L(WITH), R(iri), NIL };
static const TrackerGrammarRule helper_Modify_opt_1[] = { S(helper_Modify_seq_1), NIL };
static const TrackerGrammarRule helper_Modify_opt_2[] = { R(InsertClause), NIL };
static const TrackerGrammarRule helper_Modify_seq_2[] = { R(DeleteClause), OPT(helper_Modify_opt_2), NIL };
static const TrackerGrammarRule helper_Modify_or[] = { S(helper_Modify_seq_2), R(InsertClause), NIL };
static const TrackerGrammarRule helper_Modify_gte0[] = { R(UsingClause), NIL };
static const TrackerGrammarRule helper_Modify_seq_3[] = { L(WHERE), R(GroupGraphPattern), NIL };
static const TrackerGrammarRule helper_Modify_opt_3[] = { S(helper_Modify_seq_3), NIL };
static const TrackerGrammarRule rule_Modify[] = { OPT(helper_Modify_opt_1), OR(helper_Modify_or), GTE0(helper_Modify_gte0), OPT(helper_Modify_opt_3), NIL };

/* DeleteWhere ::= 'DELETE WHERE' QuadPattern
 */
static const TrackerGrammarRule rule_DeleteWhere[] = { L(DELETE), L(WHERE), R(QuadPattern), NIL };

/* DeleteData ::= 'DELETE DATA' QuadData
 */
static const TrackerGrammarRule rule_DeleteData[] = { L(DELETE), L(DATA), R(QuadData), NIL };

/* InsertData ::= 'INSERT DATA' QuadData
 */
static const TrackerGrammarRule rule_InsertData[] = { L(INSERT), L(DATA), R(QuadData), NIL };

/* Copy ::= 'COPY' 'SILENT'? GraphOrDefault 'TO' GraphOrDefault
 */
static const TrackerGrammarRule helper_Copy_opt[] = { L(SILENT), NIL };
static const TrackerGrammarRule rule_Copy[] = { L(COPY), OPT(helper_Copy_opt), R(GraphOrDefault), L(TO), R(GraphOrDefault), NIL };

/* Move ::= 'MOVE' 'SILENT'? GraphOrDefault 'TO' GraphOrDefault
 */
static const TrackerGrammarRule helper_Move_opt[] = { L(SILENT), NIL };
static const TrackerGrammarRule rule_Move[] = { L(MOVE), OPT(helper_Move_opt), R(GraphOrDefault), L(TO), R(GraphOrDefault), NIL };

/* Add ::= 'ADD' 'SILENT'? GraphOrDefault 'TO' GraphOrDefault
 */
static const TrackerGrammarRule helper_Add_opt[] = { L(SILENT), NIL };
static const TrackerGrammarRule rule_Add[] = { L(ADD), OPT(helper_Add_opt), R(GraphOrDefault), L(TO), R(GraphOrDefault), NIL };

/* Create ::= 'CREATE' 'SILENT'? GraphRef
 */
static const TrackerGrammarRule helper_Create_opt[] = { L(SILENT), NIL };
static const TrackerGrammarRule rule_Create[] = { L(CREATE), OPT(helper_Create_opt), R(GraphRef), NIL };

/* Drop ::= 'DROP' 'SILENT'? GraphRefAll
 */
static const TrackerGrammarRule helper_Drop_opt[] = { L(SILENT), NIL };
static const TrackerGrammarRule rule_Drop[] = { L(DROP), OPT(helper_Drop_opt), R(GraphRefAll), NIL };

/* Clear ::= 'CLEAR' 'SILENT'? GraphRefAll
 */
static const TrackerGrammarRule helper_Clear_opt[] = { L(SILENT), NIL };
static const TrackerGrammarRule rule_Clear[] = { L(CLEAR), OPT(helper_Clear_opt), R(GraphRefAll), NIL };

/* Load ::= 'LOAD' 'SILENT'? iri ( 'INTO' GraphRef )?
 */
static const TrackerGrammarRule helper_Load_opt_1[] = { L(SILENT), NIL };
static const TrackerGrammarRule helper_Load_seq[] = { L(INTO), R(GraphRef), NIL };
static const TrackerGrammarRule helper_Load_opt_2[] = { S(helper_Load_seq), NIL };
static const TrackerGrammarRule rule_Load[] = { L(LOAD), OPT(helper_Load_opt_1), R(iri), OPT(helper_Load_opt_2), NIL };

/* Update1 ::= Load | Clear | Drop | Add | Move | Copy | Create | InsertData | DeleteData | DeleteWhere | Modify
 */
static const TrackerGrammarRule helper_Update1_or[] = { R(Load), R(Clear), R(Drop), R(Add), R(Move), R(Copy), R(Create), R(InsertData), R(DeleteData), R(DeleteWhere), R(Modify), NIL };
static const TrackerGrammarRule rule_Update1[] = { OR(helper_Update1_or), NIL };

/* ValuesClause ::= ( 'VALUES' DataBlock )?
 */
static const TrackerGrammarRule helper_ValuesClause_seq[] = { L(VALUES), R(DataBlock), NIL };
static const TrackerGrammarRule helper_ValuesClause_opt[] = { S(helper_ValuesClause_seq), NIL };
static const TrackerGrammarRule rule_ValuesClause[] = { OPT(helper_ValuesClause_opt), NIL };

/* OffsetClause ::= 'OFFSET' INTEGER
 */
static const TrackerGrammarRule rule_OffsetClause[] = { L(OFFSET), T(INTEGER), NIL };

/* LimitClause ::= 'LIMIT' INTEGER
 */
static const TrackerGrammarRule rule_LimitClause[] = { L(LIMIT), T(INTEGER), NIL };

/* LimitOffsetClauses ::= LimitClause OffsetClause? | OffsetClause LimitClause?
 */
static const TrackerGrammarRule helper_LimitOffsetClauses_opt_1[] = { R(OffsetClause), NIL };
static const TrackerGrammarRule helper_LimitOffsetClauses_seq_1[] = { R(LimitClause), OPT(helper_LimitOffsetClauses_opt_1), NIL };
static const TrackerGrammarRule helper_LimitOffsetClauses_opt_2[] = { R(LimitClause), NIL };
static const TrackerGrammarRule helper_LimitOffsetClauses_seq_2[] = { R(OffsetClause), OPT(helper_LimitOffsetClauses_opt_2), NIL };
static const TrackerGrammarRule helper_LimitOffsetClauses_or[] = { S(helper_LimitOffsetClauses_seq_1), S(helper_LimitOffsetClauses_seq_2), NIL };
static const TrackerGrammarRule rule_LimitOffsetClauses[] = { OR(helper_LimitOffsetClauses_or), NIL };

/* OrderCondition ::= ( ( 'ASC' | 'DESC' ) BrackettedExpression )
 *                    | ( Constraint | Var )
 *
 * TRACKER EXTENSION:
 * The first rule is turned into the more generic:
 * ( ( 'ASC' | 'DESC' ) Expression )
 */
static const TrackerGrammarRule helper_OrderCondition_or_1[] = { L(ASC), L(DESC), NIL };
static const TrackerGrammarRule helper_OrderCondition_seq[] = { OR(helper_OrderCondition_or_1), R(Expression), NIL };
static const TrackerGrammarRule helper_OrderCondition_or_2[] = { S(helper_OrderCondition_seq), R(Constraint), R(Var), NIL };
static const TrackerGrammarRule rule_OrderCondition[] = { OR(helper_OrderCondition_or_2), NIL };

/* OrderClause ::= 'ORDER' 'BY' OrderCondition+
 */
static const TrackerGrammarRule helper_OrderClause[] = { R(OrderCondition), NIL };
static const TrackerGrammarRule rule_OrderClause[] = { L(ORDER), L(BY), GT0 (helper_OrderClause), NIL };

/* HavingCondition ::= Constraint
 */
static const TrackerGrammarRule rule_HavingCondition[] = { R(Constraint), NIL };

/* HavingClause ::= 'HAVING' HavingCondition+
 */
static const TrackerGrammarRule helper_HavingClause_gt0[] = { R(HavingCondition), NIL };
static const TrackerGrammarRule rule_HavingClause[] = { L(HAVING), GT0(helper_HavingClause_gt0), NIL };

/* GroupCondition ::= BuiltInCall | FunctionCall | '(' Expression ( 'AS' Var )? ')' | Var
 */
static const TrackerGrammarRule helper_GroupCondition_opt[] = { L(AS), R(Var), NIL };
static const TrackerGrammarRule helper_GroupCondition_seq[] = { L(OPEN_PARENS), R(Expression), OPT(helper_GroupCondition_opt), L(CLOSE_PARENS), NIL };
static const TrackerGrammarRule helper_GroupCondition_or[] = { R(BuiltInCall), R(FunctionCall), S(helper_GroupCondition_seq), R(Var), NIL };
static const TrackerGrammarRule rule_GroupCondition[] = { OR(helper_GroupCondition_or), NIL };

/* GroupClause ::= 'GROUP' 'BY' GroupCondition+
 */
static const TrackerGrammarRule helper_GroupClause_gt0[] = { R(GroupCondition), NIL };
static const TrackerGrammarRule rule_GroupClause[] = { L(GROUP), L(BY), GT0(helper_GroupClause_gt0), NIL };

/* SolutionModifier ::= GroupClause? HavingClause? OrderClause? LimitOffsetClauses?
 */
static const TrackerGrammarRule helper_SolutionModifier_opt_1[] = { R(GroupClause), NIL };
static const TrackerGrammarRule helper_SolutionModifier_opt_2[] = { R(HavingClause), NIL };
static const TrackerGrammarRule helper_SolutionModifier_opt_3[] = { R(OrderClause), NIL };
static const TrackerGrammarRule helper_SolutionModifier_opt_4[] = { R(LimitOffsetClauses), NIL };
static const TrackerGrammarRule rule_SolutionModifier[] = { OPT(helper_SolutionModifier_opt_1), OPT (helper_SolutionModifier_opt_2), OPT(helper_SolutionModifier_opt_3), OPT(helper_SolutionModifier_opt_4), NIL };


/* WhereClause ::= 'WHERE'? GroupGraphPattern
 */
static const TrackerGrammarRule helper_WhereClause_opt[] = { L(WHERE), NIL };
static const TrackerGrammarRule rule_WhereClause[] = { OPT(helper_WhereClause_opt), R(GroupGraphPattern), NIL };

/* SourceSelector ::= iri
 */
static const TrackerGrammarRule rule_SourceSelector[] = { R(iri), NIL };

/* NamedGraphClause ::= 'NAMED' SourceSelector
 */
static const TrackerGrammarRule rule_NamedGraphClause[] = { L(NAMED), R(SourceSelector), NIL };

/* DefaultGraphClause ::= SourceSelector
 */
static const TrackerGrammarRule rule_DefaultGraphClause[] = { R(SourceSelector), NIL };

/* DatasetClause ::= 'FROM' ( DefaultGraphClause | NamedGraphClause )
 */
static const TrackerGrammarRule helper_DatasetClause_or[] = { R(DefaultGraphClause), R(NamedGraphClause), NIL };
static const TrackerGrammarRule rule_DatasetClause[] = { L(FROM), OR(helper_DatasetClause_or), NIL };

/* AskQuery ::= 'ASK' DatasetClause* WhereClause SolutionModifier
 */
static const TrackerGrammarRule helper_AskQuery_gte0[] = { R(DatasetClause), NIL };
static const TrackerGrammarRule rule_AskQuery[] = { L(ASK), GTE0(helper_AskQuery_gte0), R(WhereClause), R(SolutionModifier), NIL };

/* DescribeQuery ::= 'DESCRIBE' ( VarOrIri+ | '*' ) DatasetClause* WhereClause? SolutionModifier
 */
static const TrackerGrammarRule helper_DescribeQuery_opt[] = { R(WhereClause), NIL };
static const TrackerGrammarRule helper_DescribeQuery_gte0[] = { R(DatasetClause), NIL };
static const TrackerGrammarRule helper_DescribeQuery_gt0[] = { R(VarOrIri), NIL };
static const TrackerGrammarRule helper_DescribeQuery_or[] = { GT0 (helper_DescribeQuery_gt0), L(GLOB), NIL };
static const TrackerGrammarRule rule_DescribeQuery[] = { L(DESCRIBE), OR(helper_DescribeQuery_or), GTE0(helper_DescribeQuery_gte0), OPT(helper_DescribeQuery_opt), R(SolutionModifier), NIL };

/* ConstructQuery ::= 'CONSTRUCT' ( ConstructTemplate DatasetClause* WhereClause SolutionModifier |
 *                                  DatasetClause* 'WHERE' '{' TriplesTemplate? '}' SolutionModifier )
 */
static const TrackerGrammarRule helper_ConstructQuery_gte0[] = { R(DatasetClause), NIL };
static const TrackerGrammarRule helper_ConstructQuery_seq_1[] = { R(ConstructTemplate), GTE0 (helper_ConstructQuery_gte0), R(WhereClause), R(SolutionModifier), NIL };
static const TrackerGrammarRule helper_ConstructQuery_opt[] = { R(TriplesTemplate), NIL };
static const TrackerGrammarRule helper_ConstructQuery_seq_2[] = { GTE0 (helper_ConstructQuery_gte0), L(WHERE), L(OPEN_BRACE), OPT (helper_ConstructQuery_opt), L(CLOSE_BRACE), R(SolutionModifier), NIL };
static const TrackerGrammarRule helper_ConstructQuery_or[] = { S (helper_ConstructQuery_seq_1), S (helper_ConstructQuery_seq_2), NIL };
static const TrackerGrammarRule rule_ConstructQuery[] = { L(CONSTRUCT), OR(helper_ConstructQuery_or), NIL };

/* SelectClause ::= 'SELECT' ( 'DISTINCT' | 'REDUCED' )? ( ( Var | ( '(' Expression 'AS' Var ')' ) )+ | '*' )
 *
 * TRACKER EXTENSION:
 * Variable set also accepts the following syntax:
 *   Expression ('AS' Var)?
 *   Var ('AS' Var)?
 */
static const TrackerGrammarRule ext_SelectClause_seq_1[] = { L(AS), R(Var), NIL };
static const TrackerGrammarRule ext_SelectClause_opt[] = { S(ext_SelectClause_seq_1), NIL };
static const TrackerGrammarRule ext_SelectClause_seq_2[] = { R(Var), OPT(ext_SelectClause_opt), NIL };
static const TrackerGrammarRule ext_SelectClause_seq_3[] = { R(Expression), OPT(ext_SelectClause_opt), NIL };
static const TrackerGrammarRule helper_SelectClause_seq_1[] = { L(OPEN_PARENS), R(Expression), L(AS), R(Var), L(CLOSE_PARENS), NIL };
static const TrackerGrammarRule helper_SelectClause_or_1[] = { L(DISTINCT), L(REDUCED), NIL };
static const TrackerGrammarRule helper_SelectClause_or_2[] = { S(ext_SelectClause_seq_2), S(ext_SelectClause_seq_3), R(Var), S(helper_SelectClause_seq_1), NIL };
static const TrackerGrammarRule helper_SelectClause_gt0[] = { OR(helper_SelectClause_or_2), NIL };
static const TrackerGrammarRule helper_SelectClause_opt[] = { OR(helper_SelectClause_or_1), NIL };
static const TrackerGrammarRule helper_SelectClause_or_3[] = { L(GLOB), GT0(helper_SelectClause_gt0), NIL };
static const TrackerGrammarRule rule_SelectClause[] = { L(SELECT), OPT(helper_SelectClause_opt), OR(helper_SelectClause_or_3), NIL };

/* SubSelect ::= SelectClause WhereClause SolutionModifier ValuesClause
 */
static const TrackerGrammarRule rule_SubSelect[] = { R(SelectClause), R(WhereClause), R(SolutionModifier), R(ValuesClause), NIL };

/* SelectQuery ::= SelectClause DatasetClause* WhereClause SolutionModifier
 */
static const TrackerGrammarRule helper_SelectQuery_gte0[] = { R(DatasetClause), NIL };
static const TrackerGrammarRule rule_SelectQuery[] = { R(SelectClause), GTE0(helper_SelectQuery_gte0), R(WhereClause), R(SolutionModifier), NIL };

/* PrefixDecl ::= 'PREFIX' PNAME_NS IRIREF
 */
static const TrackerGrammarRule rule_PrefixDecl[] = { L(PREFIX), T(PNAME_NS), T(IRIREF), NIL };


/* BaseDecl ::= 'BASE' IRIREF
 */
static const TrackerGrammarRule rule_BaseDecl[] = { L(BASE), T(IRIREF), NIL };

/* Prologue ::= ( BaseDecl | PrefixDecl )*
 */
static const TrackerGrammarRule helper_Prologue_or[] = { R(BaseDecl), R(PrefixDecl), NIL };
static const TrackerGrammarRule helper_Prologue_gte0[] = { OR(helper_Prologue_or), NIL };
static const TrackerGrammarRule rule_Prologue[] = { GTE0 (helper_Prologue_gte0), NIL };

/* Update ::= Prologue ( Update1 ( ';' Update )? )?
 *
 * TRACKER EXTENSION:
 * ';' separator is made optional.
 */
static const TrackerGrammarRule helper_Update_opt_3[] = { L(SEMICOLON), NIL };
static const TrackerGrammarRule helper_Update_seq_1[] = { OPT(helper_Update_opt_3), R(Update), NIL };
static const TrackerGrammarRule helper_Update_opt_1[] = { S (helper_Update_seq_1), NIL };
static const TrackerGrammarRule helper_Update_seq_2[] = { R(Update1), OPT (helper_Update_opt_1), NIL };
static const TrackerGrammarRule helper_Update_opt_2[] = { S(helper_Update_seq_2), NIL };
static const TrackerGrammarRule rule_Update[] = { R(Prologue), OPT(helper_Update_opt_2), NIL };

/* UpdateUnit ::= Update
 */
static const TrackerGrammarRule rule_UpdateUnit[] = { R(Update), NIL };

/* Query ::= Prologue
 *           ( SelectQuery | ConstructQuery | DescribeQuery | AskQuery )
 *           ValuesClause
 */
static const TrackerGrammarRule helper_Query_or[] = { R(SelectQuery), R(ConstructQuery), R(DescribeQuery), R(AskQuery), NIL };
static const TrackerGrammarRule rule_Query[] = { R(Prologue), OR(helper_Query_or), R(ValuesClause), NIL };

/* QueryUnit ::= Query
 */
static const TrackerGrammarRule rule_QueryUnit[] = { R(Query), NIL };

/* Inline funcs for terminal parsers */
#define READ_CHAR(set, _C_)			\
	G_STMT_START {				\
		gchar ch = *str;		\
		if ((set)) {			\
			str++;			\
		} else {			\
			_C_;			\
		}				\
	} G_STMT_END

#define READ_UNICHAR(set, _C_)				\
	G_STMT_START {					\
		gunichar ch = g_utf8_get_char (str);	\
		if ((set)) {				\
			str = g_utf8_next_char (str);	\
		} else {				\
			_C_;				\
		}					\
	} G_STMT_END

#define READ_STRING(set, _C_)				\
	G_STMT_START {					\
		const gchar *tmp = str;			\
		while (str < end) {			\
			gchar ch = *str;		\
			if ((set)) {			\
				str++;			\
			} else {			\
				break;			\
			}				\
		}					\
		if (tmp == str) {			\
			_C_;				\
		}					\
	} G_STMT_END

#define READ_UNICODE_STRING(set, _C_)				\
	G_STMT_START {						\
		const gchar *tmp = str;				\
		while (str < end) {				\
			gunichar ch = g_utf8_get_char (str);	\
			if ((set)) {				\
				str = g_utf8_next_char (str);	\
			} else {				\
				break;				\
			}					\
		}						\
		if (tmp == str) {				\
			_C_;					\
		}						\
	} G_STMT_END

#define OPTIONAL_CHAR(set) READ_CHAR(set, )
#define OPTIONAL_UNICHAR(set) READ_UNICHAR(set, )
#define OPTIONAL_STRING(set) READ_STRING(set, )
#define OPTIONAL_UNICODE_STRING(set) READ_UNICODE_STRING(set, )

#define ACCEPT_CHAR(set) READ_CHAR(set, return FALSE)
#define ACCEPT_UNICHAR(set) READ_UNICHAR(set, return FALSE)
#define ACCEPT_STRING(set) READ_STRING(set, return FALSE)
#define ACCEPT_UNICODE_STRING(set) READ_UNICODE_STRING(set, return FALSE)


#define RANGE_NUMBER ((ch >= '0' && ch <= '9'))
#define RANGE_UPPERCASE_ASCII ((ch >= 'A' && ch <= 'Z'))
#define RANGE_LOWERCASE_ASCII ((ch >= 'a' && ch <= 'z'))
#define RANGE_ASCII (RANGE_UPPERCASE_ASCII || RANGE_LOWERCASE_ASCII)

/* PN_CHARS_BASE ::= [A-Z] | [a-z] | [#x00C0-#x00D6] | [#x00D8-#x00F6] | [#x00F8-#x02FF] | [#x0370-#x037D] | [#x037F-#x1FFF] | [#x200C-#x200D] | [#x2070-#x218F] | [#x2C00-#x2FEF] | [#x3001-#xD7FF] | [#xF900-#xFDCF] | [#xFDF0-#xFFFD] | [#x10000-#xEFFFF]
 */
#define PN_CHARS_BASE				\
	(RANGE_ASCII ||				\
	 (ch >= 0x00C0 && ch <= 0x00D6) ||	\
	 (ch >= 0x00D8 && ch <= 0x00F6) ||	\
	 (ch >= 0x00F8 && ch <= 0x02FF) ||	\
	 (ch >= 0x0370 && ch <= 0x037D) ||	\
	 (ch >= 0x037F && ch <= 0x1FFF) ||	\
	 (ch >= 0x200C && ch <= 0x200D) ||	\
	 (ch >= 0x2070 && ch <= 0x218F) ||	\
	 (ch >= 0x2C00 && ch <= 0x2FEF) ||	\
	 (ch >= 0x3001 && ch <= 0xD7FF) ||	\
	 (ch >= 0xF900 && ch <= 0xFDCF) ||	\
	 (ch >= 0xFDF0 && ch <= 0xFFFD) ||	\
	 (ch >= 0x10000 && ch <= 0xEFFFF))

/* PN_CHARS_U ::= PN_CHARS_BASE | '_'
 */
#define PN_CHARS_U \
	(PN_CHARS_BASE || ch == '_')

/* PN_CHARS ::= PN_CHARS_U | '-' | [0-9] | #x00B7 | [#x0300-#x036F] | [#x203F-#x2040]
 */
#define PN_CHARS						  \
	(PN_CHARS_U || ch == '-' || RANGE_NUMBER ||		  \
	 ch == 0x00B7 || (ch >= 0x0300 && ch <= 0x036F) ||	  \
	 (ch >= 0x203F && ch <= 0x2040))

/* WS ::= #x20 | #x9 | #xD | #xA
 */
#define WS \
	(ch == 0x20 || ch == 0x9 || ch == 0xD || ch == 0xA)

/* HEX ::= [0-9] | [A-F] | [a-f]
 */
#define HEX \
	(RANGE_NUMBER || (ch >= 'A' && ch <= 'F') || (ch >= 'a' && ch <= 'f'))

/* IRIREF ::= '<' ([^<>"{}|^`\]-[#x00-#x20])* '>'
 */
static inline gboolean
terminal_IRIREF (const gchar  *str,
		 const gchar  *end,
		 const gchar **str_out)
{
	ACCEPT_CHAR ((ch == '<'));
	OPTIONAL_UNICODE_STRING ((ch != '<' && ch != '>' &&
				  ch != '"' && ch != '{' &&
				  ch != '}' && ch != '|' &&
				  ch != '^' && ch != '`' &&
				  ch != '\\' && !(ch >= 0x00 && ch <= 0x20)));
	ACCEPT_CHAR ((ch == '>'));
	*str_out = str;
	return TRUE;
}

/* PN_PREFIX ::= PN_CHARS_BASE ((PN_CHARS|'.')* PN_CHARS)?
 */
static inline gboolean
terminal_PN_PREFIX (const gchar  *str,
		    const gchar  *end,
		    const gchar **str_out)
{
	ACCEPT_UNICHAR (PN_CHARS_BASE);

	while (str < end) {
		READ_UNICODE_STRING ((PN_CHARS || ch == '.'), goto out);

		/* The last PN_CHARS shall be read above, check the last
		 * char being read is within that range (i.e. not a dot).
		 */
		if (*(str - 1) == '.')
			str--;

		break;
	}
out:
	*str_out = str;
	return TRUE;
}

/* PNAME_NS ::= PN_PREFIX? ':'
 */
static inline gboolean
terminal_PNAME_NS (const gchar  *str,
		   const gchar  *end,
		   const gchar **str_out)
{
	terminal_PN_PREFIX (str, end, &str);
	ACCEPT_UNICHAR ((ch == ':'));
	*str_out = str;
	return TRUE;
}

/* PERCENT ::= '%' HEX HEX
 */
static inline gboolean
terminal_PERCENT (const gchar  *str,
		  const gchar  *end,
		  const gchar **str_out)
{
	ACCEPT_CHAR ((ch == '%'));
	ACCEPT_CHAR (HEX);
	ACCEPT_CHAR (HEX);
	*str_out = str;
	return TRUE;
}

/* PN_LOCAL_ESC ::= '\' ( '_' | '~' | '.' | '-' | '!' | '$' | '&' | "'" | '(' | ')' | '*' | '+' | ',' | ';' | '=' | '/' | '?' | '#' | '@' | '%' )
 */
static inline gboolean
terminal_PN_LOCAL_ESC (const gchar  *str,
		       const gchar  *end,
		       const gchar **str_out)
{
	ACCEPT_CHAR ((ch == '\\'));
	ACCEPT_CHAR ((ch == '_' || ch == '~' || ch == '.' || ch == '-' ||
		      ch == '!' || ch == '$' || ch == '&' || ch == '\'' ||
		      ch == '(' || ch == ')' || ch == '*' || ch == '+' ||
		      ch == ',' || ch == ';' || ch == '=' || ch == '/' ||
		      ch == '?' || ch == '#' || ch == '@' || ch == '%'));
	*str_out = str;
	return TRUE;
}

/* PLX ::= PERCENT | PN_LOCAL_ESC
 */
static inline gboolean
terminal_PLX (const gchar  *str,
	      const gchar  *end,
	      const gchar **str_out)
{
	if (terminal_PERCENT (str, end, str_out))
		return TRUE;
	if (terminal_PN_LOCAL_ESC (str, end, str_out))
		return TRUE;
	return FALSE;
}

/* PN_LOCAL ::= (PN_CHARS_U | ':' | [0-9] | PLX ) ((PN_CHARS | '.' | ':' | PLX)* (PN_CHARS | ':' | PLX) )?
 */
static inline gboolean
terminal_PN_LOCAL (const gchar  *str,
		   const gchar  *end,
		   const gchar **str_out)
{
	if (!terminal_PLX (str, end, &str))
		ACCEPT_UNICHAR (PN_CHARS_U || RANGE_NUMBER || ch == ':');

	while (str < end) {
		if (!terminal_PLX (str, end, &str))
			READ_UNICHAR ((PN_CHARS || ch == '.' || ch == ':'), goto out);
	}

out:
	/* check the last char being read is not a period. */
	if (*(str - 1) == '.')
		str--;

	*str_out = str;
	return TRUE;
}

/* PNAME_LN ::= PNAME_NS PN_LOCAL
 */
static inline gboolean
terminal_PNAME_LN (const gchar  *str,
		   const gchar  *end,
		   const gchar **str_out)
{
	if (!terminal_PNAME_NS (str, end, &str))
		return FALSE;
	if (!terminal_PN_LOCAL (str, end, str_out))
		return FALSE;
	return TRUE;
}

/* BLANK_NODE_LABEL ::= '_:' ( PN_CHARS_U | [0-9] ) ((PN_CHARS|'.')* PN_CHARS)?
 */
static inline gboolean
terminal_BLANK_NODE_LABEL (const gchar  *str,
			   const gchar  *end,
			   const gchar **str_out)
{
	ACCEPT_CHAR ((ch == '_'));
	ACCEPT_CHAR ((ch == ':'));
	ACCEPT_UNICHAR (PN_CHARS_U || RANGE_NUMBER);

	OPTIONAL_UNICODE_STRING (PN_CHARS || ch == '.');
	OPTIONAL_UNICHAR (PN_CHARS);
	*str_out = str;
	return TRUE;
}

/* VARNAME ::= ( PN_CHARS_U | [0-9] ) ( PN_CHARS_U | [0-9] | #x00B7 | [#x0300-#x036F] | [#x203F-#x2040] )*
 */
static inline gboolean
terminal_VARNAME (const gchar  *str,
		  const gchar  *end,
		  const gchar **str_out)
{
	ACCEPT_CHAR(PN_CHARS_U || RANGE_NUMBER);
	OPTIONAL_UNICODE_STRING (PN_CHARS_U || RANGE_NUMBER ||
				 ch == 0x00B7 || (ch >= 0x0300 && ch <= 0x036F) ||
				 (ch >= 0x203F && ch <= 0x2040));
	*str_out = str;
	return TRUE;
}

/* VAR1 ::= '?' VARNAME
 */
static inline gboolean
terminal_VAR1 (const gchar  *str,
	       const gchar  *end,
	       const gchar **str_out)
{
	ACCEPT_CHAR((ch == '?'));
	return terminal_VARNAME (str, end, str_out);
}

/* VAR1 ::= '$' VARNAME
 */
static inline gboolean
terminal_VAR2 (const gchar  *str,
	       const gchar  *end,
	       const gchar **str_out)
{
	ACCEPT_CHAR((ch == '$'));
	return terminal_VARNAME (str, end, str_out);
}

/* PARAMETERIZED_VAR ::= '~' VARNAME
 */
static inline gboolean
terminal_PARAMETERIZED_VAR (const gchar  *str,
			    const gchar  *end,
			    const gchar **str_out)
{
	ACCEPT_CHAR((ch == '~'));
	return terminal_VARNAME (str, end, str_out);
}

/* LANGTAG ::= '@' [a-zA-Z]+ ('-' [a-zA-Z0-9]+)*
 */
static inline gboolean
terminal_LANGTAG (const gchar  *str,
		  const gchar  *end,
		  const gchar **str_out)
{
	ACCEPT_CHAR ((ch == '@'));
	ACCEPT_STRING (RANGE_ASCII);

	while (str < end) {
		READ_CHAR ((ch == '-'), goto out);
		ACCEPT_STRING (RANGE_ASCII || RANGE_NUMBER);
	}
out:
	*str_out = str;
	return TRUE;
}

/* INTEGER ::= [0-9]+
 */
static inline gboolean
terminal_INTEGER (const gchar  *str,
		  const gchar  *end,
		  const gchar **str_out)
{
	ACCEPT_STRING (RANGE_NUMBER);
	*str_out = str;
	return TRUE;
}

/* DECIMAL ::= [0-9]* '.' [0-9]+
 */
static inline gboolean
terminal_DECIMAL (const gchar  *str,
		  const gchar  *end,
		  const gchar **str_out)
{
	OPTIONAL_STRING (RANGE_NUMBER);
	ACCEPT_CHAR ((ch == '.'));
	ACCEPT_STRING (RANGE_NUMBER);
	*str_out = str;
	return TRUE;
}

/* EXPONENT ::= [eE] [+-]? [0-9]+
 */
static inline gboolean
terminal_EXPONENT (const gchar  *str,
		   const gchar  *end,
		   const gchar **str_out)
{
	ACCEPT_CHAR ((ch == 'e' || ch == 'E'));
	OPTIONAL_CHAR ((ch == '+' || ch == '-'));
	ACCEPT_STRING (RANGE_NUMBER);
	*str_out = str;
	return TRUE;
}

/* DOUBLE ::= [0-9]+ '.' [0-9]* EXPONENT | '.' ([0-9])+ EXPONENT | ([0-9])+ EXPONENT
 */
static inline gboolean
terminal_DOUBLE (const gchar  *str,
		 const gchar  *end,
		 const gchar **str_out)
{
	const gchar *start = str;

	OPTIONAL_STRING (RANGE_NUMBER);
	OPTIONAL_CHAR ((ch == '.'));
	OPTIONAL_STRING (RANGE_NUMBER);

	if (str == start)
		return FALSE;
	if (str == start + 1 && str[0] != '.')
		return FALSE;

	return terminal_EXPONENT (str, end, str_out);
}

/* INTEGER_POSITIVE ::= '+' INTEGER
 */
static inline gboolean
terminal_INTEGER_POSITIVE (const gchar  *str,
			   const gchar  *end,
			   const gchar **str_out)
{
	ACCEPT_CHAR ((ch == '+'));
	return terminal_INTEGER (str, end, str_out);
}

/* DECIMAL_POSITIVE ::= '+' DECIMAL
 */
static inline gboolean
terminal_DECIMAL_POSITIVE (const gchar  *str,
			   const gchar  *end,
			   const gchar **str_out)
{
	ACCEPT_CHAR ((ch == '+'));
	return terminal_DECIMAL (str, end, str_out);
}

/* DOUBLE_POSITIVE ::= '+' DOUBLE
 */
static inline gboolean
terminal_DOUBLE_POSITIVE (const gchar  *str,
			  const gchar  *end,
			  const gchar **str_out)
{
	ACCEPT_CHAR ((ch == '+'));
	return terminal_DOUBLE_POSITIVE (str, end, str_out);
}

/* INTEGER_NEGATIVE ::= '-' INTEGER
 */
static inline gboolean
terminal_INTEGER_NEGATIVE (const gchar  *str,
			   const gchar  *end,
			   const gchar **str_out)
{
	ACCEPT_CHAR ((ch == '-'));
	return terminal_INTEGER (str, end, str_out);
}

/* DECIMAL_NEGATIVE ::= '-' DECIMAL
 */
static inline gboolean
terminal_DECIMAL_NEGATIVE (const gchar  *str,
			   const gchar  *end,
			   const gchar **str_out)
{
	ACCEPT_CHAR ((ch == '-'));
	return terminal_DECIMAL (str, end, str_out);
}

/* DOUBLE_NEGATIVE ::= '-' DOUBLE
 */
static inline gboolean
terminal_DOUBLE_NEGATIVE (const gchar  *str,
			  const gchar  *end,
			  const gchar **str_out)
{
	ACCEPT_CHAR ((ch == '-'));
	return terminal_DOUBLE (str, end, str_out);
}

/* ECHAR ::= '\' [tbnrf\"']
 */
static inline gboolean
terminal_ECHAR (const gchar  *str,
		const gchar  *end,
		const gchar **str_out)
{
	ACCEPT_CHAR ((ch == '\\'));
	ACCEPT_CHAR ((ch == 't' || ch == 'b' || ch == 'n' ||
		      ch == 'r' || ch == 'f' || ch == '\\' ||
		      ch == '"' || ch == '\''));
	*str_out = str;
	return TRUE;
}

/* STRING_LITERAL1 ::= "'" ( ([^#x27#x5C#xA#xD]) | ECHAR )* "'"
 */
static inline gboolean
terminal_STRING_LITERAL1 (const gchar  *str,
			  const gchar  *end,
			  const gchar **str_out)
{
	ACCEPT_CHAR ((ch == '\''));

	while (str < end) {
		if (!terminal_ECHAR (str, end, &str)) {
			READ_UNICHAR ((ch != 0x27 && ch != 0x5C &&
				       ch != 0xA && ch != 0xD), goto out);
		}
	}

out:
	ACCEPT_CHAR ((ch == '\''));
	*str_out = str;
	return TRUE;
}

/* STRING_LITERAL2 ::= '"' ( ([^#x22#x5C#xA#xD]) | ECHAR )* '"'
 */
static inline gboolean
terminal_STRING_LITERAL2 (const gchar  *str,
			  const gchar  *end,
			  const gchar **str_out)
{
	ACCEPT_CHAR ((ch == '"'));

	while (str < end) {
		if (!terminal_ECHAR (str, end, &str))
			READ_UNICHAR ((ch != 0x22 && ch != 0x5C &&
				       ch != 0xA && ch != 0xD), goto out);
	}

out:
	ACCEPT_CHAR ((ch == '"'));
	*str_out = str;
	return TRUE;
}

/* STRING_LITERAL_LONG1 ::= "'''" ( ( "'" | "''" )? ( [^'\] | ECHAR ) )* "'''"
 */
static inline gboolean
terminal_STRING_LITERAL_LONG1 (const gchar  *str,
			       const gchar  *end,
			       const gchar **str_out)
{
	ACCEPT_CHAR ((ch == '\''));
	ACCEPT_CHAR ((ch == '\''));
	ACCEPT_CHAR ((ch == '\''));

	while (str < end) {
		if (strncmp (str, "\\'",  2) == 0) {
			str += 2;
		} else if (strncmp (str, "'''", 3) == 0) {
			str += 3;
			*str_out = str;
			return TRUE;
		} else {
			str++;
		}
	}

	return FALSE;
}

/* STRING_LITERAL_LONG2 ::= '"""' ( ( '"' | '""' )? ( [^"\] | ECHAR ) )* '"""'
 */
static inline gboolean
terminal_STRING_LITERAL_LONG2 (const gchar  *str,
			       const gchar  *end,
			       const gchar **str_out)
{
	ACCEPT_CHAR ((ch == '"'));
	ACCEPT_CHAR ((ch == '"'));
	ACCEPT_CHAR ((ch == '"'));

	while (str < end) {
		if (strncmp (str, "\\\"",  2) == 0) {
			str += 2;
		} else if (strncmp (str, "\"\"\"", 3) == 0) {
			str += 3;
			*str_out = str;
			return TRUE;
		} else {
			str++;
		}
	}

	return FALSE;
}

/* NIL ::= '(' WS* ')'
 */
static inline gboolean
terminal_NIL (const gchar  *str,
	      const gchar  *end,
	      const gchar **str_out)
{
	ACCEPT_CHAR ((ch == '('));
	OPTIONAL_STRING (WS);
	ACCEPT_CHAR ((ch == ')'));
	*str_out = str;
	return TRUE;
}

/* ANON ::= '[' WS* ']'
 */
static inline gboolean
terminal_ANON (const gchar  *str,
	       const gchar  *end,
	       const gchar **str_out)
{
	ACCEPT_CHAR ((ch == '['));
	OPTIONAL_STRING (WS);
	ACCEPT_CHAR ((ch == ']'));
	*str_out = str;
	return TRUE;
}

#define NAMED_RULE(rule) (named_rules[NAMED_RULE_##rule])

/* Order must match the enum's */
static const TrackerGrammarRule *named_rules[N_NAMED_RULES] = {
	rule_QueryUnit,
	rule_UpdateUnit,
	rule_Query,
	rule_Update,
	rule_SelectClause,
	rule_Prologue,
	rule_BaseDecl,
	rule_PrefixDecl,
	rule_SelectQuery,
	rule_SubSelect,
	rule_ConstructQuery,
	rule_DescribeQuery,
	rule_AskQuery,
	rule_DatasetClause,
	rule_DefaultGraphClause,
	rule_NamedGraphClause,
	rule_SourceSelector,
	rule_WhereClause,
	rule_SolutionModifier,
	rule_GroupClause,
	rule_GroupCondition,
	rule_HavingClause,
	rule_HavingCondition,
	rule_OrderClause,
	rule_OrderCondition,
	rule_LimitOffsetClauses,
	rule_LimitClause,
	rule_OffsetClause,
	rule_ValuesClause,
	rule_Update1,
	rule_Load,
	rule_Clear,
	rule_Drop,
	rule_Create,
	rule_Add,
	rule_Move,
	rule_Copy,
	rule_InsertData,
	rule_DeleteData,
	rule_DeleteWhere,
	rule_Modify,
	rule_DeleteClause,
	rule_InsertClause,
	rule_UsingClause,
	rule_GraphOrDefault,
	rule_GraphRefAll,
	rule_GraphRef,
	rule_QuadPattern,
	rule_QuadData,
	rule_Quads,
	rule_QuadsNotTriples,
	rule_TriplesTemplate,
	rule_GroupGraphPatternSub,
	rule_TriplesBlock,
	rule_GraphPatternNotTriples,
	rule_OptionalGraphPattern,
	rule_GraphGraphPattern,
	rule_ServiceGraphPattern,
	rule_Bind,
	rule_InlineData,
	rule_DataBlock,
	rule_InlineDataOneVar,
	rule_InlineDataFull,
	rule_DataBlockValue,
	rule_MinusGraphPattern,
	rule_GroupOrUnionGraphPattern,
	rule_Filter,
	rule_Constraint,
	rule_FunctionCall,
	rule_ArgList,
	rule_ExpressionList,
	rule_ConstructTemplate,
	rule_ConstructTriples,
	rule_TriplesSameSubject,
	rule_GroupGraphPattern,
	rule_PropertyList,
	rule_PropertyListNotEmpty,
	rule_Verb,
	rule_ObjectList,
	rule_Object,
	rule_TriplesSameSubjectPath,
	rule_PropertyListPath,
	rule_PropertyListPathNotEmpty,
	rule_VerbPath,
	rule_VerbSimple,
	rule_ObjectListPath,
	rule_ObjectPath,
	rule_Path,
	rule_PathAlternative,
	rule_PathSequence,
	rule_PathEltOrInverse,
	rule_PathElt,
	rule_PathMod,
	rule_PathPrimary,
	rule_PathNegatedPropertySet,
	rule_PathOneInPropertySet,
	rule_Integer,
	rule_TriplesNode,
	rule_BlankNodePropertyList,
	rule_TriplesNodePath,
	rule_BlankNodePropertyListPath,
	rule_Collection,
	rule_CollectionPath,
	rule_GraphNode,
	rule_GraphNodePath,
	rule_VarOrTerm,
	rule_VarOrIri,
	rule_Var,
	rule_GraphTerm,
	rule_Expression,
	rule_ConditionalOrExpression,
	rule_ConditionalAndExpression,
	rule_ValueLogical,
	rule_RelationalExpression,
	rule_NumericExpression,
	rule_AdditiveExpression,
	rule_MultiplicativeExpression,
	rule_UnaryExpression,
	rule_PrimaryExpression,
	rule_iriOrFunction,
	rule_BrackettedExpression,
	rule_BuiltInCall,
	rule_RegexExpression,
	rule_SubstringExpression,
	rule_StrReplaceExpression,
	rule_ExistsFunc,
	rule_NotExistsFunc,
	rule_Aggregate,
	rule_RDFLiteral,
	rule_NumericLiteral,
	rule_NumericLiteralUnsigned,
	rule_NumericLiteralPositive,
	rule_NumericLiteralNegative,
	rule_BooleanLiteral,
	rule_String,
	rule_iri,
	rule_PrefixedName,
	rule_BlankNode
};

static const TrackerTerminalFunc terminal_funcs[N_TERMINAL_TYPES] = {
	terminal_IRIREF,
	terminal_PNAME_NS,
	terminal_PNAME_LN,
	terminal_BLANK_NODE_LABEL,
	terminal_VAR1,
	terminal_VAR2,
	terminal_LANGTAG,
	terminal_INTEGER,
	terminal_DECIMAL,
	terminal_DOUBLE,
	terminal_INTEGER_POSITIVE,
	terminal_DECIMAL_POSITIVE,
	terminal_DOUBLE_POSITIVE,
	terminal_INTEGER_NEGATIVE,
	terminal_DECIMAL_NEGATIVE,
	terminal_DOUBLE_NEGATIVE,
	terminal_STRING_LITERAL1,
	terminal_STRING_LITERAL2,
	terminal_STRING_LITERAL_LONG1,
	terminal_STRING_LITERAL_LONG2,
	terminal_NIL,
	terminal_ANON,
	terminal_PARAMETERIZED_VAR,
};

static inline const TrackerGrammarRule *
tracker_grammar_rule_get_children (const TrackerGrammarRule *rule)
{
	if (rule->type == RULE_TYPE_RULE) {
		g_assert (rule->data.rule < N_NAMED_RULES);
		return named_rules[rule->data.rule];
	} else if (rule->type != RULE_TYPE_LITERAL &&
		   rule->type != RULE_TYPE_TERMINAL) {
		return rule->data.children;
	}

	return NULL;
}

static inline TrackerTerminalFunc
tracker_grammar_rule_get_terminal_func (const TrackerGrammarRule *rule)
{
	if (rule->type == RULE_TYPE_TERMINAL) {
		g_assert (rule->data.terminal < N_TERMINAL_TYPES);
		return terminal_funcs[rule->data.terminal];
	}

	return NULL;
}

static inline gboolean
tracker_grammar_rule_is_a (const TrackerGrammarRule *rule,
			   TrackerGrammarRuleType    rule_type,
			   guint                     value)
{
	if (rule->type != rule_type)
		return FALSE;

	switch (rule->type) {
	case RULE_TYPE_NIL:
	case RULE_TYPE_SEQUENCE:
	case RULE_TYPE_OR:
	case RULE_TYPE_GT0:
	case RULE_TYPE_GTE0:
	case RULE_TYPE_OPTIONAL:
		return TRUE;
	case RULE_TYPE_RULE:
		g_assert (value < N_NAMED_RULES);
		return (rule->data.rule == (TrackerGrammarNamedRule) value);
	case RULE_TYPE_TERMINAL:
		g_assert (value < N_TERMINAL_TYPES);
		return (rule->data.terminal == (TrackerGrammarTerminalType) value);
	case RULE_TYPE_LITERAL:
		g_assert (value < N_LITERALS);
		return (rule->data.literal == (TrackerGrammarLiteral) value);
	}

	return FALSE;
}

#endif /* __TRACKER_GRAMMAR_H__ */
