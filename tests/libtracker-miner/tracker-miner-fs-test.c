#include <glib.h>
#include <libtracker-miner/tracker-miner.h>
#include <libtracker-common/tracker-common.h>

typedef struct {
	TrackerMinerFS parent_instance;
	guint n_process_file;
	guint n_events;
	guint finished : 1;
} TestMiner;

typedef struct {
	TrackerMinerFSClass parent_class;
} TestMinerClass;

/* Fixture object type */
typedef struct {
	TrackerMinerFS *miner;
	TrackerSparqlConnection *connection;
	gchar *test_root_path;
	GFile *test_root;
} TrackerMinerFSTestFixture;

G_DEFINE_TYPE (TestMiner, test_miner, TRACKER_TYPE_MINER_FS)

#define ADD_TEST(name, func) \
	g_test_add ("/libtracker-miner/tracker-miner-fs/" name, \
	            TrackerMinerFSTestFixture, NULL, \
	            fixture_setup, func, fixture_teardown)

static gboolean
test_miner_process_file (TrackerMinerFS *miner,
                         GFile          *file,
                         GTask          *task)
{
	TrackerResource *resource;
	GError *error = NULL;
	GFileInfo *info;
	GTimeVal timeval;
	gchar *sparql, *str;
	gchar *urn;
	GFile *parent;

	((TestMiner *) miner)->n_process_file++;
	info = g_file_query_info (file, "standard::*,time::*", 0, NULL, &error);
	g_assert_no_error (error);

	urn = tracker_miner_fs_query_urn (miner, file);
	if (g_strcmp0 (tracker_miner_fs_get_urn (miner, file), urn) != 0) {
		g_critical ("File %s did not get up to date URN",
		            g_file_get_uri (file));
	}
	g_free (urn);

	resource = tracker_resource_new (tracker_miner_fs_get_urn (miner, file));

	if (g_file_info_get_file_type (info) == G_FILE_TYPE_DIRECTORY) {
		tracker_resource_add_uri (resource, "rdf:type", "nfo:Folder");
	}

	tracker_resource_add_uri (resource, "rdf:type", "nfo:FileDataObject");

	g_file_info_get_modification_time (info, &timeval);
	str = tracker_date_to_string (timeval.tv_sec);
	tracker_resource_set_string (resource, "nfo:fileLastModified", str);
	g_free (str);

	str = g_file_get_uri (file);
	tracker_resource_set_string (resource, "nie:url", str);
	g_free (str);

	parent = g_file_get_parent (file);
	urn = tracker_miner_fs_query_urn (miner, parent);
	g_object_unref (parent);

	if (urn) {
		tracker_resource_set_string (resource, "nfo:belongsToContainer", urn);
		g_free (urn);
	}

	sparql = tracker_resource_print_sparql_update (resource, NULL, NULL);
	tracker_miner_fs_notify_finish (miner, task, sparql, NULL);
	g_object_unref (resource);
	g_free (sparql);
	g_object_unref (info);

	return TRUE;
}

static gchar *
test_miner_remove_file (TrackerMinerFS *miner,
                        GFile          *file)
{
	gchar *sparql, *uri;

	uri = g_file_get_uri (file);
	sparql = g_strdup_printf ("DELETE {"
	                          "  ?u a rdfs:Resource . "
	                          "} WHERE {"
	                          "  ?u nie:url ?url ."
	                          "  FILTER (?url = '%s' || STRSTARTS (?url, '%s/'))"
	                          "}", uri, uri);
	g_free (uri);

	return sparql;
}

static gchar *
test_miner_remove_children (TrackerMinerFS *miner,
                            GFile          *file)
{
	gchar *sparql, *uri;

	uri = g_file_get_uri (file);
	sparql = g_strdup_printf ("DELETE {"
	                          "  ?u a rdfs:Resource . "
	                          "} WHERE {"
	                          "  ?u nie:url ?url ."
	                          "  FILTER (STRSTARTS (?url, '%s/'))"
	                          "}", uri);
	g_free (uri);

	return sparql;
}

static gchar *
test_miner_move_file (TrackerMinerFS *miner,
                      GFile          *dest,
                      GFile          *source,
                      gboolean        recursive)
{
	gchar *sparql, *uri, *dest_uri;

	uri = g_file_get_uri (source);
	dest_uri = g_file_get_uri (dest);
	sparql = g_strdup_printf ("DELETE {"
	                          "  ?u nie:url ?url . "
	                          "} INSERT {"
	                          "  ?u nie:url '%s' . "
	                          "} WHERE {"
	                          "  ?u nie:url ?url . "
	                          "  FILTER (?url = '%s')"
	                          "}", dest_uri, uri);
	g_free (dest_uri);
	g_free (uri);

	return sparql;
}

static gboolean
test_miner_filter_event (TrackerMinerFS          *miner,
                         TrackerMinerFSEventType  type,
                         GFile                   *file,
                         GFile                   *source_file)
{
	TestMiner *m = (TestMiner *) miner;

	m->n_events++;
	return FALSE;
}

static void
test_miner_finished (TrackerMinerFS *miner,
                     gdouble         elapsed,
                     gint            directories_found,
                     gint            directories_ignored,
                     gint            files_found,
                     gint            files_ignored)
{
	((TestMiner *) miner)->finished = TRUE;
}

static void
test_miner_class_init (TestMinerClass *klass)
{
	TrackerMinerFSClass *fs_class = TRACKER_MINER_FS_CLASS (klass);

	fs_class->process_file = test_miner_process_file;
	fs_class->process_file_attributes = test_miner_process_file;
	fs_class->remove_file = test_miner_remove_file;
	fs_class->remove_children = test_miner_remove_children;
	fs_class->move_file = test_miner_move_file;
	fs_class->filter_event = test_miner_filter_event;

	fs_class->finished = test_miner_finished;
}

static void
test_miner_init (TestMiner *miner)
{
}

static TrackerMinerFS *
test_miner_new (TrackerSparqlConnection  *conn,
                GError                  **error)
{
	return g_initable_new (test_miner_get_type (),
	                       NULL, error,
	                       "connection", conn,
	                       NULL);
}

static gboolean
test_miner_is_finished (TestMiner *miner)
{
	gboolean finished;

	finished = miner->finished;
	miner->finished = FALSE;

	return finished;
}

static void
test_miner_reset_counters (TestMiner *miner)
{
	miner->n_process_file = 0;
	miner->n_events = 0;
}

static void
perform_file_operation (TrackerMinerFSTestFixture *fixture,
                        gchar                     *command,
                        gchar                     *filename,
                        gchar                     *other_filename)
{
	gchar *path, *other_path, *call;

	path = g_build_filename (fixture->test_root_path, filename, NULL);

	if (other_filename) {
		other_path = g_build_filename (fixture->test_root_path, other_filename, NULL);
		call = g_strdup_printf ("%s %s %s", command, path, other_path);
		g_free (other_path);
	} else {
		call = g_strdup_printf ("%s %s", command, path);
	}

	system (call);

	g_free (call);
	g_free (path);
}

#define CREATE_FOLDER(fixture,p) perform_file_operation((fixture),"mkdir",(p),NULL)
#define CREATE_UPDATE_FILE(fixture,p) perform_file_operation((fixture),"touch",(p),NULL)
#define DELETE_FILE(fixture,p) perform_file_operation((fixture),"rm",(p),NULL)
#define DELETE_FOLDER(fixture,p) perform_file_operation((fixture),"rm -rf",(p),NULL)
#define MOVE_FILE(fixture,p1,p2) perform_file_operation((fixture),"mv",(p1),(p2))
#define UPDATE_FILE_ATOMIC(fixture,p,tmpname) \
	G_STMT_START { \
	CREATE_UPDATE_FILE((fixture), (tmpname)); \
	MOVE_FILE ((fixture), (tmpname), (p)); \
	} G_STMT_END

static void
fixture_setup (TrackerMinerFSTestFixture *fixture,
               gconstpointer              data)
{
	GError *error = NULL;
	GFile *file, *ontology;
	gchar *path;

	path = g_build_filename (g_get_tmp_dir (), "tracker-miner-fs-test-XXXXXX", NULL);
	fixture->test_root_path = g_mkdtemp_full (path, 0700);
	fixture->test_root = g_file_new_for_path (fixture->test_root_path);

	file = g_file_get_child (fixture->test_root, ".db");
	ontology = g_file_new_for_path (TEST_ONTOLOGIES_DIR);
	fixture->connection = tracker_sparql_connection_local_new (0, file, file, ontology, NULL, &error);
	g_assert_no_error (error);
	g_object_unref (file);
	g_object_unref (ontology);

	fixture->miner = test_miner_new (fixture->connection, &error);
	g_assert_no_error (error);
}

static void
fixture_teardown (TrackerMinerFSTestFixture *fixture,
                  gconstpointer              data)
{
	DELETE_FOLDER (fixture, "/");

	g_object_unref (fixture->test_root);
	g_object_unref (fixture->miner);
	g_object_unref (fixture->connection);
	g_free (fixture->test_root_path);
}

static GFile *
fixture_get_relative_file (TrackerMinerFSTestFixture *fixture,
                           const gchar               *rel_path)
{
	GFile *file;
	gchar *path;

	path = g_build_filename (fixture->test_root_path, rel_path, NULL);
	file = g_file_new_for_path (path);
	g_free (path);

	return file;
}

static void
fixture_add_indexed_folder (TrackerMinerFSTestFixture *fixture,
                            const gchar               *rel_path,
                            TrackerDirectoryFlags      flags)
{
	GFile *file;

	file = fixture_get_relative_file (fixture, rel_path);
	tracker_indexing_tree_add (tracker_miner_fs_get_indexing_tree (fixture->miner),
	                           file, flags);
	g_object_unref (file);
}

static void
fixture_remove_indexed_folder (TrackerMinerFSTestFixture *fixture,
                               const gchar               *rel_path)
{
	GFile *file;

	file = fixture_get_relative_file (fixture, rel_path);
	tracker_indexing_tree_remove (tracker_miner_fs_get_indexing_tree (fixture->miner),
	                              file);
	g_object_unref (file);
}

