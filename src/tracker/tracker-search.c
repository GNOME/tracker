/*
 * Copyright (C) 2009, Nokia <ivan.frade@nokia.com>
 * Copyright (C) 2014, SoftAtHome <contact@softathome.com>
 * Copyright (C) 2014, Lanedo <martyn@lanedo.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 */

#include "config.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <locale.h>

#include <glib.h>
#include <glib/gi18n.h>

#include <libtracker-common/tracker-common.h>
#include <libtracker-sparql/tracker-sparql.h>

#include "tracker-color.h"
#include "tracker-search.h"

static gint limit = -1;
static gint offset;
static gchar **terms;
static gboolean or_operator;
static gboolean detailed;
static gboolean all;
static gboolean disable_snippets;
static gboolean disable_fts;
static gboolean disable_color;
static gboolean files;
static gboolean folders;
static gboolean music_albums;
static gboolean music_artists;
static gboolean music_files;
static gboolean image_files;
static gboolean video_files;
static gboolean document_files;
static gboolean emails;
static gboolean contacts;
static gboolean feeds;
static gboolean software;
static gboolean software_categories;
static gboolean bookmarks;
static gchar *search_directory;

#define SEARCH_OPTIONS_ENABLED() \
	(music_albums || music_artists || music_files || \
	 bookmarks || \
	 feeds || \
	 software || \
	 software_categories || \
	 image_files || \
	 video_files || \
	 document_files || \
	 emails || \
	 contacts || \
	 files || \
	 folders || \
	 (terms && g_strv_length (terms) > 0))

static GOptionEntry entries[] = {
	/* Search types */
	{ "files", 'f', 0, G_OPTION_ARG_NONE, &files,
	  N_("Search for files"),
	  NULL
	},
	{ "folders", 's', 0, G_OPTION_ARG_NONE, &folders,
	  N_("Search for folders"),
	  NULL
	},
	{ "music", 'm', 0, G_OPTION_ARG_NONE, &music_files,
	  N_("Search for music files"),
	  NULL
	},
	{ "music-albums", 0, 0, G_OPTION_ARG_NONE, &music_albums,
	  N_("Search for music albums (--all has no effect on this)"),
	  NULL
	},
	{ "music-artists", 0, 0, G_OPTION_ARG_NONE, &music_artists,
	  N_("Search for music artists (--all has no effect on this)"),
	  NULL
	},
	{ "images", 'i', 0, G_OPTION_ARG_NONE, &image_files,
	  N_("Search for image files"),
	  NULL
	},
	{ "videos", 'v', 0, G_OPTION_ARG_NONE, &video_files,
	  N_("Search for video files"),
	  NULL
	},
	{ "documents", 't', 0, G_OPTION_ARG_NONE, &document_files,
	  N_("Search for document files"),
	  NULL
	},
	{ "emails", 'e', 0, G_OPTION_ARG_NONE, &emails,
	  N_("Search for emails"),
	  NULL
	},
	{ "contacts", 'c', 0, G_OPTION_ARG_NONE, &contacts,
	  N_("Search for contacts"),
	  NULL
	},
	{ "software", 0, 0, G_OPTION_ARG_NONE, &software,
	  N_("Search for software (--all has no effect on this)"),
	  NULL
	},
	{ "software-categories", 0, 0, G_OPTION_ARG_NONE, &software_categories,
	  N_("Search for software categories (--all has no effect on this)"),
	  NULL
	},
	{ "feeds", 0, 0, G_OPTION_ARG_NONE, &feeds,
	  N_("Search for feeds (--all has no effect on this)"),
	  NULL
	},
	{ "bookmarks", 'b', 0, G_OPTION_ARG_NONE, &bookmarks,
	  N_("Search for bookmarks (--all has no effect on this)"),
	  NULL
	},

	/* Semantic options */
	{ "limit", 'l', 0, G_OPTION_ARG_INT, &limit,
	  N_("Limit the number of results shown"),
	  "512"
	},
	{ "offset", 'o', 0, G_OPTION_ARG_INT, &offset,
	  N_("Offset the results"),
	  "0"
	},
	{ "or-operator", 'r', 0, G_OPTION_ARG_NONE, &or_operator,
	  N_("Use OR for search terms instead of AND (the default)"),
	  NULL
	},
	{ "detailed", 'd', 0, G_OPTION_ARG_NONE, &detailed,
	  N_("Show URNs for results (doesn’t apply to --music-albums, --music-artists, --feeds, --software, --software-categories)"),
	  NULL
	},
	{ "all", 'a', 0, G_OPTION_ARG_NONE, &all,
	  N_("Return all non-existing matches too (i.e. include unmounted volumes)"),
	  NULL
	},
	{ "disable-snippets", 0, 0, G_OPTION_ARG_NONE, &disable_snippets,
	  N_("Disable showing snippets with results. This is only shown for some categories, e.g. Documents, Music…"),
	  NULL,
	},
	{ "disable-fts", 0, 0, G_OPTION_ARG_NONE, &disable_fts,
	  N_("Disable Full Text Search (FTS). Implies --disable-snippets"),
	  NULL,
	},
	{ "disable-color", 0, 0, G_OPTION_ARG_NONE, &disable_color,
	  N_("Disable color when printing snippets and results"),
	  NULL,
	},
	{ "path", 'p', 0, G_OPTION_ARG_STRING, &search_directory,
	  N_("Limit search to certain directory"),
	  NULL,
	},

	/* Main arguments, the search terms */
	{ G_OPTION_REMAINING, 0, 0,
	  G_OPTION_ARG_STRING_ARRAY, &terms,
	  N_("search terms"),
	  N_("EXPRESSION")
	},
	{ NULL }
};

static void
show_limit_warning (void)
{
	/* Display '...' so the user thinks there is
	 * more items.
	 */
	g_print ("  ...\n");

	/* Display warning so the user knows this is
	 * not the WHOLE data set.
	 */
	g_printerr ("\n%s%s%s\n",
	            disable_color ? "" : WARN_BEGIN,
	            _("NOTE: Limit was reached, there are more items in the database not listed here"),
	            disable_color ? "" : WARN_END);
}

static gchar *
get_fts_string (GStrv    search_words,
                gboolean use_or_operator)
{
#if HAVE_TRACKER_FTS
	GString *fts;
	gint i, len;

	if (disable_fts) {
		return NULL;
	}

	if (!search_words) {
		return NULL;
	}

	fts = g_string_new ("");
	len = g_strv_length (search_words);

	for (i = 0; i < len; i++) {
		gchar *escaped;

		/* Properly escape the input string as it's going to be passed
		 * in a sparql query */
		escaped = tracker_sparql_escape_string (search_words[i]);

		g_string_append (fts, escaped);

		if (i < len - 1) {
			if (use_or_operator) {
				g_string_append (fts, " OR ");
			} else {
				g_string_append (fts, " ");
			}
		}

		g_free (escaped);
	}

	return g_string_free (fts, FALSE);
#else
	/* If FTS support not enabled, always do non-fts searches */
	return NULL;
#endif
}

