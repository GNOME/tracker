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

#include "tracker-db-journal.h"
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
#define TRACKER_DB_VERSION_NOW        TRACKER_DB_VERSION_0_15_2
#define TRACKER_DB_VERSION_FILE       "db-version.txt"
#define TRACKER_DB_LOCALE_FILE        "db-locale.txt"

#define TRACKER_VACUUM_CHECK_SIZE     ((goffset) 4 * 1024 * 1024 * 1024) /* 4GB */

#define IN_USE_FILENAME               ".meta.isrunning"

#define PARSER_VERSION_FILENAME       "parser-version.txt"

#define TOSTRING1(x) #x
#define TOSTRING(x) TOSTRING1(x)
#define TRACKER_PARSER_VERSION_STRING TOSTRING(TRACKER_PARSER_VERSION)

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
	TRACKER_DB_VERSION_0_15_2   /* fts4 */
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
	TrackerDBDefinition db;
	gboolean locations_initialized;
	gchar *data_dir;
	gchar *user_data_dir;
	gchar *in_use_filename;
	GFile *cache_location;
	GFile *data_location;
	TrackerDBManagerFlags flags;
	guint s_cache_size;
	guint u_cache_size;

	gpointer vtab_data;

	GWeakRef iface_data;

	GAsyncQueue *interfaces;
	GThread *wal_thread;
};

static gboolean            db_exec_no_reply                        (TrackerDBInterface   *iface,
                                                                    const gchar          *query,
                                                                    ...);
static TrackerDBInterface *tracker_db_manager_create_db_interface   (TrackerDBManager    *db_manager,
                                                                     gboolean             readonly,
                                                                     GError             **error);
static void                db_remove_locale_file                    (TrackerDBManager    *db_manager);

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
db_set_params (TrackerDBInterface   *iface,
               gint                  cache_size,
               gint                  page_size,
               gboolean              readonly,
               GError              **error)
{
	GError *internal_error = NULL;
	TrackerDBStatement *stmt;

#ifdef DISABLE_JOURNAL
	tracker_db_interface_execute_query (iface, NULL, "PRAGMA synchronous = NORMAL;");
#else
	tracker_db_interface_execute_query (iface, NULL, "PRAGMA synchronous = OFF;");
#endif /* DISABLE_JOURNAL */
	tracker_db_interface_execute_query (iface, NULL, "PRAGMA encoding = \"UTF-8\"");
	tracker_db_interface_execute_query (iface, NULL, "PRAGMA auto_vacuum = 0;");

	if (readonly) {
		tracker_db_interface_execute_query (iface, NULL, "PRAGMA temp_store = MEMORY;");
	} else {
		tracker_db_interface_execute_query (iface, NULL, "PRAGMA temp_store = FILE;");
	}

	stmt = tracker_db_interface_create_statement (iface, TRACKER_DB_STATEMENT_CACHE_TYPE_NONE,
	                                              &internal_error,
	                                              "PRAGMA journal_mode = WAL;");

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
	tracker_db_interface_execute_query (iface, NULL, "PRAGMA wal_autocheckpoint = 0");

	tracker_db_interface_execute_query (iface, NULL, "PRAGMA journal_size_limit = 10240000");

	if (page_size != TRACKER_DB_PAGE_SIZE_DONT_SET) {
		g_info ("  Setting page size to %d", page_size);
		tracker_db_interface_execute_query (iface, NULL, "PRAGMA page_size = %d", page_size);
	}

	tracker_db_interface_execute_query (iface, NULL, "PRAGMA cache_size = %d", cache_size);
	g_info ("  Setting cache size to %d", cache_size);
}

void
tracker_db_manager_remove_all (TrackerDBManager *db_manager)
{
	gchar *filename;

	g_info ("Removing all database/storage files");

	g_info ("  Removing database:'%s'", db_manager->db.abs_filename);
	g_unlink (db_manager->db.abs_filename);

	/* also delete shm and wal helper files */
	filename = g_strdup_printf ("%s-shm", db_manager->db.abs_filename);
	g_unlink (filename);
	g_free (filename);

	filename = g_strdup_printf ("%s-wal", db_manager->db.abs_filename);
	g_unlink (filename);
	g_free (filename);

	/* Remove locale file also */
	db_remove_locale_file (db_manager);
}

