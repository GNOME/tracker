/*
 * Copyright (C) 2006, Jamie McCracken <jamiemcc@gnome.org>
 * Copyright (C) 2008-2010, Nokia <ivan.frade@nokia.com>
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

/* For timegm usage on __GLIBC__ we need _GNU_SOURCE, should be
 * defined in config.h based on configure checks...
 */

#include <strings.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include <glib.h>

#include "tracker-date-time.h"
#include "tracker-type-utils.h"

GQuark tracker_date_error_quark (void) {
	return g_quark_from_static_string ("tracker_date_error-quark");
}

gdouble
tracker_string_to_date (const gchar *date_string,
                        gint        *offset_p,
                        GError      **error)
{
	/* TODO Add more checks.
	 */

	static GRegex *regex = NULL;

	GMatchInfo *match_info;
	gchar      *match;
	struct tm tm;
	gdouble t;
	gint offset;
	gboolean timezoned;

	if (!date_string) {
		g_set_error (error, TRACKER_DATE_ERROR, TRACKER_DATE_ERROR_EMPTY,
		             "Empty date string");
		return -1;
	}

	/* We should have a valid iso 8601 date in format
	 * YYYY-MM-DDThh:mm:ss with optional TZ
	 */

	if (!regex) {
		GError *e = NULL;
		regex = g_regex_new ("^(-?[0-9][0-9][0-9][0-9])-([0-9][0-9])-([0-9][0-9])T([0-9][0-9]):([0-9][0-9]):([0-9][0-9])(\\.[0-9]+)?(Z|(\\+|-)([0-9][0-9]):?([0-9][0-9]))?$", 0, 0, &e);
		if (e) {
			g_error ("%s", e->message);
		}
	}

	if (!g_regex_match (regex, date_string, 0, &match_info)) {
		g_match_info_free (match_info);
		g_set_error (error, TRACKER_DATE_ERROR, TRACKER_DATE_ERROR_INVALID_ISO8601,
		             "Not a ISO 8601 date string. Allowed form is [-]CCYY-MM-DDThh:mm:ss[Z|(+|-)hh:mm]");
		return -1;
	}

	memset (&tm, 0, sizeof (struct tm));

	/* year */
	match = g_match_info_fetch (match_info, 1);
	tm.tm_year = atoi (match) - 1900;
	g_free (match);

	/* month */
	match = g_match_info_fetch (match_info, 2);
	tm.tm_mon = atoi (match) - 1;
	g_free (match);

	/* day of month */
	match = g_match_info_fetch (match_info, 3);
	tm.tm_mday = atoi (match);
	g_free (match);

	/* hour */
	match = g_match_info_fetch (match_info, 4);
	tm.tm_hour = atoi (match);
	g_free (match);

	/* minute */
	match = g_match_info_fetch (match_info, 5);
	tm.tm_min = atoi (match);
	g_free (match);

	/* second */
	match = g_match_info_fetch (match_info, 6);
	tm.tm_sec = atoi (match);
	g_free (match);

	match = g_match_info_fetch (match_info, 8);
	timezoned = (match && strlen (match) > 0);
	g_free (match);

	if (timezoned) {
		/* timezoned */

		/* mktime() always assumes that "tm" is in locale time but we
		 * want to keep control on time, so we go to UTC
		 */
#if !(defined(__FreeBSD__) || defined(__OpenBSD__) || defined (__GLIBC__))
		t  = mktime (&tm);
		t -= timezone;
#else
		t = timegm (&tm);
#endif

		offset = 0;

		match = g_match_info_fetch (match_info, 9);
		if (match && strlen (match) > 0) {
			/* non-UTC timezone */

			gboolean positive_offset;

			positive_offset = (match[0] == '+');
			g_free (match);

			match = g_match_info_fetch (match_info, 10);
			offset = atoi (match) * 3600;
			g_free (match);

			match = g_match_info_fetch (match_info, 11);
			offset += atoi (match) * 60;
			g_free (match);

			if (!positive_offset) {
				offset = -offset;
			}

			if (offset < -14 * 3600 || offset > 14 * 3600) {
				g_set_error (error, TRACKER_DATE_ERROR, TRACKER_DATE_ERROR_OFFSET,
				             "UTC offset too large: %d seconds", offset);
				g_match_info_free (match_info);
				return -1;
			}

			t -= offset;
		}
	} else {
		time_t t2;

		/* local time */
		tm.tm_isdst = -1;

		t = mktime (&tm);

		/* calculate UTC offset, requires timegm for correct result
		   with past times when timezone had different UTC offset */
#if !(defined(__FreeBSD__) || defined(__OpenBSD__) || defined (__GLIBC__))
		offset = -timezone + (tm.tm_isdst > 0 ? 3600 : 0);
#else
		t2 = timegm (&tm);
		offset = t2 - (time_t) t;
#endif
	}

	match = g_match_info_fetch (match_info, 7);
	if (match && strlen (match) > 0) {
		char milliseconds[4] = "000\0";
		/* first character of match is decimal point
		   we're interested in a maximum of 3 decimal places (milliseconds) */
		memcpy (milliseconds, match + 1, MIN (3, strlen (match + 1)));
		t += (gdouble) atoi (milliseconds) / 1000;
	}
	g_free (match);

	g_match_info_free (match_info);

	if (offset_p) {
		*offset_p = offset;
	}

	return t;
}

