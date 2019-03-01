/*
 * Copyright (C) 2006, Jamie McCracken <jamiemcc@gnome.org>
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

#include <errno.h>
#include <string.h>

#include <glib.h>
#include <glib/gstdio.h>
#include <glib/gi18n.h>

#include "tracker-data-backup.h"
#include "tracker-data-manager.h"
#include "tracker-db-manager.h"
#include "tracker-db-backup.h"

typedef struct {
	GFile *destination, *journal;
	TrackerDataBackupFinished callback;
	gpointer user_data;
	GDestroyNotify destroy;
	GError *error;
} BackupSaveInfo;

static void
free_backup_save_info (BackupSaveInfo *info)
{
	if (info->destination) {
		g_object_unref (info->destination);
	}

	if (info->journal) {
		g_object_unref (info->journal);
	}

	if (info->destroy) {
		info->destroy (info->user_data);
	}

	g_clear_error (&info->error);

	g_free (info);
}


GQuark
tracker_data_backup_error_quark (void)
{
	return g_quark_from_static_string (TRACKER_DATA_BACKUP_ERROR_DOMAIN);
}

static void
on_backup_finished (GError *error,
                    gpointer user_data)
{
	BackupSaveInfo *info = user_data;

	if (info->callback) {
		info->callback (error, info->user_data);
	}

	free_backup_save_info (info);
}

/* delete all regular files from the directory */
static void
dir_remove_files (const gchar *path)
{
	GDir *dir;
	const gchar *name;

	dir = g_dir_open (path, 0, NULL);
	if (dir == NULL) {
		return;
	}

	while ((name = g_dir_read_name (dir)) != NULL) {
		gchar *filename;

		filename = g_build_filename (path, name, NULL);

		if (g_file_test (filename, G_FILE_TEST_IS_REGULAR)) {
			g_debug ("Removing '%s'", filename);
			if (g_unlink (filename) == -1) {
				g_warning ("Unable to remove '%s': %s", filename, g_strerror (errno));
			}
		}

		g_free (filename);
	}

	g_dir_close (dir);
}

/* move all regular files from the source directory to the destination directory */
static void
dir_move_files (const gchar *src_path, const gchar *dest_path)
{
	GDir *src_dir;
	const gchar *src_name;

	src_dir = g_dir_open (src_path, 0, NULL);
	if (src_dir == NULL) {
		return;
	}

	while ((src_name = g_dir_read_name (src_dir)) != NULL) {
		gchar *src_filename, *dest_filename;

		src_filename = g_build_filename (src_path, src_name, NULL);

		if (g_file_test (src_filename, G_FILE_TEST_IS_REGULAR)) {
			dest_filename = g_build_filename (dest_path, src_name, NULL);

			g_debug ("Renaming '%s' to '%s'", src_filename, dest_filename);
			if (g_rename (src_filename, dest_filename) == -1) {
				g_warning ("Unable to rename '%s' to '%s': %s", src_filename, dest_filename, g_strerror (errno));
			}

			g_free (dest_filename);
		}

		g_free (src_filename);
	}

	g_dir_close (src_dir);
}

static void
dir_move_to_temp (const gchar *path,
                  const gchar *tmpname)
{
	gchar *temp_dir;

	temp_dir = g_build_filename (path, tmpname, NULL);
	if (g_mkdir (temp_dir, 0777) < 0) {
		g_critical ("Could not move %s to temp directory: %m",
			    path);
		g_free (temp_dir);
		return;
	}

	/* ensure that no obsolete temporary files are around */
	dir_remove_files (temp_dir);
	dir_move_files (path, temp_dir);

	g_free (temp_dir);
}

static void
move_to_temp (GFile *cache_location,
              GFile *data_location)
{
	gchar *data_dir, *cache_dir;

	g_info ("Moving all database files to temporary location");

	data_dir = g_file_get_path (data_location);
	cache_dir = g_file_get_path (cache_location);

	dir_move_to_temp (data_dir, "tmp.data");
	dir_move_to_temp (cache_dir, "tmp.cache");

	g_free (cache_dir);
	g_free (data_dir);
}

void
tracker_data_backup_save (TrackerDataManager        *data_manager,
                          GFile                     *destination,
                          GFile                     *data_location,
                          TrackerDataBackupFinished  callback,
                          gpointer                   user_data,
                          GDestroyNotify             destroy)
{
	BackupSaveInfo *info;
	TrackerDBManager *db_manager;
	GFile *db_file;

	info = g_new0 (BackupSaveInfo, 1);
	info->destination = g_object_ref (destination);
	info->callback = callback;
	info->user_data = user_data;
	info->destroy = destroy;

	db_manager = tracker_data_manager_get_db_manager (data_manager);
	db_file = g_file_new_for_path (tracker_db_manager_get_file (db_manager));

	tracker_db_backup_save (destination, db_file,
	                        on_backup_finished,
	                        info,
	                        NULL);

	g_object_unref (db_file);
}

void
tracker_data_backup_restore (TrackerDataManager   *manager,
                             GFile                *journal,
                             GFile                *cache_location,
                             GFile                *data_location,
                             GFile                *ontology_location,
                             TrackerBusyCallback   busy_callback,
                             gpointer              busy_user_data,
                             GError              **error)
{
	BackupSaveInfo *info;
	GError *internal_error = NULL;
	TrackerDBManager *db_manager = NULL;

	if (!cache_location || !data_location || !ontology_location) {
		g_set_error (error,
		             TRACKER_DATA_ONTOLOGY_ERROR,
		             TRACKER_DATA_UNSUPPORTED_LOCATION,
		             "All data storage and ontology locations must be provided");
		return;
	}

	db_manager = tracker_data_manager_get_db_manager (manager);
	info = g_new0 (BackupSaveInfo, 1);
	info->destination = g_file_new_for_path (tracker_db_manager_get_file (db_manager));

	info->journal = g_object_ref (journal);

	if (g_file_query_exists (info->journal, NULL)) {
		TrackerDBManagerFlags flags;
		guint select_cache_size, update_cache_size;

		flags = tracker_db_manager_get_flags (db_manager, &select_cache_size, &update_cache_size);

		//tracker_data_manager_shutdown ();

		move_to_temp (cache_location, data_location);

		/* Turn off force-reindex here, no journal to replay so it wouldn't work */
		flags &= ~TRACKER_DB_MANAGER_FORCE_REINDEX;

		g_file_copy (info->journal, info->destination,
		             G_FILE_COPY_OVERWRITE, 
		             NULL, NULL, NULL,
		             &info->error);

		tracker_db_manager_ensure_locations (db_manager, cache_location, data_location);

		/* Re-set the DB version file, so that its mtime changes. The mtime of this
		 * file will change only when the whole DB is recreated (after a hard reset
		 * or after a backup restoration). */
		tracker_db_manager_create_version_file (db_manager);

		manager = tracker_data_manager_new (flags, cache_location, data_location, ontology_location,
		                                    TRUE, TRUE, select_cache_size, update_cache_size);
		g_initable_init (G_INITABLE (manager), NULL, &internal_error);

		if (internal_error) {
			g_propagate_error (error, internal_error);
		}
	} else {
		g_set_error (&info->error, TRACKER_DATA_BACKUP_ERROR,
		             TRACKER_DATA_BACKUP_ERROR_UNKNOWN,
		             "Backup file doesn't exist");
	}

	if (info->error) {
		g_propagate_error (error, info->error);
		info->error = NULL;
	}

	free_backup_save_info (info);
}

