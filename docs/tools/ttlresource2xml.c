/*
 * Copyright (C) 2015, Carlos Garnacho
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
 *
 * Authors: Carlos Garnacho <carlosg@gnome.org>
 */

#include <glib-object.h>
#include <glib/gprintf.h>
#include <gio/gio.h>
#include "ttl_loader.h"
#include "ttl_model.h"
#include "ttl_xml.h"
#include "ttlresource2xml.h"

#define TRACKER_ONTOLOGY_CLASS "http://www.tracker-project.org/ontologies/tracker#Ontology"

static void
class_get_parent_hierarchy (Ontology       *ontology,
                            const gchar    *class_name,
                            GList         **list)
{
	OntologyClass *klass;
	GList *l;

	/* Ensure we only got the same class there once */
	*list = g_list_remove (*list, (gpointer) class_name);
	*list = g_list_prepend (*list, (gpointer) class_name);

	klass = g_hash_table_lookup (ontology->classes, class_name);

	if (!klass) {
		klass = ttl_model_class_new (class_name);
		g_hash_table_insert (ontology->classes, g_strdup (klass->classname), klass);
		return;
	}

	for (l = klass->superclasses; l; l = l->next) {
		class_get_parent_hierarchy (ontology, l->data, list);
	}
}

static GList *
class_get_hierarchy (Ontology      *ontology,
                     OntologyClass *klass)
{
	GList *hierarchy = NULL, *l;

	/* Order is: parents, this class, and children. Guaranteed
	 * to be in an order where all parents are before a child.
	 * We first prepend all children, and then find out the
	 * upwards hierarchy recursively.
	 */
	for (l = klass->subclasses; l; l = l->next) {
		hierarchy = g_list_prepend (hierarchy, l->data);
	}

	class_get_parent_hierarchy (ontology, klass->classname, &hierarchy);

	return hierarchy;
}

static void
print_xml_header (FILE          *f,
                   OntologyClass *klass,
                   Ontology      *ontology)
{
	gchar *id, *shortname;

	id = ttl_model_name_to_shortname (ontology, klass->classname, "-");
	shortname = ttl_model_name_to_shortname (ontology, klass->classname, NULL);

        g_fprintf (f, "<?xml version='1.0' encoding='UTF-8'?>\n");
        g_fprintf (f, "<refentry id='%s'>\n", id);

        g_fprintf (f, "<refmeta><refentrytitle>%s</refentrytitle></refmeta>", shortname);
        g_fprintf (f, "<refnamediv><refname>%s</refname></refnamediv>", shortname);

	g_fprintf (f, "<refsect1 id='%s.description'>", id);

	if (klass->description) {
		g_fprintf (f, "<title>Description</title>\n");
		g_fprintf (f, "<para>%s</para>\n", klass->description);
	}

	if (klass->deprecated)
		g_fprintf (f, "<note><para>This class is deprecated.</para></note>\n");

	if (klass->notify)
		g_fprintf (f, "<note><para>This class emits notifications about changes, and can "
		           "be tracked through the <literal>GraphUpdated</literal> DBus signal.</para></note>\n");

	g_fprintf (f, "</refsect1>");

	g_free (shortname);
	g_free (id);
}

static void
print_xml_footer (FILE *f)
{
	g_fprintf (f, "</refentry>\n");
}

static void
print_predefined_instances (FILE          *f,
                            OntologyClass *klass,
                            Ontology      *ontology)
{
	gchar *shortname, *id;
	GList *l;

	if (!klass->instances)
		return;

	shortname = ttl_model_name_to_shortname (ontology, klass->classname, NULL);
	id = ttl_model_name_to_shortname (ontology, klass->classname, "-");

	g_fprintf (f, "<refsect3 id='%s.predefined-instances'>", id);
	g_fprintf (f, "<title>Predefined instances</title><para>");
	g_fprintf (f, "%s has the following predefined instances: ", shortname);
        g_fprintf (f, "<itemizedlist>\n");

	g_free (shortname);
	g_free (id);

	for (l = klass->instances; l; l = l->next) {
		shortname = ttl_model_name_to_shortname (ontology, l->data, NULL);

		g_fprintf (f, "<listitem><para>");
		g_fprintf (f, "<literal>%s</literal>", shortname);
		g_fprintf (f, "</para></listitem>\n");
		g_free (shortname);
	}

	g_fprintf (f, "</itemizedlist></para></refsect3>\n");
}

