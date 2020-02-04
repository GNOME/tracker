/*
 * Copyright (C) 2008, Nokia <ivan.frade@nokia.com>
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

#include <string.h>
#include <stdlib.h>
#include <regex.h>
#include <zlib.h>
#include <locale.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>

#include <glib/gstdio.h>

#include <libtracker-common/tracker-common.h>
#include <libtracker-common/tracker-parser.h>

#if HAVE_TRACKER_FTS
#include <libtracker-fts/tracker-fts.h>
#endif

#include "tracker-db-manager.h"
#include "tracker-db-interface-sqlite.h"
#include "tracker-db-interface.h"
#include "tracker-data-manager.h"

#define UNKNOWN_STATUS 0.5

/* ZLib buffer settings */
#define ZLIB_BUF_SIZE                 8192

#define MAX_INTERFACES_PER_CPU        16
#define MAX_INTERFACES                (MAX_INTERFACES_PER_CPU * g_get_num_processors ())

/* Required minimum space needed to create databases (5Mb) */
#define TRACKER_DB_MIN_REQUIRED_SPACE 5242880

/* Default memory settings for databases */
#define TRACKER_DB_PAGE_SIZE_DONT_SET -1

/* Set current database version we are working with */
#define TRACKER_DB_VERSION_NOW        TRACKER_DB_VERSION_2_3

#define TRACKER_VACUUM_CHECK_SIZE     ((goffset) 4 * 1024 * 1024 * 1024) /* 4GB */

#define IN_USE_FILENAME               ".meta.isrunning"

#define TOSTRING1(x) #x
#define TOSTRING(x) TOSTRING1(x)
#define TRACKER_PARSER_VERSION_STRING TOSTRING(TRACKER_PARSER_VERSION)

#define FTS_FLAGS (TRACKER_DB_MANAGER_FTS_ENABLE_STEMMER |	  \
                   TRACKER_DB_MANAGER_FTS_ENABLE_UNACCENT |	  \
                   TRACKER_DB_MANAGER_FTS_ENABLE_STOP_WORDS |	  \
                   TRACKER_DB_MANAGER_FTS_IGNORE_NUMBERS)

typedef enum {
	TRACKER_DB_VERSION_UNKNOWN, /* Unknown */
	TRACKER_DB_VERSION_0_6_6,   /* before indexer-split */
	TRACKER_DB_VERSION_0_6_90,  /* after  indexer-split */
	TRACKER_DB_VERSION_0_6_91,  /* stable release */
	TRACKER_DB_VERSION_0_6_92,  /* current TRUNK */
	TRACKER_DB_VERSION_0_7_0,   /* vstore branch */
	TRACKER_DB_VERSION_0_7_4,   /* nothing special */
	TRACKER_DB_VERSION_0_7_12,  /* nmo ontology */
	TRACKER_DB_VERSION_0_7_13,  /* coalesce & writeback */
	TRACKER_DB_VERSION_0_7_17,  /* mlo ontology */
	TRACKER_DB_VERSION_0_7_20,  /* nco im ontology */
	TRACKER_DB_VERSION_0_7_21,  /* named graphs/localtime */
	TRACKER_DB_VERSION_0_7_22,  /* fts-limits branch */
	TRACKER_DB_VERSION_0_7_28,  /* RC1 + mto + nco:url */
	TRACKER_DB_VERSION_0_8_0,   /* stable release */
	TRACKER_DB_VERSION_0_9_0,   /* unstable release */
	TRACKER_DB_VERSION_0_9_8,   /* affiliation cardinality + volumes */
	TRACKER_DB_VERSION_0_9_15,  /* mtp:hidden */
	TRACKER_DB_VERSION_0_9_16,  /* Fix for NB#184823 */
	TRACKER_DB_VERSION_0_9_19,  /* collation */
	TRACKER_DB_VERSION_0_9_21,  /* Fix for NB#186055 */
	TRACKER_DB_VERSION_0_9_24,  /* nmo:PhoneMessage class */
	TRACKER_DB_VERSION_0_9_34,  /* ontology cache */
	TRACKER_DB_VERSION_0_9_38,  /* nie:url an inverse functional property */
	TRACKER_DB_VERSION_0_15_2,  /* fts4 */
	TRACKER_DB_VERSION_2_3      /* sparql1.1 */
} TrackerDBVersion;

typedef struct {
	TrackerDBInterface *iface;
	TrackerDBInterface *wal_iface;
	const gchar        *file;
	const gchar        *name;
	gchar              *abs_filename;
	gint                cache_size;
	gint                page_size;
	gboolean            attached;
	gboolean            is_index;
	guint64             mtime;
} TrackerDBDefinition;

static TrackerDBDefinition db_base = {
	NULL, NULL,
	"meta.db",
	"meta",
	NULL,
	TRACKER_DB_CACHE_SIZE_DEFAULT,
	8192,
	FALSE,
	FALSE,
	0
};

struct _TrackerDBManager {
	GObject parent_instance;
	TrackerDBDefinition db;
	gboolean locations_initialized;
	gchar *data_dir;
	gchar *user_data_dir;
	gchar *in_use_filename;
	GFile *cache_location;
	TrackerDBManagerFlags flags;
	guint s_cache_size;
	guint u_cache_size;

	gpointer vtab_data;

	GWeakRef iface_data;

	GAsyncQueue *interfaces;
	GThread *wal_thread;
};

