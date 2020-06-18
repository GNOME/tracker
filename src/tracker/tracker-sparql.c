/*
 * Copyright (C) 2009, Nokia <ivan.frade@nokia.com>
 * Copyright (C) 2014, Softathome <philippe.judge@softathome.com>
 * Copyright (C) 2014, Lanedo <martyn@lanedo.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 */

#include "config.h"

#include <sys/param.h>
#include <stdlib.h>
#include <time.h>
#include <locale.h>

#include <glib.h>
#include <glib/gi18n.h>

#include <libtracker-sparql/tracker-sparql.h>

#include "tracker-sparql.h"
#include "tracker-color.h"

#define SPARQL_OPTIONS_ENABLED() \
	(list_classes || \
	 list_class_prefixes || \
	 list_properties || \
	 list_notifies || \
	 list_indexes || \
	 list_graphs || \
	 tree || \
	 search || \
	 get_shorthand || \
	 get_longhand || \
	 file || \
	 query)

typedef struct _NodeData NodeData;
typedef struct _NodeFindData NodeFindData;
typedef struct _NodePrintData NodePrintData;

struct _NodeData {
	gchar *class;
	GSList *properties;
	guint parent_known:1;
};

struct _NodeFindData {
	GEqualFunc func;
	GNode *node;
	const gchar *class;
};

struct _NodePrintData {
	GHashTable *prefixes;
	GHashTable  *filter_parents;
	const gchar *highlight_text;
};

static gboolean parse_list_properties (const gchar  *option_name,
                                       const gchar  *value,
                                       gpointer      data,
                                       GError      **error);
static gboolean parse_list_notifies   (const gchar  *option_name,
                                       const gchar  *value,
                                       gpointer      data,
                                       GError      **error);
static gboolean parse_list_indexes    (const gchar  *option_name,
                                       const gchar  *value,
                                       gpointer      data,
                                       GError      **error);
static gboolean parse_tree            (const gchar  *option_name,
                                       const gchar  *value,
                                       gpointer      data,
                                       GError      **error);

static gchar *file;
static gchar *query;
static gboolean update;
static gboolean list_classes;
static gboolean list_class_prefixes;
static gchar *list_properties;
static gchar *list_notifies;
static gchar *list_indexes;
static gchar *list_graphs;
static gchar *tree;
static gchar *get_shorthand;
static gchar *get_longhand;
static gchar *search;
static gchar *database_path;
static gchar *dbus_service;
static gchar *remote_service;

static GOptionEntry entries[] = {
	{ "database", 'd', 0, G_OPTION_ARG_FILENAME, &database_path,
	  N_("Location of the database"),
	  N_("FILE")
	},
	{ "dbus-service", 'b', 0, G_OPTION_ARG_STRING, &dbus_service,
	  N_("Connects to a DBus service"),
	  N_("DBus service name")
	},
	{ "remote-service", 'r', 0, G_OPTION_ARG_STRING, &remote_service,
	  N_("Connects to a remote service"),
	  N_("Remote service URI")
	},
	{ "file", 'f', 0, G_OPTION_ARG_FILENAME, &file,
	  N_("Path to use to run a query or update from file"),
	  N_("FILE"),
	},
	{ "query", 'q', 0, G_OPTION_ARG_STRING, &query,
	  N_("SPARQL query"),
	  N_("SPARQL"),
	},
	{ "update", 'u', 0, G_OPTION_ARG_NONE, &update,
	  N_("This is used with --query and for database updates only."),
	  NULL,
	},
	{ "list-classes", 'c', 0, G_OPTION_ARG_NONE, &list_classes,
	  N_("Retrieve classes"),
	  NULL,
	},
	{ "list-class-prefixes", 'x', 0, G_OPTION_ARG_NONE, &list_class_prefixes,
	  N_("Retrieve class prefixes"),
	  NULL,
	},
	{ "list-properties", 'p', G_OPTION_FLAG_OPTIONAL_ARG, G_OPTION_ARG_CALLBACK, parse_list_properties,
	  N_("Retrieve properties for a class, prefixes can be used too (e.g. rdfs:Resource)"),
	  N_("CLASS"),
	},
	{ "list-notifies", 'n', G_OPTION_FLAG_OPTIONAL_ARG, G_OPTION_ARG_CALLBACK, parse_list_notifies,
	  N_("Retrieve classes which notify changes in the database (CLASS is optional)"),
	  N_("CLASS"),
	},
	{ "list-indexes", 'i', G_OPTION_FLAG_OPTIONAL_ARG, G_OPTION_ARG_CALLBACK, parse_list_indexes,
	  N_("Retrieve indexes used in database to improve performance (PROPERTY is optional)"),
	  N_("PROPERTY"),
	},
	{ "list-graphs", 'g', 0, G_OPTION_ARG_NONE, &list_graphs,
	  N_("Retrieve all named graphs"),
	  NULL,
	},
	{ "tree", 't', G_OPTION_FLAG_OPTIONAL_ARG, G_OPTION_ARG_CALLBACK, parse_tree,
	  N_("Describe subclasses, superclasses (can be used with -s to highlight parts of the tree and -p to show properties)"),
	  N_("CLASS"),
	},
	{ "search", 's', 0, G_OPTION_ARG_STRING, &search,
	  N_("Search for a class or property and display more information (e.g. Document)"),
	  N_("CLASS/PROPERTY"),
	},
	{ "get-shorthand", 0, 0, G_OPTION_ARG_STRING, &get_shorthand,
	  N_("Returns the shorthand for a class (e.g. nfo:FileDataObject)."),
	  N_("CLASS"),
	},
	{ "get-longhand", 0, 0, G_OPTION_ARG_STRING, &get_longhand,
	  N_("Returns the full namespace for a class."),
	  N_("CLASS"),
	},
	{ NULL }
};

