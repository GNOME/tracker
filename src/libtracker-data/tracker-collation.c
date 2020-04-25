/*
 * Copyright (C) 2010, Nokia <ivan.frade@nokia.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 */

#include "config.h"
#include <glib.h>
#include <glib/gi18n.h>
#include <string.h>
#include <locale.h>

#include <libtracker-common/tracker-debug.h>
#include <libtracker-common/tracker-locale.h>
#include "tracker-collation.h"

/* If defined, will dump additional traces */
#ifdef G_ENABLE_DEBUG
#define trace(message, ...) TRACKER_NOTE (COLLATION, g_message (message, ##__VA_ARGS__))
#else
#define trace(...)
#endif

#ifdef HAVE_LIBUNISTRING
/* libunistring versions prior to 9.1.2 need this hack */
#define _UNUSED_PARAMETER_
#include <unistr.h>
#elif defined(HAVE_LIBICU)
#include <unicode/ucol.h>
#include <unicode/utypes.h>
#endif

/* If string lenth less than this value, allocating from the stack */
#define MAX_STACK_STR_SIZE 8192

#ifdef HAVE_LIBUNISTRING /* ---- GNU libunistring based collation ---- */

gpointer
tracker_collation_init (void)
{
	gchar *locale;

	/* Get locale! */
	locale = tracker_locale_get (TRACKER_LOCALE_COLLATE);
	TRACKER_NOTE (COLLATION, g_message ("[libunistring collation] Initializing collator for locale '%s'", locale));
	g_free (locale);
	/* Nothing to do */
	return NULL;
}

void
tracker_collation_shutdown (gpointer collator)
{
	/* Nothing to do */
}

gint
tracker_collation_utf8 (gpointer      collator,
                        gint          len1,
                        gconstpointer str1,
                        gint          len2,
                        gconstpointer str2)
{
	gint result;
	gchar *aux1;
	gchar *aux2;

	/* Note: str1 and str2 are NOT NUL-terminated */
	aux1 = (len1 < MAX_STACK_STR_SIZE) ? g_alloca (len1+1) : g_malloc (len1+1);
	aux2 = (len2 < MAX_STACK_STR_SIZE) ? g_alloca (len2+1) : g_malloc (len2+1);

	memcpy (aux1, str1, len1); aux1[len1] = '\0';
	memcpy (aux2, str2, len2); aux2[len2] = '\0';

	result = u8_strcoll (aux1, aux2);

	trace ("(libunistring) Collating '%s' and '%s' (%d)",
	       aux1, aux2, result);

	if (len1 >= MAX_STACK_STR_SIZE)
		g_free (aux1);
	if (len2 >= MAX_STACK_STR_SIZE)
		g_free (aux2);
	return result;
}

#elif defined(HAVE_LIBICU) /* ---- ICU based collation (UTF-16) ----*/

gpointer
tracker_collation_init (void)
{
	UCollator *collator = NULL;
	UErrorCode status = U_ZERO_ERROR;
	gchar *locale;

	/* Get locale! */
	locale = tracker_locale_get (TRACKER_LOCALE_COLLATE);

	TRACKER_NOTE (COLLATION, g_message ("[ICU collation] Initializing collator for locale '%s'", locale));
	collator = ucol_open (locale, &status);
	if (!collator) {
		g_warning ("[ICU collation] Collator for locale '%s' cannot be created: %s",
		           locale, u_errorName (status));
		/* Try to get UCA collator then... */
		status = U_ZERO_ERROR;
		collator = ucol_open ("root", &status);
		if (!collator) {
			g_critical ("[ICU collation] UCA Collator cannot be created: %s",
			            u_errorName (status));
		}
	}
	g_free (locale);
	return collator;
}

void
tracker_collation_shutdown (gpointer collator)
{
	if (collator)
		ucol_close ((UCollator *)collator);
}

gint
tracker_collation_utf8 (gpointer      collator,
                        gint          len1,
                        gconstpointer str1,
                        gint          len2,
                        gconstpointer str2)
{
	UErrorCode status = U_ZERO_ERROR;
	UCharIterator iter1;
	UCharIterator iter2;
	UCollationResult result;

	/* Collator must be created before trying to collate */
	g_return_val_if_fail (collator, -1);

	/* Setup iterators */
	uiter_setUTF8 (&iter1, str1, len1);
	uiter_setUTF8 (&iter2, str2, len2);

	result = ucol_strcollIter ((UCollator *)collator,
	                           &iter1,
	                           &iter2,
	                           &status);
	if (status != U_ZERO_ERROR)
		g_critical ("Error collating: %s", u_errorName (status));

#ifdef ENABLE_TRACE
	{
		gchar *aux1;
		gchar *aux2;

		/* Note: str1 and str2 are NOT NUL-terminated */
		aux1 = (len1 < MAX_STACK_STR_SIZE) ? g_alloca (len1+1) : g_malloc (len1+1);
		aux2 = (len2 < MAX_STACK_STR_SIZE) ? g_alloca (len2+1) : g_malloc (len2+1);

		memcpy (aux1, str1, len1); aux1[len1] = '\0';
		memcpy (aux2, str2, len2); aux2[len2] = '\0';

		trace ("(ICU) Collating '%s' and '%s' (%d)",
		       aux1, aux2, result);

		if (len1 >= MAX_STACK_STR_SIZE)
			g_free (aux1);
		if (len2 >= MAX_STACK_STR_SIZE)
			g_free (aux2);
	}
#endif /* ENABLE_TRACE */

	if (result == UCOL_GREATER)
		return 1;
	if (result == UCOL_LESS)
		return -1;
	return 0;
}