static inline void
print_snippet (const gchar *snippet)
{
	if (disable_snippets) {
		return;
	}

	if (!snippet || *snippet == '\0') {
		return;
	} else {
		gchar *compressed;
		gchar *p;

		compressed = g_strdup (snippet);

		for (p = compressed;
		     p && *p != '\0';
		     p = g_utf8_next_char (p)) {
			if (*p == '\r' || *p == '\n') {
				/* inline \n and \r */
				*p = ' ';
			}
		}

		g_print ("  %s\n", compressed);
		g_free (compressed);
	}

	g_print ("\n");
}

static gboolean
get_contacts_results (TrackerSparqlConnection *connection,
                      const gchar             *query,
                      gint                     search_limit,
                      gboolean                 details)
{
	GError *error = NULL;
	TrackerSparqlCursor *cursor;

	cursor = tracker_sparql_connection_query (connection, query, NULL, &error);

	if (error) {
		g_printerr ("%s, %s\n",
		            _("Could not get search results"),
		            error->message);
		g_error_free (error);

		return FALSE;
	}

	if (!cursor) {
		g_print ("%s\n",
		         _("No contacts were found"));
	} else {
		gint count = 0;

		g_print ("%s:\n", _("Contacts"));

		while (tracker_sparql_cursor_next (cursor, NULL, NULL)) {
			if (details) {
				g_print ("  '%s%s%s', %s (%s)\n",
		                         disable_color ? "" : TITLE_BEGIN,
				         tracker_sparql_cursor_get_string (cursor, 0, NULL),
		                         disable_color ? "" : TITLE_END,
				         tracker_sparql_cursor_get_string (cursor, 1, NULL),
				         tracker_sparql_cursor_get_string (cursor, 2, NULL));
			} else {
				g_print ("  '%s%s%s', %s\n",
		                         disable_color ? "" : TITLE_BEGIN,
				         tracker_sparql_cursor_get_string (cursor, 0, NULL),
		                         disable_color ? "" : TITLE_END,
				         tracker_sparql_cursor_get_string (cursor, 1, NULL));
			}

			count++;
		}

		g_print ("\n");

		if (count >= search_limit) {
			show_limit_warning ();
		}

		g_object_unref (cursor);
	}

	return TRUE;
}

static gboolean
get_contacts (TrackerSparqlConnection *connection,
              GStrv                    search_terms,
              gboolean                 show_all,
              gint                     search_offset,
              gint                     search_limit,
              gboolean                 use_or_operator,
              gboolean                 details)
{
	gchar *fts;
	gchar *query;
	gboolean success;

	fts = get_fts_string (search_terms, use_or_operator);

	if (fts) {
		query = g_strdup_printf ("SELECT tracker:coalesce(nco:fullname(?contact), fn:concat(nco:nameFamily(?contact), \" \", nco:nameGiven(?contact)),\"%s\") tracker:coalesce(nco:hasEmailAddress(?contact), \"%s\") ?contact "
		                         "WHERE { "
		                         "  ?contact a nco:Contact ;"
		                         "  fts:match \"%s\" ."
		                         "} "
		                         "ORDER BY ASC(nco:fullname(?contact)) ASC(nco:hasEmailAddress(?contact)) "
		                         "OFFSET %d "
		                         "LIMIT %d",
                                         _("No name"),
                                         _("No E-mail address"),
		                         fts,
		                         search_offset,
		                         search_limit);
	} else {
		query = g_strdup_printf ("SELECT tracker:coalesce(nco:fullname(?contact), fn:concat(nco:nameFamily(?contact), \" \", nco:nameGiven(?contact)), \"%s\") tracker:coalesce(nco:hasEmailAddress(?contact), \"%s\") ?contact "
		                         "WHERE { "
		                         "  ?contact a nco:Contact ."
		                         "} "
		                         "ORDER BY ASC(nco:fullname(?contact)) ASC(nco:hasEmailAddress(?contact)) "
		                         "OFFSET %d "
		                         "LIMIT %d",
                                         _("No name"),
                                         _("No E-mail address"),
		                         search_offset,
		                         search_limit);
	}

	success = get_contacts_results (connection, query, search_limit, details);
	g_free (query);
	g_free (fts);

	return success;
}

static gboolean
get_emails_results (TrackerSparqlConnection *connection,
                    const gchar             *query,
                    gint                     search_limit,
                    gboolean                 details)
{
	GError *error = NULL;
	TrackerSparqlCursor *cursor;

	cursor = tracker_sparql_connection_query (connection, query, NULL, &error);

	if (error) {
		g_printerr ("%s, %s\n",
		            _("Could not get search results"),
		            error->message);
		g_error_free (error);

		return FALSE;
	}

	if (!cursor) {
		g_print ("%s\n",
		         _("No emails were found"));
	} else {
		gint count = 0;

		g_print ("%s:\n", _("Emails"));

		while (tracker_sparql_cursor_next (cursor, NULL, NULL)) {
			g_print ("  %s%s%s\n"
			         "  %s, %s\n",
			         disable_color ? "" : TITLE_BEGIN,
			         tracker_sparql_cursor_get_string (cursor, 0, NULL),
			         disable_color ? "" : TITLE_END,
			         tracker_sparql_cursor_get_string (cursor, 1, NULL),
			         tracker_sparql_cursor_get_string (cursor, 2, NULL));

			print_snippet (tracker_sparql_cursor_get_string (cursor, 3, NULL));

			count++;
		}

		g_print ("\n");

		if (count >= search_limit) {
			show_limit_warning ();
		}

		g_object_unref (cursor);
	}

	return TRUE;
}