static TrackerSparqlConnection *
create_connection (GError **error)
{
	if (database_path && !dbus_service && !remote_service) {
		GFile *file;

		file = g_file_new_for_commandline_arg (database_path);
		return tracker_sparql_connection_new (TRACKER_SPARQL_CONNECTION_FLAGS_NONE,
		                                      file, NULL, NULL, error);
	} else if (dbus_service && !database_path && !remote_service) {
		GDBusConnection *dbus_conn;

		dbus_conn = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, error);
		if (!dbus_conn)
			return NULL;

		return tracker_sparql_connection_bus_new (dbus_service, NULL, dbus_conn, error);
	} else if (remote_service && !database_path && !dbus_service) {
		return tracker_sparql_connection_remote_new (remote_service);
	} else {
		/* TRANSLATORS: Those are commandline arguments */
		g_printerr (_("Specify one --database, --dbus-service or --remote-service option"));
		exit (EXIT_FAILURE);
	}
}

GHashTable *
tracker_sparql_get_prefixes (TrackerSparqlConnection *connection)
{
	TrackerSparqlCursor *cursor;
	GError *error = NULL;
	GHashTable *retval;
	const gchar *query;

	retval = g_hash_table_new_full (g_str_hash,
	                                g_str_equal,
	                                g_free,
	                                g_free);

	/* FIXME: Would like to get this in the same SPARQL that we
	 * use to get the info, but doesn't seem possible at the
	 * moment with the limited string manipulation features we
	 * support in SPARQL.
	 */
	query = "SELECT ?ns ?prefix "
	        "WHERE {"
	        "  ?ns a nrl:Namespace ;"
	        "  nrl:prefix ?prefix "
	        "}";

	cursor = tracker_sparql_connection_query (connection, query, NULL, &error);

	if (error) {
		g_printerr ("%s, %s\n",
			    _("Unable to retrieve namespace prefixes"),
			    error->message);

		g_error_free (error);
		return retval;
	}

	if (!cursor) {
		g_printerr ("%s\n", _("No namespace prefixes were returned"));
		return retval;
	}

	while (tracker_sparql_cursor_next (cursor, NULL, NULL)) {
		const gchar *key, *value;

		key = tracker_sparql_cursor_get_string (cursor, 0, NULL);
		value = tracker_sparql_cursor_get_string (cursor, 1, NULL);

		if (!key || !value) {
			continue;
		}

		g_hash_table_insert (retval,
		                     g_strndup (key, strlen (key) - 1),
		                     g_strdup (value));
	}

	if (cursor) {
		g_object_unref (cursor);
	}

	return retval;
}

static gchar *
get_class_from_prefix (TrackerSparqlConnection *connection,
                       const gchar             *prefix)
{
	GError *error = NULL;
	TrackerSparqlCursor *cursor;
	const gchar *query;
	gchar *found = NULL;

	query = "SELECT ?prefix ?ns "
		"WHERE {"
		"  ?ns a nrl:Namespace ;"
		"  nrl:prefix ?prefix "
		"}";

	/* We have namespace prefix, get full name */
	cursor = tracker_sparql_connection_query (connection, query, NULL, &error);

	if (error) {
		g_printerr ("%s, %s\n",
		            _("Could not get namespace prefixes"),
		            error->message);
		g_error_free (error);

		return NULL;
	}

	if (!cursor) {
		g_printerr ("%s\n",
		            _("No namespace prefixes were found"));

		return NULL;
	}

	while (tracker_sparql_cursor_next (cursor, NULL, NULL) && !found) {
		const gchar *class_prefix, *class_name;

		class_prefix = tracker_sparql_cursor_get_string (cursor, 0, NULL);
		class_name = tracker_sparql_cursor_get_string (cursor, 1, NULL);

		if (strcmp (class_prefix, prefix) == 0) {
			found = g_strdup (class_name);
		}
	}

	g_object_unref (cursor);

	return found;
}

static gboolean
parse_list_properties (const gchar  *option_name,
                       const gchar  *value,
                       gpointer      data,
                       GError      **error)
{
	if (!value) {
		list_properties = g_strdup ("");
	} else {
		list_properties = g_strdup (value);
	}

	return TRUE;
}

