/*
 * Copyright (C) 2020, Red Hat Inc.
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
 *
 * Author: Carlos Garnacho <carlosg@gnome.org>
 */
#include "config.h"

#include "tracker-turtle-reader.h"
#include "tracker-sparql-grammar.h"
#include "tracker-uuid.h"

#include <libtracker-sparql/tracker-connection.h>

#define BUF_SIZE 1024
#define RDF_TYPE "http://www.w3.org/1999/02/22-rdf-syntax-ns#type"

typedef enum
{
	STATE_INITIAL,
	STATE_SUBJECT,
	STATE_PREDICATE,
	STATE_OBJECT,
	STATE_STEP,
} ParserState;

typedef struct {
	gchar *subject;
	gchar *predicate;
	ParserState state;
} StateStack;

struct _TrackerTurtleReader {
	GObject parent_instance;
	GInputStream *stream;
	GBufferedInputStream *buffered_stream;
	GHashTable *blank_nodes;
	GHashTable *prefixes;
	GArray *parser_state;
	gchar *base;
	gchar *subject;
	gchar *predicate;
	gchar *object;
	gboolean object_is_uri;
	ParserState state;
};

enum {
	PROP_STREAM = 1,
	N_PROPS
};

static GParamSpec *props[N_PROPS] = { 0 };

G_DEFINE_TYPE (TrackerTurtleReader,
               tracker_turtle_reader,
               G_TYPE_OBJECT)

static void
tracker_turtle_reader_finalize (GObject *object)
{
	TrackerTurtleReader *reader = TRACKER_TURTLE_READER (object);

	g_input_stream_close (G_INPUT_STREAM (reader->buffered_stream), NULL, NULL);
	g_input_stream_close (reader->stream, NULL, NULL);
	g_clear_object (&reader->buffered_stream);
	g_clear_object (&reader->stream);
	g_clear_pointer (&reader->blank_nodes, g_hash_table_unref);
	g_clear_pointer (&reader->prefixes, g_hash_table_unref);
	g_clear_pointer (&reader->parser_state, g_array_unref);
	g_clear_pointer (&reader->subject, g_free);
	g_clear_pointer (&reader->predicate, g_free);
	g_clear_pointer (&reader->object, g_free);
	g_clear_pointer (&reader->base, g_free);

	G_OBJECT_CLASS (tracker_turtle_reader_parent_class)->finalize (object);
}

static void
tracker_turtle_reader_constructed (GObject *object)
{
	TrackerTurtleReader *reader = TRACKER_TURTLE_READER (object);

	reader->buffered_stream =
		G_BUFFERED_INPUT_STREAM (g_buffered_input_stream_new (reader->stream));

	G_OBJECT_CLASS (tracker_turtle_reader_parent_class)->constructed (object);
}