static gboolean
fixture_query_exists (TrackerMinerFSTestFixture *fixture,
                      const gchar               *rel_path)
{
	GFile *file = fixture_get_relative_file (fixture, rel_path);
	TrackerSparqlCursor *cursor;
	gchar *sparql, *uri;
	GError *error = NULL;

	uri = g_file_get_uri (file);
	sparql = g_strdup_printf ("SELECT ?u { ?u nie:url '%s' }", uri);
	g_free (uri);

	cursor = tracker_sparql_connection_query (fixture->connection, sparql,
	                                          NULL, &error);
	g_assert_no_error (error);
	g_free (sparql);

	if (tracker_sparql_cursor_next (cursor, NULL, &error)) {
		g_object_unref (cursor);
		return TRUE;
	} else {
		g_assert_no_error (error);
		return FALSE;
	}
}

static gchar *
fixture_get_content (TrackerMinerFSTestFixture *fixture)
{
	TrackerSparqlCursor *cursor;
	GString *str = g_string_new ("");
	GError *error = NULL;
	gchar *query, *root_uri;

	root_uri = g_file_get_uri (fixture->test_root);
	query = g_strdup_printf ("SELECT ?path "
	                         "{ ?u a nfo:FileDataObject ;"
	                         "     nie:url ?url ."
	                         "  BIND (SUBSTR (?url, %d) AS ?path)"
	                         "} ORDER BY ?path",
	                         (int)strlen (root_uri) + 2);
	g_free (root_uri);


	cursor = tracker_sparql_connection_query (fixture->connection, query,
	                                          NULL, &error);
	g_assert_no_error (error);
	g_free (query);

	while (tracker_sparql_cursor_next (cursor, NULL, &error)) {
		if (str->len > 0)
			g_string_append_c (str, ',');
		g_string_append (str, tracker_sparql_cursor_get_string (cursor, 0, NULL));
	}

	g_assert_no_error (error);

	tracker_sparql_cursor_close (cursor);
	g_object_unref (cursor);

	return g_string_free (str, FALSE);
}

static void
fixture_iterate (TrackerMinerFSTestFixture *fixture)
{
	while (!test_miner_is_finished ((TestMiner *) fixture->miner))
		g_main_context_iteration (NULL, TRUE);
}

static void
fixture_iterate_filter (TrackerMinerFSTestFixture *fixture,
                        guint                      n_events)
{
	TestMiner *miner = (TestMiner *) fixture->miner;

	miner->n_events = 0;

	while (miner->n_events < n_events)
		g_main_context_iteration (NULL, TRUE);
}

static void
test_recursive_indexing (TrackerMinerFSTestFixture *fixture,
                         gconstpointer              data)
{
	gchar *content;

	CREATE_FOLDER (fixture, "recursive");
	CREATE_FOLDER (fixture, "recursive/1");
	CREATE_FOLDER (fixture, "recursive/1/2");
	CREATE_FOLDER (fixture, "recursive/1/empty");
	CREATE_UPDATE_FILE (fixture, "recursive/1/a");
	CREATE_UPDATE_FILE (fixture, "recursive/1/b");
	CREATE_UPDATE_FILE (fixture, "recursive/1/2/c");
	fixture_add_indexed_folder (fixture, "recursive",
	                            TRACKER_DIRECTORY_FLAG_CHECK_MTIME |
	                            TRACKER_DIRECTORY_FLAG_RECURSE);

	tracker_miner_start (TRACKER_MINER (fixture->miner));

	fixture_iterate (fixture);

	content = fixture_get_content (fixture);
	g_assert_cmpstr (content, ==,
	                 "recursive,"
	                 "recursive/1,"
	                 "recursive/1/2,"
	                 "recursive/1/2/c,"
	                 "recursive/1/a,"
	                 "recursive/1/b,"
	                 "recursive/1/empty");
	g_free (content);
}

static void
test_non_recursive_indexing (TrackerMinerFSTestFixture *fixture,
                             gconstpointer              data)
{
	gchar *content;

	CREATE_FOLDER (fixture, "non-recursive");
	CREATE_FOLDER (fixture, "non-recursive/1");
	CREATE_FOLDER (fixture, "non-recursive/1/2");
	CREATE_FOLDER (fixture, "non-recursive/empty");
	CREATE_UPDATE_FILE (fixture, "non-recursive/a");
	CREATE_UPDATE_FILE (fixture, "non-recursive/1/b");

	fixture_add_indexed_folder (fixture, "non-recursive",
	                            TRACKER_DIRECTORY_FLAG_CHECK_MTIME);

	tracker_miner_start (TRACKER_MINER (fixture->miner));

	fixture_iterate (fixture);

	content = fixture_get_content (fixture);
	g_assert_cmpstr (content, ==,
	                 "non-recursive,"
	                 "non-recursive/1,"
	                 "non-recursive/a,"
	                 "non-recursive/empty");
	g_free (content);
}

static void
test_separate_recursive_and_non_recursive (TrackerMinerFSTestFixture *fixture,
                                           gconstpointer              data)
{
	gchar *content;

	CREATE_FOLDER (fixture, "recursive");
	CREATE_FOLDER (fixture, "recursive/1");
	CREATE_FOLDER (fixture, "recursive/1/2");
	CREATE_FOLDER (fixture, "non-recursive");
	CREATE_FOLDER (fixture, "non-recursive/1");
	CREATE_FOLDER (fixture, "non-recursive/1/2");
	CREATE_UPDATE_FILE (fixture, "recursive/a");
	CREATE_UPDATE_FILE (fixture, "recursive/1/b");
	CREATE_UPDATE_FILE (fixture, "non-recursive/a");
	CREATE_UPDATE_FILE (fixture, "non-recursive/1/b");

	fixture_add_indexed_folder (fixture, "recursive",
	                            TRACKER_DIRECTORY_FLAG_CHECK_MTIME |
	                            TRACKER_DIRECTORY_FLAG_RECURSE);
	fixture_add_indexed_folder (fixture, "non-recursive",
	                            TRACKER_DIRECTORY_FLAG_CHECK_MTIME);

	tracker_miner_start (TRACKER_MINER (fixture->miner));

	fixture_iterate (fixture);

	content = fixture_get_content (fixture);
	g_assert_cmpstr (content, ==,
	                 "non-recursive,"
	                 "non-recursive/1,"
	                 "non-recursive/a,"
	                 "recursive,"
	                 "recursive/1,"
	                 "recursive/1/2,"
	                 "recursive/1/b,"
	                 "recursive/a");
	g_free (content);
}

static void
test_recursive_in_non_recursive (TrackerMinerFSTestFixture *fixture,
                                 gconstpointer              data)
{
	gchar *content;

	CREATE_FOLDER (fixture, "non-recursive");
	CREATE_FOLDER (fixture, "non-recursive/1");
	CREATE_FOLDER (fixture, "non-recursive/1/recursive");
	CREATE_FOLDER (fixture, "non-recursive/1/recursive/2");
	CREATE_UPDATE_FILE (fixture, "non-recursive/a");
	CREATE_UPDATE_FILE (fixture, "non-recursive/1/b");
	CREATE_UPDATE_FILE (fixture, "non-recursive/1/recursive/c");
	CREATE_UPDATE_FILE (fixture, "non-recursive/1/recursive/2/d");

	fixture_add_indexed_folder (fixture, "non-recursive/1/recursive",
	                            TRACKER_DIRECTORY_FLAG_CHECK_MTIME |
	                            TRACKER_DIRECTORY_FLAG_RECURSE);
	fixture_add_indexed_folder (fixture, "non-recursive",
	                            TRACKER_DIRECTORY_FLAG_CHECK_MTIME);

	tracker_miner_start (TRACKER_MINER (fixture->miner));

	fixture_iterate (fixture);

	content = fixture_get_content (fixture);
	g_assert_cmpstr (content, ==,
	                 "non-recursive,"
	                 "non-recursive/1,"
	                 "non-recursive/1/recursive,"
	                 "non-recursive/1/recursive/2,"
	                 "non-recursive/1/recursive/2/d,"
	                 "non-recursive/1/recursive/c,"
	                 "non-recursive/a");
	g_free (content);
}

static void
test_non_recursive_in_recursive (TrackerMinerFSTestFixture *fixture,
                                 gconstpointer              data)
{
	gchar *content;

	CREATE_FOLDER (fixture, "recursive");
	CREATE_FOLDER (fixture, "recursive/1");
	CREATE_FOLDER (fixture, "recursive/1/non-recursive");
	CREATE_FOLDER (fixture, "recursive/1/non-recursive/2");
	CREATE_UPDATE_FILE (fixture, "recursive/a");
	CREATE_UPDATE_FILE (fixture, "recursive/1/b");
	CREATE_UPDATE_FILE (fixture, "recursive/1/non-recursive/c");
	CREATE_UPDATE_FILE (fixture, "recursive/1/non-recursive/2/d");

	fixture_add_indexed_folder (fixture, "recursive",
	                            TRACKER_DIRECTORY_FLAG_CHECK_MTIME |
	                            TRACKER_DIRECTORY_FLAG_RECURSE);
	fixture_add_indexed_folder (fixture, "recursive/1/non-recursive",
	                            TRACKER_DIRECTORY_FLAG_CHECK_MTIME);

	tracker_miner_start (TRACKER_MINER (fixture->miner));

	fixture_iterate (fixture);

	content = fixture_get_content (fixture);
	g_assert_cmpstr (content, ==,
	                 "recursive,"
	                 "recursive/1,"
	                 "recursive/1/b,"
	                 "recursive/1/non-recursive,"
	                 "recursive/1/non-recursive/2,"
	                 "recursive/1/non-recursive/c,"
	                 "recursive/a");
	g_free (content);
}

static void
test_empty_root (TrackerMinerFSTestFixture *fixture,
                 gconstpointer              data)
{
	gchar *content;

	CREATE_FOLDER (fixture, "empty");

	fixture_add_indexed_folder (fixture, "empty",
	                            TRACKER_DIRECTORY_FLAG_CHECK_MTIME |
	                            TRACKER_DIRECTORY_FLAG_RECURSE);

	tracker_miner_start (TRACKER_MINER (fixture->miner));

	fixture_iterate (fixture);

	content = fixture_get_content (fixture);
	g_assert_cmpstr (content, ==, "empty");
	g_free (content);
}

