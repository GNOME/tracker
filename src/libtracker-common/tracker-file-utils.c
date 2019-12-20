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

#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/file.h>
#include <fcntl.h>
#include <limits.h>
#include <errno.h>

#ifdef __linux__
#include <sys/statfs.h>
#endif

#include <glib.h>
#include <glib/gstdio.h>
#include <gio/gio.h>

#include "tracker-log.h"
#include "tracker-file-utils.h"
#include "tracker-type-utils.h"

#define TEXT_SNIFF_SIZE 4096

int
tracker_file_open_fd (const gchar *path)
{
	int fd;

	g_return_val_if_fail (path != NULL, -1);

#if defined(__linux__)
	fd = g_open (path, O_RDONLY | O_NOATIME, 0);
	if (fd == -1 && errno == EPERM) {
		fd = g_open (path, O_RDONLY, 0);
	}
#else
	fd = g_open (path, O_RDONLY, 0);
#endif

	return fd;
}

FILE *
tracker_file_open (const gchar *path)
{
	FILE *file;
	int fd;

	g_return_val_if_fail (path != NULL, NULL);

	fd = tracker_file_open_fd (path);

	if (fd == -1) {
		return NULL;
	}

	file = fdopen (fd, "r");

	if (!file) {
		return NULL;
	}

	return file;
}

void
tracker_file_close (FILE     *file,
                    gboolean  need_again_soon)
{
	g_return_if_fail (file != NULL);

#ifdef HAVE_POSIX_FADVISE
	if (!need_again_soon) {
		if (posix_fadvise (fileno (file), 0, 0, POSIX_FADV_DONTNEED) != 0)
			g_warning ("posix_fadvise() call failed: %m");
	}
#endif /* HAVE_POSIX_FADVISE */

	fclose (file);
}

goffset
tracker_file_get_size (const gchar *path)
{
	GFileInfo *info;
	GFile     *file;
	GError    *error = NULL;
	goffset    size;

	g_return_val_if_fail (path != NULL, 0);

	file = g_file_new_for_path (path);
	info = g_file_query_info (file,
	                          G_FILE_ATTRIBUTE_STANDARD_SIZE,
	                          G_FILE_QUERY_INFO_NONE,
	                          NULL,
	                          &error);

	if (G_UNLIKELY (error)) {
		gchar *uri;

		uri = g_file_get_uri (file);
		g_message ("Could not get size for '%s', %s",
		           uri,
		           error->message);
		g_free (uri);
		g_error_free (error);
		size = 0;
	} else {
		size = g_file_info_get_size (info);
		g_object_unref (info);
	}

	g_object_unref (file);

	return size;
}

static
guint64
file_get_mtime (GFile *file)
{
	GFileInfo *info;
	GError    *error = NULL;
	guint64    mtime;

	info = g_file_query_info (file,
	                          G_FILE_ATTRIBUTE_TIME_MODIFIED,
	                          G_FILE_QUERY_INFO_NONE,
	                          NULL,
	                          &error);

	if (G_UNLIKELY (error)) {
		gchar *uri;

		uri = g_file_get_uri (file);
		g_message ("Could not get mtime for '%s': %s",
		           uri,
		           error->message);
		g_free (uri);
		g_error_free (error);
		mtime = 0;
	} else {
		mtime = g_file_info_get_attribute_uint64 (info, G_FILE_ATTRIBUTE_TIME_MODIFIED);
		g_object_unref (info);
	}

	return mtime;
}

guint64
tracker_file_get_mtime (const gchar *path)
{
	GFile     *file;
	guint64    mtime;

	g_return_val_if_fail (path != NULL, 0);

	file = g_file_new_for_path (path);

	mtime = file_get_mtime (file);

	g_object_unref (file);

	return mtime;
}


guint64
tracker_file_get_mtime_uri (const gchar *uri)
{
	GFile     *file;
	guint64    mtime;

	g_return_val_if_fail (uri != NULL, 0);

	file = g_file_new_for_uri (uri);

	mtime = file_get_mtime (file);

	g_object_unref (file);

	return mtime;
}

gchar *
tracker_file_get_mime_type (GFile *file)
{
	GFileInfo *info;
	GError    *error = NULL;
	gchar     *content_type;

	g_return_val_if_fail (G_IS_FILE (file), NULL);

	info = g_file_query_info (file,
	                          G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE,
	                          G_FILE_QUERY_INFO_NONE,
	                          NULL,
	                          &error);

	if (G_UNLIKELY (error)) {
		gchar *uri;

		uri = g_file_get_uri (file);
		g_message ("Could not guess mimetype for '%s', %s",
		           uri,
		           error->message);
		g_free (uri);
		g_error_free (error);
		content_type = NULL;
	} else {
		content_type = g_strdup (g_file_info_get_content_type (info));
		g_object_unref (info);
	}

	return content_type ? content_type : g_strdup ("unknown");
}

