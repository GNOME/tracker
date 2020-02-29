/*
 * Copyright (C) 2011 Nokia <ivan.frade@nokia.com>
 *
 * Author: Carlos Garnacho <carlos@lanedo.com>
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
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301  USA
 */

/* FTS5 Tokenizer using TrackerParser */

#include "config.h"

#include <assert.h>
#include <string.h>

#include <libtracker-common/tracker-parser.h>
#include <libtracker-data/tracker-data-manager.h>
#include <libtracker-data/tracker-ontologies.h>

#include "tracker-fts-tokenizer.h"
#include "fts5.h"

typedef struct TrackerTokenizerData TrackerTokenizerData;
typedef struct TrackerTokenizer TrackerTokenizer;
typedef struct TrackerTokenizerFunctionData TrackerTokenizerFunctionData;

struct TrackerTokenizerData {
	TrackerLanguage *language;
	TrackerDBManagerFlags flags;
};

struct TrackerTokenizer {
	TrackerTokenizerData *data;
	TrackerParser *parser;
};

struct TrackerTokenizerFunctionData {
	TrackerDBInterface *interface;
	gchar **property_names;
};

#define MAX_WORD_LENGTH 200
#define MAX_WORDS 10000

static int
tracker_tokenizer_create (void           *data,
                          const char    **argv,
                          int             argc,
                          Fts5Tokenizer **tokenizer_out)
{
	TrackerTokenizer *tokenizer;

	tokenizer = g_new0 (TrackerTokenizer, 1);
	tokenizer->data = data;
	tokenizer->parser = tracker_parser_new (tokenizer->data->language);

	*tokenizer_out = (Fts5Tokenizer *) tokenizer;

	return SQLITE_OK;
}

static void
tracker_tokenizer_destroy (Fts5Tokenizer *fts5_tokenizer)
{
	TrackerTokenizer *tokenizer = (TrackerTokenizer *) fts5_tokenizer;

	tracker_parser_free (tokenizer->parser);
	g_free (tokenizer);
}

typedef int (*TokenFunc) (void       *pCtx,    /* Copy of 2nd argument to xTokenize() */
                          int         flags,   /* Mask of FTS5_TOKEN_* flags */
                          const char *token,   /* Pointer to buffer containing token */
                          int         n_token, /* Size of token in bytes */
                          int         start,   /* Byte offset of token within input text */
                          int         end);    /* Byte offset of end of token within input text */

static int
tracker_tokenizer_tokenize (Fts5Tokenizer *fts5_tokenizer,
                            void          *ctx,
                            int            flags, /* Mask of FTS5_TOKENIZE_* flags */
                            const char    *text,
                            int            length,
                            TokenFunc      token_func)
{
	TrackerTokenizer *tokenizer = (TrackerTokenizer *) fts5_tokenizer;
	TrackerTokenizerData *data = tokenizer->data;
	const gchar *token;
	gboolean stop_word, is_prefix_query;
	int n_tokens = 0, pos, start, end, len;
	int rc = SQLITE_OK;

	if (length <= 0)
		return rc;

	is_prefix_query = ((flags & (FTS5_TOKENIZE_QUERY | FTS5_TOKENIZE_PREFIX)) ==
	                   (FTS5_TOKENIZE_QUERY | FTS5_TOKENIZE_PREFIX));

	tracker_parser_reset (tokenizer->parser, text, length,
			      MAX_WORD_LENGTH,
			      !!(data->flags & TRACKER_DB_MANAGER_FTS_ENABLE_STEMMER),
			      !!(data->flags & TRACKER_DB_MANAGER_FTS_ENABLE_UNACCENT),
			      !!(data->flags & TRACKER_DB_MANAGER_FTS_ENABLE_STOP_WORDS),
			      TRUE,
			      !!(data->flags & TRACKER_DB_MANAGER_FTS_IGNORE_NUMBERS));

	while (n_tokens < MAX_WORDS) {
		token = tracker_parser_next (tokenizer->parser,
		                             &pos,
		                             &start, &end,
		                             &stop_word,
		                             &len);

		if (!token)
			break;

		/* When tokenizing prefix query tokens we don't want to
		 * mistake incomplete words as stop words (eg. "onto" when
		 * typing "ontology"), we thus let them go through
		 * even if the parser marked it as a stop word.
		 */
		if (stop_word && !is_prefix_query)
			continue;

		rc = token_func (ctx, 0, token, len, start, end);

		if (rc != SQLITE_OK)
			break;

		n_tokens++;
	}

	return rc;
}

/* Our custom tokenizer: */
static const fts5_tokenizer tracker_tokenizer_module = {
	tracker_tokenizer_create,   /* xCreate   */
	tracker_tokenizer_destroy,  /* xDelete   */
	tracker_tokenizer_tokenize, /* xTokenize */
};