static void
test_missing_root (TrackerMinerFSTestFixture *fixture,
                   gconstpointer              data)
{
	gchar *content;

	fixture_add_indexed_folder (fixture, "missing",
	                            TRACKER_DIRECTORY_FLAG_CHECK_MTIME |
	                            TRACKER_DIRECTORY_FLAG_RECURSE);

	tracker_miner_start (TRACKER_MINER (fixture->miner));

	fixture_iterate (fixture);

	content = fixture_get_content (fixture);
	g_assert_cmpstr (content, ==, "");
	g_free (content);
}

static void
test_skip_hidden_files (TrackerMinerFSTestFixture *fixture,
                        gconstpointer              data)
{
	TrackerIndexingTree *indexing_tree;
	gchar *content;

	CREATE_FOLDER (fixture, "recursive");
	CREATE_FOLDER (fixture, "recursive/1");
	CREATE_FOLDER (fixture, "recursive/1/.hidden");
	CREATE_FOLDER (fixture, "recursive/1/.hidden/2");
	CREATE_UPDATE_FILE (fixture, "recursive/.hidden-file");
	CREATE_UPDATE_FILE (fixture, "recursive/1/.hidden/2/a");

	indexing_tree = tracker_miner_fs_get_indexing_tree (fixture->miner);
	tracker_indexing_tree_set_filter_hidden (indexing_tree, TRUE);

	fixture_add_indexed_folder (fixture, "recursive",
	                            TRACKER_DIRECTORY_FLAG_CHECK_MTIME |
	                            TRACKER_DIRECTORY_FLAG_RECURSE);

	tracker_miner_start (TRACKER_MINER (fixture->miner));

	fixture_iterate (fixture);

	content = fixture_get_content (fixture);
	g_assert_cmpstr (content, ==,
	                 "recursive,"
	                 "recursive/1");
	g_free (content);
}

static void
test_index_hidden_files (TrackerMinerFSTestFixture *fixture,
                         gconstpointer              data)
{
	gchar *content;

	CREATE_FOLDER (fixture, "recursive");
	CREATE_FOLDER (fixture, "recursive/1");
	CREATE_FOLDER (fixture, "recursive/1/.hidden");
	CREATE_FOLDER (fixture, "recursive/1/.hidden/2");
	CREATE_UPDATE_FILE (fixture, "recursive/.hidden-file");
	CREATE_UPDATE_FILE (fixture, "recursive/1/.hidden/2/a");

	fixture_add_indexed_folder (fixture, "recursive",
	                            TRACKER_DIRECTORY_FLAG_CHECK_MTIME |
	                            TRACKER_DIRECTORY_FLAG_RECURSE);

	tracker_miner_start (TRACKER_MINER (fixture->miner));

	fixture_iterate (fixture);

	content = fixture_get_content (fixture);
	g_assert_cmpstr (content, ==,
	                 "recursive,"
	                 "recursive/.hidden-file,"
	                 "recursive/1,"
	                 "recursive/1/.hidden,"
	                 "recursive/1/.hidden/2,"
	                 "recursive/1/.hidden/2/a");
	g_free (content);
}

static void
test_file_filter_default_accept (TrackerMinerFSTestFixture *fixture,
                                 gconstpointer              data)
{
	TrackerIndexingTree *indexing_tree;
	gchar *content;

	CREATE_FOLDER (fixture, "recursive");
	CREATE_FOLDER (fixture, "recursive/aa");
	CREATE_FOLDER (fixture, "recursive/bb");
	CREATE_UPDATE_FILE (fixture, "recursive/aa/a1");
	CREATE_UPDATE_FILE (fixture, "recursive/aa/b2");
	CREATE_UPDATE_FILE (fixture, "recursive/bb/ab");
	CREATE_UPDATE_FILE (fixture, "recursive/bb/bb");

	indexing_tree = tracker_miner_fs_get_indexing_tree (fixture->miner);
	tracker_indexing_tree_set_default_policy (indexing_tree,
	                                          TRACKER_FILTER_FILE,
	                                          TRACKER_FILTER_POLICY_ACCEPT);
	tracker_indexing_tree_add_filter (indexing_tree, TRACKER_FILTER_FILE, "a*");

	fixture_add_indexed_folder (fixture, "recursive",
	                            TRACKER_DIRECTORY_FLAG_CHECK_MTIME |
	                            TRACKER_DIRECTORY_FLAG_RECURSE);

	tracker_miner_start (TRACKER_MINER (fixture->miner));

	fixture_iterate (fixture);

	content = fixture_get_content (fixture);
	g_assert_cmpstr (content, ==,
	                 "recursive,"
	                 "recursive/aa,"
	                 "recursive/aa/b2,"
	                 "recursive/bb,"
	                 "recursive/bb/bb");
	g_free (content);
}

static void
test_file_filter_default_deny (TrackerMinerFSTestFixture *fixture,
                               gconstpointer              data)
{
	TrackerIndexingTree *indexing_tree;
	gchar *content;

	CREATE_FOLDER (fixture, "recursive");
	CREATE_FOLDER (fixture, "recursive/aa");
	CREATE_FOLDER (fixture, "recursive/bb");
	CREATE_UPDATE_FILE (fixture, "recursive/aa/a1");
	CREATE_UPDATE_FILE (fixture, "recursive/aa/b2");
	CREATE_UPDATE_FILE (fixture, "recursive/bb/ab");
	CREATE_UPDATE_FILE (fixture, "recursive/bb/bb");

	indexing_tree = tracker_miner_fs_get_indexing_tree (fixture->miner);
	tracker_indexing_tree_set_default_policy (indexing_tree,
	                                          TRACKER_FILTER_FILE,
	                                          TRACKER_FILTER_POLICY_DENY);
	tracker_indexing_tree_add_filter (indexing_tree, TRACKER_FILTER_FILE, "a*");

	fixture_add_indexed_folder (fixture, "recursive",
	                            TRACKER_DIRECTORY_FLAG_CHECK_MTIME |
	                            TRACKER_DIRECTORY_FLAG_RECURSE);

	tracker_miner_start (TRACKER_MINER (fixture->miner));

	fixture_iterate (fixture);

	content = fixture_get_content (fixture);
	g_assert_cmpstr (content, ==,
	                 "recursive,"
	                 "recursive/aa,"
	                 "recursive/aa/a1,"
	                 "recursive/bb,"
	                 "recursive/bb/ab");
	g_free (content);
}

static void
test_directory_filter_default_accept (TrackerMinerFSTestFixture *fixture,
                                      gconstpointer              data)
{
	TrackerIndexingTree *indexing_tree;
	gchar *content;

	CREATE_FOLDER (fixture, "recursive");
	CREATE_FOLDER (fixture, "recursive/aa");
	CREATE_FOLDER (fixture, "recursive/bb");
	CREATE_UPDATE_FILE (fixture, "recursive/aa/a1");
	CREATE_UPDATE_FILE (fixture, "recursive/aa/b2");
	CREATE_UPDATE_FILE (fixture, "recursive/bb/ab");
	CREATE_UPDATE_FILE (fixture, "recursive/bb/bb");

	indexing_tree = tracker_miner_fs_get_indexing_tree (fixture->miner);
	tracker_indexing_tree_set_default_policy (indexing_tree,
	                                          TRACKER_FILTER_DIRECTORY,
	                                          TRACKER_FILTER_POLICY_ACCEPT);
	tracker_indexing_tree_add_filter (indexing_tree, TRACKER_FILTER_DIRECTORY, "a*");

	fixture_add_indexed_folder (fixture, "recursive",
	                            TRACKER_DIRECTORY_FLAG_CHECK_MTIME |
	                            TRACKER_DIRECTORY_FLAG_RECURSE);

	tracker_miner_start (TRACKER_MINER (fixture->miner));

	fixture_iterate (fixture);

	content = fixture_get_content (fixture);
	g_assert_cmpstr (content, ==,
	                 "recursive,"
	                 "recursive/bb,"
	                 "recursive/bb/ab,"
	                 "recursive/bb/bb");
	g_free (content);
}

static void
test_directory_filter_default_deny (TrackerMinerFSTestFixture *fixture,
                                      gconstpointer              data)
{
	TrackerIndexingTree *indexing_tree;
	gchar *content;

	CREATE_FOLDER (fixture, "recursive");
	CREATE_FOLDER (fixture, "recursive/aa");
	CREATE_FOLDER (fixture, "recursive/bb");
	CREATE_UPDATE_FILE (fixture, "recursive/aa/a1");
	CREATE_UPDATE_FILE (fixture, "recursive/aa/b2");
	CREATE_UPDATE_FILE (fixture, "recursive/bb/ab");
	CREATE_UPDATE_FILE (fixture, "recursive/bb/bb");

	indexing_tree = tracker_miner_fs_get_indexing_tree (fixture->miner);
	tracker_indexing_tree_set_default_policy (indexing_tree,
	                                          TRACKER_FILTER_DIRECTORY,
	                                          TRACKER_FILTER_POLICY_DENY);
	tracker_indexing_tree_add_filter (indexing_tree, TRACKER_FILTER_DIRECTORY, "a*");

	/* Guess what, the folder gets filtered otherwise */
	tracker_indexing_tree_add_filter (indexing_tree, TRACKER_FILTER_DIRECTORY, "recursive");

	fixture_add_indexed_folder (fixture, "recursive",
	                            TRACKER_DIRECTORY_FLAG_CHECK_MTIME |
	                            TRACKER_DIRECTORY_FLAG_RECURSE);

	tracker_miner_start (TRACKER_MINER (fixture->miner));

	fixture_iterate (fixture);

	content = fixture_get_content (fixture);
	g_assert_cmpstr (content, ==,
	                 "recursive,"
	                 "recursive/aa,"
	                 "recursive/aa/a1,"
	                 "recursive/aa/b2");
	g_free (content);
}