#ifdef __linux__

#define __bsize f_bsize

#ifdef __USE_LARGEFILE64
#define __statvfs statfs64
#else
#define __statvfs statfs
#endif

#else /* __linux__ */

#define __bsize f_frsize

#ifdef HAVE_STATVFS64
#define __statvfs statvfs64
#else
#define __statvfs statvfs
#endif

#endif /* __linux__ */

static gboolean
statvfs_helper (const gchar *path, struct __statvfs *st)
{
	gchar *_path;
	int retval;

//LCOV_EXCL_START
	/* Iterate up the path to the root until statvfs() doesn’t error with
	 * ENOENT. This prevents the call failing on first-startup when (for
	 * example) ~/.cache/tracker might not exist. */
	_path = g_strdup (path);

	while ((retval = __statvfs (_path, st)) == -1 && errno == ENOENT) {
		gchar *tmp = g_path_get_dirname (_path);
		g_free (_path);
		_path = tmp;
	}

	g_free (_path);
//LCOV_EXCL_STOP

	if (retval == -1) {
		g_critical ("Could not statvfs() '%s': %s",
		            path,
		            g_strerror (errno));
	}

	return (retval == 0);
}

guint64
tracker_file_system_get_remaining_space (const gchar *path)
{
	struct __statvfs st;
	guint64 available;

	if (statvfs_helper (path, &st)) {
		available = (geteuid () == 0) ? st.f_bfree : st.f_bavail;
		/* __bsize is a platform dependent #define above */
		return st.__bsize * available;
	} else {
		return 0;
	}
}

gdouble
tracker_file_system_get_remaining_space_percentage (const gchar *path)
{
	struct __statvfs st;
	guint64 available;

	if (statvfs_helper (path, &st)) {
		available = (geteuid () == 0) ? st.f_bfree : st.f_bavail;
		return (((gdouble) available * 100) / st.f_blocks);
	} else {
		return 0.0;
	}
}

gboolean
tracker_file_system_has_enough_space (const gchar *path,
                                      gulong       required_bytes,
                                      gboolean     creating_db)
{
	gchar *str1;
	gchar *str2;
	gboolean enough;
	guint64 remaining;

	g_return_val_if_fail (path != NULL, FALSE);

	remaining = tracker_file_system_get_remaining_space (path);
	enough = (remaining >= required_bytes);

	if (creating_db) {
		str1 = g_format_size (required_bytes);
		str2 = g_format_size (remaining);

		if (!enough) {
			g_critical ("Not enough disk space to create databases, "
			            "%s remaining, %s required as a minimum",
			            str2,
			            str1);
		} else {
			g_debug ("Checking for adequate disk space to create databases, "
			         "%s remaining, %s required as a minimum",
			         str2,
			         str1);
		}

		g_free (str2);
		g_free (str1);
	}

	return enough;
}

gboolean
tracker_path_is_in_path (const gchar *path,
                         const gchar *in_path)
{
	gchar    *new_path;
	gchar    *new_in_path;
	gboolean  is_in_path = FALSE;

	g_return_val_if_fail (path != NULL, FALSE);
	g_return_val_if_fail (in_path != NULL, FALSE);

	if (!g_str_has_suffix (path, G_DIR_SEPARATOR_S)) {
		new_path = g_strconcat (path, G_DIR_SEPARATOR_S, NULL);
	} else {
		new_path = g_strdup (path);
	}

	if (!g_str_has_suffix (in_path, G_DIR_SEPARATOR_S)) {
		new_in_path = g_strconcat (in_path, G_DIR_SEPARATOR_S, NULL);
	} else {
		new_in_path = g_strdup (in_path);
	}

	if (g_str_has_prefix (new_path, new_in_path)) {
		is_in_path = TRUE;
	}

	g_free (new_in_path);
	g_free (new_path);

	return is_in_path;
}

