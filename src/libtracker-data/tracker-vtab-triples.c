/*
 * Copyright (C) 2019, Red Hat Inc.
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
 *
 * Author: Carlos Garnacho <carlosg@gnome.org>
 */
#include "config.h"

#include "tracker-vtab-triples.h"

/* Define some constraints for older SQLite, we will never get
 * those in older versions, and simplifies checks in code.
 */
#if SQLITE_VERSION_NUMBER<=3021000
#define SQLITE_INDEX_CONSTRAINT_NE        68
#define SQLITE_INDEX_CONSTRAINT_ISNOTNULL 70
#define SQLITE_INDEX_CONSTRAINT_ISNULL    71
#endif

enum {
	COL_ROWID,
	COL_GRAPH,
	COL_SUBJECT,
	COL_PREDICATE,
	COL_OBJECT,
	N_COLS
};

enum {
	IDX_COL_GRAPH           = 1 << 0,
	IDX_COL_SUBJECT         = 1 << 1,
	IDX_COL_PREDICATE       = 1 << 2,
	IDX_MATCH_GRAPH_NEG     = 1 << 3,
	IDX_MATCH_SUBJECT_NEG   = 1 << 4,
	IDX_MATCH_PREDICATE_NEG = 1 << 5,
};

typedef struct {
	sqlite3 *db;
	TrackerOntologies *ontologies;
} TrackerTriplesModule;

typedef struct {
	struct sqlite3_vtab parent;
	TrackerTriplesModule *module;
	GList *cursors;
} TrackerTriplesVTab;

typedef struct {
	struct sqlite3_vtab_cursor parent;
	TrackerTriplesVTab *vtab;
	struct sqlite3_stmt *stmt;

	struct {
		sqlite3_value *graph;
		sqlite3_value *subject;
		sqlite3_value *predicate;
		sqlite3_value *object;
		guint idxFlags;
	} match;

	GList *properties;

	guint64 rowid;
	guint finished : 1;
} TrackerTriplesCursor;

static void
tracker_triples_module_free (gpointer data)
{
	TrackerTriplesModule *module = data;

	g_clear_object (&module->ontologies);
	g_free (module);
}

static void
tracker_triples_vtab_free (gpointer data)
{
	TrackerTriplesVTab *vtab = data;

	g_list_free (vtab->cursors);
	g_free (vtab);
}

static void
tracker_triples_cursor_reset (TrackerTriplesCursor *cursor)
{
	g_clear_pointer (&cursor->stmt, sqlite3_finalize);
	g_clear_pointer (&cursor->match.graph, sqlite3_value_free);
	g_clear_pointer (&cursor->match.subject, sqlite3_value_free);
	g_clear_pointer (&cursor->match.predicate, sqlite3_value_free);
	g_clear_pointer (&cursor->properties, g_list_free);
	cursor->match.idxFlags = 0;
	cursor->rowid = 0;
	cursor->finished = FALSE;
}

static void
tracker_triples_cursor_free (gpointer data)
{
	TrackerTriplesCursor *cursor = data;

	tracker_triples_cursor_reset (cursor);
	g_free (cursor);
}

static int
triples_connect (sqlite3            *db,
                 gpointer            data,
                 int                 argc,
                 const char *const  *argv,
                 sqlite3_vtab      **vtab_out,
                 char              **err_out)
{
	TrackerTriplesModule *module = data;
	TrackerTriplesVTab *vtab;
	int rc;

	vtab = g_new0 (TrackerTriplesVTab, 1);
	vtab->module = module;

	rc = sqlite3_declare_vtab (module->db,
	                           "CREATE TABLE x("
	                           "    ID INTEGER,"
	                           "    graph INTEGER,"
	                           "    subject INTEGER, "
	                           "    predicate INTEGER, "
	                           "    object INTEGER"
	                           ")");

	if (rc == SQLITE_OK) {
		*vtab_out = &vtab->parent;
	} else {
		g_free (vtab);
	}

	return rc;
}