static void
test_content_filter_default_accept (TrackerMinerFSTestFixture *fixture,
                                    gconstpointer              data)
{
	TrackerIndexingTree *indexing_tree;
	gchar *content;

	CREATE_FOLDER (fixture, "recursive");
	CREATE_FOLDER (fixture, "recursive/aa");
	CREATE_FOLDER (fixture, "recursive/bb");
	CREATE_UPDATE_FILE (fixture, "recursive/aa/a1");
	CREATE_UPDATE_FILE (fixture, "recursive/aa/b2");
	CREATE_UPDATE_FILE (fixture, "recursive/aa/ignore");
	CREATE_UPDATE_FILE (fixture, "recursive/bb/ab");
	CREATE_UPDATE_FILE (fixture, "recursive/bb/bb");

	indexing_tree = tracker_miner_fs_get_indexing_tree (fixture->miner);
	tracker_indexing_tree_set_default_policy (indexing_tree,
	                                          TRACKER_FILTER_PARENT_DIRECTORY,
	                                          TRACKER_FILTER_POLICY_ACCEPT);
	tracker_indexing_tree_add_filter (indexing_tree,
	                                  TRACKER_FILTER_PARENT_DIRECTORY, "ignore");

	fixture_add_indexed_folder (fixture, "recursive",
	                            TRACKER_DIRECTORY_FLAG_CHECK_MTIME |
	                            TRACKER_DIRECTORY_FLAG_RECURSE);

	tracker_miner_start (TRACKER_MINER (fixture->miner));

	fixture_iterate (fixture);

	content = fixture_get_content (fixture);
	g_assert_cmpstr (content, ==,
	                 "recursive,"
	                 "recursive/aa,"
	                 "recursive/bb,"
	                 "recursive/bb/ab,"
	                 "recursive/bb/bb");
	g_free (content);
}

static void
test_content_filter_default_deny (TrackerMinerFSTestFixture *fixture,
                                  gconstpointer              data)
{
	TrackerIndexingTree *indexing_tree;
	gchar *content;

	CREATE_FOLDER (fixture, "recursive");
	CREATE_FOLDER (fixture, "recursive/aa");
	CREATE_FOLDER (fixture, "recursive/bb");
	CREATE_UPDATE_FILE (fixture, "recursive/allow");
	CREATE_UPDATE_FILE (fixture, "recursive/aa/a1");
	CREATE_UPDATE_FILE (fixture, "recursive/aa/b2");
	CREATE_UPDATE_FILE (fixture, "recursive/aa/allow");
	CREATE_UPDATE_FILE (fixture, "recursive/bb/ab");
	CREATE_UPDATE_FILE (fixture, "recursive/bb/bb");

	indexing_tree = tracker_miner_fs_get_indexing_tree (fixture->miner);
	tracker_indexing_tree_set_default_policy (indexing_tree,
	                                          TRACKER_FILTER_PARENT_DIRECTORY,
	                                          TRACKER_FILTER_POLICY_DENY);
	tracker_indexing_tree_add_filter (indexing_tree,
	                                  TRACKER_FILTER_PARENT_DIRECTORY, "allow");

	fixture_add_indexed_folder (fixture, "recursive",
	                            TRACKER_DIRECTORY_FLAG_CHECK_MTIME |
	                            TRACKER_DIRECTORY_FLAG_RECURSE);

	tracker_miner_start (TRACKER_MINER (fixture->miner));

	fixture_iterate (fixture);

	content = fixture_get_content (fixture);
	g_assert_cmpstr (content, ==,
	                 "recursive,"
	                 "recursive/aa,"
	                 "recursive/aa/a1,"
	                 "recursive/aa/allow,"
	                 "recursive/aa/b2,"
	                 "recursive/allow,"
	                 "recursive/bb");
	g_free (content);
}

static void
test_content_filter_on_parent_root (TrackerMinerFSTestFixture *fixture,
				    gconstpointer              data)
{
	TrackerIndexingTree *indexing_tree;
	gchar *content;

	CREATE_FOLDER (fixture, "non-recursive");
	CREATE_FOLDER (fixture, "non-recursive/recursive");
	CREATE_FOLDER (fixture, "non-recursive/recursive/a");
	CREATE_UPDATE_FILE (fixture, "non-recursive/.ignore");
	CREATE_UPDATE_FILE (fixture, "non-recursive/recursive/c");
	CREATE_UPDATE_FILE (fixture, "non-recursive/recursive/a/d");

	indexing_tree = tracker_miner_fs_get_indexing_tree (fixture->miner);
	tracker_indexing_tree_set_filter_hidden (indexing_tree, TRUE);
	tracker_indexing_tree_add_filter (indexing_tree,
	                                  TRACKER_FILTER_PARENT_DIRECTORY, ".ignore");

	fixture_add_indexed_folder (fixture, "non-recursive",
	                            TRACKER_DIRECTORY_FLAG_PRESERVE |
	                            TRACKER_DIRECTORY_FLAG_CHECK_DELETED |
	                            TRACKER_DIRECTORY_FLAG_CHECK_MTIME);
	fixture_add_indexed_folder (fixture, "non-recursive/recursive",
	                            TRACKER_DIRECTORY_FLAG_PRESERVE |
	                            TRACKER_DIRECTORY_FLAG_CHECK_DELETED |
	                            TRACKER_DIRECTORY_FLAG_CHECK_MTIME |
	                            TRACKER_DIRECTORY_FLAG_RECURSE);

	tracker_miner_start (TRACKER_MINER (fixture->miner));

	fixture_iterate (fixture);

	content = fixture_get_content (fixture);
	g_assert_cmpstr (content, ==,
	                 "non-recursive,"
	                 "non-recursive/recursive,"
			 "non-recursive/recursive/a,"
			 "non-recursive/recursive/a/d,"
			 "non-recursive/recursive/c");
	g_free (content);

	/* Check it is ok after the content is already indexed. All
	 * files should stay and no events should be generated as
	 * there's no changes.
	 */
	fixture_remove_indexed_folder (fixture, "non-recursive");
	fixture_remove_indexed_folder (fixture, "non-recursive/recursive");

	fixture_add_indexed_folder (fixture, "non-recursive",
	                            TRACKER_DIRECTORY_FLAG_PRESERVE |
	                            TRACKER_DIRECTORY_FLAG_CHECK_DELETED |
	                            TRACKER_DIRECTORY_FLAG_CHECK_MTIME);
	fixture_add_indexed_folder (fixture, "non-recursive/recursive",
	                            TRACKER_DIRECTORY_FLAG_PRESERVE |
	                            TRACKER_DIRECTORY_FLAG_CHECK_DELETED |
	                            TRACKER_DIRECTORY_FLAG_CHECK_MTIME |
	                            TRACKER_DIRECTORY_FLAG_RECURSE);

	test_miner_reset_counters ((TestMiner *) fixture->miner);
	fixture_iterate (fixture);

	content = fixture_get_content (fixture);
	g_assert_cmpstr (content, ==,
	                 "non-recursive,"
	                 "non-recursive/recursive,"
			 "non-recursive/recursive/a,"
			 "non-recursive/recursive/a/d,"
			 "non-recursive/recursive/c");
	g_free (content);

	g_assert_cmpint (((TestMiner *) fixture->miner)->n_events, ==, 0);
}

static void
test_non_monitored_create (TrackerMinerFSTestFixture *fixture,
                           gconstpointer              data)
{
	gchar *content;

	CREATE_FOLDER (fixture, "recursive");
	CREATE_UPDATE_FILE (fixture, "recursive/a");

	fixture_add_indexed_folder (fixture, "recursive",
	                            TRACKER_DIRECTORY_FLAG_PRESERVE |
	                            TRACKER_DIRECTORY_FLAG_CHECK_MTIME |
	                            TRACKER_DIRECTORY_FLAG_RECURSE);

	tracker_miner_start (TRACKER_MINER (fixture->miner));

	fixture_iterate (fixture);

	content = fixture_get_content (fixture);
	g_assert_cmpstr (content, ==,
	                 "recursive,"
	                 "recursive/a");
	g_free (content);

	fixture_remove_indexed_folder (fixture, "recursive");

	CREATE_FOLDER (fixture, "recursive/new");
	CREATE_UPDATE_FILE (fixture, "recursive/b");
	CREATE_UPDATE_FILE (fixture, "recursive/new/c");

	fixture_add_indexed_folder (fixture, "recursive",
	                            TRACKER_DIRECTORY_FLAG_CHECK_MTIME |
	                            TRACKER_DIRECTORY_FLAG_RECURSE);

	fixture_iterate (fixture);

	content = fixture_get_content (fixture);
	g_assert_cmpstr (content, ==,
	                 "recursive,"
	                 "recursive/a,"
	                 "recursive/b,"
	                 "recursive/new,"
	                 "recursive/new/c");
	g_free (content);
}

static void
test_non_monitored_update (TrackerMinerFSTestFixture *fixture,
                           gconstpointer              data)
{
	gchar *content;

	CREATE_FOLDER (fixture, "recursive");
	CREATE_UPDATE_FILE (fixture, "recursive/a");

	fixture_add_indexed_folder (fixture, "recursive",
	                            TRACKER_DIRECTORY_FLAG_PRESERVE |
	                            TRACKER_DIRECTORY_FLAG_CHECK_MTIME |
	                            TRACKER_DIRECTORY_FLAG_RECURSE);

	tracker_miner_start (TRACKER_MINER (fixture->miner));

	fixture_iterate (fixture);

	content = fixture_get_content (fixture);
	g_assert_cmpstr (content, ==,
	                 "recursive,"
	                 "recursive/a");
	g_free (content);

	fixture_remove_indexed_folder (fixture, "recursive");
	test_miner_reset_counters ((TestMiner *) fixture->miner);

	/* Ensure mtime will really change */
	sleep (1);

	UPDATE_FILE_ATOMIC (fixture, "recursive/a", "b");

	fixture_add_indexed_folder (fixture, "recursive",
	                            TRACKER_DIRECTORY_FLAG_CHECK_MTIME |
	                            TRACKER_DIRECTORY_FLAG_RECURSE);

	fixture_iterate (fixture);

	g_assert_cmpint (((TestMiner *) fixture->miner)->n_process_file, >=, 1);

	content = fixture_get_content (fixture);
	g_assert_cmpstr (content, ==,
	                 "recursive,"
	                 "recursive/a");
	g_free (content);
}