static gboolean
get_emails (TrackerSparqlConnection *connection,
            GStrv                    search_terms,
            gboolean                 show_all,
            gint                     search_offset,
            gint                     search_limit,
            gboolean                 use_or_operator,
            gboolean                 details)
{
	gchar *fts;
	gchar *query;
	gboolean success;

	fts = get_fts_string (search_terms, use_or_operator);

	if (fts) {
		query = g_strdup_printf ("SELECT nie:url(?email) nmo:receivedDate(?email) nmo:messageSubject(?email) fts:snippet(?email, \"%s\", \"%s\") "
		                         "WHERE { "
		                         "  ?email a nmo:Email ;"
		                         "  fts:match \"%s\" ."
		                         "} "
		                         "ORDER BY ASC(nmo:messageSubject(?email)) ASC(nmo:receivedDate(?email))"
		                         "OFFSET %d "
		                         "LIMIT %d",
		                         disable_color ? "" : SNIPPET_BEGIN,
		                         disable_color ? "" : SNIPPET_END,
		                         fts,
		                         search_offset,
		                         search_limit);
	} else {
		query = g_strdup_printf ("SELECT nie:url(?email) nmo:receivedDate(?email) nmo:messageSubject(?email) "
		                         "WHERE { "
		                         "  ?email a nmo:Email ."
		                         "} "
		                         "ORDER BY ASC(nmo:messageSubject(?email)) ASC(nmo:receivedDate(?email))"
		                         "OFFSET %d "
		                         "LIMIT %d",
		                         search_offset,
		                         search_limit);
	}

	success = get_emails_results (connection, query, search_limit, details);
	g_free (query);
	g_free (fts);

	return success;
}

static gchar *
get_directory_limit_search()
{
	gchar * search_directory_str, * full_path;

	full_path = g_canonicalize_filename (search_directory, NULL); // get full path from relative if used
	//check if directory limiting is requested
	search_directory_str = full_path?
		g_strdup_printf ("nie:url ?furl ."
			"  FILTER(fn:starts-with(?furl, \"file://%s\")) ."
			, full_path) : ".";

	return search_directory_str;
}

static gboolean
get_files_results (TrackerSparqlConnection *connection,
                   const gchar             *query,
                   gint                     search_limit,
                   gboolean                 details)
{
	GError *error = NULL;
	TrackerSparqlCursor *cursor;
	cursor = tracker_sparql_connection_query (connection, query, NULL, &error);

	if (error) {
		g_printerr ("%s, %s\n",
		            _("Could not get search results"),
		            error->message);
		g_error_free (error);

		return FALSE;
	}

	if (!cursor) {
		g_print ("%s\n",
		         _("No files were found"));
	} else {
		gint count = 0;

		g_print ("%s:\n", _("Files"));

		while (tracker_sparql_cursor_next (cursor, NULL, NULL)) {
			if (details) {
				g_print ("  %s%s%s (%s)\n",
		                         disable_color ? "" : TITLE_BEGIN,
				         tracker_sparql_cursor_get_string (cursor, 1, NULL),
		                         disable_color ? "" : TITLE_END,
				         tracker_sparql_cursor_get_string (cursor, 0, NULL));

				if (tracker_sparql_cursor_get_n_columns (cursor) > 2)
					print_snippet (tracker_sparql_cursor_get_string (cursor, 2, NULL));
			} else {
				g_print ("  %s%s%s\n",
		                         disable_color ? "" : TITLE_BEGIN,
				         tracker_sparql_cursor_get_string (cursor, 1, NULL),
				         disable_color ? "" : TITLE_END);

				if (tracker_sparql_cursor_get_n_columns (cursor) > 2)
					print_snippet (tracker_sparql_cursor_get_string (cursor, 2, NULL));
			}

			count++;
		}

		g_print ("\n");

		if (count >= search_limit) {
			show_limit_warning ();
		}

		g_object_unref (cursor);
	}

	return TRUE;
}

static gboolean
get_document_files (TrackerSparqlConnection *connection,
                    GStrv                    search_terms,
                    gboolean                 show_all,
                    gint                     search_offset,
                    gint                     search_limit,
                    gboolean                 use_or_operator,
                    gboolean                 details)
{
	gchar *fts;
	gchar *query;
	const gchar *show_all_str;
	gboolean success;
	gchar *search_directory_str;

	search_directory_str = get_directory_limit_search ();
	show_all_str = show_all ? "" : "?document tracker:available true .";
	fts = get_fts_string (search_terms, use_or_operator);

	if (fts) {
		query = g_strdup_printf ("SELECT ?document nie:url(?document) fts:snippet(?document, \"%s\", \"%s\") "
		                         "WHERE { "
		                         "  ?document a nfo:Document ;"
		                         "  fts:match \"%s\" ."
		                         "  %s"
		                         "} "
		                         "ORDER BY ASC(nie:url(?document)) "
		                         "OFFSET %d "
		                         "LIMIT %d",
		                         disable_color ? "" : SNIPPET_BEGIN,
		                         disable_color ? "" : SNIPPET_END,
		                         fts,
		                         show_all_str,
		                         search_offset,
		                         search_limit);
	} else {
		query = g_strdup_printf ("SELECT ?document nie:url(?document) "
		                         "WHERE { "
		                         "  ?document a nfo:Document ;"
		                         "  %s"
		                         "  %s"
		                         "} "
		                         "ORDER BY ASC(nie:url(?document)) "
		                         "OFFSET %d "
		                         "LIMIT %d",
		                         search_directory_str,
		                         show_all_str,
		                         search_offset,
		                         search_limit);
	}

	success = get_files_results (connection, query, search_limit, details);
	g_free (query);
	g_free (fts);

	return success;
}

static gboolean
get_video_files (TrackerSparqlConnection *connection,
                 GStrv                    search_terms,
                 gboolean                 show_all,
                 gint                     search_offset,
                 gint                     search_limit,
                 gboolean                 use_or_operator,
                 gboolean                 details)
{
	gchar *fts;
	gchar *query;
	const gchar *show_all_str;
	gboolean success;
	gchar *search_directory_str;
	
	search_directory_str = get_directory_limit_search ();
	show_all_str = show_all ? "" : "?video tracker:available true . ";
	fts = get_fts_string (search_terms, use_or_operator);

	if (fts) {
		query = g_strdup_printf ("SELECT ?video nie:url(?video) fts:snippet(?video, \"%s\", \"%s\") "
		                         "WHERE { "
		                         "  ?video a nfo:Video ;"
		                         "  fts:match \"%s\" ."
		                         "  %s"
		                         "} "
		                         "ORDER BY ASC(nie:url(?video)) "
		                         "OFFSET %d "
		                         "LIMIT %d",
		                         disable_color ? "" : SNIPPET_BEGIN,
		                         disable_color ? "" : SNIPPET_END,
		                         fts,
		                         show_all_str,
		                         search_offset,
		                         search_limit);
	} else {
		query = g_strdup_printf ("SELECT ?video nie:url(?video) "
		                         "WHERE { "
		                         "  ?video a nfo:Video ;"
		                         "  %s"
		                         "  %s"
		                         "} "
		                         "ORDER BY ASC(nie:url(?video)) "
		                         "OFFSET %d "
		                         "LIMIT %d",
		                         search_directory_str,
		                         show_all_str,
		                         search_offset,
		                         search_limit);
	}

	success = get_files_results (connection, query, search_limit, details);
	g_free (query);
	g_free (fts);

	return success;
}