static int
triples_best_index (sqlite3_vtab       *vtab,
                    sqlite3_index_info *info)
{
	gboolean order_by_consumed = FALSE;
	int i, argv_idx = 1, idx = 0;
	char *idx_str;

	idx_str = sqlite3_malloc (sizeof (char) * N_COLS);
	bzero (idx_str, sizeof (char) * N_COLS);

	for (i = 0; i < info->nConstraint; i++) {
		struct {
			int mask;
			int negated_mask;
		} masks [] = {
			{ IDX_COL_GRAPH, IDX_MATCH_GRAPH_NEG },
			{ IDX_COL_SUBJECT, IDX_MATCH_SUBJECT_NEG },
			{ IDX_COL_PREDICATE, IDX_MATCH_PREDICATE_NEG },
			{ 0, 0 },
		};

		if (!info->aConstraint[i].usable)
			continue;

		/* We let object be matched in upper layers, where proper
		 * translation to strings can be done.
		 */
		if (info->aConstraint[i].iColumn == COL_OBJECT)
			continue;

		if (info->aConstraint[i].iColumn == COL_ROWID)
			return SQLITE_ERROR;

		/* We can only check for (in)equality */
		if (info->aConstraint[i].op != SQLITE_INDEX_CONSTRAINT_EQ &&
		    info->aConstraint[i].op != SQLITE_INDEX_CONSTRAINT_NE &&
		    info->aConstraint[i].op != SQLITE_INDEX_CONSTRAINT_ISNULL &&
		    info->aConstraint[i].op != SQLITE_INDEX_CONSTRAINT_ISNOTNULL)
			return SQLITE_ERROR;

		/* idxNum encodes the used columns and their operators */
		idx |= masks[info->aConstraint[i].iColumn - 1].mask;

		if (info->aConstraint[i].op == SQLITE_INDEX_CONSTRAINT_NE ||
		    info->aConstraint[i].op == SQLITE_INDEX_CONSTRAINT_ISNOTNULL)
			idx |= masks[info->aConstraint[i].iColumn - 1].negated_mask;

		/* idxStr stores the mapping between columns and filter arguments */
		idx_str[info->aConstraint[i].iColumn] = argv_idx - 1;
		info->aConstraintUsage[i].argvIndex = argv_idx;
		info->aConstraintUsage[i].omit = FALSE;
		argv_idx++;
	}

	info->idxNum = idx;
	info->orderByConsumed = order_by_consumed;
	info->idxStr = idx_str;
	info->needToFreeIdxStr = TRUE;

	return SQLITE_OK;
}

static int
triples_disconnect (sqlite3_vtab *vtab)
{
	return SQLITE_OK;
}

static int
triples_destroy (sqlite3_vtab *vtab)
{
	tracker_triples_vtab_free (vtab);
	return SQLITE_OK;
}

static int
triples_open (sqlite3_vtab         *vtab_sqlite,
	      sqlite3_vtab_cursor **cursor_ret)
{
	TrackerTriplesVTab *vtab = (TrackerTriplesVTab *) vtab_sqlite;
	TrackerTriplesCursor *cursor;

	cursor = g_new0 (TrackerTriplesCursor, 1);
	cursor->vtab = vtab;
	vtab->cursors = g_list_prepend (vtab->cursors, cursor);

	*cursor_ret = &cursor->parent;
	return SQLITE_OK;
}

static int
triples_close (sqlite3_vtab_cursor *vtab_cursor)
{
	TrackerTriplesCursor *cursor = (TrackerTriplesCursor *) vtab_cursor;
	TrackerTriplesVTab *vtab = cursor->vtab;

	vtab->cursors = g_list_remove (vtab->cursors, cursor);
	tracker_triples_cursor_free (cursor);
	return SQLITE_OK;
}

static void
collect_properties (TrackerTriplesCursor *cursor)
{
	TrackerProperty **properties;
	guint n_properties, i;

	properties = tracker_ontologies_get_properties (cursor->vtab->module->ontologies,
							&n_properties);
	for (i = 0; i < n_properties; i++) {
		if (cursor->match.predicate) {
			gboolean negated = !!(cursor->match.idxFlags & IDX_MATCH_PREDICATE_NEG);
			gboolean equals =
				(sqlite3_value_int64 (cursor->match.predicate) ==
				 tracker_property_get_id (properties[i]));

			if (equals == negated)
				continue;
		}

		cursor->properties = g_list_prepend (cursor->properties,
		                                     properties[i]);
	}
}

