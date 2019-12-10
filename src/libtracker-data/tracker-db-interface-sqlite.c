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

#include <glib/gstdio.h>

#include <sqlite3.h>
#include <stdlib.h>
#include <math.h>
#include <errno.h>

#include <libtracker-common/tracker-date-time.h>
#include <libtracker-common/tracker-locale.h>
#include <libtracker-common/tracker-parser.h>

#include <libtracker-sparql/tracker-sparql.h>

#if HAVE_TRACKER_FTS
#include <libtracker-fts/tracker-fts.h>
#endif


#ifdef HAVE_LIBUNISTRING
/* libunistring versions prior to 9.1.2 need this hack */
#define _UNUSED_PARAMETER_
#include <unistr.h>
#include <unicase.h>
#elif defined(HAVE_LIBICU)
#include <unicode/utypes.h>
#include <unicode/uregex.h>
#include <unicode/ustring.h>
#include <unicode/ucol.h>
#include <unicode/unorm2.h>
#endif

#include "tracker-collation.h"

#include "tracker-db-interface-sqlite.h"
#include "tracker-db-manager.h"
#include "tracker-data-enum-types.h"
#include "tracker-uuid.h"
#include "tracker-vtab-triples.h"

typedef struct {
	TrackerDBStatement *head;
	TrackerDBStatement *tail;
	guint size;
	guint max;
} TrackerDBStatementLru;

typedef struct {
	GRegex *syntax_check;
	GRegex *replacement;
	GRegex *unescape;
} TrackerDBReplaceFuncChecks;

struct TrackerDBInterface {
	GObject parent_instance;

	gchar *filename;
	sqlite3 *db;

	GHashTable *dynamic_statements;

	/* Compiled regular expressions */
	TrackerDBReplaceFuncChecks replace_func_checks;

	/* Number of active cursors */
	gint n_active_cursors;

	guint flags;
	GCancellable *cancellable;

	TrackerDBStatementLru select_stmt_lru;
	TrackerDBStatementLru update_stmt_lru;

	gchar *fts_properties;

	/* Used if TRACKER_DB_INTERFACE_USE_MUTEX is set */
	GMutex mutex;

	/* Wal */
	TrackerDBWalCallback wal_hook;
	gpointer wal_hook_data;

	/* User data */
	gpointer user_data;
	GDestroyNotify user_data_destroy_notify;
};

struct TrackerDBInterfaceClass {
	GObjectClass parent_class;
};

struct TrackerDBCursor {
	TrackerSparqlCursor parent_instance;
	sqlite3_stmt *stmt;
	TrackerDBStatement *ref_stmt;
	gboolean finished;
	TrackerPropertyType *types;
	gint n_types;
	gchar **variable_names;
	gint n_variable_names;
};

struct TrackerDBCursorClass {
	TrackerSparqlCursorClass parent_class;
};

struct TrackerDBStatement {
	GInitiallyUnowned parent_instance;
	TrackerDBInterface *db_interface;
	sqlite3_stmt *stmt;
	gboolean stmt_is_used;
	TrackerDBStatement *next;
	TrackerDBStatement *prev;
};

struct TrackerDBStatementClass {
	GObjectClass parent_class;
};

static void                tracker_db_interface_initable_iface_init (GInitableIface        *iface);
static TrackerDBStatement *tracker_db_statement_sqlite_new          (TrackerDBInterface    *db_interface,
                                                                     sqlite3_stmt          *sqlite_stmt);
static void                tracker_db_statement_sqlite_reset        (TrackerDBStatement    *stmt);
static TrackerDBCursor    *tracker_db_cursor_sqlite_new             (TrackerDBStatement    *ref_stmt,
                                                                     TrackerPropertyType   *types,
                                                                     gint                   n_types,
                                                                     const gchar * const   *variable_names,
                                                                     gint                   n_variable_names);
static gboolean            tracker_db_cursor_get_boolean            (TrackerSparqlCursor   *cursor,
                                                                     guint                  column);
static gboolean            db_cursor_iter_next                      (TrackerDBCursor       *cursor,
                                                                     GCancellable          *cancellable,
                                                                     GError               **error);

enum {
	PROP_0,
	PROP_FILENAME,
	PROP_FLAGS
};

enum {
	TRACKER_DB_CURSOR_PROP_0,
	TRACKER_DB_CURSOR_PROP_N_COLUMNS
};

G_DEFINE_TYPE_WITH_CODE (TrackerDBInterface, tracker_db_interface, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE,
                                                tracker_db_interface_initable_iface_init));

G_DEFINE_TYPE (TrackerDBStatement, tracker_db_statement, G_TYPE_INITIALLY_UNOWNED)

G_DEFINE_TYPE (TrackerDBCursor, tracker_db_cursor, TRACKER_SPARQL_TYPE_CURSOR)

void
tracker_db_interface_sqlite_enable_shared_cache (void)
{
	sqlite3_enable_shared_cache (1);
}

static void
function_sparql_string_join (sqlite3_context *context,
                             int              argc,
                             sqlite3_value   *argv[])
{
	GString *str = NULL;
	const gchar *separator;
	gint i;

	/* fn:string-join (str1, str2, ..., separator) */

	if (sqlite3_value_type (argv[argc-1]) != SQLITE_TEXT) {
		sqlite3_result_error (context, "Invalid separator", -1);
		return;
	}

	separator = (gchar *)sqlite3_value_text (argv[argc-1]);

	for (i = 0;i < argc-1; i++) {
		if (sqlite3_value_type (argv[argc-1]) == SQLITE_TEXT) {
			const gchar *text = (gchar *)sqlite3_value_text (argv[i]);

			if (text != NULL) {
				if (!str) {
					str = g_string_new (text);
				} else {
					g_string_append_printf (str, "%s%s", separator, text);
				}
			}
		}
	}

	if (str) {
		sqlite3_result_text (context, str->str, str->len, g_free);
		g_string_free (str, FALSE);
	} else {
		sqlite3_result_null (context);
	}

	return;
}

/* Create a title-type string from the filename for replacing missing ones */
static void
function_sparql_string_from_filename (sqlite3_context *context,
                                      int              argc,
                                      sqlite3_value   *argv[])
{
	gchar  *name = NULL;
	gchar  *suffix = NULL;

	if (argc != 1) {
		sqlite3_result_error (context, "Invalid argument count", -1);
		return;
	}

	/* "/home/user/path/title_of_the_movie.movie" -> "title of the movie"
	 * Only for local files currently, do we need to change? */

	name = g_filename_display_basename ((gchar *)sqlite3_value_text (argv[0]));

	if (!name) {
		sqlite3_result_null (context);
		return;
	}

	suffix = g_strrstr (name, ".");

	if (suffix) {
		*suffix = '\0';
	}

	g_strdelimit (name, "._", ' ');

	sqlite3_result_text (context, name, -1, g_free);
}

static void
function_sparql_uri_is_parent (sqlite3_context *context,
                               int              argc,
                               sqlite3_value   *argv[])
{
	const gchar *uri, *parent, *remaining;
	gboolean match = FALSE;
	guint parent_len;

	if (argc != 2) {
		sqlite3_result_error (context, "Invalid argument count", -1);
		return;
	}

	parent = (gchar *)sqlite3_value_text (argv[0]);
	uri = (gchar *)sqlite3_value_text (argv[1]);

	if (!parent || !uri) {
		sqlite3_result_error (context, "Invalid arguments", -1);
		return;
	}

	parent_len = sqlite3_value_bytes (argv[0]);

	/* Check only one argument, it's going to
	 * be compared with the other anyway.
	 */

	if (!(parent_len >= 7 && (parent[4] == ':' && parent[5] == '/' && parent[6] == '/'))) {
		if (strstr (parent, "://") == NULL) {
			sqlite3_result_int (context, FALSE);
			return;
		}
	}

	/* Remove trailing '/', will
	 * be checked later on uri.
	 */
	while (parent[parent_len - 1] == '/') {
		parent_len--;
	}

	if (strncmp (uri, parent, parent_len) == 0 && uri[parent_len] == '/') {
		const gchar *slash;

		while (uri[parent_len] == '/') {
			parent_len++;
		}

		remaining = &uri[parent_len];

		if (*remaining == '\0') {
			/* Exact match, not a child */
			match = FALSE;
		} else if ((slash = strchr (remaining, '/')) == NULL) {
			/* Remaining doesn't have uri
			 * separator, it's a direct child.
			 */
			match = TRUE;
		} else {
			/* Check it's not trailing slashes */
			while (*slash == '/') {
				slash++;
			}

			match = (*slash == '\0');
		}
	}

	sqlite3_result_int (context, match);
}

static gboolean
check_uri_is_descendant (const gchar *parent,
                         guint        parent_len,
                         const gchar *uri)
{
	const gchar *remaining;
	gboolean match = FALSE;

	/* Check only one argument, it's going to
	 * be compared with the other anyway.
	 */

	if (!(parent_len >= 7 && (parent[4] == ':' && parent[5] == '/' && parent[6] == '/'))) {
		if (strstr (parent, "://") == NULL) {
			return FALSE;
		}
	}

	/* Remove trailing '/', will
	 * be checked later on uri.
	 */
	while (parent[parent_len - 1] == '/') {
		parent_len--;
	}

	if (strncmp (uri, parent, parent_len) == 0 && uri[parent_len] == '/') {
		while (uri[parent_len] == '/') {
			parent_len++;
		}

		remaining = &uri[parent_len];

		if (remaining && *remaining) {
			match = TRUE;
		}
	}

	return match;
}

static void
function_sparql_uri_is_descendant (sqlite3_context *context,
                                   int              argc,
                                   sqlite3_value   *argv[])
{
	const gchar *child;
	gboolean match = FALSE;
	gint i;

	/* fn:uri-is-descendant (parent1, parent2, ..., parentN, child) */

	if (argc < 2) {
		sqlite3_result_error (context, "Invalid argument count", -1);
		return;
	}

	for (i = 0; i < argc; i++) {
		if (sqlite3_value_type (argv[i]) == SQLITE_NULL) {
			sqlite3_result_int (context, FALSE);
			return;
		} else if (sqlite3_value_type (argv[i]) != SQLITE_TEXT) {
			sqlite3_result_error (context, "Invalid non-text argument", -1);
			return;
		}
	}

	child = (gchar *)sqlite3_value_text (argv[argc-1]);

	for (i = 0; i < argc - 1 && !match; i++) {
		if (sqlite3_value_type (argv[i]) == SQLITE_TEXT) {
			const gchar *parent = (gchar *)sqlite3_value_text (argv[i]);
			guint parent_len = sqlite3_value_bytes (argv[i]);

			if (!parent)
				continue;

			match = check_uri_is_descendant (parent, parent_len, child);
		}
	}

	sqlite3_result_int (context, match);
}

static void
function_sparql_format_time (sqlite3_context *context,
                             int              argc,
                             sqlite3_value   *argv[])
{
	gdouble seconds;
	gchar *str;

	if (argc != 1) {
		sqlite3_result_error (context, "Invalid argument count", -1);
		return;
	}

	if (sqlite3_value_type (argv[0]) == SQLITE_NULL) {
		sqlite3_result_null (context);
		return;
	}

	seconds = sqlite3_value_double (argv[0]);
	str = tracker_date_to_string (seconds);

	sqlite3_result_text (context, str, -1, g_free);
}