GSList *
tracker_path_list_filter_duplicates (GSList      *roots,
                                     const gchar *basename_exception_prefix,
                                     gboolean     is_recursive)
{
	GSList *l1, *l2;
	GSList *new_list;

	new_list = tracker_gslist_copy_with_string_data (roots);
	l1 = new_list;

	while (l1) {
		const gchar *path;
		gchar       *p;
		gboolean     reset = FALSE;

		path = l1->data;

		l2 = new_list;

		while (l2 && !reset) {
			const gchar *in_path;

			in_path = l2->data;

			if (path == in_path) {
				/* Do nothing */
				l2 = l2->next;
				continue;
			}

			if (basename_exception_prefix) {
				gchar *lbasename;
				gboolean has_prefix = FALSE;

				lbasename = g_path_get_basename (path);
				if (!g_str_has_prefix (lbasename, basename_exception_prefix)) {
					g_free (lbasename);

					lbasename = g_path_get_basename (in_path);
					if (g_str_has_prefix (lbasename, basename_exception_prefix)) {
						has_prefix = TRUE;
					}
				} else {
					has_prefix = TRUE;
				}

				g_free (lbasename);

				/* This is so we can ignore this check
				 * on files which prefix with ".".
				 */
				if (has_prefix) {
					l2 = l2->next;
					continue;
				}
			}

			if (is_recursive && tracker_path_is_in_path (path, in_path)) {
				g_debug ("Removing path:'%s', it is in path:'%s'",
				         path, in_path);

				g_free (l1->data);
				new_list = g_slist_delete_link (new_list, l1);
				l1 = new_list;

				reset = TRUE;

				continue;
			} else if (is_recursive && tracker_path_is_in_path (in_path, path)) {
				g_debug ("Removing path:'%s', it is in path:'%s'",
				         in_path, path);

				g_free (l2->data);
				new_list = g_slist_delete_link (new_list, l2);
				l2 = new_list;

				reset = TRUE;

				continue;
			}

			l2 = l2->next;
		}

		if (G_LIKELY (!reset)) {
			p = strrchr (path, G_DIR_SEPARATOR);

			/* Make sure the path doesn't have the '/' suffix. */
			if (p && !p[1]) {
				*p = '\0';
			}

			l1 = l1->next;
		}
	}

#ifdef TESTING
	g_debug ("GSList paths were filtered down to:");

	if (TRUE) {
		GSList *l;

		for (l = new_list; l; l = l->next) {
			g_debug ("  %s", (gchar*) l->data);
		}
	}
#endif /* TESTING */

	return new_list;
}

const struct {
	const gchar *symbol;
	GUserDirectory user_dir;
} special_dirs[] = {
	{"&DESKTOP",      G_USER_DIRECTORY_DESKTOP},
	{"&DOCUMENTS",    G_USER_DIRECTORY_DOCUMENTS},
	{"&DOWNLOAD",     G_USER_DIRECTORY_DOWNLOAD},
	{"&MUSIC",        G_USER_DIRECTORY_MUSIC},
	{"&PICTURES",     G_USER_DIRECTORY_PICTURES},
	{"&PUBLIC_SHARE", G_USER_DIRECTORY_PUBLIC_SHARE},
	{"&TEMPLATES",    G_USER_DIRECTORY_TEMPLATES},
	{"&VIDEOS",       G_USER_DIRECTORY_VIDEOS}
};


static gboolean
get_user_special_dir_if_not_home (const gchar  *path,
                                  gchar       **special_dir)
{
	int i;
	const gchar *real_path;
	GFile *home, *file;

	real_path = NULL;
	*special_dir = NULL;

	for (i = 0; i < G_N_ELEMENTS(special_dirs); i++) {
		if (strcmp (path, special_dirs[i].symbol) == 0) {
			real_path = g_get_user_special_dir (special_dirs[i].user_dir);

			if (real_path == NULL) {
				g_warning ("Unable to get XDG user directory path for special "
				           "directory %s. Ignoring this location.", path);
			}

			break;
		}
	}

	if (real_path == NULL)
		return FALSE;

	file = g_file_new_for_path (real_path);
	home = g_file_new_for_path (g_get_home_dir ());

	/* ignore XDG directories set to $HOME */
	if (!g_file_equal (file, home)) {
		*special_dir = g_strdup (real_path);
	}

	g_object_unref (file);
	g_object_unref (home);

	return TRUE;
}