static TrackerTokenizerData *
tracker_tokenizer_data_new (TrackerDBManagerFlags flags)
{
	TrackerTokenizerData *p;

	p = g_new0 (TrackerTokenizerData, 1);
	p->language = tracker_language_new (NULL);
	p->flags = flags;

	return p;
}

static void
tracker_tokenizer_data_free (gpointer user_data)
{
	TrackerTokenizerData *data = user_data;

	g_object_unref (data->language);
	g_free (data);
}

static int
offsets_tokenizer_func (void       *data,
                        int         flags,
                        const char *token,
                        int         n_token,
                        int         start,
                        int         end)
{
	GArray *offsets = data;
	g_array_append_val (offsets, start);
	return SQLITE_OK;
}

static void
tracker_offsets_function (const Fts5ExtensionApi  *api,
                          Fts5Context             *fts_ctx,
                          sqlite3_context         *ctx,
                          int                      n_args,
                          sqlite3_value          **args)
{
	TrackerTokenizerFunctionData *data;
	GString *str;
	int rc, n_hits, i;
	GArray *offsets = NULL;
	gint cur_col = -1;
	gboolean first = TRUE;

	if (n_args > 0) {
		sqlite3_result_error (ctx, "Invalid argument count", -1);
		return;
	}

	data = api->xUserData (fts_ctx);
	rc = api->xInstCount (fts_ctx, &n_hits);

	if (rc != SQLITE_OK) {
		sqlite3_result_null (ctx);
		return;
	}

	str = g_string_new (NULL);

	for (i = 0; i < n_hits; i++) {
		int phrase, col, n_token;

		rc = api->xInst (fts_ctx, i, &phrase, &col, &n_token);
		if (rc != SQLITE_OK)
			break;

		if (first || cur_col != col) {
			const char *text;
			int length;

			if (offsets)
				g_array_free (offsets, TRUE);

			offsets = g_array_new (FALSE, FALSE, sizeof (gint));
			rc = api->xColumnText (fts_ctx, col, &text, &length);
			if (rc != SQLITE_OK)
				break;

			rc = api->xTokenize (fts_ctx, text, length,
					     offsets, &offsets_tokenizer_func);
			if (rc != SQLITE_OK)
				break;

			cur_col = col;
		}

		first = FALSE;

		if (str->len != 0)
			g_string_append_c (str, ',');

		if (rc != SQLITE_OK)
			break;

		g_string_append_printf (str, "%s,%d",
		                        data->property_names[col],
		                        g_array_index (offsets, gint, n_token));
	}

	if (offsets)
		g_array_free (offsets, TRUE);

	if (rc == SQLITE_OK) {
		sqlite3_result_text (ctx, str->str, str->len, g_free);
		g_string_free (str, FALSE);
	} else {
		sqlite3_result_error_code (ctx, rc);
		g_string_free (str, TRUE);
	}
}

static GHashTable *
get_fts_weights (TrackerDBInterface *iface,
                 sqlite3_context    *context)
{
	static GQuark iface_qdata = 0;
	GHashTable *weights;
	int rc = SQLITE_DONE;

	if (G_UNLIKELY (iface_qdata == 0))
		iface_qdata = g_quark_from_static_string ("tracker-fts-weights");

	weights = g_object_get_qdata (G_OBJECT (iface), iface_qdata);

	if (G_UNLIKELY (weights == NULL)) {
		TrackerDataManager *manager;
		TrackerOntologies *ontologies;
		sqlite3_stmt *stmt;
		sqlite3 *db;
		const gchar *uri;

		weights = g_hash_table_new (g_str_hash, g_str_equal);
		db = sqlite3_context_db_handle (context);
		rc = sqlite3_prepare_v2 (db,
		                         "SELECT \"rdf:Property\".\"tracker:weight\", "
		                         "(SELECT Uri FROM Resource where Resource.ID=\"rdf:Property\".ID) "
		                         "FROM \"rdf:Property\" "
		                         "WHERE \"rdf:Property\".\"tracker:fulltextIndexed\" = 1 ",
		                         -1, &stmt, NULL);

		manager = tracker_db_interface_get_user_data (iface);
		ontologies = tracker_data_manager_get_ontologies (manager);

		while ((rc = sqlite3_step (stmt)) != SQLITE_DONE) {
			if (rc == SQLITE_ROW) {
				TrackerProperty *property;
				guint weight;

				weight = sqlite3_column_int (stmt, 0);
				uri = (gchar *)sqlite3_column_text (stmt, 1);

				property = tracker_ontologies_get_property_by_uri (ontologies, uri);
				g_hash_table_insert (weights,
				                     (gpointer) tracker_property_get_name (property),
				                     GUINT_TO_POINTER (weight));
			} else if (rc != SQLITE_BUSY) {
				break;
			}
		}

		sqlite3_finalize (stmt);

		if (rc != SQLITE_DONE) {
			g_hash_table_destroy (weights);
			weights = NULL;
		}

		g_object_set_qdata_full (G_OBJECT (iface), iface_qdata,
		                         weights, (GDestroyNotify) g_hash_table_unref);
	}

	return weights;
}