static void
function_sparql_cartesian_distance (sqlite3_context *context,
                                    int              argc,
                                    sqlite3_value   *argv[])
{
	gdouble lat1;
	gdouble lat2;
	gdouble lon1;
	gdouble lon2;

	gdouble R;
	gdouble a;
	gdouble b;
	gdouble c;
	gdouble d;

	if (argc != 4) {
		sqlite3_result_error (context, "Invalid argument count", -1);
		return;
	}

	lat1 = sqlite3_value_double (argv[0])*M_PI/180;
	lat2 = sqlite3_value_double (argv[1])*M_PI/180;
	lon1 = sqlite3_value_double (argv[2])*M_PI/180;
	lon2 = sqlite3_value_double (argv[3])*M_PI/180;

	R = 6371000;
	a = M_PI/2 - lat1;
	b = M_PI/2 - lat2;
	c = sqrt(a*a + b*b - 2*a*b*cos(lon2 - lon1));
	d = R*c;

	sqlite3_result_double (context, d);
}

static void
function_sparql_haversine_distance (sqlite3_context *context,
                                    int              argc,
                                    sqlite3_value   *argv[])
{
	gdouble lat1;
	gdouble lat2;
	gdouble lon1;
	gdouble lon2;

	gdouble R;
	gdouble dLat;
	gdouble dLon;
	gdouble a;
	gdouble c;
	gdouble d;

	if (argc != 4) {
		sqlite3_result_error (context, "Invalid argument count", -1);
		return;
	}

	lat1 = sqlite3_value_double (argv[0])*M_PI/180;
	lat2 = sqlite3_value_double (argv[1])*M_PI/180;
	lon1 = sqlite3_value_double (argv[2])*M_PI/180;
	lon2 = sqlite3_value_double (argv[3])*M_PI/180;

	R = 6371000;
	dLat = (lat2-lat1);
	dLon = (lon2-lon1);
	a = sin(dLat/2) * sin(dLat/2) + cos(lat1) * cos(lat2) *  sin(dLon/2) * sin(dLon/2);
	c = 2 * atan2(sqrt(a), sqrt(1-a));
	d = R * c;

	sqlite3_result_double (context, d);
}

static void
function_sparql_regex (sqlite3_context *context,
                       int              argc,
                       sqlite3_value   *argv[])
{
	gboolean ret;
	const gchar *text, *pattern, *flags = "";
	GRegexCompileFlags regex_flags;
	GRegex *regex;

	if (argc != 2 && argc != 3) {
		sqlite3_result_error (context, "Invalid argument count", -1);
		return;
	}

	regex = sqlite3_get_auxdata (context, 1);

	text = (gchar *)sqlite3_value_text (argv[0]);

	if (argc == 3)
		flags = (gchar *)sqlite3_value_text (argv[2]);

	if (regex == NULL) {
		gchar *err_str;
		GError *error = NULL;

		pattern = (gchar *)sqlite3_value_text (argv[1]);

		regex_flags = 0;
		while (*flags) {
			switch (*flags) {
			case 's':
				regex_flags |= G_REGEX_DOTALL;
				break;
			case 'm':
				regex_flags |= G_REGEX_MULTILINE;
				break;
			case 'i':
				regex_flags |= G_REGEX_CASELESS;
				break;
			case 'x':
				regex_flags |= G_REGEX_EXTENDED;
				break;
			default:
				err_str = g_strdup_printf ("Invalid SPARQL regex flag '%c'", *flags);
				sqlite3_result_error (context, err_str, -1);
				g_free (err_str);
				return;
			}
			flags++;
		}

		regex = g_regex_new (pattern, regex_flags, 0, &error);

		if (error) {
			sqlite3_result_error (context, error->message, -1);
			g_clear_error (&error);
			return;
		}

		sqlite3_set_auxdata (context, 1, regex, (void (*) (void*)) g_regex_unref);
	}

	if (text != NULL) {
		ret = g_regex_match (regex, text, 0, NULL);
	} else {
		ret = FALSE;
	}

	sqlite3_result_int (context, ret);
}

static void
ensure_replace_checks (TrackerDBInterface *db_interface)
{
	if (db_interface->replace_func_checks.syntax_check != NULL)
		return;

	db_interface->replace_func_checks.syntax_check =
		g_regex_new ("(?<!\\\\)\\$\\D", G_REGEX_OPTIMIZE, 0, NULL);
	db_interface->replace_func_checks.replacement =
		g_regex_new("(?<!\\\\)\\$(\\d)", G_REGEX_OPTIMIZE, 0, NULL);
	db_interface->replace_func_checks.unescape =
		g_regex_new("\\\\\\$", G_REGEX_OPTIMIZE, 0, NULL);
}

static void
function_sparql_replace (sqlite3_context *context,
                         int              argc,
                         sqlite3_value   *argv[])
{
	TrackerDBInterface *db_interface = sqlite3_user_data (context);
	TrackerDBReplaceFuncChecks *checks = &db_interface->replace_func_checks;
	gboolean store_regex = FALSE, store_replace_regex = FALSE;
	const gchar *input, *pattern, *replacement, *flags;
	gchar *err_str, *output = NULL, *replaced = NULL, *unescaped = NULL;
	GError *error = NULL;
	GRegexCompileFlags regex_flags = 0;
	GRegex *regex, *replace_regex;
	gint capture_count, i;

	ensure_replace_checks (db_interface);

	if (argc == 3) {
		flags = "";
	} else if (argc == 4) {
		flags = (gchar *)sqlite3_value_text (argv[3]);
	} else {
		sqlite3_result_error (context, "Invalid argument count", -1);
		return;
	}

	input = (gchar *)sqlite3_value_text (argv[0]);
	regex = sqlite3_get_auxdata (context, 1);
	replacement = (gchar *)sqlite3_value_text (argv[2]);

	if (regex == NULL) {
		pattern = (gchar *)sqlite3_value_text (argv[1]);

		for (i = 0; flags[i]; i++) {
			switch (flags[i]) {
			case 's':
				regex_flags |= G_REGEX_DOTALL;
				break;
			case 'm':
				regex_flags |= G_REGEX_MULTILINE;
				break;
			case 'i':
				regex_flags |= G_REGEX_CASELESS;
				break;
			case 'x':
				regex_flags |= G_REGEX_EXTENDED;
				break;
			default:
				err_str = g_strdup_printf ("Invalid SPARQL regex flag '%c'", flags[i]);
				sqlite3_result_error (context, err_str, -1);
				g_free (err_str);
				return;
			}
		}

		regex = g_regex_new (pattern, regex_flags, 0, &error);

		if (error) {
			sqlite3_result_error (context, error->message, -1);
			g_clear_error (&error);
			return;
		}

		/* According to the XPath 2.0 standard, an error shall be raised, if the given
		 * pattern matches a zero-length string.
		 */
		if (g_regex_match (regex, "", 0, NULL)) {
			err_str = g_strdup_printf ("The given pattern '%s' matches a zero-length string.",
			                           pattern);
			sqlite3_result_error (context, err_str, -1);
			g_regex_unref (regex);
			g_free (err_str);
			return;
		}

		store_regex = TRUE;
	}

	/* According to the XPath 2.0 standard, an error shall be raised, if all dollar
	 * signs ($) of the given replacement string are not immediately followed by
	 * a digit 0-9 or not immediately preceded by a \.
	 */
	if (g_regex_match (checks->syntax_check, replacement, 0, NULL)) {
		err_str = g_strdup_printf ("The replacement string '%s' contains a \"$\" character "
		                           "that is not immediately followed by a digit 0-9 and "
		                           "not immediately preceded by a \"\\\".",
		                           replacement);
		sqlite3_result_error (context, err_str, -1);
		g_free (err_str);
		return;
	}

	/* According to the XPath 2.0 standard, the dollar sign ($) followed by a number
	 * indicates backreferences. GRegex uses the backslash (\) for this purpose.
	 * So the ($) backreferences in the given replacement string are replaced by (\)
	 * backreferences to support the standard.
	 */
	capture_count = g_regex_get_capture_count (regex);
	replace_regex = sqlite3_get_auxdata (context, 2);

	if (capture_count > 9 && !replace_regex) {
		gint i;
		GString *backref_range;
		gchar *regex_interpret;

		/* S ... capture_count, N ... the given decimal number.
		 * If N>S and N>9, The last digit of N is taken to be a literal character
		 * to be included "as is" in the replacement string, and the rules are
		 * reapplied using the number N formed by stripping off this last digit.
		 */
		backref_range = g_string_new ("(");
		for (i = 10; i <= capture_count; i++) {
			g_string_append_printf (backref_range, "%d|", i);
		}

		g_string_append (backref_range, "\\d)");
		regex_interpret = g_strdup_printf ("(?<!\\\\)\\$%s",
		                                   backref_range->str);

		replace_regex = g_regex_new (regex_interpret, 0, 0, NULL);

		g_string_free (backref_range, TRUE);
		g_free (regex_interpret);

		store_replace_regex = TRUE;
	} else if (capture_count <= 9) {
		replace_regex = checks->replacement;
	}

	replaced = g_regex_replace (replace_regex,
	                            replacement, -1, 0, "\\\\g<\\1>", 0, &error);

	if (!error) {
		/* All '\$' pairs are replaced by '$' */
		unescaped = g_regex_replace (checks->unescape,
		                             replaced, -1, 0, "$", 0, &error);
	}

	if (!error) {
		output = g_regex_replace (regex, input, -1, 0, unescaped, 0, &error);
	}

	if (error) {
		sqlite3_result_error (context, error->message, -1);
		g_clear_error (&error);
		return;
	}

	sqlite3_result_text (context, output, -1, g_free);

	if (store_replace_regex)
		sqlite3_set_auxdata (context, 2, replace_regex, (GDestroyNotify) g_regex_unref);
	if (store_regex)
		sqlite3_set_auxdata (context, 1, regex, (GDestroyNotify) g_regex_unref);

	g_free (replaced);
	g_free (unescaped);
}

#ifdef HAVE_LIBUNISTRING

static void
function_sparql_lower_case (sqlite3_context *context,
                            int              argc,
                            sqlite3_value   *argv[])
{
	const uint16_t *zInput;
	uint16_t *zOutput;
	size_t written = 0;
	int nInput;

	g_assert (argc == 1);

	zInput = sqlite3_value_text16 (argv[0]);

	if (!zInput) {
		return;
	}

	nInput = sqlite3_value_bytes16 (argv[0]);

	zOutput = u16_tolower (zInput, nInput/2, NULL, NULL, NULL, &written);

	sqlite3_result_text16 (context, zOutput, written * 2, free);
}

static void
function_sparql_upper_case (sqlite3_context *context,
                            int              argc,
                            sqlite3_value   *argv[])
{
	const uint16_t *zInput;
	uint16_t *zOutput;
	size_t written = 0;
	int nInput;

	g_assert (argc == 1);

	zInput = sqlite3_value_text16 (argv[0]);

	if (!zInput) {
		return;
	}

	nInput = sqlite3_value_bytes16 (argv[0]);

	zOutput = u16_toupper (zInput, nInput / 2, NULL, NULL, NULL, &written);

	sqlite3_result_text16 (context, zOutput, written * 2, free);
}

static void
function_sparql_case_fold (sqlite3_context *context,
                           int              argc,
                           sqlite3_value   *argv[])
{
	const uint16_t *zInput;
	uint16_t *zOutput;
	size_t written = 0;
	int nInput;

	g_assert (argc == 1);

	zInput = sqlite3_value_text16 (argv[0]);

	if (!zInput) {
		return;
	}

	nInput = sqlite3_value_bytes16 (argv[0]);

	zOutput = u16_casefold (zInput, nInput/2, NULL, NULL, NULL, &written);

	sqlite3_result_text16 (context, zOutput, written * 2, free);
}