static gboolean
parse_list_notifies (const gchar  *option_name,
                     const gchar  *value,
                     gpointer      data,
                     GError      **error)
{
	if (!value) {
		list_notifies = g_strdup ("");
	} else {
		list_notifies = g_strdup (value);
	}

	return TRUE;
}

static gboolean
parse_list_indexes (const gchar  *option_name,
                    const gchar  *value,
                    gpointer      data,
                    GError      **error)
{
	if (!value) {
		list_indexes = g_strdup ("");
	} else {
		list_indexes = g_strdup (value);
	}

	return TRUE;
}

static gboolean
parse_tree (const gchar  *option_name,
            const gchar  *value,
            gpointer      data,
            GError      **error)
{
	if (!value) {
		tree = g_strdup ("");
	} else {
		tree = g_strdup (value);
	}

	return TRUE;
}

gchar *
tracker_sparql_get_longhand (GHashTable  *prefixes,
                             const gchar *shorthand)
{
	gchar *colon, *namespace;

	namespace = g_strdup (shorthand);
	colon = strrchr (namespace, ':');

	if (colon) {
		GHashTableIter iter;
		gpointer key, value;
		gchar *property;
		const gchar *prefix = NULL;

		property = colon + 1;
		*colon = '\0';

		g_hash_table_iter_init (&iter, prefixes);
		while (g_hash_table_iter_next (&iter, &key, &value)) {
			if (strcmp (namespace, value) == 0) {
				prefix = key;
				break;
			}
		}

		if (prefix) {
			gchar *retval;

			retval = g_strdup_printf ("%s#%s", prefix, property);
			g_free (namespace);

			return retval;
		}
	}

	return namespace;
}

gchar *
tracker_sparql_get_shorthand (GHashTable  *prefixes,
                              const gchar *longhand)
{
	gchar *hash;

	hash = strrchr (longhand, '#');

	if (hash) {
		gchar *property;
		const gchar *prefix;

		property = hash + 1;
		*hash = '\0';

		prefix = g_hash_table_lookup (prefixes, longhand);

		return g_strdup_printf ("%s:%s", prefix, property);
	}

	return g_strdup (longhand);
}

static gchar *
get_shorthand_str_for_offsets (GHashTable  *prefixes,
                               const gchar *str)
{
	GString *result = NULL;
	gchar **properties;
	gint i;

	if (!str) {
		return NULL;
	}

	properties = g_strsplit (str, ",", -1);
	if (!properties) {
		return NULL;
	}

	for (i = 0; properties[i] != NULL && properties[i + 1] != NULL; i += 2) {
		const gchar *property;
		const gchar *offset;
		gchar *shorthand;

		property = properties[i];
		offset = properties[i + 1];

		if (!property || !offset) {
			g_warning ("Expected property AND offset to be valid for fts:offset results");
			continue;
		}

		shorthand = tracker_sparql_get_shorthand (prefixes, property);

		if (!shorthand) {
			shorthand = g_strdup (property);
		}

		if (!result) {
			result = g_string_new ("");
		} else {
			result = g_string_append_c (result, ' ');
		}

		g_string_append_printf (result, "%s:%s", shorthand, offset);
		g_free (shorthand);
	}

	g_strfreev (properties);

	return result ? g_string_free (result, FALSE) : NULL;
}

static void
print_cursor_with_ftsoffsets (TrackerSparqlCursor *cursor,
                              GHashTable          *prefixes,
                              const gchar         *none_found,
                              const gchar         *heading,
                              gboolean             only_first_col)
{
	if (!cursor) {
		g_print ("%s\n", none_found);
	} else {
		gint count = 0;

		g_print ("%s:\n", heading);

		if (only_first_col) {
			while (tracker_sparql_cursor_next (cursor, NULL, NULL)) {
				const gchar *str;
				gchar *shorthand;

				str = tracker_sparql_cursor_get_string (cursor, 0, NULL);
				shorthand = get_shorthand_str_for_offsets (prefixes, str);
				g_print ("  %s\n", shorthand ? shorthand : str);
				g_free (shorthand);
				count++;
			}
		} else {
			while (tracker_sparql_cursor_next (cursor, NULL, NULL)) {
				gint col;

				for (col = 0; col < tracker_sparql_cursor_get_n_columns (cursor); col++) {
					const gchar *str;
					gchar *shorthand;

					str = tracker_sparql_cursor_get_string (cursor, col, NULL);
					shorthand = get_shorthand_str_for_offsets (prefixes, str);
					g_print ("%c %s",
					         col == 0 ? ' ' : ',',
					         shorthand ? shorthand : str);
					g_free (shorthand);
				}

				g_print ("\n");

				count++;
			}
		}

		if (count == 0) {
			/* To translators: This is to say there are no
			 * search results found. We use a "foo: None"
			 * with multiple print statements, where "foo"
			 * may be Music or Images, etc. */
			g_print ("  %s\n", _("None"));
		}

		g_print ("\n");

		g_object_unref (cursor);
	}
}