gchar *
tracker_date_to_string (gdouble date_time,
                        gint    offset)
{
	gchar buffer[35];
	time_t seconds;
	gint64 total_milliseconds;
	gint milliseconds;
	struct tm utc_time;
	size_t count, size;

	memset (buffer, '\0', sizeof (buffer));
	memset (&utc_time, 0, sizeof (struct tm));

	total_milliseconds = (gint64) round (date_time * 1000);
	milliseconds = total_milliseconds % 1000;
	if (milliseconds < 0) {
		milliseconds += 1000;
	}
	seconds = (time_t) ((total_milliseconds - milliseconds) / 1000) + offset;
	gmtime_r (&seconds, &utc_time);

	/* Output is ISO 8601 format : "YYYY-MM-DDThh:mm:ss" */
	count = strftime (buffer, sizeof (buffer), "%FT%T", &utc_time);

	/* Append milliseconds (if non-zero) and time zone */
	if (milliseconds > 0) {
		size = snprintf (buffer + count, sizeof (buffer) - count, ".%03d", milliseconds);
		count += size;
	}

	if (offset != 0) {
		gint hours, mins;

		hours = ABS (offset) / 3600;
		mins = (ABS (offset) % 3600) / 60;
		size = snprintf (buffer + count, sizeof (buffer) - count, "%c%.2d:%.2d",
		                 offset < 0 ? '-' : '+',
		                 hours, mins);
		count += size;
	} else {
		buffer[count++] = 'Z';
	}

	g_assert (count <= sizeof (buffer));

	return count > 0 ? g_strdup (buffer) : NULL;
}

static void
date_time_value_init (GValue *value)
{
	value->data[0].v_double = 0;
	value->data[1].v_int = 0;
}

static void
date_time_value_copy (const GValue *src_value,
                      GValue       *dest_value)
{
	dest_value->data[0].v_double = src_value->data[0].v_double;
	dest_value->data[1].v_int = src_value->data[1].v_int;
}

GType
tracker_date_time_get_type (void)
{
	static GType tracker_date_time_type_id = 0;
	if (G_UNLIKELY (tracker_date_time_type_id == 0)) {
		static const GTypeValueTable value_table = {
			date_time_value_init,
			NULL,
			date_time_value_copy
		};
		static const GTypeInfo type_info = {
			0,
			NULL,
			NULL,
			NULL,
			NULL,
			NULL,
			0,
			0,
			NULL,
			&value_table
		};
		static const GTypeFundamentalInfo fundamental_info = {
			0
		};
		tracker_date_time_type_id = g_type_register_fundamental (
			g_type_fundamental_next (),
			"TrackerDateTime",
			&type_info,
			&fundamental_info,
			0);
	}
	return tracker_date_time_type_id;
}

void
tracker_date_time_set (GValue  *value,
                       gdouble  time,
                       gint     offset)
{
	g_return_if_fail (G_VALUE_HOLDS (value, TRACKER_TYPE_DATE_TIME));
	g_return_if_fail (offset >= -14 * 3600 && offset <= 14 * 3600);

	value->data[0].v_double = time;
	value->data[1].v_int = offset;
}

void
tracker_date_time_set_from_string (GValue      *value,
                                   const gchar *date_time_string,
                                   GError     **error)
{
	gdouble time;
	gint offset;
	GError *new_error = NULL;

	g_return_if_fail (G_VALUE_HOLDS (value, TRACKER_TYPE_DATE_TIME));
	g_return_if_fail (date_time_string != NULL);

	time = tracker_string_to_date (date_time_string, &offset, &new_error);

	if (new_error != NULL) {
		g_propagate_error (error, new_error);
		return;
	}

	tracker_date_time_set (value, time, offset);
}

gdouble
tracker_date_time_get_time (const GValue *value)
{
	g_return_val_if_fail (G_VALUE_HOLDS (value, TRACKER_TYPE_DATE_TIME), 0);

	/* UTC timestamp */
	return value->data[0].v_double;
}

gint
tracker_date_time_get_offset (const GValue *value)
{
	g_return_val_if_fail (G_VALUE_HOLDS (value, TRACKER_TYPE_DATE_TIME), 0);

	/* UTC offset */
	return value->data[1].v_int;
}

gint
tracker_date_time_get_local_date (const GValue *value)
{
	gdouble local_timestamp;

	g_return_val_if_fail (G_VALUE_HOLDS (value, TRACKER_TYPE_DATE_TIME), 0);

	/* return number of days since epoch */
	local_timestamp = tracker_date_time_get_time (value) + tracker_date_time_get_offset (value);
	return (gint) (local_timestamp / 3600 / 24);
}

gint
tracker_date_time_get_local_time (const GValue *value)
{
	gdouble local_timestamp;

	g_return_val_if_fail (G_VALUE_HOLDS (value, TRACKER_TYPE_DATE_TIME), 0);

	/* return local time of day */
	local_timestamp = tracker_date_time_get_time (value) + tracker_date_time_get_offset (value);
	return (int) local_timestamp % (24 * 3600);
}