static gboolean
get_image_files (TrackerSparqlConnection *connection,
                 GStrv                    search_terms,
                 gboolean                 show_all,
                 gint                     search_offset,
                 gint                     search_limit,
                 gboolean                 use_or_operator,
                 gboolean                 details)
{
	gchar *fts;
	gchar *query;
	const gchar *show_all_str;
	gboolean success;
	gchar *search_directory_str;
	
	search_directory_str = get_directory_limit_search ();
	show_all_str = show_all ? "" : "?image tracker:available true . ";
	fts = get_fts_string (search_terms, use_or_operator);

	if (fts) {
		query = g_strdup_printf ("SELECT ?image nie:url(?image) fts:snippet(?image, \"%s\", \"%s\") "
		                         "WHERE { "
		                         "  ?image a nfo:Image ;"
		                         "  fts:match \"%s\" ."
		                         "  %s"
		                         "} "
		                         "ORDER BY ASC(nie:url(?image)) "
		                         "OFFSET %d "
		                         "LIMIT %d",
		                         disable_color ? "" : SNIPPET_BEGIN,
		                         disable_color ? "" : SNIPPET_END,
		                         fts,
		                         show_all_str,
		                         search_offset,
		                         search_limit);
	} else {
		

		query = g_strdup_printf ("SELECT ?image nie:url(?image) "
		                         "WHERE { "
		                         "  ?image a nfo:Image ;"
		                         "  %s"
		                         "  %s"
		                         "} "
		                         "ORDER BY ASC(nie:url(?image)) "
		                         "OFFSET %d "
		                         "LIMIT %d",
		                         search_directory_str,
		                         show_all_str,
		                         search_offset,
		                         search_limit);
	}

	success = get_files_results (connection, query, search_limit, details);
	g_free (query);
	g_free (fts);

	return success;
}

static gboolean
get_music_files (TrackerSparqlConnection *connection,
                 GStrv                    search_terms,
                 gboolean                 show_all,
                 gint                     search_offset,
                 gint                     search_limit,
                 gboolean                 use_or_operator,
                 gboolean                 details)
{
	gchar *fts;
	gchar *query;
	const gchar *show_all_str;
	gboolean success;
	gchar *search_directory_str;
	
	search_directory_str = get_directory_limit_search ();
	show_all_str = show_all ? "" : "?song tracker:available true . ";
	fts = get_fts_string (search_terms, use_or_operator);

	if (fts) {
		query = g_strdup_printf ("SELECT ?song nie:url(?song) fts:snippet(?song, \"%s\", \"%s\")"
		                         "WHERE { "
		                         "  ?song a nmm:MusicPiece ;"
		                         "  fts:match \"%s\" ."
		                         "  %s"
		                         "} "
		                         "ORDER BY ASC(nie:url(?song)) "
		                         "OFFSET %d "
		                         "LIMIT %d",
		                         disable_color ? "" : SNIPPET_BEGIN,
		                         disable_color ? "" : SNIPPET_END,
		                         fts,
		                         show_all_str,
		                         search_offset,
		                         search_limit);
	} else {
		query = g_strdup_printf ("SELECT ?song nie:url(?song) "
		                         "WHERE { "
		                         "  ?song a nmm:MusicPiece ;"
		                         "  %s"
		                         "  %s"
		                         "} "
		                         "ORDER BY ASC(nie:url(?song)) "
		                         "OFFSET %d "
		                         "LIMIT %d",
		                         search_directory_str,
		                         show_all_str,
		                         search_offset,
		                         search_limit);
	}

	success = get_files_results (connection, query, search_limit, details);
	g_free (query);
	g_free (fts);

	return success;
}

static gboolean
get_music_artists (TrackerSparqlConnection *connection,
                   GStrv                    search_terms,
                   gint                     search_offset,
                   gint                     search_limit,
                   gboolean                 use_or_operator,
                   gboolean                 details)
{
	GError *error = NULL;
	TrackerSparqlCursor *cursor;
	gchar *fts;
	gchar *query;
	gchar *search_directory_str;
	
	search_directory_str = get_directory_limit_search ();
	fts = get_fts_string (search_terms, use_or_operator);

	if (fts) {
		query = g_strdup_printf ("SELECT ?artist ?title "
		                         "WHERE {"
		                         "  ?artist a nmm:Artist ;"
		                         "  nmm:artistName ?title ;"
		                         "  fts:match \"%s\" . "
		                         "} "
		                         "ORDER BY ASC(?title) "
		                         "OFFSET %d "
		                         "LIMIT %d",
		                         fts,
		                         search_offset,
		                         search_limit);
	} else {
		query = g_strdup_printf ("SELECT ?artist ?title "
		                         "WHERE {"
		                         "  ?artist a nmm:Artist ;"
		                         "  nmm:artistName ?title ; "
		                         "  %s"
		                         "} "
		                         "ORDER BY ASC(?title) "
		                         "OFFSET %d "
		                         "LIMIT %d",
		                         search_directory_str,
		                         search_offset,
		                         search_limit);
	}

	g_free (fts);

	cursor = tracker_sparql_connection_query (connection, query, NULL, &error);
	g_free (query);

	if (error) {
		g_printerr ("%s, %s\n",
		            _("Could not get search results"),
		            error->message);
		g_error_free (error);

		return FALSE;
	}

	if (!cursor) {
		g_print ("%s\n",
		         _("No artists were found"));
	} else {
		gint count = 0;

		g_print ("%s:\n", _("Artists"));

		while (tracker_sparql_cursor_next (cursor, NULL, NULL)) {
			if (details) {
				g_print ("  '%s%s%s' (%s)\n",
		                         disable_color ? "" : TITLE_BEGIN,
				         tracker_sparql_cursor_get_string (cursor, 1, NULL),
		                         disable_color ? "" : TITLE_END,
				         tracker_sparql_cursor_get_string (cursor, 0, NULL));
			} else {
				g_print ("  '%s%s%s'\n",
		                         disable_color ? "" : TITLE_BEGIN,
				         tracker_sparql_cursor_get_string (cursor, 1, NULL),
				         disable_color ? "" : TITLE_END);
			}
			count++;
		}

		g_print ("\n");

		if (count >= search_limit) {
			show_limit_warning ();
		}

		g_object_unref (cursor);
	}

	return TRUE;
}