static void
print_cursor (TrackerSparqlCursor *cursor,
              const gchar         *none_found,
              const gchar         *heading,
              gboolean             only_first_col)
{
	if (!cursor) {
		g_print ("%s\n", none_found);
	} else {
		gint count = 0;

		g_print ("%s:\n", heading);

		if (only_first_col) {
			while (tracker_sparql_cursor_next (cursor, NULL, NULL)) {
				g_print ("  %s\n", tracker_sparql_cursor_get_string (cursor, 0, NULL));
				count++;
			}
		} else {
			while (tracker_sparql_cursor_next (cursor, NULL, NULL)) {
				gint col;

				for (col = 0; col < tracker_sparql_cursor_get_n_columns (cursor); col++) {
					g_print ("%c %s",
					         col == 0 ? ' ' : ',',
					         tracker_sparql_cursor_get_string (cursor, col, NULL));
				}

				g_print ("\n");

				count++;
			}
		}

		if (count == 0) {
			/* To translators: This is to say there are no
			 * search results found. We use a "foo: None"
			 * with multiple print statements, where "foo"
			 * may be Music or Images, etc. */
			g_print ("  %s\n", _("None"));
		}

		g_print ("\n");

		g_object_unref (cursor);
	}
}

static NodeData *
tree_node_data_new (const gchar *class,
                    gboolean     parent_known)
{
	NodeData *data;

	data = g_slice_new0 (NodeData);
	data->class = g_strdup (class);
	data->parent_known = parent_known;

	return data;
}

static void
tree_node_data_free (NodeData *data)
{
	if (data == NULL) {
		return;
	}

	if (data->properties) {
		g_slist_foreach (data->properties, (GFunc) g_free, NULL);
		g_slist_free (data->properties);
	}

	g_free (data->class);
	g_slice_free (NodeData, data);
}

static GNode *
tree_new (void)
{
	return g_node_new (NULL);
}

static gboolean
tree_free_foreach (GNode *node)
{
	tree_node_data_free (node->data);
	return FALSE;
}

static void
tree_free (GNode *node)
{
	g_node_traverse (node,
	                 G_POST_ORDER,
	                 G_TRAVERSE_ALL,
	                 -1,
	                 (GNodeTraverseFunc) tree_free_foreach,
	                 NULL);
	g_node_destroy (node);
}

static gboolean
tree_node_find_foreach (GNode    *node,
                        gpointer  user_data)
{
	NodeFindData *data;
	NodeData *node_data;

	if (!node) {
		return FALSE;
	}

	node_data = node->data;

	if (!node_data) {
		return FALSE;
	}

	data = user_data;

	if ((data->func) (data->class, node_data->class)) {
		data->node = node;
		return TRUE;
	}

	return FALSE;
}

static GNode *
tree_node_find (GNode       *node,
                const gchar *class,
                GEqualFunc   func)
{
	NodeFindData data;

	data.class = class;
	data.node = NULL;
	data.func = func;

	g_node_traverse (node,
	                 G_POST_ORDER,
	                 G_TRAVERSE_ALL,
	                 -1,
	                 tree_node_find_foreach,
	                 &data);

	return data.node;
}

static GNode *
tree_node_lookup (GNode        *tree,
                  const gchar  *class,
                  GNode       **parent_node)
{
	GNode *node;

	node = tree_node_find (tree, class, g_str_equal);

	if (parent_node) {
		if (node) {
			*parent_node = node->parent;
		} else {
			*parent_node = NULL;
		}
	}

	return node;
}

static GNode *
tree_add_class (GNode       *root,
                const gchar *class,
                const gchar *parent,
                gboolean     parent_known)
{
	GNode *node, *parent_node;

	/* Look up class node */
	node = parent_node = NULL;

	/* If there is no class, node is root */
	if (class) {
		node = tree_node_lookup (root, class, &parent_node);
	}

	if (!node) {
		/* Create node */
		NodeData *data;

		data = tree_node_data_new (class, parent_known);
		node = g_node_new (data);

		if (!parent_known || !parent) {
			/* Add to root node */
			g_node_append (root, node);

			/* If node is currently an orphan, add to root
			 * and we will reorder it when we know more...
			 */
		} else {
			/* Lookup parent node and add to that. */
			parent_node = tree_node_lookup (root, parent, NULL);

			if (!parent_node) {
				/* Create parent node. */
				parent_node = tree_add_class (root, parent, NULL, FALSE);
				g_assert (parent_node != NULL);
			}

			g_node_append (parent_node, node);
		}
	} else {
		/* Lookup parent node and add to that. */
		parent_node = tree_node_lookup (root, parent, NULL);

		/* Reparent found node, if we need to */
		if (parent_node) {
			NodeData *parent_data;

			parent_data = parent_node->data;

			if (!parent_data->parent_known) {
				/* Add to right parent. */
				g_node_append (parent_node, node);
				parent_data->parent_known = TRUE;
			} else {
				NodeData *data;

				/* Cater for multiple parents, create
				 * new node if parents differ */
				data = tree_node_data_new (class, TRUE);
				node = g_node_new (data);
				g_node_append (parent_node, node);
			}
		}
	}

	return node;
}

