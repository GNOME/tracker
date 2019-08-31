/*
 * Copyright (C) 2010, Nokia <ivan.frade@nokia.com>
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

namespace Tracker.Sparql {
	/**
	 * SECTION: tracker-misc
	 * @short_description: General purpose utilities provided by the library
	 * @title: Utilities
	 * @stability: Stable
	 * @include: tracker-sparql.h
	 *
	 * <para>
	 * The libtracker-sparql utilities help in the creation of proper SPARQL queries.
	 * </para>
	 */

	// Imported from tracker-uri.c
	public extern string escape_uri_vprintf (string format, va_list args);
	public extern string escape_uri_printf (string format, ...);
	public extern string escape_uri (string uri);

	/**
	 * tracker_sparql_escape_string:
	 * @literal: a string to escape
	 *
	 * Escapes a string so that it can be used in a SPARQL query.
	 *
	 * Returns: a newly-allocated string with the escaped version of @literal.
	 * The returned string should be freed with g_free() when no longer needed.
	 *
	 * Since: 0.10
	 */
	public string escape_string (string literal) {
		StringBuilder str = new StringBuilder ();

		/* Shouldn't cast from const to non-const here, but we know
		 * the compiler is going to complain and it's just because
		 * Vala string manipulation doesn't allow us to do this more
		 * easily. */
		char *p = (char*) literal;

		while (*p != '\0') {
			size_t len = Posix.strcspn ((string) p, "\t\n\r\b\f\'\"\\");
			str.append_len ((string) p, (long) len);
			p += len;

			switch (*p) {
			case '\t':
				str.append ("\\t");
				break;
			case '\n':
				str.append ("\\n");
				break;
			case '\r':
				str.append ("\\r");
				break;
			case '\b':
				str.append ("\\b");
				break;
			case '\f':
				str.append ("\\f");
				break;
			case '\'':
				str.append ("\\'");
				break;
			case '"':
				str.append ("\\\"");
				break;
			case '\\':
				str.append ("\\\\");
				break;
			default:
				continue;
			}

			p++;
		}

		return str.str;
	}

	[CCode (cname = "uuid_generate")]
	private extern static void uuid_generate ([CCode (array_length = false)] uchar[] uuid);

	[CCode (cname = "uuid_unparse_lower")]
	private extern static void uuid_unparse_lower ([CCode (array_length = false)] uchar[] uuid, char* out);

	/**
	 * tracker_sparql_get_uuid_urn:
	 *
	 * Generates a unique universal identifier to be used for urns
	 * when inserting SPARQL into the database. The string returned is
	 * in lower case and has the format "urn:uuid:&percnt;s" where
	 * &percnt;s is the uuid generated.
	 *
	 * Returns: a newly-allocated string. The returned string should
	 * be freed with g_free() when no longer needed.
	 *
	 * Since: 0.10
	 */
	public string get_uuid_urn () {
		uchar[] base_uuid = new uchar[16];
		string lower = (string) new char[37];

		uuid_generate (base_uuid);
		uuid_unparse_lower (base_uuid, (char *) lower);

		// generate uuid
		return "urn:uuid:%s".printf (lower);
	}
}