static void
test_non_monitored_delete (TrackerMinerFSTestFixture *fixture,
                           gconstpointer              data)
{
	gchar *content;

	CREATE_FOLDER (fixture, "recursive");
	CREATE_FOLDER (fixture, "recursive/1");
	CREATE_FOLDER (fixture, "recursive/1/2");
	CREATE_FOLDER (fixture, "recursive/1/2/3");
	CREATE_UPDATE_FILE (fixture, "recursive/a");
	CREATE_UPDATE_FILE (fixture, "recursive/1/2/b");
	CREATE_UPDATE_FILE (fixture, "recursive/1/2/3/c");

	fixture_add_indexed_folder (fixture, "recursive",
	                            TRACKER_DIRECTORY_FLAG_PRESERVE |
	                            TRACKER_DIRECTORY_FLAG_CHECK_MTIME |
	                            TRACKER_DIRECTORY_FLAG_CHECK_DELETED |
	                            TRACKER_DIRECTORY_FLAG_RECURSE);

	tracker_miner_start (TRACKER_MINER (fixture->miner));

	fixture_iterate (fixture);

	content = fixture_get_content (fixture);
	g_assert_cmpstr (content, ==,
	                 "recursive,"
	                 "recursive/1,"
	                 "recursive/1/2,"
	                 "recursive/1/2/3,"
	                 "recursive/1/2/3/c,"
	                 "recursive/1/2/b,"
	                 "recursive/a");
	g_free (content);

	fixture_remove_indexed_folder (fixture, "recursive");

	DELETE_FILE (fixture, "recursive/a");
	DELETE_FOLDER (fixture, "recursive/1/2");

	fixture_add_indexed_folder (fixture, "recursive",
	                            TRACKER_DIRECTORY_FLAG_CHECK_MTIME |
	                            TRACKER_DIRECTORY_FLAG_CHECK_DELETED |
	                            TRACKER_DIRECTORY_FLAG_RECURSE);

	fixture_iterate (fixture);

	content = fixture_get_content (fixture);
	g_assert_cmpstr (content, ==,
	                 "recursive,"
	                 "recursive/1");
	g_free (content);
}

static void
test_non_monitored_move (TrackerMinerFSTestFixture *fixture,
                         gconstpointer              data)
{
	gchar *content;

	CREATE_FOLDER (fixture, "recursive");
	CREATE_FOLDER (fixture, "recursive/1");
	CREATE_FOLDER (fixture, "non-recursive");
	CREATE_FOLDER (fixture, "non-recursive/2");
	CREATE_FOLDER (fixture, "not-indexed/");
	CREATE_UPDATE_FILE (fixture, "recursive/a");
	CREATE_UPDATE_FILE (fixture, "recursive/1/b");
	CREATE_UPDATE_FILE (fixture, "non-recursive/2/c");
	CREATE_UPDATE_FILE (fixture, "recursive/d");

	fixture_add_indexed_folder (fixture, "recursive",
	                            TRACKER_DIRECTORY_FLAG_PRESERVE |
	                            TRACKER_DIRECTORY_FLAG_CHECK_MTIME |
	                            TRACKER_DIRECTORY_FLAG_CHECK_DELETED |
	                            TRACKER_DIRECTORY_FLAG_RECURSE);
	fixture_add_indexed_folder (fixture, "non-recursive",
	                            TRACKER_DIRECTORY_FLAG_PRESERVE |
	                            TRACKER_DIRECTORY_FLAG_CHECK_MTIME |
	                            TRACKER_DIRECTORY_FLAG_CHECK_DELETED);

	tracker_miner_start (TRACKER_MINER (fixture->miner));

	fixture_iterate (fixture);

	content = fixture_get_content (fixture);
	g_assert_cmpstr (content, ==,
	                 "non-recursive,"
	                 "non-recursive/2,"
	                 "recursive,"
	                 "recursive/1,"
	                 "recursive/1/b,"
	                 "recursive/a,"
	                 "recursive/d");
	g_free (content);

	fixture_remove_indexed_folder (fixture, "recursive");
	fixture_remove_indexed_folder (fixture, "non-recursive");

	MOVE_FILE (fixture, "recursive/a", "non-recursive/e");
	MOVE_FILE (fixture, "recursive/1", "non-recursive/3");
	MOVE_FILE (fixture, "non-recursive/2", "recursive/4");
	MOVE_FILE (fixture, "recursive/d", "not-indexed/f");

	fixture_add_indexed_folder (fixture, "recursive",
	                            TRACKER_DIRECTORY_FLAG_CHECK_MTIME |
	                            TRACKER_DIRECTORY_FLAG_CHECK_DELETED |
	                            TRACKER_DIRECTORY_FLAG_RECURSE);
	fixture_add_indexed_folder (fixture, "non-recursive",
	                            TRACKER_DIRECTORY_FLAG_CHECK_MTIME |
	                            TRACKER_DIRECTORY_FLAG_CHECK_DELETED);

	fixture_iterate (fixture);

	content = fixture_get_content (fixture);
	g_assert_cmpstr (content, ==,
	                 "non-recursive,"
	                 "non-recursive/3,"
	                 "non-recursive/e,"
	                 "recursive,"
	                 "recursive/4,"
	                 "recursive/4/c");
	g_free (content);
}

static void
test_monitored_create (TrackerMinerFSTestFixture *fixture,
                       gconstpointer              data)
{
	gchar *content;

	CREATE_FOLDER (fixture, "recursive");
	CREATE_UPDATE_FILE (fixture, "recursive/a");

	fixture_add_indexed_folder (fixture, "recursive",
	                            TRACKER_DIRECTORY_FLAG_MONITOR |
	                            TRACKER_DIRECTORY_FLAG_CHECK_MTIME |
	                            TRACKER_DIRECTORY_FLAG_RECURSE);

	tracker_miner_start (TRACKER_MINER (fixture->miner));

	fixture_iterate (fixture);

	content = fixture_get_content (fixture);
	g_assert_cmpstr (content, ==,
	                 "recursive,"
	                 "recursive/a");
	g_free (content);

	CREATE_FOLDER (fixture, "recursive/new");
	CREATE_UPDATE_FILE (fixture, "recursive/b");
	CREATE_UPDATE_FILE (fixture, "recursive/new/c");

	fixture_iterate (fixture);

	content = fixture_get_content (fixture);
	g_assert_cmpstr (content, ==,
	                 "recursive,"
	                 "recursive/a,"
	                 "recursive/b,"
	                 "recursive/new,"
	                 "recursive/new/c");
	g_free (content);
}

static void
test_monitored_update (TrackerMinerFSTestFixture *fixture,
                       gconstpointer              data)
{
	gchar *content;

	CREATE_FOLDER (fixture, "recursive");
	CREATE_UPDATE_FILE (fixture, "recursive/a");

	fixture_add_indexed_folder (fixture, "recursive",
	                            TRACKER_DIRECTORY_FLAG_MONITOR |
	                            TRACKER_DIRECTORY_FLAG_CHECK_MTIME |
	                            TRACKER_DIRECTORY_FLAG_RECURSE);

	tracker_miner_start (TRACKER_MINER (fixture->miner));

	fixture_iterate (fixture);

	content = fixture_get_content (fixture);
	g_assert_cmpstr (content, ==,
	                 "recursive,"
	                 "recursive/a");
	g_free (content);

	test_miner_reset_counters ((TestMiner *) fixture->miner);

	UPDATE_FILE_ATOMIC (fixture, "recursive/a", "b");

	fixture_iterate (fixture);

	g_assert_cmpint (((TestMiner *) fixture->miner)->n_process_file, ==, 1);

	content = fixture_get_content (fixture);
	g_assert_cmpstr (content, ==,
	                 "recursive,"
	                 "recursive/a");
	g_free (content);
}

static void
test_monitored_delete (TrackerMinerFSTestFixture *fixture,
                       gconstpointer              data)
{
	gchar *content;
	gint n_tries = 0;

	CREATE_FOLDER (fixture, "recursive");
	CREATE_FOLDER (fixture, "recursive/1");
	CREATE_FOLDER (fixture, "recursive/1/2");
	CREATE_FOLDER (fixture, "recursive/1/2/3");
	CREATE_UPDATE_FILE (fixture, "recursive/a");
	CREATE_UPDATE_FILE (fixture, "recursive/1/2/b");
	CREATE_UPDATE_FILE (fixture, "recursive/1/2/3/c");

	fixture_add_indexed_folder (fixture, "recursive",
	                            TRACKER_DIRECTORY_FLAG_MONITOR |
	                            TRACKER_DIRECTORY_FLAG_CHECK_MTIME |
	                            TRACKER_DIRECTORY_FLAG_CHECK_DELETED |
	                            TRACKER_DIRECTORY_FLAG_RECURSE);

	tracker_miner_start (TRACKER_MINER (fixture->miner));

	fixture_iterate (fixture);

	content = fixture_get_content (fixture);
	g_assert_cmpstr (content, ==,
	                 "recursive,"
	                 "recursive/1,"
	                 "recursive/1/2,"
	                 "recursive/1/2/3,"
	                 "recursive/1/2/3/c,"
	                 "recursive/1/2/b,"
	                 "recursive/a");
	g_free (content);

	DELETE_FOLDER (fixture, "recursive/1/2");

	/* This may take several ::finished callbacks, never
	 * more than the number of files deleted, possibly less
	 * due to coalescing.
	 */
	while (fixture_query_exists (fixture, "recursive/1/2")) {
		g_assert_cmpint (n_tries, <, 3);
		fixture_iterate (fixture);
		n_tries++;
	}

	content = fixture_get_content (fixture);
	g_assert_cmpstr (content, ==,
	                 "recursive,"
	                 "recursive/1,"
	                 "recursive/a");
	g_free (content);

	DELETE_FILE (fixture, "recursive/a");

	fixture_iterate (fixture);

	content = fixture_get_content (fixture);
	g_assert_cmpstr (content, ==,
	                 "recursive,"
	                 "recursive/1");
	g_free (content);
}