static void
print_fts_properties (FILE          *f,
                      OntologyClass *klass,
                      Ontology      *ontology)
{
	gchar *shortname, *id;
	GList *l, *fts_props = NULL;

	for (l = klass->in_domain_of; l; l = l->next) {
		OntologyProperty *prop;

		prop = g_hash_table_lookup (ontology->properties, l->data);

		if (prop->fulltextIndexed)
			fts_props = g_list_prepend (fts_props, prop);
	}

	if (!fts_props)
		return;

	shortname = ttl_model_name_to_shortname (ontology, klass->classname, NULL);
	id = ttl_model_name_to_shortname (ontology, klass->classname, "-");

	g_fprintf (f, "<refsect3 id='%s.fts-properties'>", id);
	g_fprintf (f, "<title>Full-text-indexed properties</title><para>");
	g_fprintf (f, "%s has the following full-text-indexed properties: ", shortname);
        g_fprintf (f, "<itemizedlist>\n");

	for (l = fts_props; l; l = l->next) {
		gchar *prop_shortname, *prop_id;
		OntologyProperty *prop = l->data;

		prop_shortname = ttl_model_name_to_shortname (ontology, prop->propertyname, NULL);
		prop_id = ttl_model_name_to_shortname (ontology, prop->propertyname, "-");

		g_fprintf (f, "<listitem><para>");
		g_fprintf (f, "<link linkend=\"%s.%s\">%s</link>", id, prop_id, prop_shortname);
		g_fprintf (f, "</para></listitem>\n");
		g_free (prop_shortname);
		g_free (prop_id);
	}

	g_fprintf (f, "</itemizedlist></para></refsect3>\n");
	g_free (shortname);
	g_free (id);
	g_list_free (fts_props);
}

typedef struct {
	GString *str;
	gint visible_len;
} HierarchyString;

typedef struct {
	OntologyClass *class;
	GList *hierarchy;
	GHashTable *resolved_children;
	GHashTable *resolved_parents;
	GHashTable *placed;
	GPtrArray *strings;
} HierarchyContext;

static HierarchyString *
hierarchy_string_new (void)
{
	HierarchyString *str;

	str = g_new0 (HierarchyString, 1);
	str->str = g_string_new ("");

	return str;
}

static void
hierarchy_string_free (HierarchyString *str)
{
	g_string_free (str->str, TRUE);
	g_free (str);
}

static void
hierarchy_string_append (HierarchyString *str,
                         const gchar     *substr)
{
	g_string_append (str->str, substr);
	str->visible_len += g_utf8_strlen (substr, -1);
}

static void
hierarchy_string_append_link (HierarchyString *str,
                              const gchar     *substr,
                              const gchar     *link,
                              gboolean         bold)
{
	if (bold)
		g_string_append_printf (str->str, "<emphasis><link linkend=\"%s\">%s</link></emphasis>",
					link, substr);
	else
		g_string_append_printf (str->str, "<link linkend=\"%s\">%s</link>", link, substr);

	str->visible_len += g_utf8_strlen (substr, -1);
}

static GList *
list_filter (GList *original,
             GList *valid)
{
	GList *l, *filtered = NULL;

	for (l = original; l; l = l->next) {
		if (!g_list_find_custom (valid, l->data,
		                         (GCompareFunc) g_strcmp0))
			continue;

		filtered = g_list_prepend (filtered, l->data);
	}

	return filtered;
}

static HierarchyContext *
hierarchy_context_new (OntologyClass *klass,
                       Ontology      *ontology)
{
	HierarchyContext *context;
	GList *l;

	context = g_new0 (HierarchyContext, 1);
	context->class = klass;
	context->hierarchy = class_get_hierarchy (ontology, klass);
	context->placed = g_hash_table_new (g_str_hash, g_str_equal);
	context->resolved_parents = g_hash_table_new_full (g_str_hash,
	                                                   g_str_equal,
	                                                   NULL,
	                                                   (GDestroyNotify) g_list_free);
	context->resolved_children = g_hash_table_new_full (g_str_hash,
	                                                    g_str_equal,
	                                                    NULL,
	                                                    (GDestroyNotify) g_list_free);
	context->strings = g_ptr_array_new_with_free_func ((GDestroyNotify) hierarchy_string_free);

	for (l = context->hierarchy; l; l = l->next) {
		OntologyClass *cl = g_hash_table_lookup (ontology->classes, l->data);

		g_hash_table_insert (context->resolved_parents,
		                     cl->classname,
		                     list_filter (cl->superclasses,
		                                  context->hierarchy));
		g_hash_table_insert (context->resolved_children,
		                     cl->classname,
		                     list_filter (cl->subclasses,
		                                  context->hierarchy));

		g_ptr_array_add (context->strings, hierarchy_string_new ());
	}

	return context;
}