static TrackerDBVersion
db_get_version (TrackerDBManager *db_manager)
{
	TrackerDBVersion  version;
	gchar            *filename;

	filename = g_build_filename (db_manager->data_dir, TRACKER_DB_VERSION_FILE, NULL);

	if (G_LIKELY (g_file_test (filename, G_FILE_TEST_EXISTS))) {
		gchar *contents;

		/* Check version is correct */
		if (G_LIKELY (g_file_get_contents (filename, &contents, NULL, NULL))) {
			if (contents && strlen (contents) <= 2) {
				version = atoi (contents);
			} else {
				g_info ("  Version file content size is either 0 or bigger than expected");

				version = TRACKER_DB_VERSION_UNKNOWN;
			}

			g_free (contents);
		} else {
			g_info ("  Could not get content of file '%s'", filename);

			version = TRACKER_DB_VERSION_UNKNOWN;
		}
	} else {
		g_info ("  Could not find database version file:'%s'", filename);
		g_info ("  Current databases are either old or no databases are set up yet");

		version = TRACKER_DB_VERSION_UNKNOWN;
	}

	g_free (filename);

	return version;
}

void
tracker_db_manager_create_version_file (TrackerDBManager *db_manager)
{
	GError *error = NULL;
	gchar  *filename;
	gchar  *str;

	filename = g_build_filename (db_manager->data_dir, TRACKER_DB_VERSION_FILE, NULL);
	g_info ("  Creating version file '%s'", filename);

	str = g_strdup_printf ("%d", TRACKER_DB_VERSION_NOW);

	if (!g_file_set_contents (filename, str, -1, &error)) {
		g_info ("  Could not set file contents, %s",
		        error ? error->message : "no error given");
		g_clear_error (&error);
	}

	g_free (str);
	g_free (filename);
}

void
tracker_db_manager_remove_version_file (TrackerDBManager *db_manager)
{
	gchar *filename;

	filename = g_build_filename (db_manager->data_dir, TRACKER_DB_VERSION_FILE, NULL);
	g_info ("  Removing db-version file:'%s'", filename);
	g_unlink (filename);
	g_free (filename);
}

static void
db_remove_locale_file (TrackerDBManager *db_manager)
{
	gchar *filename;

	filename = g_build_filename (db_manager->data_dir, TRACKER_DB_LOCALE_FILE, NULL);
	g_info ("  Removing db-locale file:'%s'", filename);
	g_unlink (filename);
	g_free (filename);
}

static gchar *
db_get_locale (TrackerDBManager *db_manager)
{
	gchar *locale = NULL;
	gchar *filename;

	filename = g_build_filename (db_manager->data_dir, TRACKER_DB_LOCALE_FILE, NULL);

	if (G_LIKELY (g_file_test (filename, G_FILE_TEST_EXISTS))) {
		gchar *contents;

		/* Check locale is correct */
		if (G_LIKELY (g_file_get_contents (filename, &contents, NULL, NULL))) {
			if (contents && strlen (contents) == 0) {
				g_critical ("  Empty locale file found at '%s'", filename);
				g_free (contents);
			} else {
				/* Re-use contents */
				locale = contents;
			}
		} else {
			g_critical ("  Could not get content of file '%s'", filename);
		}
	} else {
		/* expected when restoring from backup, always recreate indices */
		g_info ("  Could not find database locale file:'%s'", filename);
		locale = g_strdup ("unknown");
	}

	g_free (filename);

	return locale;
}

static void
db_set_locale (TrackerDBManager *db_manager,
	       const gchar      *locale)
{
	GError *error = NULL;
	gchar  *filename;
	gchar  *str;

	filename = g_build_filename (db_manager->data_dir, TRACKER_DB_LOCALE_FILE, NULL);
	g_info ("  Creating locale file '%s'", filename);

	str = g_strdup_printf ("%s", locale ? locale : "");

	if (!g_file_set_contents (filename, str, -1, &error)) {
		g_info ("  Could not set file contents, %s",
		        error ? error->message : "no error given");
		g_clear_error (&error);
	}

	g_free (str);
	g_free (filename);
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
	tracker_db_manager_ensure_locations (db_manager, db_manager->cache_location, db_manager->data_location);

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
		             db_locale, current_locale);
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
	gchar *locale;
	GError *internal_error = NULL;

	/* We call an internal version of this function here
	 * because at the time 'initialized' = FALSE and that
	 * will cause errors and do nothing.
	 */
	g_info ("Cleaning up database files for reindex");

	tracker_db_manager_remove_all (db_manager);

	/* Now create the databases and close them */
	g_info ("Creating database files, this may take a few moments...");

	db_manager->db.iface = tracker_db_manager_create_db_interface (db_manager, FALSE, &internal_error);
	if (internal_error) {
		g_propagate_error (error, internal_error);
		return;
	}

	g_clear_object (&db_manager->db.iface);
	g_clear_object (&db_manager->db.wal_iface);

	locale = tracker_locale_get (TRACKER_LOCALE_COLLATE);
	/* Initialize locale file */
	db_set_locale (db_manager, locale);
	g_free (locale);
}

