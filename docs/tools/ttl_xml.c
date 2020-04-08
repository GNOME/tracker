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

#include <glib/gprintf.h>

#include "ttl_xml.h"
#include "ttlresource2xml.h"

typedef struct {
	Ontology *ontology;
	OntologyDescription *description;
	FILE *output;
} CallbackInfo;


static void
print_itemized_list (FILE *f, GList *list)
{
	GList *it;

	g_fprintf (f, "<itemizedlist>\n");
	for (it = list; it != NULL; it = it->next) {
		g_fprintf (f, "<listitem>%s</listitem>\n", (gchar *)it->data);
	}
	g_fprintf (f, "</itemizedlist>\n");
}

static void
print_people_list (FILE *f,
                   const gchar *role,
                   GList *list) 
{
        if (!list) {
                return;
        }

        g_fprintf (f, "<varlistentry>\n");
        g_fprintf (f, "  <term>%s</term>\n", role);
        g_fprintf (f, "  <listitem>\n");
        print_itemized_list (f, list);
        g_fprintf (f, "  </listitem>\n");
        g_fprintf (f, "</varlistentry>\n");
}

static void
print_link_as_varlistentry (FILE *f,
                            const gchar *term,
                            const gchar *link_text,
                            const gchar *link)
{
        g_fprintf (f, "  <varlistentry>\n");
        g_fprintf (f,"    <term>%s</term>\n", term);
	if (link) {
                g_fprintf (f, 
                           " <listitem><para><ulink url=\"%s\">%s</ulink></para></listitem>\n",
		           link, link_text);
	} else {
		g_fprintf (f, " <listitem><para>Not available</para></listitem>\n");
	}
        g_fprintf (f, "  </varlistentry>\n");
}

#if 0
static void
print_deprecated_message (FILE *f)
{
        g_fprintf (f, "<note>\n");
        g_fprintf (f, "<title>Note:</title>\n");
        g_fprintf (f, "<para>This item is deprecated</para>\n");
        g_fprintf (f, "</note>\n");
}
#endif

static void
print_xml_header (FILE *f, OntologyDescription *desc)
{
	g_fprintf (f, "<?xml version='1.0' encoding='UTF-8'?>\n");
	g_fprintf (f, "<!DOCTYPE book PUBLIC \"-//OASIS//DTD DocBook XML V4.5//EN\"\n"
	           "        \"http://www.oasis-open.org/docbook/xml/4.5/docbookx.dtd\" [\n");
	g_fprintf (f, "<!ENTITY %% local.common.attrib \"xmlns:xi  CDATA  #FIXED 'http://www.w3.org/2003/XInclude'\">\n");
	g_fprintf (f, "]>");

	g_fprintf (f, "<refentry id='%s' xmlns:xi=\"http://www.w3.org/2003/XInclude\">\n", desc->localPrefix);
	g_fprintf (f, "<refmeta>\n");
	g_fprintf (f, "  <refentrytitle>%s</refentrytitle>\n", desc->title);
	g_fprintf (f, "</refmeta>\n");
	g_fprintf (f, "<refnamediv>\n");
	g_fprintf (f, "<refname>%s</refname>", desc->title);
	g_fprintf (f, "<refpurpose>%s</refpurpose>", desc->description);
	g_fprintf (f, "</refnamediv>\n");
}

static void
print_xml_footer (FILE *f, OntologyDescription *desc)
{
	g_fprintf (f, "<refsect1>\n");
	g_fprintf (f, "<title>Credits and Copyright</title>\n");
	print_people_list (f, "Authors:", desc->authors);
	print_people_list (f, "Editors:", desc->editors);
	print_people_list (f, "Contributors:", desc->contributors);

	print_link_as_varlistentry (f, "Upstream:", "Upstream version", desc->upstream);
	print_link_as_varlistentry (f, "ChangeLog:", "Tracker changes", desc->gitlog);

	if (desc->copyright) {
		g_fprintf (f, "<varlistentry>\n");
		g_fprintf (f, "  <term>Copyright:</term>\n");
		g_fprintf (f, "  <listitem>\n");
		g_fprintf (f, "<para>%s</para>\n", desc->copyright);
		g_fprintf (f, "  </listitem>\n");
		g_fprintf (f, "</varlistentry>\n");
    }

	g_fprintf (f, "</refsect1>\n");
	g_fprintf (f, "</refentry>\n");
}

static void
print_class_list (FILE       *f,
                  Ontology   *ontology,
                  const char *id,
                  GList      *classes)
{
	GList *l;

	g_fprintf (f, "<refsect1 id=\"%s.classes\">", id);
	g_fprintf (f, "<title>Classes</title>");

	g_fprintf (f, "<simplelist>\n");
	for (l = classes; l; l = l->next) {
		OntologyClass *klass;
		g_autofree char *basename = NULL, *id = NULL;

		klass = l->data;
		basename = ttl_model_name_to_basename (ontology, klass->classname);
		id = ttl_model_name_to_shortname (ontology, klass->classname, "-");
		g_fprintf (f, "<member>");
		g_fprintf (f, "<link linkend=\"%s\">%s</link>", id, basename);
		if (klass->description) {
			g_fprintf (f, ": %s", klass->description);
		}
		g_fprintf (f, "</member>\n");
	}
	g_fprintf (f, "</simplelist>\n");

	g_fprintf (f, "</refsect1>");
}

void
ttl_xml_print (OntologyDescription *description,
                Ontology            *ontology,
                GFile               *file,
                const gchar         *description_dir)
{
	GHashTableIter iter;
	gchar *upper_name, *path, *introduction, *basename;
	GList *classes, *l;
	FILE *f;

	path = g_file_get_path (file);
	f = fopen (path, "w");
	g_assert (f != NULL);
	g_free (path);

	upper_name = g_ascii_strup (description->localPrefix, -1);
	classes = g_hash_table_get_values (ontology->classes);

	print_xml_header (f, description);

	print_class_list (f, ontology, description->localPrefix, classes);

	basename = g_strdup_printf ("%s-introduction.xml", description->localPrefix);
	introduction = g_build_filename (description_dir, basename, NULL);
	g_free (basename);

	if (g_file_test (introduction, G_FILE_TEST_EXISTS)) {
		g_fprintf (f, "<xi:include href='%s'><xi:fallback/></xi:include>",
		           introduction);
	}

    g_fprintf (f, "<refsect1 id='%s-classes'>\n", description->localPrefix);
	g_fprintf (f, "<title>%s Ontology Classes</title>\n", upper_name);
	g_hash_table_iter_init (&iter, ontology->classes);

	for (l = classes; l; l = l->next) {
		print_ontology_class (ontology, l->data, f);
	}

    g_fprintf (f, "</refsect1>\n");
	print_xml_footer (f, description);

	g_list_free (classes);

	g_free (upper_name);
	g_free (introduction);
	fclose (f);
}
