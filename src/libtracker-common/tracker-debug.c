/*
 * Copyright (C) 2020, Sam Thursfield <sam@afuera.me.uk>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.          See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 */

#include "config.h"

#include "tracker-debug.h"

#ifdef G_ENABLE_DEBUG
static const GDebugKey tracker_debug_keys[] = {
  { "collation", TRACKER_DEBUG_COLLATION },
  { "ontology-changes", TRACKER_DEBUG_ONTOLOGY_CHANGES },
  { "sqlite", TRACKER_DEBUG_SQLITE },
  { "sparql", TRACKER_DEBUG_SPARQL },
  { "sql-statements", TRACKER_DEBUG_SQL_STATEMENTS },
};
#endif /* G_ENABLE_DEBUG */

static gpointer
parse_debug_flags ()
{
	const gchar *env_string;
	guint flags = 0;

	env_string = g_getenv ("TRACKER_DEBUG");
	if (env_string != NULL) {
#ifdef G_ENABLE_DEBUG
		flags = g_parse_debug_string (env_string, tracker_debug_keys, G_N_ELEMENTS (tracker_debug_keys));
#else
		g_warning ("TRACKER_DEBUG set but ignored because tracker isn't built with G_ENABLE_DEBUG");
#endif  /* G_ENABLE_DEBUG */
		env_string = NULL;
	}

	return GINT_TO_POINTER (flags);
}

guint
tracker_get_debug_flags (void)
{
	static GOnce once = G_ONCE_INIT;

	g_once (&once, parse_debug_flags, NULL);

	return GPOINTER_TO_INT (once.retval);
}