static void
hierarchy_context_free (HierarchyContext *context)
{
	g_list_free (context->hierarchy);
	g_ptr_array_unref (context->strings);
	g_hash_table_unref (context->placed);
	g_hash_table_unref (context->resolved_parents);
	g_hash_table_unref (context->resolved_children);
	g_free (context);
}

static GList *
hierarchy_context_get_single_parented_children (HierarchyContext *context,
                                                OntologyClass    *klass,
                                                Ontology         *ontology)
{
	GList *filtered = NULL, *children, *l;

	children = g_hash_table_lookup (context->resolved_children,
	                                klass->classname);

	for (l = children; l; l = l->next) {
		GList *parents;

		parents = g_hash_table_lookup (context->resolved_parents, l->data);

		if (g_list_length (parents) == 1)
			filtered = g_list_prepend (filtered, l->data);
	}

	return filtered;
}

static void
fill_padding (HierarchyString *str,
              gint             max_len,
              gchar           *substr)
{
	gint padding = max_len - str->visible_len;

	if (padding <= 0)
		return;

	while (padding > 0) {
		hierarchy_string_append (str, substr);
		padding--;
	}
}

static gboolean
check_parents_placed (GList      *parents,
                      GHashTable *ht)
{
	GList *l;

	for (l = parents; l; l = l->next) {
		if (!g_hash_table_lookup (ht, l->data))
			return FALSE;
	}

	return TRUE;
}

static void
hierarchy_context_resolve_class (HierarchyContext *context,
                                 OntologyClass    *klass,
                                 Ontology         *ontology)
{
	GList *l = g_list_find_custom (context->hierarchy, klass->classname,
				       (GCompareFunc) g_strcmp0);
	gint pos = g_list_position (context->hierarchy, l);
	GList *children, *parents;
	gchar *shortname, *link;
	HierarchyString *str;
	gboolean is_child;

	if (pos < 0)
		return;

	shortname = ttl_model_name_to_shortname (ontology, klass->classname, NULL);
	link = ttl_model_name_to_shortname (ontology, klass->classname, "-");
	parents = g_hash_table_lookup (context->resolved_parents,
	                               klass->classname);

	if (g_list_length (parents) > 1) {
		gboolean first_pending = TRUE;
		gint i, max_len = 0;

		/* This class has more than one parent in the current graph,
		 * those paint the lines on the right side.
		 */

		/* Step 1: Find the longest string */
		first_pending = TRUE;
		for (i = 0, l = context->hierarchy; i < pos && l; i++, l = l->next) {
			is_child = g_list_find_custom (klass->superclasses, l->data,
			                               (GCompareFunc) g_strcmp0) != NULL;
			if (!is_child && first_pending)
				continue;

			str = g_ptr_array_index (context->strings, i);
			max_len = MAX (max_len, str->visible_len);
		}

		/* Step 2: append the line art, we must fill in some padding if
		 * necessary.
		 */
		first_pending = TRUE;
		for (i = 0, l = context->hierarchy; i < pos && l; i++, l = l->next) {
			is_child = g_list_find_custom (klass->superclasses, l->data,
			                               (GCompareFunc) g_strcmp0) != NULL;

			if (!is_child && first_pending)
				continue;

			str = g_ptr_array_index (context->strings, i);

			fill_padding (str, max_len, is_child ? "─" : " ");

			if (first_pending) {
				hierarchy_string_append (str, "──┐");
				first_pending = FALSE;
			} else if (is_child) {
				hierarchy_string_append (str, "──┤");
			} else {
				hierarchy_string_append (str, "  │");
			}
		}

		/* Step 3: Finally, print the current class */
		str = g_ptr_array_index (context->strings, pos);
		fill_padding (str, max_len - 1, " ");

		hierarchy_string_append (str, "   └── ");
		hierarchy_string_append_link (str, shortname, link, klass == context->class);
		hierarchy_string_append (str, " ");
	} else {
		/* The current class has only 1 parent, lineart for those is
		 * displayed on the left side.
		 */

		/* Step 1: Print the current class */
		str = g_ptr_array_index (context->strings, pos);
		hierarchy_string_append_link (str, shortname, link, klass == context->class);
		hierarchy_string_append (str, " ");

		/* Step 2: Modify all strings downwards, adding the lineart
		 * necessary for all children of this class.
		 */
		children = hierarchy_context_get_single_parented_children (context, klass, ontology);
		l = l->next;
		pos++;

		for (; l; l = l->next, pos++) {
			guint len = g_list_length (children);
			GList *cur;

			str = g_ptr_array_index (context->strings, pos);
			cur = g_list_find_custom (children, l->data,
			                          (GCompareFunc) g_strcmp0);
			is_child = (cur != NULL);

			if (is_child) {
				if (len > 1)
					hierarchy_string_append (str, "├── ");
				else if (len == 1)
					hierarchy_string_append (str, "╰── ");

				children = g_list_delete_link (children, cur);
				g_hash_table_insert (context->placed,
				                     l->data, GINT_TO_POINTER (TRUE));
			} else {
				if (len > 0)
					hierarchy_string_append (str, "│   ");
				else if (len == 0 &&
					 !g_hash_table_lookup (context->placed, l->data)) {
					GList *cl_parents;

					cl_parents = g_hash_table_lookup (context->resolved_parents, l->data);

					if (g_list_length (cl_parents) == 1 ||
					    !check_parents_placed (cl_parents, context->placed)) {
						hierarchy_string_append (str, "    ");
					}
				}
			}
		}
	}

	g_free (shortname);
	g_free (link);
}