enum {
	SETUP_INTERFACE,
	UPDATE_INTERFACE,
	N_SIGNALS
};

static guint signals[N_SIGNALS] = { 0 };

G_DEFINE_TYPE (TrackerDBManager, tracker_db_manager, G_TYPE_OBJECT)

static gboolean            db_exec_no_reply                        (TrackerDBInterface   *iface,
                                                                    const gchar          *query,
                                                                    ...);
static TrackerDBInterface *tracker_db_manager_create_db_interface   (TrackerDBManager    *db_manager,
                                                                     gboolean             readonly,
                                                                     GError             **error);

static TrackerDBInterface * init_writable_db_interface              (TrackerDBManager *db_manager);

static gboolean
db_exec_no_reply (TrackerDBInterface *iface,
                  const gchar        *query,
                  ...)
{
	va_list                     args;

	va_start (args, query);
	tracker_db_interface_execute_vquery (iface, NULL, query, args);
	va_end (args);

	return TRUE;
}

TrackerDBManagerFlags
tracker_db_manager_get_flags (TrackerDBManager *db_manager,
                              guint            *select_cache_size,
                              guint            *update_cache_size)
{
	if (select_cache_size)
		*select_cache_size = db_manager->s_cache_size;

	if (update_cache_size)
		*update_cache_size = db_manager->u_cache_size;

	return db_manager->flags;
}

static void
iface_set_params (TrackerDBInterface   *iface,
                  gboolean              readonly,
                  GError              **error)
{
	tracker_db_interface_execute_query (iface, NULL, "PRAGMA encoding = \"UTF-8\"");

	if (readonly) {
		tracker_db_interface_execute_query (iface, NULL, "PRAGMA temp_store = MEMORY;");
	} else {
		tracker_db_interface_execute_query (iface, NULL, "PRAGMA temp_store = FILE;");
	}
}

static void
db_set_params (TrackerDBInterface   *iface,
               const gchar          *database,
               gint                  cache_size,
               gint                  page_size,
               GError              **error)
{
	GError *internal_error = NULL;
	TrackerDBStatement *stmt;

	tracker_db_interface_execute_query (iface, NULL, "PRAGMA \"%s\".synchronous = NORMAL", database);
	tracker_db_interface_execute_query (iface, NULL, "PRAGMA \"%s\".auto_vacuum = 0", database);

	stmt = tracker_db_interface_create_statement (iface, TRACKER_DB_STATEMENT_CACHE_TYPE_NONE,
	                                              &internal_error,
	                                              "PRAGMA \"%s\".journal_mode = WAL", database);

	if (internal_error) {
		g_info ("Can't set journal mode to WAL: '%s'",
		        internal_error->message);
		g_propagate_error (error, internal_error);
	} else {
		TrackerDBCursor *cursor;

		cursor = tracker_db_statement_start_cursor (stmt, NULL);
		if (tracker_db_cursor_iter_next (cursor, NULL, NULL)) {
			if (g_ascii_strcasecmp (tracker_db_cursor_get_string (cursor, 0, NULL), "WAL") != 0) {
				g_set_error (error,
				             TRACKER_DB_INTERFACE_ERROR,
				             TRACKER_DB_OPEN_ERROR,
				             "Can't set journal mode to WAL");
			}
		}
		g_object_unref (cursor);
	}

	if (stmt) {
		g_object_unref (stmt);
	}

	/* disable autocheckpoint */
	tracker_db_interface_execute_query (iface, NULL, "PRAGMA \"%s\".wal_autocheckpoint = 0", database);

	tracker_db_interface_execute_query (iface, NULL, "PRAGMA \"%s\".journal_size_limit = 10240000", database);

	if (page_size != TRACKER_DB_PAGE_SIZE_DONT_SET) {
		g_debug ("  Setting page size to %d", page_size);
		tracker_db_interface_execute_query (iface, NULL, "PRAGMA \"%s\".page_size = %d", database, page_size);
	}

	tracker_db_interface_execute_query (iface, NULL, "PRAGMA \"%s\".cache_size = %d", database, cache_size);
	g_debug ("  Setting cache size to %d", cache_size);
}

void
tracker_db_manager_remove_all (TrackerDBManager *db_manager)
{
	gchar *filename;

	g_info ("Removing all files for database %s", db_manager->db.abs_filename);

	g_unlink (db_manager->db.abs_filename);

	/* also delete shm and wal helper files */
	filename = g_strdup_printf ("%s-shm", db_manager->db.abs_filename);
	g_unlink (filename);
	g_free (filename);

	filename = g_strdup_printf ("%s-wal", db_manager->db.abs_filename);
	g_unlink (filename);
	g_free (filename);
}

static gboolean
tracker_db_manager_get_metadata (TrackerDBManager   *db_manager,
                                 const gchar        *key,
                                 GValue             *value)
{
	TrackerDBInterface *iface;
	TrackerDBStatement *stmt;
	TrackerDBCursor *cursor;

	iface = tracker_db_manager_get_writable_db_interface (db_manager);
	stmt = tracker_db_interface_create_statement (iface, TRACKER_DB_STATEMENT_CACHE_TYPE_NONE,
	                                              NULL,
	                                              "SELECT value FROM metadata WHERE key = ?");
	if (!stmt)
		return FALSE;

	tracker_db_statement_bind_text (stmt, 0, key);
	cursor = tracker_db_statement_start_cursor (stmt, NULL);
	g_object_unref (stmt);

	if (!cursor || !tracker_db_cursor_iter_next (cursor, NULL, NULL)) {
		g_clear_object (&cursor);
		return FALSE;
	}

	tracker_db_cursor_get_value (cursor, 0, value);
	g_object_unref (cursor);

	return G_VALUE_TYPE (value) != G_TYPE_INVALID;
}