void
tracker_db_manager_ensure_locations (TrackerDBManager *db_manager,
                                     GFile            *cache_location,
                                     GFile            *data_location)
{
	gchar *dir;

	if (db_manager->locations_initialized) {
		return;
	}

	db_manager->locations_initialized = TRUE;
	db_manager->data_dir = g_file_get_path (cache_location);

	/* For DISABLE_JOURNAL case we should use g_get_user_data_dir here. For now
	 * keeping this as-is */
	db_manager->user_data_dir = g_file_get_path (data_location);

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
			GFile                  *data_location,
			gboolean               *first_time,
			gboolean                restoring_backup,
			gboolean                shared_cache,
			guint                   select_cache_size,
			guint                   update_cache_size,
			TrackerBusyCallback     busy_callback,
			gpointer                busy_user_data,
			const gchar            *busy_operation,
                        GObject                *iface_data,
			GError                **error)
{
	TrackerDBManager *db_manager;
	TrackerDBVersion version;
	gboolean need_reindex;
	int in_use_file;
	gboolean loaded = FALSE;
	TrackerDBInterface *resources_iface;
	GError *internal_error = NULL;

	if (!cache_location || !data_location) {
		g_set_error (error,
		             TRACKER_DATA_ONTOLOGY_ERROR,
		             TRACKER_DATA_UNSUPPORTED_LOCATION,
		             "All data storage and ontology locations must be provided");
		return NULL;
	}

	db_manager = g_new0 (TrackerDBManager, 1);

	/* First set defaults for return values */
	if (first_time) {
		*first_time = FALSE;
	}

	need_reindex = FALSE;

	/* Set up locations */
	g_info ("Setting database locations");

	db_manager->flags = flags;
	db_manager->s_cache_size = select_cache_size;
	db_manager->u_cache_size = update_cache_size;
	db_manager->interfaces = g_async_queue_new_full (g_object_unref);

	g_set_object (&db_manager->cache_location, cache_location);
	g_set_object (&db_manager->data_location, data_location);
	g_weak_ref_init (&db_manager->iface_data, iface_data);

	tracker_db_manager_ensure_locations (db_manager, cache_location, data_location);
	db_manager->in_use_filename = g_build_filename (db_manager->user_data_dir,
							IN_USE_FILENAME,
							NULL);

	/* Don't do need_reindex checks for readonly (direct-access) */
	if ((flags & TRACKER_DB_MANAGER_READONLY) == 0) {

		/* Make sure the directories exist */
		g_info ("Checking database directories exist");

		g_mkdir_with_parents (db_manager->data_dir, 00755);
		g_mkdir_with_parents (db_manager->user_data_dir, 00755);

		g_info ("Checking database version");

		version = db_get_version (db_manager);

		if (version < TRACKER_DB_VERSION_NOW) {
			g_info ("  A reindex will be forced");
			need_reindex = TRUE;
		}

		if (need_reindex) {
			tracker_db_manager_create_version_file (db_manager);
		}
	}

	g_info ("Checking whether database files exist");

	/* Check we have the database in place, if it is
	 * missing, we reindex.
	 *
	 * There's no need to check for files not existing (for
	 * reindex) if reindexing is already needed.
	 */
	if (!need_reindex &&
	    !g_file_test (db_manager->db.abs_filename, G_FILE_TEST_EXISTS)) {
		if ((flags & TRACKER_DB_MANAGER_READONLY) == 0) {
			g_info ("Could not find database file:'%s', reindex will be forced", db_manager->db.abs_filename);
			need_reindex = TRUE;
		} else {
			g_set_error (error,
			             TRACKER_DB_INTERFACE_ERROR,
			             TRACKER_DB_OPEN_ERROR,
			             "Could not find database file:'%s'.", db_manager->db.abs_filename);

			tracker_db_manager_free (db_manager);
			return NULL;
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

			tracker_db_manager_free (db_manager);
			return NULL;
		}

		perform_recreate (db_manager, first_time, &internal_error);

		if (internal_error) {
			g_propagate_error (error, internal_error);
			tracker_db_manager_free (db_manager);
			return NULL;
		}

		/* Load databases */
		g_info ("Loading databases files...");

	} else if ((flags & TRACKER_DB_MANAGER_READONLY) == 0) {
		/* do not do shutdown check for read-only mode (direct access) */
		gboolean must_recreate = FALSE;

		/* Load databases */
		g_info ("Loading databases files...");

#ifndef DISABLE_JOURNAL
		must_recreate = !tracker_db_journal_reader_verify_last (data_location,
		                                                        NULL);
#endif /* DISABLE_JOURNAL */

		if (!must_recreate && g_file_test (db_manager->in_use_filename, G_FILE_TEST_EXISTS)) {
			gsize size = 0;
			struct stat st;
			TrackerDBStatement *stmt;
#ifndef DISABLE_JOURNAL
			gchar *busy_status;
#endif /* DISABLE_JOURNAL */

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
					tracker_db_manager_free (db_manager);
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
						tracker_db_manager_free (db_manager);
						return NULL;
					}
				}
			}

			if (!must_recreate) {
				db_manager->db.mtime = tracker_file_get_mtime (db_manager->db.abs_filename);

				loaded = TRUE;

#ifndef DISABLE_JOURNAL
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
#endif /* DISABLE_JOURNAL */
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
			g_info ("Database severely damaged. We will recreate it"
#ifndef DISABLE_JOURNAL
			        " and replay the journal if available.");
#else
			        ".");
