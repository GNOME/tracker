/*
 * Copyright (C) 2008-2010, Nokia
 * Copyright (C) 2017-2018, Carlos Garnacho
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

#include <string.h>

#include "tracker-string-builder.h"

typedef struct _TrackerStringChunk TrackerStringChunk;
typedef struct _TrackerStringElement TrackerStringElement;

struct _TrackerStringChunk
{
	gchar *string;
	gsize allocated_size;
	gsize len;
};

enum {
	ELEM_TYPE_STRING,
	ELEM_TYPE_BUILDER
};

struct _TrackerStringElement
{
	guint type;
	union {
		TrackerStringChunk *chunk;
		TrackerStringBuilder *builder;
	} data;
};

struct _TrackerStringBuilder
{
	GArray *elems;
};

static void
free_string_chunk (TrackerStringChunk *chunk)
{
	g_free (chunk->string);
	g_free (chunk);
}

static void
free_string_element (gpointer data)
{
	TrackerStringElement *elem = data;

	if (elem->type == ELEM_TYPE_STRING)
		free_string_chunk (elem->data.chunk);
	else if (elem->type == ELEM_TYPE_BUILDER)
		tracker_string_builder_free (elem->data.builder);
}

TrackerStringBuilder *
tracker_string_builder_new (void)
{
	TrackerStringBuilder *builder;

	builder = g_slice_new0 (TrackerStringBuilder);
	builder->elems = g_array_new (FALSE, TRUE, sizeof (TrackerStringElement));
	g_array_set_clear_func (builder->elems, free_string_element);

	return builder;
}

void
tracker_string_builder_free (TrackerStringBuilder *builder)
{
	g_array_free (builder->elems, TRUE);
	g_slice_free (TrackerStringBuilder, builder);
}

TrackerStringBuilder *
tracker_string_builder_append_placeholder (TrackerStringBuilder *builder)
{
	TrackerStringBuilder *child;
	TrackerStringElement elem;

	child = tracker_string_builder_new ();

	elem.type = ELEM_TYPE_BUILDER;
	elem.data.builder = child;
	g_array_append_val (builder->elems, elem);

	return child;
}

TrackerStringBuilder *
tracker_string_builder_prepend_placeholder (TrackerStringBuilder *builder)
{
	TrackerStringBuilder *child;
	TrackerStringElement elem;

	child = tracker_string_builder_new ();

	elem.type = ELEM_TYPE_BUILDER;
	elem.data.builder = child;
	g_array_prepend_val (builder->elems, elem);

	return child;
}

static TrackerStringChunk *
ensure_last_chunk (TrackerStringBuilder *builder)
{
	TrackerStringElement elem;
	TrackerStringChunk *chunk;

	if (builder->elems->len > 0) {
		TrackerStringElement *last;

		last = &g_array_index (builder->elems, TrackerStringElement,
		                       builder->elems->len - 1);
		if (last->type == ELEM_TYPE_STRING)
			return last->data.chunk;
	}

	chunk = g_new0 (TrackerStringChunk, 1);

	elem.type = ELEM_TYPE_STRING;
	elem.data.chunk = chunk;
	g_array_append_val (builder->elems, elem);

	return chunk;
}

static TrackerStringChunk *
ensure_first_chunk (TrackerStringBuilder *builder)
{
	TrackerStringElement elem;
	TrackerStringChunk *chunk;

	/* Always create a new element instead of trying to prepend on
	 * the first string chunk. Between memory relocations and memory
	 * fragmentation, we choose the latter. This object is short lived
	 * anyway.
	 */
	chunk = g_new0 (TrackerStringChunk, 1);

	elem.type = ELEM_TYPE_STRING;
	elem.data.chunk = chunk;
	g_array_prepend_val (builder->elems, elem);

	return chunk;
}

static inline gsize
fitting_power_of_two (gsize string_len)
{
	gsize s = 1;

	while (s <= string_len)
		s <<= 1;

	return s;
}

static void
string_chunk_append (TrackerStringChunk *chunk,
                     const gchar        *str,
                     gssize              len)
{
	if (len < 0)
		len = strlen (str);

	if (chunk->len + len > chunk->allocated_size) {
		/* Expand size */
		gssize new_size = fitting_power_of_two (chunk->len + len);

		g_assert (new_size > chunk->allocated_size);
		chunk->string = g_realloc (chunk->string, new_size);
		chunk->allocated_size = new_size;
	}

	/* String (now) fits in allocated size */
	strncpy (&chunk->string[chunk->len], str, len);
	chunk->len += len;
	g_assert (chunk->len <= chunk->allocated_size);
}

void
tracker_string_builder_append (TrackerStringBuilder *builder,
                               const gchar          *string,
                               gssize                len)
{
	TrackerStringChunk *chunk;

	chunk = ensure_last_chunk (builder);
	string_chunk_append (chunk, string, len);
}

void
tracker_string_builder_prepend (TrackerStringBuilder *builder,
                                const gchar          *string,
                                gssize                len)
{
	TrackerStringChunk *chunk;

	chunk = ensure_first_chunk (builder);
	string_chunk_append (chunk, string, len);
}

void
tracker_string_builder_append_valist (TrackerStringBuilder *builder,
                                      const gchar          *format,
                                      va_list               args)
{
	TrackerStringChunk *chunk;
	gchar *str;

	str = g_strdup_vprintf (format, args);

	chunk = ensure_last_chunk (builder);
	string_chunk_append (chunk, str, -1);
	g_free (str);
}

void
tracker_string_builder_prepend_valist (TrackerStringBuilder *builder,
                                       const gchar          *format,
                                       va_list               args)
{
	TrackerStringChunk *chunk;
	gchar *str;

	str = g_strdup_vprintf (format, args);

	chunk = ensure_first_chunk (builder);
	string_chunk_append (chunk, str, -1);
	g_free (str);
}

void
tracker_string_builder_append_printf (TrackerStringBuilder *builder,
                                      const gchar          *format,
                                      ...)
{
	va_list varargs;

	va_start (varargs, format);
	tracker_string_builder_append_valist (builder, format, varargs);
	va_end (varargs);
}

void
tracker_string_builder_prepend_printf (TrackerStringBuilder *builder,
                                       const gchar          *format,
                                       ...)
{
	va_list varargs;

	va_start (varargs, format);
	tracker_string_builder_prepend_valist (builder, format, varargs);
	va_end (varargs);
}

static void
tracker_string_builder_to_gstring (TrackerStringBuilder *builder,
                                   GString              *str)
{
	guint i;

	for (i = 0; i < builder->elems->len; i++) {
		TrackerStringElement *elem;

		elem = &g_array_index (builder->elems, TrackerStringElement, i);

		if (elem->type == ELEM_TYPE_STRING) {
			g_string_append_len (str,
			                     elem->data.chunk->string,
			                     elem->data.chunk->len);
		} else if (elem->type == ELEM_TYPE_BUILDER) {
			tracker_string_builder_to_gstring (elem->data.builder,
			                                   str);
		}
	}
}

gchar *
tracker_string_builder_to_string (TrackerStringBuilder *builder)
{
	GString *str = g_string_new (NULL);

	tracker_string_builder_to_gstring (builder, str);

	return g_string_free (str, FALSE);
}

gboolean
tracker_string_builder_is_empty (TrackerStringBuilder *builder)
{
	return builder->elems->len == 0;
}