static gboolean
tree_add_property (GNode       *root,
                   const gchar *class,
                   const gchar *property)
{
	GNode *node;
	NodeData *data;

	if (!property) {
		return FALSE;
	}

	node = tree_node_lookup (root, class, NULL);
	if (!node) {
		return FALSE;
	}

	data = node->data;

	data->properties = g_slist_prepend (data->properties,
	                                    g_strdup (property));

	return TRUE;
}

static gchar *
highlight (const gchar *text,
           const gchar *highlight_text)
{
	GString *s;
	gchar *text_down, *highlight_text_down, *p;

	if (!text) {
		return NULL;
	}

	if (!highlight_text) {
		return g_strdup (text);
	}

	s = g_string_new (text);

	text_down = g_utf8_casefold (text, -1);
	highlight_text_down = g_utf8_casefold (highlight_text, -1);
	p = text_down;

	while ((p = strstr (p, highlight_text_down)) != NULL) {
		gint offset_begin, offset_end;

		offset_begin = p - text_down;
		g_string_insert_len (s, offset_begin, SNIPPET_BEGIN, -1);

		offset_end = (p - text_down) + strlen (highlight_text) + strlen (SNIPPET_BEGIN);
		g_string_insert_len (s, offset_end, SNIPPET_END, -1);

		p += offset_end + strlen (SNIPPET_END);
	}

	g_free (highlight_text_down);
	g_free (text_down);

	return g_string_free (s, FALSE);
}

static inline void
tree_print_properties (NodeData      *nd,
                       NodePrintData *pd,
                       gint           depth)
{
	GSList *l;

	/* Sort first */
	nd->properties = g_slist_sort (nd->properties, (GCompareFunc) g_strcmp0);

	/* Print properties */
	for (l = nd->properties; l; l = l->next) {
		gchar *highlighted;
		gint i;

		for (i = 1; i < depth; i++) {
			g_print ("  |");
		}

		highlighted = highlight (l->data, pd->highlight_text);
		g_print ("  --> %s (P)\n", (gchar*) highlighted);
		g_free (highlighted);
	}
}

static gboolean
tree_print_foreach (GNode    *node,
                    gpointer  user_data)
{
	NodeData *nd;
	NodePrintData *pd;
	gboolean print = TRUE;

	gchar *shorthand, *highlighted;
	const gchar *text;
	gint depth, i;

	nd = node->data;
	pd = user_data;

	if (!nd) {
		g_print ("ROOT\n");
		return FALSE;
	}

	/* Filter based on parent classes */
	if (pd->filter_parents) {
		print = g_hash_table_lookup (pd->filter_parents, nd->class) != NULL;
	}

	if (!print) {
		return FALSE;
	}

	shorthand = NULL;

	if (pd->prefixes) {
		shorthand = tracker_sparql_get_shorthand (pd->prefixes, nd->class);
	}

	depth = g_node_depth (node);

	for (i = 1; i < depth; i++) {
		if (i == depth - 1) {
			const gchar *branch = "+";

			if (!node->next) {
				branch = "`";
			} else if (G_NODE_IS_LEAF (node)) {
				branch = "|";
			}

			g_print ("  %s", branch);
		} else {
			g_print ("  |");
		}
	}

	text = shorthand ? shorthand : nd->class;
	highlighted = highlight (text, pd->highlight_text);
	g_print ("-- %s (C)\n", highlighted);
	g_free (highlighted);
	g_free (shorthand);

	tree_print_properties (nd, pd, depth);

	return FALSE;
}

static void
tree_print (GNode       *node,
            GHashTable  *prefixes,
            GHashTable  *filter_parents,
            const gchar *highlight_text)
{
	NodePrintData data;

	data.prefixes = prefixes;
	data.filter_parents = filter_parents;
	data.highlight_text = highlight_text;

	g_node_traverse (node,
	                 G_PRE_ORDER,
	                 G_TRAVERSE_ALL,
	                 -1,
	                 tree_print_foreach,
	                 &data);
}