static void
function_sparql_normalize (sqlite3_context *context,
                           int              argc,
                           sqlite3_value   *argv[])
{
	const gchar *nfstr;
	const uint16_t *zInput;
	uint16_t *zOutput;
	size_t written = 0;
	int nInput;
	uninorm_t nf;

	if (argc != 2) {
		sqlite3_result_error (context, "Invalid argument count", -1);
		return;
	}

	zInput = sqlite3_value_text16 (argv[0]);

	if (!zInput) {
		return;
	}

	nfstr = sqlite3_value_text (argv[1]);
	if (g_ascii_strcasecmp (nfstr, "nfc") == 0)
		nf = UNINORM_NFC;
	else if (g_ascii_strcasecmp (nfstr, "nfd") == 0)
		nf = UNINORM_NFD;
	else if (g_ascii_strcasecmp (nfstr, "nfkc") == 0)
		nf = UNINORM_NFKC;
	else if (g_ascii_strcasecmp (nfstr, "nfkd") == 0)
		nf = UNINORM_NFKD;
	else {
		sqlite3_result_error (context, "Invalid normalization specified, options are 'nfc', 'nfd', 'nfkc' or 'nfkd'", -1);
		return;
	}

	nInput = sqlite3_value_bytes16 (argv[0]);

	zOutput = u16_normalize (nf, zInput, nInput/2, NULL, &written);

	sqlite3_result_text16 (context, zOutput, written * 2, free);
}

static void
function_sparql_unaccent (sqlite3_context *context,
                          int              argc,
                          sqlite3_value   *argv[])
{
	const gchar *zInput;
	gchar *zOutput;
	gsize written = 0;
	int nInput;

	g_assert (argc == 1);

	zInput = sqlite3_value_text (argv[0]);

	if (!zInput) {
		return;
	}

	nInput = sqlite3_value_bytes (argv[0]);

	zOutput = u8_normalize (UNINORM_NFKD, zInput, nInput, NULL, &written);

	/* Unaccenting is done in place */
	tracker_parser_unaccent_nfkd_string (zOutput, &written);

	sqlite3_result_text (context, zOutput, written, free);
}

#elif defined(HAVE_LIBICU)

static void
function_sparql_lower_case (sqlite3_context *context,
                            int              argc,
                            sqlite3_value   *argv[])
{
	const UChar *zInput;
	UChar *zOutput;
	int nInput;
	int nOutput;
	UErrorCode status = U_ZERO_ERROR;

	g_assert (argc == 1);

	zInput = sqlite3_value_text16 (argv[0]);

	if (!zInput) {
		return;
	}

	nInput = sqlite3_value_bytes16 (argv[0]);

	nOutput = nInput * 2 + 2;
	zOutput = sqlite3_malloc (nOutput);

	if (!zOutput) {
		return;
	}

	u_strToLower (zOutput, nOutput/2, zInput, nInput/2, NULL, &status);

	if (!U_SUCCESS (status)){
		char zBuf[128];
		sqlite3_snprintf (128, zBuf, "ICU error: u_strToLower(): %s", u_errorName (status));
		zBuf[127] = '\0';
		sqlite3_free (zOutput);
		sqlite3_result_error (context, zBuf, -1);
		return;
	}

	sqlite3_result_text16 (context, zOutput, -1, sqlite3_free);
}

static void
function_sparql_upper_case (sqlite3_context *context,
                            int              argc,
                            sqlite3_value   *argv[])
{
	const UChar *zInput;
	UChar *zOutput;
	int nInput;
	int nOutput;
	UErrorCode status = U_ZERO_ERROR;

	g_assert (argc == 1);

	zInput = sqlite3_value_text16 (argv[0]);

	if (!zInput) {
		return;
	}

	nInput = sqlite3_value_bytes16 (argv[0]);

	nOutput = nInput * 2 + 2;
	zOutput = sqlite3_malloc (nOutput);

	if (!zOutput) {
		return;
	}

	u_strToUpper (zOutput, nOutput / 2, zInput, nInput / 2, NULL, &status);

	if (!U_SUCCESS (status)){
		char zBuf[128];
		sqlite3_snprintf (128, zBuf, "ICU error: u_strToUpper(): %s", u_errorName (status));
		zBuf[127] = '\0';
		sqlite3_free (zOutput);
		sqlite3_result_error (context, zBuf, -1);
		return;
	}

	sqlite3_result_text16 (context, zOutput, -1, sqlite3_free);
}

static void
function_sparql_case_fold (sqlite3_context *context,
                           int              argc,
                           sqlite3_value   *argv[])
{
	const UChar *zInput;
	UChar *zOutput;
	int nInput;
	int nOutput;
	UErrorCode status = U_ZERO_ERROR;

	g_assert (argc == 1);

	zInput = sqlite3_value_text16 (argv[0]);

	if (!zInput) {
		return;
	}

	nInput = sqlite3_value_bytes16 (argv[0]);

	nOutput = nInput * 2 + 2;
	zOutput = sqlite3_malloc (nOutput);

	if (!zOutput) {
		return;
	}

	u_strFoldCase (zOutput, nOutput/2, zInput, nInput/2, U_FOLD_CASE_DEFAULT, &status);

	if (!U_SUCCESS (status)){
		char zBuf[128];
		sqlite3_snprintf (128, zBuf, "ICU error: u_strFoldCase: %s", u_errorName (status));
		zBuf[127] = '\0';
		sqlite3_free (zOutput);
		sqlite3_result_error (context, zBuf, -1);
		return;
	}

	sqlite3_result_text16 (context, zOutput, -1, sqlite3_free);
}

static gunichar2 *
normalize_string (const gunichar2    *string,
                  gsize               string_len, /* In gunichar2s */
                  const UNormalizer2 *normalizer,
                  gsize              *len_out,    /* In gunichar2s */
                  UErrorCode         *status)
{
	int nOutput;
	gunichar2 *zOutput;

	nOutput = (string_len * 2) + 1;
	zOutput = g_new0 (gunichar2, nOutput);

	nOutput = unorm2_normalize (normalizer, string, string_len, zOutput, nOutput, status);

	if (*status == U_BUFFER_OVERFLOW_ERROR) {
		/* Try again after allocating enough space for the normalization */
		*status = U_ZERO_ERROR;
		zOutput = g_renew (gunichar2, zOutput, nOutput);
		memset (zOutput, 0, nOutput * sizeof (gunichar2));
		nOutput = unorm2_normalize (normalizer, string, string_len, zOutput, nOutput, status);
	}

	if (!U_SUCCESS (*status)) {
		g_clear_pointer (&zOutput, g_free);
		nOutput = 0;
	}

	if (len_out)
		*len_out = nOutput;

	return zOutput;
}

static void
function_sparql_normalize (sqlite3_context *context,
                           int              argc,
                           sqlite3_value   *argv[])
{
	const gchar *nfstr;
	const uint16_t *zInput;
	uint16_t *zOutput = NULL;
	int nInput;
	gsize nOutput;
	const UNormalizer2 *normalizer;
	UErrorCode status = U_ZERO_ERROR;

	if (argc != 2) {
		sqlite3_result_error (context, "Invalid argument count", -1);
		return;
	}

	zInput = sqlite3_value_text16 (argv[0]);

	if (!zInput) {
		return;
	}

	nfstr = (gchar *)sqlite3_value_text (argv[1]);
	if (g_ascii_strcasecmp (nfstr, "nfc") == 0)
		normalizer = unorm2_getNFCInstance (&status);
	else if (g_ascii_strcasecmp (nfstr, "nfd") == 0)
		normalizer = unorm2_getNFDInstance (&status);
	else if (g_ascii_strcasecmp (nfstr, "nfkc") == 0)
		normalizer = unorm2_getNFKCInstance (&status);
	else if (g_ascii_strcasecmp (nfstr, "nfkd") == 0)
		normalizer = unorm2_getNFKDInstance (&status);
	else {
		sqlite3_result_error (context, "Invalid normalization specified", -1);
		return;
	}

	if (U_SUCCESS (status)) {
		nInput = sqlite3_value_bytes16 (argv[0]);
		zOutput = normalize_string (zInput, nInput / 2, normalizer, &nOutput, &status);
	}

	if (!U_SUCCESS (status)) {
		char zBuf[128];
		sqlite3_snprintf (128, zBuf, "ICU error: unorm_normalize: %s", u_errorName (status));
		zBuf[127] = '\0';
		sqlite3_free (zOutput);
		sqlite3_result_error (context, zBuf, -1);
		return;
	}

	sqlite3_result_text16 (context, zOutput, nOutput * sizeof (gunichar2), g_free);
}

static void
function_sparql_unaccent (sqlite3_context *context,
                          int              argc,
                          sqlite3_value   *argv[])
{
	const uint16_t *zInput;
	uint16_t *zOutput = NULL;
	int nInput;
	gsize nOutput;
	const UNormalizer2 *normalizer;
	UErrorCode status = U_ZERO_ERROR;

	g_assert (argc == 1);

	zInput = sqlite3_value_text16 (argv[0]);

	if (!zInput) {
		return;
	}

	normalizer = unorm2_getNFKDInstance (&status);

	if (U_SUCCESS (status)) {
		nInput = sqlite3_value_bytes16 (argv[0]);
		zOutput = normalize_string (zInput, nInput / 2, normalizer, &nOutput, &status);
	}

	if (!U_SUCCESS (status)) {
		char zBuf[128];
		sqlite3_snprintf (128, zBuf, "ICU error: unorm_normalize: %s", u_errorName (status));
		zBuf[127] = '\0';
		sqlite3_free (zOutput);
		sqlite3_result_error (context, zBuf, -1);
		return;
	}

	/* Unaccenting is done in place */
	tracker_parser_unaccent_nfkd_string (zOutput, &nOutput);

	sqlite3_result_text16 (context, zOutput, nOutput * sizeof (gunichar2), g_free);
}

#endif

static void
function_sparql_encode_for_uri (sqlite3_context *context,
                                int              argc,
                                sqlite3_value   *argv[])
{
	const gchar *str;
	gchar *encoded;

	if (argc != 1) {
		sqlite3_result_error (context, "Invalid argument count", -1);
		return;
	}

	str = (gchar *)sqlite3_value_text (argv[0]);
	encoded = g_uri_escape_string (str, NULL, FALSE);
	sqlite3_result_text (context, encoded, -1, g_free);
}

static void
function_sparql_string_before (sqlite3_context *context,
                               int              argc,
                               sqlite3_value   *argv[])
{
	const gchar *str, *substr, *loc;
	gint len;

	if (argc != 2) {
		sqlite3_result_error (context, "Invalid argument count", -1);
		return;
	}

	if (sqlite3_value_type (argv[0]) != SQLITE_TEXT ||
	    sqlite3_value_type (argv[1]) != SQLITE_TEXT) {
		sqlite3_result_error (context, "Invalid argument types", -1);
		return;
	}

	str = (gchar *)sqlite3_value_text (argv[0]);
	substr = (gchar *)sqlite3_value_text (argv[1]);
	len = strlen (substr);

	if (len == 0) {
		sqlite3_result_text (context, "", -1, NULL);
		return;
	}

	loc = strstr (str, substr);

	if (!loc) {
		sqlite3_result_text (context, "", -1, NULL);
		return;
	}

	sqlite3_result_text (context, str, loc - str, NULL);
}

static void
function_sparql_string_after (sqlite3_context *context,
                              int              argc,
                              sqlite3_value   *argv[])
{
	const gchar *str, *substr, *loc;
	gint len;

	if (argc != 2) {
		sqlite3_result_error (context, "Invalid argument count", -1);
		return;
	}

	if (sqlite3_value_type (argv[0]) != SQLITE_TEXT ||
	    sqlite3_value_type (argv[1]) != SQLITE_TEXT) {
		sqlite3_result_error (context, "Invalid argument types", -1);
		return;
	}

	str = (gchar *)sqlite3_value_text (argv[0]);
	substr = (gchar *)sqlite3_value_text (argv[1]);
	len = strlen (substr);

	if (len == 0) {
		sqlite3_result_text (context, g_strdup (str), -1, g_free);
		return;
	}

	loc = strstr (str, substr);

	if (!loc) {
		sqlite3_result_text (context, "", -1, NULL);
		return;
	}

	sqlite3_result_text (context, loc + len, -1, NULL);
}