static void
tracker_turtle_reader_set_property (GObject      *object,
                                    guint         prop_id,
                                    const GValue *value,
                                    GParamSpec   *pspec)
{
	TrackerTurtleReader *reader = TRACKER_TURTLE_READER (object);

	switch (prop_id) {
	case PROP_STREAM:
		reader->stream = g_value_dup_object (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
tracker_turtle_reader_get_property (GObject    *object,
                                    guint       prop_id,
                                    GValue     *value,
                                    GParamSpec *pspec)
{
	TrackerTurtleReader *reader = TRACKER_TURTLE_READER (object);

	switch (prop_id) {
	case PROP_STREAM:
		g_value_set_object (value, reader->stream);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
tracker_turtle_reader_class_init (TrackerTurtleReaderClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = tracker_turtle_reader_finalize;
	object_class->constructed = tracker_turtle_reader_constructed;
	object_class->set_property = tracker_turtle_reader_set_property;
	object_class->get_property = tracker_turtle_reader_get_property;

	props[PROP_STREAM] =
		g_param_spec_object ("stream",
		                     "Stream",
		                     "Stream",
		                     G_TYPE_INPUT_STREAM,
		                     G_PARAM_READWRITE |
		                     G_PARAM_CONSTRUCT_ONLY);

	g_object_class_install_properties (object_class, N_PROPS, props);
}

static void
tracker_turtle_reader_init (TrackerTurtleReader *reader)
{
	reader->blank_nodes = g_hash_table_new_full (g_str_hash, g_str_equal,
	                                             g_free, g_free);
	reader->prefixes = g_hash_table_new_full (g_str_hash, g_str_equal,
	                                          g_free, g_free);
	reader->parser_state = g_array_new (FALSE, FALSE, sizeof (StateStack));
}

TrackerTurtleReader *
tracker_turtle_reader_new (GInputStream *istream)
{
	g_return_val_if_fail (G_IS_INPUT_STREAM (istream), NULL);

	return g_object_new (TRACKER_TYPE_TURTLE_READER,
	                     "stream", istream,
	                     NULL);
}

TrackerTurtleReader *
tracker_turtle_reader_new_for_file (GFile   *file,
                                    GError **error)
{
	TrackerTurtleReader *reader;
	GInputStream *istream;

	g_return_val_if_fail (G_IS_FILE (file), NULL);
	g_return_val_if_fail (!error || !*error, NULL);

	istream = G_INPUT_STREAM (g_file_read (file, NULL, error));
	if (!istream)
		return NULL;

	reader = tracker_turtle_reader_new (istream);
	g_object_unref (istream);

	return reader;
}

static void
push_stack (TrackerTurtleReader *reader)
{
	StateStack state;

	state.subject = g_strdup (reader->subject);
	state.predicate = g_strdup (reader->predicate);
	state.state = reader->state;
	g_array_append_val (reader->parser_state, state);
}

static void
pop_stack (TrackerTurtleReader *reader)
{
	StateStack *state;
	gchar *s, *p, *o;

	s = reader->subject;
	p = reader->predicate;
	o = reader->object;
	reader->subject = reader->predicate = reader->object = NULL;

	state = &g_array_index (reader->parser_state, StateStack, reader->parser_state->len - 1);
	reader->subject = state->subject;
	reader->predicate = state->predicate;
	reader->state = state->state;

	if (reader->state == STATE_OBJECT) {
		/* Restore the old subject as current object */
		reader->object = s;
		reader->object_is_uri = TRUE;
		s = NULL;
	} else if (reader->state == STATE_SUBJECT) {
		g_clear_pointer (&reader->subject, g_free);
		reader->subject = s;
		s = NULL;
	}

	g_free (s);
	g_free (p);
	g_free (o);
	g_array_remove_index (reader->parser_state, reader->parser_state->len - 1);
}

static gboolean
parse_token (TrackerTurtleReader *reader,
             const gchar         *token)
{
	int len = strlen (token);
	const gchar *buffer;
	gsize size;

	buffer = g_buffered_input_stream_peek_buffer (reader->buffered_stream,
	                                              &size);
	if (size == 0)
		return FALSE;
	if (strncasecmp (buffer, token, len) != 0)
		return FALSE;
	if (!g_input_stream_skip (G_INPUT_STREAM (reader->buffered_stream),
	                          len, NULL, NULL))
		return FALSE;

	return TRUE;
}

static gboolean
parse_terminal (TrackerTurtleReader  *reader,
                TrackerTerminalFunc   terminal_func,
                guint                 padding,
                gchar               **out)
{
	const gchar *end, *buffer;
	gchar *str;
	gsize size;

	buffer = g_buffered_input_stream_peek_buffer (reader->buffered_stream,
	                                              &size);
	if (size == 0)
		return FALSE;

	if (!terminal_func (buffer, &buffer[size], &end))
		return FALSE;

	if (end - buffer < 2 * padding)
		return FALSE;

	str = g_strndup (&buffer[padding], end - buffer - (2 * padding));

	if (!g_input_stream_skip (G_INPUT_STREAM (reader->buffered_stream),
	                          end - buffer, NULL, NULL)) {
		g_free (str);
		return FALSE;
	}

	if (out)
		*out = str;

	return TRUE;
}

static gchar *
generate_bnode (TrackerTurtleReader *reader,
                const gchar         *label)
{
	gchar *bnode;

	if (!label)
		return tracker_generate_uuid ("urn:uuid");

	bnode = g_hash_table_lookup (reader->blank_nodes, label);

	if (!bnode) {
		bnode = tracker_generate_uuid ("urn:uuid");
		g_hash_table_insert (reader->blank_nodes, g_strdup (label), bnode);
	}

	return g_strdup (bnode);
}

static gchar *
expand_prefix (TrackerTurtleReader *reader,
               const gchar         *shortname)
{
	GHashTableIter iter;
	gpointer key, value;

	g_hash_table_iter_init (&iter, reader->prefixes);

	while (g_hash_table_iter_next (&iter, &key, &value)) {
		if (g_str_has_prefix (shortname, key)) {
			GString *str;

			str = g_string_new (value);
			g_string_append (str, &shortname[strlen(key)]);
			return g_string_free (str, FALSE);
		}
	}

	return NULL;
}

static gchar *
expand_base (TrackerTurtleReader *reader,
             gchar               *suffix)
{
	if (reader->base) {
		gchar *str;

		str = g_strdup_printf ("%s%s", reader->base, suffix);
		g_free (suffix);
		return str;
	} else {
		return suffix;
	}
}

static void
advance_whitespace (TrackerTurtleReader *reader)
{
	while (TRUE) {
		gsize size;
		const gchar *data;
		gchar ch;

		data = g_buffered_input_stream_peek_buffer (reader->buffered_stream, &size);
		if (size == 0)
			break;

		ch = data[0];
		if (!(WS))
			break;

		if (!g_input_stream_skip (G_INPUT_STREAM (reader->buffered_stream),
		                          1, NULL, NULL))
			break;
	}
}

static gboolean
handle_prefix (TrackerTurtleReader  *reader,
               GError              **error)
{
	gchar *prefix = NULL, *uri = NULL;

	advance_whitespace (reader);
	if (!parse_terminal (reader, terminal_PNAME_NS, 0, &prefix))
		goto error;

	advance_whitespace (reader);
	if (!parse_terminal (reader, terminal_IRIREF, 1, &uri))
		goto error;

	advance_whitespace (reader);
	if (!parse_token (reader, "."))
		goto error;

	g_hash_table_insert (reader->prefixes, prefix, uri);
	return TRUE;
error:
	g_free (prefix);
	g_free (uri);
	g_set_error (error,
	             TRACKER_SPARQL_ERROR,
	             TRACKER_SPARQL_ERROR_PARSE,
	             "Could not parse @prefix");
	return FALSE;
}

static gboolean
handle_base (TrackerTurtleReader  *reader,
             GError              **error)
{
	gchar *base = NULL;

	advance_whitespace (reader);
	if (!parse_terminal (reader, terminal_IRIREF, 0, &base))
		goto error;

	advance_whitespace (reader);
	if (!parse_token (reader, "."))
		goto error;

	g_clear_pointer (&reader->base, g_free);
	reader->base = base;
	return TRUE;
error:
	g_free (base);
	g_set_error (error,
	             TRACKER_SPARQL_ERROR,
	             TRACKER_SPARQL_ERROR_PARSE,
	             "Could not parse @base");
	return FALSE;
}

static gboolean
handle_type_cast (TrackerTurtleReader  *reader,
                  GError              **error)
{
	/* These actually go ignored, imposed by the ontology */
	if (parse_token (reader, "^^")) {
		if (parse_terminal (reader, terminal_IRIREF, 1, NULL) ||
		    parse_terminal (reader, terminal_PNAME_LN, 0, NULL) ||
		    parse_terminal (reader, terminal_PNAME_NS, 0, NULL))
			return TRUE;

		g_set_error (error,
		             TRACKER_SPARQL_ERROR,
		             TRACKER_SPARQL_ERROR_PARSE,
		             "Error parsing type cast");
		return FALSE;
	}

	return TRUE;
}

static void
skip_comments (TrackerTurtleReader *reader)
{
	const gchar *buffer, *str;
	gsize size;

	while (TRUE) {
		buffer = g_buffered_input_stream_peek_buffer (reader->buffered_stream,
		                                              &size);
		if (size == 0)
			break;
		if (buffer[0] != '#')
			break;

		str = strchr (buffer, '\n');
		if (!str)
			break;

		if (!g_input_stream_skip (G_INPUT_STREAM (reader->buffered_stream),
		                          str + 1 - buffer, NULL, NULL))
			break;

		advance_whitespace (reader);
	}
}

static gboolean
tracker_turtle_reader_iterate_next (TrackerTurtleReader  *reader,
                                    GError              **error)
{
	while (TRUE) {
		gchar *str;

		advance_whitespace (reader);

		if (g_buffered_input_stream_fill (reader->buffered_stream, -1, NULL, error) < 0)
			return FALSE;

		switch (reader->state) {
		case STATE_INITIAL:
			reader->state = STATE_SUBJECT;
			break;
		case STATE_SUBJECT:
			skip_comments (reader);

			if (g_buffered_input_stream_get_available (reader->buffered_stream) == 0)
				return FALSE;

			if (parse_token (reader, "@prefix")) {
				if (!handle_prefix (reader, error))
					return FALSE;
				break;
			} else if (parse_token (reader, "@base")) {
				if (!handle_base (reader, error))
					return FALSE;
				break;
			}

			g_clear_pointer (&reader->subject, g_free);

			if (parse_token (reader, "[")) {
				/* Anonymous blank node */
				push_stack (reader);
				reader->subject = generate_bnode (reader, NULL);
				reader->state = STATE_PREDICATE;
				continue;
			}

			if (parse_terminal (reader, terminal_IRIREF, 1, &str)) {
				reader->subject = expand_base (reader, str);
			} else if (parse_terminal (reader, terminal_PNAME_LN, 0, &str) ||
			           parse_terminal (reader, terminal_PNAME_NS, 0, &str)) {
				reader->subject = expand_prefix (reader, str);
				g_free (str);
			} else if (parse_terminal (reader, terminal_BLANK_NODE_LABEL, 0, &str)) {
				reader->subject = generate_bnode (reader, str);
				g_free (str);
			} else {
				g_set_error (error,
				             TRACKER_SPARQL_ERROR,
				             TRACKER_SPARQL_ERROR_PARSE,
				             "Wrong subject token");
				return FALSE;
			}

			reader->state = STATE_PREDICATE;
			break;
		case STATE_PREDICATE:
			g_clear_pointer (&reader->predicate, g_free);

			if (parse_token (reader, "a")) {
				reader->predicate = g_strdup (RDF_TYPE);
			} else if (parse_terminal (reader, terminal_IRIREF, 1, &str)) {
				reader->predicate = expand_base (reader, str);
			} else if (parse_terminal (reader, terminal_PNAME_LN, 0, &str) ||
			           parse_terminal (reader, terminal_PNAME_NS, 0, &str)) {
				reader->predicate = expand_prefix (reader, str);
				g_free (str);
			} else {
				g_set_error (error,
				             TRACKER_SPARQL_ERROR,
				             TRACKER_SPARQL_ERROR_PARSE,
				             "Wrong predicate token");
				return FALSE;
			}

			reader->state = STATE_OBJECT;
			break;
		case STATE_OBJECT:
			g_clear_pointer (&reader->object, g_free);
			reader->object_is_uri = FALSE;

			if (parse_token (reader, "[")) {
				/* Anonymous blank node */
				push_stack (reader);
				reader->subject = generate_bnode (reader, NULL);
				reader->state = STATE_PREDICATE;
				continue;
			}

			if (parse_terminal (reader, terminal_IRIREF, 1, &str)) {
				reader->object = expand_base (reader, str);
				reader->object_is_uri = TRUE;
			} else if (parse_terminal (reader, terminal_PNAME_LN, 0, &str) ||
			           parse_terminal (reader, terminal_PNAME_NS, 0, &str)) {
				reader->object = expand_prefix (reader, str);
				reader->object_is_uri = TRUE;
				g_free (str);
			} else if (parse_terminal (reader, terminal_BLANK_NODE_LABEL, 0, &str)) {
				reader->object = generate_bnode (reader, str);
				reader->object_is_uri = TRUE;
				g_free (str);
			} else if (parse_terminal (reader, terminal_STRING_LITERAL1, 1, &str) ||
			           parse_terminal (reader, terminal_STRING_LITERAL2, 1, &str)) {
				reader->object = str;
				if (!handle_type_cast (reader, error))
					return FALSE;
			} else if (parse_terminal (reader, terminal_STRING_LITERAL_LONG1, 3, &str) ||
			           parse_terminal (reader, terminal_STRING_LITERAL_LONG2, 3, &str)) {
				reader->object = str;
				if (!handle_type_cast (reader, error))
					return FALSE;
			} else if (parse_terminal (reader, terminal_DOUBLE, 0, &str) ||
			           parse_terminal (reader, terminal_INTEGER, 0, &str)) {
				reader->object = str;
			} else if (parse_token (reader, "true")) {
				reader->object = g_strdup ("true");
			} else if (parse_token (reader, "false")) {
				reader->object = g_strdup ("false");
			} else {
				g_set_error (error,
				             TRACKER_SPARQL_ERROR,
				             TRACKER_SPARQL_ERROR_PARSE,
				             "Wrong object token");
				return FALSE;
			}

			reader->state = STATE_STEP;

			/* This is where next() stops, on lack of errors */
			return TRUE;
			break;
		case STATE_STEP:
			if (reader->parser_state->len > 0 && parse_token (reader, "]")) {
				pop_stack (reader);
				if (reader->state == STATE_SUBJECT) {
					reader->state = STATE_PREDICATE;
					continue;
				} else if (reader->state == STATE_OBJECT) {
					reader->state = STATE_STEP;
					return TRUE;
				}
			}

			if (parse_token (reader, ",")) {
				reader->state = STATE_OBJECT;
			} else if (parse_token (reader, ";")) {
				/* Dot is allowed after semicolon */
				advance_whitespace (reader);
				if (parse_token (reader, "."))
					reader->state = STATE_SUBJECT;
				else
					reader->state = STATE_PREDICATE;
			} else if (parse_token (reader, ".")) {
				reader->state = STATE_SUBJECT;
			} else {
				g_set_error (error,
				             TRACKER_SPARQL_ERROR,
				             TRACKER_SPARQL_ERROR_PARSE,
				             "Expected comma, semicolon, or dot");
				return FALSE;
			}

			break;
		}
	}
}

gboolean
tracker_turtle_reader_next (TrackerTurtleReader  *reader,
                            const gchar         **subject,
                            const gchar         **predicate,
                            const gchar         **object,
                            gboolean             *object_is_uri,
                            GError              **error)
{
	g_return_val_if_fail (TRACKER_IS_TURTLE_READER (reader), FALSE);
	g_return_val_if_fail (subject, FALSE);
	g_return_val_if_fail (predicate, FALSE);
	g_return_val_if_fail (object, FALSE);
	g_return_val_if_fail (!error || !*error, FALSE);

	if (!tracker_turtle_reader_iterate_next (reader, error))
		return FALSE;

	*subject = reader->subject;
	*predicate = reader->predicate;
	*object = reader->object;
	if (object_is_uri)
		*object_is_uri = reader->object_is_uri;

	return TRUE;
}
