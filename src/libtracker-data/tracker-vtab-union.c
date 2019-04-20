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

#include "tracker-vtab-union.h"

/* Define some constraints for older SQLite, we will never get
 * those in older versions, and simplifies checks in code.
 */
#if SQLITE_VERSION_NUMBER<=3021000
#define SQLITE_INDEX_CONSTRAINT_NE        68
#endif

typedef struct {
	sqlite3 *db;
	TrackerOntologies *ontologies;
} TrackerUnionModule;

typedef struct {
	struct sqlite3_vtab parent;
	TrackerUnionModule *module;
	GList *cursors;
	GPtrArray *col_names;
	gchar *table_name;
} TrackerUnionVTab;

typedef struct {
	struct sqlite3_vtab_cursor parent;
	TrackerUnionVTab *vtab;
	GPtrArray *databases;
	GString *where_clause;
	GPtrArray *values;
	GPtrArray *cached_stmts;
	sqlite3_stmt *cur_stmt;
	guint64 rowid;
	guint cur_db;
	guint finished : 1;
} TrackerUnionCursor;

typedef struct {
	int column;
	int op;
} ConstraintData;

static void
tracker_union_module_free (gpointer data)
{
	TrackerUnionModule *module = data;

	g_clear_object (&module->ontologies);
	g_free (module);
}

static void
tracker_union_vtab_free (gpointer data)
{
	TrackerUnionVTab *vtab = data;

	g_list_free (vtab->cursors);
	g_free (vtab);
}

static void
tracker_union_cursor_free (gpointer data)
{
	TrackerUnionCursor *cursor = data;

	if (cursor->where_clause)
		g_string_free (cursor->where_clause, TRUE);
	if (cursor->cached_stmts)
		g_ptr_array_unref (cursor->cached_stmts);

	g_ptr_array_unref (cursor->databases);
	g_free (cursor);
}

static int
prepare_query (sqlite3       *db,
               sqlite3_stmt **stmt,
               const gchar   *str)
{
	int rc;

	rc = sqlite3_prepare_v2 (db, str, -1, stmt, 0);
	return rc;
}

static int
prepare_query_printf (sqlite3       *db,
                      sqlite3_stmt **stmt,
                      const gchar   *query,
                      ...)
{
	va_list args;
	gchar *str;
	int rc;

	va_start (args, query);
	str = g_strdup_vprintf (query, args);
	va_end (args);

	rc = prepare_query (db, stmt, str);
	g_free (str);

	return rc;
}

static int
union_create (sqlite3            *db,
              gpointer            data,
              int                 argc,
              const char *const  *argv,
              sqlite3_vtab      **vtab_out,
              char              **err_out)
{
	TrackerUnionModule *module = data;
	TrackerUnionVTab *vtab;
	sqlite3_stmt *stmt;
	GString *str;
	int rc;

	vtab = g_new0 (TrackerUnionVTab, 1);
	vtab->module = module;
	vtab->col_names = g_ptr_array_new_with_free_func (g_free);
	vtab->table_name = g_strdup (argv[3]);

	rc = prepare_query_printf (module->db, &stmt,
	                           "SELECT name, type "
	                           "FROM pragma_table_info (%s, \"main\")",
	                           argv[3]);
	if (rc != SQLITE_OK)
		return rc;

	str = g_string_new ("CREATE TABLE x(\n");

	while (sqlite3_step (stmt) == SQLITE_ROW) {
		g_ptr_array_add (vtab->col_names,
		                 g_strdup (sqlite3_column_text (stmt, 0)));
		g_string_append_printf (str, "\"%s\" %s,\n",
		                        sqlite3_column_text (stmt, 0),
		                        sqlite3_column_text (stmt, 1));
	}

	g_string_append (str, "graph INTEGER)");
	sqlite3_finalize (stmt);

	rc = sqlite3_declare_vtab (module->db, str->str);
	g_string_free (str, TRUE);

	if (rc == SQLITE_OK) {
		*vtab_out = &vtab->parent;
	} else {
		g_free (vtab);
	}

	return rc;
}

static int
union_best_index (sqlite3_vtab       *vtab,
                  sqlite3_index_info *info)
{
	int i, argv_idx = 1;
	ConstraintData *data;

	data = sqlite3_malloc (sizeof (ConstraintData) * info->nConstraint);
	bzero (data, sizeof (ConstraintData) * info->nConstraint);

	for (i = 0; i < info->nConstraint; i++) {
		if (!info->aConstraint[i].usable)
			continue;

		if (info->aConstraint[i].op != SQLITE_INDEX_CONSTRAINT_EQ &&
		    info->aConstraint[i].op != SQLITE_INDEX_CONSTRAINT_NE &&
		    info->aConstraint[i].op != SQLITE_INDEX_CONSTRAINT_GT &&
		    info->aConstraint[i].op != SQLITE_INDEX_CONSTRAINT_GE &&
		    info->aConstraint[i].op != SQLITE_INDEX_CONSTRAINT_LT &&
		    info->aConstraint[i].op != SQLITE_INDEX_CONSTRAINT_LE)
			continue;

		data[i].column = info->aConstraint[i].iColumn;
		data[i].op = info->aConstraint[i].op;

		info->aConstraintUsage[i].argvIndex = argv_idx;
		info->aConstraintUsage[i].omit = FALSE;
		argv_idx++;
	}

	info->orderByConsumed = FALSE;
	info->idxStr = (char *) data;
	info->needToFreeIdxStr = TRUE;

	return SQLITE_OK;
}