static gint
tree_get (TrackerSparqlConnection *connection,
          const gchar             *class_lookup,
          const gchar             *highlight_text)
{
	TrackerSparqlCursor *cursor;
	GHashTable *prefixes;
	GHashTable *filter_parents;
	GError *error = NULL;
	gchar *query;
	gchar *class_lookup_longhand;
	GNode *root, *found_node, *node;

	root = tree_new ();

	/* Get shorthand prefixes for printing / filtering */
	prefixes = tracker_sparql_get_prefixes (connection);

	/* Is class_lookup a shothand string, e.g. nfo:FileDataObject? */
	if (class_lookup && *class_lookup && strchr (class_lookup, ':')) {
		class_lookup_longhand = tracker_sparql_get_longhand (prefixes, class_lookup);
	} else {
		class_lookup_longhand = g_strdup (class_lookup);
	}

	/* Get subclasses of classes, using longhand */
	query = "SELECT ?p ?c WHERE { ?c a rdfs:Class . OPTIONAL { ?c rdfs:subClassOf ?p } }";
	cursor = tracker_sparql_connection_query (connection, query, NULL, &error);

	if (error) {
		g_printerr ("%s, %s\n",
		            _("Could not create tree: subclass query failed"),
		            error->message);
		g_error_free (error);
		g_object_unref (connection);

		return EXIT_FAILURE;
	}

	while (tracker_sparql_cursor_next (cursor, NULL, NULL)) {
		const gchar *parent = tracker_sparql_cursor_get_string (cursor, 0, NULL);
		const gchar *class = tracker_sparql_cursor_get_string (cursor, 1, NULL);

		tree_add_class (root, class, parent, TRUE);
	}

	/* Create filter (so we only deal with properties for classes
	 * we care about) */
	if (class_lookup_longhand && *class_lookup_longhand) {
		found_node = tree_node_lookup (root, class_lookup_longhand, NULL);
		filter_parents = g_hash_table_new_full (g_str_hash,
		                                        g_str_equal,
		                                        NULL,
		                                        NULL);

		for (node = found_node; node; node = node->parent) {
			NodeData *data = node->data;

			if (!data || !data->class) {
				continue;
			}

			g_hash_table_insert (filter_parents,
			                     data->class,
			                     GINT_TO_POINTER(1));
		}
	} else {
		filter_parents = NULL;
	}

	/* Get properties of classes */
	if (list_properties) {
		TrackerSparqlCursor *properties;

		query = "SELECT ?c ?p WHERE { ?p rdf:type rdf:Property ; rdfs:domain ?c }";

		properties = tracker_sparql_connection_query (connection, query, NULL, &error);

		if (error) {
			g_printerr ("%s, %s\n",
			            _("Could not create tree: class properties query failed"),
			            error->message);
			g_error_free (error);
			g_object_unref (connection);

			g_free (class_lookup_longhand);

			if (filter_parents) {
				g_hash_table_unref (filter_parents);
			}

			if (prefixes) {
				g_hash_table_unref (prefixes);
			}

			if (cursor) {
				g_object_unref (cursor);
			}

			return EXIT_FAILURE;
		}

		while (tracker_sparql_cursor_next (properties, NULL, NULL)) {
			const gchar *class = tracker_sparql_cursor_get_string (properties, 0, NULL);
			const gchar *property = tracker_sparql_cursor_get_string (properties, 1, NULL);
			gchar *property_lookup_shorthand;

			property_lookup_shorthand = tracker_sparql_get_shorthand (prefixes, property);
			tree_add_property (root, class, property_lookup_shorthand);
			g_free (property_lookup_shorthand);
		}

		if (properties) {
			g_object_unref (properties);
		}
	}

	g_free (class_lookup_longhand);

	/* Print */
	tree_print (root, prefixes, filter_parents, highlight_text);

	if (filter_parents) {
		g_hash_table_unref (filter_parents);
	}

	if (prefixes) {
		g_hash_table_unref (prefixes);
	}

	if (cursor) {
		g_object_unref (cursor);
	}

	tree_free (root);

	return EXIT_SUCCESS;
}

