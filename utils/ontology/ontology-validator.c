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

#include "config.h"

#include <string.h>

#include <glib.h>
#include <glib/gstdio.h>
#include <gio/gio.h>

#include <libtracker-data/tracker-data.h>
#include <libtracker-data/tracker-turtle-reader.h>

static gchar         *ontology_dir = NULL;

static GOptionEntry   entries[] = {
	{ "ontology-dir", 'o', 0, G_OPTION_ARG_FILENAME, &ontology_dir,
	  "Directory containing the ontology description files (TTL FORMAT)",
	  NULL
	},
	{ NULL }
};

#define RDFS_CLASS "http://www.w3.org/2000/01/rdf-schema#Class"
#define RDF_PROPERTY "http://www.w3.org/1999/02/22-rdf-syntax-ns#Property"
#define RDFS_SUBCLASSOF  "http://www.w3.org/2000/01/rdf-schema#subClassOf"
#define RDFS_SUBPROPERTYOF  "http://www.w3.org/2000/01/rdf-schema#subPropertyOf"
#define RDFS_TYPE "http://www.w3.org/1999/02/22-rdf-syntax-ns#type"
#define TRACKER_NS "http://www.tracker-project.org/ontologies/tracker#Namespace"
#define TRACKER_ONTO "http://www.tracker-project.org/ontologies/tracker#Ontology"
#define TRACKER_PREFIX "http://www.tracker-project.org/ontologies/tracker#prefix"
#define RDFS_RANGE "http://www.w3.org/2000/01/rdf-schema#range"
#define RDFS_DOMAIN "http://www.w3.org/2000/01/rdf-schema#domain"
#define NRL_IFP "http://www.semanticdesktop.org/ontologies/2007/08/15/nrl#InverseFunctionalProperty"

static GList *unknown_items = NULL;
static GList *known_items = NULL;
static GList *unknown_predicates = NULL;
static GList *namespaces = NULL;
static GList *ontologies = NULL;

static gboolean
exists_or_already_reported (const gchar *item)
{
	if (!g_list_find_custom (known_items,
	                         item,
	                         (GCompareFunc) g_strcmp0)){
		if (!g_list_find_custom (unknown_items,
		                         item,
		                         (GCompareFunc) g_strcmp0)) {
			return FALSE;
		}
	}
	return TRUE;
}

static void
turtle_load_ontology (const gchar *turtle_subject,
                      const gchar *turtle_predicate,
                      const gchar *turtle_object)
{

	/* nmo:Email a rdfs:Class
	 *  If rdfs:Class exists, add nmo:Email to the known_items
	 **/
	if (!g_strcmp0 (turtle_predicate, RDFS_TYPE)) {

		if (!g_strcmp0 (turtle_object, TRACKER_NS)) {
			/* Check there is no Namespace redefinition */
                        if (g_list_find_custom (namespaces, 
                                                turtle_subject, 
                                                (GCompareFunc) g_strcmp0)) {
                                g_error ("Redefining Tracker:Namespace '%s'",
                                         turtle_subject);
                        } else {
                                namespaces = g_list_prepend (namespaces, g_strdup (turtle_subject));
                        }
			return;
		}

		if (!g_strcmp0 (turtle_object, TRACKER_ONTO)) {
			/* Check there is no Ontology redefinition */
                        if (g_list_find_custom (ontologies, 
                                                turtle_subject, 
                                                (GCompareFunc) g_strcmp0)) {
                                g_error ("Redefining Tracker:Ontology '%s'",
                                         turtle_subject);
                        } else {
                                ontologies = g_list_prepend (ontologies, g_strdup (turtle_subject));
                        }
			return;
		}

		if (!g_strcmp0 (turtle_object, NRL_IFP)) {
			/* Ignore the InverseFunctionalProperty subclassing */
			return;
		}

		/* Check the nmo:Email hasn't already be defined
		 *  (ignoring rdfs:Class, rdf:Property and tracker:Ontology for bootstraping reasons)
		 */
		if (exists_or_already_reported (turtle_subject)
		    && g_strcmp0 (turtle_subject, RDFS_CLASS)
		    && g_strcmp0 (turtle_subject, RDF_PROPERTY)
                    && g_strcmp0 (turtle_subject, TRACKER_ONTO)) {
			g_error ("%s is already defined", turtle_subject);
			return;
		}

		/* Check the object class is already defined */
		if (!exists_or_already_reported (turtle_object)) {
			g_error ("%s is a %s but %s is not defined",
			         turtle_subject, turtle_object, turtle_object);
		} else {
			/* A new type defined... if it is a predicate,
			 *  remove it from the maybe list!
			 */
			if (!g_strcmp0 (turtle_object, RDF_PROPERTY)) {
				GList *link;
				link = g_list_find_custom (unknown_predicates,
				                           turtle_subject,
				                           (GCompareFunc)g_strcmp0);
				if (link) {
					unknown_predicates = g_list_remove_link (unknown_predicates,
					                                         link);
				}
			}
			known_items = g_list_prepend (known_items, g_strdup (turtle_subject));
		}
		return;
	}

	/*
	 * nmo:Message rdfs:subClassOf nie:InformationElement
	 *  Check nie:InformationElement is defined
	 */
	if (!g_strcmp0 (turtle_predicate, RDFS_SUBCLASSOF)
	    || !g_strcmp0 (turtle_predicate, RDFS_SUBPROPERTYOF)
	    || !g_strcmp0 (turtle_predicate, RDFS_RANGE)
	    || !g_strcmp0 (turtle_predicate, RDFS_DOMAIN)) {
		/* Check the class is already defined */
		if (!exists_or_already_reported (turtle_object)) {
			g_error ("Class %s refers to %s but it is not defined",
			         turtle_subject, turtle_object);
		}
		return;
	}

	/*
	 * The predicate is not type, subclass, range, domain...
	 *   Put it in maybe
	 */
	if (!exists_or_already_reported (turtle_predicate)
	    && !g_list_find_custom (unknown_predicates,
	                            turtle_predicate,
	                            (GCompareFunc) g_strcmp0)) {
		unknown_predicates = g_list_prepend (unknown_predicates,
		                                     g_strdup (turtle_predicate));
	}
}