static int
union_destroy (sqlite3_vtab *vtab)
{
	tracker_union_vtab_free (vtab);
	return SQLITE_OK;
}

static void
cache_databases (TrackerUnionCursor *cursor)
{
	TrackerUnionVTab *vtab = cursor->vtab;
	TrackerUnionModule *module = vtab->module;
	sqlite3_stmt *stmt;
	gint rc;

	if (cursor->databases->len > 0)
		return;

	rc = prepare_query (module->db, &stmt,
	                    "SELECT name FROM pragma_database_list "
	                    "WHERE name != 'temp'");
	if (rc != SQLITE_OK)
		return;

	while ((rc = sqlite3_step (stmt)) == SQLITE_ROW) {
		g_ptr_array_add (cursor->databases,
		                 g_strdup (sqlite3_column_text (stmt, 0)));
	}

	sqlite3_finalize (stmt);
}

static int
union_open (sqlite3_vtab         *vtab_sqlite,
            sqlite3_vtab_cursor **cursor_ret)
{
	TrackerUnionVTab *vtab = (TrackerUnionVTab *) vtab_sqlite;
	TrackerUnionCursor *cursor;

	cursor = g_new0 (TrackerUnionCursor, 1);
	cursor->vtab = vtab;
	cursor->databases = g_ptr_array_new_with_free_func (g_free);
	cache_databases (cursor);

	vtab->cursors = g_list_prepend (vtab->cursors, cursor);
	*cursor_ret = (sqlite3_vtab_cursor *) cursor;

	return SQLITE_OK;
}

static int
union_close (sqlite3_vtab_cursor *vtab_cursor)
{
	TrackerUnionCursor *cursor = (TrackerUnionCursor *) vtab_cursor;
	TrackerUnionVTab *vtab = cursor->vtab;

	vtab->cursors = g_list_remove (vtab->cursors, cursor);
	tracker_union_cursor_free (cursor);
	return SQLITE_OK;
}

static const gchar *
operator_to_string (int op)
{
	if (op == SQLITE_INDEX_CONSTRAINT_EQ)
		return "=";
	else if (op == SQLITE_INDEX_CONSTRAINT_NE)
		return "!=";
	else if (op == SQLITE_INDEX_CONSTRAINT_GT)
		return ">";
	else if (op == SQLITE_INDEX_CONSTRAINT_GE)
		return ">=";
	else if (op == SQLITE_INDEX_CONSTRAINT_LT)
		return "<";
	else if (op == SQLITE_INDEX_CONSTRAINT_LE)
		return "<=";

	return NULL;
}

static sqlite3_stmt *
prepare_stmt (TrackerUnionCursor *cursor,
              const gchar        *database)
{
	TrackerUnionVTab *vtab = cursor->vtab;
	TrackerUnionModule *module = vtab->module;
	sqlite3_stmt *stmt;
	GString *str;
	int rc;

	str = g_string_new ("SELECT *, ");

	if (g_strcmp0 (database, "main") == 0) {
		g_string_append (str, "0 ");
	} else {
		g_string_append_printf (str,
		                        "(SELECT ID From Resource where Uri = \"%s\") ",
		                        database);
	}

	g_string_append_printf (str, "FROM \"%s\".%s %s",
	                        database, vtab->table_name,
	                        cursor->where_clause->str);

	rc = prepare_query (module->db, &stmt, str->str);
	g_string_free (str, TRUE);

	if (rc != SQLITE_OK)
		stmt = NULL;

	return stmt;
}

static int
init_stmt (TrackerUnionCursor *cursor)
{
	gint i, rc;

	while (cursor->cur_db < cursor->databases->len) {
		cursor->cur_stmt = g_ptr_array_index (cursor->cached_stmts,
		                                      cursor->cur_db);

		for (i = 0; i < cursor->values->len; i++) {
			sqlite3_bind_value (cursor->cur_stmt, i + 1,
			                    g_ptr_array_index (cursor->values, i));
		}

		rc = sqlite3_step (cursor->cur_stmt);

		if (rc == SQLITE_DONE) {
			cursor->cur_db++;
			continue;
		} else if (rc == SQLITE_ROW) {
			return SQLITE_OK;
		} else {
			return rc;
		}
	}

	cursor->finished = TRUE;
	return SQLITE_OK;
}