static void
test_monitored_move (TrackerMinerFSTestFixture *fixture,
                     gconstpointer              data)
{
	gchar *content;
	gint n_tries = 0;

	CREATE_FOLDER (fixture, "recursive");
	CREATE_FOLDER (fixture, "recursive/1");
	CREATE_FOLDER (fixture, "non-recursive");
	CREATE_FOLDER (fixture, "non-recursive/2");
	CREATE_FOLDER (fixture, "not-indexed/");
	CREATE_UPDATE_FILE (fixture, "recursive/a");
	CREATE_UPDATE_FILE (fixture, "recursive/1/b");
	CREATE_UPDATE_FILE (fixture, "non-recursive/2/c");
	CREATE_UPDATE_FILE (fixture, "recursive/d");

	fixture_add_indexed_folder (fixture, "recursive",
	                            TRACKER_DIRECTORY_FLAG_MONITOR |
	                            TRACKER_DIRECTORY_FLAG_CHECK_MTIME |
	                            TRACKER_DIRECTORY_FLAG_CHECK_DELETED |
	                            TRACKER_DIRECTORY_FLAG_RECURSE);
	fixture_add_indexed_folder (fixture, "non-recursive",
	                            TRACKER_DIRECTORY_FLAG_MONITOR |
	                            TRACKER_DIRECTORY_FLAG_CHECK_MTIME |
	                            TRACKER_DIRECTORY_FLAG_CHECK_DELETED);

	tracker_miner_start (TRACKER_MINER (fixture->miner));

	fixture_iterate (fixture);

	content = fixture_get_content (fixture);
	g_assert_cmpstr (content, ==,
	                 "non-recursive,"
	                 "non-recursive/2,"
	                 "recursive,"
	                 "recursive/1,"
	                 "recursive/1/b,"
	                 "recursive/a,"
	                 "recursive/d");
	g_free (content);

	MOVE_FILE (fixture, "recursive/a", "non-recursive/e");
	MOVE_FILE (fixture, "recursive/1", "non-recursive/3");
	MOVE_FILE (fixture, "recursive/d", "not-indexed/f");
	MOVE_FILE (fixture, "non-recursive/2", "recursive/4");

	while (!fixture_query_exists (fixture, "recursive/4/c")) {
		g_assert_cmpint (n_tries, <, 4);
		fixture_iterate (fixture);
		n_tries++;
	}

	content = fixture_get_content (fixture);
	g_assert_cmpstr (content, ==,
	                 "non-recursive,"
	                 "non-recursive/3,"
	                 "non-recursive/e,"
	                 "recursive,"
	                 "recursive/4,"
	                 "recursive/4/c");
	g_free (content);
}

static void
test_monitored_atomic_replace (TrackerMinerFSTestFixture *fixture,
                               gconstpointer              data)
{
	gchar *content;

	CREATE_FOLDER (fixture, "recursive");
	CREATE_UPDATE_FILE (fixture, "recursive/a");

	fixture_add_indexed_folder (fixture, "recursive",
	                            TRACKER_DIRECTORY_FLAG_MONITOR |
	                            TRACKER_DIRECTORY_FLAG_CHECK_MTIME |
	                            TRACKER_DIRECTORY_FLAG_CHECK_DELETED |
	                            TRACKER_DIRECTORY_FLAG_RECURSE);

	tracker_miner_start (TRACKER_MINER (fixture->miner));

	fixture_iterate (fixture);

	content = fixture_get_content (fixture);
	g_assert_cmpstr (content, ==,
	                 "recursive,"
	                 "recursive/a");
	g_free (content);

	UPDATE_FILE_ATOMIC (fixture, "recursive/a", "recursive/b");

	fixture_iterate (fixture);

	content = fixture_get_content (fixture);
	g_assert_cmpstr (content, ==,
	                 "recursive,"
	                 "recursive/a");
	g_free (content);
}

static void
test_changes_after_no_mtime_check (TrackerMinerFSTestFixture *fixture,
                                   gconstpointer              data)
{
	gchar *content;

	CREATE_FOLDER (fixture, "recursive");
	CREATE_FOLDER (fixture, "recursive/1");
	CREATE_FOLDER (fixture, "recursive/1/2");
	CREATE_UPDATE_FILE (fixture, "recursive/a");
	CREATE_UPDATE_FILE (fixture, "recursive/1/2/b");

	fixture_add_indexed_folder (fixture, "recursive",
	                            TRACKER_DIRECTORY_FLAG_PRESERVE |
	                            TRACKER_DIRECTORY_FLAG_CHECK_MTIME |
	                            TRACKER_DIRECTORY_FLAG_CHECK_DELETED |
	                            TRACKER_DIRECTORY_FLAG_RECURSE);

	tracker_miner_start (TRACKER_MINER (fixture->miner));

	fixture_iterate (fixture);

	content = fixture_get_content (fixture);
	g_assert_cmpstr (content, ==,
	                 "recursive,"
	                 "recursive/1,"
	                 "recursive/1/2,"
	                 "recursive/1/2/b,"
	                 "recursive/a");
	g_free (content);

	fixture_remove_indexed_folder (fixture, "recursive");

	CREATE_FOLDER (fixture, "recursive/1/3");
	CREATE_UPDATE_FILE (fixture, "recursive/c");
	DELETE_FOLDER (fixture, "recursive/1/2");

	fixture_add_indexed_folder (fixture, "recursive",
	                            TRACKER_DIRECTORY_FLAG_MONITOR |
	                            TRACKER_DIRECTORY_FLAG_RECURSE);
	fixture_iterate (fixture);

	content = fixture_get_content (fixture);
	g_assert_cmpstr (content, ==,
	                 "recursive,"
	                 "recursive/1,"
	                 "recursive/1/2,"
	                 "recursive/1/2/b,"
	                 "recursive/a");
	g_free (content);

	CREATE_UPDATE_FILE (fixture, "recursive/1/3/c");
	CREATE_UPDATE_FILE (fixture, "recursive/1/2");
	CREATE_UPDATE_FILE (fixture, "recursive/c");

	while (!fixture_query_exists (fixture, "recursive/c"))
		fixture_iterate (fixture);

	content = fixture_get_content (fixture);
	g_assert_cmpstr (content, ==,
	                 "recursive,"
	                 "recursive/1,"
	                 "recursive/1/2,"
	                 "recursive/1/2/b," /* FIXME: this file should disappear */
	                 "recursive/1/3,"
	                 "recursive/1/3/c,"
	                 "recursive/a,"
	                 "recursive/c");
	g_free (content);
}

static void
test_event_queue_create_and_update (TrackerMinerFSTestFixture *fixture,
                                    gconstpointer              data)
{
	gchar *content;

	CREATE_FOLDER (fixture, "recursive");

	fixture_add_indexed_folder (fixture, "recursive",
	                            TRACKER_DIRECTORY_FLAG_MONITOR |
	                            TRACKER_DIRECTORY_FLAG_CHECK_MTIME |
	                            TRACKER_DIRECTORY_FLAG_RECURSE);

	tracker_miner_start (TRACKER_MINER (fixture->miner));

	fixture_iterate (fixture);

	content = fixture_get_content (fixture);
	g_assert_cmpstr (content, ==, "recursive");
	g_free (content);

	tracker_miner_pause (TRACKER_MINER (fixture->miner));

	CREATE_UPDATE_FILE (fixture, "recursive/a");
	fixture_iterate_filter (fixture, 1);
	UPDATE_FILE_ATOMIC (fixture, "recursive/a", "recursive/b");
	fixture_iterate_filter (fixture, 1);

	content = fixture_get_content (fixture);
	g_assert_cmpstr (content, ==, "recursive");
	g_free (content);

	tracker_miner_resume (TRACKER_MINER (fixture->miner));

	g_assert_cmpint (((TestMiner *) fixture->miner)->n_process_file, ==, 1);
	fixture_iterate (fixture);

	content = fixture_get_content (fixture);
	g_assert_cmpstr (content, ==,
	                 "recursive,"
	                 "recursive/a");
	g_free (content);
}

static void
test_event_queue_create_and_delete (TrackerMinerFSTestFixture *fixture,
                                    gconstpointer              data)
{
	gchar *content;

	CREATE_FOLDER (fixture, "recursive");

	fixture_add_indexed_folder (fixture, "recursive",
	                            TRACKER_DIRECTORY_FLAG_MONITOR |
	                            TRACKER_DIRECTORY_FLAG_CHECK_MTIME |
	                            TRACKER_DIRECTORY_FLAG_RECURSE);

	tracker_miner_start (TRACKER_MINER (fixture->miner));

	fixture_iterate (fixture);

	content = fixture_get_content (fixture);
	g_assert_cmpstr (content, ==, "recursive");
	g_free (content);

	tracker_miner_pause (TRACKER_MINER (fixture->miner));

	CREATE_UPDATE_FILE (fixture, "recursive/a");
	fixture_iterate_filter (fixture, 1);
	g_assert (tracker_miner_fs_has_items_to_process (fixture->miner) == TRUE);

	DELETE_FILE (fixture, "recursive/a");
	fixture_iterate_filter (fixture, 1);

	tracker_miner_resume (TRACKER_MINER (fixture->miner));
	fixture_iterate (fixture);
	g_assert (tracker_miner_fs_has_items_to_process (fixture->miner) == FALSE);
}

static void
test_event_queue_create_and_move (TrackerMinerFSTestFixture *fixture,
                                  gconstpointer              data)
{
	gchar *content;

	CREATE_FOLDER (fixture, "recursive");

	fixture_add_indexed_folder (fixture, "recursive",
	                            TRACKER_DIRECTORY_FLAG_MONITOR |
	                            TRACKER_DIRECTORY_FLAG_CHECK_MTIME |
	                            TRACKER_DIRECTORY_FLAG_RECURSE);

	tracker_miner_start (TRACKER_MINER (fixture->miner));

	fixture_iterate (fixture);

	content = fixture_get_content (fixture);
	g_assert_cmpstr (content, ==, "recursive");
	g_free (content);

	tracker_miner_pause (TRACKER_MINER (fixture->miner));

	CREATE_UPDATE_FILE (fixture, "recursive/a");
	fixture_iterate_filter (fixture, 1);
	MOVE_FILE (fixture, "recursive/a", "recursive/b");
	fixture_iterate_filter (fixture, 1);

	content = fixture_get_content (fixture);
	g_assert_cmpstr (content, ==, "recursive");
	g_free (content);

	tracker_miner_resume (TRACKER_MINER (fixture->miner));

	fixture_iterate (fixture);

	content = fixture_get_content (fixture);
	g_assert_cmpstr (content, ==,
	                 "recursive,"
	                 "recursive/b");
	g_free (content);
}