static gboolean
get_music_albums (TrackerSparqlConnection *connection,
                  GStrv                    search_words,
                  gint                     search_offset,
                  gint                     search_limit,
                  gboolean                 use_or_operator,
                  gboolean                 details)
{
	GError *error = NULL;
	TrackerSparqlCursor *cursor;
	gchar *fts;
	gchar *query;
	gchar *search_directory_str;
	
	search_directory_str = get_directory_limit_search ();
	fts = get_fts_string (search_words, use_or_operator);

	if (fts) {
		query = g_strdup_printf ("SELECT ?album nie:title(?album) "
		                         "WHERE {"
		                         "  ?album a nmm:MusicAlbum ;"
		                         "  fts:match \"%s\" ."
		                         "} "
		                         "ORDER BY ASC(nie:title(?album)) "
		                         "OFFSET %d "
		                         "LIMIT %d",
		                         fts,
		                         search_offset,
		                         search_limit);
	} else {
		query = g_strdup_printf ("SELECT ?album nie:title(?album) "
		                         "WHERE {"
		                         "  ?album a nmm:MusicAlbum ;"
		                         "  %s"
		                         "} "
		                         "ORDER BY ASC(nie:title(?album)) "
		                         "OFFSET %d "
		                         "LIMIT %d",
		                         search_directory_str,
		                         search_offset,
		                         search_limit);
	}

	g_free (fts);

	cursor = tracker_sparql_connection_query (connection, query, NULL, &error);
	g_free (query);

	if (error) {
		g_printerr ("%s, %s\n",
		            _("Could not get search results"),
		            error->message);
		g_error_free (error);

		return FALSE;
	}

	if (!cursor) {
		g_print ("%s\n",
		         _("No music was found"));
	} else {
		gint count = 0;

		g_print ("%s:\n", _("Albums"));

		while (tracker_sparql_cursor_next (cursor, NULL, NULL)) {
			if (details) {
				g_print ("  '%s%s%s' (%s)\n",
		                         disable_color ? "" : TITLE_BEGIN,
				         tracker_sparql_cursor_get_string (cursor, 1, NULL),
		                         disable_color ? "" : TITLE_END,
				         tracker_sparql_cursor_get_string (cursor, 0, NULL));
			} else {
				g_print ("  '%s%s%s'\n",
		                         disable_color ? "" : TITLE_BEGIN,
				         tracker_sparql_cursor_get_string (cursor, 1, NULL),
				         disable_color ? "" : TITLE_END);
			}
			count++;
		}

		g_print ("\n");

		if (count >= search_limit) {
			show_limit_warning ();
		}

		g_object_unref (cursor);
	}

	return TRUE;
}

static gboolean
get_bookmarks (TrackerSparqlConnection *connection,
               GStrv                    search_terms,
               gint                     search_offset,
               gint                     search_limit,
               gboolean                 use_or_operator)
{
	GError *error = NULL;
	TrackerSparqlCursor *cursor;
	gchar *fts;
	gchar *query;
	gchar *search_directory_str;
	
	search_directory_str = get_directory_limit_search ();
	fts = get_fts_string (search_terms, use_or_operator);

	if (fts) {
		query = g_strdup_printf ("SELECT nie:title(?urn) nie:url(?bookmark) "
		                         "WHERE {"
		                         "  ?urn a nfo:Bookmark ;"
		                         "       nfo:bookmarks ?bookmark ."
		                         "  ?urn fts:match \"%s\" . "
		                         "} "
		                         "ORDER BY ASC(nie:title(?urn)) "
		                         "OFFSET %d "
		                         "LIMIT %d",
		                         fts,
		                         search_offset,
		                         search_limit);
	} else {
		query = g_strdup_printf ("SELECT nie:title(?urn) nie:url(?bookmark) "
		                         "WHERE {"
		                         "  ?urn a nfo:Bookmark ;"
		                         "       nfo:bookmarks ?bookmark ;"
		                         "  %s"
		                         "} "
		                         "ORDER BY ASC(nie:title(?urn)) "
		                         "OFFSET %d "
		                         "LIMIT %d",
		                         search_directory_str,
		                         search_offset,
		                         search_limit);
	}

	g_free (fts);

	cursor = tracker_sparql_connection_query (connection, query, NULL, &error);
	g_free (query);

	if (error) {
		g_printerr ("%s, %s\n",
		            _("Could not get search results"),
		            error->message);
		g_error_free (error);

		return FALSE;
	}

	if (!cursor) {
		g_print ("%s\n",
		         _("No bookmarks were found"));
	} else {
		gint count = 0;

		g_print ("%s:\n", _("Bookmarks"));

		while (tracker_sparql_cursor_next (cursor, NULL, NULL)) {
			g_print ("  %s%s%s (%s)\n",
			         disable_color ? "" : TITLE_BEGIN,
			         tracker_sparql_cursor_get_string (cursor, 0, NULL),
			         disable_color ? "" : TITLE_END,
			         tracker_sparql_cursor_get_string (cursor, 1, NULL));

			count++;
		}

		g_print ("\n");

		if (count >= search_limit) {
			show_limit_warning ();
		}

		g_object_unref (cursor);
	}

	return TRUE;
}

static gboolean
get_feeds (TrackerSparqlConnection *connection,
           GStrv                    search_terms,
           gint                     search_offset,
           gint                     search_limit,
           gboolean                 use_or_operator)
{
	GError *error = NULL;
	TrackerSparqlCursor *cursor;
	gchar *fts;
	gchar *query;
	gchar *search_directory_str;
	
	search_directory_str = get_directory_limit_search ();
	fts = get_fts_string (search_terms, use_or_operator);

	if (fts) {
		query = g_strdup_printf ("SELECT ?feed nie:title(?feed) "
		                         "WHERE {"
		                         "  ?feed a mfo:FeedMessage ;"
		                         "  fts:match \"%s\" . "
		                         "} "
		                         "ORDER BY ASC(nie:title(?feed)) "
		                         "OFFSET %d "
		                         "LIMIT %d",
		                         fts,
		                         search_offset,
		                         search_limit);
	} else {
		query = g_strdup_printf ("SELECT ?feed nie:title(?feed) "
		                         "WHERE {"
		                         "  ?feed a mfo:FeedMessage ;"
		                         "  %s"
		                         "} "
		                         "ORDER BY ASC(nie:title(?feed)) "
		                         "OFFSET %d "
		                         "LIMIT %d",
		                         search_directory_str,
		                         search_offset,
		                         search_limit);
	}

	g_free (fts);

	cursor = tracker_sparql_connection_query (connection, query, NULL, &error);
	g_free (query);

	if (error) {
		g_printerr ("%s, %s\n",
		            _("Could not get search results"),
		            error->message);
		g_error_free (error);

		return FALSE;
	}

	if (!cursor) {
		g_print ("%s\n",
		         _("No feeds were found"));
	} else {
		gint count = 0;

		g_print ("%s:\n", _("Feeds"));

		while (tracker_sparql_cursor_next (cursor, NULL, NULL)) {
			g_print ("  %s%s%s (%s)\n",
			         disable_color ? "" : TITLE_BEGIN,
			         tracker_sparql_cursor_get_string (cursor, 0, NULL),
			         disable_color ? "" : TITLE_END,
			         tracker_sparql_cursor_get_string (cursor, 1, NULL));

			count++;
		}

		g_print ("\n");

		if (count >= search_limit) {
			show_limit_warning ();
		}

		g_object_unref (cursor);
	}

	return TRUE;
}