static int
sparql_run (void)
{
	TrackerSparqlConnection *connection;
	TrackerSparqlCursor *cursor;
	GError *error = NULL;

	connection = create_connection (&error);

	if (!connection) {
		g_printerr ("%s: %s\n",
		            _("Could not establish a connection to Tracker"),
		            error ? error->message : _("No error given"));
		g_clear_error (&error);
		return EXIT_FAILURE;
	}

	if (list_classes) {
		const gchar *query;

		query = "SELECT ?c WHERE { ?c a rdfs:Class }";
		cursor = tracker_sparql_connection_query (connection, query, NULL, &error);

		if (error) {
			g_printerr ("%s, %s\n",
			            _("Could not list classes"),
			            error->message);
			g_error_free (error);
			g_object_unref (connection);

			return EXIT_FAILURE;
		}

		print_cursor (cursor, _("No classes were found"), _("Classes"), TRUE);
	}

	if (list_class_prefixes) {
		const gchar *query;

		query = "SELECT ?prefix ?ns "
		        "WHERE {"
		        "  ?ns a nrl:Namespace ;"
		        "  nrl:prefix ?prefix "
		        "}";

		cursor = tracker_sparql_connection_query (connection, query, NULL, &error);

		if (error) {
			g_printerr ("%s, %s\n",
			            _("Could not list class prefixes"),
			            error->message);
			g_error_free (error);
			g_object_unref (connection);

			return EXIT_FAILURE;
		}

		print_cursor (cursor, _("No class prefixes were found"), _("Prefixes"), FALSE);
	}

	if (list_properties && list_properties[0] != '\0') {
		gchar *query;
		gchar *class_name;

		if (g_str_has_prefix (list_properties, "http://")) {
			/* We have full class name */
			class_name = g_strdup (list_properties);
		} else {
			gchar *p;
			gchar *prefix, *property;
			gchar *class_name_no_property;

			prefix = g_strdup (list_properties);
			p = strchr (prefix, ':');

			if (!p) {
				g_printerr ("%s\n",
				            _("Could not find property for class prefix, "
				              "e.g. :Resource in “rdfs:Resource”"));
				g_free (prefix);
				g_object_unref (connection);

				return EXIT_FAILURE;
			}

			property = g_strdup (p + 1);
			*p = '\0';

			class_name_no_property = get_class_from_prefix (connection, prefix);
			g_free (prefix);

			if (!class_name_no_property) {
				g_free (property);
				g_object_unref (connection);

				return EXIT_FAILURE;
			}

			class_name = g_strconcat (class_name_no_property, property, NULL);
			g_free (class_name_no_property);
			g_free (property);
		}

		query = g_strdup_printf ("SELECT ?p "
		                         "WHERE {"
		                         "  ?p a rdf:Property ;"
		                         "  rdfs:domain <%s>"
		                         "}",
		                         class_name);

		cursor = tracker_sparql_connection_query (connection, query, NULL, &error);
		g_free (query);
		g_free (class_name);

		if (error) {
			g_printerr ("%s, %s\n",
			            _("Could not list properties"),
			            error->message);
			g_error_free (error);
			g_object_unref (connection);

			return EXIT_FAILURE;
		}

		print_cursor (cursor, _("No properties were found"), _("Properties"), TRUE);
	}

	if (list_notifies) {
		gchar *query;

		/* First list classes */
		if (*list_notifies == '\0') {
			query = g_strdup_printf ("SELECT ?c "
			                         "WHERE {"
			                         "  ?c a rdfs:Class ."
			                         "  ?c nrl:notify true ."
			                         "}");
		} else {
			query = g_strdup_printf ("SELECT ?c "
			                         "WHERE {"
			                         "  ?c a rdfs:Class ."
			                         "  ?c nrl:notify true "
			                         "  FILTER regex (?c, \"%s\", \"i\") "
			                         "}",
			                         list_notifies);
		}

		cursor = tracker_sparql_connection_query (connection, query, NULL, &error);
		g_free (query);

		if (error) {
			g_printerr ("%s, %s\n",
			            _("Could not find notify classes"),
			            error->message);
			g_error_free (error);
			g_object_unref (connection);

			return EXIT_FAILURE;
		}

		print_cursor (cursor, _("No notifies were found"), _("Notifies"), TRUE);
	}

	if (list_indexes) {
		gchar *query;

		/* First list classes */
		if (*list_indexes == '\0') {
			query = g_strdup_printf ("SELECT ?p "
			                         "WHERE {"
			                         "  ?p nrl:indexed true ."
			                         "}");
		} else {
			query = g_strdup_printf ("SELECT ?p "
			                         "WHERE {"
			                         "  ?p nrl:indexed true "
			                         "  FILTER regex (?p, \"%s\", \"i\") "
			                         "}",
			                         list_indexes);
		}

		cursor = tracker_sparql_connection_query (connection, query, NULL, &error);
		g_free (query);

		if (error) {
			g_printerr ("%s, %s\n",
			            _("Could not find indexed properties"),
			            error->message);
			g_error_free (error);
			g_object_unref (connection);

			return EXIT_FAILURE;
		}

		print_cursor (cursor, _("No indexes were found"), _("Indexes"), TRUE);
	}

	if (list_graphs) {
		const gchar *query;

		/* First list classes */
		query = g_strdup_printf ("SELECT DISTINCT ?g "
		                         "WHERE {"
		                         "  GRAPH ?g { ?s ?p ?o }"
		                         "}");

		cursor = tracker_sparql_connection_query (connection, query, NULL, &error);

		if (error) {
			g_printerr ("%s, %s\n",
			            _("Could not list named graphs"),
			            error->message);
			g_error_free (error);
			g_object_unref (connection);

			return EXIT_FAILURE;
		}

		print_cursor (cursor, _("No graphs were found"), _("Named graphs"), TRUE);
	}

	if (tree) {
		return tree_get (connection, tree, search);
	}

	if (search) {
		gchar *query;

		/* First list classes */
		query = g_strdup_printf ("SELECT ?c "
		                         "WHERE {"
		                         "  ?c a rdfs:Class"
		                         "  FILTER regex (?c, \"%s\", \"i\") "
		                         "}",
		                         search);
		cursor = tracker_sparql_connection_query (connection, query, NULL, &error);
		g_free (query);

		if (error) {
			g_printerr ("%s, %s\n",
			            _("Could not search classes"),
			            error->message);
			g_error_free (error);
			g_object_unref (connection);

			return EXIT_FAILURE;
		}

		print_cursor (cursor, _("No classes were found to match search term"), _("Classes"), TRUE);

		/* Second list properties */
		query = g_strdup_printf ("SELECT ?p "
		                         "WHERE {"
		                         "  ?p a rdf:Property"
		                         "  FILTER regex (?p, \"%s\", \"i\") "
		                         "}",
		                         search);

		cursor = tracker_sparql_connection_query (connection, query, NULL, &error);
		g_free (query);

		if (error) {
			g_printerr ("%s, %s\n",
			            _("Could not search properties"),
			            error->message);
			g_error_free (error);
			g_object_unref (connection);

			return EXIT_FAILURE;
		}

		print_cursor (cursor, _("No properties were found to match search term"), _("Properties"), TRUE);
	}

	if (get_shorthand) {
		GHashTable *prefixes = tracker_sparql_get_prefixes (connection);
		gchar *result;

		result = tracker_sparql_get_shorthand (prefixes, get_shorthand);
		g_print ("%s\n", result);
		g_free (result);

		if (prefixes) {
			g_hash_table_unref (prefixes);
		}
	}

	if (get_longhand) {
		GHashTable *prefixes = tracker_sparql_get_prefixes (connection);
		gchar *result;

		result = tracker_sparql_get_longhand (prefixes, get_longhand);
		g_print ("%s\n", result);
		g_free (result);

		if (prefixes) {
			g_hash_table_unref (prefixes);
		}
	}

	if (file) {
		gchar *path_in_utf8;
		gsize size;

		path_in_utf8 = g_filename_to_utf8 (file, -1, NULL, NULL, &error);
		if (error) {
			g_printerr ("%s:'%s', %s\n",
			            _("Could not get UTF-8 path from path"),
			            file,
			            error->message);
			g_error_free (error);
			g_object_unref (connection);

			return EXIT_FAILURE;
		}

		g_file_get_contents (path_in_utf8, &query, &size, &error);
		if (error) {
			g_printerr ("%s:'%s', %s\n",
			            _("Could not read file"),
			            path_in_utf8,
			            error->message);
			g_error_free (error);
			g_free (path_in_utf8);
			g_object_unref (connection);

			return EXIT_FAILURE;
		}

		g_free (path_in_utf8);
	}

	if (query) {
		if (G_UNLIKELY (update)) {
			tracker_sparql_connection_update (connection, query, NULL, &error);

			if (error) {
				g_printerr ("%s, %s\n",
				            _("Could not run update"),
				            error->message);
				g_error_free (error);

				return EXIT_FAILURE;
			}

			g_print ("%s\n", _("Done"));

#if 0
			if (results) {
				GPtrArray *insert;
				GHashTable *solution;
				GHashTableIter iter;
				gpointer key, value;
				gint i, s, n;

				for (i = 0; i < results->len; i++) {
					insert = results->pdata[i];

					for (s = 0; s < insert->len; s++) {
						solution = insert->pdata[s];

						g_hash_table_iter_init (&iter, solution);
						n = 0;
						while (g_hash_table_iter_next (&iter, &key, &value)) {
							g_print ("%s%s: %s",
							         n > 0 ? ", " : "",
							         (const gchar *) key,
							         (const gchar *) value);
							n++;
						}
						g_print ("\n");
					}
				}
			}
#endif
		} else {
			GHashTable *prefixes = NULL;

			if (strstr (query, "fts:offsets")) {
				prefixes = tracker_sparql_get_prefixes (connection);
			}

			cursor = tracker_sparql_connection_query (connection, query, NULL, &error);

			if (error) {
				g_printerr ("%s, %s\n",
				            _("Could not run query"),
				            error->message);
				g_error_free (error);

				if (prefixes) {
					g_hash_table_unref (prefixes);
				}

				return EXIT_FAILURE;
			}

			if (G_UNLIKELY (prefixes)) {
				print_cursor_with_ftsoffsets (cursor, prefixes, _("No results found matching your query"), _("Results"), FALSE);
				g_hash_table_unref (prefixes);
			} else {
				print_cursor (cursor, _("No results found matching your query"), _("Results"), FALSE);
			}
		}
	}

	g_object_unref (connection);

	return EXIT_SUCCESS;
}