static void
tracker_db_manager_set_metadata (TrackerDBManager   *db_manager,
				 const gchar        *key,
				 GValue             *value)
{
	TrackerDBInterface *iface;
	TrackerDBStatement *stmt;
	GError *error = NULL;

	iface = tracker_db_manager_get_writable_db_interface (db_manager);
	stmt = tracker_db_interface_create_statement (iface, TRACKER_DB_STATEMENT_CACHE_TYPE_NONE,
	                                              &error,
	                                              "INSERT OR REPLACE INTO metadata VALUES (?, ?)");
	if (stmt) {
		tracker_db_statement_bind_text (stmt, 0, key);
		tracker_db_statement_bind_value (stmt, 1, value);
		tracker_db_statement_execute (stmt, &error);

		g_object_unref (stmt);
	}

	if (error) {
		g_critical ("Could not store database metadata: %s\n", error->message);
		g_error_free (error);
	}
}

static TrackerDBVersion
db_get_version (TrackerDBManager *db_manager)
{
	TrackerDBInterface *iface;
	TrackerDBStatement *stmt;
	TrackerDBCursor *cursor;
	TrackerDBVersion version;

	iface = tracker_db_manager_get_writable_db_interface (db_manager);
	stmt = tracker_db_interface_create_statement (iface, TRACKER_DB_STATEMENT_CACHE_TYPE_NONE,
	                                              NULL, "PRAGMA user_version");
	if (!stmt)
		return TRACKER_DB_VERSION_UNKNOWN;

	cursor = tracker_db_statement_start_cursor (stmt, NULL);
	g_object_unref (stmt);

	if (!cursor || !tracker_db_cursor_iter_next (cursor, NULL, NULL)) {
		g_clear_object (&cursor);
		return TRACKER_DB_VERSION_UNKNOWN;
	}

	version = tracker_db_cursor_get_int (cursor, 0);
	g_object_unref (cursor);

	return version;
}

void
tracker_db_manager_update_version (TrackerDBManager *db_manager)
{
	TrackerDBInterface *iface;
	TrackerDBStatement *stmt;
	GError *error = NULL;

	iface = tracker_db_manager_get_writable_db_interface (db_manager);
	stmt = tracker_db_interface_create_statement (iface, TRACKER_DB_STATEMENT_CACHE_TYPE_NONE,
	                                              &error, "PRAGMA user_version = %d",
						      TRACKER_DB_VERSION_NOW);
	if (stmt) {
		tracker_db_statement_execute (stmt, &error);
		g_object_unref (stmt);
	}

	if (error) {
		g_critical ("Could not set database version: %s\n", error->message);
		g_error_free (error);
	}
}

static gchar *
db_get_locale (TrackerDBManager *db_manager)
{
	GValue value = G_VALUE_INIT;
	gchar *locale = NULL;

	if (!tracker_db_manager_get_metadata (db_manager, "locale", &value))
		return NULL;

	locale = g_value_dup_string (&value);
	g_value_unset (&value);

	return locale;
}

static void
db_set_locale (TrackerDBManager *db_manager,
	       const gchar      *locale)
{
	GValue value = G_VALUE_INIT;

	g_value_init (&value, G_TYPE_STRING);
	g_value_set_string (&value, locale);

	tracker_db_manager_set_metadata (db_manager, "locale", &value);
	g_value_unset (&value);
}

gboolean
tracker_db_manager_locale_changed (TrackerDBManager  *db_manager,
                                   GError           **error)
{
	gchar *db_locale;
	gchar *current_locale;
	gboolean changed;

	/* As a special case, we allow calling this API function before
	 * tracker_data_manager_init() has been called, so it can be used
	 * to check for locale mismatches for initializing the database.
	 */
	tracker_db_manager_ensure_location (db_manager, db_manager->cache_location);

	/* Get current collation locale */
	current_locale = tracker_locale_get (TRACKER_LOCALE_COLLATE);

	/* Get db locale */
	db_locale = db_get_locale (db_manager);

	/* If they are different, recreate indexes. Note that having
	 * both to NULL is actually valid, they would default to
	 * the unicode collation without locale-specific stuff. */
	if (g_strcmp0 (db_locale, current_locale) != 0) {
		g_set_error (error,
		             TRACKER_DB_INTERFACE_ERROR,
		             TRACKER_DB_OPEN_ERROR,
		             "Locale change detected (DB:%s, User/App:%s)",
		             db_locale ? db_locale : "unknown",
		             current_locale);
		changed = TRUE;
	} else {
		g_info ("Current and DB locales match: '%s'", db_locale);
		changed = FALSE;
	}

	g_free (db_locale);
	g_free (current_locale);

	return changed;
}

void
tracker_db_manager_set_current_locale (TrackerDBManager *db_manager)
{
	gchar *current_locale;

	/* Get current collation locale */
	current_locale = tracker_locale_get (TRACKER_LOCALE_COLLATE);
	g_info ("Saving DB locale as: '%s'", current_locale);
	db_set_locale (db_manager, current_locale);
	g_free (current_locale);
}

