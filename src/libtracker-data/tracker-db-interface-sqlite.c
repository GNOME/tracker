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
#elif HAVE_LIBICU
#include <unicode/utypes.h>
#include <unicode/uregex.h>
#include <unicode/ustring.h>
#include <unicode/ucol.h>
#endif

#include "tracker-collation.h"

#include "tracker-db-interface-sqlite.h"
#include "tracker-db-manager.h"

#define UNKNOWN_STATUS 0.5

typedef struct {
	TrackerDBStatement *head;
	TrackerDBStatement *tail;
	guint size;
	guint max;
} TrackerDBStatementLru;

struct TrackerDBInterface {
	GObject parent_instance;

	gchar *filename;
	sqlite3 *db;

	GHashTable *dynamic_statements;

	GSList *function_data;

	/* Collation and locale change */
	gpointer locale_notification_id;
	gint collator_reset_requested;

	/* Number of active cursors */
	gint n_active_cursors;

	guint ro : 1;
	GCancellable *cancellable;

	TrackerDBStatementLru select_stmt_lru;
	TrackerDBStatementLru update_stmt_lru;

	TrackerBusyCallback busy_callback;
	gpointer busy_user_data;
	gchar *busy_status;

	gchar *fts_insert_str;
	gchar *fts_delete_str;
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

	/* used for direct access as libtracker-sparql is thread-safe and
	   uses a single shared connection with SQLite mutex disabled */
	gboolean threadsafe;
};

struct TrackerDBCursorClass {
	TrackerSparqlCursorClass parent_class;
};

struct TrackerDBStatement {
	GObject parent_instance;
	TrackerDBInterface *db_interface;
	sqlite3_stmt *stmt;
	gboolean stmt_is_sunk;
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
static TrackerDBCursor    *tracker_db_cursor_sqlite_new             (sqlite3_stmt          *sqlite_stmt,
                                                                     TrackerDBStatement    *ref_stmt,
                                                                     TrackerPropertyType   *types,
                                                                     gint                   n_types,
                                                                     const gchar          **variable_names,
                                                                     gint                   n_variable_names,
                                                                     gboolean               threadsafe);
static gboolean            tracker_db_cursor_get_boolean            (TrackerSparqlCursor   *cursor,
                                                                     guint                  column);
static gboolean            db_cursor_iter_next                      (TrackerDBCursor       *cursor,
                                                                     GCancellable          *cancellable,
                                                                     GError               **error);

enum {
	PROP_0,
	PROP_FILENAME,
	PROP_RO
};

enum {
	TRACKER_DB_CURSOR_PROP_0,
	TRACKER_DB_CURSOR_PROP_N_COLUMNS
};

G_DEFINE_TYPE_WITH_CODE (TrackerDBInterface, tracker_db_interface, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE,
                                                tracker_db_interface_initable_iface_init));

