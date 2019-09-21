/*
 * Copyright (C) 2009, Nokia <ivan.frade@nokia.com>
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

#include <libtracker-common/tracker-common.h>

#include "tracker-crawler.h"
#include "tracker-miner-fs.h"
#include "tracker-monitor.h"
#include "tracker-utils.h"
#include "tracker-priority-queue.h"
#include "tracker-task-pool.h"
#include "tracker-sparql-buffer.h"
#include "tracker-file-notifier.h"

/* If defined will print push/pop actions on queues */
#ifdef EVENT_QUEUE_ENABLE_TRACE
#warning Event Queue traces enabled
#define EVENT_QUEUE_LOG_PREFIX "[Event Queues] "
#define EVENT_QUEUE_STATUS_TIMEOUT_SECS 30
#define trace_eq(message, ...) g_message (EVENT_QUEUE_LOG_PREFIX message, ##__VA_ARGS__)
#define trace_eq_event(event) \
	do { \
		const gchar *event_type_name[] = { "CREATED", "UPDATED", "DELETED", "MOVED" }; \
		gchar *uri1 = g_file_get_uri (event->file); \
		gchar *uri2 = event->dest_file ? g_file_get_uri (event->dest_file) : NULL; \
		g_message ("%s New %s event: %s%s%s%s", \
		           EVENT_QUEUE_LOG_PREFIX, \
		           event_type_name[event->type], \
		           event->attributes_update ? "(attributes only) " : "", \
		           uri1, \
		           uri2 ? "->" : "", \
		           uri2 ? uri2 : ""); \
		g_free (uri1); \
		g_free (uri2); \
	} while (0)
#else
#define trace_eq(...)
#define trace_eq_event(...)
#endif /* EVENT_QUEUE_ENABLE_TRACE */

/* Default processing pool limits to be set */
#define DEFAULT_WAIT_POOL_LIMIT 1
#define DEFAULT_READY_POOL_LIMIT 1

/* Put tasks processing at a lower priority so other events
 * (timeouts, monitor events, etc...) are guaranteed to be
 * dispatched promptly.
 */
#define TRACKER_TASK_PRIORITY G_PRIORITY_DEFAULT_IDLE + 10

#define MAX_SIMULTANEOUS_ITEMS 64

/**
 * SECTION:tracker-miner-fs
 * @short_description: Abstract base class for filesystem miners
 * @include: libtracker-miner/tracker-miner.h
 *
 * #TrackerMinerFS is an abstract base class for miners that collect data
 * from a filesystem where parent/child relationships need to be
 * inserted into the database correctly with queue management.
 *
 * All the filesystem crawling and monitoring is abstracted away,
 * leaving to implementations the decisions of what directories/files
 * should it process, and the actual data extraction.
 *
 * Example creating a TrackerMinerFS with our own file system root and
 * data provider.
 *
 * First create our class and base it on TrackerMinerFS:
 * |[
 * G_DEFINE_TYPE_WITH_CODE (MyMinerFiles, my_miner_files, TRACKER_TYPE_MINER_FS,
 *                          G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE,
 *                                                 my_miner_files_initable_iface_init))
 * ]|
 *
 * Later in our class creation function, we are supplying the
 * arguments we want. In this case, the 'root' is a #GFile pointing to
 * a root URI location (for example 'file:///') and 'data_provider' is a
 * #TrackerDataProvider used to enumerate 'root' and return children it
 * finds. If 'data_provider' is %NULL (the default), then a
 * #TrackerFileDataProvider is created automatically.
 * |[
 * // Note that only 'name' is mandatory
 * miner = g_initable_new (MY_TYPE_MINER_FILES,
 *                         NULL,
 *                         error,
 *                         "name", "MyMinerFiles",
 *                         "root", root,
 *                         "data-provider", data_provider,
 *                         "processing-pool-wait-limit", 10,
 *                         "processing-pool-ready-limit", 100,
 *                         NULL);
 * ]|
 **/

typedef struct {
	guint16 type;
	guint attributes_update : 1;
	GFile *file;
	GFile *dest_file;
} QueueEvent;

typedef struct {
	GFile *file;
	gchar *urn;
	gint priority;
	GCancellable *cancellable;
	TrackerMiner *miner;
} UpdateProcessingTaskContext;

struct _TrackerMinerFSPrivate {
	TrackerPriorityQueue *items;

	guint item_queues_handler_id;
	GFile *item_queue_blocker;

	/* Root / tree / index */
	GFile *root;
	TrackerIndexingTree *indexing_tree;
	TrackerFileNotifier *file_notifier;
	TrackerDataProvider *data_provider;

	/* Sparql insertion tasks */
	TrackerTaskPool *task_pool;
	TrackerSparqlBuffer *sparql_buffer;
	guint sparql_buffer_limit;

	/* File properties */
	GQuark quark_recursive_removal;

	/* Properties */
	gdouble throttle;

	/* Status */
	GTimer *timer;
	GTimer *extraction_timer;

	guint been_started : 1;     /* TRUE if miner has been started */
	guint been_crawled : 1;     /* TRUE if initial crawling has been
	                             * done */
	guint shown_totals : 1;     /* TRUE if totals have been shown */
	guint is_paused : 1;        /* TRUE if miner is paused */

	guint timer_stopped : 1;    /* TRUE if main timer is stopped */
	guint extraction_timer_stopped : 1; /* TRUE if the extraction
	                                     * timer is stopped */

	GHashTable *roots_to_notify;        /* Used to signal indexing
	                                     * trees finished */

	/*
	 * Statistics
	 */

	/* How many we found during crawling and how many were black
	 * listed (ignored). Reset to 0 when processing stops. */
	guint total_directories_found;
	guint total_directories_ignored;
	guint total_files_found;
	guint total_files_ignored;

	/* How many we indexed and how many had errors indexing. */
	guint total_files_processed;
	guint total_files_notified;
	guint total_files_notified_error;
};

typedef enum {
	QUEUE_NONE,
	QUEUE_CREATED,
	QUEUE_UPDATED,
	QUEUE_DELETED,
	QUEUE_MOVED,
	QUEUE_WAIT,
} QueueState;

typedef enum {
	QUEUE_ACTION_NONE           = 0,
	QUEUE_ACTION_DELETE_FIRST   = 1 << 0,
	QUEUE_ACTION_DELETE_SECOND  = 1 << 1,
} QueueCoalesceAction;

enum {
	PROCESS_FILE,
	PROCESS_FILE_ATTRIBUTES,
	FINISHED,
	FINISHED_ROOT,
	REMOVE_FILE,
	REMOVE_CHILDREN,
	MOVE_FILE,
	LAST_SIGNAL
};

enum {
	PROP_0,
	PROP_THROTTLE,
	PROP_ROOT,
	PROP_WAIT_POOL_LIMIT,
	PROP_READY_POOL_LIMIT,
	PROP_DATA_PROVIDER
};

static void           miner_fs_initable_iface_init        (GInitableIface       *iface);

static void           fs_finalize                         (GObject              *object);
static void           fs_constructed                      (GObject              *object);
static void           fs_set_property                     (GObject              *object,
                                                           guint                 prop_id,
                                                           const GValue         *value,
                                                           GParamSpec           *pspec);
static void           fs_get_property                     (GObject              *object,
                                                           guint                 prop_id,
                                                           GValue               *value,
                                                           GParamSpec           *pspec);

static void           miner_started                       (TrackerMiner         *miner);
static void           miner_stopped                       (TrackerMiner         *miner);
static void           miner_paused                        (TrackerMiner         *miner);
static void           miner_resumed                       (TrackerMiner         *miner);

static void           indexing_tree_directory_removed     (TrackerIndexingTree  *indexing_tree,
                                                           GFile                *directory,
                                                           gpointer              user_data);
static void           file_notifier_file_created          (TrackerFileNotifier  *notifier,
                                                           GFile                *file,
                                                           gpointer              user_data);
static void           file_notifier_file_deleted          (TrackerFileNotifier  *notifier,
                                                           GFile                *file,
                                                           gpointer              user_data);
static void           file_notifier_file_updated          (TrackerFileNotifier  *notifier,
                                                           GFile                *file,
                                                           gboolean              attributes_only,
                                                           gpointer              user_data);
static void           file_notifier_file_moved            (TrackerFileNotifier  *notifier,
                                                           GFile                *source,
                                                           GFile                *dest,
                                                           gpointer              user_data);
static void           file_notifier_directory_started     (TrackerFileNotifier *notifier,
                                                           GFile               *directory,
                                                           gpointer             user_data);
static void           file_notifier_directory_finished    (TrackerFileNotifier *notifier,
                                                           GFile               *directory,
                                                           guint                directories_found,
                                                           guint                directories_ignored,
                                                           guint                files_found,
                                                           guint                files_ignored,
                                                           gpointer             user_data);
static void           file_notifier_finished              (TrackerFileNotifier *notifier,
                                                           gpointer             user_data);

static void           item_queue_handlers_set_up          (TrackerMinerFS       *fs);

static void           task_pool_cancel_foreach                (gpointer        data,
                                                               gpointer        user_data);
static void           task_pool_limit_reached_notify_cb       (GObject        *object,
                                                               GParamSpec     *pspec,
                                                               gpointer        user_data);

static void           miner_fs_queue_event                (TrackerMinerFS *fs,
							   QueueEvent     *event,
							   guint           priority);

static GQuark quark_last_queue_event = 0;
static GInitableIface* miner_fs_initable_parent_iface;
static guint signals[LAST_SIGNAL] = { 0, };

/**
 * tracker_miner_fs_error_quark:
 *
 * Gives the caller the #GQuark used to identify #TrackerMinerFS errors
 * in #GError structures. The #GQuark is used as the domain for the error.
 *
 * Returns: the #GQuark used for the domain of a #GError.
 *
 * Since: 1.2
 **/
G_DEFINE_QUARK (TrackerMinerFSError, tracker_miner_fs_error)

G_DEFINE_ABSTRACT_TYPE_WITH_CODE (TrackerMinerFS, tracker_miner_fs, TRACKER_TYPE_MINER,
                                  G_ADD_PRIVATE (TrackerMinerFS)
                                  G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE,
                                                         miner_fs_initable_iface_init));