#else /* ---- GLib based collation ---- */

gpointer
tracker_collation_init (void)
{
	gchar *locale;

	/* Get locale! */
	locale = tracker_locale_get (TRACKER_LOCALE_COLLATE);
	TRACKER_NOTE (COLLATION, g_message ("[GLib collation] Initializing collator for locale '%s'", locale));
	g_free (locale);
	/* Nothing to do */
	return NULL;
}

void
tracker_collation_shutdown (gpointer collator)
{
	/* Nothing to do */
}

gint
tracker_collation_utf8 (gpointer      collator,
                        gint          len1,
                        gconstpointer str1,
                        gint          len2,
                        gconstpointer str2)
{
	gint result;
	gchar *aux1;
	gchar *aux2;

	/* Note: str1 and str2 are NOT NUL-terminated */
	aux1 = (len1 < MAX_STACK_STR_SIZE) ? g_alloca (len1+1) : g_malloc (len1+1);
	aux2 = (len2 < MAX_STACK_STR_SIZE) ? g_alloca (len2+1) : g_malloc (len2+1);

	memcpy (aux1, str1, len1); aux1[len1] = '\0';
	memcpy (aux2, str2, len2); aux2[len2] = '\0';

	result = g_utf8_collate (aux1, aux2);

	trace ("(GLib) Collating '%s' and '%s' (%d)",
	       aux1, aux2, result);

	if (len1 >= MAX_STACK_STR_SIZE)
		g_free (aux1);
	if (len2 >= MAX_STACK_STR_SIZE)
		g_free (aux2);
	return result;
}

#endif

static gboolean
skip_non_alphanumeric (const gchar **str,
                       gint         *len)
{
	const gchar *remaining = *str, *end = &remaining[*len];
	gboolean found = FALSE, is_alnum;
	gunichar unichar;

	while (remaining < end) {
		unichar = g_utf8_get_char (remaining);
		is_alnum = g_unichar_isalnum (unichar);
		if (is_alnum)
			break;

		found = TRUE;
		remaining = g_utf8_next_char (remaining);
	}

	/* The string must not be left empty */
	if (remaining == end)
		return FALSE;

	if (found) {
		*len = end - remaining;
		*str = remaining;
	}

	return found;
}

static gboolean
check_remove_prefix (const gchar  *str,
                     gint          len,
                     const gchar  *prefix,
                     gint          prefix_len,
                     const gchar **str_out,
                     gint         *len_out)
{
	const gchar *remaining;
	gchar *strstart;
	gint remaining_len;

	if (len <= prefix_len)
		return FALSE;

	/* Check that the prefix matches */
	strstart = g_utf8_casefold (str, prefix_len);
	if (strcmp (strstart, prefix) != 0) {
		g_free (strstart);
		return FALSE;
	}

	/* Check that the following letter is a break
	 * character.
	 */
	g_free (strstart);
	remaining = &str[prefix_len];
	remaining_len = len - prefix_len;

	if (!skip_non_alphanumeric (&remaining, &remaining_len))
		return FALSE;

	*len_out = remaining_len;
	*str_out = remaining;
	return TRUE;
}

/* Helper function valid for all implementations */
gint
tracker_collation_utf8_title (gpointer      collator,
                              gint          len1,
                              gconstpointer str1,
                              gint          len2,
                              gconstpointer str2)
{
	const gchar *title_beginnings_str;
	static gchar **title_beginnings = NULL;
	const gchar *res1 = NULL, *res2 = NULL;
	gint i;

	skip_non_alphanumeric ((const gchar **) &str1, &len1);
	skip_non_alphanumeric ((const gchar **) &str2, &len2);

	/* Translators: this is a '|' (U+007C) separated list of common
	 * title beginnings. Meant to be skipped for sorting purposes,
	 * case doesn't matter. Given English media is quite common, it is
	 * advised to leave the untranslated articles in addition to
	 * the translated ones.
	 */
	title_beginnings_str = N_("the|a|an");

	if (!title_beginnings)
		title_beginnings = g_strsplit (_(title_beginnings_str), "|", -1);

	for (i = 0; title_beginnings[i]; i++) {
		gchar *prefix;
		gint prefix_len;

		prefix = g_utf8_casefold (title_beginnings[i], -1);
		prefix_len = strlen (prefix);

		if (!res1)
			check_remove_prefix (str1, len1, prefix, prefix_len,
			                     &res1, &len1);
		if (!res2)
			check_remove_prefix (str2, len2, prefix, prefix_len,
			                     &res2, &len2);
		g_free (prefix);
	}

	if (!res1)
		res1 = str1;
	if (!res2)
		res2 = str2;

	return tracker_collation_utf8 (collator, len1, res1, len2, res2);
}