static gboolean
get_software (TrackerSparqlConnection *connection,
              GStrv                    search_terms,
              gint                     search_offset,
              gint                     search_limit,
              gboolean                 use_or_operator)
{
	GError *error = NULL;
	TrackerSparqlCursor *cursor;
	gchar *fts;
	gchar *query;
	gchar *search_directory_str;
	
	search_directory_str = get_directory_limit_search ();
	fts = get_fts_string (search_terms, use_or_operator);

	if (fts) {
		query = g_strdup_printf ("SELECT ?soft nie:title(?soft) fts:snippet(?soft, \"%s\", \"%s\") "
		                         "WHERE {"
		                         "  ?soft a nfo:Software ;"
		                         "  fts:match \"%s\" . "
		                         "} "
		                         "ORDER BY ASC(nie:title(?soft)) "
		                         "OFFSET %d "
		                         "LIMIT %d",
		                         disable_color ? "" : SNIPPET_BEGIN,
		                         disable_color ? "" : SNIPPET_END,
		                         fts,
		                         search_offset,
		                         search_limit);
	} else {
		query = g_strdup_printf ("SELECT ?soft nie:title(?soft) "
		                         "WHERE {"
		                         "  ?soft a nfo:Software ;"
		                         "  %s"
		                         "} "
		                         "ORDER BY ASC(nie:title(?soft)) "
		                         "OFFSET %d "
		                         "LIMIT %d",
		                         search_directory_str,
		                         search_offset,
		                         search_limit);
	}

	g_free (fts);

	cursor = tracker_sparql_connection_query (connection, query, NULL, &error);
	g_free (query);

	if (error) {
		g_printerr ("%s, %s\n",
		            _("Could not get search results"),
		            error->message);
		g_error_free (error);

		return FALSE;
	}

	if (!cursor) {
		g_print ("%s\n",
		         _("No software was found"));
	} else {
		gint count = 0;

		g_print ("%s:\n", _("Software"));

		while (tracker_sparql_cursor_next (cursor, NULL, NULL)) {
			g_print ("  %s%s%s (%s)\n",
			         disable_color ? "" : TITLE_BEGIN,
			         tracker_sparql_cursor_get_string (cursor, 0, NULL),
			         disable_color ? "" : TITLE_END,
			         tracker_sparql_cursor_get_string (cursor, 1, NULL));
			print_snippet (tracker_sparql_cursor_get_string (cursor, 2, NULL));
			count++;
		}

		g_print ("\n");

		if (count >= search_limit) {
			show_limit_warning ();
		}

		g_object_unref (cursor);
	}

	return TRUE;
}

static gboolean
get_software_categories (TrackerSparqlConnection *connection,
                         GStrv                    search_terms,
                         gint                     search_offset,
                         gint                     search_limit,
                         gboolean                 use_or_operator)
{
	GError *error = NULL;
	TrackerSparqlCursor *cursor;
	gchar *fts;
	gchar *query;
	gchar *search_directory_str;
	
	search_directory_str = get_directory_limit_search ();
	fts = get_fts_string (search_terms, use_or_operator);

	if (fts) {
		query = g_strdup_printf ("SELECT ?cat nie:title(?cat) "
		                         "WHERE {"
		                         "  ?cat a nfo:SoftwareCategory ;"
		                         "  fts:match \"%s\" . "
		                         "} "
		                         "ORDER BY ASC(nie:title(?cat)) "
		                         "OFFSET %d "
		                         "LIMIT %d",
		                         fts,
		                         search_offset,
		                         search_limit);
	} else {
		query = g_strdup_printf ("SELECT ?cat nie:title(?cat) "
		                         "WHERE {"
		                         "  ?cat a nfo:SoftwareCategory ;"
		                         "  %s"
		                         "} "
		                         "ORDER BY ASC(nie:title(?cat)) "
		                         "OFFSET %d "
		                         "LIMIT %d",
		                         search_directory_str,
		                         search_offset,
		                         search_limit);
	}

	g_free (fts);

	cursor = tracker_sparql_connection_query (connection, query, NULL, &error);
	g_free (query);

	if (error) {
		g_printerr ("%s, %s\n",
		            _("Could not get search results"),
		            error->message);
		g_error_free (error);

		return FALSE;
	}

	if (!cursor) {
		g_print ("%s\n",
		         _("No software categories were found"));
	} else {
		gint count = 0;

		g_print ("%s:\n", _("Software Categories"));

		while (tracker_sparql_cursor_next (cursor, NULL, NULL)) {
			g_print ("  %s%s%s (%s)\n",
			         disable_color ? "" : TITLE_BEGIN,
			         tracker_sparql_cursor_get_string (cursor, 0, NULL),
			         disable_color ? "" : TITLE_END,
			         tracker_sparql_cursor_get_string (cursor, 1, NULL));

			count++;
		}

		g_print ("\n");

		if (count >= search_limit) {
			show_limit_warning ();
		}

		g_object_unref (cursor);
	}

	return TRUE;
}

static gboolean
get_files (TrackerSparqlConnection *connection,
           GStrv                    search_terms,
           gboolean                 show_all,
           gint                     search_offset,
           gint                     search_limit,
           gboolean                 use_or_operator,
           gboolean                 details)
{
	gchar *fts;
	gchar *query;
	const gchar *show_all_str;
	gboolean success;
	gchar *search_directory_str;
	
	search_directory_str = get_directory_limit_search ();
	show_all_str = show_all ? "" : "?u tracker:available true . ";
	fts = get_fts_string (search_terms, use_or_operator);

	if (fts) {
		query = g_strdup_printf ("SELECT ?u ?url "
		                         "WHERE { "
		                         "  ?u a nie:InformationElement ;"
		                         "  nie:url ?url ;"
		                         "  fts:match \"%s\" ."
		                         "  %s"
		                         "} "
		                         "ORDER BY ASC(nie:url(?u)) "
		                         "OFFSET %d "
		                         "LIMIT %d",
		                         fts,
		                         show_all_str,
		                         search_offset,
		                         search_limit);
	} else {
		query = g_strdup_printf ("SELECT ?u ?url "
		                         "WHERE { "
		                         "  ?u a nie:InformationElement ;"
		                         "     nie:url ?url ;"
		                         "  %s"
		                         "  %s"
		                         "} "
		                         "ORDER BY ASC(nie:url(?u)) "
		                         "OFFSET %d "
		                         "LIMIT %d",
		                         search_directory_str,
		                         show_all_str,
		                         search_offset,
		                         search_limit);
	}

	success = get_files_results (connection, query, search_limit, details);
	g_free (query);
	g_free (fts);

	return success;
}