static void
db_manager_analyze (TrackerDBManager   *db_manager,
                    TrackerDBInterface *iface)
{
	guint64             current_mtime;

	current_mtime = tracker_file_get_mtime (db_manager->db.abs_filename);

	if (current_mtime > db_manager->db.mtime) {
		g_info ("  Analyzing DB:'%s'", db_manager->db.name);
		db_exec_no_reply (iface, "ANALYZE %s.Services", db_manager->db.name);

		/* Remember current mtime for future */
		db_manager->db.mtime = current_mtime;
	} else {
		g_info ("  Not updating DB:'%s', no changes since last optimize", db_manager->db.name);
	}
}

static void
db_recreate_all (TrackerDBManager  *db_manager,
		 GError           **error)
{
	GError *internal_error = NULL;

	/* We call an internal version of this function here
	 * because at the time 'initialized' = FALSE and that
	 * will cause errors and do nothing.
	 */
	g_debug ("Cleaning up database files for reindex");

	tracker_db_manager_remove_all (db_manager);

	/* Now create the databases and close them */
	g_info ("Creating database files for %s...", db_manager->db.abs_filename);

	db_manager->db.iface = tracker_db_manager_create_db_interface (db_manager, FALSE, &internal_error);
	if (internal_error) {
		g_propagate_error (error, internal_error);
		return;
	}

	g_clear_object (&db_manager->db.iface);
	g_clear_object (&db_manager->db.wal_iface);
}

void
tracker_db_manager_ensure_location (TrackerDBManager *db_manager,
				    GFile            *cache_location)
{
	gchar *dir;

	if (db_manager->locations_initialized) {
		return;
	}

	db_manager->locations_initialized = TRUE;
	db_manager->data_dir = g_file_get_path (cache_location);

	db_manager->db = db_base;

	dir = g_file_get_path (cache_location);
	db_manager->db.abs_filename = g_build_filename (dir, db_manager->db.file, NULL);
	g_free (dir);
}

static void
perform_recreate (TrackerDBManager  *db_manager,
		  gboolean          *first_time,
		  GError           **error)
{
	GError *internal_error = NULL;

	if (first_time) {
		*first_time = TRUE;
	}

	g_clear_object (&db_manager->db.iface);
	g_clear_object (&db_manager->db.wal_iface);

	if (!tracker_file_system_has_enough_space (db_manager->data_dir, TRACKER_DB_MIN_REQUIRED_SPACE, TRUE)) {
		g_set_error (error,
		             TRACKER_DB_INTERFACE_ERROR,
		             TRACKER_DB_OPEN_ERROR,
		             "Filesystem has not enough space");
		return;
	}

	db_recreate_all (db_manager, &internal_error);

	if (internal_error) {
		g_propagate_error (error, internal_error);
	}
}

