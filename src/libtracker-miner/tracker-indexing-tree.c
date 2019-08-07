/*
 * Copyright (C) 2011, Nokia <ivan.frade@nokia.com>
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
 * Author: Carlos Garnacho  <carlos@lanedo.com>
 */

#include <libtracker-common/tracker-file-utils.h>
#include "tracker-indexing-tree.h"

/**
 * SECTION:tracker-indexing-tree
 * @short_description: Indexing tree handling
 *
 * #TrackerIndexingTree handles the tree of directories configured to be indexed
 * by the #TrackerMinerFS.
 **/

typedef struct _TrackerIndexingTreePrivate TrackerIndexingTreePrivate;
typedef struct _NodeData NodeData;
typedef struct _PatternData PatternData;
typedef struct _FindNodeData FindNodeData;

struct _NodeData
{
	GFile *file;
	guint flags;
	guint shallow : 1;
	guint removing : 1;
};

struct _PatternData
{
	GPatternSpec *pattern;
	TrackerFilterType type;
	GFile *file; /* Only filled in in absolute paths */
};

struct _FindNodeData
{
	GEqualFunc func;
	GNode *node;
	GFile *file;
};

struct _TrackerIndexingTreePrivate
{
	GNode *config_tree;
	GList *filter_patterns;
	TrackerFilterPolicy policies[TRACKER_FILTER_PARENT_DIRECTORY + 1];

	GFile *root;
	guint filter_hidden : 1;
};

G_DEFINE_TYPE_WITH_PRIVATE (TrackerIndexingTree, tracker_indexing_tree, G_TYPE_OBJECT)

enum {
	PROP_0,
	PROP_ROOT,
	PROP_FILTER_HIDDEN
};