static void
process_file (const gchar *ttl_path)
{
	const gchar *subject, *predicate, *object;
	TrackerTurtleReader *reader;
	GError *error = NULL;
	GFile *ttl_file = g_file_new_for_path (ttl_path);

	g_print ("Processing %s\n", ttl_path);

	reader = tracker_turtle_reader_new_for_file (ttl_file, NULL);

	while (tracker_turtle_reader_next (reader,
	                                   &subject, &predicate, &object,
	                                   NULL, &error)) {
		turtle_load_ontology (subject, predicate, object);
	}

	g_object_unref (reader);
	g_object_unref (ttl_file);

	if (error) {
		g_message ("Turtle parse error: %s", error->message);
		g_error_free (error);
	}
}

static void
load_ontology_files (const gchar *services_dir)
{
	GList       *files = NULL;
	GDir        *services;
	const gchar *conf_file;
	GFile       *f;
	gchar       *dir_uri, *fullpath;

	f = g_file_new_for_path (services_dir);
	dir_uri = g_file_get_path (f);

	g_print ("dir_uri %s\n", dir_uri);
	services = g_dir_open (dir_uri, 0, NULL);

	conf_file = g_dir_read_name (services);

	while (conf_file) {

		if (!g_str_has_suffix (conf_file, "ontology")) {
			conf_file = g_dir_read_name (services);
			continue;
		}

		fullpath = g_build_filename (dir_uri, conf_file, NULL);
		files = g_list_insert_sorted (files, fullpath, (GCompareFunc) g_strcmp0);
		conf_file = g_dir_read_name (services);
	}

	g_dir_close (services);

	//process_file (fullpath, turtle_load_ontology, NULL);
	g_list_foreach (files, (GFunc) process_file, turtle_load_ontology);

	g_list_foreach (files, (GFunc) g_free, NULL);
	g_object_unref (f);
	g_free (dir_uri);
}

static void
load_basic_classes ()
{
	known_items = g_list_prepend (known_items, g_strdup (RDFS_CLASS));
	known_items = g_list_prepend (known_items, g_strdup (RDF_PROPERTY));
        known_items = g_list_prepend (known_items, g_strdup (TRACKER_ONTO));
}

static void
clean_lists ()
{
        if (unknown_items) {
                g_list_foreach (unknown_items, (GFunc)g_free, NULL);
                unknown_items = NULL;
        }

        if (known_items) {
                g_list_foreach (known_items, (GFunc)g_free, NULL);
                known_items = NULL;
        }

        if (unknown_predicates) {
                g_list_foreach (unknown_predicates, (GFunc)g_free, NULL);
                unknown_predicates = NULL;
        }

        if (ontologies) {
                g_list_foreach (ontologies, (GFunc)g_free, NULL);
                ontologies = NULL;
        }
}

gint
main (gint argc, gchar **argv)
{
	GOptionContext *context;
	GList *it;

	/* Translators: this messagge will apper immediately after the  */
	/* usage string - Usage: COMMAND [OPTION]... <THIS_MESSAGE>     */
	context = g_option_context_new ("- Validate the ontology consistency");

	/* Translators: this message will appear after the usage string */
	/* and before the list of options.                              */
	g_option_context_add_main_entries (context, entries, NULL);
	g_option_context_parse (context, &argc, &argv, NULL);

	if (!ontology_dir) {
		gchar *help;

		g_printerr ("%s\n\n",
		            "Ontology directory is mandatory");

		help = g_option_context_get_help (context, TRUE, NULL);
		g_option_context_free (context);
		g_printerr ("%s", help);
		g_free (help);

		return -1;
	}

	load_basic_classes ();
	//"/home/ivan/devel/codethink/tracker-ssh/data/services"
	load_ontology_files (ontology_dir);


	for (it = unknown_predicates; it != NULL; it = it->next) {
		g_error ("Predicate '%s' is used in the ontology, but it is not defined\n",
		         (gchar *)it->data);
	}

        clean_lists ();

	return 0;
}
