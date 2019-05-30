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

#include "tracker-vtab-service.h"
#include <libtracker-sparql/tracker-sparql.h>

#define N_VIRTUAL_COLUMNS 100
#define COL_SERVICE 101
#define COL_QUERY 102
#define COL_SILENT 103

typedef struct {
	sqlite3 *db;
} TrackerServiceModule;

typedef struct {
	struct sqlite3_vtab parent;
	TrackerServiceModule *module;
	GList *cursors;
} TrackerServiceVTab;

typedef struct {
	struct sqlite3_vtab_cursor parent;
	TrackerServiceVTab *vtab;
	TrackerSparqlConnection *conn;
	TrackerSparqlCursor *sparql_cursor;
	gchar *service;
	gchar *query;
	guint64 rowid;
	guint silent   : 1;
	guint finished : 1;
} TrackerServiceCursor;

typedef struct {
	int column;
	int op;
} ConstraintData;

static void
tracker_service_module_free (gpointer data)
{
	TrackerServiceModule *module = data;

	g_free (module);
}

static void
tracker_service_vtab_free (gpointer data)
{
	TrackerServiceVTab *vtab = data;

	g_list_free (vtab->cursors);
	g_free (vtab);
}

static void
tracker_service_cursor_free (gpointer data)
{
	TrackerServiceCursor *cursor = data;

	g_free (cursor->service);
	g_free (cursor->query);
	g_clear_object (&cursor->conn);
	g_clear_object (&cursor->sparql_cursor);

	g_free (cursor);
}

static int
service_create (sqlite3            *db,
		gpointer            data,
		int                 argc,
		const char *const  *argv,
		sqlite3_vtab      **vtab_out,
		char              **err_out)
{
	TrackerServiceModule *module = data;
	TrackerServiceVTab *vtab;
	GString *str;
	gint i, rc;

	vtab = g_new0 (TrackerServiceVTab, 1);
	vtab->module = module;

	str = g_string_new ("CREATE TABLE x(\n");

	for (i = 0; i <= N_VIRTUAL_COLUMNS; i++)
		g_string_append_printf (str, "col%d TEXT, ", i);

	g_string_append (str,
			 "service TEXT HIDDEN, "
			 "query TEXT HIDDEN, "
			 "silent INTEGER HIDDEN)");
	rc = sqlite3_declare_vtab (module->db, str->str);
	g_string_free (str, TRUE);

	if (rc == SQLITE_OK)
		*vtab_out = &vtab->parent;
	else
		g_free (vtab);

	return rc;
}