static gchar *
convert_to_string (const gchar         *table_name,
		   TrackerPropertyType  type)
{
	switch (type) {
	case TRACKER_PROPERTY_TYPE_STRING:
	case TRACKER_PROPERTY_TYPE_INTEGER:
		return g_strdup_printf ("t.\"%s\"", table_name);
	case TRACKER_PROPERTY_TYPE_RESOURCE:
		return g_strdup_printf ("(SELECT Uri FROM Resource WHERE ID = t.\"%s\")",
		                        table_name);
	case TRACKER_PROPERTY_TYPE_BOOLEAN:
		return g_strdup_printf ("CASE t.\"%s\" "
		                        "WHEN 1 THEN 'true' "
		                        "WHEN 0 THEN 'false' "
		                        "ELSE NULL END",
		                        table_name);
	case TRACKER_PROPERTY_TYPE_DATE:
		return g_strdup_printf ("strftime (\"%%Y-%%m-%%d\", t.\"%s\", \"unixepoch\")",
		                        table_name);
	case TRACKER_PROPERTY_TYPE_DATETIME:
		return g_strdup_printf ("SparqlFormatTime (t.\"%s\")",
		                        table_name);
	default:
		/* Let sqlite convert the expression to string */
		return g_strdup_printf ("CAST (t.\"%s\" AS TEXT)",
		                        table_name);
	}
}

static void
add_arg_check (GString       *str,
	       sqlite3_value *value,
	       gboolean       negated,
	       const gchar   *var_name)
{
	if (sqlite3_value_type (value) == SQLITE_NULL) {
		if (negated)
			g_string_append (str, "IS NOT NULL ");
		else
			g_string_append (str, "IS NULL ");
	} else {
		if (negated)
			g_string_append_printf (str, "!= %s ", var_name);
		else
			g_string_append_printf (str, "= %s ", var_name);
	}
}

static void
bind_arg (sqlite3_stmt  *stmt,
	  sqlite3_value *value,
	  const gchar   *var_name)
{
	gint idx;

	if (sqlite3_value_type (value) == SQLITE_NULL)
		return;

	idx = sqlite3_bind_parameter_index (stmt, var_name);
	if (idx == 0)
		return;

	sqlite3_bind_value (stmt, idx, value);
}

static int
init_stmt (TrackerTriplesCursor *cursor)
{
	TrackerProperty *property;
	GString *sql;
	int rc;

	while (cursor->properties) {
		gchar *string_expr;

		property = cursor->properties->data;
		cursor->properties = g_list_remove (cursor->properties, property);

		string_expr = convert_to_string (tracker_property_get_name (property),
						 tracker_property_get_data_type (property));

		sql = g_string_new (NULL);
		g_string_append_printf (sql,
		                        "SELECT t.\"%s:graph\", t.ID, "
		                        "       (SELECT ID From Resource WHERE Uri = \"%s\"), "
		                        "       %s "
		                        "FROM \"%s\" AS t "
		                        "WHERE 1 ",
		                        tracker_property_get_name (property),
		                        tracker_property_get_uri (property),
		                        string_expr,
		                        tracker_property_get_table_name (property));

		if (cursor->match.graph) {
			g_string_append_printf (sql,
			                        "AND t.\"%s:graph\" ",
			                        tracker_property_get_name (property));
			add_arg_check (sql, cursor->match.graph,
			               !!(cursor->match.idxFlags & IDX_MATCH_GRAPH_NEG),
			               "@g");
		}

		if (cursor->match.subject) {
			g_string_append (sql, "AND t.ID ");
			add_arg_check (sql, cursor->match.subject,
			               !!(cursor->match.idxFlags & IDX_MATCH_SUBJECT_NEG),
			               "@s");
		}

		rc = sqlite3_prepare_v2 (cursor->vtab->module->db,
					 sql->str, -1, &cursor->stmt, 0);
		g_string_free (sql, TRUE);
		g_free (string_expr);

		if (rc == SQLITE_OK) {
			if (cursor->match.graph)
				bind_arg (cursor->stmt, cursor->match.graph, "@g");
			if (cursor->match.subject)
				bind_arg (cursor->stmt, cursor->match.subject, "@s");

			rc = sqlite3_step (cursor->stmt);
		}

		if (rc != SQLITE_DONE)
			return rc;

		g_clear_pointer (&cursor->stmt, sqlite3_finalize);
	}

	return SQLITE_DONE;
}