static void
function_sparql_ceil (sqlite3_context *context,
                      int              argc,
                      sqlite3_value   *argv[])
{
	gdouble value;

	if (argc != 1) {
		sqlite3_result_error (context, "Invalid argument count", -1);
		return;
	}

	value = sqlite3_value_double (argv[0]);
	sqlite3_result_double (context, ceil (value));
}

static void
function_sparql_floor (sqlite3_context *context,
                       int              argc,
                       sqlite3_value   *argv[])
{
	gdouble value;

	if (argc != 1) {
		sqlite3_result_error (context, "Invalid argument count", -1);
		return;
	}

	value = sqlite3_value_double (argv[0]);
	sqlite3_result_double (context, floor (value));
}

static void
function_sparql_rand (sqlite3_context *context,
                      int              argc,
                      sqlite3_value   *argv[])
{
	if (argc != 0) {
		sqlite3_result_error (context, "Invalid argument count", -1);
		return;
	}

	sqlite3_result_double (context, g_random_double ());
}

static void
function_sparql_checksum (sqlite3_context *context,
			  int              argc,
			  sqlite3_value   *argv[])
{
	const gchar *str, *checksumstr;
	GChecksumType checksum;
	gchar *result;

	if (argc != 2) {
		sqlite3_result_error (context, "Invalid argument count", -1);
		return;
	}

	str = (gchar *)sqlite3_value_text (argv[0]);
	checksumstr = (gchar *)sqlite3_value_text (argv[1]);

	if (!str || !checksumstr) {
		sqlite3_result_error (context, "Invalid arguments", -1);
		return;
	}

	if (g_ascii_strcasecmp (checksumstr, "md5") == 0)
		checksum = G_CHECKSUM_MD5;
	else if (g_ascii_strcasecmp (checksumstr, "sha1") == 0)
		checksum = G_CHECKSUM_SHA1;
	else if (g_ascii_strcasecmp (checksumstr, "sha256") == 0)
		checksum = G_CHECKSUM_SHA256;
#if GLIB_CHECK_VERSION (2, 51, 0)
	else if (g_ascii_strcasecmp (checksumstr, "sha384") == 0)
		checksum = G_CHECKSUM_SHA384;
#endif
	else if (g_ascii_strcasecmp (checksumstr, "sha512") == 0)
		checksum = G_CHECKSUM_SHA512;
	else {
		sqlite3_result_error (context, "Invalid checksum method specified", -1);
		return;
	}

	result = g_compute_checksum_for_string (checksum, str, -1);
	sqlite3_result_text (context, result, -1, g_free);
}

static inline int
stmt_step (sqlite3_stmt *stmt)
{
	int result;

	result = sqlite3_step (stmt);

	/* If the statement expired between preparing it and executing
	 * sqlite3_step(), we are supposed to get SQLITE_SCHEMA error in
	 * sqlite3_errcode(), BUT there seems to be a bug in sqlite and
	 * SQLITE_ABORT is being returned instead for that case. So, the
	 * only way to see if a given statement was expired is to use
	 * sqlite3_expired(stmt), which is marked as DEPRECATED in sqlite.
	 * If found that the statement is expired, we need to reset it
	 * and retry the sqlite3_step().
	 * NOTE, that this expiration may only happen between preparing
	 * the statement and step-ing it, NOT between steps. */
	if ((result == SQLITE_ABORT || result == SQLITE_SCHEMA) &&
	    sqlite3_expired (stmt)) {
		sqlite3_reset (stmt);
		result = sqlite3_step (stmt);
	}

	return result;
}

static void
function_sparql_uuid (sqlite3_context *context,
                      int              argc,
                      sqlite3_value   *argv[])
{
	gchar *uuid = NULL;
	sqlite3_stmt *stmt;
	sqlite3 *db;
	gint result;

	if (argc > 1) {
		sqlite3_result_error (context, "Invalid argument count", -1);
		return;
	}

	db = sqlite3_context_db_handle (context);

	result = sqlite3_prepare_v2 (db, "SELECT ID FROM Resource WHERE Uri=?",
				     -1, &stmt, NULL);
	if (result != SQLITE_OK) {
		sqlite3_result_error (context, sqlite3_errstr (result), -1);
		return;
	}

	do {
		g_clear_pointer (&uuid, g_free);
		uuid = tracker_generate_uuid ();

		sqlite3_reset (stmt);
		sqlite3_bind_text (stmt, 1, uuid, -1, SQLITE_TRANSIENT);
		result = stmt_step (stmt);
	} while (result == SQLITE_ROW);

	sqlite3_finalize (stmt);

	if (result != SQLITE_DONE) {
		sqlite3_result_error (context, sqlite3_errstr (result), -1);
		g_free (uuid);
		return;
	}

	sqlite3_result_text (context, uuid, -1, g_free);
}

static int
check_interrupt (void *user_data)
{
	TrackerDBInterface *db_interface = user_data;

	return g_cancellable_is_cancelled (db_interface->cancellable) ? 1 : 0;
}

static void
initialize_functions (TrackerDBInterface *db_interface)
{
	gint i;
	struct {
		gchar *name;
		int n_args;
		int mods;
		void (*func) (sqlite3_context *, int, sqlite3_value**);
	} functions[] = {
		/* Geolocation */
		{ "SparqlHaversineDistance", 4, SQLITE_ANY | SQLITE_DETERMINISTIC,
		  function_sparql_haversine_distance },
		{ "SparqlCartesianDistance", 4, SQLITE_ANY | SQLITE_DETERMINISTIC,
		  function_sparql_cartesian_distance },
		/* Date/time */
		{ "SparqlFormatTime", 1, SQLITE_ANY | SQLITE_DETERMINISTIC,
		  function_sparql_format_time },
		/* Paths and filenames */
		{ "SparqlStringFromFilename", 1, SQLITE_ANY | SQLITE_DETERMINISTIC,
		  function_sparql_string_from_filename },
		{ "SparqlUriIsParent", 2, SQLITE_ANY | SQLITE_DETERMINISTIC,
		  function_sparql_uri_is_parent },
		{ "SparqlUriIsDescendant", -1, SQLITE_ANY | SQLITE_DETERMINISTIC,
		  function_sparql_uri_is_descendant },
		{ "SparqlEncodeForUri", 1, SQLITE_ANY | SQLITE_DETERMINISTIC,
		  function_sparql_encode_for_uri },
		/* Strings */
		{ "SparqlRegex", -1, SQLITE_ANY | SQLITE_DETERMINISTIC,
		  function_sparql_regex },
		{ "SparqlStringJoin", -1, SQLITE_ANY | SQLITE_DETERMINISTIC,
		  function_sparql_string_join },
		{ "SparqlLowerCase", 1, SQLITE_ANY | SQLITE_DETERMINISTIC,
		  function_sparql_lower_case },
		{ "SparqlUpperCase", 1, SQLITE_ANY | SQLITE_DETERMINISTIC,
		  function_sparql_upper_case },
		{ "SparqlCaseFold", 1, SQLITE_ANY | SQLITE_DETERMINISTIC,
		  function_sparql_case_fold },
		{ "SparqlNormalize", 2, SQLITE_ANY | SQLITE_DETERMINISTIC,
		  function_sparql_normalize },
		{ "SparqlUnaccent", 1, SQLITE_ANY | SQLITE_DETERMINISTIC,
		  function_sparql_unaccent },
		{ "SparqlStringBefore", 2, SQLITE_ANY | SQLITE_DETERMINISTIC,
		  function_sparql_string_before },
		{ "SparqlStringAfter", 2, SQLITE_ANY | SQLITE_DETERMINISTIC,
		  function_sparql_string_after },
		{ "SparqlReplace", -1, SQLITE_ANY | SQLITE_DETERMINISTIC,
		  function_sparql_replace },
		{ "SparqlChecksum", 2, SQLITE_ANY | SQLITE_DETERMINISTIC,
		  function_sparql_checksum },
		/* Numbers */
		{ "SparqlCeil", 1, SQLITE_ANY | SQLITE_DETERMINISTIC,
		  function_sparql_ceil },
		{ "SparqlFloor", 1, SQLITE_ANY | SQLITE_DETERMINISTIC,
		  function_sparql_floor },
		{ "SparqlRand", 0, SQLITE_ANY, function_sparql_rand },
		/* UUID */
		{ "SparqlUUID", 0, SQLITE_ANY, function_sparql_uuid },
	};

	for (i = 0; i < G_N_ELEMENTS (functions); i++) {
		sqlite3_create_function (db_interface->db,
		                         functions[i].name, functions[i].n_args,
		                         functions[i].mods, db_interface,
		                         functions[i].func, NULL, NULL);
	}
}

static inline void
tracker_db_interface_lock (TrackerDBInterface *iface)
{
	if (iface->flags & TRACKER_DB_INTERFACE_USE_MUTEX)
		g_mutex_lock (&iface->mutex);
}

static inline void
tracker_db_interface_unlock (TrackerDBInterface *iface)
{
	if (iface->flags & TRACKER_DB_INTERFACE_USE_MUTEX)
		g_mutex_unlock (&iface->mutex);
}

static void
open_database (TrackerDBInterface  *db_interface,
               GError             **error)
{
	int mode;
	int result;

	g_assert (db_interface->filename != NULL);

	if ((db_interface->flags & TRACKER_DB_INTERFACE_READONLY) == 0) {
		mode = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE;
	} else {
		mode = SQLITE_OPEN_READONLY;
	}

	result = sqlite3_open_v2 (db_interface->filename, &db_interface->db, mode | SQLITE_OPEN_NOMUTEX, NULL);
	if (result != SQLITE_OK) {
		const gchar *str;

		str = sqlite3_errstr (result);
		g_set_error (error,
		             TRACKER_DB_INTERFACE_ERROR,
		             TRACKER_DB_OPEN_ERROR,
		             "Could not open sqlite3 database:'%s': %s", db_interface->filename, str);
		return;
	} else {
		g_info ("Opened sqlite3 database:'%s'", db_interface->filename);
	}

	/* Set our unicode collation function */
	tracker_db_interface_sqlite_reset_collator (db_interface);

	sqlite3_progress_handler (db_interface->db, 100,
	                          check_interrupt, db_interface);

	initialize_functions (db_interface);

	sqlite3_extended_result_codes (db_interface->db, 0);
	sqlite3_busy_timeout (db_interface->db, 100000);

#ifndef SQLITE_DBCONFIG_ENABLE_LOAD_EXTENSION
#warning Using sqlite3_enable_load_extension instead of SQLITE_DBCONFIG_ENABLE_LOAD_EXTENSION, this is unsafe
	sqlite3_enable_load_extension (db_interface->db, 1);
#else
	sqlite3_db_config (db_interface->db, SQLITE_DBCONFIG_ENABLE_LOAD_EXTENSION, 1, NULL);
#endif
}

static gboolean
tracker_db_interface_initable_init (GInitable     *initable,
                                    GCancellable  *cancellable,
                                    GError       **error)
{
	TrackerDBInterface *db_iface;
	GError *internal_error = NULL;

	db_iface = TRACKER_DB_INTERFACE (initable);

	open_database (db_iface, &internal_error);

	if (internal_error) {
		g_propagate_error (error, internal_error);
		return FALSE;
	}

	return TRUE;
}

static void
tracker_db_interface_initable_iface_init (GInitableIface *iface)
{
	iface->init = tracker_db_interface_initable_init;
}