#endif /* DISABLE_JOURNAL */

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
				tracker_db_manager_free (db_manager);
				return NULL;
			}
		} else {
			g_propagate_error (error, internal_error);
			tracker_db_manager_free (db_manager);
			return NULL;
		}
	}

	g_clear_object (&resources_iface);

	return db_manager;
}

void
tracker_db_manager_free (TrackerDBManager *db_manager)
{
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
	g_free (db_manager);
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

	db_set_params (connection,
	               db_manager->db.cache_size,
	               db_manager->db.page_size,
	               readonly,
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

	if (!interface) {
		/* Create a new one to satisfy the request */
		interface = tracker_db_manager_create_db_interface (db_manager,
		                                                    TRUE, &internal_error);

		if (interface) {
#if HAVE_TRACKER_FTS
			tracker_data_manager_init_fts (interface, FALSE);
#endif
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
wal_checkpoint (TrackerDBInterface *iface,
                gboolean            blocking)
{
	GError *error = NULL;

	g_debug ("Checkpointing database...");

	tracker_db_interface_sqlite_wal_checkpoint (iface, blocking,
	                                            blocking ? &error : NULL);

	if (error) {
		g_warning ("Error in %s WAL checkpoint: %s",
			   blocking ? "blocking" : "deferred",
			   error->message);
		g_error_free (error);
	}

	g_debug ("Checkpointing complete");
}

static gpointer
wal_checkpoint_thread (gpointer data)
{
	TrackerDBManager *db_manager = data;

	if (!db_manager->db.wal_iface)
		db_manager->db.wal_iface = init_writable_db_interface (db_manager);

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

inline static gchar *
get_parser_version_filename (TrackerDBManager *db_manager)
{
	return g_build_filename (db_manager->data_dir,
	                         PARSER_VERSION_FILENAME,
	                         NULL);
}


gboolean
tracker_db_manager_get_tokenizer_changed (TrackerDBManager *db_manager)
{
	gchar *filename, *version;
	gboolean changed = TRUE;

	filename = get_parser_version_filename (db_manager);

	if (g_file_get_contents (filename, &version, NULL, NULL)) {
		changed = strcmp (version, TRACKER_PARSER_VERSION_STRING) != 0;
		g_free (version);
	}

	g_free (filename);

	return changed;
}

void
tracker_db_manager_tokenizer_update (TrackerDBManager *db_manager)
{
	GError *error = NULL;
	gchar *filename;

	filename = get_parser_version_filename (db_manager);

	if (!g_file_set_contents (filename, TRACKER_PARSER_VERSION_STRING, -1, &error)) {
		g_warning ("The file '%s' could not be rewritten by Tracker and "
		           "should be deleted manually. Not doing so will result "
		           "in Tracker rebuilding its FTS tokens on every startup. "
		           "The error received was: '%s'", filename, error->message);
		g_error_free (error);
	}

	g_free (filename);
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

void
tracker_db_manager_set_vtab_user_data (TrackerDBManager *db_manager,
                                       gpointer          user_data)
{
	db_manager->vtab_data = user_data;
}