TrackerDBManager *
tracker_db_manager_new (TrackerDBManagerFlags   flags,
			GFile                  *cache_location,
			gboolean               *first_time,
			gboolean                restoring_backup,
			gboolean                shared_cache,
			guint                   select_cache_size,
			guint                   update_cache_size,
			TrackerBusyCallback     busy_callback,
			gpointer                busy_user_data,
			const gchar            *busy_operation,
                        GObject                *iface_data,
                        gpointer                vtab_data,
			GError                **error)
{
	TrackerDBManager *db_manager;
	TrackerDBVersion version;
	gboolean need_reindex;
	int in_use_file;
	gboolean loaded = FALSE;
	TrackerDBInterface *resources_iface;
	GError *internal_error = NULL;

	if (!cache_location) {
		g_set_error (error,
		             TRACKER_DATA_ONTOLOGY_ERROR,
		             TRACKER_DATA_UNSUPPORTED_LOCATION,
		             "All data storage and ontology locations must be provided");
		return NULL;
	}

	db_manager = g_object_new (TRACKER_TYPE_DB_MANAGER, NULL);
	db_manager->vtab_data = vtab_data;

	/* First set defaults for return values */
	if (first_time) {
		*first_time = FALSE;
	}

	need_reindex = FALSE;

	/* Set up locations */
	g_debug ("Setting database locations");

	db_manager->flags = flags;
	db_manager->s_cache_size = select_cache_size;
	db_manager->u_cache_size = update_cache_size;
	db_manager->interfaces = g_async_queue_new_full (g_object_unref);

	g_set_object (&db_manager->cache_location, cache_location);
	g_weak_ref_init (&db_manager->iface_data, iface_data);

	tracker_db_manager_ensure_location (db_manager, cache_location);
	db_manager->in_use_filename = g_build_filename (db_manager->user_data_dir,
							IN_USE_FILENAME,
							NULL);

	/* Don't do need_reindex checks for readonly (direct-access) */
	if ((flags & TRACKER_DB_MANAGER_READONLY) == 0) {

		/* Make sure the directories exist */
		g_debug ("Checking database directories exist");

		g_mkdir_with_parents (db_manager->data_dir, 00755);
		g_mkdir_with_parents (db_manager->user_data_dir, 00755);
	}

	g_debug ("Checking whether database files exist");

	/* Check we have the database in place, if it is
	 * missing, we reindex.
	 *
	 * There's no need to check for files not existing (for
	 * reindex) if reindexing is already needed.
	 */
	if (!g_file_test (db_manager->db.abs_filename, G_FILE_TEST_EXISTS)) {
		if ((flags & TRACKER_DB_MANAGER_READONLY) == 0) {
			g_info ("Could not find database file:'%s', reindex will be forced", db_manager->db.abs_filename);
			need_reindex = TRUE;
		} else {
			g_set_error (error,
			             TRACKER_DB_INTERFACE_ERROR,
			             TRACKER_DB_OPEN_ERROR,
			             "Could not find database file:'%s'.", db_manager->db.abs_filename);

			g_object_unref (db_manager);
			return NULL;
		}
	} else {
		g_info ("Checking database version");

		version = db_get_version (db_manager);

		if (version < TRACKER_DB_VERSION_NOW) {
			g_info ("  A reindex will be forced");
			need_reindex = TRUE;
		}
	}


	db_manager->locations_initialized = TRUE;

	/* Don't do remove-dbs for readonly (direct-access) */
	if ((flags & TRACKER_DB_MANAGER_READONLY) == 0) {

		/* If we are just initializing to remove the databases,
		 * return here.
		 */
		if ((flags & TRACKER_DB_MANAGER_REMOVE_ALL) != 0) {
			return db_manager;
		}
	} else {
		GValue value = G_VALUE_INIT;
		TrackerDBManagerFlags fts_flags = 0;

		if (tracker_db_manager_get_metadata (db_manager, "fts-flags", &value)) {
			fts_flags = g_ascii_strtoll (g_value_get_string (&value), NULL, 10);
			g_value_unset (&value);
		}

		/* Readonly connections should go with the FTS flags as stored
		 * in metadata.
		 */
		db_manager->flags = (db_manager->flags & ~(FTS_FLAGS)) | fts_flags;
	}

	/* Set general database options */
	if (shared_cache) {
		g_info ("Enabling database shared cache");
		tracker_db_interface_sqlite_enable_shared_cache ();
	}

	/* Should we reindex? If so, just remove all databases files,
	 * NOT the paths, note, that these paths are also used for
	 * other things like the nfs lock file.
	 */
	if (flags & TRACKER_DB_MANAGER_FORCE_REINDEX || need_reindex) {

		if (flags & TRACKER_DB_MANAGER_READONLY) {
			/* no reindexing supported in read-only mode (direct access) */

			g_set_error (error,
			             TRACKER_DB_INTERFACE_ERROR,
			             TRACKER_DB_OPEN_ERROR,
			             "No reindexing supported in read-only mode (direct access)");

			g_object_unref (db_manager);
			return NULL;
		}

		perform_recreate (db_manager, first_time, &internal_error);

		if (internal_error) {
			g_propagate_error (error, internal_error);
			g_object_unref (db_manager);
			return NULL;
		}

		tracker_db_manager_update_version (db_manager);

		/* Load databases */
		g_info ("Loading files for database %s...", db_manager->db.abs_filename);

	} else if ((flags & TRACKER_DB_MANAGER_READONLY) == 0) {
		/* do not do shutdown check for read-only mode (direct access) */
		gboolean must_recreate = FALSE;

		/* Load databases */
		g_info ("Loading files for database %s...", db_manager->db.abs_filename);

		if (!must_recreate && g_file_test (db_manager->in_use_filename, G_FILE_TEST_EXISTS)) {
			gsize size = 0;
			struct stat st;
			TrackerDBStatement *stmt;

			g_info ("Didn't shut down cleanly last time, doing integrity checks");

			if (g_stat (db_manager->db.abs_filename, &st) == 0) {
				size = st.st_size;
			}

			/* Size is 1 when using echo > file.db, none of our databases
			 * are only one byte in size even initually. */

			if (size <= 1) {
				if (!restoring_backup) {
					must_recreate = TRUE;
				} else {
					g_set_error (error,
					             TRACKER_DB_INTERFACE_ERROR,
					             TRACKER_DB_OPEN_ERROR,
					             "Corrupt db file");
					g_object_unref (db_manager);
					return NULL;
				}
			}

			if (!must_recreate) {
				db_manager->db.iface = tracker_db_manager_create_db_interface (db_manager, FALSE, &internal_error);

				if (internal_error) {
					/* If this already doesn't succeed, then surely the file is
					 * corrupt. No need to check for integrity anymore. */
					if (!restoring_backup) {
						g_clear_error (&internal_error);
						must_recreate = TRUE;
					} else {
						g_propagate_error (error, internal_error);
						g_object_unref (db_manager);
						return NULL;
					}
				}
			}

			if (!must_recreate) {
				gchar *busy_status;

				db_manager->db.mtime = tracker_file_get_mtime (db_manager->db.abs_filename);

				loaded = TRUE;

				/* Report OPERATION - STATUS */
				busy_status = g_strdup_printf ("%s - %s",
				                               busy_operation,
				                               "Integrity checking");
				busy_callback (busy_status, 0, busy_user_data);
				g_free (busy_status);

				stmt = tracker_db_interface_create_statement (db_manager->db.iface, TRACKER_DB_STATEMENT_CACHE_TYPE_NONE,
				                                              &internal_error,
				                                              "PRAGMA integrity_check(1)");

				if (internal_error != NULL) {
					if (internal_error->domain == TRACKER_DB_INTERFACE_ERROR &&
					    internal_error->code == TRACKER_DB_QUERY_ERROR) {
						must_recreate = TRUE;
					} else {
						g_critical ("%s", internal_error->message);
					}
					g_error_free (internal_error);
					internal_error = NULL;
				} else {
					TrackerDBCursor *cursor = NULL;

					if (stmt) {
						cursor = tracker_db_statement_start_cursor (stmt, NULL);
						g_object_unref (stmt);
					} else {
						g_critical ("Can't create stmt for integrity_check, no error given");
					}

					if (cursor) {
						if (tracker_db_cursor_iter_next (cursor, NULL, NULL)) {
							if (g_strcmp0 (tracker_db_cursor_get_string (cursor, 0, NULL), "ok") != 0) {
								must_recreate = TRUE;
							}
						}
						g_object_unref (cursor);
					}
				}
			}

			if (!must_recreate) {
				/* ensure that database has been initialized by an earlier tracker-store start
				   by checking whether Resource table exists */
				stmt = tracker_db_interface_create_statement (db_manager->db.iface, TRACKER_DB_STATEMENT_CACHE_TYPE_NONE,
				                                              &internal_error,
				                                              "SELECT 1 FROM Resource");
				if (internal_error != NULL) {
					if (!restoring_backup) {
						must_recreate = TRUE;
						g_error_free (internal_error);
						internal_error = NULL;
					}
				} else {
					g_object_unref (stmt);
				}
			}
		}

		if (must_recreate) {
			g_info ("Database severely damaged. We will recreate it.");

			perform_recreate (db_manager, first_time, &internal_error);
			if (internal_error) {
				g_propagate_error (error, internal_error);
				return FALSE;
			}
			loaded = FALSE;
		} else {
			if (internal_error) {
				g_propagate_error (error, internal_error);
				return FALSE;
			}
		}
	}

	if (!loaded) {
		db_manager->db.mtime = tracker_file_get_mtime (db_manager->db.abs_filename);
	}

	if ((flags & TRACKER_DB_MANAGER_READONLY) == 0) {
		/* do not create in-use file for read-only mode (direct access) */
		in_use_file = g_open (db_manager->in_use_filename,
			              O_WRONLY | O_APPEND | O_CREAT | O_SYNC,
			              S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);

		if (in_use_file >= 0) {
		        fsync (in_use_file);
		        close (in_use_file);
		}
	}

	resources_iface = tracker_db_manager_create_db_interface (db_manager,
	                                                          TRUE, &internal_error);

	if (internal_error) {
		if ((!restoring_backup) && (flags & TRACKER_DB_MANAGER_READONLY) == 0) {
			GError *new_error = NULL;

			perform_recreate (db_manager, first_time, &new_error);
			if (!new_error) {
				resources_iface = tracker_db_manager_create_db_interface (db_manager, TRUE,
				                                                          &internal_error);
			} else {
				/* Most serious error is the recreate one here */
				g_clear_error (&internal_error);
				g_propagate_error (error, new_error);
				g_object_unref (db_manager);
				return NULL;
			}
		} else {
			g_propagate_error (error, internal_error);
			g_object_unref (db_manager);
			return NULL;
		}
	}

	g_clear_object (&resources_iface);

	return db_manager;
}