static int
service_best_index (sqlite3_vtab       *vtab,
		    sqlite3_index_info *info)
{
	int i, argv_idx = 1;
	ConstraintData *data;

	data = sqlite3_malloc (sizeof (ConstraintData) * info->nConstraint);
	bzero (data, sizeof (ConstraintData) * info->nConstraint);

	for (i = 0; i < info->nConstraint; i++) {
		if (!info->aConstraint[i].usable)
			continue;

		if (info->aConstraint[i].iColumn != COL_SERVICE &&
		    info->aConstraint[i].iColumn != COL_QUERY &&
		    info->aConstraint[i].iColumn != COL_SILENT) {
			info->aConstraintUsage[i].argvIndex = -1;
			continue;
		}

		if (info->aConstraint[i].op != SQLITE_INDEX_CONSTRAINT_EQ)
			goto error;

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

error:
	sqlite3_free (data);
	return SQLITE_ERROR;
}

static int
service_destroy (sqlite3_vtab *vtab)
{
	tracker_service_vtab_free (vtab);
	return SQLITE_OK;
}

static int
service_open (sqlite3_vtab         *vtab_sqlite,
	      sqlite3_vtab_cursor **cursor_ret)
{
	TrackerServiceVTab *vtab = (TrackerServiceVTab *) vtab_sqlite;
	TrackerServiceCursor *cursor;

	cursor = g_new0 (TrackerServiceCursor, 1);
	cursor->vtab = vtab;

	vtab->cursors = g_list_prepend (vtab->cursors, cursor);
	*cursor_ret = (sqlite3_vtab_cursor *) cursor;

	return SQLITE_OK;
}

static int
service_close (sqlite3_vtab_cursor *vtab_cursor)
{
	TrackerServiceCursor *cursor = (TrackerServiceCursor *) vtab_cursor;
	TrackerServiceVTab *vtab = cursor->vtab;

	vtab->cursors = g_list_remove (vtab->cursors, cursor);
	tracker_service_cursor_free (cursor);
	return SQLITE_OK;
}

static int
service_filter (sqlite3_vtab_cursor  *vtab_cursor,
		int                   idx,
		const char           *idx_str,
		int                   argc,
		sqlite3_value       **argv)
{
	TrackerServiceCursor *cursor = (TrackerServiceCursor *) vtab_cursor;
	const ConstraintData *constraints = (const ConstraintData *) idx_str;
	GError *error = NULL;
	gint i;

	cursor->finished = FALSE;
	cursor->rowid = 0;

	for (i = 0; i < argc; i++) {
		if (constraints[i].column == COL_SERVICE)
			cursor->service = g_strdup (sqlite3_value_text (argv[i]));
		if (constraints[i].column == COL_QUERY)
			cursor->query = g_strdup (sqlite3_value_text (argv[i]));
		if (constraints[i].column == COL_SILENT)
			cursor->silent = !!sqlite3_value_int (argv[i]);
	}

	if (!cursor->service || !cursor->query)
		return SQLITE_ERROR;

	cursor->conn = tracker_sparql_connection_remote_new (cursor->service);
	cursor->sparql_cursor = tracker_sparql_connection_query (cursor->conn,
								 cursor->query,
								 NULL, &error);
	if (error)
		goto fail;

	cursor->finished =
		!tracker_sparql_cursor_next (cursor->sparql_cursor, NULL, &error);

	if (error)
		goto fail;

	return SQLITE_OK;

fail:
	if (cursor->silent) {
		cursor->finished = TRUE;
		g_error_free (error);
		return SQLITE_OK;
	} else {
		g_warning ("Could not create remote cursor: %s\n", error->message);
		g_error_free (error);
		return SQLITE_ERROR;
	}
}

static int
service_next (sqlite3_vtab_cursor *vtab_cursor)
{
	TrackerServiceCursor *cursor = (TrackerServiceCursor *) vtab_cursor;

	if (!cursor->sparql_cursor)
		return SQLITE_ERROR;

	cursor->finished =
		!tracker_sparql_cursor_next (cursor->sparql_cursor, NULL, NULL);

	cursor->rowid++;
	return SQLITE_OK;
}

static int
service_eof (sqlite3_vtab_cursor *vtab_cursor)
{
	TrackerServiceCursor *cursor = (TrackerServiceCursor *) vtab_cursor;

	return cursor->finished;
}

static int
service_column (sqlite3_vtab_cursor *vtab_cursor,
		sqlite3_context     *context,
		int                  n_col)
{
	TrackerServiceCursor *cursor = (TrackerServiceCursor *) vtab_cursor;
	const gchar *str;

	if (n_col == COL_SERVICE) {
		sqlite3_result_text (context, cursor->service, -1, NULL);
	} else if (n_col == COL_QUERY) {
		sqlite3_result_text (context, cursor->query, -1, NULL);
	} else if (n_col == COL_SILENT) {
		sqlite3_result_int (context, cursor->silent);
	} else if (n_col < tracker_sparql_cursor_get_n_columns (cursor->sparql_cursor)) {
		/* FIXME: Handle other types better */
		str = tracker_sparql_cursor_get_string (cursor->sparql_cursor, n_col, NULL);
		sqlite3_result_text (context, g_strdup (str), -1, g_free);
	} else {
		sqlite3_result_null (context);
	}

	return SQLITE_OK;
}

static int
service_rowid (sqlite3_vtab_cursor *vtab_cursor,
	       sqlite_int64        *rowid_out)
{
	TrackerServiceCursor *cursor = (TrackerServiceCursor *) vtab_cursor;

	*rowid_out = cursor->rowid;
	return SQLITE_OK;
}

void
tracker_vtab_service_init (sqlite3           *db,
			   TrackerOntologies *ontologies)
{
	TrackerServiceModule *module;
	static const sqlite3_module service_module = {
		2, /* version */
		service_create,
		service_create,
		service_best_index,
		service_destroy,
		service_destroy,
		service_open,
		service_close,
		service_filter,
		service_next,
		service_eof,
		service_column,
		service_rowid,
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

	module = g_new0 (TrackerServiceModule, 1);
	module->db = db;
	sqlite3_create_module_v2 (db, "tracker_service", &service_module,
	                          module, tracker_service_module_free);
}