static void
tracker_miner_fs_class_init (TrackerMinerFSClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	TrackerMinerClass *miner_class = TRACKER_MINER_CLASS (klass);

	object_class->finalize = fs_finalize;
	object_class->constructed = fs_constructed;
	object_class->set_property = fs_set_property;
	object_class->get_property = fs_get_property;

	miner_class->started = miner_started;
	miner_class->stopped = miner_stopped;
	miner_class->paused  = miner_paused;
	miner_class->resumed = miner_resumed;

	g_object_class_install_property (object_class,
	                                 PROP_THROTTLE,
	                                 g_param_spec_double ("throttle",
	                                                      "Throttle",
	                                                      "Modifier for the indexing speed, 0 is max speed",
	                                                      0, 1, 0,
	                                                      G_PARAM_READWRITE));
	g_object_class_install_property (object_class,
	                                 PROP_ROOT,
	                                 g_param_spec_object ("root",
	                                                      "Root",
	                                                      "Top level URI for our indexing tree and file notify clases",
	                                                      G_TYPE_FILE,
	                                                      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	g_object_class_install_property (object_class,
	                                 PROP_WAIT_POOL_LIMIT,
	                                 g_param_spec_uint ("processing-pool-wait-limit",
	                                                    "Processing pool limit for WAIT tasks",
	                                                    "Maximum number of files that can be concurrently "
	                                                    "processed by the upper layer",
	                                                    1, G_MAXUINT, DEFAULT_WAIT_POOL_LIMIT,
	                                                    G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
	g_object_class_install_property (object_class,
	                                 PROP_READY_POOL_LIMIT,
	                                 g_param_spec_uint ("processing-pool-ready-limit",
	                                                    "Processing pool limit for READY tasks",
	                                                    "Maximum number of SPARQL updates that can be merged "
	                                                    "in a single connection to the store",
	                                                    1, G_MAXUINT, DEFAULT_READY_POOL_LIMIT,
	                                                    G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
	g_object_class_install_property (object_class,
	                                 PROP_DATA_PROVIDER,
	                                 g_param_spec_object ("data-provider",
	                                                      "Data provider",
	                                                      "Data provider populating data, e.g. like GFileEnumerator",
	                                                      TRACKER_TYPE_DATA_PROVIDER,
	                                                      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	/**
	 * TrackerMinerFS::process-file:
	 * @miner_fs: the #TrackerMinerFS
	 * @file: a #GFile
	 * @builder: a #TrackerSparqlBuilder
	 * @cancellable: a #GCancellable
	 *
	 * The ::process-file signal is emitted whenever a file should
	 * be processed, and it's metadata extracted.
	 *
	 * @builder is the #TrackerSparqlBuilder where all sparql updates
	 * to be performed for @file will be appended.
	 *
	 * This signal allows both synchronous and asynchronous extraction,
	 * in the synchronous case @cancellable can be safely ignored. In
	 * either case, on successful metadata extraction, implementations
	 * must call tracker_miner_fs_notify_finish() to indicate that
	 * processing has finished on @file, so the miner can execute
	 * the SPARQL updates and continue processing other files.
	 *
	 * Returns: %TRUE if the file is accepted for processing,
	 *          %FALSE if the file should be ignored.
	 *
	 * Since: 0.8
	 **/
	signals[PROCESS_FILE] =
		g_signal_new ("process-file",
		              G_OBJECT_CLASS_TYPE (object_class),
		              G_SIGNAL_RUN_LAST,
		              G_STRUCT_OFFSET (TrackerMinerFSClass, process_file),
		              NULL, NULL,
		              NULL,
		              G_TYPE_BOOLEAN,
		              2, G_TYPE_FILE, G_TYPE_TASK);

	/**
	 * TrackerMinerFS::process-file-attributes:
	 * @miner_fs: the #TrackerMinerFS
	 * @file: a #GFile
	 * @builder: a #TrackerSparqlBuilder
	 * @cancellable: a #GCancellable
	 *
	 * The ::process-file-attributes signal is emitted whenever a file should
	 * be processed, but only the attribute-related metadata extracted.
	 *
	 * @builder is the #TrackerSparqlBuilder where all sparql updates
	 * to be performed for @file will be appended. For the properties being
	 * updated, the DELETE statements should be included as well.
	 *
	 * This signal allows both synchronous and asynchronous extraction,
	 * in the synchronous case @cancellable can be safely ignored. In
	 * either case, on successful metadata extraction, implementations
	 * must call tracker_miner_fs_notify_finish() to indicate that
	 * processing has finished on @file, so the miner can execute
	 * the SPARQL updates and continue processing other files.
	 *
	 * Returns: %TRUE if the file is accepted for processing,
	 *          %FALSE if the file should be ignored.
	 *
	 * Since: 0.10
	 **/
	signals[PROCESS_FILE_ATTRIBUTES] =
		g_signal_new ("process-file-attributes",
		              G_OBJECT_CLASS_TYPE (object_class),
		              G_SIGNAL_RUN_LAST,
		              G_STRUCT_OFFSET (TrackerMinerFSClass, process_file_attributes),
		              NULL, NULL,
		              NULL,
		              G_TYPE_BOOLEAN,
		              2, G_TYPE_FILE, G_TYPE_TASK);

	/**
	 * TrackerMinerFS::finished:
	 * @miner_fs: the #TrackerMinerFS
	 * @elapsed: elapsed time since mining was started
	 * @directories_found: number of directories found
	 * @directories_ignored: number of ignored directories
	 * @files_found: number of files found
	 * @files_ignored: number of ignored files
	 *
	 * The ::finished signal is emitted when @miner_fs has finished
	 * all pending processing.
	 *
	 * Since: 0.8
	 **/
	signals[FINISHED] =
		g_signal_new ("finished",
		              G_TYPE_FROM_CLASS (object_class),
		              G_SIGNAL_RUN_LAST,
		              G_STRUCT_OFFSET (TrackerMinerFSClass, finished),
		              NULL, NULL,
		              NULL,
		              G_TYPE_NONE,
		              5,
		              G_TYPE_DOUBLE,
		              G_TYPE_UINT,
		              G_TYPE_UINT,
		              G_TYPE_UINT,
		              G_TYPE_UINT);

	/**
	 * TrackerMinerFS::finished-root:
	 * @miner_fs: the #TrackerMinerFS
	 * @file: a #GFile
	 *
	 * The ::finished-crawl signal is emitted when @miner_fs has
	 * finished finding all resources that need to be indexed
	 * with the root location of @file. At this point, it's likely
	 * many are still in the queue to be added to the database,
	 * but this gives some indication that a location is
	 * processed.
	 *
	 * Since: 1.2
	 **/
	signals[FINISHED_ROOT] =
		g_signal_new ("finished-root",
		              G_TYPE_FROM_CLASS (object_class),
		              G_SIGNAL_RUN_LAST,
		              G_STRUCT_OFFSET (TrackerMinerFSClass, finished_root),
		              NULL, NULL,
		              NULL,
		              G_TYPE_NONE,
		              1,
		              G_TYPE_FILE);

	/**
	 * TrackerMinerFS::remove-file:
	 * @miner_fs: the #TrackerMinerFS
	 * @file: a #GFile
	 * @children_only: #TRUE if only the children of @file are to be deleted
	 * @builder: a #TrackerSparqlBuilder
	 *
	 * The ::remove-file signal will be emitted on files that need removal
	 * according to the miner configuration (either the files themselves are
	 * deleted, or the directory/contents no longer need inspection according
	 * to miner configuration and their location.
	 *
	 * This operation is always assumed to be recursive, the @children_only
	 * argument will be %TRUE if for any reason the topmost directory needs
	 * to stay (e.g. moved from a recursively indexed directory tree to a
	 * non-recursively indexed location).
	 *
	 * The @builder argument can be used to provide additional SPARQL
	 * deletes and updates necessary around the deletion of those items. If
	 * the return value of this signal is %TRUE, @builder is expected to
	 * contain all relevant deletes for this operation.
	 *
	 * If the return value of this signal is %FALSE, the miner will apply
	 * its default behavior, which is deleting all triples that correspond
	 * to the affected URIs.
	 *
	 * Returns: %TRUE if @builder contains all the necessary operations to
	 *          delete the affected resources, %FALSE to let the miner
	 *          implicitly handle the deletion.
	 *
	 * Since: 1.8
	 **/
	signals[REMOVE_FILE] =
		g_signal_new ("remove-file",
		              G_TYPE_FROM_CLASS (object_class),
		              G_SIGNAL_RUN_LAST,
		              G_STRUCT_OFFSET (TrackerMinerFSClass, remove_file),
		              NULL, NULL, NULL,
		              G_TYPE_STRING,
		              1, G_TYPE_FILE);

	signals[REMOVE_CHILDREN] =
		g_signal_new ("remove-children",
		              G_TYPE_FROM_CLASS (object_class),
		              G_SIGNAL_RUN_LAST,
		              G_STRUCT_OFFSET (TrackerMinerFSClass, remove_children),
		              NULL, NULL, NULL,
		              G_TYPE_STRING,
		              1, G_TYPE_FILE);

	signals[MOVE_FILE] =
		g_signal_new ("move-file",
		              G_TYPE_FROM_CLASS (object_class),
		              G_SIGNAL_RUN_LAST,
		              G_STRUCT_OFFSET (TrackerMinerFSClass, move_file),
		              NULL, NULL, NULL,
		              G_TYPE_STRING,
		              3, G_TYPE_FILE, G_TYPE_FILE, G_TYPE_BOOLEAN);

	quark_last_queue_event = g_quark_from_static_string ("tracker-last-queue-event");
}

static void
tracker_miner_fs_init (TrackerMinerFS *object)
{
	TrackerMinerFSPrivate *priv;

	object->priv = tracker_miner_fs_get_instance_private (object);

	priv = object->priv;

	priv->timer = g_timer_new ();
	priv->extraction_timer = g_timer_new ();

	g_timer_stop (priv->timer);
	g_timer_stop (priv->extraction_timer);

	priv->timer_stopped = TRUE;
	priv->extraction_timer_stopped = TRUE;

	priv->items = tracker_priority_queue_new ();

	/* Create processing pools */
	priv->task_pool = tracker_task_pool_new (DEFAULT_WAIT_POOL_LIMIT);
	g_signal_connect (priv->task_pool, "notify::limit-reached",
	                  G_CALLBACK (task_pool_limit_reached_notify_cb), object);

	priv->quark_recursive_removal = g_quark_from_static_string ("tracker-recursive-removal");

	priv->roots_to_notify = g_hash_table_new_full (g_file_hash,
	                                               (GEqualFunc) g_file_equal,
	                                               g_object_unref,
	                                               NULL);
}

static gboolean
miner_fs_initable_init (GInitable     *initable,
                        GCancellable  *cancellable,
                        GError       **error)
{
	TrackerMinerFSPrivate *priv;
	guint limit;

	if (!miner_fs_initable_parent_iface->init (initable, cancellable, error)) {
		return FALSE;
	}

	priv = TRACKER_MINER_FS (initable)->priv;

	g_object_get (initable, "processing-pool-ready-limit", &limit, NULL);
	priv->sparql_buffer = tracker_sparql_buffer_new (tracker_miner_get_connection (TRACKER_MINER (initable)),
	                                                 limit);

	if (!priv->sparql_buffer) {
		g_set_error (error,
		             tracker_miner_fs_error_quark (),
		             TRACKER_MINER_FS_ERROR_INIT,
		             "Could not create TrackerSparqlBuffer needed to process resources");
		return FALSE;
	}

	g_signal_connect (priv->sparql_buffer, "notify::limit-reached",
	                  G_CALLBACK (task_pool_limit_reached_notify_cb),
	                  initable);

	if (!priv->indexing_tree) {
		g_set_error (error,
		             tracker_miner_fs_error_quark (),
		             TRACKER_MINER_FS_ERROR_INIT,
		             "Could not create TrackerIndexingTree needed to manage content indexed");
		return FALSE;
	}

	g_signal_connect (priv->indexing_tree, "directory-removed",
	                  G_CALLBACK (indexing_tree_directory_removed),
	                  initable);

	/* Create the file notifier */
	priv->file_notifier = tracker_file_notifier_new (priv->indexing_tree,
	                                                 priv->data_provider,
							 tracker_miner_get_connection (TRACKER_MINER (initable)));

	if (!priv->file_notifier) {
		g_set_error (error,
		             tracker_miner_fs_error_quark (),
		             TRACKER_MINER_FS_ERROR_INIT,
		             "Could not create TrackerFileNotifier needed to signal new resources to be indexed");
		return FALSE;
	}

	g_signal_connect (priv->file_notifier, "file-created",
	                  G_CALLBACK (file_notifier_file_created),
	                  initable);
	g_signal_connect (priv->file_notifier, "file-updated",
	                  G_CALLBACK (file_notifier_file_updated),
	                  initable);
	g_signal_connect (priv->file_notifier, "file-deleted",
	                  G_CALLBACK (file_notifier_file_deleted),
	                  initable);
	g_signal_connect (priv->file_notifier, "file-moved",
	                  G_CALLBACK (file_notifier_file_moved),
	                  initable);
	g_signal_connect (priv->file_notifier, "directory-started",
	                  G_CALLBACK (file_notifier_directory_started),
	                  initable);
	g_signal_connect (priv->file_notifier, "directory-finished",
	                  G_CALLBACK (file_notifier_directory_finished),
	                  initable);
	g_signal_connect (priv->file_notifier, "finished",
	                  G_CALLBACK (file_notifier_finished),
	                  initable);

	return TRUE;
}

static void
miner_fs_initable_iface_init (GInitableIface *iface)
{
	miner_fs_initable_parent_iface = g_type_interface_peek_parent (iface);
	iface->init = miner_fs_initable_init;
}

static QueueEvent *
queue_event_new (TrackerMinerFSEventType  type,
                 GFile                   *file)
{
	QueueEvent *event;

	g_assert (type != TRACKER_MINER_FS_EVENT_MOVED);

	event = g_new0 (QueueEvent, 1);
	event->type = type;
	g_set_object (&event->file, file);

	return event;
}

static QueueEvent *
queue_event_moved_new (GFile *source,
                       GFile *dest)
{
	QueueEvent *event;

	event = g_new0 (QueueEvent, 1);
	event->type = TRACKER_MINER_FS_EVENT_MOVED;
	g_set_object (&event->dest_file, dest);
	g_set_object (&event->file, source);

	return event;
}

static GList *
queue_event_get_last_event_node (QueueEvent *event)
{
	return g_object_get_qdata (G_OBJECT (event->file),
				   quark_last_queue_event);
}

static void
queue_event_save_node (QueueEvent *event,
		       GList      *node)
{
	g_assert (node->data == event);
	g_object_set_qdata (G_OBJECT (event->file),
			    quark_last_queue_event, node);
}

static void
queue_event_dispose_node (QueueEvent *event)
{
	GList *node;

	node = queue_event_get_last_event_node (event);

	if (node && node->data == event) {
		g_object_steal_qdata (G_OBJECT (event->file),
				      quark_last_queue_event);
	}
}

static void
queue_event_free (QueueEvent *event)
{
	queue_event_dispose_node (event);

	g_clear_object (&event->dest_file);
	g_clear_object (&event->file);
	g_free (event);
}

static QueueCoalesceAction
queue_event_coalesce (const QueueEvent  *first,
		      const QueueEvent  *second,
		      QueueEvent       **replacement)
{
	*replacement = NULL;

	if (first->type == TRACKER_MINER_FS_EVENT_CREATED) {
		if ((second->type == TRACKER_MINER_FS_EVENT_UPDATED ||
		     second->type == TRACKER_MINER_FS_EVENT_CREATED) &&
		    first->file == second->file) {
			return QUEUE_ACTION_DELETE_SECOND;
		} else if (second->type == TRACKER_MINER_FS_EVENT_MOVED &&
			   first->file == second->file) {
			*replacement = queue_event_new (TRACKER_MINER_FS_EVENT_CREATED,
							second->dest_file);
			return (QUEUE_ACTION_DELETE_FIRST |
				QUEUE_ACTION_DELETE_SECOND);
		} else if (second->type == TRACKER_MINER_FS_EVENT_DELETED &&
			   first->file == second->file) {
			/* We can't be sure that "create" is replacing a file
			 * here. Preserve the second event just in case.
			 */
			return QUEUE_ACTION_DELETE_FIRST;
		}
	} else if (first->type == TRACKER_MINER_FS_EVENT_UPDATED) {
		if (second->type == TRACKER_MINER_FS_EVENT_UPDATED &&
		    first->file == second->file) {
			if (first->attributes_update && !second->attributes_update)
				return QUEUE_ACTION_DELETE_FIRST;
			else
				return QUEUE_ACTION_DELETE_SECOND;
		} else if (second->type == TRACKER_MINER_FS_EVENT_DELETED &&
			   first->file == second->file) {
			return QUEUE_ACTION_DELETE_FIRST;
		}
	} else if (first->type == TRACKER_MINER_FS_EVENT_MOVED) {
		if (second->type == TRACKER_MINER_FS_EVENT_MOVED &&
		    first->dest_file == second->file) {
			if (first->file != second->dest_file) {
				*replacement = queue_event_moved_new (first->file,
								      second->dest_file);
			}

			return (QUEUE_ACTION_DELETE_FIRST |
				QUEUE_ACTION_DELETE_SECOND);
		} else if (second->type == TRACKER_MINER_FS_EVENT_DELETED &&
			   first->dest_file == second->file) {
			*replacement = queue_event_new (TRACKER_MINER_FS_EVENT_DELETED,
							first->file);
			return (QUEUE_ACTION_DELETE_FIRST |
				QUEUE_ACTION_DELETE_SECOND);
		}
	} else if (first->type == TRACKER_MINER_FS_EVENT_DELETED &&
		   second->type == TRACKER_MINER_FS_EVENT_DELETED) {
		return QUEUE_ACTION_DELETE_SECOND;
	}

	return QUEUE_ACTION_NONE;
}

static gboolean
queue_event_is_descendant (QueueEvent *event,
			   GFile      *prefix)
{
	return g_file_has_prefix (event->file, prefix);
}

static gboolean
queue_event_is_equal_or_descendant (QueueEvent *event,
				    GFile      *prefix)
{
	return (g_file_equal (event->file, prefix) ||
		g_file_has_prefix (event->file, prefix));
}

static void
fs_finalize (GObject *object)
{
	TrackerMinerFSPrivate *priv;

	priv = TRACKER_MINER_FS (object)->priv;

	g_timer_destroy (priv->timer);
	g_timer_destroy (priv->extraction_timer);

	if (priv->item_queues_handler_id) {
		g_source_remove (priv->item_queues_handler_id);
		priv->item_queues_handler_id = 0;
	}

	if (priv->item_queue_blocker) {
		g_object_unref (priv->item_queue_blocker);
	}

	if (priv->file_notifier) {
		tracker_file_notifier_stop (priv->file_notifier);
	}

	/* Cancel every pending task */
	tracker_task_pool_foreach (priv->task_pool,
	                           task_pool_cancel_foreach,
	                           NULL);
	g_object_unref (priv->task_pool);

	if (priv->sparql_buffer) {
		g_object_unref (priv->sparql_buffer);
	}

	tracker_priority_queue_foreach (priv->items,
					(GFunc) queue_event_free,
					NULL);
	tracker_priority_queue_unref (priv->items);

	g_object_unref (priv->root);

	if (priv->indexing_tree) {
		g_object_unref (priv->indexing_tree);
	}

	if (priv->file_notifier) {
		g_object_unref (priv->file_notifier);
	}

	if (priv->roots_to_notify) {
		g_hash_table_unref (priv->roots_to_notify);

		/* Just in case we end up using this AFTER finalize, not expected */
		priv->roots_to_notify = NULL;
	}

	G_OBJECT_CLASS (tracker_miner_fs_parent_class)->finalize (object);
}

static void
fs_constructed (GObject *object)
{
	TrackerMinerFSPrivate *priv;

	/* NOTE: We have to do this in this order because initables
	 * are called _AFTER_ constructed and for subclasses that are
	 * not initables we don't have any other way than to chain
	 * constructed and root/indexing tree must exist at that
	 * point.
	 *
	 * If priv->indexing_tree is NULL after this function, the
	 * initiable functions will fail and this class will not be
	 * created anyway.
	 */
	G_OBJECT_CLASS (tracker_miner_fs_parent_class)->constructed (object);

	priv = TRACKER_MINER_FS (object)->priv;

	/* Create root if one didn't exist */
	if (priv->root == NULL) {
		/* We default to file:/// */
		priv->root = g_file_new_for_uri ("file:///");
	}

	/* Create indexing tree */
	priv->indexing_tree = tracker_indexing_tree_new_with_root (priv->root);
}

static void
fs_set_property (GObject      *object,
                 guint         prop_id,
                 const GValue *value,
                 GParamSpec   *pspec)
{
	TrackerMinerFS *fs = TRACKER_MINER_FS (object);

	switch (prop_id) {
	case PROP_THROTTLE:
		tracker_miner_fs_set_throttle (TRACKER_MINER_FS (object),
		                               g_value_get_double (value));
		break;
	case PROP_ROOT:
		/* We expect this to only occur once, on object construct */
		fs->priv->root = g_value_dup_object (value);
		break;
	case PROP_WAIT_POOL_LIMIT:
		tracker_task_pool_set_limit (fs->priv->task_pool,
		                             g_value_get_uint (value));
		break;
	case PROP_READY_POOL_LIMIT:
		fs->priv->sparql_buffer_limit = g_value_get_uint (value);

		if (fs->priv->sparql_buffer) {
			tracker_task_pool_set_limit (TRACKER_TASK_POOL (fs->priv->sparql_buffer),
			                             fs->priv->sparql_buffer_limit);
		}
		break;
	case PROP_DATA_PROVIDER:
		fs->priv->data_provider = g_value_dup_object (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
fs_get_property (GObject    *object,
                 guint       prop_id,
                 GValue     *value,
                 GParamSpec *pspec)
{
	TrackerMinerFS *fs;

	fs = TRACKER_MINER_FS (object);

	switch (prop_id) {
	case PROP_THROTTLE:
		g_value_set_double (value, fs->priv->throttle);
		break;
	case PROP_ROOT:
		g_value_set_object (value, fs->priv->root);
		break;
	case PROP_WAIT_POOL_LIMIT:
		g_value_set_uint (value, tracker_task_pool_get_limit (fs->priv->task_pool));
		break;
	case PROP_READY_POOL_LIMIT:
		g_value_set_uint (value, fs->priv->sparql_buffer_limit);
		break;
	case PROP_DATA_PROVIDER:
		g_value_set_object (value, fs->priv->data_provider);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
task_pool_limit_reached_notify_cb (GObject    *object,
				   GParamSpec *pspec,
				   gpointer    user_data)
{
	if (!tracker_task_pool_limit_reached (TRACKER_TASK_POOL (object))) {
		item_queue_handlers_set_up (TRACKER_MINER_FS (user_data));
	}
}

static void
miner_started (TrackerMiner *miner)
{
	TrackerMinerFS *fs;

	fs = TRACKER_MINER_FS (miner);

	fs->priv->been_started = TRUE;

	g_info ("Initializing");

	g_object_set (miner,
	              "progress", 0.0,
	              "status", "Initializing",
	              "remaining-time", 0,
	              NULL);

	tracker_file_notifier_start (fs->priv->file_notifier);
}

static void
miner_stopped (TrackerMiner *miner)
{
	g_info ("Idle");

	g_object_set (miner,
	              "progress", 1.0,
	              "status", "Idle",
	              "remaining-time", -1,
	              NULL);
}

static void
miner_paused (TrackerMiner *miner)
{
	TrackerMinerFS *fs;

	fs = TRACKER_MINER_FS (miner);

	fs->priv->is_paused = TRUE;

	tracker_file_notifier_stop (fs->priv->file_notifier);

	if (fs->priv->item_queues_handler_id) {
		g_source_remove (fs->priv->item_queues_handler_id);
		fs->priv->item_queues_handler_id = 0;
	}
}

static void
miner_resumed (TrackerMiner *miner)
{
	TrackerMinerFS *fs;

	fs = TRACKER_MINER_FS (miner);

	fs->priv->is_paused = FALSE;

	tracker_file_notifier_start (fs->priv->file_notifier);

	/* Only set up queue handler if we have items waiting to be
	 * processed.
	 */
	if (tracker_miner_fs_has_items_to_process (fs)) {
		item_queue_handlers_set_up (fs);
	}
}

static void
notify_roots_finished (TrackerMinerFS *fs,
                       gboolean        check_queues)
{
	GHashTableIter iter;
	gpointer key, value;

	if (check_queues &&
	    fs->priv->roots_to_notify &&
	    g_hash_table_size (fs->priv->roots_to_notify) < 2) {
		/* Technically, if there is only one root, it's
		 * pointless to do anything before the FINISHED (not
		 * FINISHED_ROOT) signal is emitted. In that
		 * situation we calls function first anyway with
		 * check_queues=FALSE so we still notify roots. This
		 * is really just for efficiency.
		 */
		return;
	} else if (fs->priv->roots_to_notify == NULL ||
	           g_hash_table_size (fs->priv->roots_to_notify) < 1) {
		/* Nothing to do */
		return;
	}

	g_hash_table_iter_init (&iter, fs->priv->roots_to_notify);
	while (g_hash_table_iter_next (&iter, &key, &value)) {
		GFile *root = key;

		/* Check if any content for root is still in the queue
		 * to be processed. This is only called each time a
		 * container/folder has been added to Tracker (so not
		 * too frequently)
		 */
		if (check_queues &&
		    tracker_priority_queue_find (fs->priv->items, NULL, (GEqualFunc) queue_event_is_descendant, root)) {
			continue;
		}

		/* Signal root is finished */
		g_signal_emit (fs, signals[FINISHED_ROOT], 0, root);

		/* Remove from hash table */
		g_hash_table_iter_remove (&iter);
	}
}

static void
process_print_stats (TrackerMinerFS *fs)
{
	/* Only do this the first time, otherwise the results are
	 * likely to be inaccurate. Devices can be added or removed so
	 * we can't assume stats are correct.
	 */
	if (!fs->priv->shown_totals) {
		fs->priv->shown_totals = TRUE;

		g_info ("--------------------------------------------------");
		g_info ("Total directories : %d (%d ignored)",
		        fs->priv->total_directories_found,
		        fs->priv->total_directories_ignored);
		g_info ("Total files       : %d (%d ignored)",
		        fs->priv->total_files_found,
		        fs->priv->total_files_ignored);
#if 0
		g_info ("Total monitors    : %d",
		        tracker_monitor_get_count (fs->priv->monitor));
#endif
		g_info ("Total processed   : %d (%d notified, %d with error)",
		        fs->priv->total_files_processed,
		        fs->priv->total_files_notified,
		        fs->priv->total_files_notified_error);
		g_info ("--------------------------------------------------\n");
	}
}

static void
process_stop (TrackerMinerFS *fs)
{
	/* Now we have finished crawling, print stats and enable monitor events */
	process_print_stats (fs);

	g_timer_stop (fs->priv->timer);
	g_timer_stop (fs->priv->extraction_timer);

	fs->priv->timer_stopped = TRUE;
	fs->priv->extraction_timer_stopped = TRUE;

	g_info ("Idle");

	g_object_set (fs,
	              "progress", 1.0,
	              "status", "Idle",
	              "remaining-time", 0,
	              NULL);

	/* Make sure we signal _ALL_ roots as finished before the
	 * main FINISHED signal
	 */
	notify_roots_finished (fs, FALSE);

	g_signal_emit (fs, signals[FINISHED], 0,
	               g_timer_elapsed (fs->priv->timer, NULL),
	               fs->priv->total_directories_found,
	               fs->priv->total_directories_ignored,
	               fs->priv->total_files_found,
	               fs->priv->total_files_ignored);

	g_timer_stop (fs->priv->timer);
	g_timer_stop (fs->priv->extraction_timer);

	fs->priv->total_directories_found = 0;
	fs->priv->total_directories_ignored = 0;
	fs->priv->total_files_found = 0;
	fs->priv->total_files_ignored = 0;

	fs->priv->been_crawled = TRUE;
}

static gboolean
item_queue_is_blocked_by_file (TrackerMinerFS *fs,
                               GFile *file)
{
	g_return_val_if_fail (G_IS_FILE (file), FALSE);

	if (fs->priv->item_queue_blocker != NULL &&
	    (fs->priv->item_queue_blocker == file ||
	     g_file_equal (fs->priv->item_queue_blocker, file))) {
		return TRUE;
	}

	return FALSE;
}

static void
sparql_buffer_task_finished_cb (GObject      *object,
                                GAsyncResult *result,
                                gpointer      user_data)
{
	TrackerMinerFS *fs;
	TrackerMinerFSPrivate *priv;
	TrackerTask *task;
	GFile *task_file;
	gboolean recursive;
	GError *error = NULL;

	fs = user_data;
	priv = fs->priv;

	task = tracker_sparql_buffer_push_finish (TRACKER_SPARQL_BUFFER (object),
	                                          result, &error);

	if (error) {
		g_critical ("Could not execute sparql: %s", error->message);
		priv->total_files_notified_error++;
		g_error_free (error);
	}

	task_file = tracker_task_get_file (task);

	recursive = GPOINTER_TO_INT (g_object_steal_qdata (G_OBJECT (task_file),
	                                                     priv->quark_recursive_removal));
	tracker_file_notifier_invalidate_file_iri (priv->file_notifier, task_file, recursive);

	if (item_queue_is_blocked_by_file (fs, task_file)) {
		g_object_unref (priv->item_queue_blocker);
		priv->item_queue_blocker = NULL;
	}

	if (priv->item_queue_blocker != NULL) {
		if (tracker_task_pool_get_size (TRACKER_TASK_POOL (object)) > 0) {
			tracker_sparql_buffer_flush (TRACKER_SPARQL_BUFFER (object),
			                             "Item queue still blocked after flush");

			/* Check if we've finished inserting for given prefixes ... */
			notify_roots_finished (fs, TRUE);
		}
	} else {
		item_queue_handlers_set_up (fs);
	}

	tracker_task_unref (task);
}

static UpdateProcessingTaskContext *
update_processing_task_context_new (TrackerMiner         *miner,
                                    gint                  priority,
                                    const gchar          *urn,
                                    GCancellable         *cancellable)
{
	UpdateProcessingTaskContext *ctxt;

	ctxt = g_slice_new0 (UpdateProcessingTaskContext);
	ctxt->miner = miner;
	ctxt->urn = g_strdup (urn);
	ctxt->priority = priority;

	if (cancellable) {
		ctxt->cancellable = g_object_ref (cancellable);
	}

	return ctxt;
}

static void
update_processing_task_context_free (UpdateProcessingTaskContext *ctxt)
{
	g_free (ctxt->urn);

	if (ctxt->cancellable) {
		g_object_unref (ctxt->cancellable);
	}

	g_slice_free (UpdateProcessingTaskContext, ctxt);
}

static void
on_signal_gtask_complete (GObject      *source,
			  GAsyncResult *res,
			  gpointer      user_data)
{
	TrackerMinerFS *fs = TRACKER_MINER_FS (source);
	TrackerTask *task, *sparql_task = NULL;
	UpdateProcessingTaskContext *ctxt;
	GError *error = NULL;
	GFile *file = user_data;
	gchar *uri, *sparql;

	sparql = g_task_propagate_pointer (G_TASK (res), &error);
	g_object_unref (res);

	task = tracker_task_pool_find (fs->priv->task_pool, file);
	g_assert (task != NULL);

	ctxt = tracker_task_get_data (task);
	uri = g_file_get_uri (file);

	if (error) {
		g_message ("Could not process '%s': %s", uri, error->message);
		g_error_free (error);

		fs->priv->total_files_notified_error++;
	} else {
		fs->priv->total_files_notified++;

		if (ctxt->urn) {
			/* The SPARQL builder will already contain the necessary
			 * DELETE statements for the properties being updated */
			g_debug ("Updating item '%s' with urn '%s'",
			         uri,
			         ctxt->urn);
		} else {
			g_debug ("Creating new item '%s'", uri);
		}

		sparql_task = tracker_sparql_task_new_take_sparql_str (file, sparql);
	}

	if (sparql_task) {
		tracker_sparql_buffer_push (fs->priv->sparql_buffer,
		                            sparql_task,
		                            ctxt->priority,
		                            sparql_buffer_task_finished_cb,
		                            fs);

		if (item_queue_is_blocked_by_file (fs, file)) {
			tracker_sparql_buffer_flush (fs->priv->sparql_buffer, "Current file is blocking item queue");

			/* Check if we've finished inserting for given prefixes ... */
			notify_roots_finished (fs, TRUE);
		}

		/* We can let go of our reference here because the
		 * sparql buffer takes its own reference when adding
		 * it to the task pool.
		 */
		tracker_task_unref (sparql_task);
	} else {
		if (item_queue_is_blocked_by_file (fs, file)) {
			/* Make sure that we don't stall the item queue, although we could
			 * expect the file to be reenqueued until the loop detector makes
			 * us drop it since we were specifically waiting for it to complete.
			 */
			g_object_unref (fs->priv->item_queue_blocker);
			fs->priv->item_queue_blocker = NULL;
			item_queue_handlers_set_up (fs);
		}
	}

	/* Last reference is kept by the pool, removing the task from
	 * the pool cleans up the task too!
	 *
	 * NOTE that calling this any earlier actually causes invalid
	 * reads because the task frees up the
	 * UpdateProcessingTaskContext and GFile.
	 */
	tracker_task_pool_remove (fs->priv->task_pool, task);

	if (tracker_miner_fs_has_items_to_process (fs) == FALSE &&
	    tracker_task_pool_get_size (TRACKER_TASK_POOL (fs->priv->task_pool)) == 0) {
		/* We need to run this one more time to trigger process_stop() */
		item_queue_handlers_set_up (fs);
	}

	g_free (uri);
}

static gboolean
item_add_or_update (TrackerMinerFS *fs,
                    GFile          *file,
                    gint            priority,
                    gboolean        attributes_update)
{
	TrackerMinerFSPrivate *priv;
	UpdateProcessingTaskContext *ctxt;
	GCancellable *cancellable;
	gboolean processing;
	TrackerTask *task;
	const gchar *urn;
	gchar *uri;
	GTask *gtask;

	priv = fs->priv;

	cancellable = g_cancellable_new ();
	g_object_ref (file);

	urn = tracker_file_notifier_get_file_iri (fs->priv->file_notifier,
	                                          file, FALSE);

	/* Create task and add it to the pool as a WAIT task (we need to extract
	 * the file metadata and such) */
	ctxt = update_processing_task_context_new (TRACKER_MINER (fs),
	                                           priority,
	                                           urn,
	                                           cancellable);
	task = tracker_task_new (file, ctxt,
	                         (GDestroyNotify) update_processing_task_context_free);

	tracker_task_pool_add (priv->task_pool, task);
	tracker_task_unref (task);

	/* Call ::process-file to see if we handle this resource or not */
	uri = g_file_get_uri (file);

	gtask = g_task_new (fs, ctxt->cancellable, on_signal_gtask_complete, file);

	if (!attributes_update) {
		g_debug ("Processing file '%s'...", uri);
		g_signal_emit (fs, signals[PROCESS_FILE], 0,
		               file, gtask,
		               &processing);
	} else {
		g_debug ("Processing attributes in file '%s'...", uri);
		g_signal_emit (fs, signals[PROCESS_FILE_ATTRIBUTES], 0,
		               file, gtask,
		               &processing);
	}

	if (!processing) {
		GError *error;

		error = g_error_new (tracker_miner_fs_error_quark (),
				     TRACKER_MINER_FS_ERROR_INIT,
				     "TrackerMinerFS::process-file returned FALSE");
		g_task_return_error (gtask, error);
	} else {
		fs->priv->total_files_processed++;
	}

	g_free (uri);
	g_object_unref (file);
	g_object_unref (cancellable);

	return !tracker_task_pool_limit_reached (priv->task_pool);
}

static gboolean
item_remove (TrackerMinerFS *fs,
             GFile          *file,
             gboolean        only_children,
             GString        *task_sparql)
{
	gchar *uri, *sparql;
	guint signal_num;

	uri = g_file_get_uri (file);

	g_debug ("Removing item: '%s' (Deleted from filesystem or no longer monitored)",
	         uri);

	g_object_set_qdata (G_OBJECT (file),
	                    fs->priv->quark_recursive_removal,
	                    GINT_TO_POINTER (TRUE));

	/* Call the implementation to generate a SPARQL update for the removal. */
	signal_num = only_children ? REMOVE_CHILDREN : REMOVE_FILE;
	g_signal_emit (fs, signals[signal_num], 0, file, &sparql);

	if (sparql && sparql[0] != '\0') {
		g_string_append (task_sparql, sparql);
		g_string_append (task_sparql, ";\n");
	}

	g_free (sparql);
	g_free (uri);

	return TRUE;
}

static gboolean
item_move (TrackerMinerFS *fs,
           GFile          *dest_file,
           GFile          *source_file,
           GString        *dest_task_sparql,
           GString        *source_task_sparql)
{
	gchar     *uri, *source_uri, *sparql;
	GFileInfo *file_info;
	const gchar *source_iri;
	gboolean source_exists;
	TrackerDirectoryFlags source_flags, flags;
	gboolean recursive;

	uri = g_file_get_uri (dest_file);
	source_uri = g_file_get_uri (source_file);

	/* FIXME: Should check the _NO_STAT on TrackerDirectoryFlags first! */
	file_info = g_file_query_info (dest_file,
	                               G_FILE_ATTRIBUTE_STANDARD_TYPE,
	                               G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
	                               NULL, NULL);

	/* Get 'source' ID */
	source_iri = tracker_file_notifier_get_file_iri (fs->priv->file_notifier,
	                                                 source_file, TRUE);
	source_exists = (source_iri != NULL);

	if (!file_info) {
		gboolean retval;

		if (source_exists) {
			/* Destination file has gone away, ignore dest file and remove source if any */
			retval = item_remove (fs, source_file, FALSE, source_task_sparql);
		} else {
			/* Destination file went away, and source wasn't indexed either */
			retval = TRUE;
		}

		g_free (source_uri);
		g_free (uri);

		return retval;
	} else if (!source_exists) {
		gboolean retval;

		/* The source file might not be indexed yet (eg. temporary save
		 * files that are immediately renamed to the definitive path).
		 * Deal with those as newly added items.
		 */
		g_debug ("Source file '%s' not yet in store, indexing '%s' "
		         "from scratch", source_uri, uri);

		retval = item_add_or_update (fs, dest_file, G_PRIORITY_DEFAULT, FALSE);

		g_free (source_uri);
		g_free (uri);
		g_object_unref (file_info);

		return retval;
	}

	g_debug ("Moving item from '%s' to '%s'",
	         source_uri,
	         uri);

	tracker_indexing_tree_get_root (fs->priv->indexing_tree, source_file, &source_flags);
	tracker_indexing_tree_get_root (fs->priv->indexing_tree, dest_file, &flags);
	recursive = ((source_flags & TRACKER_DIRECTORY_FLAG_RECURSE) != 0 &&
	             (flags & TRACKER_DIRECTORY_FLAG_RECURSE) != 0 &&
	             g_file_info_get_file_type (file_info) == G_FILE_TYPE_DIRECTORY);

	/* Delete destination item from store if any */
	item_remove (fs, dest_file, FALSE, dest_task_sparql);

	/* If the original location is recursive, but the destination location
	 * is not, remove all children.
	 */
	if (!recursive &&
	    (source_flags & TRACKER_DIRECTORY_FLAG_RECURSE) != 0)
		item_remove (fs, source_file, TRUE, source_task_sparql);

	g_signal_emit (fs, signals[MOVE_FILE], 0, dest_file, source_file, recursive, &sparql);

	if (sparql && sparql[0] != '\0') {
		/* This is treated as a task on dest_file */
		g_string_append (dest_task_sparql, sparql);
		g_string_append (dest_task_sparql, ";\n");
	}

	g_free (sparql);
	g_free (uri);
	g_free (source_uri);
	g_object_unref (file_info);

	return TRUE;
}

static gboolean
should_wait (TrackerMinerFS *fs,
             GFile          *file)
{
	GFile *parent;

	/* Is the item already being processed? */
	if (tracker_task_pool_find (fs->priv->task_pool, file) ||
	    tracker_task_pool_find (TRACKER_TASK_POOL (fs->priv->sparql_buffer), file)) {
		/* Yes, a previous event on same item currently
		 * being processed */
		fs->priv->item_queue_blocker = g_object_ref (file);
		return TRUE;
	}

	/* Is the item's parent being processed right now? */
	parent = g_file_get_parent (file);
	if (parent) {
		if (tracker_task_pool_find (fs->priv->task_pool, parent) ||
		    tracker_task_pool_find (TRACKER_TASK_POOL (fs->priv->sparql_buffer), parent)) {
			/* Yes, a previous event on the parent of this item
			 * currently being processed */
			fs->priv->item_queue_blocker = parent;
			return TRUE;
		}

		g_object_unref (parent);
	}
	return FALSE;
}

static gboolean
item_queue_get_next_file (TrackerMinerFS           *fs,
                          GFile                   **file,
                          GFile                   **source_file,
                          TrackerMinerFSEventType  *type,
                          gint                     *priority_out,
                          gboolean                 *attributes_update)
{
	QueueEvent *event;
	gint priority;

	*file = NULL;
	*source_file = NULL;

	if (tracker_file_notifier_is_active (fs->priv->file_notifier) ||
	    tracker_task_pool_limit_reached (fs->priv->task_pool) ||
	    tracker_task_pool_limit_reached (TRACKER_TASK_POOL (fs->priv->sparql_buffer))) {
		if (tracker_task_pool_get_size (fs->priv->task_pool) == 0) {
			fs->priv->extraction_timer_stopped = TRUE;
			g_timer_stop (fs->priv->extraction_timer);
		}

		/* There are still pending items to crawl,
		 * or extract pool limit is reached
		 */
		return FALSE;
	}

	event = tracker_priority_queue_peek (fs->priv->items, &priority);

	if (event) {
		if (should_wait (fs, event->file) ||
		    (event->dest_file && should_wait (fs, event->dest_file))) {
			return FALSE;
		}

		if (event->type == TRACKER_MINER_FS_EVENT_MOVED) {
			g_set_object (file, event->dest_file);
			g_set_object (source_file, event->file);
		} else {
			g_set_object (file, event->file);
		}

		*type = event->type;
		*priority_out = priority;
		*attributes_update = event->attributes_update;

		queue_event_free (event);
		tracker_priority_queue_pop (fs->priv->items, NULL);
	}

	return TRUE;
}

static gdouble
item_queue_get_progress (TrackerMinerFS *fs,
                         guint          *n_items_processed,
                         guint          *n_items_remaining)
{
	guint items_to_process = 0;
	guint items_total = 0;

	items_to_process += tracker_priority_queue_get_length (fs->priv->items);

	items_total += fs->priv->total_directories_found;
	items_total += fs->priv->total_files_found;

	if (n_items_processed) {
		*n_items_processed = ((items_total >= items_to_process) ?
		                      (items_total - items_to_process) : 0);
	}

	if (n_items_remaining) {
		*n_items_remaining = items_to_process;
	}

	if (items_total == 0 ||
	    items_to_process == 0 ||
	    items_to_process > items_total) {
		return 1.0;
	}

	return (gdouble) (items_total - items_to_process) / items_total;
}

/* Add a task to the processing pool to update stored information on 'file'.
 *
 * This function takes ownership of the 'sparql' string.
 */
static void
push_task (TrackerMinerFS *fs,
           GFile          *file,
           gchar          *sparql)
{
	TrackerTask *task;

	task = tracker_sparql_task_new_take_sparql_str (file, sparql);
	tracker_sparql_buffer_push (fs->priv->sparql_buffer,
	                            task,
	                            G_PRIORITY_DEFAULT,
	                            sparql_buffer_task_finished_cb,
	                            fs);
	tracker_task_unref (task);
}

static gboolean
miner_handle_next_item (TrackerMinerFS *fs)
{
	GFile *file = NULL;
	GFile *source_file = NULL;
	GFile *parent;
	GTimeVal time_now;
	static GTimeVal time_last = { 0 };
	gboolean keep_processing = TRUE;
	gboolean attributes_update = FALSE;
	TrackerMinerFSEventType type;
	gint priority = 0;
	GString *task_sparql = NULL;
	GString *source_task_sparql = NULL;

	if (fs->priv->timer_stopped) {
		g_timer_start (fs->priv->timer);
		fs->priv->timer_stopped = FALSE;
	}

	if (tracker_task_pool_limit_reached (TRACKER_TASK_POOL (fs->priv->sparql_buffer))) {
		/* Task pool is full, give it a break */
		return FALSE;
	}

	if (!item_queue_get_next_file (fs, &file, &source_file, &type, &priority, &attributes_update)) {
		/* We should flush the processing pool buffer here, because
		 * if there was a previous task on the same file we want to
		 * process now, we want it to get finished before we can go
		 * on with the queues... */
		tracker_sparql_buffer_flush (fs->priv->sparql_buffer,
		                             "Queue handlers WAIT");

		/* Check if we've finished inserting for given prefixes ... */
		notify_roots_finished (fs, TRUE);

		/* Items are still being processed, so wait until
		 * the processing pool is cleared before starting with
		 * the next directories batch.
		 */
		return FALSE;
	}

	if (file == NULL) {
		g_timer_stop (fs->priv->extraction_timer);
		fs->priv->extraction_timer_stopped = TRUE;
	} else if (fs->priv->extraction_timer_stopped) {
		g_timer_continue (fs->priv->extraction_timer);
		fs->priv->extraction_timer_stopped = FALSE;
	}

	/* Update progress, but don't spam it. */
	g_get_current_time (&time_now);

	if ((time_now.tv_sec - time_last.tv_sec) >= 1) {
		guint items_processed, items_remaining;
		gdouble progress_now;
		static gdouble progress_last = 0.0;
		static gint info_last = 0;
		gdouble seconds_elapsed, extraction_elapsed;

		time_last = time_now;

		/* Update progress? */
		progress_now = item_queue_get_progress (fs,
		                                        &items_processed,
		                                        &items_remaining);
		seconds_elapsed = g_timer_elapsed (fs->priv->timer, NULL);
		extraction_elapsed = g_timer_elapsed (fs->priv->extraction_timer, NULL);

		if (!tracker_file_notifier_is_active (fs->priv->file_notifier)) {
			gchar *status;
			gint remaining_time;

			g_object_get (fs, "status", &status, NULL);

			/* Compute remaining time */
			remaining_time = (gint)tracker_seconds_estimate (extraction_elapsed,
			                                                 items_processed,
			                                                 items_remaining);

			/* CLAMP progress so it doesn't go back below
			 * 2% (which we use for crawling)
			 */
			if (g_strcmp0 (status, "Processing…") != 0) {
				/* Don't spam this */
				g_info ("Processing…");
				g_object_set (fs,
				              "status", "Processing…",
				              "progress", CLAMP (progress_now, 0.02, 1.00),
				              "remaining-time", remaining_time,
				              NULL);
			} else {
				g_object_set (fs,
				              "progress", CLAMP (progress_now, 0.02, 1.00),
				              "remaining-time", remaining_time,
				              NULL);
			}

			g_free (status);
		}

		if (++info_last >= 5 &&
		    (gint) (progress_last * 100) != (gint) (progress_now * 100)) {
			gchar *str1, *str2;

			info_last = 0;
			progress_last = progress_now;

			/* Log estimated remaining time */
			str1 = tracker_seconds_estimate_to_string (extraction_elapsed,
			                                           TRUE,
			                                           items_processed,
			                                           items_remaining);
			str2 = tracker_seconds_to_string (seconds_elapsed, TRUE);

			g_info ("Processed %u/%u, estimated %s left, %s elapsed",
			        items_processed,
			        items_processed + items_remaining,
			        str1,
			        str2);

			g_free (str2);
			g_free (str1);
		}
	}

	if (file == NULL) {
		if (!tracker_file_notifier_is_active (fs->priv->file_notifier) &&
		    tracker_task_pool_get_size (fs->priv->task_pool) == 0) {
			if (tracker_task_pool_get_size (TRACKER_TASK_POOL (fs->priv->sparql_buffer)) == 0) {
				/* Print stats and signal finished */
				process_stop (fs);
			} else {
				/* Flush any possible pending update here */
				tracker_sparql_buffer_flush (fs->priv->sparql_buffer,
				                             "Queue handlers NONE");

				/* Check if we've finished inserting for given prefixes ... */
				notify_roots_finished (fs, TRUE);
			}
		}

		/* No more files left to process */
		return FALSE;
	}

	/* Handle queues */
	switch (type) {
	case TRACKER_MINER_FS_EVENT_MOVED:
		task_sparql = g_string_new ("");
		source_task_sparql = g_string_new ("");
		keep_processing = item_move (fs, file, source_file, task_sparql, source_task_sparql);
		break;
	case TRACKER_MINER_FS_EVENT_DELETED:
		task_sparql = g_string_new ("");
		keep_processing = item_remove (fs, file, FALSE, task_sparql);
		break;
	case TRACKER_MINER_FS_EVENT_CREATED:
	case TRACKER_MINER_FS_EVENT_UPDATED:
		parent = g_file_get_parent (file);

		if (!parent ||
		    tracker_indexing_tree_file_is_root (fs->priv->indexing_tree, file) ||
		    !tracker_indexing_tree_get_root (fs->priv->indexing_tree, file, NULL) ||
		    tracker_file_notifier_get_file_iri (fs->priv->file_notifier, parent, TRUE)) {
			keep_processing = item_add_or_update (fs, file, priority, attributes_update);
		} else {
			gchar *uri;

			/* We got an event on a file that has not its parent indexed
			 * even though it should. Given item_queue_get_next_file()
			 * above should return FALSE whenever the parent file is
			 * being processed, this means the parent is neither
			 * being processed nor indexed, no good.
			 *
			 * Bail out in these cases by removing all queued files
			 * inside the missing file. Whatever it was, it shall
			 * hopefully be fixed on next index.
			 */
			uri = g_file_get_uri (parent);
			g_warning ("Parent '%s' not indexed yet", uri);
			g_free (uri);

			tracker_priority_queue_foreach_remove (fs->priv->items,
							       (GEqualFunc) queue_event_is_equal_or_descendant,
							       parent,
							       (GDestroyNotify) queue_event_free);
			keep_processing = TRUE;
		}

		if (parent) {
			g_object_unref (parent);
		}

		break;
	default:
		g_assert_not_reached ();
	}

	if (source_task_sparql) {
		if (source_task_sparql->len == 0) {
			g_string_free (source_task_sparql, TRUE);
		} else {
			push_task (fs, source_file, g_string_free (source_task_sparql, FALSE));
		}
	}

	if (task_sparql) {
		if (task_sparql->len == 0) {
			g_string_free (task_sparql, TRUE);
		} else {
			push_task (fs, file, g_string_free (task_sparql, FALSE));
		}
	}

	if (!tracker_task_pool_limit_reached (TRACKER_TASK_POOL (fs->priv->sparql_buffer))) {
		item_queue_handlers_set_up (fs);
	}

	if (file) {
		g_object_unref (file);
	}

	if (source_file) {
		g_object_unref (source_file);
	}

	return keep_processing;
}

static gboolean
item_queue_handlers_cb (gpointer user_data)
{
	TrackerMinerFS *fs = user_data;
	gboolean retval = FALSE;
	gint i;

	for (i = 0; i < MAX_SIMULTANEOUS_ITEMS; i++) {
		retval = miner_handle_next_item (fs);
		if (retval == FALSE)
			break;
	}

	if (retval == FALSE) {
		fs->priv->item_queues_handler_id = 0;
	}

	return retval;
}

static guint
_tracker_idle_add (TrackerMinerFS *fs,
                   GSourceFunc     func,
                   gpointer        user_data)
{
	guint interval;

	interval = TRACKER_CRAWLER_MAX_TIMEOUT_INTERVAL * fs->priv->throttle;

	if (interval == 0) {
		return g_idle_add_full (TRACKER_TASK_PRIORITY, func, user_data, NULL);
	} else {
		return g_timeout_add_full (TRACKER_TASK_PRIORITY, interval, func, user_data, NULL);
	}
}

static void
item_queue_handlers_set_up (TrackerMinerFS *fs)
{
	trace_eq ("Setting up queue handlers...");
	if (fs->priv->item_queues_handler_id != 0) {
		trace_eq ("   cancelled: already one active");
		return;
	}

	if (fs->priv->is_paused) {
		trace_eq ("   cancelled: paused");
		return;
	}

	if (fs->priv->item_queue_blocker) {
		trace_eq ("   cancelled: item queue blocked waiting for file '%s'",
		          g_file_get_uri (fs->priv->item_queue_blocker));
		return;
	}

	/* Already processing max number of sparql updates */
	if (tracker_task_pool_limit_reached (fs->priv->task_pool)) {
		trace_eq ("   cancelled: pool limit reached (tasks: %u (max %u)",
		          tracker_task_pool_get_size (fs->priv->task_pool),
		          tracker_task_pool_get_limit (fs->priv->task_pool));
		return;
	}

	if (tracker_task_pool_limit_reached (TRACKER_TASK_POOL (fs->priv->sparql_buffer))) {
		trace_eq ("   cancelled: pool limit reached (sparql buffer: %u)",
		          tracker_task_pool_get_limit (TRACKER_TASK_POOL (fs->priv->sparql_buffer)));
		return;
	}

	if (!tracker_file_notifier_is_active (fs->priv->file_notifier)) {
		gchar *status;
		gdouble progress;

		g_object_get (fs,
		              "progress", &progress,
		              "status", &status,
		              NULL);

		/* Don't spam this */
		if (progress > 0.01 && g_strcmp0 (status, "Processing…") != 0) {
			g_info ("Processing…");
			g_object_set (fs, "status", "Processing…", NULL);
		}

		g_free (status);
	}

	trace_eq ("   scheduled in idle");
	fs->priv->item_queues_handler_id =
		_tracker_idle_add (fs,
		                   item_queue_handlers_cb,
		                   fs);
}

static gboolean
should_check_file (TrackerMinerFS *fs,
                   GFile          *file,
                   gboolean        is_dir)
{
	GFileType file_type;

	file_type = (is_dir) ? G_FILE_TYPE_DIRECTORY : G_FILE_TYPE_REGULAR;
	return tracker_indexing_tree_file_is_indexable (fs->priv->indexing_tree,
	                                                file, file_type);
}

static gint
miner_fs_get_queue_priority (TrackerMinerFS *fs,
                             GFile          *file)
{
	TrackerDirectoryFlags flags;

	tracker_indexing_tree_get_root (fs->priv->indexing_tree,
	                                file, &flags);

	return (flags & TRACKER_DIRECTORY_FLAG_PRIORITY) ?
	        G_PRIORITY_HIGH : G_PRIORITY_DEFAULT;
}

static void
miner_fs_queue_event (TrackerMinerFS *fs,
		      QueueEvent     *event,
		      guint           priority)
{
	GList *old = NULL, *link = NULL;

	if (event->type == TRACKER_MINER_FS_EVENT_MOVED) {
		/* Remove all children of the dest location from being processed. */
		tracker_priority_queue_foreach_remove (fs->priv->items,
						       (GEqualFunc) queue_event_is_equal_or_descendant,
						       event->dest_file,
						       (GDestroyNotify) queue_event_free);
	}

	old = queue_event_get_last_event_node (event);

	if (old) {
		QueueCoalesceAction action;
		QueueEvent *replacement = NULL;

		action = queue_event_coalesce (old->data, event, &replacement);

		if (action & QUEUE_ACTION_DELETE_FIRST) {
			queue_event_free (old->data);
			tracker_priority_queue_remove_node (fs->priv->items,
							    old);
		}

		if (action & QUEUE_ACTION_DELETE_SECOND) {
			queue_event_free (event);
			event = NULL;
		}

		if (replacement)
			event = replacement;
	}

	if (event) {
		if (event->type == TRACKER_MINER_FS_EVENT_DELETED) {
			/* Remove all children of this file from being processed. */
			tracker_priority_queue_foreach_remove (fs->priv->items,
							       (GEqualFunc) queue_event_is_equal_or_descendant,
							       event->file,
							       (GDestroyNotify) queue_event_free);
		}

		/* Ensure IRI is cached */
		tracker_file_notifier_get_file_iri (fs->priv->file_notifier,
						    event->file, TRUE);

		trace_eq_event (event);

		link = tracker_priority_queue_add (fs->priv->items, event, priority);
		queue_event_save_node (event, link);
		item_queue_handlers_set_up (fs);
	}
}

static gboolean
filter_event (TrackerMinerFS          *fs,
              TrackerMinerFSEventType  type,
              GFile                   *file,
              GFile                   *source_file)
{
	TrackerMinerFSClass *klass = TRACKER_MINER_FS_GET_CLASS (fs);

	if (!klass->filter_event)
		return FALSE;

	return klass->filter_event (fs, type, file, source_file);
}

static void
file_notifier_file_created (TrackerFileNotifier  *notifier,
                            GFile                *file,
                            gpointer              user_data)
{
	TrackerMinerFS *fs = user_data;
	QueueEvent *event;

	if (filter_event (fs, TRACKER_MINER_FS_EVENT_CREATED, file, NULL))
		return;

	event = queue_event_new (TRACKER_MINER_FS_EVENT_CREATED, file);
	miner_fs_queue_event (fs, event, miner_fs_get_queue_priority (fs, file));
}

static void
file_notifier_file_deleted (TrackerFileNotifier  *notifier,
                            GFile                *file,
                            gpointer              user_data)
{
	TrackerMinerFS *fs = user_data;
	QueueEvent *event;

	if (filter_event (fs, TRACKER_MINER_FS_EVENT_DELETED, file, NULL))
		return;

	if (tracker_file_notifier_get_file_type (notifier, file) == G_FILE_TYPE_DIRECTORY) {
		/* Cancel all pending tasks on files inside the path given by file */
		tracker_task_pool_foreach (fs->priv->task_pool,
					   task_pool_cancel_foreach,
					   file);
	}

	event = queue_event_new (TRACKER_MINER_FS_EVENT_DELETED, file);
	miner_fs_queue_event (fs, event, miner_fs_get_queue_priority (fs, file));
}

static void
file_notifier_file_updated (TrackerFileNotifier  *notifier,
                            GFile                *file,
                            gboolean              attributes_only,
                            gpointer              user_data)
{
	TrackerMinerFS *fs = user_data;
	QueueEvent *event;

	if (!attributes_only &&
	    filter_event (fs, TRACKER_MINER_FS_EVENT_UPDATED, file, NULL))
		return;

	event = queue_event_new (TRACKER_MINER_FS_EVENT_UPDATED, file);
	event->attributes_update = attributes_only;
	miner_fs_queue_event (fs, event, miner_fs_get_queue_priority (fs, file));
}

static void
file_notifier_file_moved (TrackerFileNotifier *notifier,
                          GFile               *source,
                          GFile               *dest,
                          gpointer             user_data)
{
	TrackerMinerFS *fs = user_data;
	QueueEvent *event;

	if (filter_event (fs, TRACKER_MINER_FS_EVENT_MOVED, dest, source))
		return;

	event = queue_event_moved_new (source, dest);
	miner_fs_queue_event (fs, event, miner_fs_get_queue_priority (fs, source));
}

static void
file_notifier_directory_started (TrackerFileNotifier *notifier,
                                 GFile               *directory,
                                 gpointer             user_data)
{
	TrackerMinerFS *fs = user_data;
	TrackerDirectoryFlags flags;
	gchar *str, *uri;

	uri = g_file_get_uri (directory);
	tracker_indexing_tree_get_root (fs->priv->indexing_tree,
					directory, &flags);

	if ((flags & TRACKER_DIRECTORY_FLAG_RECURSE) != 0) {
                str = g_strdup_printf ("Crawling recursively directory '%s'", uri);
        } else {
                str = g_strdup_printf ("Crawling single directory '%s'", uri);
        }

	if (fs->priv->timer_stopped) {
		g_timer_start (fs->priv->timer);
		fs->priv->timer_stopped = FALSE;
	}

	if (fs->priv->extraction_timer_stopped) {
		g_timer_start (fs->priv->timer);
		fs->priv->extraction_timer_stopped = FALSE;
	}

	/* Always set the progress here to at least 1%, and the remaining time
         * to -1 as we cannot guess during crawling (we don't know how many directories
         * we will find) */
        g_object_set (fs,
                      "progress", 0.01,
                      "status", str,
                      "remaining-time", -1,
                      NULL);
	g_free (str);
	g_free (uri);
}

static void
file_notifier_directory_finished (TrackerFileNotifier *notifier,
                                  GFile               *directory,
                                  guint                directories_found,
                                  guint                directories_ignored,
                                  guint                files_found,
                                  guint                files_ignored,
                                  gpointer             user_data)
{
	TrackerMinerFS *fs = user_data;
	gchar *str, *uri;

	/* Update stats */
	fs->priv->total_directories_found += directories_found;
	fs->priv->total_directories_ignored += directories_ignored;
	fs->priv->total_files_found += files_found;
	fs->priv->total_files_ignored += files_ignored;

	uri = g_file_get_uri (directory);
	str = g_strdup_printf ("Crawl finished for directory '%s'", uri);

        g_object_set (fs,
                      "progress", 0.01,
                      "status", str,
                      "remaining-time", -1,
                      NULL);

	g_free (str);
	g_free (uri);

	if (directories_found == 0 &&
	    files_found == 0) {
		/* Signal now because we have nothing to index */
		g_signal_emit (fs, signals[FINISHED_ROOT], 0, directory);
	} else {
		/* Add root to list we want to be notified about when
		 * finished indexing! */
		g_hash_table_replace (fs->priv->roots_to_notify,
		                      g_object_ref (directory),
		                      GUINT_TO_POINTER(time(NULL)));
	}
}

static void
file_notifier_finished (TrackerFileNotifier *notifier,
                        gpointer             user_data)
{
	TrackerMinerFS *fs = user_data;

	if (!tracker_miner_fs_has_items_to_process (fs)) {
		g_info ("Finished all tasks");
		process_stop (fs);
	} else {
		item_queue_handlers_set_up (fs);
	}
}

static void
task_pool_cancel_foreach (gpointer data,
                          gpointer user_data)
{
	TrackerTask *task = data;
	GFile *file = user_data;
	GFile *task_file;
	UpdateProcessingTaskContext *ctxt;

	ctxt = tracker_task_get_data (task);
	task_file = tracker_task_get_file (task);

	if (ctxt &&
	    ctxt->cancellable &&
	    (!file ||
	     (g_file_equal (task_file, file) ||
	      g_file_has_prefix (task_file, file)))) {
		g_cancellable_cancel (ctxt->cancellable);
	}
}

static void
indexing_tree_directory_removed (TrackerIndexingTree *indexing_tree,
                                 GFile               *directory,
                                 gpointer             user_data)
{
	TrackerMinerFS *fs = user_data;
	TrackerMinerFSPrivate *priv = fs->priv;
	GTimer *timer = g_timer_new ();

	/* Cancel all pending tasks on files inside the path given by file */
	tracker_task_pool_foreach (priv->task_pool,
	                           task_pool_cancel_foreach,
	                           directory);

	g_debug ("  Cancelled processing pool tasks at %f\n", g_timer_elapsed (timer, NULL));

	/* Remove anything contained in the removed directory
	 * from all relevant processing queues.
	 */
	tracker_priority_queue_foreach_remove (priv->items,
					       (GEqualFunc) queue_event_is_equal_or_descendant,
					       directory,
					       (GDestroyNotify) queue_event_free);

	g_debug ("  Removed files at %f\n", g_timer_elapsed (timer, NULL));
	g_timer_destroy (timer);
}

static gboolean
check_file_parents (TrackerMinerFS *fs,
                    GFile          *file)
{
	GFile *parent, *root;
	GList *parents = NULL, *p;
	QueueEvent *event;

	parent = g_file_get_parent (file);

	if (!parent) {
		return FALSE;
	}

	root = tracker_indexing_tree_get_root (fs->priv->indexing_tree,
	                                       parent, NULL);
	if (!root) {
		g_object_unref (parent);
		return FALSE;
	}

	/* Add parent directories until we're past the config dir */
	while (parent &&
	       !g_file_has_prefix (root, parent)) {
		parents = g_list_prepend (parents, parent);
		parent = g_file_get_parent (parent);
	}

	/* Last parent fetched is not added to the list */
	if (parent) {
		g_object_unref (parent);
	}

	for (p = parents; p; p = p->next) {
		event = queue_event_new (TRACKER_MINER_FS_EVENT_UPDATED, p->data);
		miner_fs_queue_event (fs, event, miner_fs_get_queue_priority (fs, p->data));
		g_object_unref (p->data);
	}

	g_list_free (parents);

	return TRUE;
}

/**
 * tracker_miner_fs_check_file:
 * @fs: a #TrackerMinerFS
 * @file: #GFile for the file to check
 * @priority: the priority of the check task
 * @check_parents: whether to check parents and eligibility or not
 *
 * Tells the filesystem miner to check and index a file at
 * a given priority, this file must be part of the usual
 * crawling directories of #TrackerMinerFS. See
 * tracker_indexing_tree_add().
 *
 * Since: 0.10
 **/
void
tracker_miner_fs_check_file (TrackerMinerFS *fs,
                             GFile          *file,
                             gint            priority,
                             gboolean        check_parents)
{
	gboolean should_process = TRUE;
	QueueEvent *event;
	gchar *uri;

	g_return_if_fail (TRACKER_IS_MINER_FS (fs));
	g_return_if_fail (G_IS_FILE (file));

	if (check_parents) {
		should_process = should_check_file (fs, file, FALSE);
	}

	uri = g_file_get_uri (file);

	g_debug ("%s:'%s' (FILE) (requested by application)",
	         should_process ? "Found " : "Ignored",
	         uri);

	if (should_process) {
		if (check_parents && !check_file_parents (fs, file)) {
			return;
		}

		tracker_file_notifier_get_file_iri (fs->priv->file_notifier,
		                                    file, TRUE);

		event = queue_event_new (TRACKER_MINER_FS_EVENT_UPDATED, file);
		trace_eq_event (event);
		miner_fs_queue_event (fs, event, priority);
	}

	g_free (uri);
}

/**
 * tracker_miner_fs_notify_finish:
 * @fs: a #TrackerMinerFS
 * @task: a #GTask obtained in a #TrackerMinerFS signal/vmethod
 * @sparql: (nullable): Resulting sparql for the given operation, or %NULL if
 *   there is an error
 * @error: a #GError with the error that happened during processing, or %NULL.
 *
 * Notifies @fs that all processing on @file has been finished, if any error
 * happened during file data processing, it should be passed in @error, else
 * @sparql should contain correct SPARQL representing the operation in
 * particular.
 *
 * This function is expected to be called in reaction to all #TrackerMinerFS
 * signals
 **/
void
tracker_miner_fs_notify_finish (TrackerMinerFS *fs,
                                GTask          *task,
                                const gchar    *sparql,
                                GError         *error)
{
	g_return_if_fail (TRACKER_IS_MINER_FS (fs));
	g_return_if_fail (G_IS_TASK (task));
	g_return_if_fail (sparql || error);

	if (error)
		g_task_return_error (task, error);
	else
		g_task_return_pointer (task, g_strdup (sparql), g_free);
}

/**
 * tracker_miner_fs_set_throttle:
 * @fs: a #TrackerMinerFS
 * @throttle: a double between 0.0 and 1.0
 *
 * Tells the filesystem miner to throttle its operations. A value of
 * 0.0 means no throttling at all, so the miner will perform
 * operations at full speed, 1.0 is the slowest value. With a value of
 * 1.0, the @fs is typically waiting one full second before handling
 * the next batch of queued items to be processed.
 *
 * Since: 0.8
 **/
void
tracker_miner_fs_set_throttle (TrackerMinerFS *fs,
                               gdouble         throttle)
{
	g_return_if_fail (TRACKER_IS_MINER_FS (fs));

	throttle = CLAMP (throttle, 0, 1);

	if (fs->priv->throttle == throttle) {
		return;
	}

	fs->priv->throttle = throttle;

	/* Update timeouts */
	if (fs->priv->item_queues_handler_id != 0) {
		g_source_remove (fs->priv->item_queues_handler_id);

		fs->priv->item_queues_handler_id =
			_tracker_idle_add (fs,
			                   item_queue_handlers_cb,
			                   fs);
	}
}

/**
 * tracker_miner_fs_get_throttle:
 * @fs: a #TrackerMinerFS
 *
 * Gets the current throttle value, see
 * tracker_miner_fs_set_throttle() for more details.
 *
 * Returns: a double representing a value between 0.0 and 1.0.
 *
 * Since: 0.8
 **/
gdouble
tracker_miner_fs_get_throttle (TrackerMinerFS *fs)
{
	g_return_val_if_fail (TRACKER_IS_MINER_FS (fs), 0);

	return fs->priv->throttle;
}

/**
 * tracker_miner_fs_get_urn:
 * @fs: a #TrackerMinerFS
 * @file: a #GFile obtained in #TrackerMinerFS::process-file
 *
 * If the item exists in the store, this function retrieves
 * the URN for a #GFile being currently processed.

 * If @file is not being currently processed by @fs, or doesn't
 * exist in the store yet, %NULL will be returned.
 *
 * Returns: (transfer none) (nullable): The URN containing the data associated to @file,
 *          or %NULL.
 *
 * Since: 0.8
 **/
const gchar *
tracker_miner_fs_get_urn (TrackerMinerFS *fs,
                          GFile          *file)
{
	TrackerTask *task;

	g_return_val_if_fail (TRACKER_IS_MINER_FS (fs), NULL);
	g_return_val_if_fail (G_IS_FILE (file), NULL);

	/* Check if found in currently processed data */
	task = tracker_task_pool_find (fs->priv->task_pool, file);

	if (!task) {
		gchar *uri;

		uri = g_file_get_uri (file);

		g_critical ("File '%s' is not being currently processed, "
		            "so the URN cannot be retrieved.", uri);
		g_free (uri);

		return NULL;
	} else {
		UpdateProcessingTaskContext *ctxt;

		/* We are only storing the URN in the created/updated tasks */
		ctxt = tracker_task_get_data (task);

		if (!ctxt) {
			gchar *uri;

			uri = g_file_get_uri (file);
			g_critical ("File '%s' is being processed, but not as a "
			            "CREATED/UPDATED task, so cannot get URN",
			            uri);
			g_free (uri);
			return NULL;
		}

		return ctxt->urn;
	}
}

/**
 * tracker_miner_fs_query_urn:
 * @fs: a #TrackerMinerFS
 * @file: a #GFile
 *
 * If the item exists in the store, this function retrieves
 * the URN of the given #GFile

 * If @file doesn't exist in the store yet, %NULL will be returned.
 *
 * Returns: (transfer full): A newly allocated string with the URN containing the data associated
 *          to @file, or %NULL.
 *
 * Since: 0.10
 **/
gchar *
tracker_miner_fs_query_urn (TrackerMinerFS *fs,
                            GFile          *file)
{
	g_return_val_if_fail (TRACKER_IS_MINER_FS (fs), NULL);
	g_return_val_if_fail (G_IS_FILE (file), NULL);

	return g_strdup (tracker_file_notifier_get_file_iri (fs->priv->file_notifier, file, TRUE));
}

/**
 * tracker_miner_fs_has_items_to_process:
 * @fs: a #TrackerMinerFS
 *
 * The @fs keeps many priority queus for content it is processing.
 * This function returns %TRUE if the sum of all (or any) priority
 * queues is more than 0. This includes items deleted, created,
 * updated, moved or being written back.
 *
 * Returns: %TRUE if there are items to process in the internal
 * queues, otherwise %FALSE.
 *
 * Since: 0.10
 **/
gboolean
tracker_miner_fs_has_items_to_process (TrackerMinerFS *fs)
{
	g_return_val_if_fail (TRACKER_IS_MINER_FS (fs), FALSE);

	if (tracker_file_notifier_is_active (fs->priv->file_notifier) ||
	    !tracker_priority_queue_is_empty (fs->priv->items)) {
		return TRUE;
	}

	return FALSE;
}

/**
 * tracker_miner_fs_get_indexing_tree:
 * @fs: a #TrackerMinerFS
 *
 * Returns the #TrackerIndexingTree which determines
 * what files/directories are indexed by @fs
 *
 * Returns: (transfer none): The #TrackerIndexingTree
 *          holding the indexing configuration
 **/
TrackerIndexingTree *
tracker_miner_fs_get_indexing_tree (TrackerMinerFS *fs)
{
	g_return_val_if_fail (TRACKER_IS_MINER_FS (fs), NULL);

	return fs->priv->indexing_tree;
}

/**
 * tracker_miner_fs_get_data_provider:
 * @fs: a #TrackerMinerFS
 *
 * Returns the #TrackerDataProvider implementation, which is being used
 * to supply #GFile and #GFileInfo content to Tracker.
 *
 * Returns: (transfer none): The #TrackerDataProvider supplying content
 *
 * Since: 1.2
 **/
TrackerDataProvider *
tracker_miner_fs_get_data_provider (TrackerMinerFS *fs)
{
	g_return_val_if_fail (TRACKER_IS_MINER_FS (fs), NULL);

	return fs->priv->data_provider;
}