enum {
	DIRECTORY_ADDED,
	DIRECTORY_REMOVED,
	DIRECTORY_UPDATED,
	CHILD_UPDATED,
	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

static NodeData *
node_data_new (GFile *file,
               guint  flags)
{
	NodeData *data;

	data = g_slice_new0 (NodeData);
	data->file = g_object_ref (file);
	data->flags = flags;

	return data;
}

static void
node_data_free (NodeData *data)
{
	g_object_unref (data->file);
	g_slice_free (NodeData, data);
}

static gboolean
node_free (GNode    *node,
           gpointer  user_data)
{
	node_data_free (node->data);
	return FALSE;
}

static PatternData *
pattern_data_new (const gchar *glob_string,
                  guint        type)
{
	PatternData *data;

	data = g_slice_new0 (PatternData);
	data->pattern = g_pattern_spec_new (glob_string);
	data->type = type;

	if (g_path_is_absolute (glob_string)) {
		data->file = g_file_new_for_path (glob_string);
	}

	return data;
}

static void
pattern_data_free (PatternData *data)
{
	if (data->file) {
		g_object_unref (data->file);
	}

	g_pattern_spec_free (data->pattern);
	g_slice_free (PatternData, data);
}

static void
tracker_indexing_tree_get_property (GObject    *object,
                                    guint       prop_id,
                                    GValue     *value,
                                    GParamSpec *pspec)
{
	TrackerIndexingTreePrivate *priv;

	priv = TRACKER_INDEXING_TREE (object)->priv;

	switch (prop_id) {
	case PROP_ROOT:
		g_value_set_object (value, priv->root);
		break;
	case PROP_FILTER_HIDDEN:
		g_value_set_boolean (value, priv->filter_hidden);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
tracker_indexing_tree_set_property (GObject      *object,
                                    guint         prop_id,
                                    const GValue *value,
                                    GParamSpec   *pspec)
{
	TrackerIndexingTree *tree;
	TrackerIndexingTreePrivate *priv;

	tree = TRACKER_INDEXING_TREE (object);
	priv = tree->priv;

	switch (prop_id) {
	case PROP_ROOT:
		priv->root = g_value_dup_object (value);
		break;
	case PROP_FILTER_HIDDEN:
		tracker_indexing_tree_set_filter_hidden (tree,
		                                         g_value_get_boolean (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
tracker_indexing_tree_constructed (GObject *object)
{
	TrackerIndexingTree *tree;
	TrackerIndexingTreePrivate *priv;
	NodeData *data;

	G_OBJECT_CLASS (tracker_indexing_tree_parent_class)->constructed (object);

	tree = TRACKER_INDEXING_TREE (object);
	priv = tree->priv;

	/* Add a shallow root node */
	if (priv->root == NULL) {
		priv->root = g_file_new_for_uri ("file:///");
	}

	data = node_data_new (priv->root, 0);
	data->shallow = TRUE;

	priv->config_tree = g_node_new (data);
}

static void
tracker_indexing_tree_finalize (GObject *object)
{
	TrackerIndexingTreePrivate *priv;
	TrackerIndexingTree *tree;

	tree = TRACKER_INDEXING_TREE (object);
	priv = tree->priv;

	g_list_foreach (priv->filter_patterns, (GFunc) pattern_data_free, NULL);
	g_list_free (priv->filter_patterns);

	g_node_traverse (priv->config_tree,
	                 G_POST_ORDER,
	                 G_TRAVERSE_ALL,
	                 -1,
	                 (GNodeTraverseFunc) node_free,
	                 NULL);
	g_node_destroy (priv->config_tree);

	if (priv->root) {
		g_object_unref (priv->root);
	}

	G_OBJECT_CLASS (tracker_indexing_tree_parent_class)->finalize (object);
}

static void
tracker_indexing_tree_class_init (TrackerIndexingTreeClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = tracker_indexing_tree_finalize;
	object_class->constructed = tracker_indexing_tree_constructed;
	object_class->set_property = tracker_indexing_tree_set_property;
	object_class->get_property = tracker_indexing_tree_get_property;

	g_object_class_install_property (object_class,
	                                 PROP_ROOT,
	                                 g_param_spec_object ("root",
	                                                      "Root URL",
	                                                      "The root GFile for the indexing tree",
	                                                      G_TYPE_FILE,
	                                                      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	g_object_class_install_property (object_class,
	                                 PROP_FILTER_HIDDEN,
	                                 g_param_spec_boolean ("filter-hidden",
	                                                       "Filter hidden",
	                                                       "Whether hidden resources are filtered",
	                                                       FALSE,
	                                                       G_PARAM_READWRITE));
	/**
	 * TrackerIndexingTree::directory-added:
	 * @indexing_tree: a #TrackerIndexingTree
	 * @directory: a #GFile
	 *
	 * the ::directory-added signal is emitted when a new
	 * directory is added to the list of other directories which
	 * are to be considered for indexing. Typically this is
	 * signalled when the tracker_indexing_tree_add() API is
	 * called.
	 *
	 * Since: 0.14
	 **/
	signals[DIRECTORY_ADDED] =
		g_signal_new ("directory-added",
		              G_OBJECT_CLASS_TYPE (object_class),
		              G_SIGNAL_RUN_LAST,
		              G_STRUCT_OFFSET (TrackerIndexingTreeClass,
		                               directory_added),
		              NULL, NULL,
		              NULL,
		              G_TYPE_NONE, 1, G_TYPE_FILE);

	/**
	 * TrackerIndexingTree::directory-removed:
	 * @indexing_tree: a #TrackerIndexingTree
	 * @directory: a #GFile
	 *
	 * the ::directory-removed signal is emitted when a
	 * directory is removed from the list of other directories
	 * which are to be considered for indexing. Typically this is
	 * signalled when the tracker_indexing_tree_remove() API is
	 * called.
	 *
	 * Since: 0.14
	 **/
	signals[DIRECTORY_REMOVED] =
		g_signal_new ("directory-removed",
		              G_OBJECT_CLASS_TYPE (object_class),
		              G_SIGNAL_RUN_LAST,
		              G_STRUCT_OFFSET (TrackerIndexingTreeClass,
		                               directory_removed),
		              NULL, NULL,
		              NULL,
		              G_TYPE_NONE, 1, G_TYPE_FILE);

	/**
	 * TrackerIndexingTree::directory-updated:
	 * @indexing_tree: a #TrackerIndexingTree
	 * @directory: a #GFile
	 *
	 * The ::directory-updated signal is emitted on a root
	 * when either its indexing flags change (e.g. due to consecutive
	 * calls to tracker_indexing_tree_add()), or anytime an update is
	 * requested through tracker_indexing_tree_notify_update().
	 *
	 * Since: 0.14
	 **/
	signals[DIRECTORY_UPDATED] =
		g_signal_new ("directory-updated",
		              G_OBJECT_CLASS_TYPE (object_class),
		              G_SIGNAL_RUN_LAST,
		              G_STRUCT_OFFSET (TrackerIndexingTreeClass,
		                               directory_updated),
		              NULL, NULL,
		              NULL,
		              G_TYPE_NONE, 1, G_TYPE_FILE);

	/**
	 * TrackerIndexingTree::child-updated:
	 * @indexing_tree: a #TrackerIndexingTree
	 * @root: the root of this child
	 * @child: the updated child
	 *
	 * The ::child-updated signal may be emitted to notify
	 * about possible changes on children of a root.
	 *
	 * #TrackerIndexingTree does not emit those by itself,
	 * those may be triggered through tracker_indexing_tree_notify_update().
	 *
	 * Since: 1.10
	 **/
	signals[CHILD_UPDATED] =
		g_signal_new ("child-updated",
		              G_OBJECT_CLASS_TYPE (object_class),
		              G_SIGNAL_RUN_LAST,
		              G_STRUCT_OFFSET (TrackerIndexingTreeClass,
		                               child_updated),
		              NULL, NULL,
		              NULL,
		              G_TYPE_NONE, 2, G_TYPE_FILE, G_TYPE_FILE);
}

static void
tracker_indexing_tree_init (TrackerIndexingTree *tree)
{
	TrackerIndexingTreePrivate *priv;
	gint i;

	priv = tree->priv = tracker_indexing_tree_get_instance_private (tree);

	for (i = TRACKER_FILTER_FILE; i <= TRACKER_FILTER_PARENT_DIRECTORY; i++) {
		priv->policies[i] = TRACKER_FILTER_POLICY_ACCEPT;
	}
}

/**
 * tracker_indexing_tree_new:
 *
 * Returns a newly created #TrackerIndexingTree
 *
 * Returns: a newly allocated #TrackerIndexingTree
 *
 * Since: 0.14
 **/
TrackerIndexingTree *
tracker_indexing_tree_new (void)
{
	return g_object_new (TRACKER_TYPE_INDEXING_TREE, NULL);
}

/**
 * tracker_indexing_tree_new_with_root:
 * @root: The top level URL
 *
 * If @root is %NULL, the default value is 'file:///'. Using %NULL
 * here is the equivalent to calling tracker_indexing_tree_new() which
 * takes no @root argument.
 *
 * Returns: a newly allocated #TrackerIndexingTree
 *
 * Since: 1.2.2
 **/
TrackerIndexingTree *
tracker_indexing_tree_new_with_root (GFile *root)
{
	return g_object_new (TRACKER_TYPE_INDEXING_TREE,
	                     "root", root,
	                     NULL);
}

#ifdef PRINT_INDEXING_TREE
static gboolean
print_node_foreach (GNode    *node,
                    gpointer  user_data)
{
	NodeData *node_data = node->data;
	gchar *uri;

	uri = g_file_get_uri (node_data->file);
	g_debug ("%*s %s", g_node_depth (node), "-", uri);
	g_free (uri);

	return FALSE;
}

static void
print_tree (GNode *node)
{
	g_debug ("Printing modified tree...");
	g_node_traverse (node,
	                 G_PRE_ORDER,
	                 G_TRAVERSE_ALL,
	                 -1,
	                 print_node_foreach,
	                 NULL);
}

#endif /* PRINT_INDEXING_TREE */

static gboolean
find_node_foreach (GNode    *node,
                   gpointer  user_data)
{
	FindNodeData *data = user_data;
	NodeData *node_data = node->data;

	if ((data->func) (data->file, node_data->file)) {
		data->node = node;
		return TRUE;
	}

	return FALSE;
}

static GNode *
find_directory_node (GNode      *node,
                     GFile      *file,
                     GEqualFunc  func)
{
	FindNodeData data;

	data.file = file;
	data.node = NULL;
	data.func = func;

	g_node_traverse (node,
	                 G_POST_ORDER,
	                 G_TRAVERSE_ALL,
	                 -1,
	                 find_node_foreach,
	                 &data);

	return data.node;
}

static void
check_reparent_node (GNode    *node,
                     gpointer  user_data)
{
	GNode *new_node = user_data;
	NodeData *new_node_data, *node_data;

	new_node_data = new_node->data;
	node_data = node->data;

	if (g_file_has_prefix (node_data->file,
	                       new_node_data->file)) {
		g_node_unlink (node);
		g_node_append (new_node, node);
	}
}

/**
 * tracker_indexing_tree_add:
 * @tree: a #TrackerIndexingTree
 * @directory: #GFile pointing to a directory
 * @flags: Configuration flags for the directory
 *
 * Adds a directory to the indexing tree with the
 * given configuration flags.
 **/
void
tracker_indexing_tree_add (TrackerIndexingTree   *tree,
                           GFile                 *directory,
                           TrackerDirectoryFlags  flags)
{
	TrackerIndexingTreePrivate *priv;
	GNode *parent, *node;
	NodeData *data;

	g_return_if_fail (TRACKER_IS_INDEXING_TREE (tree));
	g_return_if_fail (G_IS_FILE (directory));

	priv = tree->priv;
	node = find_directory_node (priv->config_tree, directory,
	                            (GEqualFunc) g_file_equal);

	if (node) {
		/* Node already existed */
		data = node->data;
		data->shallow = FALSE;

		/* Overwrite flags if they are different */
		if (data->flags != flags) {
			gchar *uri;

			uri = g_file_get_uri (directory);
			g_message ("Overwriting flags for directory '%s'", uri);
			g_free (uri);

			data->flags = flags;
			g_signal_emit (tree, signals[DIRECTORY_UPDATED], 0,
			               data->file);
		}
		return;
	}

	/* Find out the parent */
	parent = find_directory_node (priv->config_tree, directory,
	                              (GEqualFunc) g_file_has_prefix);

	/* Create node, move children of parent that
	 * could be children of this new node now.
	 */
	data = node_data_new (directory, flags);
	node = g_node_new (data);

	g_node_children_foreach (parent, G_TRAVERSE_ALL,
	                         check_reparent_node, node);

	/* Add the new node underneath the parent */
	g_node_append (parent, node);

	g_signal_emit (tree, signals[DIRECTORY_ADDED], 0, directory);

#ifdef PRINT_INDEXING_TREE
	/* Print tree */
	print_tree (priv->config_tree);
#endif /* PRINT_INDEXING_TREE */
}

/**
 * tracker_indexing_tree_remove:
 * @tree: a #TrackerIndexingTree
 * @directory: #GFile pointing to a directory
 *
 * Removes @directory from the indexing tree, note that
 * only directories previously added with tracker_indexing_tree_add()
 * can be effectively removed.
 **/
void
tracker_indexing_tree_remove (TrackerIndexingTree *tree,
                              GFile               *directory)
{
	TrackerIndexingTreePrivate *priv;
	GNode *node, *parent;
	NodeData *data;

	g_return_if_fail (TRACKER_IS_INDEXING_TREE (tree));
	g_return_if_fail (G_IS_FILE (directory));

	priv = tree->priv;
	node = find_directory_node (priv->config_tree, directory,
	                            (GEqualFunc) g_file_equal);
	if (!node) {
		return;
	}

	data = node->data;

	if (data->removing) {
		return;
	}

	data->removing = TRUE;

	if (!node->parent) {
		/* Node is the config tree
		 * root, mark as shallow again
		 */
		data->shallow = TRUE;
		return;
	}

	g_signal_emit (tree, signals[DIRECTORY_REMOVED], 0, data->file);

	parent = node->parent;
	g_node_unlink (node);

	/* Move children to parent */
	g_node_children_foreach (node, G_TRAVERSE_ALL,
	                         check_reparent_node, parent);

	node_data_free (node->data);
	g_node_destroy (node);
}

/**
 * tracker_indexing_tree_notify_update:
 * @tree: a #TrackerIndexingTree
 * @file: a #GFile
 * @recursive: Whether contained indexing roots are affected by the update
 *
 * Signals either #TrackerIndexingTree::directory-updated or
 * #TrackerIndexingTree::child-updated on the given file and
 * returns #TRUE. If @file is not indexed according to the
 * #TrackerIndexingTree, #FALSE is returned.
 *
 * If @recursive is #TRUE, #TrackerIndexingTree::directory-updated
 * will be emitted on the indexing roots that are contained in @file.
 *
 * Returns: #TRUE if a signal is emitted.
 *
 * Since: 1.10
 **/
gboolean
tracker_indexing_tree_notify_update (TrackerIndexingTree *tree,
                                     GFile               *file,
                                     gboolean             recursive)
{
	TrackerDirectoryFlags flags;
	gboolean emitted = FALSE;
	GFile *root;

	g_return_val_if_fail (TRACKER_IS_INDEXING_TREE (tree), FALSE);
	g_return_val_if_fail (G_IS_FILE (file), FALSE);

	root = tracker_indexing_tree_get_root (tree, file, &flags);

	if (tracker_indexing_tree_file_is_root (tree, file)) {
		g_signal_emit (tree, signals[DIRECTORY_UPDATED], 0, root);
		emitted = TRUE;
	} else if (root &&
	           ((flags & TRACKER_DIRECTORY_FLAG_RECURSE) ||
	            g_file_has_parent (file, root))) {
		g_signal_emit (tree, signals[CHILD_UPDATED], 0, root, file);
		emitted = TRUE;
	}

	if (recursive) {
		GList *roots, *l;

		roots = tracker_indexing_tree_list_roots (tree);

		for (l = roots; l; l = l->next) {
			if (!g_file_has_prefix (l->data, file))
				continue;

			g_signal_emit (tree, signals[DIRECTORY_UPDATED], 0, l->data);
			emitted = TRUE;
		}

		g_list_free (roots);
	}

	return emitted;
}

/**
 * tracker_indexing_tree_add_filter:
 * @tree: a #TrackerIndexingTree
 * @filter: filter type
 * @glob_string: glob-style string for the filter
 *
 * Adds a new filter for basenames.
 **/
void
tracker_indexing_tree_add_filter (TrackerIndexingTree *tree,
                                  TrackerFilterType    filter,
                                  const gchar         *glob_string)
{
	TrackerIndexingTreePrivate *priv;
	PatternData *data;

	g_return_if_fail (TRACKER_IS_INDEXING_TREE (tree));
	g_return_if_fail (glob_string != NULL);

	priv = tree->priv;

	data = pattern_data_new (glob_string, filter);
	priv->filter_patterns = g_list_prepend (priv->filter_patterns, data);
}

/**
 * tracker_indexing_tree_clear_filters:
 * @tree: a #TrackerIndexingTree
 * @type: filter type to clear
 *
 * Clears all filters of a given type.
 **/
void
tracker_indexing_tree_clear_filters (TrackerIndexingTree *tree,
                                     TrackerFilterType    type)
{
	TrackerIndexingTreePrivate *priv;
	GList *l;

	g_return_if_fail (TRACKER_IS_INDEXING_TREE (tree));

	priv = tree->priv;

	for (l = priv->filter_patterns; l; l = l->next) {
		PatternData *data = l->data;

		if (data->type == type) {
			/* When we delete the link 'l', we point back
			 * to the beginning of the list to make sure
			 * we don't miss anything.
			 */
			l = priv->filter_patterns = g_list_delete_link (priv->filter_patterns, l);
			pattern_data_free (data);
		}
	}
}

/**
 * tracker_indexing_tree_file_matches_filter:
 * @tree: a #TrackerIndexingTree
 * @type: filter type
 * @file: a #GFile
 *
 * Returns %TRUE if @file matches any filter of the given filter type.
 *
 * Returns: %TRUE if @file is filtered.
 **/
gboolean
tracker_indexing_tree_file_matches_filter (TrackerIndexingTree *tree,
                                           TrackerFilterType    type,
                                           GFile               *file)
{
	TrackerIndexingTreePrivate *priv;
	GList *filters;
	gchar *basename, *str, *reverse;
	gboolean match = FALSE;
	gint len;

	g_return_val_if_fail (TRACKER_IS_INDEXING_TREE (tree), FALSE);
	g_return_val_if_fail (G_IS_FILE (file), FALSE);

	priv = tree->priv;
	filters = priv->filter_patterns;
	basename = g_file_get_basename (file);

	str = g_utf8_make_valid (basename, -1);
	len = strlen (str);
	reverse = g_utf8_strreverse (str, len);

	while (filters) {
		PatternData *data = filters->data;

		filters = filters->next;

		if (data->type != type)
			continue;

		if (data->file &&
		    (g_file_equal (file, data->file) ||
		     g_file_has_prefix (file, data->file))) {
			match = TRUE;
			break;
		}

		if (g_pattern_match (data->pattern, len, str, reverse)) {
			match = TRUE;
			break;
		}
	}

	g_free (basename);
	g_free (str);
	g_free (reverse);

	return match;
}

static gboolean
parent_or_equals (GFile *file1,
                  GFile *file2)
{
	return (file1 == file2 ||
	        g_file_equal (file1, file2) ||
	        g_file_has_prefix (file1, file2));
}

static gboolean
indexing_tree_file_is_filtered (TrackerIndexingTree *tree,
                                TrackerFilterType    filter,
                                GFile               *file)
{
	TrackerIndexingTreePrivate *priv;

	priv = tree->priv;

	if (tracker_indexing_tree_file_matches_filter (tree, filter, file)) {
		if (priv->policies[filter] == TRACKER_FILTER_POLICY_ACCEPT) {
			/* Filter blocks otherwise accepted
			 * (by the default policy) file
			 */
			return TRUE;
		}
	} else {
		if (priv->policies[filter] == TRACKER_FILTER_POLICY_DENY) {
			/* No match, and the default policy denies it */
			return TRUE;
		}
	}

	return FALSE;
}

/**
 * tracker_indexing_tree_file_is_indexable:
 * @tree: a #TrackerIndexingTree
 * @file: a #GFile
 * @file_type: a #GFileType
 *
 * returns %TRUE if @file should be indexed according to the
 * parameters given through tracker_indexing_tree_add() and
 * tracker_indexing_tree_add_filter().
 *
 * If @file_type is #G_FILE_TYPE_UNKNOWN, file type will be queried to the
 * file system.
 *
 * Returns: %TRUE if @file should be indexed.
 **/
gboolean
tracker_indexing_tree_file_is_indexable (TrackerIndexingTree *tree,
                                         GFile               *file,
                                         GFileType            file_type)
{
	TrackerFilterType filter;
	TrackerDirectoryFlags config_flags;
	GFile *config_file;

	g_return_val_if_fail (TRACKER_IS_INDEXING_TREE (tree), FALSE);
	g_return_val_if_fail (G_IS_FILE (file), FALSE);

	config_file = tracker_indexing_tree_get_root (tree, file, &config_flags);
	if (!config_file) {
		/* Not under an added dir */
		return FALSE;
	}

	/* Don't check file type if _NO_STAT is given in flags */
	if (file_type == G_FILE_TYPE_UNKNOWN &&
	    (config_flags & TRACKER_DIRECTORY_FLAG_NO_STAT) != 0) {
		GFileQueryInfoFlags file_flags;

		file_flags = G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS;

		file_type = g_file_query_file_type (file, file_flags, NULL);

		filter = (file_type == G_FILE_TYPE_DIRECTORY) ?
			TRACKER_FILTER_DIRECTORY : TRACKER_FILTER_FILE;

		if (indexing_tree_file_is_filtered (tree, filter, file)) {
			return FALSE;
		}
	} else if (file_type != G_FILE_TYPE_UNKNOWN) {
		filter = (file_type == G_FILE_TYPE_DIRECTORY) ?
			TRACKER_FILTER_DIRECTORY : TRACKER_FILTER_FILE;

		if (indexing_tree_file_is_filtered (tree, filter, file)) {
			return FALSE;
		}
	}

	/* FIXME: Shouldn't we only do this for file_type == G_FILE_TYPE_DIRECTORY ? */
	if (config_flags & TRACKER_DIRECTORY_FLAG_IGNORE) {
		return FALSE;
	}

	if (g_file_equal (file, config_file)) {
		return TRUE;
	} else {
		if ((config_flags & TRACKER_DIRECTORY_FLAG_RECURSE) == 0 &&
		    !g_file_has_parent (file, config_file)) {
			/* Non direct child in a non-recursive dir, ignore */
			return FALSE;
		}

		if (tracker_indexing_tree_get_filter_hidden (tree) &&
		    tracker_file_is_hidden (file)) {
			return FALSE;
		}

		return TRUE;
	}
}

/**
 * tracker_indexing_tree_parent_is_indexable:
 * @tree: a #TrackerIndexingTree
 * @parent: parent directory
 * @children: (element-type GFile): children within @parent
 *
 * returns %TRUE if @parent should be indexed based on its contents.
 *
 * Returns: %TRUE if @parent should be indexed.
 **/
gboolean
tracker_indexing_tree_parent_is_indexable (TrackerIndexingTree *tree,
                                           GFile               *parent,
                                           GList               *children)
{
	TrackerIndexingTreePrivate *priv;
	gboolean has_match = FALSE;

	g_return_val_if_fail (TRACKER_IS_INDEXING_TREE (tree), FALSE);
	g_return_val_if_fail (G_IS_FILE (parent), FALSE);

	priv = tree->priv;

	if (!tracker_indexing_tree_file_is_indexable (tree,
	                                              parent,
	                                              G_FILE_TYPE_DIRECTORY)) {
		return FALSE;
	}

	while (children && !has_match) {
		has_match = tracker_indexing_tree_file_matches_filter (tree,
		                                                       TRACKER_FILTER_PARENT_DIRECTORY,
		                                                       children->data);
		children = children->next;
	}

	if (priv->policies[TRACKER_FILTER_PARENT_DIRECTORY] == TRACKER_FILTER_POLICY_ACCEPT)
		return !has_match;
	else
		return has_match;
}

/**
 * tracker_indexing_tree_get_filter_hidden:
 * @tree: a #TrackerIndexingTree
 *
 * Describes if the @tree should index hidden content. To change this
 * setting, see tracker_indexing_tree_set_filter_hidden().
 *
 * Returns: %FALSE if hidden files are indexed, otherwise %TRUE.
 *
 * Since: 0.18
 **/
gboolean
tracker_indexing_tree_get_filter_hidden (TrackerIndexingTree *tree)
{
	TrackerIndexingTreePrivate *priv;

	g_return_val_if_fail (TRACKER_IS_INDEXING_TREE (tree), FALSE);

	priv = tree->priv;
	return priv->filter_hidden;
}

/**
 * tracker_indexing_tree_set_filter_hidden:
 * @tree: a #TrackerIndexingTree
 * @filter_hidden: a boolean
 *
 * When indexing content, sometimes it is preferable to ignore hidden
 * content, for example, files prefixed with &quot;.&quot;. This is
 * common for files in a home directory which are usually config
 * files.
 *
 * Sets the indexing policy for @tree with hidden files and content.
 * To ignore hidden files, @filter_hidden should be %TRUE, otherwise
 * %FALSE.
 *
 * Since: 0.18
 **/
void
tracker_indexing_tree_set_filter_hidden (TrackerIndexingTree *tree,
                                         gboolean             filter_hidden)
{
	TrackerIndexingTreePrivate *priv;

	g_return_if_fail (TRACKER_IS_INDEXING_TREE (tree));

	priv = tree->priv;
	priv->filter_hidden = filter_hidden;

	g_object_notify (G_OBJECT (tree), "filter-hidden");
}

/**
 * tracker_indexing_tree_set_default_policy:
 * @tree: a #TrackerIndexingTree
 * @filter: a #TrackerFilterType
 * @policy: a #TrackerFilterPolicy
 *
 * Set the default @policy (to allow or deny) for content in @tree
 * based on the type - in this case @filter. Here, @filter is a file
 * or directory and there are some other options too.
 *
 * For example, you can (by default), disable indexing all directories
 * using this function.
 *
 * Since: 0.18
 **/
void
tracker_indexing_tree_set_default_policy (TrackerIndexingTree *tree,
                                          TrackerFilterType    filter,
                                          TrackerFilterPolicy  policy)
{
	TrackerIndexingTreePrivate *priv;

	g_return_if_fail (TRACKER_IS_INDEXING_TREE (tree));
	g_return_if_fail (filter >= TRACKER_FILTER_FILE && filter <= TRACKER_FILTER_PARENT_DIRECTORY);

	priv = tree->priv;
	priv->policies[filter] = policy;
}

/**
 * tracker_indexing_tree_get_default_policy:
 * @tree: a #TrackerIndexingTree
 * @filter: a #TrackerFilterType
 *
 * Get the default filtering policies for @tree when indexing content.
 * Some content is black listed or white listed and the default policy
 * for that is returned here. The @filter allows specific type of
 * policies to be returned, for example, the default policy for files
 * (#TRACKER_FILTER_FILE).
 *
 * Returns: Either #TRACKER_FILTER_POLICY_DENY or
 * #TRACKER_FILTER_POLICY_ACCEPT.
 *
 * Since: 0.18
 **/
TrackerFilterPolicy
tracker_indexing_tree_get_default_policy (TrackerIndexingTree *tree,
                                          TrackerFilterType    filter)
{
	TrackerIndexingTreePrivate *priv;

	g_return_val_if_fail (TRACKER_IS_INDEXING_TREE (tree),
	                      TRACKER_FILTER_POLICY_DENY);
	g_return_val_if_fail (filter >= TRACKER_FILTER_FILE &&
	                      filter <= TRACKER_FILTER_PARENT_DIRECTORY,
	                      TRACKER_FILTER_POLICY_DENY);

	priv = tree->priv;
	return priv->policies[filter];
}

/**
 * tracker_indexing_tree_get_root:
 * @tree: a #TrackerIndexingTree
 * @file: a #GFile
 * @directory_flags: (out): return location for the applying #TrackerDirectoryFlags
 *
 * Returns the #GFile that was previously added through tracker_indexing_tree_add()
 * and would equal or contain @file, or %NULL if none applies.
 *
 * If the return value is non-%NULL, @directory_flags would contain the
 * #TrackerDirectoryFlags applying to @file.
 *
 * Returns: (transfer none): the effective parent in @tree, or %NULL
 **/
GFile *
tracker_indexing_tree_get_root (TrackerIndexingTree   *tree,
                                GFile                 *file,
                                TrackerDirectoryFlags *directory_flags)
{
	TrackerIndexingTreePrivate *priv;
	NodeData *data;
	GNode *parent;

	if (directory_flags) {
		*directory_flags = TRACKER_DIRECTORY_FLAG_NONE;
	}

	g_return_val_if_fail (TRACKER_IS_INDEXING_TREE (tree), NULL);
	g_return_val_if_fail (G_IS_FILE (file), NULL);

	priv = tree->priv;
	parent = find_directory_node (priv->config_tree, file,
	                              (GEqualFunc) parent_or_equals);
	if (!parent) {
		return NULL;
	}

	data = parent->data;

	if (!data->shallow &&
	    (file == data->file ||
	     g_file_equal (file, data->file) ||
	     g_file_has_prefix (file, data->file))) {
		if (directory_flags) {
			*directory_flags = data->flags;
		}

		return data->file;
	}

	return NULL;
}

/**
 * tracker_indexing_tree_get_master_root:
 * @tree: a #TrackerIndexingTree
 *
 * Returns the #GFile that represents the master root location for all
 * indexing locations. For example, if
 * <filename>file:///etc</filename> is an indexed path and so was
 * <filename>file:///home/user</filename>, the master root is
 * <filename>file:///</filename>. Only one scheme per @tree can be
 * used, so you can not mix <filename>http</filename> and
 * <filename>file</filename> roots in @tree.
 *
 * The return value should <emphasis>NEVER</emphasis> be %NULL. In
 * cases where no root is given, we fallback to
 * <filename>file:///</filename>.
 *
 * Roots explained:
 *
 * - master root = top most level root node,
 *   e.g. file:///
 *
 * - config root = a root node from GSettings,
 *   e.g. file:///home/martyn/Documents
 *
 * - root = ANY root, normally config root, but it can also apply to
 *   roots added for devices, which technically are not a config root or a
 *   master root.
 *
 * Returns: (transfer none): the effective root for all locations, or
 * %NULL on error. The root is owned by @tree and should not be freed.
 * It can be referenced using g_object_ref().
 *
 * Since: 1.2
 **/
GFile *
tracker_indexing_tree_get_master_root (TrackerIndexingTree *tree)
{
	TrackerIndexingTreePrivate *priv;

	g_return_val_if_fail (TRACKER_IS_INDEXING_TREE (tree), NULL);

	priv = tree->priv;

	return priv->root;
}

/**
 * tracker_indexing_tree_file_is_root:
 * @tree: a #TrackerIndexingTree
 * @file: a #GFile to compare
 *
 * Evaluates if the URL represented by @file is the same of that for
 * the root of the @tree.
 *
 * Returns: %TRUE if @file matches the URL canonically, otherwise %FALSE.
 *
 * Since: 1.2
 **/
gboolean
tracker_indexing_tree_file_is_root (TrackerIndexingTree *tree,
                                    GFile               *file)
{
	TrackerIndexingTreePrivate *priv;
	GNode *node;

	g_return_val_if_fail (TRACKER_IS_INDEXING_TREE (tree), FALSE);
	g_return_val_if_fail (G_IS_FILE (file), FALSE);

	priv = tree->priv;
	node = find_directory_node (priv->config_tree, file,
	                            (GEqualFunc) g_file_equal);
	return node != NULL;
}

static gboolean
prepend_config_root (GNode    *node,
                     gpointer  user_data)
{
	GList **list = user_data;
	NodeData *data = node->data;

	if (!data->shallow && !data->removing)
		*list = g_list_prepend (*list, data->file);
	return FALSE;
}

/**
 * tracker_indexing_tree_list_roots:
 * @tree: a #TrackerIndexingTree
 *
 * Returns the list of indexing roots in @tree
 *
 * Returns: (transfer container) (element-type GFile): The list
 *          of roots, the list itself must be freed with g_list_free(),
 *          the list elements are owned by @tree and should not be
 *          freed.
 **/
GList *
tracker_indexing_tree_list_roots (TrackerIndexingTree *tree)
{
	TrackerIndexingTreePrivate *priv;
	GList *nodes = NULL;

	g_return_val_if_fail (TRACKER_IS_INDEXING_TREE (tree), NULL);

	priv = tree->priv;
	g_node_traverse (priv->config_tree,
	                 G_POST_ORDER,
	                 G_TRAVERSE_ALL,
	                 -1,
	                 prepend_config_root,
	                 &nodes);
	return nodes;
}
