/*
 * Copyright (C) 2009, Nokia <ivan.frade@nokia.com>
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

#include <glib.h>
#include <gio/gio.h>
#include <string.h>
#include <stdio.h>
#include "ttl_loader.h"
#include "ttl_model.h"
#include "ttl_sgml.h"
#include "ttlresource2sgml.h"

static gchar *ontology_dir = NULL;
static gchar *output_dir = NULL;

static GOptionEntry   entries[] = {
	{ "ontology-dir", 'd', 0, G_OPTION_ARG_FILENAME, &ontology_dir,
	  "Ontology directory",
	  NULL
	},
	{ "output-dir", 'o', 0, G_OPTION_ARG_FILENAME, &output_dir,
	  "File to write the output (default stdout)",
	  NULL
	},
	{ NULL }
};

static gint
compare_files (gconstpointer a,
	       gconstpointer b)
{
	const GFile *file_a = a, *file_b = b;
	gchar *basename_a, *basename_b;
	gint res;

	basename_a = g_file_get_basename ((GFile*) file_a);
	basename_b = g_file_get_basename ((GFile*) file_b);
	res = strcmp (basename_a, basename_b);

	g_free (basename_a);
	g_free (basename_b);

	return res;
}

static GList *
get_description_files (GFile *dir)
{
	GFileEnumerator *enumerator;
	GFileInfo *info;
	GFile *desc_file;
	GList *files;
	const gchar *name;

	enumerator = g_file_enumerate_children (dir,
	                                        G_FILE_ATTRIBUTE_STANDARD_NAME,
	                                        G_FILE_QUERY_INFO_NONE,
	                                        NULL, NULL);

	if (!enumerator) {
		return NULL;
	}

	files = NULL;

	while ((info = g_file_enumerator_next_file (enumerator, NULL, NULL)) != NULL) {
		name = g_file_info_get_name (info);

		if (g_str_has_suffix (name, ".description")) {
			desc_file = g_file_enumerator_get_child (enumerator, info);
			files = g_list_insert_sorted (files, desc_file, compare_files);
		}

		g_object_unref (info);
	}

	g_object_unref (enumerator);

	return files;
}

gint
main (gint argc, gchar **argv)
{
	GOptionContext *context;
	Ontology *ontology = NULL;
	OntologyDescription *description = NULL;
	GList *description_files, *l;
	GFile *ontology_file, *output_file;
	gchar *path;

	/* Translators: this messagge will apper immediately after the  */
	/* usage string - Usage: COMMAND [OPTION]... <THIS_MESSAGE>     */
	context = g_option_context_new ("- Generates HTML doc for a TTL file");

	/* Translators: this message will appear after the usage string */
	/* and before the list of options.                              */
	g_option_context_add_main_entries (context, entries, NULL);
	g_option_context_parse (context, &argc, &argv, NULL);

	if (!ontology_dir || !output_dir) {
		gchar *help;

		g_printerr ("%s\n\n",
		            "Ontology and output dirs are mandatory");

		help = g_option_context_get_help (context, TRUE, NULL);
		g_option_context_free (context);
		g_printerr ("%s", help);
		g_free (help);

		return -1;
	}

	ontology_file = g_file_new_for_commandline_arg (ontology_dir);
	output_file = g_file_new_for_commandline_arg (output_dir);
	description_files = get_description_files (ontology_file);

	if (!description_files) {
		g_printerr ("Ontology description files not found in dir\n");
		return -1;
	}

	path = g_file_get_path (output_file);
	g_mkdir_with_parents (path, 0755);
	g_free (path);

	ontology = ttl_loader_new_ontology ();

	for (l = description_files; l; l = l->next) {
		Ontology *file_ontology = NULL;
		GFile *ttl_file, *ttl_output_file;
		gchar *filename;

		description = ttl_loader_load_description (l->data);
		ttl_file = g_file_get_child (ontology_file, description->relativePath);

		filename = g_strdup_printf ("%s-ontology.xml", description->localPrefix);
		ttl_output_file = g_file_get_child (output_file, filename);
		g_free (filename);

		file_ontology = ttl_loader_new_ontology ();

		ttl_loader_load_ontology (ontology, ttl_file);
		ttl_loader_load_ontology (file_ontology, ttl_file);
		ttl_loader_load_prefix_from_description (ontology, description);

		ttl_sgml_print (description, file_ontology, ttl_output_file);

		ttl_loader_free_ontology (file_ontology);
		ttl_loader_free_description (description);
	}

	generate_ontology_class_docs (ontology, output_file);

	ttl_loader_free_ontology();
	g_option_context_free (context);

	return 0;
}