static gboolean
update_filter (TrackerUnionCursor   *cursor,
               const ConstraintData *constraints,
               guint                 n_constraints)
{
	GString *str;
	gint i;

	str = g_string_new (NULL);

	for (i = 0; i < n_constraints; i++) {
		const gchar *name, *op;

		if (constraints[i].op == 0)
			continue;
		if (constraints[i].column >= cursor->vtab->col_names->len)
			continue;

		op = operator_to_string (constraints[i].op);
		g_assert (op != NULL);

		if (i == 0)
			g_string_append (str, "WHERE ");
		else
			g_string_append (str, "AND ");

		if (constraints[i].column < 0)
			name = "ROWID";
		else
			name = g_ptr_array_index (cursor->vtab->col_names, constraints[i].column);

		g_string_append_printf (str, "\"%s\" %s ?", name, op);
	}

	if (g_strcmp0 (str->str,
	               (cursor->where_clause ?
	                cursor->where_clause->str : NULL)) == 0) {
		g_string_free (str, TRUE);
		return FALSE;
	}

	if (cursor->where_clause)
		g_string_free (cursor->where_clause, TRUE);
	cursor->where_clause = str;

	return TRUE;
}

static int
union_filter (sqlite3_vtab_cursor  *vtab_cursor,
              int                   idx,
              const char           *idx_str,
              int                   argc,
              sqlite3_value       **argv)
{
	TrackerUnionCursor *cursor = (TrackerUnionCursor *) vtab_cursor;
	const ConstraintData *constraints = (const ConstraintData *) idx_str;
	gint i;

	cursor->finished = FALSE;
	cursor->cur_db = 0;
	cursor->rowid = 0;

	if (cursor->values)
		g_ptr_array_unref (cursor->values);
	cursor->values = g_ptr_array_new_with_free_func ((GDestroyNotify) sqlite3_value_free);

	for (i = 0; i < argc; i++)
		g_ptr_array_add (cursor->values, sqlite3_value_dup (argv[i]));

	if (update_filter (cursor, constraints, argc)) {
		if (cursor->cached_stmts)
			g_ptr_array_unref (cursor->cached_stmts);
		cursor->cached_stmts = g_ptr_array_new_with_free_func ((GDestroyNotify) sqlite3_finalize);

		for (i = 0; i < cursor->databases->len; i++) {
			sqlite3_stmt *stmt;

			stmt = prepare_stmt (cursor, g_ptr_array_index (cursor->databases, i));
			g_ptr_array_add (cursor->cached_stmts, stmt);
		}
	} else {
		g_assert (cursor->cached_stmts != NULL);
		for (i = 0; i < cursor->cached_stmts->len; i++)
			sqlite3_reset (g_ptr_array_index (cursor->cached_stmts, i));
	}

	return init_stmt (cursor);
}

static int
union_next (sqlite3_vtab_cursor *vtab_cursor)
{
	TrackerUnionCursor *cursor = (TrackerUnionCursor *) vtab_cursor;
	int rc;

	rc = sqlite3_step (cursor->cur_stmt);
	cursor->rowid++;

	if (rc == SQLITE_ROW) {
		return SQLITE_OK;
	} else if (rc == SQLITE_DONE) {
		cursor->cur_db++;
		return init_stmt (cursor);
	}

	return rc;
}

static int
union_eof (sqlite3_vtab_cursor *vtab_cursor)
{
	TrackerUnionCursor *cursor = (TrackerUnionCursor *) vtab_cursor;

	return cursor->finished;
}

static int
union_column (sqlite3_vtab_cursor *vtab_cursor,
              sqlite3_context     *context,
              int                  n_col)
{
	TrackerUnionCursor *cursor = (TrackerUnionCursor *) vtab_cursor;
	sqlite3_value *value;

	value = sqlite3_column_value (cursor->cur_stmt, n_col);
	sqlite3_result_value (context, value);
	return SQLITE_OK;
}

static int
union_rowid (sqlite3_vtab_cursor *vtab_cursor,
             sqlite_int64        *rowid_out)
{
	TrackerUnionCursor *cursor = (TrackerUnionCursor *) vtab_cursor;

	*rowid_out = cursor->rowid;
	return SQLITE_OK;
}

void
tracker_vtab_union_init (sqlite3           *db,
                         TrackerOntologies *ontologies)
{
	TrackerUnionModule *module;
	static const sqlite3_module union_module = {
		2, /* version */
		union_create,
		union_create,
		union_best_index,
		union_destroy,
		union_destroy,
		union_open,
		union_close,
		union_filter,
		union_next,
		union_eof,
		union_column,
		union_rowid,
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

	module = g_new0 (TrackerUnionModule, 1);
	module->db = db;
	g_set_object (&module->ontologies, ontologies);
	sqlite3_create_module_v2 (db, "tracker_union", &union_module,
	                          module, tracker_union_module_free);
}