static int
triples_filter (sqlite3_vtab_cursor  *vtab_cursor,
                int                   idx,
                const char           *idx_str,
                int                   argc,
                sqlite3_value       **argv)
{
	TrackerTriplesCursor *cursor = (TrackerTriplesCursor *) vtab_cursor;
	int rc;

	tracker_triples_cursor_reset (cursor);

	if (idx & IDX_COL_GRAPH) {
		int idx = idx_str[COL_GRAPH];
		cursor->match.graph = sqlite3_value_dup (argv[idx]);
	}

	if (idx & IDX_COL_SUBJECT) {
		int idx = idx_str[COL_SUBJECT];
		cursor->match.subject = sqlite3_value_dup (argv[idx]);
	}

	if (idx & IDX_COL_PREDICATE) {
		int idx = idx_str[COL_PREDICATE];
		cursor->match.predicate = sqlite3_value_dup (argv[idx]);
	}

	cursor->match.idxFlags = idx;

	collect_properties (cursor);

	rc = init_stmt (cursor);

	if (rc == SQLITE_DONE)
		cursor->finished = TRUE;

	if (rc == SQLITE_ROW || rc == SQLITE_DONE)
		return SQLITE_OK;

	return rc;
}

static int
triples_next (sqlite3_vtab_cursor *vtab_cursor)
{
	TrackerTriplesCursor *cursor = (TrackerTriplesCursor *) vtab_cursor;
	int rc;

	rc = sqlite3_step (cursor->stmt);

	if (rc == SQLITE_DONE) {
		g_clear_pointer (&cursor->stmt, sqlite3_finalize);
		rc = init_stmt (cursor);
	}

	if (rc == SQLITE_ROW) {
		cursor->rowid++;
	} else {
		cursor->finished = TRUE;
	}

	if (rc != SQLITE_ROW && rc != SQLITE_DONE)
		return rc;

	return SQLITE_OK;
}

static int
triples_eof (sqlite3_vtab_cursor *vtab_cursor)
{
	TrackerTriplesCursor *cursor = (TrackerTriplesCursor *) vtab_cursor;

	return cursor->finished;
}

static int
triples_column (sqlite3_vtab_cursor *vtab_cursor,
                sqlite3_context     *context,
                int                  n_col)
{
	TrackerTriplesCursor *cursor = (TrackerTriplesCursor *) vtab_cursor;
	sqlite3_value *value;

	if (n_col == COL_ROWID) {
		sqlite3_result_int64 (context, cursor->rowid);
	} else {
		value = sqlite3_column_value (cursor->stmt, n_col - 1);
		sqlite3_result_value (context, value);
	}

	return SQLITE_OK;
}

static int
triples_rowid (sqlite3_vtab_cursor *vtab_cursor,
               sqlite_int64        *rowid_out)
{
	TrackerTriplesCursor *cursor = (TrackerTriplesCursor *) vtab_cursor;

	*rowid_out = cursor->rowid;
	return SQLITE_OK;
}

void
tracker_vtab_triples_init (sqlite3           *db,
                           TrackerOntologies *ontologies)
{
	TrackerTriplesModule *module;
	static const sqlite3_module triples_module = {
		2, /* version */
		NULL, /* create(), null because this is an eponymous-only table */
		triples_connect,
		triples_best_index,
		triples_disconnect,
		triples_destroy,
		triples_open,
		triples_close,
		triples_filter,
		triples_next,
		triples_eof,
		triples_column,
		triples_rowid,
		NULL, /* update */
		NULL, /* begin */
		NULL, /* sync */
		NULL, /* commit */
		NULL, /* rollback */
		NULL, /* find function */
		NULL, /* rename */
		NULL, /* savepoint */
		NULL, /* release */
		NULL, /* rollback to */
	};

	module = g_new0 (TrackerTriplesModule, 1);
	module->db = db;
	g_set_object (&module->ontologies, ontologies);
	sqlite3_create_module_v2 (db, "tracker_triples", &triples_module,
	                          module, tracker_triples_module_free);
}