static int
sparql_run_default (void)
{
	GOptionContext *context;
	gchar *help;

	context = g_option_context_new (NULL);
	g_option_context_add_main_entries (context, entries, NULL);
	help = g_option_context_get_help (context, FALSE, NULL);
	g_option_context_free (context);
	g_printerr ("%s\n", help);
	g_free (help);

	return EXIT_FAILURE;
}

static gboolean
sparql_options_enabled (void)
{
	return SPARQL_OPTIONS_ENABLED ();
}

int
tracker_sparql (int argc, const char **argv)
{
	GOptionContext *context;
	GError *error = NULL;
	const gchar *failed;

	context = g_option_context_new (NULL);
	g_option_context_add_main_entries (context, entries, NULL);

	argv[0] = "tracker sparql";

	if (!g_option_context_parse (context, &argc, (char***) &argv, &error)) {
		g_printerr ("%s, %s\n", _("Unrecognized options"), error->message);
		g_error_free (error);
		g_option_context_free (context);
		return EXIT_FAILURE;
	}

	g_option_context_free (context);

	if (file && query) {
		failed = _("File and query can not be used together");
	} else if (list_properties && list_properties[0] == '\0' && !tree) {
		failed = _("The --list-properties argument can only be empty when used with the --tree argument");
	} else {
		failed = NULL;
	}

	if (failed) {
		g_printerr ("%s\n\n", failed);
		return EXIT_FAILURE;
	}

	if (sparql_options_enabled ()) {
		return sparql_run ();
	}

	return sparql_run_default ();
}
