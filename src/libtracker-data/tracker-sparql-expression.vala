/*
 * Copyright (C) 2008-2010, Nokia
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

class Tracker.Sparql.Expression : Object {
	weak Query query;

	const int MAX_VARIABLES_FOR_IN = 20;
	const string XSD_NS = "http://www.w3.org/2001/XMLSchema#";
	const string FN_NS = "http://www.w3.org/2005/xpath-functions#";
	const string FTS_NS = "http://www.tracker-project.org/ontologies/fts#";
	const string TRACKER_NS = "http://www.tracker-project.org/ontologies/tracker#";

	string? fts_sql;

	public Expression (Query query) {
		this.query = query;
	}

	Context context {
		get { return query.context; }
	}

	Pattern pattern {
		get { return query.pattern; }
	}

	inline bool next () throws Sparql.Error {
		return query.next ();
	}

	inline SparqlTokenType current () {
		return query.current ();
	}

	inline SparqlTokenType last () {
		return query.last ();
	}

	inline bool accept (SparqlTokenType type) throws Sparql.Error {
		return query.accept (type);
	}

	Sparql.Error get_error (string msg) {
		return query.get_error (msg);
	}

	bool expect (SparqlTokenType type) throws Sparql.Error {
		return query.expect (type);
	}

	string get_last_string (int strip = 0) {
		return query.get_last_string (strip);
	}

	string escape_sql_string_literal (string literal) {
		return "'%s'".printf (string.joinv ("''", literal.split ("'")));
	}

	bool maybe_numeric (PropertyType type) {
		return (type == PropertyType.INTEGER || type == PropertyType.DOUBLE || type == PropertyType.DATE || type == PropertyType.DATETIME || type == PropertyType.UNKNOWN);
	}

	void append_collate (StringBuilder sql) {
		sql.append_printf (" COLLATE %s", COLLATION_NAME);
	}

	void skip_bracketted_expression () throws Sparql.Error {
		expect (SparqlTokenType.OPEN_PARENS);
		while (true) {
			switch (current ()) {
			case SparqlTokenType.OPEN_PARENS:
				// skip nested bracketted expression
				skip_bracketted_expression ();
				continue;
			case SparqlTokenType.CLOSE_PARENS:
			case SparqlTokenType.EOF:
				break;
			default:
				next ();
				continue;
			}
			break;
		}
		expect (SparqlTokenType.CLOSE_PARENS);
	}

	internal void skip_select_variables () throws Sparql.Error {
		while (true) {
			switch (current ()) {
			case SparqlTokenType.OPEN_PARENS:
				skip_bracketted_expression ();
				continue;
			case SparqlTokenType.FROM:
			case SparqlTokenType.WHERE:
			case SparqlTokenType.OPEN_BRACE:
			case SparqlTokenType.GROUP:
			case SparqlTokenType.ORDER:
			case SparqlTokenType.LIMIT:
			case SparqlTokenType.OFFSET:
			case SparqlTokenType.EOF:
				break;
			default:
				next ();
				continue;
			}
			break;
		}
	}

	internal PropertyType translate_select_expression (StringBuilder sql, bool subquery, int variable_index) throws Sparql.Error {
		Variable variable = null;
		bool expect_close_parens = false;
		bool as_handled = false;

		long begin = sql.len;
		var type = PropertyType.UNKNOWN;
		if (current () == SparqlTokenType.VAR) {
			type = translate_expression (sql);
			// we need variable name in case of compositional subqueries
			variable = context.get_variable (get_last_string ().substring (1));

			if (variable.binding == null) {
				throw get_error ("use of undefined variable `%s'".printf (variable.name));
			}
		} else if (accept (SparqlTokenType.OPEN_PARENS)) {
			if (current () == SparqlTokenType.SELECT) {
				// no parenthesis around expression
				// deprecated but supported for backward compatibility
				sql.append ("(");
				var select_context = pattern.translate_select (sql, true, true);
				sql.append (")");

				expect (SparqlTokenType.CLOSE_PARENS);
				type = select_context.type;
			} else {
				type = translate_expression (sql);
				if (accept (SparqlTokenType.CLOSE_PARENS)) {
					// missing AS
					// deprecated but supported for backward compatibility
				} else {
					// syntax from SPARQL 1.1 Draft
					// (Expression AS Var)
					expect_close_parens = true;
				}
			}
		} else {
			// no parenthesis around expression
			// deprecated but supported for backward compatibility
			type = translate_expression (sql);
		}

		if (!subquery) {
			convert_expression_to_string (sql, type, begin);
		}

		if (accept (SparqlTokenType.AS)) {
			if (accept (SparqlTokenType.PN_PREFIX)) {
				// deprecated but supported for backward compatibility
				// (...) AS foo
				variable = context.get_variable (get_last_string ());
			} else {
				// syntax from SPARQL 1.1 Draft
				// (...) AS ?foo
				expect (SparqlTokenType.VAR);
				variable = context.get_variable (get_last_string ().substring (1));
			}
			sql.append_printf (" AS %s", variable.sql_expression);
			as_handled = true;

			if (subquery) {
				var binding = new VariableBinding ();
				binding.data_type = type;
				binding.variable = variable;
				binding.sql_expression = variable.sql_expression;
				pattern.add_variable_binding (new StringBuilder (), binding, VariableState.BOUND);
			}
		}

		if (pattern.fts_subject != null) {
			if (variable == null) {
				// FTS matches still need aliases as the outer MATCH query
				// will fetch futher values from the joined select
				variable = context.get_variable ("var%d".printf (variable_index + 1));
			}

			if (fts_sql == null) {
				pattern.fts_variables += variable.sql_expression;

				if (as_handled == false) {
					sql.append_printf (" AS %s", variable.sql_expression);
				}
			} else {
				pattern.fts_variables += fts_sql;
				pattern.queries_fts_data = true;
			}
		}

		if (expect_close_parens) {
			expect (SparqlTokenType.CLOSE_PARENS);
		}

		if (variable != null) {
			int state = context.var_set.lookup (variable);
			if (state == 0) {
				state = VariableState.BOUND;
			}
			context.select_var_set.insert (variable, state);

			((SelectContext) context).variable_names += variable.name;
		} else {
			((SelectContext) context).variable_names += "var%d".printf (variable_index + 1);
		}

		fts_sql = null;

		return type;
	}

	void translate_expression_as_order_condition (StringBuilder sql) throws Sparql.Error {
		long begin = sql.len;
		if (translate_expression (sql) == PropertyType.RESOURCE) {
			// ID => Uri
			sql.insert (begin, "(SELECT Uri FROM Resource WHERE ID = ");
			sql.append (")");
		}
	}

	internal void translate_order_condition (StringBuilder sql) throws Sparql.Error {
		if (accept (SparqlTokenType.ASC)) {
			translate_expression_as_order_condition (sql);
			sql.append (" ASC");
		} else if (accept (SparqlTokenType.DESC)) {
			translate_expression_as_order_condition (sql);
			sql.append (" DESC");
		} else {
			translate_expression_as_order_condition (sql);
		}
	}

	void translate_bound_call (StringBuilder sql) throws Sparql.Error {
		expect (SparqlTokenType.BOUND);
		expect (SparqlTokenType.OPEN_PARENS);
		sql.append ("(");
		translate_expression (sql);
		sql.append (" IS NOT NULL)");
		expect (SparqlTokenType.CLOSE_PARENS);
	}

	PropertyType translate_if_call (StringBuilder sql) throws Sparql.Error {
		expect (SparqlTokenType.IF);
		expect (SparqlTokenType.OPEN_PARENS);

		// condition
		sql.append ("(CASE ");
		translate_expression (sql);

		// if condition is true
		sql.append (" WHEN 1 THEN ");
		expect (SparqlTokenType.COMMA);
		var type = translate_expression (sql);

		// if condition is false
		sql.append (" WHEN 0 THEN ");
		expect (SparqlTokenType.COMMA);
		translate_expression (sql);

		sql.append (" ELSE NULL END)");

		expect (SparqlTokenType.CLOSE_PARENS);

		return type;
	}

	void translate_regex (StringBuilder sql) throws Sparql.Error {
		expect (SparqlTokenType.REGEX);
		expect (SparqlTokenType.OPEN_PARENS);
		sql.append ("SparqlRegex(");
		translate_expression_as_string (sql);
		sql.append (", ");
		expect (SparqlTokenType.COMMA);
		// SQLite's sqlite3_set_auxdata doesn't work correctly with bound
		// strings for the regex in function_sparql_regex.
		// translate_expression (sql);
		sql.append (escape_sql_string_literal (parse_string_literal ()));
		sql.append (", ");
		if (accept (SparqlTokenType.COMMA)) {
			// Same as above
			// translate_expression (sql);
			sql.append (escape_sql_string_literal (parse_string_literal ()));
		} else {
			sql.append ("''");
		}
		sql.append (")");
		expect (SparqlTokenType.CLOSE_PARENS);
	}

	void translate_exists (StringBuilder sql) throws Sparql.Error {
		sql.append ("(");
		pattern.translate_exists (sql);
		sql.append (")");
	}

	internal static void append_expression_as_string (StringBuilder sql, string expression, PropertyType type) {
		long begin = sql.len;
		sql.append (expression);
		convert_expression_to_string (sql, type, begin);
	}

	static void convert_expression_to_string (StringBuilder sql, PropertyType type, long begin) {
		switch (type) {
		case PropertyType.STRING:
		case PropertyType.INTEGER:
			// nothing to convert
			// do not use CAST to convert integers to strings as this breaks use
			// of index when sorting by variable introduced in select expression
			break;
		case PropertyType.RESOURCE:
			// ID => Uri
			sql.insert (begin, "(SELECT Uri FROM Resource WHERE ID = ");
			sql.append (")");
			break;
		case PropertyType.BOOLEAN:
			// 0/1 => false/true
			sql.insert (begin, "CASE ");
			sql.append (" WHEN 1 THEN 'true' WHEN 0 THEN 'false' ELSE NULL END");
			break;
		case PropertyType.DATE:
			// ISO 8601 format
			sql.insert (begin, "strftime (\"%Y-%m-%d\", ");
			sql.append (", \"unixepoch\")");
			break;
		case PropertyType.DATETIME:
			// ISO 8601 format
			sql.insert (begin, "SparqlFormatTime (");
			sql.append (")");
			break;
		default:
			// let sqlite convert the expression to string
			sql.insert (begin, "CAST (");
			sql.append (" AS TEXT)");
			break;
		}
	}

	void translate_expression_as_string (StringBuilder sql) throws Sparql.Error {
		switch (current ()) {
		case SparqlTokenType.IRI_REF:
		case SparqlTokenType.PN_PREFIX:
		case SparqlTokenType.COLON:
			// handle IRI literals separately as it wouldn't work for unknown IRIs otherwise
			var binding = new LiteralBinding ();
			bool is_var;
			binding.literal = pattern.parse_var_or_term (null, out is_var);
			if (accept (SparqlTokenType.OPEN_PARENS)) {
				// function call
				long begin = sql.len;
				var type = translate_function (sql, binding.literal);
				expect (SparqlTokenType.CLOSE_PARENS);
				convert_expression_to_string (sql, type, begin);
			} else {
				sql.append ("?");
				query.bindings.append (binding);
			}
			break;
		default:
			long begin = sql.len;
			var type = translate_expression (sql);
			convert_expression_to_string (sql, type, begin);
			break;
		}
	}

	void translate_str (StringBuilder sql) throws Sparql.Error {
		expect (SparqlTokenType.STR);
		expect (SparqlTokenType.OPEN_PARENS);

		translate_expression_as_string (sql);

		expect (SparqlTokenType.CLOSE_PARENS);
	}

	void translate_isuri (StringBuilder sql) throws Sparql.Error {
		if (!accept (SparqlTokenType.ISURI)) {
			expect (SparqlTokenType.ISIRI);
		}

		expect (SparqlTokenType.OPEN_PARENS);

		sql.append ("?");
		var new_binding = new LiteralBinding ();
		new_binding.data_type = PropertyType.INTEGER;

		if (current() == SparqlTokenType.IRI_REF) {
			new_binding.literal = "1";
			next ();
		} else if (translate_expression (new StringBuilder ()) == PropertyType.RESOURCE) {
			new_binding.literal = "1";
		} else {
			new_binding.literal = "0";
		}

		query.bindings.append (new_binding);

		expect (SparqlTokenType.CLOSE_PARENS);
	}

	void translate_datatype (StringBuilder sql) throws Sparql.Error {
		expect (SparqlTokenType.DATATYPE);
		expect (SparqlTokenType.OPEN_PARENS);

		if (accept (SparqlTokenType.VAR)) {
			string variable_name = get_last_string().substring(1);
			var variable = context.get_variable (variable_name);

			if (variable.binding == null) {
				throw get_error ("`%s' is not a valid variable".printf (variable.name));
			}

			if (variable.binding.data_type == PropertyType.RESOURCE || variable.binding.type == null) {
				throw get_error ("Invalid FILTER");
			}

			sql.append ("(SELECT ID FROM Resource WHERE Uri = ?)");

			var new_binding = new LiteralBinding ();
			new_binding.literal = variable.binding.type.uri;
			query.bindings.append (new_binding);

		} else {
			throw get_error ("Invalid FILTER");
		}

		expect (SparqlTokenType.CLOSE_PARENS);
	}

	PropertyType translate_function (StringBuilder sql, string uri) throws Sparql.Error {
		if (uri == XSD_NS + "string") {
			// conversion to string
			translate_expression_as_string (sql);

			return PropertyType.STRING;
		} else if (uri == XSD_NS + "integer") {
			// conversion to integer
			sql.append ("CAST (");
			translate_expression_as_string (sql);
			sql.append (" AS INTEGER)");

			return PropertyType.INTEGER;
		} else if (uri == XSD_NS + "double") {
			// conversion to double
			sql.append ("CAST (");
			translate_expression_as_string (sql);
			sql.append (" AS REAL)");

			return PropertyType.DOUBLE;
		} else if (uri == TRACKER_NS + "case-fold") {
			// conversion to string
			sql.append ("SparqlCaseFold (");
			translate_expression_as_string (sql);
			sql.append (")");
			return PropertyType.STRING;
		} else if (uri == TRACKER_NS + "ascii-lower-case") {
			// conversion to string
			sql.append ("lower (");
			translate_expression_as_string (sql);
			sql.append (")");
			return PropertyType.STRING;
		} else if (uri == FN_NS + "lower-case") {
			// conversion to string
			sql.append ("SparqlLowerCase (");
			translate_expression_as_string (sql);
			sql.append (")");
			return PropertyType.STRING;
		} else if (uri == TRACKER_NS + "normalize") {
			// conversion to string
			sql.append ("SparqlNormalize (");
			translate_expression_as_string (sql);
			sql.append (", ");
			expect (SparqlTokenType.COMMA);
			translate_expression_as_string (sql);
			sql.append (")");
			return PropertyType.STRING;
		} else if (uri == TRACKER_NS + "unaccent") {
			// conversion to string
			sql.append ("SparqlUnaccent (");
			translate_expression_as_string (sql);
			sql.append (")");
			return PropertyType.STRING;
		} else if (uri == FN_NS + "contains") {
			// fn:contains('A','B') => 'A' GLOB '*B*'
			sql.append ("(");
			translate_expression_as_string (sql);
			sql.append (" GLOB ");
			expect (SparqlTokenType.COMMA);

			sql.append ("?");
			var binding = new LiteralBinding ();
			binding.literal = "*%s*".printf (parse_string_literal ());
			query.bindings.append (binding);

			sql.append (")");

			return PropertyType.BOOLEAN;
		} else if (uri == FN_NS + "starts-with") {
			// fn:starts-with('A','B') => 'A' BETWEEN 'B' AND 'B\u0010fffd'
			// 0010fffd always sorts last

			translate_expression_as_string (sql);
			sql.append (" BETWEEN ");

			expect (SparqlTokenType.COMMA);
			string prefix = parse_string_literal ();

			sql.append ("?");
			var binding = new LiteralBinding ();
			binding.literal = prefix;
			query.bindings.append (binding);

			sql.append (" AND ");

			sql.append ("?");
			binding = new LiteralBinding ();
			binding.literal = prefix + COLLATION_LAST_CHAR.to_string ();
			query.bindings.append (binding);

			return PropertyType.BOOLEAN;
		} else if (uri == FN_NS + "ends-with") {
			// fn:ends-with('A','B') => 'A' GLOB '*B'
			sql.append ("(");
			translate_expression_as_string (sql);
			sql.append (" GLOB ");
			expect (SparqlTokenType.COMMA);

			sql.append ("?");
			var binding = new LiteralBinding ();
			binding.literal = "*%s".printf (parse_string_literal ());
			query.bindings.append (binding);

			sql.append (")");

			return PropertyType.BOOLEAN;
		} else if (uri == FN_NS + "substring") {
			sql.append ("substr(");
			translate_expression_as_string (sql);

			sql.append (", ");
			expect (SparqlTokenType.COMMA);
			translate_expression_as_string (sql);

			if (accept (SparqlTokenType.COMMA)) {
			      sql.append (", ");
			      translate_expression_as_string (sql);
			}

			sql.append (")");

			return PropertyType.STRING;
		} else if (uri == FN_NS + "concat") {
			translate_expression_as_string (sql);
			sql.append ("||");
			expect (SparqlTokenType.COMMA);
			translate_expression_as_string (sql);
			while (accept (SparqlTokenType.COMMA)) {
			      sql.append ("||");
			      translate_expression_as_string (sql);
			}

			return PropertyType.STRING;
		} else if (uri == FN_NS + "string-join") {
			sql.append ("SparqlStringJoin(");
			expect (SparqlTokenType.OPEN_PARENS);

			translate_expression_as_string (sql);
			sql.append (", ");
			expect (SparqlTokenType.COMMA);
			translate_expression_as_string (sql);
			while (accept (SparqlTokenType.COMMA)) {
			      sql.append (", ");
			      translate_expression_as_string (sql);
			}

			expect (SparqlTokenType.CLOSE_PARENS);
			sql.append (",");
			expect (SparqlTokenType.COMMA);
			translate_expression (sql);
			sql.append (")");

			return PropertyType.STRING;
		} else if (uri == FN_NS + "year-from-dateTime") {
			expect (SparqlTokenType.VAR);
			string variable_name = get_last_string ().substring (1);
			var variable = context.get_variable (variable_name);

			sql.append ("strftime (\"%Y\", ");
			sql.append (variable.get_extra_sql_expression ("localDate"));
			sql.append (" * 24 * 3600, \"unixepoch\")");

			return PropertyType.INTEGER;
		} else if (uri == FN_NS + "month-from-dateTime") {
			expect (SparqlTokenType.VAR);
			string variable_name = get_last_string ().substring (1);
			var variable = context.get_variable (variable_name);

			sql.append ("strftime (\"%m\", ");
			sql.append (variable.get_extra_sql_expression ("localDate"));
			sql.append (" * 24 * 3600, \"unixepoch\")");

			return PropertyType.INTEGER;
		} else if (uri == FN_NS + "day-from-dateTime") {
			expect (SparqlTokenType.VAR);
			string variable_name = get_last_string ().substring (1);
			var variable = context.get_variable (variable_name);

			sql.append ("strftime (\"%d\", ");
			sql.append (variable.get_extra_sql_expression ("localDate"));
			sql.append (" * 24 * 3600, \"unixepoch\")");

			return PropertyType.INTEGER;
		} else if (uri == FN_NS + "hours-from-dateTime") {
			expect (SparqlTokenType.VAR);
			string variable_name = get_last_string ().substring (1);
			var variable = context.get_variable (variable_name);

			sql.append ("(");
			sql.append (variable.get_extra_sql_expression ("localTime"));
			sql.append (" / 3600)");

			return PropertyType.INTEGER;
		} else if (uri == FN_NS + "minutes-from-dateTime") {
			expect (SparqlTokenType.VAR);
			string variable_name = get_last_string ().substring (1);
			var variable = context.get_variable (variable_name);

			sql.append ("(");
			sql.append (variable.get_extra_sql_expression ("localTime"));
			sql.append (" / 60 % 60)");

			return PropertyType.INTEGER;
		} else if (uri == FN_NS + "seconds-from-dateTime") {
			expect (SparqlTokenType.VAR);
			string variable_name = get_last_string ().substring (1);
			var variable = context.get_variable (variable_name);

			sql.append ("(");
			sql.append (variable.get_extra_sql_expression ("localTime"));
			sql.append ("% 60)");

			return PropertyType.INTEGER;
		} else if (uri == FN_NS + "timezone-from-dateTime") {
			expect (SparqlTokenType.VAR);
			string variable_name = get_last_string ().substring (1);
			var variable = context.get_variable (variable_name);

			sql.append ("(");
			sql.append (variable.get_extra_sql_expression ("localDate"));
			sql.append (" * 24 * 3600 + ");
			sql.append (variable.get_extra_sql_expression ("localTime"));
			sql.append ("- ");
			sql.append ("CAST (");
			sql.append (variable.sql_expression);
			sql.append (" AS INTEGER)");
			sql.append (")");

			return PropertyType.INTEGER;
		} else if (uri == FN_NS + "replace") {
			sql.append ("SparqlReplace(");
			translate_expression_as_string (sql);
			sql.append (", ");

			expect (SparqlTokenType.COMMA);
			translate_expression_as_string (sql);
			sql.append (", ");

			expect (SparqlTokenType.COMMA);
			translate_expression_as_string (sql);
			sql.append (", ");

			if (accept (SparqlTokenType.COMMA)) {
				sql.append (escape_sql_string_literal (parse_string_literal ()));
			} else {
				sql.append ("''");
			}
			sql.append (")");
			return PropertyType.STRING;
		} else if (uri == FTS_NS + "rank") {
			bool is_var;
			string v = pattern.parse_var_or_term (null, out is_var);
			sql.append_printf ("\"%s_u_rank\"", v);

			return PropertyType.DOUBLE;
		} else if (uri == FTS_NS + "offsets") {
			bool is_var;
			string v = pattern.parse_var_or_term (null, out is_var);
			var variable = context.get_variable (v);

			sql.append (variable.sql_expression);
			fts_sql = "tracker_offsets(offsets(\"fts\"),fts_property_names())";
			return PropertyType.STRING;
		} else if (uri == FTS_NS + "snippet") {
			bool is_var;

			string v = pattern.parse_var_or_term (null, out is_var);
			var variable = context.get_variable (v);
			var fts = new StringBuilder ();

			fts.append_printf ("snippet(\"fts\"");

			// "start match" text
			if (accept (SparqlTokenType.COMMA)) {
				fts.append (", ");
				translate_expression_as_string (fts);

				// "end match" text
				expect (SparqlTokenType.COMMA);
				fts.append (", ");
				translate_expression_as_string (fts);
			} else {
				fts.append(",'',''");
			}

			// "ellipsis" text
			if (accept (SparqlTokenType.COMMA)) {
				fts.append (", ");
				translate_expression_as_string (fts);
			} else {
				fts.append (", '...'");
			}

			// lookup column
			fts.append (", -1");

			// Approximate number of words in context
			if (accept (SparqlTokenType.COMMA)) {
				fts.append (", ");
				translate_expression_as_string (fts);
			} else {
				fts.append (", 5");
			}

			fts.append (")");

			fts_sql = fts.str;
			sql.append (variable.sql_expression);
			return PropertyType.STRING;
		} else if (uri == TRACKER_NS + "id") {
			var type = translate_expression (sql);
			if (type != PropertyType.RESOURCE) {
				throw get_error ("expected resource");
			}

			return PropertyType.INTEGER;
		} else if (uri == TRACKER_NS + "uri") {
			var type = translate_expression (sql);
			if (type != PropertyType.INTEGER) {
				throw get_error ("expected integer ID");
			}

			return PropertyType.RESOURCE;
		} else if (uri == TRACKER_NS + "cartesian-distance") {
			sql.append ("SparqlCartesianDistance(");
			translate_expression (sql);
			sql.append (", ");
			expect (SparqlTokenType.COMMA);
			translate_expression (sql);
			sql.append (", ");
			expect (SparqlTokenType.COMMA);
			translate_expression (sql);
			sql.append (", ");
			expect (SparqlTokenType.COMMA);
			translate_expression (sql);
			sql.append (")");

			return PropertyType.DOUBLE;
		} else if (uri == TRACKER_NS + "haversine-distance") {
			sql.append ("SparqlHaversineDistance(");
			translate_expression (sql);
			sql.append (", ");
			expect (SparqlTokenType.COMMA);
			translate_expression (sql);
			sql.append (", ");
			expect (SparqlTokenType.COMMA);
			translate_expression (sql);
			sql.append (", ");
			expect (SparqlTokenType.COMMA);
			translate_expression (sql);
			sql.append (")");

			return PropertyType.DOUBLE;
		} else if (uri == TRACKER_NS + "coalesce") {
			sql.append ("COALESCE(");
			translate_expression_as_string (sql);
			sql.append (", ");
			expect (SparqlTokenType.COMMA);
			translate_expression_as_string (sql);
			while (accept (SparqlTokenType.COMMA)) {
			      sql.append (", ");
			      translate_expression_as_string (sql);
			}
			sql.append (")");

			return PropertyType.STRING;
		} else if (uri == TRACKER_NS + "uri-is-parent") {
			sql.append ("SparqlUriIsParent(");
			translate_expression_as_string (sql);
			sql.append (", ");
			expect (SparqlTokenType.COMMA);

			translate_expression_as_string (sql);
			sql.append (")");

			return PropertyType.BOOLEAN;
		} else if (uri == TRACKER_NS + "uri-is-descendant") {
			sql.append ("SparqlUriIsDescendant(");
			translate_expression_as_string (sql);
			sql.append (", ");
			expect (SparqlTokenType.COMMA);

                        translate_expression_as_string (sql);
                        while (accept (SparqlTokenType.COMMA)) {
			      sql.append (", ");
			      translate_expression_as_string (sql);
			}
			sql.append (")");

			return PropertyType.BOOLEAN;
		} else if (uri == TRACKER_NS + "string-from-filename") {
			sql.append ("SparqlStringFromFilename(");
			translate_expression_as_string (sql);
			sql.append (")");

			return PropertyType.STRING;
		} else {
			// support properties as functions
			var prop = Ontologies.get_property_by_uri (uri);
			if (prop == null) {
				throw get_error ("Unknown function");
			}

			var expr = new StringBuilder ();
			translate_expression (expr);

			string value_separator = ",";
			string? graph_separator = null;

			if (accept (SparqlTokenType.COMMA)) {
				value_separator = parse_string_literal ();

				if (accept (SparqlTokenType.COMMA)) {
					graph_separator = parse_string_literal ();
				}
			}

			if (prop.multiple_values) {
				// multi-valued property
				sql.append ("(SELECT GROUP_CONCAT(");
				long begin = sql.len;
				sql.append_printf ("\"%s\"", prop.name);
				convert_expression_to_string (sql, prop.data_type, begin);
				if (graph_separator != null) {
					sql.append_printf (" || %s || COALESCE((SELECT Uri FROM Resource WHERE ID = \"%s:graph\"), '')", escape_sql_string_literal (graph_separator), prop.name);
				}
				sql.append_printf (",%s)", escape_sql_string_literal (value_separator));
				sql.append_printf (" FROM \"%s\" WHERE ID = %s)", prop.table_name, expr.str);

				return PropertyType.STRING;
			} else {
				// single-valued property
				if (graph_separator == null) {
					sql.append_printf ("(SELECT \"%s\" FROM \"%s\" WHERE ID = %s)", prop.name, prop.table_name, expr.str);

					if (prop.data_type == PropertyType.STRING) {
						append_collate (sql);
					}

					return prop.data_type;
				} else {
					sql.append ("(SELECT ");
					long begin = sql.len;
					sql.append_printf ("\"%s\"", prop.name);
					convert_expression_to_string (sql, prop.data_type, begin);
					sql.append_printf (" || %s || COALESCE((SELECT Uri FROM Resource WHERE ID = \"%s:graph\"), '')", escape_sql_string_literal (graph_separator), prop.name);
					sql.append_printf (" FROM \"%s\" WHERE ID = %s)", prop.table_name, expr.str);

					return PropertyType.STRING;
				}
			}
		}
	}

	PropertyType parse_type_uri () throws Sparql.Error {
		string type_iri;
		PropertyType type;

		if (accept (SparqlTokenType.IRI_REF)) {
			type_iri = get_last_string (1);
		} else if (accept (SparqlTokenType.PN_PREFIX)) {
			string ns = get_last_string ();
			expect (SparqlTokenType.COLON);
			type_iri = query.resolve_prefixed_name (ns, get_last_string ().substring (1));
		} else {
			expect (SparqlTokenType.COLON);
			type_iri = query.resolve_prefixed_name ("", get_last_string ().substring (1));
		}

		if (type_iri == XSD_NS + "boolean") {
			type = PropertyType.BOOLEAN;
		} else if (type_iri == XSD_NS + "integer" ||
		           type_iri == XSD_NS + "nonPositiveInteger" ||
		           type_iri == XSD_NS + "negativeInteger" ||
		           type_iri == XSD_NS + "long" ||
		           type_iri == XSD_NS + "int" ||
		           type_iri == XSD_NS + "short" ||
		           type_iri == XSD_NS + "byte" ||
		           type_iri == XSD_NS + "nonNegativeInteger" ||
		           type_iri == XSD_NS + "unsignedLong" ||
		           type_iri == XSD_NS + "unsignedInt" ||
		           type_iri == XSD_NS + "unsignedShort" ||
		           type_iri == XSD_NS + "unsignedByte" ||
		           type_iri == XSD_NS + "positiveInteger") {
			type = PropertyType.INTEGER;
		} else if (type_iri == XSD_NS + "double") {
			type = PropertyType.DOUBLE;
		} else if (type_iri == XSD_NS + "date") {
			type = PropertyType.DATE;
		} else if (type_iri == XSD_NS + "dateTime") {
			type = PropertyType.DATETIME;
		} else {
			type = PropertyType.STRING;
		}

		return type;
	}

	internal string parse_string_literal (out PropertyType type = null) throws Sparql.Error {
		type = PropertyType.STRING;

		next ();
		switch (last ()) {
		case SparqlTokenType.STRING_LITERAL1:
		case SparqlTokenType.STRING_LITERAL2:
			var sb = new StringBuilder ();

			string s = get_last_string (1);
			string* p = s;
			string* end = p + s.length;
			while ((long) p < (long) end) {
				string* q = Posix.strchr (p, '\\');
				if (q == null) {
					sb.append_len (p, (long) (end - p));
					p = end;
				} else {
					sb.append_len (p, (long) (q - p));
					p = q + 1;
					switch (((char*) p)[0]) {
					case '\'':
					case '"':
					case '\\':
						sb.append_c (((char*) p)[0]);
						break;
					case 'b':
						sb.append_c ('\b');
						break;
					case 'f':
						sb.append_c ('\f');
						break;
					case 'n':
						sb.append_c ('\n');
						break;
					case 'r':
						sb.append_c ('\r');
						break;
					case 't':
						sb.append_c ('\t');
						break;
					case 'u':
						char* ptr = (char*) p + 1;
						unichar c = (((unichar) ptr[0].xdigit_value () * 16 + ptr[1].xdigit_value ()) * 16 + ptr[2].xdigit_value ()) * 16 + ptr[3].xdigit_value ();
						sb.append_unichar (c);
						p += 4;
						break;
					}
					p++;
				}
			}

			if (accept (SparqlTokenType.DOUBLE_CIRCUMFLEX)) {
				// typed literal
				type = parse_type_uri ();
			}

			return sb.str;
		case SparqlTokenType.STRING_LITERAL_LONG1:
		case SparqlTokenType.STRING_LITERAL_LONG2:
			string result = get_last_string (3);

			if (accept (SparqlTokenType.DOUBLE_CIRCUMFLEX)) {
				// typed literal
				type = parse_type_uri ();
			}

			return result;
		default:
			throw get_error ("expected string literal");
		}
	}

	PropertyType translate_uri_expression (StringBuilder sql, string uri) throws Sparql.Error {
		if (accept (SparqlTokenType.OPEN_PARENS)) {
			// function
			var result = translate_function (sql, uri);
			expect (SparqlTokenType.CLOSE_PARENS);
			return result;
		} else {
			// resource
			sql.append ("COALESCE((SELECT ID FROM Resource WHERE Uri = ?), 0)");
			var binding = new LiteralBinding ();
			binding.literal = uri;
			query.bindings.append (binding);
			return PropertyType.RESOURCE;
		}
	}

	PropertyType translate_primary_expression (StringBuilder sql) throws Sparql.Error {
		PropertyType type;

		switch (current ()) {
		case SparqlTokenType.OPEN_PARENS:
			return translate_bracketted_expression (sql);
		case SparqlTokenType.IRI_REF:
			next ();
			return translate_uri_expression (sql, get_last_string (1));
		case SparqlTokenType.DECIMAL:
		case SparqlTokenType.DOUBLE:
			next ();

			if (query.no_cache) {
				sql.append (get_last_string ());
			} else {
				sql.append ("?");

				var binding = new LiteralBinding ();
				binding.literal = get_last_string ();
				query.bindings.append (binding);
			}

			return PropertyType.DOUBLE;
		case SparqlTokenType.TRUE:
			next ();

			if (query.no_cache) {
				sql.append ("1");
			} else {
				sql.append ("?");

				var binding = new LiteralBinding ();
				binding.literal = "1";
				binding.data_type = PropertyType.INTEGER;
				query.bindings.append (binding);
			}

			return PropertyType.BOOLEAN;
		case SparqlTokenType.FALSE:
			next ();

			if (query.no_cache) {
				sql.append ("0");
			} else {
				sql.append ("?");

				var binding = new LiteralBinding ();
				binding.literal = "0";
				binding.data_type = PropertyType.INTEGER;
				query.bindings.append (binding);
			}

			return PropertyType.BOOLEAN;
		case SparqlTokenType.STRING_LITERAL1:
		case SparqlTokenType.STRING_LITERAL2:
		case SparqlTokenType.STRING_LITERAL_LONG1:
		case SparqlTokenType.STRING_LITERAL_LONG2:
			var literal = parse_string_literal (out type);

			switch (type) {
			case PropertyType.INTEGER:
			case PropertyType.BOOLEAN:
			case PropertyType.DOUBLE:
			case PropertyType.DATE:
			case PropertyType.DATETIME:
				if (query.no_cache) {
					sql.append (escape_sql_string_literal (literal));
				} else {
					var binding = new LiteralBinding ();
					binding.literal = literal;
					binding.data_type = type;
					query.bindings.append (binding);
					sql.append ("?");
				}
				return type;
			default:
				if (query.no_cache) {
					sql.append (escape_sql_string_literal (literal));
				} else {
					var binding = new LiteralBinding ();
					binding.literal = literal;
					query.bindings.append (binding);
					sql.append ("?");
				}
				append_collate (sql);
				return PropertyType.STRING;
			}
		case SparqlTokenType.INTEGER:
			next ();

			if (query.no_cache) {
				sql.append (get_last_string ());
			} else {
				sql.append ("?");

				var binding = new LiteralBinding ();
				binding.literal = get_last_string ();
				binding.data_type = PropertyType.INTEGER;
				query.bindings.append (binding);
			}

			return PropertyType.INTEGER;
		case SparqlTokenType.VAR:
			next ();
			string variable_name = get_last_string ().substring (1);
			var variable = context.get_variable (variable_name);
			sql.append (variable.sql_expression);

			if (variable.binding == null) {
				return PropertyType.UNKNOWN;
			} else {
				if (variable.binding.data_type == PropertyType.STRING) {
					append_collate (sql);
				}
				return variable.binding.data_type;
			}
		case SparqlTokenType.STR:
			translate_str (sql);
			return PropertyType.STRING;
		case SparqlTokenType.LANG:
			next ();
			sql.append ("''");
			return PropertyType.STRING;
		case SparqlTokenType.LANGMATCHES:
			next ();
			sql.append ("0");
			return PropertyType.BOOLEAN;
		case SparqlTokenType.DATATYPE:
			translate_datatype (sql);
			return PropertyType.RESOURCE;
		case SparqlTokenType.BOUND:
			translate_bound_call (sql);
			return PropertyType.BOOLEAN;
		case SparqlTokenType.COALESCE:
			next ();
			expect (SparqlTokenType.OPEN_PARENS);
			var result = translate_function (sql, TRACKER_NS + "coalesce");
			expect (SparqlTokenType.CLOSE_PARENS);
			return result;
		case SparqlTokenType.IF:
			return translate_if_call (sql);
		case SparqlTokenType.SAMETERM:
			next ();
			expect (SparqlTokenType.OPEN_PARENS);
			sql.append ("(");
			translate_expression (sql);
			sql.append (" = ");
			expect (SparqlTokenType.COMMA);
			translate_expression (sql);
			sql.append (")");
			expect (SparqlTokenType.CLOSE_PARENS);
			return PropertyType.BOOLEAN;
		case SparqlTokenType.ISIRI:
		case SparqlTokenType.ISURI:
			translate_isuri (sql);
			return PropertyType.BOOLEAN;
		case SparqlTokenType.ISBLANK:
			next ();
			expect (SparqlTokenType.OPEN_PARENS);
			next ();
			// TODO: support ISBLANK properly
			sql.append ("0");
			expect (SparqlTokenType.CLOSE_PARENS);
			return PropertyType.BOOLEAN;
		case SparqlTokenType.ISLITERAL:
			next ();
			return PropertyType.BOOLEAN;
		case SparqlTokenType.REGEX:
			translate_regex (sql);
			query.no_cache = true;
			return PropertyType.BOOLEAN;
		case SparqlTokenType.EXISTS:
		case SparqlTokenType.NOT:
			translate_exists (sql);
			return PropertyType.BOOLEAN;
		case SparqlTokenType.COUNT:
			next ();
			sql.append ("COUNT(");
			translate_aggregate_expression (sql);
			sql.append (")");
			return PropertyType.INTEGER;
		case SparqlTokenType.SUM:
			next ();
			sql.append ("SUM(");
			type = translate_aggregate_expression (sql);
			sql.append (")");
			return type;
		case SparqlTokenType.AVG:
			next ();
			sql.append ("AVG(");
			type = translate_aggregate_expression (sql);
			sql.append (")");
			return type;
		case SparqlTokenType.MIN:
			next ();
			sql.append ("MIN(");
			type = translate_aggregate_expression (sql);
			sql.append (")");
			return type;
		case SparqlTokenType.MAX:
			next ();
			sql.append ("MAX(");
			type = translate_aggregate_expression (sql);
			sql.append (")");
			return type;
		case SparqlTokenType.GROUP_CONCAT:
			next ();
			sql.append ("GROUP_CONCAT(");
			expect (SparqlTokenType.OPEN_PARENS);
			translate_expression_as_string (sql);
			sql.append (", ");
			expect (SparqlTokenType.COMMA);
			sql.append (escape_sql_string_literal (parse_string_literal ()));
			sql.append (")");
			expect (SparqlTokenType.CLOSE_PARENS);
			return PropertyType.STRING;
		case SparqlTokenType.PN_PREFIX:
			next ();
			string ns = get_last_string ();
			expect (SparqlTokenType.COLON);
			string uri = query.resolve_prefixed_name (ns, get_last_string ().substring (1));
			return translate_uri_expression (sql, uri);
		case SparqlTokenType.COLON:
			next ();
			string uri = query.resolve_prefixed_name ("", get_last_string ().substring (1));
			return translate_uri_expression (sql, uri);
		default:
			throw get_error ("expected primary expression");
		}
	}

	PropertyType translate_unary_expression (StringBuilder sql) throws Sparql.Error {
		if (accept (SparqlTokenType.OP_NEG)) {
			sql.append ("NOT (");
			var optype = translate_primary_expression (sql);
			sql.append (")");
			if (optype != PropertyType.BOOLEAN) {
				throw get_error ("expected boolean expression");
			}
			return PropertyType.BOOLEAN;
		} else if (accept (SparqlTokenType.PLUS)) {
			return translate_primary_expression (sql);
		} else if (accept (SparqlTokenType.MINUS)) {
			sql.append ("-(");
			var optype = translate_primary_expression (sql);
			sql.append (")");
			return optype;
		}
		return translate_primary_expression (sql);
	}

	PropertyType translate_multiplicative_expression (StringBuilder sql) throws Sparql.Error {
		long begin = sql.len;
		var optype = translate_unary_expression (sql);
		while (true) {
			if (accept (SparqlTokenType.STAR)) {
				if (!maybe_numeric (optype)) {
					throw get_error ("expected numeric operand");
				}
				sql.insert (begin, "(");
				sql.append (" * ");
				if (!maybe_numeric (translate_unary_expression (sql))) {
					throw get_error ("expected numeric operand");
				}
				sql.append (")");
			} else if (accept (SparqlTokenType.DIV)) {
				if (!maybe_numeric (optype)) {
					throw get_error ("expected numeric operand");
				}
				sql.insert (begin, "(");
				sql.append (" / ");
				if (!maybe_numeric (translate_unary_expression (sql))) {
					throw get_error ("expected numeric operand");
				}
				sql.append (")");
			} else {
				break;
			}
		}
		return optype;
	}

	PropertyType translate_additive_expression (StringBuilder sql) throws Sparql.Error {
		long begin = sql.len;
		var optype = translate_multiplicative_expression (sql);
		while (true) {
			if (accept (SparqlTokenType.PLUS)) {
				if (!maybe_numeric (optype)) {
					throw get_error ("expected numeric operand");
				}
				sql.insert (begin, "(");
				sql.append (" + ");
				if (!maybe_numeric (translate_multiplicative_expression (sql))) {
					throw get_error ("expected numeric operand");
				}
				sql.append (")");
			} else if (accept (SparqlTokenType.MINUS)) {
				if (!maybe_numeric (optype)) {
					throw get_error ("expected numeric operand");
				}
				sql.insert (begin, "(");
				sql.append (" - ");
				if (!maybe_numeric (translate_multiplicative_expression (sql))) {
					throw get_error ("expected numeric operand");
				}
				sql.append (")");
			} else {
				break;
			}
		}
		return optype;
	}

	PropertyType translate_numeric_expression (StringBuilder sql) throws Sparql.Error {
		return translate_additive_expression (sql);
	}

	PropertyType process_relational_expression (StringBuilder sql, long begin, uint n_bindings, PropertyType op1type, string operator) throws Sparql.Error {
		sql.insert (begin, "(");
		sql.append (operator);
		var op2type = translate_numeric_expression (sql);
		sql.append (")");
		if ((op1type == PropertyType.DATETIME && op2type == PropertyType.STRING)
		    || (op1type == PropertyType.STRING && op2type == PropertyType.DATETIME)) {
			// TODO: improve performance (linked list)
			if (query.bindings.length () == n_bindings + 1) {
				// trigger string => datetime conversion
				query.bindings.last ().data.data_type = PropertyType.DATETIME;
			}
		} else if ((op1type == PropertyType.DATE && op2type == PropertyType.STRING)
		           || (op1type == PropertyType.STRING && op2type == PropertyType.DATE)) {
			// TODO: improve performance (linked list)
			if (query.bindings.length () == n_bindings + 1) {
				// trigger string => date conversion
				query.bindings.last ().data.data_type = PropertyType.DATE;
			}
		}
		return PropertyType.BOOLEAN;
	}

	PropertyType translate_in (StringBuilder sql, bool not) throws Sparql.Error {

		int in_variable_count = 0;

		if (not) {
			sql.append (" NOT");
		}

		expect (SparqlTokenType.OPEN_PARENS);
		sql.append (" IN (");
		if (!accept (SparqlTokenType.CLOSE_PARENS)) {
			in_variable_count++;
			translate_expression (sql);
			while (accept (SparqlTokenType.COMMA)) {
				sql.append (", ");

				in_variable_count++;

				if (in_variable_count > MAX_VARIABLES_FOR_IN && !query.no_cache) {
					query.no_cache = true;
				}

				translate_expression (sql);
			}
			expect (SparqlTokenType.CLOSE_PARENS);
		}
		sql.append (")");

		return PropertyType.BOOLEAN;
	}

	PropertyType translate_relational_expression (StringBuilder sql) throws Sparql.Error {
		long begin = sql.len;
		// TODO: improve performance (linked list)
		uint n_bindings = query.bindings.length ();
		var optype = translate_numeric_expression (sql);
		if (accept (SparqlTokenType.OP_GE)) {
			return process_relational_expression (sql, begin, n_bindings, optype, " >= ");
		} else if (accept (SparqlTokenType.OP_EQ)) {
			return process_relational_expression (sql, begin, n_bindings, optype, " = ");
		} else if (accept (SparqlTokenType.OP_NE)) {
			return process_relational_expression (sql, begin, n_bindings, optype, " <> ");
		} else if (accept (SparqlTokenType.OP_LT)) {
			return process_relational_expression (sql, begin, n_bindings, optype, " < ");
		} else if (accept (SparqlTokenType.OP_LE)) {
			return process_relational_expression (sql, begin, n_bindings, optype, " <= ");
		} else if (accept (SparqlTokenType.OP_GT)) {
			return process_relational_expression (sql, begin, n_bindings, optype, " > ");
		} else if (accept (SparqlTokenType.OP_IN)) {
			return translate_in (sql, false);
		} else if (accept (SparqlTokenType.NOT)) {
			expect (SparqlTokenType.OP_IN);
			return translate_in (sql, true);
		}
		return optype;
	}

	PropertyType translate_value_logical (StringBuilder sql) throws Sparql.Error {
		return translate_relational_expression (sql);
	}

	PropertyType translate_conditional_and_expression (StringBuilder sql) throws Sparql.Error {
		long begin = sql.len;
		var optype = translate_value_logical (sql);
		while (accept (SparqlTokenType.OP_AND)) {
			if (optype != PropertyType.BOOLEAN) {
				throw get_error ("expected boolean expression");
			}
			sql.insert (begin, "(");
			sql.append (" AND ");
			optype = translate_value_logical (sql);
			sql.append (")");
			if (optype != PropertyType.BOOLEAN) {
				throw get_error ("expected boolean expression");
			}
		}
		return optype;
	}

	PropertyType translate_conditional_or_expression (StringBuilder sql) throws Sparql.Error {
		long begin = sql.len;
		var optype = translate_conditional_and_expression (sql);
		while (accept (SparqlTokenType.OP_OR)) {
			if (optype != PropertyType.BOOLEAN) {
				throw get_error ("expected boolean expression");
			}
			sql.insert (begin, "(");
			sql.append (" OR ");
			optype = translate_conditional_and_expression (sql);
			sql.append (")");
			if (optype != PropertyType.BOOLEAN) {
				throw get_error ("expected boolean expression");
			}
		}
		return optype;
	}

	internal PropertyType translate_expression (StringBuilder sql) throws Sparql.Error {
		return translate_conditional_or_expression (sql);
	}

	PropertyType translate_bracketted_expression (StringBuilder sql) throws Sparql.Error {
		expect (SparqlTokenType.OPEN_PARENS);

		if (current () == SparqlTokenType.SELECT) {
			// scalar subquery

			sql.append ("(");
			var select_context = pattern.translate_select (sql, true, true);
			sql.append (")");

			expect (SparqlTokenType.CLOSE_PARENS);
			return select_context.type;
		}

		var optype = translate_expression (sql);
		expect (SparqlTokenType.CLOSE_PARENS);
		return optype;
	}

	PropertyType translate_aggregate_expression (StringBuilder sql) throws Sparql.Error {
		expect (SparqlTokenType.OPEN_PARENS);
		if (accept (SparqlTokenType.DISTINCT)) {
			sql.append ("DISTINCT ");
		}
		var optype = translate_expression (sql);
		expect (SparqlTokenType.CLOSE_PARENS);
		return optype;
	}

	internal PropertyType translate_constraint (StringBuilder sql) throws Sparql.Error {
		switch (current ()) {
		case SparqlTokenType.STR:
		case SparqlTokenType.LANG:
		case SparqlTokenType.LANGMATCHES:
		case SparqlTokenType.DATATYPE:
		case SparqlTokenType.BOUND:
		case SparqlTokenType.IF:
		case SparqlTokenType.SAMETERM:
		case SparqlTokenType.ISIRI:
		case SparqlTokenType.ISURI:
		case SparqlTokenType.ISBLANK:
		case SparqlTokenType.ISLITERAL:
		case SparqlTokenType.REGEX:
		case SparqlTokenType.EXISTS:
		case SparqlTokenType.NOT:
			return translate_primary_expression (sql);
		default:
			return translate_bracketted_expression (sql);
		}
	}
}