static gboolean
get_folders (TrackerSparqlConnection *connection,
             GStrv                    search_terms,
             gboolean                 show_all,
             gint                     search_offset,
             gint                     search_limit,
             gboolean                 use_or_operator,
             gboolean                 details)
{
	gchar *fts;
	gchar *query;
	const gchar *show_all_str;
	gboolean success;
	gchar *search_directory_str;
	
	search_directory_str = get_directory_limit_search ();
	show_all_str = show_all ? "" : "?u tracker:available true . ";
	fts = get_fts_string (search_terms, use_or_operator);

	if (fts) {
		query = g_strdup_printf ("SELECT ?u nie:url(?u) "
		                         "WHERE { "
		                         "  ?u a nfo:Folder ;"
		                         "  fts:match \"%s\" ."
		                         "  %s"
		                         "} "
		                         "ORDER BY ASC(nie:url(?u)) "
		                         "OFFSET %d "
		                         "LIMIT %d",
		                         fts,
		                         show_all_str,
		                         search_offset,
		                         search_limit);
	} else {
		query = g_strdup_printf ("SELECT ?u nie:url(?u) "
		                         "WHERE { "
		                         "  ?u a nfo:Folder ;"
		                         "  %s"
		                         "  %s"
		                         "} "
		                         "ORDER BY ASC(nie:url(?u)) "
		                         "OFFSET %d "
		                         "LIMIT %d",
		                         search_directory_str,
		                         show_all_str,
		                         search_offset,
		                         search_limit);
	}

	success = get_files_results (connection, query, search_limit, details);
	g_free (query);
	g_free (fts);

	return success;
}

static gboolean
get_all_by_search (TrackerSparqlConnection *connection,
                   GStrv                    search_words,
                   gboolean                 show_all,
                   gint                     search_offset,
                   gint                     search_limit,
                   gboolean                 use_or_operator,
                   gboolean                 details)
{
	GError *error = NULL;
	TrackerSparqlCursor *cursor;
	gchar *fts;
	gchar *query;
	const gchar *show_all_str;
	gchar *search_directory_str;
	
	search_directory_str = get_directory_limit_search ();
	fts = get_fts_string (search_words, use_or_operator);
	if (!fts) {
		return FALSE;
	}

	show_all_str = show_all ? "" : "?s tracker:available true . ";

	if (details) {
		query = g_strdup_printf ("SELECT tracker:coalesce (nie:url (?s), ?s) nie:mimeType (?s) ?type fts:snippet(?s, \"%s\", \"%s\") "
		                         "WHERE {"
		                         "  ?s fts:match \"%s\" ;"
		                         "  rdf:type ?type ."
		                         "  %s"
		                         "} "
		                         "GROUP BY nie:url(?s) "
		                         "ORDER BY nie:url(?s) "
		                         "OFFSET %d LIMIT %d",
		                         disable_color ? "" : SNIPPET_BEGIN,
		                         disable_color ? "" : SNIPPET_END,
		                         fts,
		                         show_all_str,
		                         search_offset,
		                         search_limit);
	} else {
		query = g_strdup_printf ("SELECT tracker:coalesce (nie:url (?s), ?s) fts:snippet(?s, \"%s\", \"%s\") "
		                         "WHERE {"
		                         "  ?s fts:match \"%s\" ;"
		                         "  %s"
		                         "  %s"
		                         "} "
		                         "ORDER BY nie:url(?s) "
		                         "OFFSET %d LIMIT %d",
		                         disable_color ? "" : SNIPPET_BEGIN,
		                         disable_color ? "" : SNIPPET_END,
		                         fts,
		                         search_directory_str,
		                         show_all_str,
		                         search_offset,
		                         search_limit);
	}

	g_free (fts);

	cursor = tracker_sparql_connection_query (connection, query, NULL, &error);
	g_free (query);

	if (error) {
		g_printerr ("%s, %s\n",
		            _("Could not get search results"),
		            error->message);
		g_error_free (error);

		return FALSE;
	}

	if (!cursor) {
		g_print ("%s\n",
		         _("No results were found matching your query"));
	} else {
		gint count = 0;

		g_print ("%s:\n", _("Results"));

		while (tracker_sparql_cursor_next (cursor, NULL, NULL)) {
			if (details) {
				const gchar *urn;
				const gchar *mime_type;
				const gchar *class;

				g_print ("cols:%d\n", tracker_sparql_cursor_get_n_columns (cursor));

				urn = tracker_sparql_cursor_get_string (cursor, 0, NULL);
				mime_type = tracker_sparql_cursor_get_string (cursor, 1, NULL);
				class = tracker_sparql_cursor_get_string (cursor, 2, NULL);

				if (mime_type && mime_type[0] == '\0') {
					mime_type = NULL;
				}

				if (mime_type) {
					g_print ("  %s%s%s\n"
					         "    %s\n"
					         "    %s\n",
					         disable_color ? "" : TITLE_BEGIN,
					         urn,
					         disable_color ? "" : TITLE_END,
					         mime_type,
					         class);
				} else {
					g_print ("  %s%s%s\n"
					         "    %s\n",
					         disable_color ? "" : TITLE_BEGIN,
					         urn,
					         disable_color ? "" : TITLE_END,
					         class);
				}
				print_snippet (tracker_sparql_cursor_get_string (cursor, 3, NULL));
			} else {
				g_print ("  %s%s%s\n",
		                         disable_color ? "" : TITLE_BEGIN,
				         tracker_sparql_cursor_get_string (cursor, 0, NULL),
				         disable_color ? "" : TITLE_END);
				print_snippet (tracker_sparql_cursor_get_string (cursor, 1, NULL));
			}

			count++;
		}

		g_print ("\n");

		if (count >= search_limit) {
			show_limit_warning ();
		}

		g_object_unref (cursor);
	}

	return TRUE;
}

