/*
 * Copyright (C) 2010, Nokia <ivan.frade@nokia.com>
 * Copyright (C) 2014, Lanedo <martyn@lanedo.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

#include "config.h"

#include <glib.h>
#include <glib/gi18n.h>

#include <libtracker-common/tracker-common.h>

#include "tracker-config.h"
#include "tracker-miner-manager.h"

GSList *
tracker_gsettings_get_all (gint *longest_name_length)
{
	typedef struct {
		const gchar *schema;
		const gchar *path;
	} SchemaWithPath;

	TrackerMinerManager *manager;
	GSettingsSchemaSource *source;
	GError *error = NULL;
	GSettings *settings;
	GSList *all = NULL;
	GSList *l;
	GSList *miners_available;
	GSList *valid_schemas = NULL;
	gchar **schemas;
	gint i, len = 0;
	SchemaWithPath components[] = {
		{ "Store", "store" },
		{ "Extract", "extract" },
		{ "Writeback", "writeback" },
		{ 0 }
	};
	SchemaWithPath *swp;

	/* Don't auto-start the miners here */
	manager = tracker_miner_manager_new_full (FALSE, &error);
	if (!manager) {
		g_printerr (_("Could not get GSettings for miners, manager could not be created, %s"),
		            error ? error->message : _("No error given"));
		g_printerr ("\n");
		g_clear_error (&error);
		return NULL;
	}

	miners_available = tracker_miner_manager_get_available (manager);

	source = g_settings_schema_source_get_default ();
	g_settings_schema_source_list_schemas (source, TRUE, &schemas, NULL);

	for (i = 0; schemas[i]; i++) {
		if (!g_str_has_prefix (schemas[i], "org.freedesktop.Tracker.")) {
			continue;
		}

		valid_schemas = g_slist_prepend (valid_schemas, g_strdup (schemas[i]));
	}

	/* Store / General */
	for (swp = components; swp && swp->schema; swp++) {
		GSettingsSchema *settings_schema;
		gchar *schema;
		gchar *path;

		schema = g_strdup_printf ("org.freedesktop.Tracker.%s", swp->schema);
		path = g_strdup_printf ("/org/freedesktop/tracker/%s/", swp->path);

		settings_schema = g_settings_schema_source_lookup (source, schema, FALSE);

		/* If miner doesn't have a schema, no point in getting config */
		if (!tracker_string_in_gslist (schema, valid_schemas)) {
			g_free (path);
			g_free (schema);
			continue;
		}

		len = MAX (len, strlen (swp->schema));

		settings = g_settings_new_with_path (schema, path);
		if (settings) {
			ComponentGSettings *c = g_slice_new (ComponentGSettings);

			c->name = g_strdup (swp->schema);
			c->settings = settings;
			c->schema = settings_schema;
			c->is_miner = FALSE;

			all = g_slist_prepend (all, c);
		}
	}

	/* Miners */
	for (l = miners_available; l; l = l->next) {
		const gchar *name;
		gchar *schema;
		gchar *name_lowercase;
		gchar *path;
		gchar *miner;

		miner = l->data;
		if (!miner) {
			continue;
		}

		name = g_utf8_strrchr (miner, -1, '.');
		if (!name) {
			continue;
		}

		name++;
		name_lowercase = g_utf8_strdown (name, -1);

		schema = g_strdup_printf ("org.freedesktop.Tracker.Miner.%s", name);
		path = g_strdup_printf ("/org/freedesktop/tracker/miner/%s/", name_lowercase);
		g_free (name_lowercase);

		/* If miner doesn't have a schema, no point in getting config */
		if (!tracker_string_in_gslist (schema, valid_schemas)) {
			g_free (path);
			g_free (schema);
			continue;
		}

		settings = g_settings_new_with_path (schema, path);
		g_free (path);
		g_free (schema);

		if (settings) {
			ComponentGSettings *c = g_slice_new (ComponentGSettings);

			c->name = g_strdup (name);
			c->settings = settings;
			c->is_miner = TRUE;

			all = g_slist_prepend (all, c);
			len = MAX (len, strlen (name));
		}
	}

	g_slist_foreach (valid_schemas, (GFunc) g_free, NULL);
	g_slist_free (valid_schemas);
	g_slist_foreach (miners_available, (GFunc) g_free, NULL);
	g_slist_free (miners_available);
	g_object_unref (manager);
	g_strfreev (schemas);

	if (longest_name_length) {
		*longest_name_length = len;
	}

	return g_slist_reverse (all);
}

gboolean
tracker_gsettings_set_all (GSList           *all,
                           TrackerVerbosity  verbosity)
{
	GSList *l;
	gboolean success = TRUE;

	for (l = all; l && success; l = l->next) {
		ComponentGSettings *c = l->data;

		if (!c) {
			continue;
		}

		success &= g_settings_set_enum (c->settings, "verbosity", verbosity);
		g_settings_apply (c->settings);
	}

	g_settings_sync ();

	return success;
}

void
tracker_gsettings_free (GSList *all)
{
	GSList *l;

	/* Clean up */
	for (l = all; l; l = l->next) {
		ComponentGSettings *c = l->data;

		g_free (c->name);
		g_object_unref (c->settings);
		g_clear_pointer (&c->schema, g_settings_schema_unref);
		g_slice_free (ComponentGSettings, c);
	}
}