static void
test_event_queue_update_and_update (TrackerMinerFSTestFixture *fixture,
                                    gconstpointer              data)
{
	gchar *content;

	CREATE_FOLDER (fixture, "recursive");
	CREATE_UPDATE_FILE (fixture, "recursive/a");

	fixture_add_indexed_folder (fixture, "recursive",
	                            TRACKER_DIRECTORY_FLAG_MONITOR |
	                            TRACKER_DIRECTORY_FLAG_CHECK_MTIME |
	                            TRACKER_DIRECTORY_FLAG_RECURSE);

	tracker_miner_start (TRACKER_MINER (fixture->miner));

	fixture_iterate (fixture);

	content = fixture_get_content (fixture);
	g_assert_cmpstr (content, ==,
	                 "recursive,"
	                 "recursive/a");
	g_free (content);

	tracker_miner_pause (TRACKER_MINER (fixture->miner));
	test_miner_reset_counters ((TestMiner *) fixture->miner);

	UPDATE_FILE_ATOMIC (fixture, "recursive/a", "b");
	fixture_iterate_filter (fixture, 1);
	CREATE_UPDATE_FILE (fixture, "b");
	MOVE_FILE (fixture, "b", "recursive/a");
	fixture_iterate_filter (fixture, 1);

	content = fixture_get_content (fixture);
	g_assert_cmpstr (content, ==,
	                 "recursive,"
	                 "recursive/a");
	g_free (content);

	tracker_miner_resume (TRACKER_MINER (fixture->miner));

	fixture_iterate (fixture);
	/* Coalescing desirable, but not mandatory */
	g_assert_cmpint (((TestMiner *) fixture->miner)->n_process_file, >=, 1);

	content = fixture_get_content (fixture);
	g_assert_cmpstr (content, ==,
	                 "recursive,"
	                 "recursive/a");
	g_free (content);
}

static void
test_event_queue_update_and_delete (TrackerMinerFSTestFixture *fixture,
                                    gconstpointer              data)
{
	gchar *content;

	CREATE_FOLDER (fixture, "recursive");
	CREATE_UPDATE_FILE (fixture, "recursive/a");

	fixture_add_indexed_folder (fixture, "recursive",
	                            TRACKER_DIRECTORY_FLAG_MONITOR |
	                            TRACKER_DIRECTORY_FLAG_CHECK_MTIME |
	                            TRACKER_DIRECTORY_FLAG_RECURSE);

	tracker_miner_start (TRACKER_MINER (fixture->miner));

	fixture_iterate (fixture);

	content = fixture_get_content (fixture);
	g_assert_cmpstr (content, ==,
	                 "recursive,"
	                 "recursive/a");
	g_free (content);

	tracker_miner_pause (TRACKER_MINER (fixture->miner));

	UPDATE_FILE_ATOMIC (fixture, "recursive/a", "b");
	fixture_iterate_filter (fixture, 1);
	DELETE_FILE (fixture, "recursive/a");
	fixture_iterate_filter (fixture, 1);

	content = fixture_get_content (fixture);
	g_assert_cmpstr (content, ==,
	                 "recursive,"
	                 "recursive/a");
	g_free (content);

	tracker_miner_resume (TRACKER_MINER (fixture->miner));

	fixture_iterate (fixture);

	content = fixture_get_content (fixture);
	g_assert_cmpstr (content, ==, "recursive");
	g_free (content);
}

static void
test_event_queue_update_and_move (TrackerMinerFSTestFixture *fixture,
                                  gconstpointer              data)
{
	gchar *content;

	CREATE_FOLDER (fixture, "recursive");
	CREATE_UPDATE_FILE (fixture, "recursive/a");

	fixture_add_indexed_folder (fixture, "recursive",
	                            TRACKER_DIRECTORY_FLAG_MONITOR |
	                            TRACKER_DIRECTORY_FLAG_CHECK_MTIME |
	                            TRACKER_DIRECTORY_FLAG_RECURSE);

	tracker_miner_start (TRACKER_MINER (fixture->miner));

	fixture_iterate (fixture);

	content = fixture_get_content (fixture);
	g_assert_cmpstr (content, ==,
	                 "recursive,"
	                 "recursive/a");
	g_free (content);

	tracker_miner_pause (TRACKER_MINER (fixture->miner));

	DELETE_FILE (fixture, "recursive/a");
	CREATE_UPDATE_FILE (fixture, "recursive/a");
	fixture_iterate_filter (fixture, 1);
	MOVE_FILE (fixture, "recursive/a", "recursive/b");
	fixture_iterate_filter (fixture, 1);

	content = fixture_get_content (fixture);
	g_assert_cmpstr (content, ==,
	                 "recursive,"
	                 "recursive/a");
	g_free (content);

	tracker_miner_resume (TRACKER_MINER (fixture->miner));

	fixture_iterate (fixture);

	content = fixture_get_content (fixture);
	g_assert_cmpstr (content, ==,
	                 "recursive,"
	                 "recursive/b");
	g_free (content);
}

static void
test_event_queue_delete_and_create (TrackerMinerFSTestFixture *fixture,
                                    gconstpointer              data)
{
	gchar *content;

	CREATE_FOLDER (fixture, "recursive");
	CREATE_UPDATE_FILE (fixture, "recursive/a");

	fixture_add_indexed_folder (fixture, "recursive",
	                            TRACKER_DIRECTORY_FLAG_MONITOR |
	                            TRACKER_DIRECTORY_FLAG_CHECK_MTIME |
	                            TRACKER_DIRECTORY_FLAG_RECURSE);

	tracker_miner_start (TRACKER_MINER (fixture->miner));

	fixture_iterate (fixture);

	content = fixture_get_content (fixture);
	g_assert_cmpstr (content, ==,
	                 "recursive,"
	                 "recursive/a");
	g_free (content);

	tracker_miner_pause (TRACKER_MINER (fixture->miner));

	DELETE_FILE (fixture, "recursive/a");
	fixture_iterate_filter (fixture, 1);
	CREATE_UPDATE_FILE (fixture, "recursive/a");
	fixture_iterate_filter (fixture, 1);

	content = fixture_get_content (fixture);
	g_assert_cmpstr (content, ==,
	                 "recursive,"
	                 "recursive/a");
	g_free (content);

	tracker_miner_resume (TRACKER_MINER (fixture->miner));

	fixture_iterate (fixture);

	content = fixture_get_content (fixture);
	g_assert_cmpstr (content, ==,
	                 "recursive,"
	                 "recursive/a");
	g_free (content);
}

static void
test_event_queue_move_and_update (TrackerMinerFSTestFixture *fixture,
                                  gconstpointer              data)
{
	gchar *content;

	CREATE_FOLDER (fixture, "recursive");
	CREATE_UPDATE_FILE (fixture, "recursive/a");

	fixture_add_indexed_folder (fixture, "recursive",
	                            TRACKER_DIRECTORY_FLAG_MONITOR |
	                            TRACKER_DIRECTORY_FLAG_CHECK_MTIME |
	                            TRACKER_DIRECTORY_FLAG_RECURSE);

	tracker_miner_start (TRACKER_MINER (fixture->miner));

	fixture_iterate (fixture);

	content = fixture_get_content (fixture);
	g_assert_cmpstr (content, ==,
	                 "recursive,"
	                 "recursive/a");
	g_free (content);

	tracker_miner_pause (TRACKER_MINER (fixture->miner));

	test_miner_reset_counters ((TestMiner *) fixture->miner);

	MOVE_FILE (fixture, "recursive/a", "recursive/b");
	fixture_iterate_filter (fixture, 1);
	UPDATE_FILE_ATOMIC (fixture, "recursive/b", "c");
	fixture_iterate_filter (fixture, 1);

	content = fixture_get_content (fixture);
	g_assert_cmpstr (content, ==,
	                 "recursive,"
	                 "recursive/a");
	g_free (content);

	tracker_miner_resume (TRACKER_MINER (fixture->miner));

	fixture_iterate (fixture);
	g_assert_cmpint (((TestMiner *) fixture->miner)->n_process_file, ==, 1);

	content = fixture_get_content (fixture);
	g_assert_cmpstr (content, ==,
	                 "recursive,"
	                 "recursive/b");
	g_free (content);
}

static void
test_event_queue_move_and_create_origin (TrackerMinerFSTestFixture *fixture,
                                         gconstpointer              data)
{
	gchar *content;

	CREATE_FOLDER (fixture, "recursive");
	CREATE_UPDATE_FILE (fixture, "recursive/a");

	fixture_add_indexed_folder (fixture, "recursive",
	                            TRACKER_DIRECTORY_FLAG_MONITOR |
	                            TRACKER_DIRECTORY_FLAG_CHECK_MTIME |
	                            TRACKER_DIRECTORY_FLAG_RECURSE);

	tracker_miner_start (TRACKER_MINER (fixture->miner));

	fixture_iterate (fixture);

	content = fixture_get_content (fixture);
	g_assert_cmpstr (content, ==,
	                 "recursive,"
	                 "recursive/a");
	g_free (content);

	tracker_miner_pause (TRACKER_MINER (fixture->miner));

	MOVE_FILE (fixture, "recursive/a", "recursive/b");
	fixture_iterate_filter (fixture, 1);
	CREATE_UPDATE_FILE (fixture, "recursive/a");
	fixture_iterate_filter (fixture, 1);

	content = fixture_get_content (fixture);
	g_assert_cmpstr (content, ==,
	                 "recursive,"
	                 "recursive/a");
	g_free (content);

	tracker_miner_resume (TRACKER_MINER (fixture->miner));

	fixture_iterate (fixture);

	content = fixture_get_content (fixture);
	g_assert_cmpstr (content, ==,
	                 "recursive,"
	                 "recursive/a,"
	                 "recursive/b");
	g_free (content);
}