gchar *
tracker_path_evaluate_name (const gchar *path)
{
	gchar        *special_dir_path;
	gchar        *final_path;
	gchar       **tokens;
	gchar       **token;
	gchar        *start;
	gchar        *end;
	const gchar  *env;
	gchar        *expanded;

	if (!path || path[0] == '\0') {
		return NULL;
	}

	/* See if it is a special directory name. */
	if (get_user_special_dir_if_not_home (path, &special_dir_path))
		return special_dir_path;

	/* First check the simple case of using tilde */
	if (path[0] == '~') {
		const gchar *home;

		home = g_getenv ("HOME");
		if (! home) {
			home = g_get_home_dir ();
		}

		if (!home || home[0] == '\0') {
			return NULL;
		}

		return g_build_path (G_DIR_SEPARATOR_S,
		                     home,
		                     path + 1,
		                     NULL);
	}

	/* Second try to find any environment variables and expand
	 * them, like $HOME or ${FOO}
	 */
	tokens = g_strsplit (path, G_DIR_SEPARATOR_S, -1);

	for (token = tokens; *token; token++) {
		if (**token != '$') {
			continue;
		}

		start = *token + 1;

		if (*start == '{') {
			start++;
			end = start + (strlen (start)) - 1;
			*end='\0';
		}

		env = g_getenv (start);
		g_free (*token);

		/* Don't do g_strdup (s?s1:s2) as that doesn't work
		 * with certain gcc 2.96 versions.
		 */
		*token = env ? g_strdup (env) : g_strdup ("");
	}

	/* Third get the real path removing any "../" and other
	 * symbolic links to other places, returning only the REAL
	 * location.
	 */
	expanded = g_strjoinv (G_DIR_SEPARATOR_S, tokens);
	g_strfreev (tokens);

	/* Only resolve relative paths if there is a directory
	 * separator in the path, otherwise it is just a name.
	 */
	if (strchr (expanded, G_DIR_SEPARATOR)) {
		GFile *file;

		file = g_file_new_for_commandline_arg (expanded);
		final_path = g_file_get_path (file);
		g_object_unref (file);
		g_free (expanded);
	} else {
		final_path = expanded;
	}

	return final_path;
}

gboolean
tracker_file_is_hidden (GFile *file)
{
	GFileInfo *file_info;
	gboolean is_hidden = FALSE;

	file_info = g_file_query_info (file,
	                               G_FILE_ATTRIBUTE_STANDARD_IS_HIDDEN,
	                               G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
	                               NULL, NULL);
	if (file_info) {
		/* Check if GIO says the file is hidden */
		is_hidden = g_file_info_get_is_hidden (file_info);
		g_object_unref (file_info);
	} else {
		gchar *basename;

		/* Resort last to basename checks, this might happen on
		 * already deleted files.
		 */
		basename = g_file_get_basename (file);
		is_hidden = basename[0] == '.';
		g_free (basename);
	}

	return is_hidden;
}

gint
tracker_file_cmp (GFile *file_a,
                  GFile *file_b)
{
	/* Returns 0 if files are equal.
	 * Useful to be used in g_list_find_custom() or g_queue_find_custom() */
	return !g_file_equal (file_a, file_b);
}

/**
 * tracker_filename_casecmp_without_extension:
 * @a: a string containing a file name
 * @b: filename to be compared with @a
 *
 * This function performs a case-insensitive comparison of @a and @b.
 * Additionally, text beyond the last '.' in a string is not considered
 * part of the match, so for example given the inputs "file.mp3" and
 * "file.wav" this function will return %TRUE.
 *
 * Internally, the g_ascii_tolower() function is used - this means that
 * @a and @b must be in an encoding in which ASCII characters always
 * represent themselves, such as UTF-8 or the ISO-8859-* charsets.
 *
 * Returns: %TRUE if the two file names match.
 **/
gboolean
tracker_filename_casecmp_without_extension (const gchar *a,
                                            const gchar *b)
{
	gchar *pa;
	gchar *pb;
	gint len_a;
	gint len_b;

	g_return_val_if_fail (a != NULL, FALSE);
	g_return_val_if_fail (b != NULL, FALSE);

	pa = strrchr (a, '.');
	pb = strrchr (b, '.');

	/* Did we find a "." */
	if (pa) {
		len_a = pa - a;
	} else {
		len_a = -1;
	}

	if (pb) {
		len_b = pb - b;
	} else {
		len_b = -1;
	}

	/* If one has a "." and the other doesn't, we do length
	 * comparison with strlen() which is less optimal but this is
	 * not a case we consider common operation.
	 */
	if (len_a == -1 && len_b > -1) {
		len_a = strlen (a);
	} else if (len_b == -1 && len_a > -1) {
		len_b = strlen (b);
	}

	/* If we have length for both and it's different then these
	 * strings are not the same. If we have no length for the
	 * strings then it's a simple -1 != -1 comparison.
	 */
	if (len_a != len_b) {
		return FALSE;
	}

	/* Now we know we either have the same length string or no
	 * extension in a and b, meaning it's a strcmp() of the
	 * string only. We test only len_a or len_b here for that:
	 */
	if (G_UNLIKELY (len_a == -1)) {
		return g_ascii_strcasecmp (a, b) == 0;
	}

	return g_ascii_strncasecmp (a, b, len_a) == 0;
}