static gint
search_run (void)
{
	TrackerSparqlConnection *connection;
	GError *error = NULL;

	if (disable_fts) {
		disable_snippets = TRUE;
	}

#if HAVE_TRACKER_FTS
	/* Only check stopwords if FTS is enabled */
	if (terms) {
		TrackerLanguage *language;
		gboolean stop_words_found;
		gchar **p;

		/* Check terms don't have additional quotes */
		for (p = terms; *p; p++) {
			gchar *term = *p;
			gint end = strlen (term) - 1;

			if ((term[0] == '"' && term[end] == '"') ||
			    (term[0] == '\'' && term[end] == '\'')) {
				/* We never have a quote JUST at the end */
				term[0] = term[end] = ' ';
				g_strstrip (term);
			}
		}

		/* Check if terms are stopwords, and warn if so */
		language = tracker_language_new (NULL);
		stop_words_found = FALSE;
		for (p = terms; *p; p++) {
			gchar *down;

			down = g_utf8_strdown (*p, -1);

			if (tracker_language_is_stop_word (language, down)) {
				g_printerr (_("Search term “%s” is a stop word."),
				            down);
				g_printerr ("\n");

				stop_words_found = TRUE;
			}

			g_free (down);
		}

		if (stop_words_found) {
			g_printerr (_("Stop words are common words which "
			              "may be ignored during the indexing "
			              "process."));
			g_printerr ("\n\n");
		}

		g_object_unref (language);
	}
#else
	disable_snippets = TRUE;
#endif

	connection = tracker_sparql_connection_get (NULL, &error);

	if (!connection) {
		g_printerr ("%s: %s\n",
		            _("Could not establish a connection to Tracker"),
		            error ? error->message : _("No error given"));
		g_clear_error (&error);
		return EXIT_FAILURE;
	}

	if (limit <= 0) {
		/* Default to 10 for snippets because more is not
		 * useful on the screen. The categories are those not
		 * using snippets yet.
		 */
		if (disable_snippets || !terms ||
		    (files || folders || contacts || emails ||
		     music_albums || music_artists || bookmarks ||
		     feeds)) {
			limit = 512;
		} else {
			limit = 10;
		}
	}
	
	if (files) {
		gboolean success;

		success = get_files (connection, terms, all, offset, limit, or_operator, detailed);
		g_object_unref (connection);

		return success ? EXIT_SUCCESS : EXIT_FAILURE;
	}

	if (folders) {
		gboolean success;

		success = get_folders (connection, terms, all, offset, limit, or_operator, detailed);
		g_object_unref (connection);

		return success ? EXIT_SUCCESS : EXIT_FAILURE;
	}

	if (music_albums) {
		gboolean success;

		success = get_music_albums (connection, terms, offset, limit, or_operator, detailed);
		g_object_unref (connection);

		return success ? EXIT_SUCCESS : EXIT_FAILURE;
	}

	if (music_artists) {
		gboolean success;

		success = get_music_artists (connection, terms, offset, limit, or_operator, detailed);
		g_object_unref (connection);

		return success ? EXIT_SUCCESS : EXIT_FAILURE;
	}

	if (music_files) {
		gboolean success;

		success = get_music_files (connection, terms, all, offset, limit, or_operator, detailed);
		g_object_unref (connection);

		return success ? EXIT_SUCCESS : EXIT_FAILURE;
	}

	if (feeds) {
		gboolean success;

		success = get_feeds (connection, terms, offset, limit, or_operator);
		g_object_unref (connection);

		return success ? EXIT_SUCCESS : EXIT_FAILURE;
	}

	if (image_files) {
		gboolean success;

		success = get_image_files (connection, terms, all, offset, limit, or_operator, detailed);
		g_object_unref (connection);

		return success ? EXIT_SUCCESS : EXIT_FAILURE;
	}

	if (video_files) {
		gboolean success;

		success = get_video_files (connection, terms, all, offset, limit, or_operator, detailed);
		g_object_unref (connection);

		return success ? EXIT_SUCCESS : EXIT_FAILURE;
	}

	if (document_files) {
		gboolean success;

		success = get_document_files (connection, terms, all, offset, limit, or_operator, detailed);
		g_object_unref (connection);

		return success ? EXIT_SUCCESS : EXIT_FAILURE;
	}

	if (emails) {
		gboolean success;

		success = get_emails (connection, terms, all, offset, limit, or_operator, detailed);
		g_object_unref (connection);

		return success ? EXIT_SUCCESS : EXIT_FAILURE;
	}

	if (contacts) {
		gboolean success;

		success = get_contacts (connection, terms, all, offset, limit, or_operator, detailed);
		g_object_unref (connection);

		return success ? EXIT_SUCCESS : EXIT_FAILURE;
	}

	if (software) {
		gboolean success;

		success = get_software (connection, terms, offset, limit, or_operator);
		g_object_unref (connection);

		return success ? EXIT_SUCCESS : EXIT_FAILURE;
	}

	if (software_categories) {
		gboolean success;

		success = get_software_categories (connection, terms, offset, limit, or_operator);
		g_object_unref (connection);

		return success ? EXIT_SUCCESS : EXIT_FAILURE;
	}

	if (bookmarks) {
		gboolean success;

		success = get_bookmarks (connection, terms, offset, limit, or_operator);
		g_object_unref (connection);

		return success ? EXIT_SUCCESS : EXIT_FAILURE;
	}

	if (terms) {
		gboolean success;

		success = get_all_by_search (connection, terms, all, offset, limit, or_operator, detailed);
		g_object_unref (connection);

		return success ? EXIT_SUCCESS : EXIT_FAILURE;
	}

	g_object_unref (connection);

	/* All known options have their own exit points */
	g_warn_if_reached ();

	return EXIT_FAILURE;
}

static int
search_run_default (void)
{
	GOptionContext *context;
	gchar *help;

	context = g_option_context_new (NULL);
	g_option_context_add_main_entries (context, entries, NULL);
	help = g_option_context_get_help (context, FALSE, NULL);
	g_option_context_free (context);
	g_printerr ("%s\n", help);
	g_free (help);

	return EXIT_FAILURE;
}

static gboolean
search_options_enabled (void)
{
	return SEARCH_OPTIONS_ENABLED ();
}

int
tracker_search (int argc, const char **argv)
{
	GOptionContext *context;
	GError *error = NULL;

	context = g_option_context_new (NULL);
	g_option_context_add_main_entries (context, entries, NULL);

	argv[0] = "tracker search";

	if (!g_option_context_parse (context, &argc, (char***) &argv, &error)) {
		g_printerr ("%s, %s\n", _("Unrecognized options"), error->message);
		g_error_free (error);
		g_option_context_free (context);
		return EXIT_FAILURE;
	}

	g_option_context_free (context);

	if (search_options_enabled ()) {
		return search_run ();
	}

	return search_run_default ();
}