static void
test_event_queue_move_and_delete (TrackerMinerFSTestFixture *fixture,
                                  gconstpointer              data)
{
	gchar *content;

	CREATE_FOLDER (fixture, "recursive");
	CREATE_UPDATE_FILE (fixture, "recursive/a");

	fixture_add_indexed_folder (fixture, "recursive",
	                            TRACKER_DIRECTORY_FLAG_MONITOR |
	                            TRACKER_DIRECTORY_FLAG_CHECK_MTIME |
	                            TRACKER_DIRECTORY_FLAG_RECURSE);

	tracker_miner_start (TRACKER_MINER (fixture->miner));

	fixture_iterate (fixture);

	content = fixture_get_content (fixture);
	g_assert_cmpstr (content, ==,
	                 "recursive,"
	                 "recursive/a");
	g_free (content);

	tracker_miner_pause (TRACKER_MINER (fixture->miner));

	MOVE_FILE (fixture, "recursive/a", "recursive/b");
	fixture_iterate_filter (fixture, 1);
	DELETE_FILE (fixture, "recursive/b");
	fixture_iterate_filter (fixture, 1);

	content = fixture_get_content (fixture);
	g_assert_cmpstr (content, ==,
	                 "recursive,"
	                 "recursive/a");
	g_free (content);

	tracker_miner_resume (TRACKER_MINER (fixture->miner));

	fixture_iterate (fixture);

	content = fixture_get_content (fixture);
	g_assert_cmpstr (content, ==,
	                 "recursive");
	g_free (content);
}

static void
test_event_queue_move_and_move (TrackerMinerFSTestFixture *fixture,
                                gconstpointer              data)
{
	gchar *content;

	CREATE_FOLDER (fixture, "recursive");
	CREATE_UPDATE_FILE (fixture, "recursive/a");

	fixture_add_indexed_folder (fixture, "recursive",
	                            TRACKER_DIRECTORY_FLAG_MONITOR |
	                            TRACKER_DIRECTORY_FLAG_CHECK_MTIME |
	                            TRACKER_DIRECTORY_FLAG_RECURSE);

	tracker_miner_start (TRACKER_MINER (fixture->miner));

	fixture_iterate (fixture);

	content = fixture_get_content (fixture);
	g_assert_cmpstr (content, ==,
	                 "recursive,"
	                 "recursive/a");
	g_free (content);

	tracker_miner_pause (TRACKER_MINER (fixture->miner));

	MOVE_FILE (fixture, "recursive/a", "recursive/b");
	fixture_iterate_filter (fixture, 1);
	MOVE_FILE (fixture, "recursive/b", "recursive/c");
	fixture_iterate_filter (fixture, 1);

	content = fixture_get_content (fixture);
	g_assert_cmpstr (content, ==,
	                 "recursive,"
	                 "recursive/a");
	g_free (content);

	tracker_miner_resume (TRACKER_MINER (fixture->miner));

	fixture_iterate (fixture);

	content = fixture_get_content (fixture);
	g_assert_cmpstr (content, ==,
	                 "recursive,"
	                 "recursive/c");
	g_free (content);
}

static void
test_event_queue_move_and_move_back (TrackerMinerFSTestFixture *fixture,
                                     gconstpointer              data)
{
	gchar *content;

	CREATE_FOLDER (fixture, "recursive");
	CREATE_UPDATE_FILE (fixture, "recursive/a");

	fixture_add_indexed_folder (fixture, "recursive",
	                            TRACKER_DIRECTORY_FLAG_MONITOR |
	                            TRACKER_DIRECTORY_FLAG_CHECK_MTIME |
	                            TRACKER_DIRECTORY_FLAG_RECURSE);

	tracker_miner_start (TRACKER_MINER (fixture->miner));

	fixture_iterate (fixture);

	content = fixture_get_content (fixture);
	g_assert_cmpstr (content, ==,
	                 "recursive,"
	                 "recursive/a");
	g_free (content);

	tracker_miner_pause (TRACKER_MINER (fixture->miner));

	MOVE_FILE (fixture, "recursive/a", "recursive/b");
	fixture_iterate_filter (fixture, 1);
	MOVE_FILE (fixture, "recursive/b", "recursive/a");
	fixture_iterate_filter (fixture, 1);

	content = fixture_get_content (fixture);
	g_assert_cmpstr (content, ==,
	                 "recursive,"
	                 "recursive/a");
	g_free (content);

	tracker_miner_resume (TRACKER_MINER (fixture->miner));

	fixture_iterate (fixture);

	content = fixture_get_content (fixture);
	g_assert_cmpstr (content, ==,
	                 "recursive,"
	                 "recursive/a");
	g_free (content);
}

static void
test_api_check_file (TrackerMinerFSTestFixture *fixture,
                     gconstpointer              data)
{
	GFile *file;
	gchar *content;

	CREATE_FOLDER (fixture, "recursive");
	CREATE_FOLDER (fixture, "not-indexed");
	CREATE_UPDATE_FILE (fixture, "recursive/a");
	CREATE_UPDATE_FILE (fixture, "not-indexed/b");
	CREATE_UPDATE_FILE (fixture, "not-indexed/c");

	fixture_add_indexed_folder (fixture, "recursive",
	                            TRACKER_DIRECTORY_FLAG_MONITOR |
	                            TRACKER_DIRECTORY_FLAG_CHECK_MTIME |
	                            TRACKER_DIRECTORY_FLAG_RECURSE);

	tracker_miner_start (TRACKER_MINER (fixture->miner));

	fixture_iterate (fixture);

	content = fixture_get_content (fixture);
	g_assert_cmpstr (content, ==,
	                 "recursive,"
	                 "recursive/a");
	g_free (content);

	test_miner_reset_counters ((TestMiner *) fixture->miner);

	file = fixture_get_relative_file (fixture, "recursive/a");
	tracker_miner_fs_check_file (fixture->miner, file, G_PRIORITY_DEFAULT, FALSE);
	g_object_unref (file);

	file = fixture_get_relative_file (fixture, "not-indexed/b");
	tracker_miner_fs_check_file (fixture->miner, file, G_PRIORITY_DEFAULT, FALSE);
	g_object_unref (file);

	file = fixture_get_relative_file (fixture, "not-indexed/c");
	tracker_miner_fs_check_file (fixture->miner, file, G_PRIORITY_DEFAULT, TRUE);
	g_object_unref (file);

	fixture_iterate (fixture);

	g_assert_cmpint (((TestMiner *) fixture->miner)->n_process_file, ==, 2);

	content = fixture_get_content (fixture);
	g_assert_cmpstr (content, ==,
	                 "not-indexed/b,"
	                 "recursive,"
	                 "recursive/a");
	g_free (content);
}

gint
main (gint    argc,
      gchar **argv)
{
	g_test_init (&argc, &argv, NULL);

	g_test_message ("Testing filesystem miner");

	ADD_TEST ("/indexing-tree/recursive-indexing",
	          test_recursive_indexing);
	ADD_TEST ("/indexing-tree/non-recursive-indexing",
	          test_non_recursive_indexing);
	/* FIXME: test other directory flags */
	ADD_TEST ("/indexing-tree/separate-recursive-and-non-recursive",
	          test_separate_recursive_and_non_recursive);
	ADD_TEST ("/indexing-tree/recursive-in-non-recursive",
	          test_recursive_in_non_recursive);
	ADD_TEST ("/indexing-tree/non-recursive-in-recursive",
	          test_non_recursive_in_recursive);
	ADD_TEST ("/indexing-tree/empty-root",
	          test_empty_root);
	ADD_TEST ("/indexing-tree/missing-root",
	          test_missing_root);
	ADD_TEST ("/indexing-tree/skip-hidden-files",
	          test_skip_hidden_files);
	ADD_TEST ("/indexing-tree/index-hidden-files",
	          test_index_hidden_files);
	ADD_TEST ("/indexing-tree/file-filter-default-accept",
	          test_file_filter_default_accept);
	ADD_TEST ("/indexing-tree/file-filter-default-deny",
	          test_file_filter_default_deny);
	ADD_TEST ("/indexing-tree/directory-filter-default-accept",
	          test_directory_filter_default_accept);
	ADD_TEST ("/indexing-tree/directory-filter-default-deny",
	          test_directory_filter_default_deny);
	ADD_TEST ("/indexing-tree/content-filter-default-accept",
	          test_content_filter_default_accept);
	ADD_TEST ("/indexing-tree/content-filter-default-deny",
	          test_content_filter_default_deny);
	ADD_TEST ("/indexing-tree/content-filter-on-parent-root",
		  test_content_filter_on_parent_root);

	/* Tests for non-monitored FS changes (eg. between reindexes) */
	ADD_TEST ("/non-monitored/create",
	          test_non_monitored_create);
	ADD_TEST ("/non-monitored/update",
	          test_non_monitored_update);
	ADD_TEST ("/non-monitored/delete",
	          test_non_monitored_delete);
	ADD_TEST ("/non-monitored/move",
	          test_non_monitored_move);

	/* Tests for monitored FS changes (from file monitors) */
	ADD_TEST ("/monitored/create",
	          test_monitored_create);
	ADD_TEST ("/monitored/update",
	          test_monitored_update);
	ADD_TEST ("/monitored/delete",
	          test_monitored_delete);
	ADD_TEST ("/monitored/move",
	          test_monitored_move);
	ADD_TEST ("/monitored/atomic-replace",
	          test_monitored_atomic_replace);
	ADD_TEST ("monitored/changes-after-no-mtime-check",
	          test_changes_after_no_mtime_check);

	/* Tests for event queues */
	ADD_TEST ("event-queue/create-and-update",
	          test_event_queue_create_and_update);
	ADD_TEST ("event-queue/create-and-delete",
	          test_event_queue_create_and_delete);
	ADD_TEST ("event-queue/create-and-move",
	          test_event_queue_create_and_move);
	ADD_TEST ("event-queue/update-and-update",
	          test_event_queue_update_and_update);
	ADD_TEST ("event-queue/update-and-delete",
	          test_event_queue_update_and_delete);
	ADD_TEST ("event-queue/update-and-move",
	          test_event_queue_update_and_move);
	ADD_TEST ("event-queue/delete-and-create",
	          test_event_queue_delete_and_create);
	ADD_TEST ("event-queue/move-and-update",
	          test_event_queue_move_and_update);
	ADD_TEST ("event-queue/move-and-create-origin",
	          test_event_queue_move_and_create_origin);
	ADD_TEST ("event-queue/move-and-delete",
	          test_event_queue_move_and_delete);
	ADD_TEST ("event-queue/move-and-move",
	          test_event_queue_move_and_move);
	ADD_TEST ("event-queue/move-and-move-back",
	          test_event_queue_move_and_move_back);

	/* API tests */
	ADD_TEST ("api/check_file",
	          test_api_check_file);

	return g_test_run ();
}