G_DEFINE_TYPE (TrackerDBStatement, tracker_db_statement, G_TYPE_OBJECT)

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

	separator = sqlite3_value_text (argv[argc-1]);

	for (i = 0;i < argc-1; i++) {
		if (sqlite3_value_type (argv[argc-1]) == SQLITE_TEXT) {
			const gchar *text = sqlite3_value_text (argv[i]);

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

	name = g_filename_display_basename (sqlite3_value_text (argv[0]));

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

	parent = sqlite3_value_text (argv[0]);
	uri = sqlite3_value_text (argv[1]);

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

	if (sqlite3_value_type (argv[argc-1]) != SQLITE_TEXT) {
		sqlite3_result_error (context, "Invalid child", -1);
		return;
	}

	if (sqlite3_value_type (argv[0]) != SQLITE_TEXT) {
		sqlite3_result_error (context, "Invalid first parent", -1);
		return;
	}

	child = sqlite3_value_text (argv[argc-1]);

	for (i = 0; i < argc - 1 && !match; i++) {
		if (sqlite3_value_type (argv[i]) == SQLITE_TEXT) {
			const gchar *parent = sqlite3_value_text (argv[i]);
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
	const gchar *text, *pattern, *flags;
	GRegexCompileFlags regex_flags;
	GRegex *regex;

	if (argc != 3) {
		sqlite3_result_error (context, "Invalid argument count", -1);
		return;
	}

	regex = sqlite3_get_auxdata (context, 1);

	text = sqlite3_value_text (argv[0]);
	flags = sqlite3_value_text (argv[2]);

	if (regex == NULL) {
		gchar *err_str;
		GError *error = NULL;

		pattern = sqlite3_value_text (argv[1]);

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

	ret = g_regex_match (regex, text, 0, NULL);

	sqlite3_result_int (context, ret);
}

static void
function_sparql_replace(sqlite3_context *context,
                        int             argc,
                        sqlite3_value   *argv[])
{
	const gchar *string_input;
	const gchar *pattern;
	const gchar *new_substring;
	const gchar *flags;
	gchar *string_output;
	gchar *err_str;
	GError *error = NULL;
	GRegexCompileFlags regex_flags;
	GRegex *regex;

	if (argc != 4) {
		sqlite3_result_error (context, "Invalid argument count", -1);
		return;
	}

	string_input = sqlite3_value_text (argv[0]);
	regex = sqlite3_get_auxdata (context, 1);
	new_substring = sqlite3_value_text (argv[2]);
	flags = sqlite3_value_text (argv[3]);

	if (regex == NULL) {
		pattern = sqlite3_value_text (argv[1]);
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

	string_output = g_regex_replace (regex, string_input, -1, 0, new_substring, 0, &error);

	if(error){
		sqlite3_result_error (context, error->message, -1);
		g_clear_error (&error);
		return;
	}
	sqlite3_result_text (context, string_output, -1, free);
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

#elif HAVE_LIBICU

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

static void
function_sparql_normalize (sqlite3_context *context,
                           int              argc,
                           sqlite3_value   *argv[])
{
	const gchar *nfstr;
	const uint16_t *zInput;
	uint16_t *zOutput;
	int nInput;
	int nOutput;
	UNormalizationMode nf;
	UErrorCode status = U_ZERO_ERROR;

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
		nf = UNORM_NFC;
	else if (g_ascii_strcasecmp (nfstr, "nfd") == 0)
		nf = UNORM_NFD;
	else if (g_ascii_strcasecmp (nfstr, "nfkc") == 0)
		nf = UNORM_NFKC;
	else if (g_ascii_strcasecmp (nfstr, "nfkd") == 0)
		nf = UNORM_NFKD;
	else {
		sqlite3_result_error (context, "Invalid normalization specified", -1);
		return;
	}

	nInput = sqlite3_value_bytes16 (argv[0]);

	nOutput = nInput * 2 + 2;
	zOutput = sqlite3_malloc (nOutput);

	if (!zOutput) {
		return;
	}

	unorm_normalize (zInput, nInput/2, nf, 0, zOutput, nOutput/2, &status);
	if (!U_SUCCESS (status)) {
		char zBuf[128];
		sqlite3_snprintf (128, zBuf, "ICU error: unorm_normalize: %s", u_errorName (status));
		zBuf[127] = '\0';
		sqlite3_free (zOutput);
		sqlite3_result_error (context, zBuf, -1);
		return;
	}

	sqlite3_result_text16 (context, zOutput, -1, sqlite3_free);
}

static void
function_sparql_unaccent (sqlite3_context *context,
                          int              argc,
                          sqlite3_value   *argv[])
{
	const gchar *nfstr;
	const uint16_t *zInput;
	uint16_t *zOutput;
	int nInput;
	gsize nOutput;
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

	nOutput = unorm_normalize (zInput, nInput/2, UNORM_NFKD, 0, zOutput, nOutput/2, &status);
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

	sqlite3_result_text16 (context, zOutput, -1, sqlite3_free);
}

#endif

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

static int
check_interrupt (void *user_data)
{
	TrackerDBInterface *db_interface = user_data;

	if (db_interface->busy_callback) {
		db_interface->busy_callback (db_interface->busy_status,
		                             UNKNOWN_STATUS, /* No idea to get the status from SQLite */
		                             db_interface->busy_user_data);
	}

	return g_cancellable_is_cancelled (db_interface->cancellable) ? 1 : 0;
}

static void
open_database (TrackerDBInterface  *db_interface,
               GError             **error)
{
	int mode;
	int result;

	g_assert (db_interface->filename != NULL);

	if (!db_interface->ro) {
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
		g_message ("Opened sqlite3 database:'%s'", db_interface->filename);
	}

	/* Set our unicode collation function */
	tracker_db_interface_sqlite_reset_collator (db_interface);

	sqlite3_progress_handler (db_interface->db, 100,
	                          check_interrupt, db_interface);

	sqlite3_create_function (db_interface->db, "SparqlRegex", 3, SQLITE_ANY,
	                         db_interface, &function_sparql_regex,
	                         NULL, NULL);

	sqlite3_create_function (db_interface->db, "SparqlHaversineDistance", 4, SQLITE_ANY,
	                         db_interface, &function_sparql_haversine_distance,
	                         NULL, NULL);

	sqlite3_create_function (db_interface->db, "SparqlCartesianDistance", 4, SQLITE_ANY,
	                         db_interface, &function_sparql_cartesian_distance,
	                         NULL, NULL);

	sqlite3_create_function (db_interface->db, "SparqlStringFromFilename", 1, SQLITE_ANY,
	                         db_interface, &function_sparql_string_from_filename,
	                         NULL, NULL);

	sqlite3_create_function (db_interface->db, "SparqlStringJoin", -1, SQLITE_ANY,
	                         db_interface, &function_sparql_string_join,
	                         NULL, NULL);

	sqlite3_create_function (db_interface->db, "SparqlUriIsParent", 2, SQLITE_ANY,
	                         db_interface, &function_sparql_uri_is_parent,
	                         NULL, NULL);

	sqlite3_create_function (db_interface->db, "SparqlUriIsDescendant", -1, SQLITE_ANY,
	                         db_interface, &function_sparql_uri_is_descendant,
	                         NULL, NULL);

	sqlite3_create_function (db_interface->db, "SparqlLowerCase", 1, SQLITE_ANY,
	                         db_interface, &function_sparql_lower_case,
	                         NULL, NULL);

	sqlite3_create_function (db_interface->db, "SparqlCaseFold", 1, SQLITE_ANY,
	                         db_interface, &function_sparql_case_fold,
	                         NULL, NULL);

	sqlite3_create_function (db_interface->db, "SparqlNormalize", 2, SQLITE_ANY,
	                         db_interface, &function_sparql_normalize,
	                         NULL, NULL);

	sqlite3_create_function (db_interface->db, "SparqlUnaccent", 1, SQLITE_ANY,
	                         db_interface, &function_sparql_unaccent,
	                         NULL, NULL);

	sqlite3_create_function (db_interface->db, "SparqlFormatTime", 1, SQLITE_ANY,
	                         db_interface, &function_sparql_format_time,
	                         NULL, NULL);

	sqlite3_create_function (db_interface->db, "SparqlReplace", 4, SQLITE_ANY,
	                         db_interface, &function_sparql_replace,
	                         NULL, NULL);

	sqlite3_extended_result_codes (db_interface->db, 0);
	sqlite3_busy_timeout (db_interface->db, 100000);
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
	case PROP_RO:
		db_iface->ro = g_value_get_boolean (value);
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
	case PROP_RO:
		g_value_set_boolean (value, db_iface->ro);
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

	if (db_interface->function_data) {
		g_slist_foreach (db_interface->function_data, (GFunc) g_free, NULL);
		g_slist_free (db_interface->function_data);
		db_interface->function_data = NULL;
	}

	if (db_interface->db) {
		rc = sqlite3_close (db_interface->db);
		g_warn_if_fail (rc == SQLITE_OK);
	}
}

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

void
tracker_db_interface_sqlite_fts_init (TrackerDBInterface  *db_interface,
                                      GHashTable          *properties,
                                      GHashTable          *multivalued,
                                      gboolean             create)
{
#if HAVE_TRACKER_FTS
	GStrv fts_columns;

	tracker_fts_init_db (db_interface->db, properties);

	if (create &&
	    !tracker_fts_create_table (db_interface->db, "fts",
				       properties, multivalued)) {
		g_warning ("FTS tables creation failed");
	}

	fts_columns = _fts_create_properties (properties);

	if (fts_columns) {
		GString *insert, *select, *delete, cols;
		gint i = 0;

		insert = g_string_new ("INSERT INTO fts (docid");
		select = g_string_new ("SELECT rowid");
		delete = g_string_new ("UPDATE fts SET docid=?");

		while (fts_columns[i]) {
			g_string_append_printf (insert, ", \"%s\"",
						fts_columns[i]);
			g_string_append_printf (select, ", \"%s\"",
						fts_columns[i]);
			g_string_append_printf (delete, ", \"%s\"=\"\"",
						fts_columns[i]);
			i++;
		}

		g_string_append (select, " FROM fts_view WHERE rowid=?");
		g_string_append (insert, ") ");
		g_string_append (insert, select->str);

		g_string_free (select, TRUE);
		db_interface->fts_insert_str = g_string_free (insert, FALSE);

		g_string_append (delete, " WHERE docid=?");
		db_interface->fts_delete_str = g_string_free (delete, FALSE);

		g_strfreev (fts_columns);
	}
#endif
}

#if HAVE_TRACKER_FTS

void
tracker_db_interface_sqlite_fts_alter_table (TrackerDBInterface  *db_interface,
					     GHashTable          *properties,
					     GHashTable          *multivalued)
{
	if (!tracker_fts_alter_table (db_interface->db, "fts", properties, multivalued)) {
		g_critical ("Failed to update FTS columns");
	}
}

gboolean
tracker_db_interface_sqlite_fts_update_text (TrackerDBInterface  *db_interface,
                                             int                  id,
                                             const gchar        **properties,
                                             const gchar        **text,
                                             gboolean             create)
{
	TrackerDBStatement *stmt;
	GError *error = NULL;

	if (!create) {
		stmt = tracker_db_interface_create_statement (db_interface,
							      TRACKER_DB_STATEMENT_CACHE_TYPE_UPDATE,
							      &error,
							      "DELETE FROM fts WHERE docid=?");

		if (!stmt || error) {
			if (error) {
				g_warning ("Could not create FTS update statement: %s",
				           error->message);
				g_error_free (error);
			}
			return FALSE;
		}

		tracker_db_statement_bind_int (stmt, 0, id);
		tracker_db_statement_execute (stmt, &error);
		g_object_unref (stmt);

		if (error) {
			g_warning ("Could not update FTS text: %s", error->message);
			g_error_free (error);
			return FALSE;
		}
	}

	stmt = tracker_db_interface_create_statement (db_interface,
	                                              TRACKER_DB_STATEMENT_CACHE_TYPE_UPDATE,
	                                              &error,
	                                              "%s",
	                                              db_interface->fts_insert_str);

	if (!stmt || error) {
		if (error) {
			g_warning ("Could not create FTS insert statement: %s\n",
			           error->message);
			g_error_free (error);
		}
		return FALSE;
	}

	tracker_db_statement_bind_int (stmt, 0, id);
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
tracker_db_interface_sqlite_fts_delete_text (TrackerDBInterface *db_interface,
                                             int                 id,
					     const gchar        *property)
{
	TrackerDBStatement *stmt;
	GError *error = NULL;

	stmt = tracker_db_interface_create_statement (db_interface,
	                                              TRACKER_DB_STATEMENT_CACHE_TYPE_UPDATE,
	                                              &error,
	                                              "UPDATE fts SET \"%s\" = '' WHERE docid = ?",
	                                              property);

	if (!stmt || error) {
		if (error) {
			g_warning ("Could not create FTS update statement: %s\n",
			           error->message);
			g_error_free (error);
		}
		return FALSE;
	}

	tracker_db_statement_bind_int (stmt, 0, id);
	tracker_db_statement_execute (stmt, &error);
	g_object_unref (stmt);

	if (error) {
		g_warning ("Could not execute FTS update: %s", error->message);
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

	stmt = tracker_db_interface_create_statement (db_interface,
	                                              TRACKER_DB_STATEMENT_CACHE_TYPE_UPDATE,
	                                              &error,
	                                              "%s",
	                                              db_interface->fts_delete_str);
	if (!stmt || error) {
		if (error) {
			g_warning ("Could not create FTS delete statement: %s",
			           error->message);
			g_error_free (error);
		}
		return FALSE;
	}

	tracker_db_statement_bind_int (stmt, 0, id);
	tracker_db_statement_bind_int (stmt, 1, id);
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
	tracker_fts_rebuild_tokens (interface->db, "fts");
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
}

static gint
wal_hook (gpointer     user_data,
          sqlite3     *db,
          const gchar *db_name,
          gint         n_pages)
{
	((TrackerDBWalCallback) user_data) (n_pages);

	return SQLITE_OK;
}

void
tracker_db_interface_sqlite_wal_hook (TrackerDBInterface   *interface,
                                      TrackerDBWalCallback  callback)
{
	sqlite3_wal_hook (interface->db, wal_hook, callback);
}


static void
tracker_db_interface_sqlite_finalize (GObject *object)
{
	TrackerDBInterface *db_interface;

	db_interface = TRACKER_DB_INTERFACE (object);

	close_database (db_interface);
	g_free (db_interface->fts_insert_str);
	g_free (db_interface->fts_delete_str);

	g_message ("Closed sqlite3 database:'%s'", db_interface->filename);

	g_free (db_interface->filename);
	g_free (db_interface->busy_status);

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
	                                 PROP_RO,
	                                 g_param_spec_boolean ("read-only",
	                                                       "Read only",
	                                                       "Whether the connection is read only",
	                                                       FALSE,
	                                                       G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
}

static void
prepare_database (TrackerDBInterface *db_interface)
{
	db_interface->dynamic_statements = g_hash_table_new_full (g_str_hash, g_str_equal,
	                                                          NULL,
	                                                          (GDestroyNotify) g_object_unref);
}

static void
tracker_db_interface_init (TrackerDBInterface *db_interface)
{
	db_interface->ro = FALSE;

	prepare_database (db_interface);
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

void
tracker_db_interface_set_busy_handler (TrackerDBInterface  *db_interface,
                                       TrackerBusyCallback  busy_callback,
                                       const gchar         *busy_status,
                                       gpointer             busy_user_data)
{
	g_return_if_fail (TRACKER_IS_DB_INTERFACE (db_interface));
	db_interface->busy_callback = busy_callback;
	db_interface->busy_user_data = busy_user_data;
	g_free (db_interface->busy_status);

	if (busy_status) {
		db_interface->busy_status = g_strdup (busy_status);
	} else {
		db_interface->busy_status = NULL;
	}
}

TrackerDBStatement *
tracker_db_interface_create_statement (TrackerDBInterface           *db_interface,
                                       TrackerDBStatementCacheType   cache_type,
                                       GError                      **error,
                                       const gchar                  *query,
                                       ...)
{
	TrackerDBStatementLru *stmt_lru = NULL;
	TrackerDBStatement *stmt;
	va_list args;
	gchar *full_query;

	g_return_val_if_fail (TRACKER_IS_DB_INTERFACE (db_interface), NULL);

	va_start (args, query);
	full_query = g_strdup_vprintf (query, args);
	va_end (args);

	/* There are three kinds of queries:
	 * a) Cached queries: SELECT and UPDATE ones (cache_type)
	 * b) Non-Cached queries: NONE ones (cache_type)
	 * c) Forced Non-Cached: in case of a stmt being already in use, we can't
	 *    reuse it (you can't use two different loops on a sqlite3_stmt, of
	 *    course). This happens with recursive uses of a cursor, for example */

	if (cache_type != TRACKER_DB_STATEMENT_CACHE_TYPE_NONE) {
		stmt = g_hash_table_lookup (db_interface->dynamic_statements, full_query);

		if (stmt && stmt->stmt_is_sunk) {
			/* c) Forced non-cached
			 * prepared statement is still in use, create new uncached one */
			stmt = NULL;
			/* Make sure to set cache_type here, to ensure the right flow in the
			 * LRU-cache below */
			cache_type = TRACKER_DB_STATEMENT_CACHE_TYPE_NONE;
		}

		if (cache_type == TRACKER_DB_STATEMENT_CACHE_TYPE_UPDATE) {
			/* a) Cached */
			stmt_lru = &db_interface->update_stmt_lru;
		} else {
			/* a) Cached */
			stmt_lru = &db_interface->select_stmt_lru;
		}

	} else {
		/* b) Non-Cached */
		stmt = NULL;
	}

	if (!stmt) {
		sqlite3_stmt *sqlite_stmt;
		int retval;

		g_debug ("Preparing query: '%s'", full_query);

		retval = sqlite3_prepare_v2 (db_interface->db, full_query, -1, &sqlite_stmt, NULL);

		if (retval != SQLITE_OK) {

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

			g_free (full_query);

			return NULL;
		}

		stmt = tracker_db_statement_sqlite_new (db_interface, sqlite_stmt);

		if (cache_type != TRACKER_DB_STATEMENT_CACHE_TYPE_NONE) {
			/* use replace instead of insert to make sure we store the string that
			   belongs to the right sqlite statement to ensure the lifetime of the string
			   matches the statement */
			g_hash_table_replace (db_interface->dynamic_statements,
			                      (gpointer) sqlite3_sql (sqlite_stmt),
			                      stmt);

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
				 * Then we assign head->next as new head. */

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
			 * current tail, and we set tail to current stmt. */

			stmt_lru->size++;
			stmt->next = stmt_lru->head;
			stmt_lru->head->prev = stmt;

			stmt_lru->tail->next = stmt;
			stmt->prev = stmt_lru->tail;
			stmt_lru->tail = stmt;
		}
	} else {
		tracker_db_statement_sqlite_reset (stmt);

		if (cache_type != TRACKER_DB_STATEMENT_CACHE_TYPE_NONE) {
			if (stmt == stmt_lru->head) {

				/* Current stmt is least recently used, shift head and tail
				 * of the ring to efficiently make it most recently used. */

				stmt_lru->head = stmt_lru->head->next;
				stmt_lru->tail = stmt_lru->tail->next;
			} else if (stmt != stmt_lru->tail) {

				/* Current statement isn't most recently used, make it most
				 * recently used now (less efficient way than above). */

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
	}

	g_free (full_query);

	return (cache_type != TRACKER_DB_STATEMENT_CACHE_TYPE_NONE) ? g_object_ref (stmt) : stmt;
}

static void
execute_stmt (TrackerDBInterface  *interface,
              sqlite3_stmt        *stmt,
              GCancellable        *cancellable,
              GError             **error)
{
	gint result;

	result = SQLITE_OK;

	/* Statement is going to start, check if we got a request to reset the
	 * collator, and if so, do it. */
	if (g_atomic_int_add (&interface->n_active_cursors, 1) == 0 &&
	    g_atomic_int_compare_and_exchange (&(interface->collator_reset_requested), TRUE, FALSE)) {
		tracker_db_interface_sqlite_reset_collator (interface);
	}

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


	if (result == SQLITE_DONE) {
		/* Statement finished, check if we got a request to reset the
		 * collator, and if so, do it.
		 */
		if (g_atomic_int_dec_and_test (&interface->n_active_cursors) &&
		    g_atomic_int_compare_and_exchange (&(interface->collator_reset_requested), TRUE, FALSE)) {
			tracker_db_interface_sqlite_reset_collator (interface);
		}
	} else {
		g_atomic_int_add (&interface->n_active_cursors, -1);

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
	int retval;

	full_query = g_strdup_vprintf (query, args);

	/* g_debug ("Running query: '%s'", full_query); */
	retval = sqlite3_prepare_v2 (db_interface->db, full_query, -1, &stmt, NULL);

	if (retval != SQLITE_OK) {
		g_set_error (error,
		             TRACKER_DB_INTERFACE_ERROR,
		             TRACKER_DB_QUERY_ERROR,
		             "%s",
		             sqlite3_errmsg (db_interface->db));
		g_free (full_query);
		return;
	} else if (stmt == NULL) {
		g_set_error (error,
		             TRACKER_DB_INTERFACE_ERROR,
		             TRACKER_DB_QUERY_ERROR,
		             "Could not prepare SQL statement:'%s'",
		             full_query);

		g_free (full_query);
		return;
	}

	execute_stmt (db_interface, stmt, NULL, error);
	sqlite3_finalize (stmt);

	g_free (full_query);
}

TrackerDBInterface *
tracker_db_interface_sqlite_new (const gchar  *filename,
                                 GError      **error)
{
	TrackerDBInterface *object;
	GError *internal_error = NULL;

	object = g_initable_new (TRACKER_TYPE_DB_INTERFACE,
	                         NULL,
	                         &internal_error,
	                         "filename", filename,
	                         NULL);

	if (internal_error) {
		g_propagate_error (error, internal_error);
		return NULL;
	}

	return object;
}

TrackerDBInterface *
tracker_db_interface_sqlite_new_ro (const gchar  *filename,
                                    GError      **error)
{
	TrackerDBInterface *object;
	GError *internal_error = NULL;

	object = g_initable_new (TRACKER_TYPE_DB_INTERFACE,
	                         NULL,
	                         &internal_error,
	                         "filename", filename,
	                         "read-only", TRUE,
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

	g_assert (!stmt->stmt_is_sunk);

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
	stmt->stmt_is_sunk = FALSE;

	return stmt;
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

	/* As soon as we finalize the cursor, check if we need a collator reset
	 * and notify the iface about the removed cursor */
	iface = cursor->ref_stmt->db_interface;
	if (g_atomic_int_dec_and_test (&iface->n_active_cursors) &&
	    g_atomic_int_compare_and_exchange (&(iface->collator_reset_requested), TRUE, FALSE)) {
		tracker_db_interface_sqlite_reset_collator (iface);
	}

	if (cursor->threadsafe) {
		tracker_db_manager_lock ();
	}

	cursor->ref_stmt->stmt_is_sunk = FALSE;
	tracker_db_statement_sqlite_reset (cursor->ref_stmt);
	g_object_unref (cursor->ref_stmt);
	cursor->ref_stmt = NULL;

	if (cursor->threadsafe) {
		tracker_db_manager_unlock ();
	}
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
tracker_db_cursor_iter_next_thread (GSimpleAsyncResult *res,
                                    GObject            *object,
                                    GCancellable       *cancellable)
{
	/* run in thread */

	TrackerDBCursor *cursor = TRACKER_DB_CURSOR (object);
	GError *error = NULL;
	gboolean result;

	result = db_cursor_iter_next (cursor, cancellable, &error);
	if (error) {
		g_simple_async_result_set_from_error (res, error);
	} else {
		g_simple_async_result_set_op_res_gboolean (res, result);
	}
}

static void
tracker_db_cursor_iter_next_async (TrackerDBCursor     *cursor,
                                   GCancellable        *cancellable,
                                   GAsyncReadyCallback  callback,
                                   gpointer             user_data)
{
	GSimpleAsyncResult *res;

	res = g_simple_async_result_new (G_OBJECT (cursor), callback, user_data, tracker_db_cursor_iter_next_async);
	g_simple_async_result_run_in_thread (res, tracker_db_cursor_iter_next_thread, 0, cancellable);
	g_object_unref (res);
}

static gboolean
tracker_db_cursor_iter_next_finish (TrackerDBCursor  *cursor,
                                    GAsyncResult     *res,
                                    GError          **error)
{
	if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error)) {
		return FALSE;
	}
	return g_simple_async_result_get_op_res_gboolean (G_SIMPLE_ASYNC_RESULT (res));
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
tracker_db_cursor_sqlite_new (sqlite3_stmt        *sqlite_stmt,
                              TrackerDBStatement  *ref_stmt,
                              TrackerPropertyType *types,
                              gint                 n_types,
                              const gchar        **variable_names,
                              gint                 n_variable_names,
                              gboolean             threadsafe)
{
	TrackerDBCursor *cursor;
	TrackerDBInterface *iface;

	/* As soon as we create a cursor, check if we need a collator reset
	 * and notify the iface about the new cursor */
	iface = ref_stmt->db_interface;
	if (g_atomic_int_add (&iface->n_active_cursors, 1) == 0 &&
	    g_atomic_int_compare_and_exchange (&(iface->collator_reset_requested), TRUE, FALSE)) {
		tracker_db_interface_sqlite_reset_collator (iface);
	}

	cursor = g_object_new (TRACKER_TYPE_DB_CURSOR, NULL);

	cursor->finished = FALSE;

	/* used for direct access as libtracker-sparql is thread-safe and
	   uses a single shared connection with SQLite mutex disabled */
	cursor->threadsafe = threadsafe;

	cursor->stmt = sqlite_stmt;
	ref_stmt->stmt_is_sunk = TRUE;
	cursor->ref_stmt = g_object_ref (ref_stmt);

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

	g_assert (!stmt->stmt_is_sunk);

	sqlite3_bind_double (stmt->stmt, index + 1, value);
}

void
tracker_db_statement_bind_int (TrackerDBStatement *stmt,
                               int                 index,
                               gint64              value)
{
	g_return_if_fail (TRACKER_IS_DB_STATEMENT (stmt));

	g_assert (!stmt->stmt_is_sunk);

	sqlite3_bind_int64 (stmt->stmt, index + 1, value);
}

void
tracker_db_statement_bind_null (TrackerDBStatement *stmt,
                                int                 index)
{
	g_return_if_fail (TRACKER_IS_DB_STATEMENT (stmt));

	g_assert (!stmt->stmt_is_sunk);

	sqlite3_bind_null (stmt->stmt, index + 1);
}

void
tracker_db_statement_bind_text (TrackerDBStatement *stmt,
                                int                 index,
                                const gchar        *value)
{
	g_return_if_fail (TRACKER_IS_DB_STATEMENT (stmt));

	g_assert (!stmt->stmt_is_sunk);

	sqlite3_bind_text (stmt->stmt, index + 1, value, -1, SQLITE_TRANSIENT);
}

void
tracker_db_cursor_rewind (TrackerDBCursor *cursor)
{
	g_return_if_fail (TRACKER_IS_DB_CURSOR (cursor));

	if (cursor->threadsafe) {
		tracker_db_manager_lock ();
	}

	sqlite3_reset (cursor->stmt);
	cursor->finished = FALSE;

	if (cursor->threadsafe) {
		tracker_db_manager_unlock ();
	}
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

		if (cursor->threadsafe) {
			tracker_db_manager_lock ();
		}

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

		if (cursor->threadsafe) {
			tracker_db_manager_unlock ();
		}
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
	gint64 result;

	if (cursor->threadsafe) {
		tracker_db_manager_lock ();
	}

	result = (gint64) sqlite3_column_int64 (cursor->stmt, column);

	if (cursor->threadsafe) {
		tracker_db_manager_unlock ();
	}

	return result;
}

gdouble
tracker_db_cursor_get_double (TrackerDBCursor *cursor,
                              guint            column)
{
	gdouble result;

	if (cursor->threadsafe) {
		tracker_db_manager_lock ();
	}

	result = (gdouble) sqlite3_column_double (cursor->stmt, column);

	if (cursor->threadsafe) {
		tracker_db_manager_unlock ();
	}

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
	gint column_type;
	gint n_columns = sqlite3_column_count (cursor->stmt);

	g_return_val_if_fail (column < n_columns, TRACKER_SPARQL_VALUE_TYPE_UNBOUND);

	if (cursor->threadsafe) {
		tracker_db_manager_lock ();
	}

	column_type = sqlite3_column_type (cursor->stmt, column);

	if (cursor->threadsafe) {
		tracker_db_manager_unlock ();
	}

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
	const gchar *result;

	if (cursor->threadsafe) {
		tracker_db_manager_lock ();
	}

	if (column < cursor->n_variable_names) {
		result = cursor->variable_names[column];
	} else {
		result = sqlite3_column_name (cursor->stmt, column);
	}

	if (cursor->threadsafe) {
		tracker_db_manager_unlock ();
	}

	return result;
}

const gchar*
tracker_db_cursor_get_string (TrackerDBCursor *cursor,
                              guint            column,
                              glong           *length)
{
	const gchar *result;

	if (cursor->threadsafe) {
		tracker_db_manager_lock ();
	}

	if (length) {
		sqlite3_value *val = sqlite3_column_value (cursor->stmt, column);

		*length = sqlite3_value_bytes (val);
		result = (const gchar *) sqlite3_value_text (val);
	} else {
		result = (const gchar *) sqlite3_column_text (cursor->stmt, column);
	}

	if (cursor->threadsafe) {
		tracker_db_manager_unlock ();
	}

	return result;
}

void
tracker_db_statement_execute (TrackerDBStatement  *stmt,
                              GError             **error)
{
	g_return_if_fail (TRACKER_IS_DB_STATEMENT (stmt));
	g_return_if_fail (!stmt->stmt_is_sunk);

	execute_stmt (stmt->db_interface, stmt->stmt, NULL, error);
}

TrackerDBCursor *
tracker_db_statement_start_cursor (TrackerDBStatement  *stmt,
                                   GError             **error)
{
	g_return_val_if_fail (TRACKER_IS_DB_STATEMENT (stmt), NULL);
	g_return_val_if_fail (!stmt->stmt_is_sunk, NULL);

	return tracker_db_cursor_sqlite_new (stmt->stmt, stmt, NULL, 0, NULL, 0, FALSE);
}

TrackerDBCursor *
tracker_db_statement_start_sparql_cursor (TrackerDBStatement   *stmt,
                                          TrackerPropertyType  *types,
                                          gint                  n_types,
                                          const gchar         **variable_names,
                                          gint                  n_variable_names,
                                          gboolean              threadsafe,
                                          GError              **error)
{
	g_return_val_if_fail (TRACKER_IS_DB_STATEMENT (stmt), NULL);
	g_return_val_if_fail (!stmt->stmt_is_sunk, NULL);

	return tracker_db_cursor_sqlite_new (stmt->stmt, stmt, types, n_types, variable_names, n_variable_names, threadsafe);
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
	g_assert (!stmt->stmt_is_sunk);

	sqlite3_reset (stmt->stmt);
	sqlite3_clear_bindings (stmt->stmt);
}