static GPtrArray *
class_get_parent_hierarchy_strings (OntologyClass *klass,
                                    Ontology      *ontology)
{
	HierarchyContext *context;
	GList *c;
	GPtrArray *strings = NULL;

	context = hierarchy_context_new (klass, ontology);

	/* Proceed from parent to child classes, populating the 
	 * context->strings array.
	 */
	for (c = context->hierarchy; c; c = c->next) {
		OntologyClass *cl = g_hash_table_lookup (ontology->classes, c->data);
		hierarchy_context_resolve_class (context, cl, ontology);
	}

	strings = g_ptr_array_ref (context->strings);
	hierarchy_context_free (context);

	return strings;
}

static void
print_class_hierarchy (FILE          *f,
                       OntologyClass *klass,
                       Ontology      *ontology)
{
	GPtrArray *strings;
	gchar *id;
	gint i;

	strings = class_get_parent_hierarchy_strings (klass, ontology);

	if (!strings)
		return;

	id = ttl_model_name_to_shortname (ontology, klass->classname, "-");

	g_fprintf (f, "<refsect3 id='%s.hierarchy'>", id);
	g_fprintf (f, "<title>Class hierarchy</title>");
	g_fprintf (f, "<screen>");

	g_free (id);

	for (i = 0; i < strings->len; i++) {
		HierarchyString *str = g_ptr_array_index (strings, i);
		g_fprintf (f, "    %s\n", str->str->str);
	}

	g_fprintf (f, "</screen></refsect3>\n");
	g_ptr_array_unref (strings);
}