void
tracker_db_manager_finalize (GObject *object)
{
	TrackerDBManager *db_manager = TRACKER_DB_MANAGER (object);
	gboolean readonly = (db_manager->flags & TRACKER_DB_MANAGER_READONLY) != 0;

	g_async_queue_unref (db_manager->interfaces);
	g_free (db_manager->db.abs_filename);

	if (db_manager->wal_thread)
		g_thread_join (db_manager->wal_thread);

	g_clear_object (&db_manager->db.wal_iface);

	if (db_manager->db.iface) {
		if (!readonly)
			tracker_db_interface_sqlite_wal_checkpoint (db_manager->db.iface, TRUE, NULL);
		g_object_unref (db_manager->db.iface);
	}

	g_weak_ref_clear (&db_manager->iface_data);

	g_free (db_manager->data_dir);
	g_free (db_manager->user_data_dir);

	if (!readonly) {
		/* do not delete in-use file for read-only mode (direct access) */
		g_unlink (db_manager->in_use_filename);
	}

	g_free (db_manager->in_use_filename);

	G_OBJECT_CLASS (tracker_db_manager_parent_class)->finalize (object);
}

void
tracker_db_manager_optimize (TrackerDBManager *db_manager)
{
	gboolean dbs_are_open = FALSE;
	TrackerDBInterface *iface;

	g_info ("Optimizing database...");

	g_info ("  Checking database is not in use");

	iface = tracker_db_manager_get_writable_db_interface (db_manager);

	/* Check if any connections are open? */
	if (G_OBJECT (iface)->ref_count > 1) {
		g_info ("  database is still in use with %d references!",
		        G_OBJECT (iface)->ref_count);

		dbs_are_open = TRUE;
	}

	if (dbs_are_open) {
		g_info ("  Not optimizing database, still in use with > 1 reference");
		return;
	}

	/* Optimize the metadata database */
	db_manager_analyze (db_manager, iface);
}

const gchar *
tracker_db_manager_get_file (TrackerDBManager *db_manager)
{
	return db_manager->db.abs_filename;
}