static void
tracker_db_interface_sqlite_set_property (GObject       *object,
                                          guint          prop_id,
                                          const GValue  *value,
                                          GParamSpec    *pspec)
{
	TrackerDBInterface *db_iface;

	db_iface = TRACKER_DB_INTERFACE (object);

	switch (prop_id) {
	case PROP_FLAGS:
		db_iface->flags = g_value_get_flags (value);
		break;
	case PROP_FILENAME:
		db_iface->filename = g_value_dup_string (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
tracker_db_interface_sqlite_get_property (GObject    *object,
                                          guint       prop_id,
                                          GValue     *value,
                                          GParamSpec *pspec)
{
	TrackerDBInterface *db_iface;

	db_iface = TRACKER_DB_INTERFACE (object);

	switch (prop_id) {
	case PROP_FLAGS:
		g_value_set_flags (value, db_iface->flags);
		break;
	case PROP_FILENAME:
		g_value_set_string (value, db_iface->filename);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
close_database (TrackerDBInterface *db_interface)
{
	gint rc;

	if (db_interface->dynamic_statements) {
		g_hash_table_unref (db_interface->dynamic_statements);
		db_interface->dynamic_statements = NULL;
	}

	if (db_interface->replace_func_checks.syntax_check)
		g_regex_unref (db_interface->replace_func_checks.syntax_check);
	if (db_interface->replace_func_checks.replacement)
		g_regex_unref (db_interface->replace_func_checks.replacement);
	if (db_interface->replace_func_checks.unescape)
		g_regex_unref (db_interface->replace_func_checks.unescape);

	if (db_interface->db) {
		rc = sqlite3_close (db_interface->db);
		g_warn_if_fail (rc == SQLITE_OK);
	}
}

#if HAVE_TRACKER_FTS
static gchar **
_fts_create_properties (GHashTable *properties)
{
	GHashTableIter iter;
	GPtrArray *cols;
	GList *columns;
	gchar *table;

	if (g_hash_table_size (properties) == 0) {
		return NULL;
	}

	g_hash_table_iter_init (&iter, properties);
	cols = g_ptr_array_new ();

	while (g_hash_table_iter_next (&iter, (gpointer *) &table,
				       (gpointer *) &columns)) {
		while (columns) {
			g_ptr_array_add (cols, g_strdup (columns->data));
			columns = columns->next;
		}
	}

	g_ptr_array_add (cols, NULL);

	return (gchar **) g_ptr_array_free (cols, FALSE);
}
#endif

void
tracker_db_interface_sqlite_fts_init (TrackerDBInterface  *db_interface,
                                      GHashTable          *properties,
                                      GHashTable          *multivalued,
                                      gboolean             create)
{
#if HAVE_TRACKER_FTS
	GStrv fts_columns;

	tracker_fts_init_db (db_interface->db, db_interface, properties);

	if (create &&
	    !tracker_fts_create_table (db_interface->db, "fts5",
				       properties, multivalued)) {
		g_warning ("FTS tables creation failed");
	}

	fts_columns = _fts_create_properties (properties);

	if (fts_columns) {
		GString *fts_properties;
		gint i;

		fts_properties = g_string_new (NULL);

		for (i = 0; fts_columns[i] != NULL; i++) {
			g_string_append_printf (fts_properties, ", \"%s\"",
			                        fts_columns[i]);
		}

		g_free (db_interface->fts_properties);
		db_interface->fts_properties = g_string_free (fts_properties,
		                                              FALSE);
		g_strfreev (fts_columns);
	}
#endif
}

#if HAVE_TRACKER_FTS

void
tracker_db_interface_sqlite_fts_delete_table (TrackerDBInterface  *db_interface)
{
	if (!tracker_fts_delete_table (db_interface->db, "fts5")) {
		g_critical ("Failed to delete FTS table");
	}
}

void
tracker_db_interface_sqlite_fts_alter_table (TrackerDBInterface  *db_interface,
					     GHashTable          *properties,
					     GHashTable          *multivalued)
{
	if (!tracker_fts_alter_table (db_interface->db, "fts5", properties, multivalued)) {
		g_critical ("Failed to update FTS columns");
	}
}

static gchar *
tracker_db_interface_sqlite_fts_create_query (TrackerDBInterface  *db_interface,
                                              gboolean             delete,
                                              const gchar        **properties)
{
	GString *insert_str, *values_str;
	gint i;

	insert_str = g_string_new ("INSERT INTO fts5 (");
	values_str = g_string_new (NULL);

	if (delete) {
		g_string_append (insert_str, "fts5,");
		g_string_append (values_str, "'delete',");
	}

	g_string_append (insert_str, "rowid");
	g_string_append (values_str, "?");

	for (i = 0; properties[i] != NULL; i++) {
		g_string_append_printf (insert_str, ",\"%s\"", properties[i]);
		g_string_append (values_str, ",?");
	}

	g_string_append_printf (insert_str, ") VALUES (%s)", values_str->str);
	g_string_free (values_str, TRUE);

	return g_string_free (insert_str, FALSE);
}

static gchar *
tracker_db_interface_sqlite_fts_create_delete_all_query (TrackerDBInterface *db_interface)
{
	GString *insert_str;

	insert_str = g_string_new (NULL);
	g_string_append_printf (insert_str,
	                        "INSERT INTO fts5 (fts5, rowid %s) "
	                        "SELECT 'delete', rowid %s FROM fts_view "
	                        "WHERE rowid = ?",
	                        db_interface->fts_properties,
	                        db_interface->fts_properties);
	return g_string_free (insert_str, FALSE);
}

gboolean
tracker_db_interface_sqlite_fts_update_text (TrackerDBInterface  *db_interface,
                                             int                  id,
                                             const gchar        **properties,
                                             const gchar        **text)
{
	TrackerDBStatement *stmt;
	GError *error = NULL;
	gchar *query;
	gint i;

	query = tracker_db_interface_sqlite_fts_create_query (db_interface,
	                                                      FALSE, properties);
	stmt = tracker_db_interface_create_statement (db_interface,
	                                              TRACKER_DB_STATEMENT_CACHE_TYPE_UPDATE,
	                                              &error,
	                                              "%s", query);
	g_free (query);

        if (!stmt || error) {
                if (error) {
                        g_warning ("Could not create FTS insert statement: %s\n",
                                   error->message);
                        g_error_free (error);
                }
                return FALSE;
        }

        tracker_db_statement_bind_int (stmt, 0, id);
        for (i = 0; text[i] != NULL; i++) {
	        tracker_db_statement_bind_text (stmt, i + 1, text[i]);
        }

        tracker_db_statement_execute (stmt, &error);
        g_object_unref (stmt);

        if (error) {
                g_warning ("Could not insert FTS text: %s", error->message);
                g_error_free (error);
                return FALSE;
        }

        return TRUE;
}

gboolean
tracker_db_interface_sqlite_fts_delete_text (TrackerDBInterface  *db_interface,
                                             int                  rowid,
                                             const gchar        **properties,
                                             const gchar        **old_text)
{
	TrackerDBStatement *stmt;
	GError *error = NULL;
	gchar *query;
	gint i;

	query = tracker_db_interface_sqlite_fts_create_query (db_interface,
	                                                      TRUE, properties);
	stmt = tracker_db_interface_create_statement (db_interface,
	                                              TRACKER_DB_STATEMENT_CACHE_TYPE_UPDATE,
	                                              &error,
	                                              "%s", query);
	g_free (query);

	if (!stmt || error) {
		g_warning ("Could not create FTS delete statement: %s",
		           error ? error->message : "No error given");
		g_clear_error (&error);
		return FALSE;
	}

	tracker_db_statement_bind_int (stmt, 0, rowid);
	for (i = 0; old_text[i] != NULL; i++)
		tracker_db_statement_bind_text (stmt, i + 1, old_text[i]);

	tracker_db_statement_execute (stmt, &error);
	g_object_unref (stmt);

	if (error) {
		g_warning ("Could not delete FTS text: %s", error->message);
		g_error_free (error);
		return FALSE;
	}

	return TRUE;
}

gboolean
tracker_db_interface_sqlite_fts_delete_id (TrackerDBInterface *db_interface,
                                           int                 id)
{
	TrackerDBStatement *stmt;
	GError *error = NULL;
	gchar *query;

	query = tracker_db_interface_sqlite_fts_create_delete_all_query (db_interface);
	stmt = tracker_db_interface_create_statement (db_interface,
	                                              TRACKER_DB_STATEMENT_CACHE_TYPE_UPDATE,
	                                              &error,
	                                              "%s", query);
	g_free (query);

	if (!stmt || error) {
		if (error) {
			g_warning ("Could not create FTS delete statement: %s",
			           error->message);
			g_error_free (error);
		}
		return FALSE;
	}

	tracker_db_statement_bind_int (stmt, 0, id);
	tracker_db_statement_execute (stmt, &error);
	g_object_unref (stmt);

	if (error) {
		g_warning ("Could not delete FTS content: %s", error->message);
		g_error_free (error);
		return FALSE;
	}

	return TRUE;
}

void
tracker_db_interface_sqlite_fts_rebuild_tokens (TrackerDBInterface *interface)
{
	tracker_fts_rebuild_tokens (interface->db, "fts5");
}

#endif

void
tracker_db_interface_sqlite_reset_collator (TrackerDBInterface *db_interface)
{
	g_debug ("Resetting collator in db interface %p", db_interface);

	/* This will overwrite any other collation set before, if any */
	if (sqlite3_create_collation_v2 (db_interface->db,
	                                 TRACKER_COLLATION_NAME,
	                                 SQLITE_UTF8,
	                                 tracker_collation_init (),
	                                 tracker_collation_utf8,
	                                 tracker_collation_shutdown) != SQLITE_OK)
	{
		g_critical ("Couldn't set collation function: %s",
		            sqlite3_errmsg (db_interface->db));
	}

	if (sqlite3_create_collation_v2 (db_interface->db,
	                                 TRACKER_TITLE_COLLATION_NAME,
	                                 SQLITE_UTF8,
	                                 tracker_collation_init (),
	                                 tracker_collation_utf8_title,
	                                 tracker_collation_shutdown) != SQLITE_OK) {
		g_critical ("Couldn't set title collation function: %s",
		            sqlite3_errmsg (db_interface->db));
	}
}

static gint
wal_hook (gpointer     user_data,
          sqlite3     *db,
          const gchar *db_name,
          gint         n_pages)
{
	TrackerDBInterface *iface = user_data;

	iface->wal_hook (iface, n_pages, iface->wal_hook_data);
	return SQLITE_OK;
}

void
tracker_db_interface_sqlite_wal_hook (TrackerDBInterface   *interface,
                                      TrackerDBWalCallback  callback,
                                      gpointer              user_data)
{
	interface->wal_hook = callback;
	interface->wal_hook_data = user_data;
	sqlite3_wal_hook (interface->db, wal_hook, interface);
}

gboolean
tracker_db_interface_sqlite_wal_checkpoint (TrackerDBInterface  *interface,
                                            gboolean             blocking,
                                            GError             **error)
{
	int return_val;

	return_val = sqlite3_wal_checkpoint_v2 (interface->db, NULL,
	                                        blocking ? SQLITE_CHECKPOINT_FULL : SQLITE_CHECKPOINT_PASSIVE,
	                                        NULL, NULL);

	if (return_val != SQLITE_OK) {
		g_set_error (error,
		             TRACKER_DB_INTERFACE_ERROR,
		             TRACKER_DB_QUERY_ERROR,
		             "%s", sqlite3_errstr (return_val));
		return FALSE;
	}

	return TRUE;
}

static void
tracker_db_interface_sqlite_finalize (GObject *object)
{
	TrackerDBInterface *db_interface;

	db_interface = TRACKER_DB_INTERFACE (object);

	close_database (db_interface);
	g_free (db_interface->fts_properties);

	g_info ("Closed sqlite3 database:'%s'", db_interface->filename);

	g_free (db_interface->filename);

	if (db_interface->user_data && db_interface->user_data_destroy_notify)
		db_interface->user_data_destroy_notify (db_interface->user_data);

	G_OBJECT_CLASS (tracker_db_interface_parent_class)->finalize (object);
}

static void
tracker_db_interface_class_init (TrackerDBInterfaceClass *class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (class);

	object_class->set_property = tracker_db_interface_sqlite_set_property;
	object_class->get_property = tracker_db_interface_sqlite_get_property;
	object_class->finalize = tracker_db_interface_sqlite_finalize;

	g_object_class_install_property (object_class,
	                                 PROP_FILENAME,
	                                 g_param_spec_string ("filename",
	                                                      "DB filename",
	                                                      "DB filename",
	                                                      NULL,
	                                                      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	g_object_class_install_property (object_class,
	                                 PROP_FLAGS,
	                                 g_param_spec_flags ("flags",
	                                                     "Flags",
	                                                     "Interface flags",
	                                                     TRACKER_TYPE_DB_INTERFACE_FLAGS, 0,
	                                                     G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
}

static void
tracker_db_interface_init (TrackerDBInterface *db_interface)
{
	db_interface->dynamic_statements = g_hash_table_new_full (g_str_hash, g_str_equal,
	                                                          NULL,
	                                                          (GDestroyNotify) g_object_unref);
}

void
tracker_db_interface_set_max_stmt_cache_size (TrackerDBInterface         *db_interface,
                                              TrackerDBStatementCacheType cache_type,
                                              guint                       max_size)
{
	TrackerDBStatementLru *stmt_lru;

	if (cache_type == TRACKER_DB_STATEMENT_CACHE_TYPE_UPDATE) {
		stmt_lru = &db_interface->update_stmt_lru;
	} else if (cache_type == TRACKER_DB_STATEMENT_CACHE_TYPE_SELECT) {
		stmt_lru = &db_interface->select_stmt_lru;
	} else {
		return;
	}

	/* Must be larger than 2 to make sense (to have a tail and head) */
	if (max_size > 2) {
		stmt_lru->max = max_size;
	} else {
		stmt_lru->max = 3;
	}
}

static sqlite3_stmt *
tracker_db_interface_prepare_stmt (TrackerDBInterface  *db_interface,
                                   const gchar         *full_query,
                                   GError             **error)
{
	sqlite3_stmt *sqlite_stmt;
	int retval;

	g_debug ("Preparing query: '%s'", full_query);
	retval = sqlite3_prepare_v2 (db_interface->db, full_query, -1, &sqlite_stmt, NULL);

	if (retval != SQLITE_OK) {
		sqlite_stmt = NULL;

		if (retval == SQLITE_INTERRUPT) {
			g_set_error (error,
			             TRACKER_DB_INTERFACE_ERROR,
			             TRACKER_DB_INTERRUPTED,
			             "Interrupted");
		} else {
			g_set_error (error,
			             TRACKER_DB_INTERFACE_ERROR,
			             TRACKER_DB_QUERY_ERROR,
			             "%s",
			             sqlite3_errmsg (db_interface->db));
		}
	}

	return sqlite_stmt;
}

static TrackerDBStatement *
tracker_db_interface_lru_lookup (TrackerDBInterface          *db_interface,
                                 TrackerDBStatementCacheType *cache_type,
                                 const gchar                 *full_query)
{
	TrackerDBStatement *stmt;

	g_return_val_if_fail (*cache_type == TRACKER_DB_STATEMENT_CACHE_TYPE_UPDATE ||
	                      *cache_type == TRACKER_DB_STATEMENT_CACHE_TYPE_SELECT,
	                      NULL);

	/* There are three kinds of queries:
	 * a) Cached queries: SELECT and UPDATE ones (cache_type)
	 * b) Non-Cached queries: NONE ones (cache_type)
	 * c) Forced Non-Cached: in case of a stmt being already in use, we can't
	 *    reuse it (you can't use two different loops on a sqlite3_stmt, of
	 *    course). This happens with recursive uses of a cursor, for example.
	 */

	stmt = g_hash_table_lookup (db_interface->dynamic_statements,
	                            full_query);
	if (!stmt) {
		/* Query not in LRU */
		return NULL;
	}

	/* a) Cached */

	if (stmt && stmt->stmt_is_used) {
		/* c) Forced non-cached
		 * prepared statement is still in use, create new uncached one
		 */
		stmt = NULL;
		/* Make sure to set cache_type here, to avoid replacing
		 * the current statement.
		 */
		*cache_type = TRACKER_DB_STATEMENT_CACHE_TYPE_NONE;
	}

	return stmt;
}

static void
tracker_db_interface_lru_insert_unchecked (TrackerDBInterface          *db_interface,
                                           TrackerDBStatementCacheType  cache_type,
                                           TrackerDBStatement          *stmt)
{
	TrackerDBStatementLru *stmt_lru;

	g_return_if_fail (cache_type == TRACKER_DB_STATEMENT_CACHE_TYPE_UPDATE ||
	                  cache_type == TRACKER_DB_STATEMENT_CACHE_TYPE_SELECT);

	/* LRU holds a reference to the stmt */
	stmt_lru = cache_type == TRACKER_DB_STATEMENT_CACHE_TYPE_UPDATE ?
		&db_interface->update_stmt_lru : &db_interface->select_stmt_lru;

	/* use replace instead of insert to make sure we store the string that
	 * belongs to the right sqlite statement to ensure the lifetime of the string
	 * matches the statement
	 */
	g_hash_table_replace (db_interface->dynamic_statements,
	                      (gpointer) sqlite3_sql (stmt->stmt),
	                      g_object_ref_sink (stmt));

	/* So the ring looks a bit like this: *
	 *                                    *
	 *    .--tail  .--head                *
	 *    |        |                      *
	 *  [p-n] -> [p-n] -> [p-n] -> [p-n]  *
	 *    ^                          |    *
	 *    `- [n-p] <- [n-p] <--------'    *
	 *                                    */

	if (stmt_lru->size >= stmt_lru->max) {
		TrackerDBStatement *new_head;

		/* We reached max-size of the LRU stmt cache. Destroy current
		 * least recently used (stmt_lru.head) and fix the ring. For
		 * that we take out the current head, and close the ring.
		 * Then we assign head->next as new head.
		 */
		new_head = stmt_lru->head->next;
		g_hash_table_remove (db_interface->dynamic_statements,
		                     (gpointer) sqlite3_sql (stmt_lru->head->stmt));
		stmt_lru->size--;
		stmt_lru->head = new_head;
	} else {
		if (stmt_lru->size == 0) {
			stmt_lru->head = stmt;
			stmt_lru->tail = stmt;
		}
	}

	/* Set the current stmt (which is always new here) as the new tail
	 * (new most recent used). We insert current stmt between head and
	 * current tail, and we set tail to current stmt.
	 */
	stmt_lru->size++;
	stmt->next = stmt_lru->head;
	stmt_lru->head->prev = stmt;

	stmt_lru->tail->next = stmt;
	stmt->prev = stmt_lru->tail;
	stmt_lru->tail = stmt;
}

static void
tracker_db_interface_lru_update (TrackerDBInterface          *db_interface,
                                 TrackerDBStatementCacheType  cache_type,
                                 TrackerDBStatement          *stmt)
{
	TrackerDBStatementLru *stmt_lru;

	g_return_if_fail (cache_type == TRACKER_DB_STATEMENT_CACHE_TYPE_UPDATE ||
	                  cache_type == TRACKER_DB_STATEMENT_CACHE_TYPE_SELECT);

	stmt_lru = cache_type == TRACKER_DB_STATEMENT_CACHE_TYPE_UPDATE ?
		&db_interface->update_stmt_lru : &db_interface->select_stmt_lru;

	tracker_db_statement_sqlite_reset (stmt);

	if (stmt == stmt_lru->head) {
		/* Current stmt is least recently used, shift head and tail
		 * of the ring to efficiently make it most recently used.
		 */
		stmt_lru->head = stmt_lru->head->next;
		stmt_lru->tail = stmt_lru->tail->next;
	} else if (stmt != stmt_lru->tail) {
		/* Current statement isn't most recently used, make it most
		 * recently used now (less efficient way than above).
		 */

		/* Take stmt out of the list and close the ring */
		stmt->prev->next = stmt->next;
		stmt->next->prev = stmt->prev;

		/* Put stmt as tail (most recent used) */
		stmt->next = stmt_lru->head;
		stmt_lru->head->prev = stmt;
		stmt->prev = stmt_lru->tail;
		stmt_lru->tail->next = stmt;
		stmt_lru->tail = stmt;
	}

	/* if (stmt == tail), it's already the most recently used in the
	 * ring, so in this case we do nothing of course */
}

TrackerDBStatement *
tracker_db_interface_create_statement (TrackerDBInterface           *db_interface,
                                       TrackerDBStatementCacheType   cache_type,
                                       GError                      **error,
                                       const gchar                  *query,
                                       ...)
{
	TrackerDBStatement *stmt = NULL;
	va_list args;
	gchar *full_query;

	g_return_val_if_fail (TRACKER_IS_DB_INTERFACE (db_interface), NULL);

	va_start (args, query);
	full_query = g_strdup_vprintf (query, args);
	va_end (args);

	tracker_db_interface_lock (db_interface);

	if (cache_type != TRACKER_DB_STATEMENT_CACHE_TYPE_NONE) {
		stmt = tracker_db_interface_lru_lookup (db_interface, &cache_type,
		                                        full_query);
	}

	if (!stmt) {
		sqlite3_stmt *sqlite_stmt;

		sqlite_stmt = tracker_db_interface_prepare_stmt (db_interface,
		                                                 full_query,
		                                                 error);
		if (!sqlite_stmt) {
			tracker_db_interface_unlock (db_interface);
			g_free (full_query);
			return NULL;
		}

		stmt = tracker_db_statement_sqlite_new (db_interface,
		                                        sqlite_stmt);

		if (cache_type != TRACKER_DB_STATEMENT_CACHE_TYPE_NONE) {
			tracker_db_interface_lru_insert_unchecked (db_interface,
			                                           cache_type,
			                                           stmt);
		}
	} else if (cache_type != TRACKER_DB_STATEMENT_CACHE_TYPE_NONE) {
		tracker_db_interface_lru_update (db_interface, cache_type,
		                                 stmt);
	}

	g_free (full_query);

	tracker_db_interface_unlock (db_interface);

	return g_object_ref_sink (stmt);
}

static void
execute_stmt (TrackerDBInterface  *interface,
              sqlite3_stmt        *stmt,
              GCancellable        *cancellable,
              GError             **error)
{
	gint result;

	result = SQLITE_OK;

	g_atomic_int_inc (&interface->n_active_cursors);

	while (result == SQLITE_OK  ||
	       result == SQLITE_ROW) {

		if (g_cancellable_is_cancelled (cancellable)) {
			result = SQLITE_INTERRUPT;
			sqlite3_reset (stmt);
		} else {
			/* only one statement can be active at the same time per interface */
			interface->cancellable = cancellable;
			result = stmt_step (stmt);

			interface->cancellable = NULL;
		}

		switch (result) {
		case SQLITE_ERROR:
			sqlite3_reset (stmt);
			break;
		case SQLITE_ROW:
			break;
		default:
			break;
		}
	}

	g_atomic_int_add (&interface->n_active_cursors, -1);

	if (result != SQLITE_DONE) {
		/* This is rather fatal */
		if (errno != ENOSPC &&
		    (sqlite3_errcode (interface->db) == SQLITE_IOERR ||
		     sqlite3_errcode (interface->db) == SQLITE_CORRUPT ||
		     sqlite3_errcode (interface->db) == SQLITE_NOTADB)) {

			g_critical ("SQLite error: %s (errno: %s)",
			            sqlite3_errmsg (interface->db),
			            g_strerror (errno));

#ifndef DISABLE_JOURNAL
			g_unlink (interface->filename);

			g_error ("SQLite experienced an error with file:'%s'. "
			         "It is either NOT a SQLite database or it is "
			         "corrupt or there was an IO error accessing the data. "
			         "This file has now been removed and will be recreated on the next start. "
			         "Shutting down now.",
			         interface->filename);

			return;
#endif /* DISABLE_JOURNAL */
		}

		if (!error) {
			g_warning ("Could not perform SQLite operation, error:%d->'%s'",
			           sqlite3_errcode (interface->db),
			           sqlite3_errmsg (interface->db));
		} else {
			if (result == SQLITE_INTERRUPT) {
				g_set_error (error,
				             TRACKER_DB_INTERFACE_ERROR,
				             TRACKER_DB_INTERRUPTED,
				             "Interrupted");
			} else {
				g_set_error (error,
				             TRACKER_DB_INTERFACE_ERROR,
				             errno != ENOSPC ? TRACKER_DB_QUERY_ERROR : TRACKER_DB_NO_SPACE,
				             "%s%s%s%s",
				             sqlite3_errmsg (interface->db),
				             errno != 0 ? " (strerror of errno (not necessarily related): " : "",
				             errno != 0 ? g_strerror (errno) : "",
				             errno != 0 ? ")" : "");
			}
		}
	}
}

void
tracker_db_interface_execute_vquery (TrackerDBInterface  *db_interface,
                                     GError             **error,
                                     const gchar         *query,
                                     va_list              args)
{
	gchar *full_query;
	sqlite3_stmt *stmt;

	tracker_db_interface_lock (db_interface);

	full_query = g_strdup_vprintf (query, args);
	stmt = tracker_db_interface_prepare_stmt (db_interface,
	                                          full_query,
	                                          error);
	g_free (full_query);
	if (stmt) {
		execute_stmt (db_interface, stmt, NULL, error);
		sqlite3_finalize (stmt);
	}

	tracker_db_interface_unlock (db_interface);
}

TrackerDBInterface *
tracker_db_interface_sqlite_new (const gchar              *filename,
                                 TrackerDBInterfaceFlags   flags,
                                 GError                  **error)
{
	TrackerDBInterface *object;
	GError *internal_error = NULL;

	object = g_initable_new (TRACKER_TYPE_DB_INTERFACE,
	                         NULL,
	                         &internal_error,
	                         "filename", filename,
	                         "flags", flags,
	                         NULL);

	if (internal_error) {
		g_propagate_error (error, internal_error);
		return NULL;
	}

	return object;
}

gint64
tracker_db_interface_sqlite_get_last_insert_id (TrackerDBInterface *interface)
{
	g_return_val_if_fail (TRACKER_IS_DB_INTERFACE (interface), 0);

	return (gint64) sqlite3_last_insert_rowid (interface->db);
}

static void
tracker_db_statement_finalize (GObject *object)
{
	TrackerDBStatement *stmt;

	stmt = TRACKER_DB_STATEMENT (object);

	/* A cursor was still open while we're being finalized, because a cursor
	 * holds its own reference, this means that somebody is unreffing a stmt
	 * too often. We mustn't sqlite3_finalize the priv->stmt in this case,
	 * though. It would crash&burn the cursor. */

	g_assert (!stmt->stmt_is_used);

	sqlite3_finalize (stmt->stmt);

	G_OBJECT_CLASS (tracker_db_statement_parent_class)->finalize (object);
}

static void
tracker_db_statement_class_init (TrackerDBStatementClass *class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (class);

	object_class->finalize = tracker_db_statement_finalize;
}

static TrackerDBStatement *
tracker_db_statement_sqlite_new (TrackerDBInterface *db_interface,
                                 sqlite3_stmt       *sqlite_stmt)
{
	TrackerDBStatement *stmt;

	stmt = g_object_new (TRACKER_TYPE_DB_STATEMENT, NULL);

	stmt->db_interface = db_interface;
	stmt->stmt = sqlite_stmt;
	stmt->stmt_is_used = FALSE;

	return stmt;
}

static TrackerDBStatement *
tracker_db_statement_sqlite_grab (TrackerDBStatement *stmt)
{
	g_assert (!stmt->stmt_is_used);
	stmt->stmt_is_used = TRUE;
	g_object_ref (stmt->db_interface);
	return g_object_ref (stmt);
}

static void
tracker_db_statement_sqlite_release (TrackerDBStatement *stmt)
{
	TrackerDBInterface *iface = stmt->db_interface;

	g_assert (stmt->stmt_is_used);
	stmt->stmt_is_used = FALSE;
	tracker_db_statement_sqlite_reset (stmt);
	g_object_unref (stmt);
	g_object_unref (iface);
}

static void
tracker_db_cursor_close (TrackerDBCursor *cursor)
{
	TrackerDBInterface *iface;

	g_return_if_fail (TRACKER_IS_DB_CURSOR (cursor));

	if (cursor->ref_stmt == NULL) {
		/* already closed */
		return;
	}

	iface = cursor->ref_stmt->db_interface;

	g_object_ref (iface);
	g_atomic_int_add (&iface->n_active_cursors, -1);

	tracker_db_interface_lock (iface);
	g_clear_pointer (&cursor->ref_stmt, tracker_db_statement_sqlite_release);
	tracker_db_interface_unlock (iface);

	g_object_unref (iface);
}

static void
tracker_db_cursor_finalize (GObject *object)
{
	TrackerDBCursor *cursor;
	int i;

	cursor = TRACKER_DB_CURSOR (object);

	tracker_db_cursor_close (cursor);

	g_free (cursor->types);

	for (i = 0; i < cursor->n_variable_names; i++) {
		g_free (cursor->variable_names[i]);
	}
	g_free (cursor->variable_names);

	G_OBJECT_CLASS (tracker_db_cursor_parent_class)->finalize (object);
}

static void
tracker_db_cursor_get_property (GObject    *object,
                                guint       prop_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
	TrackerDBCursor *cursor = TRACKER_DB_CURSOR (object);
	switch (prop_id) {
	case TRACKER_DB_CURSOR_PROP_N_COLUMNS:
		g_value_set_int (value, tracker_db_cursor_get_n_columns (cursor));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
tracker_db_cursor_iter_next_thread (GTask        *task,
                                    gpointer      object,
                                    gpointer      task_data,
                                    GCancellable *cancellable)
{
	/* run in thread */

	TrackerDBCursor *cursor = object;
	GError *error = NULL;
	gboolean result;

	result = db_cursor_iter_next (cursor, cancellable, &error);
	if (error) {
		g_task_return_error (task, error);
	} else {
		g_task_return_boolean (task, result);
	}
}

static void
tracker_db_cursor_iter_next_async (TrackerDBCursor     *cursor,
                                   GCancellable        *cancellable,
                                   GAsyncReadyCallback  callback,
                                   gpointer             user_data)
{
	GTask *task;

	task = g_task_new (G_OBJECT (cursor), cancellable, callback, user_data);
	g_task_run_in_thread (task, tracker_db_cursor_iter_next_thread);
	g_object_unref (task);
}

static gboolean
tracker_db_cursor_iter_next_finish (TrackerDBCursor  *cursor,
                                    GAsyncResult     *res,
                                    GError          **error)
{
	return g_task_propagate_boolean (G_TASK (res), error);
}

static void
tracker_db_cursor_class_init (TrackerDBCursorClass *class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (class);
	TrackerSparqlCursorClass *sparql_cursor_class = TRACKER_SPARQL_CURSOR_CLASS (class);

	object_class->finalize = tracker_db_cursor_finalize;
	object_class->get_property = tracker_db_cursor_get_property;

	sparql_cursor_class->get_value_type = (TrackerSparqlValueType (*) (TrackerSparqlCursor *, gint)) tracker_db_cursor_get_value_type;
	sparql_cursor_class->get_variable_name = (const gchar * (*) (TrackerSparqlCursor *, gint)) tracker_db_cursor_get_variable_name;
	sparql_cursor_class->get_n_columns = (gint (*) (TrackerSparqlCursor *)) tracker_db_cursor_get_n_columns;
	sparql_cursor_class->get_string = (const gchar * (*) (TrackerSparqlCursor *, gint, glong*)) tracker_db_cursor_get_string;
	sparql_cursor_class->next = (gboolean (*) (TrackerSparqlCursor *, GCancellable *, GError **)) tracker_db_cursor_iter_next;
	sparql_cursor_class->next_async = (void (*) (TrackerSparqlCursor *, GCancellable *, GAsyncReadyCallback, gpointer)) tracker_db_cursor_iter_next_async;
	sparql_cursor_class->next_finish = (gboolean (*) (TrackerSparqlCursor *, GAsyncResult *, GError **)) tracker_db_cursor_iter_next_finish;
	sparql_cursor_class->rewind = (void (*) (TrackerSparqlCursor *)) tracker_db_cursor_rewind;
	sparql_cursor_class->close = (void (*) (TrackerSparqlCursor *)) tracker_db_cursor_close;

	sparql_cursor_class->get_integer = (gint64 (*) (TrackerSparqlCursor *, gint)) tracker_db_cursor_get_int;
	sparql_cursor_class->get_double = (gdouble (*) (TrackerSparqlCursor *, gint)) tracker_db_cursor_get_double;
	sparql_cursor_class->get_boolean = (gboolean (*) (TrackerSparqlCursor *, gint)) tracker_db_cursor_get_boolean;

	g_object_class_override_property (object_class, TRACKER_DB_CURSOR_PROP_N_COLUMNS, "n-columns");
}

static TrackerDBCursor *
tracker_db_cursor_sqlite_new (TrackerDBStatement  *ref_stmt,
                              TrackerPropertyType *types,
                              gint                 n_types,
                              const gchar * const *variable_names,
                              gint                 n_variable_names)
{
	TrackerDBCursor *cursor;
	TrackerDBInterface *iface;

	iface = ref_stmt->db_interface;
	g_atomic_int_inc (&iface->n_active_cursors);

	cursor = g_object_new (TRACKER_TYPE_DB_CURSOR, NULL);

	cursor->finished = FALSE;

	cursor->stmt = ref_stmt->stmt;
	cursor->ref_stmt = tracker_db_statement_sqlite_grab (ref_stmt);

	if (types) {
		gint i;

		cursor->types = g_new (TrackerPropertyType, n_types);
		cursor->n_types = n_types;
		for (i = 0; i < n_types; i++) {
			cursor->types[i] = types[i];
		}
	}

	if (variable_names) {
		gint i;

		cursor->variable_names = g_new (gchar *, n_variable_names);
		cursor->n_variable_names = n_variable_names;
		for (i = 0; i < n_variable_names; i++) {
			cursor->variable_names[i] = g_strdup (variable_names[i]);
		}
	}

	return cursor;
}

void
tracker_db_statement_bind_double (TrackerDBStatement *stmt,
                                  int                 index,
                                  double              value)
{
	g_return_if_fail (TRACKER_IS_DB_STATEMENT (stmt));

	g_assert (!stmt->stmt_is_used);

	tracker_db_interface_lock (stmt->db_interface);
	sqlite3_bind_double (stmt->stmt, index + 1, value);
	tracker_db_interface_unlock (stmt->db_interface);
}

void
tracker_db_statement_bind_int (TrackerDBStatement *stmt,
                               int                 index,
                               gint64              value)
{
	g_return_if_fail (TRACKER_IS_DB_STATEMENT (stmt));

	g_assert (!stmt->stmt_is_used);

	tracker_db_interface_lock (stmt->db_interface);
	sqlite3_bind_int64 (stmt->stmt, index + 1, value);
	tracker_db_interface_unlock (stmt->db_interface);
}

void
tracker_db_statement_bind_null (TrackerDBStatement *stmt,
                                int                 index)
{
	g_return_if_fail (TRACKER_IS_DB_STATEMENT (stmt));

	g_assert (!stmt->stmt_is_used);

	tracker_db_interface_lock (stmt->db_interface);
	sqlite3_bind_null (stmt->stmt, index + 1);
	tracker_db_interface_unlock (stmt->db_interface);
}

void
tracker_db_statement_bind_text (TrackerDBStatement *stmt,
                                int                 index,
                                const gchar        *value)
{
	g_return_if_fail (TRACKER_IS_DB_STATEMENT (stmt));

	g_assert (!stmt->stmt_is_used);

	tracker_db_interface_lock (stmt->db_interface);
	sqlite3_bind_text (stmt->stmt, index + 1, value, -1, SQLITE_TRANSIENT);
	tracker_db_interface_unlock (stmt->db_interface);
}

void
tracker_db_statement_bind_value (TrackerDBStatement *stmt,
				 int                 index,
				 const GValue       *value)
{
	GType type;

	g_return_if_fail (TRACKER_IS_DB_STATEMENT (stmt));

	g_assert (!stmt->stmt_is_used);

	tracker_db_interface_lock (stmt->db_interface);

	type = G_VALUE_TYPE (value);

	if (type == G_TYPE_INT) {
		sqlite3_bind_int64 (stmt->stmt, index + 1, g_value_get_int (value));
	} else if (type == G_TYPE_INT64) {
		sqlite3_bind_int64 (stmt->stmt, index + 1, g_value_get_int64 (value));
	} else if (type == G_TYPE_DOUBLE) {
		sqlite3_bind_double (stmt->stmt, index + 1, g_value_get_double (value));
	} else if (type == G_TYPE_FLOAT) {
		sqlite3_bind_double (stmt->stmt, index + 1, g_value_get_float (value));
	} else if (type == G_TYPE_STRING) {
		sqlite3_bind_text (stmt->stmt, index + 1,
				   g_value_get_string (value), -1, SQLITE_TRANSIENT);
	} else {
		GValue dest = G_VALUE_INIT;

		g_value_init (&dest, G_TYPE_STRING);

		if (g_value_transform (value, &dest)) {
			sqlite3_bind_text (stmt->stmt, index + 1,
					   g_value_get_string (&dest), -1, SQLITE_TRANSIENT);
			g_value_unset (&dest);
		} else {
			g_assert_not_reached ();
		}
	}

	tracker_db_interface_unlock (stmt->db_interface);
}

void
tracker_db_cursor_rewind (TrackerDBCursor *cursor)
{
	TrackerDBInterface *iface;

	g_return_if_fail (TRACKER_IS_DB_CURSOR (cursor));

	iface = cursor->ref_stmt->db_interface;

	tracker_db_interface_lock (iface);

	sqlite3_reset (cursor->stmt);
	cursor->finished = FALSE;

	tracker_db_interface_unlock (iface);
}

gboolean
tracker_db_cursor_iter_next (TrackerDBCursor *cursor,
                             GCancellable    *cancellable,
                             GError         **error)
{
	if (!cursor) {
		return FALSE;
	}

	return db_cursor_iter_next (cursor, cancellable, error);
}


static gboolean
db_cursor_iter_next (TrackerDBCursor *cursor,
                     GCancellable    *cancellable,
                     GError         **error)
{
	TrackerDBStatement *stmt = cursor->ref_stmt;
	TrackerDBInterface *iface = stmt->db_interface;

	if (!cursor->finished) {
		guint result;

		tracker_db_interface_lock (iface);

		if (g_cancellable_is_cancelled (cancellable)) {
			result = SQLITE_INTERRUPT;
			sqlite3_reset (cursor->stmt);
		} else {
			/* only one statement can be active at the same time per interface */
			iface->cancellable = cancellable;
			result = stmt_step (cursor->stmt);
			iface->cancellable = NULL;
		}

		if (result == SQLITE_INTERRUPT) {
			g_set_error (error,
			             TRACKER_DB_INTERFACE_ERROR,
			             TRACKER_DB_INTERRUPTED,
			             "Interrupted");
		} else if (result != SQLITE_ROW && result != SQLITE_DONE) {
			g_set_error (error,
			             TRACKER_DB_INTERFACE_ERROR,
			             TRACKER_DB_QUERY_ERROR,
			             "%s", sqlite3_errmsg (iface->db));
		}

		cursor->finished = (result != SQLITE_ROW);

		tracker_db_interface_unlock (iface);
	}

	return (!cursor->finished);
}

guint
tracker_db_cursor_get_n_columns (TrackerDBCursor *cursor)
{
	return sqlite3_column_count (cursor->stmt);
}

void
tracker_db_cursor_get_value (TrackerDBCursor *cursor,
                             guint            column,
                             GValue          *value)
{
	gint col_type;

	col_type = sqlite3_column_type (cursor->stmt, column);

	switch (col_type) {
	case SQLITE_TEXT:
		g_value_init (value, G_TYPE_STRING);
		g_value_set_string (value, (gchar *) sqlite3_column_text (cursor->stmt, column));
		break;
	case SQLITE_INTEGER:
		g_value_init (value, G_TYPE_INT64);
		g_value_set_int64 (value, sqlite3_column_int64 (cursor->stmt, column));
		break;
	case SQLITE_FLOAT:
		g_value_init (value, G_TYPE_DOUBLE);
		g_value_set_double (value, sqlite3_column_double (cursor->stmt, column));
		break;
	case SQLITE_NULL:
		/* just ignore NULLs */
		break;
	default:
		g_critical ("Unknown sqlite3 database column type:%d", col_type);
	}

}

gint64
tracker_db_cursor_get_int (TrackerDBCursor *cursor,
                           guint            column)
{
	TrackerDBInterface *iface;
	gint64 result;

	iface = cursor->ref_stmt->db_interface;

	tracker_db_interface_lock (iface);

	result = (gint64) sqlite3_column_int64 (cursor->stmt, column);

	tracker_db_interface_unlock (iface);

	return result;
}

gdouble
tracker_db_cursor_get_double (TrackerDBCursor *cursor,
                              guint            column)
{
	TrackerDBInterface *iface;
	gdouble result;

	iface = cursor->ref_stmt->db_interface;

	tracker_db_interface_lock (iface);

	result = (gdouble) sqlite3_column_double (cursor->stmt, column);

	tracker_db_interface_unlock (iface);

	return result;
}

static gboolean
tracker_db_cursor_get_boolean (TrackerSparqlCursor *sparql_cursor,
                               guint                column)
{
	TrackerDBCursor *cursor = (TrackerDBCursor *) sparql_cursor;
	return (g_strcmp0 (tracker_db_cursor_get_string (cursor, column, NULL), "true") == 0);
}

TrackerSparqlValueType
tracker_db_cursor_get_value_type (TrackerDBCursor *cursor,
                                  guint            column)
{
	TrackerDBInterface *iface;
	gint column_type;
	gint n_columns = sqlite3_column_count (cursor->stmt);

	g_return_val_if_fail (column < n_columns, TRACKER_SPARQL_VALUE_TYPE_UNBOUND);

	iface = cursor->ref_stmt->db_interface;

	tracker_db_interface_lock (iface);

	column_type = sqlite3_column_type (cursor->stmt, column);

	tracker_db_interface_unlock (iface);

	if (column_type == SQLITE_NULL) {
		return TRACKER_SPARQL_VALUE_TYPE_UNBOUND;
	} else if (column < cursor->n_types) {
		switch (cursor->types[column]) {
		case TRACKER_PROPERTY_TYPE_RESOURCE:
			return TRACKER_SPARQL_VALUE_TYPE_URI;
		case TRACKER_PROPERTY_TYPE_INTEGER:
			return TRACKER_SPARQL_VALUE_TYPE_INTEGER;
		case TRACKER_PROPERTY_TYPE_DOUBLE:
			return TRACKER_SPARQL_VALUE_TYPE_DOUBLE;
		case TRACKER_PROPERTY_TYPE_DATETIME:
			return TRACKER_SPARQL_VALUE_TYPE_DATETIME;
		case TRACKER_PROPERTY_TYPE_BOOLEAN:
			return TRACKER_SPARQL_VALUE_TYPE_BOOLEAN;
		default:
			return TRACKER_SPARQL_VALUE_TYPE_STRING;
		}
	} else {
		return TRACKER_SPARQL_VALUE_TYPE_STRING;
	}
}

const gchar*
tracker_db_cursor_get_variable_name (TrackerDBCursor *cursor,
                                     guint            column)
{
	TrackerDBInterface *iface;
	const gchar *result;

	iface = cursor->ref_stmt->db_interface;

	tracker_db_interface_lock (iface);

	if (column < cursor->n_variable_names) {
		result = cursor->variable_names[column];
	} else {
		result = sqlite3_column_name (cursor->stmt, column);
	}

	tracker_db_interface_unlock (iface);

	return result;
}

const gchar*
tracker_db_cursor_get_string (TrackerDBCursor *cursor,
                              guint            column,
                              glong           *length)
{
	TrackerDBInterface *iface;
	const gchar *result;

	iface = cursor->ref_stmt->db_interface;

	tracker_db_interface_lock (iface);

	if (length) {
		sqlite3_value *val = sqlite3_column_value (cursor->stmt, column);

		*length = sqlite3_value_bytes (val);
		result = (const gchar *) sqlite3_value_text (val);
	} else {
		result = (const gchar *) sqlite3_column_text (cursor->stmt, column);
	}

	tracker_db_interface_unlock (iface);

	return result;
}

void
tracker_db_statement_execute (TrackerDBStatement  *stmt,
                              GError             **error)
{
	g_return_if_fail (TRACKER_IS_DB_STATEMENT (stmt));
	g_return_if_fail (!stmt->stmt_is_used);

	execute_stmt (stmt->db_interface, stmt->stmt, NULL, error);
}

TrackerDBCursor *
tracker_db_statement_start_cursor (TrackerDBStatement  *stmt,
                                   GError             **error)
{
	g_return_val_if_fail (TRACKER_IS_DB_STATEMENT (stmt), NULL);
	g_return_val_if_fail (!stmt->stmt_is_used, NULL);

	return tracker_db_cursor_sqlite_new (stmt, NULL, 0, NULL, 0);
}

TrackerDBCursor *
tracker_db_statement_start_sparql_cursor (TrackerDBStatement   *stmt,
                                          TrackerPropertyType  *types,
                                          gint                  n_types,
                                          const gchar * const  *variable_names,
                                          gint                  n_variable_names,
                                          GError              **error)
{
	g_return_val_if_fail (TRACKER_IS_DB_STATEMENT (stmt), NULL);
	g_return_val_if_fail (!stmt->stmt_is_used, NULL);

	return tracker_db_cursor_sqlite_new (stmt, types, n_types, variable_names, n_variable_names);
}

static void
tracker_db_statement_init (TrackerDBStatement *stmt)
{
}

static void
tracker_db_cursor_init (TrackerDBCursor *cursor)
{
}

static void
tracker_db_statement_sqlite_reset (TrackerDBStatement *stmt)
{
	g_assert (!stmt->stmt_is_used);

	sqlite3_reset (stmt->stmt);
	sqlite3_clear_bindings (stmt->stmt);
}


void
tracker_db_interface_set_user_data (TrackerDBInterface *db_interface,
                                    gpointer            user_data,
                                    GDestroyNotify      destroy_notify)
{
	if (db_interface->user_data && db_interface->user_data_destroy_notify)
		db_interface->user_data_destroy_notify (db_interface->user_data);

	db_interface->user_data = user_data;
	db_interface->user_data_destroy_notify = destroy_notify;
}

gpointer
tracker_db_interface_get_user_data (TrackerDBInterface *db_interface)
{
	return db_interface->user_data;
}

gboolean
tracker_db_interface_get_is_used (TrackerDBInterface *db_interface)
{
	return g_atomic_int_get (&db_interface->n_active_cursors) > 0;
}

gboolean
tracker_db_interface_init_vtabs (TrackerDBInterface *db_interface,
                                 TrackerOntologies  *ontologies)
{
	tracker_vtab_triples_init (db_interface->db, ontologies);
	return TRUE;
}