static void
print_properties (FILE          *f,
                  OntologyClass *klass,
                  Ontology      *ontology)
{
	g_autofree gchar *id = NULL;
	GList *l;

	if (!klass->in_domain_of)
		return;

	id = ttl_model_name_to_shortname (ontology, klass->classname, "-");
	g_fprintf (f, "<refsect3 id='%s.properties'>", id);
	g_fprintf (f, "<title>Properties</title>");

	g_fprintf (f, "<table>");
	g_fprintf (f, "<thead><tr><td>Name</td><td>Type</td><td>Description</td></tr></thead>");

	g_fprintf (f, "<tbody>");
	for (l = klass->in_domain_of; l; l = l->next) {
		OntologyProperty *prop;
		g_autofree gchar *shortname = NULL, *basename = NULL, *type_name = NULL, *type_class_id = NULL, *prop_id = NULL;

		prop = g_hash_table_lookup (ontology->properties, l->data);

		prop_id = ttl_model_name_to_shortname (ontology, prop->propertyname, "-");
		shortname = ttl_model_name_to_shortname (ontology, prop->propertyname, NULL);
		basename = ttl_model_name_to_basename (ontology, prop->propertyname);
		type_name = ttl_model_name_to_shortname (ontology, prop->range->data, NULL);
		type_class_id = ttl_model_name_to_shortname (ontology, prop->range->data, "-");

		g_fprintf (f, "<tr>");

		g_fprintf (f, "<td>");
		g_fprintf (f, "<indexterm zone='%s.%s'><primary sortas='%s'>%s</primary></indexterm>",
		           id, prop_id, shortname, shortname);
		g_fprintf (f, "%s", basename);
		g_fprintf (f, "</td>");

		g_fprintf (f, "<td>");
		g_fprintf (f, "<link linkend=\"%s\">%s</link>", type_class_id, type_name);
		g_fprintf (f, "</td>");

		g_fprintf (f, "<td>");
		if (prop->description) {
			g_fprintf (f, "<para>%s</para>", prop->description);
		}
		g_fprintf (f, "</td>");
		g_fprintf (f, "</tr>");

		/* FIXME: where to put this info? */
		/*
		if (prop->max_cardinality) {
			g_fprintf (f, "<para>Number of possible elements per resource (Cardinality): %s</para>",
				   prop->max_cardinality);
		} else {
			g_fprintf (f, "<para>Number of possible elements per resource (Cardinality): Unlimited</para>");
		}

		if (prop->fulltextIndexed) {
			g_fprintf (f, "<note><para>This property is full-text-indexed, and can be looked up through <literal>fts:match</literal>.</para></note>\n");
		}

		if (prop->deprecated) {
			g_fprintf (f, "<note><para>This property is deprecated.</para></note>\n");
		}

		if (prop->superproperties) {
			GList *l;

			g_fprintf (f, "<note><para>This property supersedes the following properties from this or parent classes:</para>\n");
			g_fprintf (f, "<itemizedlist>\n");

			for (l = prop->superproperties; l; l = l->next) {
				gchar *class_shortname, *shortname, *class_id, *superprop_id;
				OntologyProperty *superprop;
				OntologyClass *cl;

				superprop = g_hash_table_lookup (ontology->properties, l->data);

				if (!superprop) {
					superprop = ttl_model_property_new (l->data);
					g_hash_table_insert (ontology->properties, g_strdup (superprop->propertyname), superprop);
				}

				if (!superprop->domain)
					continue;

				cl = g_hash_table_lookup (ontology->classes, superprop->domain->data);

				shortname = ttl_model_name_to_shortname (ontology, superprop->propertyname, NULL);
				class_shortname = ttl_model_name_to_shortname (ontology, cl->classname, NULL);
				superprop_id = ttl_model_name_to_shortname (ontology, superprop->propertyname, "-");
				class_id = ttl_model_name_to_shortname (ontology, cl->classname, "-");

				g_fprintf (f, "<listitem><para>");
				g_fprintf (f, "<link linkend=\"%s.%s\"><literal>“%s”</literal> from the <literal>%s</literal> class</link>",
				           class_id, superprop_id,
				           shortname, class_shortname);
				g_fprintf (f, "</para></listitem>\n");

				g_free (class_id);
				g_free (superprop_id);
				g_free (class_shortname);
				g_free (shortname);
			}

			g_fprintf (f, "</itemizedlist></note>\n");
		}*/
	}
	g_fprintf (f, "</tbody>");
	g_fprintf (f, "</table>");

	g_fprintf (f, "</refsect3>");
}

void
print_ontology_class (Ontology      *ontology,
                      OntologyClass *klass,
                      FILE          *f)
{
	g_autofree gchar *name, *id;

	g_return_if_fail (f != NULL);

	name = ttl_model_name_to_basename (ontology, klass->classname);
	id = ttl_model_name_to_shortname (ontology, klass->classname, "-");

	g_fprintf (f, "<refsect2 id='%s'>\n", id);
	g_fprintf (f, "<title>%s</title>\n", name);

	g_fprintf (f, "<refsect3 id='%s.description'>\n", id);
	g_fprintf (f, "  <title>Description</title>\n");
	if (klass->description) {
		g_fprintf (f, "  <para>%s</para>", klass->description);
	}
	g_fprintf (f, "</refsect3>\n");

	print_class_hierarchy (f, klass, ontology);
	print_predefined_instances (f, klass, ontology);
	print_fts_properties (f, klass, ontology);
	print_properties (f, klass, ontology);
	g_fprintf (f, "</refsect2>\n");
}