static TrackerDBInterface *
tracker_db_manager_create_db_interface (TrackerDBManager  *db_manager,
                                        gboolean           readonly,
                                        GError           **error)
{
	TrackerDBInterface *connection;
	GError *internal_error = NULL;
	TrackerDBInterfaceFlags flags = 0;

	if (readonly)
		flags |= TRACKER_DB_INTERFACE_READONLY;
	if (db_manager->flags & TRACKER_DB_MANAGER_ENABLE_MUTEXES)
		flags |= TRACKER_DB_INTERFACE_USE_MUTEX;

	connection = tracker_db_interface_sqlite_new (db_manager->db.abs_filename,
	                                              flags,
	                                              &internal_error);
	if (internal_error) {
		g_propagate_error (error, internal_error);
		return NULL;
	}

	tracker_db_interface_set_user_data (connection,
	                                    g_weak_ref_get (&db_manager->iface_data),
	                                    g_object_unref);

	tracker_db_interface_init_vtabs (connection, db_manager->vtab_data);

	iface_set_params (connection,
	                  readonly,
	                  &internal_error);
	db_set_params (connection, "main",
	               db_manager->db.cache_size,
	               db_manager->db.page_size,
	               &internal_error);

	if (internal_error) {
		g_propagate_error (error, internal_error);
		g_object_unref (connection);
		return NULL;
	}

	tracker_db_interface_set_max_stmt_cache_size (connection,
	                                              TRACKER_DB_STATEMENT_CACHE_TYPE_SELECT,
	                                              db_manager->s_cache_size);

	if (!readonly) {
		tracker_db_interface_set_max_stmt_cache_size (connection,
		                                              TRACKER_DB_STATEMENT_CACHE_TYPE_UPDATE,
		                                              db_manager->u_cache_size);
	}

	return connection;
}

/**
 * tracker_db_manager_get_db_interface:
 *
 * Request a database connection to the database
 *
 * The caller must NOT g_object_unref the result
 *
 * returns: (callee-owns): a database connection
 **/
TrackerDBInterface *
tracker_db_manager_get_db_interface (TrackerDBManager *db_manager)
{
	GError *internal_error = NULL;
	TrackerDBInterface *interface;

	/* The interfaces never actually leave the async queue,
	 * we use it as a thread synchronized LRU, which doesn't
	 * mean the interface found has no other active cursors,
	 * in which case we either optionally create a new
	 * TrackerDBInterface, or resign to sharing the obtained
	 * one with other threads (thus getting increased contention
	 * in the interface lock).
	 */
	g_async_queue_lock (db_manager->interfaces);
	interface = g_async_queue_try_pop_unlocked (db_manager->interfaces);

	if (interface && tracker_db_interface_get_is_used (interface) &&
	    g_async_queue_length_unlocked (db_manager->interfaces) < MAX_INTERFACES) {
		/* Put it back and go at creating a new one */
		g_async_queue_push_front_unlocked (db_manager->interfaces, interface);
		interface = NULL;
	}

	if (interface) {
		g_signal_emit (db_manager, signals[UPDATE_INTERFACE], 0, interface);
	} else {
		/* Create a new one to satisfy the request */
		interface = tracker_db_manager_create_db_interface (db_manager,
		                                                    TRUE, &internal_error);

		if (interface) {
			g_signal_emit (db_manager, signals[SETUP_INTERFACE], 0, interface);
		} else {
			if (g_async_queue_length_unlocked (db_manager->interfaces) == 0) {
				g_critical ("Error opening database: %s", internal_error->message);
				g_error_free (internal_error);
				g_async_queue_unlock (db_manager->interfaces);
				return NULL;
			} else {
				g_error_free (internal_error);
				/* Fetch the first interface back. Oh well */
				interface = g_async_queue_try_pop_unlocked (db_manager->interfaces);
			}
		}
	}

	g_async_queue_push_unlocked (db_manager->interfaces, interface);
	g_async_queue_unlock (db_manager->interfaces);

	return interface;
}

static void
tracker_db_manager_init (TrackerDBManager *manager)
{
}

static void
tracker_db_manager_class_init (TrackerDBManagerClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = tracker_db_manager_finalize;

	signals[SETUP_INTERFACE] =
               g_signal_new ("setup-interface",
                             G_TYPE_FROM_CLASS (klass),
                             G_SIGNAL_RUN_LAST, 0,
                             NULL, NULL,
                             g_cclosure_marshal_VOID__OBJECT,
                             G_TYPE_NONE,
                             1, TRACKER_TYPE_DB_INTERFACE);
	signals[UPDATE_INTERFACE] =
               g_signal_new ("update-interface",
                             G_TYPE_FROM_CLASS (klass),
                             G_SIGNAL_RUN_LAST, 0,
                             NULL, NULL,
                             g_cclosure_marshal_VOID__OBJECT,
                             G_TYPE_NONE,
                             1, TRACKER_TYPE_DB_INTERFACE);
}

static void
wal_checkpoint (TrackerDBInterface *iface,
                gboolean            blocking)
{
	GError *error = NULL;

	tracker_db_interface_sqlite_wal_checkpoint (iface, blocking,
	                                            blocking ? &error : NULL);

	if (error) {
		g_warning ("Error in %s WAL checkpoint: %s",
			   blocking ? "blocking" : "deferred",
			   error->message);
		g_error_free (error);
	}
}

static gpointer
wal_checkpoint_thread (gpointer data)
{
	TrackerDBManager *db_manager = data;

	if (!db_manager->db.wal_iface)
		db_manager->db.wal_iface = init_writable_db_interface (db_manager);

	if (db_manager->db.wal_iface)
		wal_checkpoint (db_manager->db.wal_iface, FALSE);

	return NULL;
}