static void
tracker_rank_function (const Fts5ExtensionApi  *api,
                       Fts5Context             *fts_ctx,
                       sqlite3_context         *ctx,
                       int                      n_args,
                       sqlite3_value          **args)
{
	TrackerTokenizerFunctionData *data;
	int i, rc = SQLITE_OK, n_columns, n_tokens;
	GHashTable *weights;
	gdouble rank = 0;

	if (n_args != 0) {
		sqlite3_result_error (ctx, "Invalid argument count", -1);
		return;
	}

	n_columns = api->xColumnCount (fts_ctx);
	data = api->xUserData (fts_ctx);
	weights = get_fts_weights (data->interface, ctx);

	if (!weights) {
		sqlite3_result_error (ctx, "Could not read FTS weights", -1);
		return;
	}

	for (i = 0; i < n_columns; i++) {
		const gchar *property;
		guint weight;

		rc = api->xColumnSize (fts_ctx, i, &n_tokens);
		if (rc != SQLITE_OK)
			break;

		if (n_tokens <= 0)
			continue;

		property = data->property_names[i];
		weight = GPOINTER_TO_UINT (g_hash_table_lookup (weights, property));
		rank += weight;
	}

	if (rc == SQLITE_OK) {
		sqlite3_result_double (ctx, rank);
	} else {
		sqlite3_result_error_code (ctx, rc);
	}
}

static fts5_api *
get_fts5_api (sqlite3 *db) {
	int rc = SQLITE_OK;
	sqlite3_stmt *stmt;
	fts5_api *api = NULL;

#if SQLITE_VERSION_NUMBER >= 3020000
	/* Sqlite >= 3.20.0 mandates sqlite3_bind_pointer() to fetch fts5 */
	if (sqlite3_libversion_number () >= 3020000) {
		rc = sqlite3_prepare_v2(db, "SELECT fts5(?1)",
		                        -1, &stmt, 0);
		if (rc != SQLITE_OK)
			return NULL;

		sqlite3_bind_pointer (stmt, 1, (void*) &api, "fts5_api_ptr", NULL);
		sqlite3_step (stmt);
	} else
#endif
	{
		rc = sqlite3_prepare_v2(db, "SELECT fts5()",
		                        -1, &stmt, 0);

		if (rc != SQLITE_OK)
			return NULL;

		if (sqlite3_step (stmt) == SQLITE_ROW)
			memcpy (&api, sqlite3_column_blob (stmt, 0), sizeof (api));
	}

	sqlite3_finalize (stmt);

	return api;
}

static TrackerTokenizerFunctionData *
tracker_tokenizer_function_data_new (TrackerDBInterface  *interface,
                                     const gchar        **property_names)
{
	TrackerTokenizerFunctionData *data;

	data = g_new0 (TrackerTokenizerFunctionData, 1);
	data->interface = interface;
	data->property_names = g_strdupv ((gchar **) property_names);

	return data;
}

static void
tracker_tokenizer_function_data_free (TrackerTokenizerFunctionData *data)
{
	g_strfreev (data->property_names);
	g_free (data);
}

gboolean
tracker_tokenizer_initialize (sqlite3                *db,
                              TrackerDBInterface     *interface,
                              TrackerDBManagerFlags   flags,
                              const gchar           **property_names)
{
	TrackerTokenizerData *data;
	TrackerTokenizerFunctionData *func_data;
	fts5_tokenizer *tokenizer;
	fts5_api *api;

	api = get_fts5_api (db);

	if (!api)
		return FALSE;

	data = tracker_tokenizer_data_new (flags);
	tokenizer = (fts5_tokenizer *) &tracker_tokenizer_module;
	api->xCreateTokenizer (api, "TrackerTokenizer", data, tokenizer,
	                       tracker_tokenizer_data_free);

	/* Offsets */
	func_data = tracker_tokenizer_function_data_new (interface, property_names);
	api->xCreateFunction (api, "tracker_offsets", func_data,
	                      &tracker_offsets_function,
	                      (GDestroyNotify) tracker_tokenizer_function_data_free);

	/* Rank */
	func_data = tracker_tokenizer_function_data_new (interface, property_names);
	api->xCreateFunction (api, "tracker_rank", func_data,
	                      &tracker_rank_function,
	                      (GDestroyNotify) tracker_tokenizer_function_data_free);

	return TRUE;
}
