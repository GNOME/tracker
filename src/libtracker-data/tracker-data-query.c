/*
 * Copyright (C) 2006, Jamie McCracken <jamiemcc@gnome.org>
 * Copyright (C) 2007, Jason Kivlighn <jkivlighn@gmail.com>
 * Copyright (C) 2007, Creative Commons <http://creativecommons.org>
 * Copyright (C) 2008, Nokia <ivan.frade@nokia.com>
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

#include <string.h>
#include <stdlib.h>

#include "tracker-class.h"
#include "tracker-data-manager.h"
#include "tracker-data-query.h"
#include "tracker-db-interface-sqlite.h"
#include "tracker-db-manager.h"
#include "tracker-ontologies.h"
#include "tracker-sparql.h"

GPtrArray*
tracker_data_query_rdf_type (TrackerDataManager *manager,
                             gint                id)
{
	TrackerDBCursor *cursor = NULL;
	TrackerDBInterface *iface;
	TrackerDBStatement *stmt;
	GPtrArray *ret = NULL;
	GError *error = NULL;
	TrackerOntologies *ontologies;

	iface = tracker_data_manager_get_writable_db_interface (manager);
	ontologies = tracker_data_manager_get_ontologies (manager);

	stmt = tracker_db_interface_create_statement (iface, TRACKER_DB_STATEMENT_CACHE_TYPE_SELECT, &error,
	                                              "SELECT (SELECT Uri FROM Resource WHERE ID = \"rdf:type\") "
	                                              "FROM \"rdfs:Resource_rdf:type\" "
	                                              "WHERE ID = ?");

	if (stmt) {
		tracker_db_statement_bind_int (stmt, 0, id);
		cursor = tracker_db_statement_start_cursor (stmt, &error);
		g_object_unref (stmt);
	}

	if (cursor) {

		/* Query is usually a rather small result, but let's try to
		 * avoid reallocs in gptrarray.c as much as possible (this
		 * function is called fairly often) */

		ret = g_ptr_array_sized_new (20);
		while (tracker_db_cursor_iter_next (cursor, NULL, &error)) {
			const gchar *class_uri;
			TrackerClass *cl;

			class_uri = tracker_db_cursor_get_string (cursor, 0, NULL);
			cl = tracker_ontologies_get_class_by_uri (ontologies, class_uri);
			if (!cl) {
				g_critical ("Unknown class %s", class_uri);
				continue;
			}
			g_ptr_array_add (ret, cl);
		}
		g_object_unref (cursor);
	}

	if (G_UNLIKELY (error)) {
		g_critical ("Could not query RDF type: %s\n", error->message);
		g_error_free (error);

		if (ret) {
			g_ptr_array_free (ret, FALSE);
			ret = NULL;
		}
	}

	return ret;
}

gint
tracker_data_query_resource_id (TrackerDataManager *manager,
                                TrackerDBInterface *iface,
                                const gchar        *uri)
{
	TrackerDBCursor *cursor = NULL;
	TrackerDBStatement *stmt;
	GError *error = NULL;
	gint id = 0;

	g_return_val_if_fail (uri != NULL, 0);

	stmt = tracker_db_interface_create_statement (iface, TRACKER_DB_STATEMENT_CACHE_TYPE_SELECT, &error,
	                                              "SELECT ID FROM Resource WHERE Uri = ?");

	if (stmt) {
		tracker_db_statement_bind_text (stmt, 0, uri);
		cursor = tracker_db_statement_start_cursor (stmt, &error);
		g_object_unref (stmt);
	}

	if (cursor) {
		if (tracker_db_cursor_iter_next (cursor, NULL, &error)) {
			id = tracker_db_cursor_get_int (cursor, 0);
		}

		g_object_unref (cursor);
	}

	if (G_UNLIKELY (error)) {
		g_critical ("Could not query resource ID: %s\n", error->message);
		g_error_free (error);
	}

	return id;
}

gchar *
tracker_data_query_unused_uuid (TrackerDataManager *manager,
                                TrackerDBInterface *iface)
{
	TrackerDBCursor *cursor = NULL;
	TrackerDBStatement *stmt;
	GError *error = NULL;
	gchar *uuid = NULL;

	stmt = tracker_db_interface_create_statement (iface, TRACKER_DB_STATEMENT_CACHE_TYPE_SELECT, &error,
	                                              "SELECT SparqlUUID()");

	if (stmt) {
		cursor = tracker_db_statement_start_cursor (stmt, &error);
		g_object_unref (stmt);
	}

	if (cursor) {
		if (tracker_db_cursor_iter_next (cursor, NULL, &error)) {
			uuid = g_strdup (tracker_db_cursor_get_string (cursor, 0, NULL));
		}

		g_object_unref (cursor);
	}

	if (G_UNLIKELY (error)) {
		g_critical ("Could not query resource ID: %s\n", error->message);
		g_error_free (error);
	}

	return uuid;
}


TrackerDBCursor *
tracker_data_query_sparql_cursor (TrackerDataManager  *manager,
                                  const gchar         *query,
                                  GError             **error)
{
	TrackerSparql *sparql_query;
	TrackerSparqlCursor *cursor;

	g_return_val_if_fail (query != NULL, NULL);

	sparql_query = tracker_sparql_new (manager, query);

	cursor = tracker_sparql_execute_cursor (sparql_query, NULL, error);

	g_object_unref (sparql_query);

	return TRACKER_DB_CURSOR (cursor);
}

