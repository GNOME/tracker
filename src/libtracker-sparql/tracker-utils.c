/*
 * Copyright (C) 2010, Nokia <ivan.frade@nokia.com>
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

#include "tracker-utils.h"

#include "libtracker-data/tracker-uuid.h"

gchar *
tracker_sparql_escape_string (const gchar* literal)
{
	GString *str;
	const gchar *p;

	str = g_string_new (NULL);
	p = literal;

	while (*p != '\0') {
		size_t len;

		len = strcspn (p, "\t\n\r\b\f\'\"\\");
		g_string_append_len (str, p, len);
		p += len;

		switch (*p) {
		case '\t':
			g_string_append (str, "\\t");
			break;
		case '\n':
			g_string_append (str, "\\n");
			break;
		case '\r':
			g_string_append (str, "\\r");
			break;
		case '\b':
			g_string_append (str, "\\b");
			break;
		case '\f':
			g_string_append (str, "\\f");
			break;
		case '"':
			g_string_append (str, "\\\"");
			break;
		case '\'':
			g_string_append (str, "\\'");
			break;
		case '\\':
			g_string_append (str, "\\\\");
			break;
		default:
			continue;
		}

		p++;
	}

	return g_string_free (str, FALSE);
}

gchar *
tracker_sparql_get_uuid_urn (void)
{
	return tracker_generate_uuid ("urn:uuid");
}

/**
 * tracker_sparql_get_ontology_nepomuk:
 *
 * Returns a path to the built-in Nepomuk ontologies.
 *
 * Returns: (transfer full): a #GFile instance.
 */
GFile *
tracker_sparql_get_ontology_nepomuk (void)
{
	return g_file_new_for_path (SHAREDIR "/tracker3/ontologies/nepomuk");
}