static void
wal_hook (TrackerDBInterface *iface,
          gint                n_pages,
          gpointer            user_data)
{
	TrackerDBManager *db_manager = user_data;

	/* Ensure there is only one WAL checkpoint at a time */
	if (db_manager->wal_thread)
		g_thread_join (db_manager->wal_thread);

	if (n_pages >= 10000) {
		/* Do immediate checkpointing (blocking updates) to
		 * prevent excessive WAL file growth.
		 */
		wal_checkpoint (iface, TRUE);
	} else {
		/* Defer non-blocking checkpoint to thread */
		db_manager->wal_thread = g_thread_try_new ("wal-checkpoint", wal_checkpoint_thread,
							   db_manager, NULL);
	}
}

static TrackerDBInterface *
init_writable_db_interface (TrackerDBManager *db_manager)
{
	TrackerDBInterface *iface;
	GError *error = NULL;
	gboolean readonly;

	/* Honor anyway the DBManager readonly flag */
	readonly = (db_manager->flags & TRACKER_DB_MANAGER_READONLY) != 0;
	iface = tracker_db_manager_create_db_interface (db_manager, readonly, &error);
	if (error) {
		g_critical ("Error opening readwrite database: %s", error->message);
		g_error_free (error);
	}

	return iface;
}

TrackerDBInterface *
tracker_db_manager_get_writable_db_interface (TrackerDBManager *db_manager)
{
	if (db_manager->db.iface == NULL) {
		db_manager->db.iface = init_writable_db_interface (db_manager);

		if (db_manager->db.iface &&
		    (db_manager->flags & TRACKER_DB_MANAGER_READONLY) == 0) {
			tracker_db_interface_sqlite_wal_hook (db_manager->db.iface,
			                                      wal_hook, db_manager);
		}
	}

	return db_manager->db.iface;
}

/**
 * tracker_db_manager_has_enough_space:
 *
 * Checks whether the file system, where the database files are stored,
 * has enough free space to allow modifications.
 *
 * returns: TRUE if there is enough space, FALSE otherwise
 **/
gboolean
tracker_db_manager_has_enough_space (TrackerDBManager *db_manager)
{
	return tracker_file_system_has_enough_space (db_manager->data_dir, TRACKER_DB_MIN_REQUIRED_SPACE, FALSE);
}

gboolean
tracker_db_manager_get_tokenizer_changed (TrackerDBManager *db_manager)
{
	GValue value = G_VALUE_INIT;
	const gchar *version;
	TrackerDBManagerFlags flags;
	gboolean changed;

	if (!tracker_db_manager_get_metadata (db_manager, "fts-flags", &value))
		return TRUE;

	flags = g_ascii_strtoll (g_value_get_string (&value), NULL, 10);
	g_value_unset (&value);

	if ((db_manager->flags & TRACKER_DB_MANAGER_READONLY) == 0 &&
	    flags != (db_manager->flags & FTS_FLAGS)) {
		return TRUE;
	}

	if (!tracker_db_manager_get_metadata (db_manager, "parser-version", &value))
		return TRUE;

	version = g_value_get_string (&value);
	changed = strcmp (version, TRACKER_PARSER_VERSION_STRING) != 0;
	g_value_unset (&value);

	return changed;
}

void
tracker_db_manager_tokenizer_update (TrackerDBManager *db_manager)
{
	GValue value = G_VALUE_INIT;

	g_value_init (&value, G_TYPE_STRING);
	g_value_set_string (&value, TRACKER_PARSER_VERSION_STRING);
	tracker_db_manager_set_metadata (db_manager, "parser-version", &value);
	g_value_unset (&value);

	g_value_init (&value, G_TYPE_INT64);
	g_value_set_int64 (&value, (db_manager->flags & FTS_FLAGS));
	tracker_db_manager_set_metadata (db_manager, "fts-flags", &value);
	g_value_unset (&value);
}

void
tracker_db_manager_check_perform_vacuum (TrackerDBManager *db_manager)
{
	TrackerDBInterface *iface;

	if (tracker_file_get_size (db_manager->db.abs_filename) < TRACKER_VACUUM_CHECK_SIZE)
		return;

	iface = tracker_db_manager_get_writable_db_interface (db_manager);
	tracker_db_interface_execute_query (iface, NULL, "VACUUM");
}

gboolean
tracker_db_manager_attach_database (TrackerDBManager    *db_manager,
                                    TrackerDBInterface  *iface,
                                    const gchar         *name,
                                    gboolean             create,
                                    GError             **error)
{
	gchar *filename, *escaped;
	GFile *file;

	filename = g_strdup_printf ("%s.db", name);
	escaped = g_uri_escape_string (filename, NULL, FALSE);
	file = g_file_get_child (db_manager->cache_location, escaped);
	g_free (filename);
	g_free (escaped);

	if (create) {
		GError *inner_error = NULL;

		/* Create the database from scratch */
		if (!g_file_delete (file, NULL, &inner_error)) {
			if (!g_error_matches (inner_error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND)) {
				g_object_unref (file);
				g_propagate_error (error, inner_error);
				return FALSE;
			}
		}
	}

	if (!tracker_db_interface_attach_database (iface, file, name, error)) {
		g_object_unref (file);
		return FALSE;
	}

	g_object_unref (file);
	db_set_params (iface, name,
	               db_manager->db.cache_size,
	               db_manager->db.page_size,
	               error);
	return TRUE;
}

gboolean
tracker_db_manager_detach_database (TrackerDBManager    *db_manager,
                                    TrackerDBInterface  *iface,
                                    const gchar         *name,
                                    GError             **error)
{
	return tracker_db_interface_detach_database (iface, name, error);
}
