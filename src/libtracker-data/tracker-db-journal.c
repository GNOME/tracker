/*
 * Copyright (C) 2009, Nokia <ivan.frade@nokia.com>
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
 * Author: Philip Van Hoof <philip@codeminded.be>
 */

#include "config.h"

#include <unistd.h>
#include <sys/types.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>

#include <glib/gstdio.h>

#ifndef O_LARGEFILE
# define O_LARGEFILE 0
#endif

#include "tracker-crc32.h"
#include "tracker-db-journal.h"

#ifndef DISABLE_JOURNAL

#define MIN_BLOCK_SIZE    1024

/*
 * data_format:
 * #... 0000 0000 (total size is 4 bytes)
 *         | |||`- resource insert (all other bits must be 0 if 1)
 *         | ||`-- object type (1 = id, 0 = cstring)
 *         | |`--- operation type (0 = insert, 1 = delete)
 *         | `---- graph (0 = default graph, 1 = named graph)
 *         `------ update (0 = insert, 1 = update)
 */

typedef enum {
	DATA_FORMAT_RESOURCE_INSERT  = 1 << 0,
	DATA_FORMAT_OBJECT_ID        = 1 << 1,
	DATA_FORMAT_OPERATION_DELETE = 1 << 2,
	DATA_FORMAT_GRAPH            = 1 << 3,
	DATA_FORMAT_OPERATION_UPDATE = 1 << 4
} DataFormat;

typedef enum {
	TRANSACTION_FORMAT_NONE      = 0,
	TRANSACTION_FORMAT_DATA      = 1 << 0,
	TRANSACTION_FORMAT_ONTOLOGY  = 1 << 1,
} TransactionFormat;

struct _TrackerDBJournalReader {
	gchar *filename;
	GFile *journal_location;
	GDataInputStream *stream;
	GInputStream *underlying_stream;
	GFileInfo *underlying_stream_info;
	GMappedFile *file;
	const gchar *current;
	const gchar *end;
	const gchar *entry_begin;
	const gchar *entry_end;
	const gchar *last_success;
	const gchar *start;
	guint32 amount_of_triples;
	gint64 time;
	TrackerDBJournalEntryType type;
	gchar *uri;
	gint g_id;
	gint s_id;
	gint p_id;
	gint o_id;
	gchar *object;
	guint current_file;
	guint total_chunks;
};

struct _TrackerDBJournal {
	gchar *journal_filename;
	GFile *journal_location;
	int journal;
	gsize cur_size;
	guint cur_block_len;
	guint cur_block_alloc;
	gchar *cur_block;
	guint cur_entry_amount;
	guint cur_pos;

	TransactionFormat transaction_format;
	gboolean in_transaction;
	gint cur_journal_file;
};

static struct {
	gsize chunk_size;
	gboolean do_rotating;
	gchar *rotate_to;
	gboolean rotate_progress_flag;
} rotating_settings = {0};

static gboolean tracker_db_journal_rotate (TrackerDBJournal  *jwriter,
                                           GError           **error);

#ifndef HAVE_STRNLEN

size_t
strnlen (const char *str, size_t max)
{
	const char *end = memchr (str, 0, max);
	return end ? (size_t)(end - str) : max;
}

#endif /* HAVE_STRNLEN */

static gboolean
journal_eof (TrackerDBJournalReader *jreader)
{
	if (jreader->stream) {
		GBufferedInputStream *bstream;

		bstream = G_BUFFERED_INPUT_STREAM (jreader->stream);

		if (g_buffered_input_stream_get_available (bstream) == 0) {
			if (g_buffered_input_stream_fill (bstream, -1, NULL, NULL) == 0) {
				return TRUE;
			}
		}
	} else {
		if (jreader->current >= jreader->end) {
			return TRUE;
		}
	}

	return FALSE;
}

static guint32
read_uint32 (const guint8 *data)
{
	return data[0] << 24 |
	       data[1] << 16 |
	       data[2] << 8 |
	       data[3];
}

static guint32
journal_read_uint32 (TrackerDBJournalReader  *jreader,
                     GError                 **error)
{
	guint32 result;

	if (jreader->stream) {
		result = g_data_input_stream_read_uint32 (jreader->stream, NULL, error);
	} else {
		if (jreader->end - jreader->current < sizeof (guint32)) {
			/* damaged journal entry */
			g_set_error (error, TRACKER_DB_JOURNAL_ERROR,
			             TRACKER_DB_JOURNAL_ERROR_DAMAGED_JOURNAL_ENTRY,
			             "Damaged journal entry, %d < sizeof(guint32)",
			             (gint) (jreader->end - jreader->current));
			return 0;
		}

		result = read_uint32 (jreader->current);
		jreader->current += 4;
	}

	return result;
}

/* based on GDataInputStream code */
static gssize
scan_for_nul (GBufferedInputStream *stream,
              gsize                *checked_out)
{
	const gchar *buffer;
	gsize start, end, peeked;
	gint i;
	gsize available, checked;

	checked = *checked_out;

	start = checked;
	buffer = (const gchar *) g_buffered_input_stream_peek_buffer (stream, &available) + start;
	end = available;
	peeked = end - start;

	for (i = 0; checked < available && i < peeked; i++) {
		if (buffer[i] == '\0') {
			return (start + i);
		}
	}

	checked = end;

	*checked_out = checked;
	return -1;
}

static gchar *
journal_read_string (TrackerDBJournalReader  *jreader,
                     GError                 **error)
{
	gchar *result;

	if (jreader->stream) {
		/* based on GDataInputStream code */

		GBufferedInputStream *bstream;
		gsize checked;
		gssize found_pos;

		bstream = G_BUFFERED_INPUT_STREAM (jreader->stream);

		checked = 0;

		while ((found_pos = scan_for_nul (bstream, &checked)) == -1) {
			if (g_buffered_input_stream_get_available (bstream) == g_buffered_input_stream_get_buffer_size (bstream)) {
				g_buffered_input_stream_set_buffer_size (bstream, 2 * g_buffered_input_stream_get_buffer_size (bstream));
			}

			if (g_buffered_input_stream_fill (bstream, -1, NULL, error) <= 0) {
				/* error or end of stream */
				g_set_error (error, TRACKER_DB_JOURNAL_ERROR,
				             TRACKER_DB_JOURNAL_ERROR_DAMAGED_JOURNAL_ENTRY,
				             "Damaged journal entry, no terminating zero found");
				return NULL;
			}
		}

		result = g_malloc (found_pos + 1);

		if (g_input_stream_read (G_INPUT_STREAM (bstream), result, found_pos + 1, NULL, error) < 0)
			return NULL;
	} else {
		gsize str_length;

		str_length = strnlen (jreader->current, jreader->end - jreader->current);
		if (str_length == jreader->end - jreader->current) {
			/* damaged journal entry (no terminating '\0' character) */
			g_set_error (error, TRACKER_DB_JOURNAL_ERROR,
			             TRACKER_DB_JOURNAL_ERROR_DAMAGED_JOURNAL_ENTRY,
			             "Damaged journal entry, no terminating zero found");
			return NULL;

		}

		result = g_strdup (jreader->current);

		jreader->current += str_length + 1;
	}

	if (!g_utf8_validate (result, -1, NULL)) {
		/* damaged journal entry (invalid UTF-8) */
		g_set_error (error, TRACKER_DB_JOURNAL_ERROR,
		             TRACKER_DB_JOURNAL_ERROR_DAMAGED_JOURNAL_ENTRY,
		             "Damaged journal entry, invalid UTF-8");
		g_free (result);
		return NULL;
	}

	return result;
}

static gboolean
journal_verify_header (TrackerDBJournalReader *jreader)
{
	gchar header[8];
	gint i;
	GError *error = NULL;

	/* Version 00003 is identical, it just has no UPDATE operations */

	if (jreader->stream) {
		for (i = 0; i < sizeof (header); i++) {
			header[i] = g_data_input_stream_read_byte (jreader->stream, NULL, &error);
			if (error) {
				g_clear_error (&error);
				return FALSE;
			}
		}

		if (memcmp (header, "trlog\00004", 8) && memcmp (header, "trlog\00003", 8)) {
			return FALSE;
		}
	} else {
		/* verify journal file header */
		if (jreader->end - jreader->current < 8) {
			return FALSE;
		}

		if (memcmp (jreader->current, "trlog\00004", 8) && memcmp (jreader->current, "trlog\00003", 8)) {
			return FALSE;
		}

		jreader->current += 8;
	}

	return TRUE;
}

void
tracker_db_journal_get_rotating (gboolean *do_rotating,
                                 gsize    *chunk_size,
                                 gchar   **rotate_to)
{
	*do_rotating = rotating_settings.do_rotating;
	*chunk_size = rotating_settings.chunk_size;
	if (rotating_settings.rotate_to) {
		*rotate_to = g_strdup (rotating_settings.rotate_to);
	} else {
		*rotate_to = NULL;
	}
}

void
tracker_db_journal_set_rotating (gboolean     do_rotating,
                                 gsize        chunk_size,
                                 const gchar *rotate_to)
{
	rotating_settings.do_rotating = do_rotating;
	rotating_settings.chunk_size = chunk_size;
	g_free (rotating_settings.rotate_to);
	if (rotate_to) {
		rotating_settings.rotate_to = g_strdup (rotate_to);
	} else {
		rotating_settings.rotate_to = NULL;
	}
}

static gint
nearest_pow (gint num)
{
	gint n = 1;
	while (n < num)
		n <<= 1;
	return n;
}

static void
cur_block_maybe_expand (TrackerDBJournal *jwriter,
                        guint             len)
{
	guint want_alloc = jwriter->cur_block_len + len;

	if (want_alloc > jwriter->cur_block_alloc) {
		want_alloc = nearest_pow (want_alloc);
		want_alloc = MAX (want_alloc, MIN_BLOCK_SIZE);
		jwriter->cur_block = g_realloc (jwriter->cur_block, want_alloc);
		jwriter->cur_block_alloc = want_alloc;
	}
}

static void
cur_block_kill (TrackerDBJournal *jwriter)
{
	jwriter->cur_block_len = 0;
	jwriter->cur_pos = 0;
	jwriter->cur_entry_amount = 0;
	jwriter->cur_block_alloc = 0;

	g_free (jwriter->cur_block);
	jwriter->cur_block = NULL;
}

static void
cur_setnum (gchar   *dest,
            guint   *pos,
            guint32  val)
{
	memset (dest + (*pos)++, val >> 24 & 0xff, 1);
	memset (dest + (*pos)++, val >> 16 & 0xff, 1);
	memset (dest + (*pos)++, val >>  8 & 0xff, 1);
	memset (dest + (*pos)++, val >>  0 & 0xff, 1);
}

static void
cur_setstr (gchar       *dest,
            guint       *pos,
            const gchar *str,
            gsize        len)
{
	memcpy (dest + *pos, str, len);
	(*pos) += len;
	memset (dest + (*pos)++, 0 & 0xff, 1);
}

static gboolean
write_all_data (int      fd,
                gchar   *data,
                gsize    len,
                GError **error)
{
	gssize written;

	while (len > 0) {
		written = write (fd, data, len);

		if (written < 0) {
			gint err = errno;

			if (err == EINTR) {
				/* interrupted by signal, try again */
				continue;
			}

			g_set_error (error, TRACKER_DB_JOURNAL_ERROR,
			             TRACKER_DB_JOURNAL_ERROR_COULD_NOT_WRITE,
			             "Could not write to journal file, %s",
			             g_strerror (err));

			return FALSE;
		} else if (written == 0) {
			g_set_error (error, TRACKER_DB_JOURNAL_ERROR,
			             TRACKER_DB_JOURNAL_ERROR_COULD_NOT_WRITE,
			             "Could not write to journal file, write returned 0 without error");

			return FALSE;
		}

		len -= written;
		data += written;
	}

	return TRUE; /* Succeeded! */
}

GQuark
tracker_db_journal_error_quark (void)
{
	return g_quark_from_static_string (TRACKER_DB_JOURNAL_ERROR_DOMAIN);
}

static gboolean
db_journal_init_file (TrackerDBJournal  *jwriter,
                      gboolean           truncate,
                      GError           **error)
{
	struct stat st;
	int flags;
	int mode;

	jwriter->cur_block_len = 0;
	jwriter->cur_pos = 0;
	jwriter->cur_entry_amount = 0;
	jwriter->cur_block_alloc = 0;
	jwriter->cur_block = NULL;

	mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP;
	flags = O_WRONLY | O_APPEND | O_CREAT | O_LARGEFILE;
	if (truncate) {
		/* existing journal contents are invalid: reindex where journal
		 * does not even contain a single valid entry
		 *
		 * or should be ignored: only for test cases
		 */
		flags |= O_TRUNC;
	}

	jwriter->journal = g_open (jwriter->journal_filename, flags, mode);

	if (jwriter->journal == -1) {
		g_set_error (error, TRACKER_DB_JOURNAL_ERROR,
		             TRACKER_DB_JOURNAL_ERROR_COULD_NOT_WRITE,
		             "Could not open journal for writing, %s",
		             g_strerror (errno));
		return FALSE;
	}

	if (fstat (jwriter->journal, &st) == 0) {
		jwriter->cur_size = (gsize) st.st_size;
	}

	if (jwriter->cur_size == 0) {
		g_assert (jwriter->cur_block_len == 0);
		g_assert (jwriter->cur_block_alloc == 0);
		g_assert (jwriter->cur_block == NULL);

		cur_block_maybe_expand (jwriter, 8);

		jwriter->cur_block[0] = 't';
		jwriter->cur_block[1] = 'r';
		jwriter->cur_block[2] = 'l';
		jwriter->cur_block[3] = 'o';
		jwriter->cur_block[4] = 'g';
		jwriter->cur_block[5] = '\0';
		jwriter->cur_block[6] = '0';
		jwriter->cur_block[7] = '4';

		if (!write_all_data (jwriter->journal, jwriter->cur_block, 8, error)) {
			cur_block_kill (jwriter);
			/* delete empty journal file */
			g_unlink (jwriter->journal_filename);
			close (jwriter->journal);
			jwriter->journal = 0;
			return FALSE;
		}

		jwriter->cur_size += 8;
		cur_block_kill (jwriter);
	}

	return TRUE;
}

static gboolean
db_journal_writer_init (TrackerDBJournal  *jwriter,
                        gboolean           truncate,
                        gboolean           global_writer,
                        const gchar       *filename,
                        GFile             *journal_location,
                        GError           **error)
{
	gchar *directory;
	gint mode;
	GError *n_error = NULL;
	gboolean ret;

	directory = g_path_get_dirname (filename);
	if (g_strcmp0 (directory, ".")) {
		mode = S_IRWXU | S_IRWXG | S_IRWXO;
		if (g_mkdir_with_parents (directory, mode)) {

			g_set_error (error, TRACKER_DB_JOURNAL_ERROR,
			             TRACKER_DB_JOURNAL_ERROR_COULD_NOT_WRITE,
			             "tracker data directory does not exist and "
			             "could not be created: %s",
			             g_strerror (errno));
			g_free (directory);

			return FALSE;
		}
	}
	g_free (directory);

	jwriter->journal_filename = g_strdup (filename);
	g_set_object (&jwriter->journal_location, journal_location);

	ret = db_journal_init_file (jwriter, truncate, &n_error);

	if (n_error) {
		g_propagate_error (error, n_error);
		g_free (jwriter->journal_filename);
		jwriter->journal_filename = NULL;
	}

	return ret;
}

TrackerDBJournal *
tracker_db_journal_new (GFile        *data_location,
                        gboolean      truncate,
                        GError      **error)
{
	TrackerDBJournal *writer;
	gboolean ret;
	gchar *filename;
	GError *n_error = NULL;
	GFile *child;

	writer = g_new0 (TrackerDBJournal, 1);
	writer->transaction_format = TRANSACTION_FORMAT_DATA;

	child = g_file_get_child (data_location, TRACKER_DB_JOURNAL_FILENAME);
	filename = g_file_get_path (child);
	g_object_unref (child);

	g_assert (filename != NULL);

	ret = db_journal_writer_init (writer, truncate, TRUE, filename, data_location, &n_error);
	g_free (filename);

	if (!ret) {
		g_propagate_error (error, n_error);
		g_clear_pointer (&writer, g_free);
	}

	return writer;
}

TrackerDBJournal *
tracker_db_journal_ontology_new (GFile     *data_location,
                                 GError   **error)
{
	TrackerDBJournal *writer;
	gboolean ret;
	gchar *filename;
	GError *n_error = NULL;
	GFile *child;

	writer = g_new0 (TrackerDBJournal, 1);
	writer->transaction_format = TRANSACTION_FORMAT_ONTOLOGY;

	child = g_file_get_child (data_location, TRACKER_DB_JOURNAL_ONTOLOGY_FILENAME);
	filename = g_file_get_path (child);
	g_object_unref (child);

	g_assert (filename != NULL);

	ret = db_journal_writer_init (writer, FALSE, FALSE, filename, data_location, &n_error);
	g_free (filename);

	if (!ret) {
		g_propagate_error (error, n_error);
		g_clear_pointer (&writer, g_free);
	}

	return writer;
}

static gboolean
db_journal_writer_clear (TrackerDBJournal  *jwriter,
                         GError           **error)
{
	g_clear_pointer (&jwriter->journal_filename, g_free);
	g_clear_object (&jwriter->journal_location);

	if (jwriter->journal == 0) {
		return TRUE;
	}

	if (close (jwriter->journal) != 0) {
		g_set_error (error, TRACKER_DB_JOURNAL_ERROR,
		             TRACKER_DB_JOURNAL_ERROR_COULD_NOT_CLOSE,
		             "Could not close journal, %s",
		             g_strerror (errno));
		return FALSE;
	}

	jwriter->journal = 0;

	return TRUE;
}

gboolean
tracker_db_journal_free (TrackerDBJournal  *writer,
                         GError           **error)
{
	GError *n_error = NULL;

	db_journal_writer_clear (writer, &n_error);
	g_free (writer);

	if (n_error) {
		g_propagate_error (error, n_error);
		return FALSE;
	}

	return TRUE;
}

gsize
tracker_db_journal_get_size (TrackerDBJournal *writer)
{
	g_return_val_if_fail (writer->journal > 0, FALSE);

	return writer->cur_size;
}

gboolean
tracker_db_journal_start_transaction (TrackerDBJournal *jwriter,
                                      time_t            time)
{
	guint size;

	g_return_val_if_fail (jwriter->journal > 0, FALSE);
	g_return_val_if_fail (jwriter->in_transaction == FALSE, FALSE);

	jwriter->in_transaction = TRUE;

	size = sizeof (guint32) * 3;
	cur_block_maybe_expand (jwriter, size);

	/* Leave space for size, amount and crc
	 * Check and keep in sync the offset variable at
	 * tracker_db_journal_commit_db_transaction too */

	memset (jwriter->cur_block, 0, size);

	jwriter->cur_pos = jwriter->cur_block_len = size;
	jwriter->cur_entry_amount = 0;

	/* add timestamp */
	cur_block_maybe_expand (jwriter, sizeof (gint32));
	cur_setnum (jwriter->cur_block, &(jwriter->cur_pos), time);
	jwriter->cur_block_len += sizeof (gint32);

	/* Add format */
	cur_block_maybe_expand (jwriter, sizeof (gint32));
	cur_setnum (jwriter->cur_block, &(jwriter->cur_pos), jwriter->transaction_format);
	jwriter->cur_block_len += sizeof (gint32);

	return TRUE;
}

gboolean
tracker_db_journal_append_delete_statement (TrackerDBJournal *jwriter,
                                            gint              g_id,
                                            gint              s_id,
                                            gint              p_id,
                                            const gchar      *object)
{
	gint o_len;
	DataFormat df;
	gint size;

	g_return_val_if_fail (jwriter->journal > 0, FALSE);
	g_return_val_if_fail (g_id >= 0, FALSE);
	g_return_val_if_fail (s_id > 0, FALSE);
	g_return_val_if_fail (p_id > 0, FALSE);
	g_return_val_if_fail (object != NULL, FALSE);
	g_return_val_if_fail (jwriter->in_transaction == TRUE, FALSE);

	if (jwriter->transaction_format == TRANSACTION_FORMAT_ONTOLOGY) {
		return TRUE;
	}

	o_len = strlen (object);
	if (g_id == 0) {
		df = DATA_FORMAT_OPERATION_DELETE;
		size = (sizeof (guint32) * 3) + o_len + 1;
	} else {
		df = DATA_FORMAT_OPERATION_DELETE | DATA_FORMAT_GRAPH;
		size = (sizeof (guint32) * 4) + o_len + 1;
	}

	cur_block_maybe_expand (jwriter, size);

	cur_setnum (jwriter->cur_block, &(jwriter->cur_pos), df);
	if (g_id > 0) {
		cur_setnum (jwriter->cur_block, &(jwriter->cur_pos), g_id);
	}
	cur_setnum (jwriter->cur_block, &(jwriter->cur_pos), s_id);
	cur_setnum (jwriter->cur_block, &(jwriter->cur_pos), p_id);
	cur_setstr (jwriter->cur_block, &(jwriter->cur_pos), object, o_len);

	jwriter->cur_entry_amount++;
	jwriter->cur_block_len += size;

	return TRUE;
}

gboolean
tracker_db_journal_append_delete_statement_id  (TrackerDBJournal *jwriter,
                                                gint              g_id,
                                                gint              s_id,
                                                gint              p_id,
                                                gint              o_id)
{
	DataFormat df;
	gint size;

	g_return_val_if_fail (jwriter->journal > 0, FALSE);
	g_return_val_if_fail (g_id >= 0, FALSE);
	g_return_val_if_fail (s_id > 0, FALSE);
	g_return_val_if_fail (p_id > 0, FALSE);
	g_return_val_if_fail (o_id > 0, FALSE);
	g_return_val_if_fail (jwriter->in_transaction == TRUE, FALSE);

	if (jwriter->transaction_format == TRANSACTION_FORMAT_ONTOLOGY) {
		return TRUE;
	}

	if (g_id == 0) {
		df = DATA_FORMAT_OPERATION_DELETE | DATA_FORMAT_OBJECT_ID;
		size = sizeof (guint32) * 4;
	} else {
		df = DATA_FORMAT_OPERATION_DELETE | DATA_FORMAT_OBJECT_ID | DATA_FORMAT_GRAPH;
		size = sizeof (guint32) * 5;
	}

	cur_block_maybe_expand (jwriter, size);

	cur_setnum (jwriter->cur_block, &(jwriter->cur_pos), df);
	if (g_id > 0) {
		cur_setnum (jwriter->cur_block, &(jwriter->cur_pos), g_id);
	}
	cur_setnum (jwriter->cur_block, &(jwriter->cur_pos), s_id);
	cur_setnum (jwriter->cur_block, &(jwriter->cur_pos), p_id);
	cur_setnum (jwriter->cur_block, &(jwriter->cur_pos), o_id);

	jwriter->cur_entry_amount++;
	jwriter->cur_block_len += size;

	return TRUE;
}

gboolean
tracker_db_journal_append_insert_statement (TrackerDBJournal *jwriter,
                                            gint              g_id,
                                            gint              s_id,
                                            gint              p_id,
                                            const gchar      *object)
{
	gint o_len;
	DataFormat df;
	gint size;

	g_return_val_if_fail (jwriter->journal > 0, FALSE);
	g_return_val_if_fail (g_id >= 0, FALSE);
	g_return_val_if_fail (s_id > 0, FALSE);
	g_return_val_if_fail (p_id > 0, FALSE);
	g_return_val_if_fail (object != NULL, FALSE);
	g_return_val_if_fail (jwriter->in_transaction == TRUE, FALSE);

	if (jwriter->transaction_format == TRANSACTION_FORMAT_ONTOLOGY) {
		return TRUE;
	}

	o_len = strlen (object);
	if (g_id == 0) {
		df = 0x00;
		size = (sizeof (guint32) * 3) + o_len + 1;
	} else {
		df = DATA_FORMAT_GRAPH;
		size = (sizeof (guint32) * 4) + o_len + 1;
	}

	cur_block_maybe_expand (jwriter, size);

	cur_setnum (jwriter->cur_block, &(jwriter->cur_pos), df);
	if (g_id > 0) {
		cur_setnum (jwriter->cur_block, &(jwriter->cur_pos), g_id);
	}
	cur_setnum (jwriter->cur_block, &(jwriter->cur_pos), s_id);
	cur_setnum (jwriter->cur_block, &(jwriter->cur_pos), p_id);
	cur_setstr (jwriter->cur_block, &(jwriter->cur_pos), object, o_len);

	jwriter->cur_entry_amount++;
	jwriter->cur_block_len += size;

	return TRUE;
}

gboolean
tracker_db_journal_append_insert_statement_id (TrackerDBJournal *jwriter,
                                               gint              g_id,
                                               gint              s_id,
                                               gint              p_id,
                                               gint              o_id)
{
	DataFormat df;
	gint size;

	g_return_val_if_fail (jwriter->journal > 0, FALSE);
	g_return_val_if_fail (g_id >= 0, FALSE);
	g_return_val_if_fail (s_id > 0, FALSE);
	g_return_val_if_fail (p_id > 0, FALSE);
	g_return_val_if_fail (o_id > 0, FALSE);
	g_return_val_if_fail (jwriter->in_transaction == TRUE, FALSE);

	if (jwriter->transaction_format == TRANSACTION_FORMAT_ONTOLOGY) {
		return TRUE;
	}

	if (g_id == 0) {
		df = DATA_FORMAT_OBJECT_ID;
		size = sizeof (guint32) * 4;
	} else {
		df = DATA_FORMAT_OBJECT_ID | DATA_FORMAT_GRAPH;
		size = sizeof (guint32) * 5;
	}

	cur_block_maybe_expand (jwriter, size);

	cur_setnum (jwriter->cur_block, &(jwriter->cur_pos), df);
	if (g_id > 0) {
		cur_setnum (jwriter->cur_block, &(jwriter->cur_pos), g_id);
	}
	cur_setnum (jwriter->cur_block, &(jwriter->cur_pos), s_id);
	cur_setnum (jwriter->cur_block, &(jwriter->cur_pos), p_id);
	cur_setnum (jwriter->cur_block, &(jwriter->cur_pos), o_id);

	jwriter->cur_entry_amount++;
	jwriter->cur_block_len += size;

	return TRUE;
}

gboolean
tracker_db_journal_append_update_statement (TrackerDBJournal *jwriter,
                                            gint              g_id,
                                            gint              s_id,
                                            gint              p_id,
                                            const gchar      *object)
{
	gint o_len;
	DataFormat df;
	gint size;

	g_return_val_if_fail (jwriter->journal > 0, FALSE);
	g_return_val_if_fail (g_id >= 0, FALSE);
	g_return_val_if_fail (s_id > 0, FALSE);
	g_return_val_if_fail (p_id > 0, FALSE);
	g_return_val_if_fail (object != NULL, FALSE);
	g_return_val_if_fail (jwriter->in_transaction == TRUE, FALSE);

	if (jwriter->transaction_format == TRANSACTION_FORMAT_ONTOLOGY) {
		return TRUE;
	}

	o_len = strlen (object);
	if (g_id == 0) {
		df = DATA_FORMAT_OPERATION_UPDATE;
		size = (sizeof (guint32) * 3) + o_len + 1;
	} else {
		df = DATA_FORMAT_OPERATION_UPDATE | DATA_FORMAT_GRAPH;
		size = (sizeof (guint32) * 4) + o_len + 1;
	}

	cur_block_maybe_expand (jwriter, size);

	cur_setnum (jwriter->cur_block, &(jwriter->cur_pos), df);
	if (g_id > 0) {
		cur_setnum (jwriter->cur_block, &(jwriter->cur_pos), g_id);
	}
	cur_setnum (jwriter->cur_block, &(jwriter->cur_pos), s_id);
	cur_setnum (jwriter->cur_block, &(jwriter->cur_pos), p_id);
	cur_setstr (jwriter->cur_block, &(jwriter->cur_pos), object, o_len);

	jwriter->cur_entry_amount++;
	jwriter->cur_block_len += size;

	return TRUE;
}

gboolean
tracker_db_journal_append_update_statement_id (TrackerDBJournal *jwriter,
                                               gint              g_id,
                                               gint              s_id,
                                               gint              p_id,
                                               gint              o_id)
{
	DataFormat df;
	gint size;

	g_return_val_if_fail (jwriter->journal > 0, FALSE);
	g_return_val_if_fail (g_id >= 0, FALSE);
	g_return_val_if_fail (s_id > 0, FALSE);
	g_return_val_if_fail (p_id > 0, FALSE);
	g_return_val_if_fail (o_id > 0, FALSE);
	g_return_val_if_fail (jwriter->in_transaction == TRUE, FALSE);

	if (jwriter->transaction_format == TRANSACTION_FORMAT_ONTOLOGY) {
		return TRUE;
	}

	if (g_id == 0) {
		df = DATA_FORMAT_OPERATION_UPDATE | DATA_FORMAT_OBJECT_ID;
		size = sizeof (guint32) * 4;
	} else {
		df = DATA_FORMAT_OPERATION_UPDATE | DATA_FORMAT_OBJECT_ID | DATA_FORMAT_GRAPH;
		size = sizeof (guint32) * 5;
	}

	cur_block_maybe_expand (jwriter, size);

	cur_setnum (jwriter->cur_block, &(jwriter->cur_pos), df);
	if (g_id > 0) {
		cur_setnum (jwriter->cur_block, &(jwriter->cur_pos), g_id);
	}
	cur_setnum (jwriter->cur_block, &(jwriter->cur_pos), s_id);
	cur_setnum (jwriter->cur_block, &(jwriter->cur_pos), p_id);
	cur_setnum (jwriter->cur_block, &(jwriter->cur_pos), o_id);

	jwriter->cur_entry_amount++;
	jwriter->cur_block_len += size;

	return TRUE;
}

gboolean
tracker_db_journal_append_resource (TrackerDBJournal *jwriter,
                                    gint              s_id,
                                    const gchar      *uri)
{
	gint o_len;
	DataFormat df;
	gint size;

	g_return_val_if_fail (jwriter->journal > 0, FALSE);

	o_len = strlen (uri);
	df = DATA_FORMAT_RESOURCE_INSERT;
	size = (sizeof (guint32) * 2) + o_len + 1;

	cur_block_maybe_expand (jwriter, size);

	cur_setnum (jwriter->cur_block, &(jwriter->cur_pos), df);
	cur_setnum (jwriter->cur_block, &(jwriter->cur_pos), s_id);
	cur_setstr (jwriter->cur_block, &(jwriter->cur_pos), uri, o_len);

	jwriter->cur_entry_amount++;
	jwriter->cur_block_len += size;

	return TRUE;
}

gboolean
tracker_db_journal_rollback_transaction (TrackerDBJournal *writer)
{
	g_return_val_if_fail (writer->journal > 0, FALSE);
	g_return_val_if_fail (writer->in_transaction == TRUE, FALSE);

	cur_block_kill (writer);
	writer->in_transaction = FALSE;

	return TRUE;
}

gboolean
tracker_db_journal_truncate (TrackerDBJournal *writer,
                             gsize             new_size)
{
	g_return_val_if_fail (writer->journal > 0, FALSE);

	return (ftruncate (writer->journal, new_size) != -1);
}

static gboolean
db_journal_writer_commit_db_transaction (TrackerDBJournal  *jwriter,
                                         GError           **error)
{
	guint32 crc;
	guint begin_pos;
	guint size;
	guint offset;

	g_return_val_if_fail (jwriter->journal > 0, FALSE);

	begin_pos = 0;
	size = sizeof (guint32);
	offset = sizeof (guint32) * 3;

	/* Expand by uint32 for the size check at the end of the entry */
	cur_block_maybe_expand (jwriter, size);

	jwriter->cur_block_len += size;

	/* Write size and amount */
	cur_setnum (jwriter->cur_block, &begin_pos, jwriter->cur_block_len);
	cur_setnum (jwriter->cur_block, &begin_pos, jwriter->cur_entry_amount);

	/* Write size check to end of current journal data */
	cur_setnum (jwriter->cur_block, &(jwriter->cur_pos), jwriter->cur_block_len);

	/* Calculate CRC from entry triples start (i.e. without size,
	 * amount and crc) until the end of the entry block.
	 *
	 * NOTE: the size check at the end is included in the CRC!
	 */
	crc = tracker_crc32 (jwriter->cur_block + offset, jwriter->cur_block_len - offset);
	cur_setnum (jwriter->cur_block, &begin_pos, crc);

	if (!write_all_data (jwriter->journal, jwriter->cur_block, jwriter->cur_block_len, error)) {
		return FALSE;
	}

	/* Update journal size */
	jwriter->cur_size += jwriter->cur_block_len;

	/* Clean up for next transaction */
	cur_block_kill (jwriter);

	return TRUE;
}

gboolean
tracker_db_journal_commit_db_transaction (TrackerDBJournal  *writer,
                                          GError           **error)
{
	gboolean ret;
	GError *n_error = NULL;

	g_return_val_if_fail (writer->in_transaction == TRUE, FALSE);

	ret = db_journal_writer_commit_db_transaction (writer, &n_error);

	if (ret && writer->transaction_format == TRANSACTION_FORMAT_DATA) {
		if (rotating_settings.do_rotating && (writer->cur_size > rotating_settings.chunk_size)) {
			ret = tracker_db_journal_rotate (writer, &n_error);
		}
	}

	if (n_error) {
		g_propagate_error (error, n_error);
	}

	writer->in_transaction = FALSE;

	return ret;
}

gboolean
tracker_db_journal_fsync (TrackerDBJournal *writer)
{
	g_return_val_if_fail (writer->journal > 0, FALSE);

	return fsync (writer->journal) == 0;
}

/*
 * Reader API
 */

static gboolean db_journal_reader_clear (TrackerDBJournalReader *jreader);

static gchar*
reader_get_next_filepath (TrackerDBJournalReader *jreader)
{
	gchar *filename_open = NULL;
	gchar *test;

	test = g_strdup_printf ("%s.%d", jreader->filename, jreader->current_file + 1);

	if (g_file_test (test, G_FILE_TEST_EXISTS)) {
		jreader->current_file++;
		filename_open = test;
	} else {
		gchar *filename;
		GFile *dest_dir, *possible;

		/* This is where chunks are being rotated to */
		if (rotating_settings.rotate_to) {
			dest_dir = g_file_new_for_path (rotating_settings.rotate_to);
		} else {
			GFile *source;

			/* keep compressed journal files in same directory */
			source = g_file_new_for_path (test);
			dest_dir = g_file_get_parent (source);
			g_object_unref (source);
		}

		filename = g_path_get_basename (test);
		g_free (test);
		test = filename;
		filename = g_strconcat (test, ".gz", NULL);
		g_free (test);
		possible = g_file_get_child (dest_dir, filename);
		g_object_unref (dest_dir);
		g_free (filename);

		if (g_file_query_exists (possible, NULL)) {
			jreader->current_file++;
			filename_open = g_file_get_path (possible);
		}
		g_object_unref (possible);
	}

	if (filename_open == NULL) {
		filename_open = g_strdup (jreader->filename);
		/* Last file is the active journal file */
		jreader->current_file = 0;
	}

	return filename_open;
}

static gboolean
db_journal_reader_init_file (TrackerDBJournalReader  *jreader,
                             const gchar             *filename,
                             GError                 **error)
{
	if (g_str_has_suffix (filename, ".gz")) {
		GFile *file;
		GInputStream *stream, *cstream;
		GConverter *converter;

		file = g_file_new_for_path (filename);

		stream = G_INPUT_STREAM (g_file_read (file, NULL, error));
		g_object_unref (file);
		if (!stream) {
			return FALSE;
		}

		jreader->underlying_stream = g_object_ref (stream);

		if (jreader->underlying_stream_info) {
			g_object_unref (jreader->underlying_stream_info);
			jreader->underlying_stream_info = NULL;
		}

		converter = G_CONVERTER (g_zlib_decompressor_new (G_ZLIB_COMPRESSOR_FORMAT_GZIP));
		cstream = g_converter_input_stream_new (stream, converter);
		g_object_unref (stream);
		g_object_unref (converter);

		jreader->stream = g_data_input_stream_new (cstream);
		g_object_unref (cstream);
	} else {
		jreader->file = g_mapped_file_new (filename, FALSE, error);

		if (!jreader->file) {
			return FALSE;
		}

		jreader->last_success = jreader->start = jreader->current =
			g_mapped_file_get_contents (jreader->file);

		jreader->end = jreader->current + g_mapped_file_get_length (jreader->file);
	}

	if (!journal_verify_header (jreader)) {
		g_set_error (error, TRACKER_DB_JOURNAL_ERROR,
		             TRACKER_DB_JOURNAL_ERROR_BEGIN_OF_JOURNAL,
		             "Damaged journal entry at begin of journal");
		return FALSE;
	}

	return TRUE;
}

static gboolean
db_journal_reader_init (TrackerDBJournalReader  *jreader,
                        gboolean                 global_reader,
                        const gchar             *filename,
                        GFile                   *data_location,
                        GError                 **error)
{
	gchar *filename_open;
	GError *n_error = NULL;

	g_return_val_if_fail (jreader->file == NULL, FALSE);

	jreader->filename = g_strdup (filename);
	g_set_object (&jreader->journal_location, data_location);

	jreader->current_file = 0;
	if (global_reader) {
		filename_open = reader_get_next_filepath (jreader);
	} else {
		filename_open = g_strdup (filename);
	}

	jreader->type = TRACKER_DB_JOURNAL_START;

	if (!db_journal_reader_init_file (jreader, filename_open, &n_error)) {
		if (!g_error_matches (n_error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND) &&
		    !g_error_matches (n_error, G_FILE_ERROR, G_FILE_ERROR_NOENT)) {
			/* Do not set error if the file does not exist, just return FALSE */

			g_propagate_prefixed_error (error,
			                            n_error,
			                            "Could not create TrackerDBJournalReader for file '%s', ",
			                            jreader->filename);
		} else {
			g_error_free (n_error);
		}

		g_free (filename_open);

		db_journal_reader_clear (jreader);
		return FALSE;
	}

	g_free (filename_open);

	return TRUE;
}

TrackerDBJournalReader  *
tracker_db_journal_reader_new (GFile   *data_location,
                               GError **error)
{
	TrackerDBJournalReader *reader;
	GError *n_error = NULL;
	gchar *filename;
	GFile *child;

	child = g_file_get_child (data_location, TRACKER_DB_JOURNAL_FILENAME);
	filename = g_file_get_path (child);
	g_object_unref (child);

	reader = g_new0 (TrackerDBJournalReader, 1);

	if (!db_journal_reader_init (reader, TRUE, filename, data_location, &n_error)) {
		if (n_error)
			g_propagate_error (error, n_error);
		g_clear_pointer (&reader, g_free);
	}

	g_free (filename);

	return reader;
}

TrackerDBJournalReader *
tracker_db_journal_reader_ontology_new (GFile   *data_location,
                                        GError **error)
{
	TrackerDBJournalReader *reader;
	gchar *filename;
	GError *n_error = NULL;
	GFile *child;

	child = g_file_get_child (data_location, TRACKER_DB_JOURNAL_ONTOLOGY_FILENAME);
	filename = g_file_get_path (child);
	g_object_unref (child);

	reader = g_new0 (TrackerDBJournalReader, 1);

	if (!db_journal_reader_init (reader, TRUE, filename, data_location, &n_error)) {
		g_propagate_error (error, n_error);
		g_clear_pointer (&reader, g_free);
	}

	g_free (filename);

	return reader;
}

gsize
tracker_db_journal_reader_get_size_of_correct (TrackerDBJournalReader *reader)
{
	g_return_val_if_fail (reader->file != NULL, FALSE);

	return (gsize) (reader->last_success - reader->start);
}

static gboolean
reader_next_file (TrackerDBJournalReader  *reader,
                  GError                 **error)
{
	gchar *filename_open;

	filename_open = reader_get_next_filepath (reader);

	if (reader->stream) {
		g_object_unref (reader->stream);
		reader->stream = NULL;

		g_object_unref (reader->underlying_stream);
		reader->underlying_stream = NULL;
		if (reader->underlying_stream_info) {
			g_object_unref (reader->underlying_stream_info);
			reader->underlying_stream_info = NULL;
		}

	} else {
		g_mapped_file_unref (reader->file);
		reader->file = NULL;
	}

	if (!db_journal_reader_init_file (reader, filename_open, error)) {
		g_free (filename_open);
		return FALSE;
	}

	g_free (filename_open);

	reader->type = TRACKER_DB_JOURNAL_END_TRANSACTION;

	reader->entry_begin = NULL;
	reader->entry_end = NULL;
	reader->amount_of_triples = 0;

	return TRUE;
}

static gboolean
db_journal_reader_clear (TrackerDBJournalReader *jreader)
{
	if (jreader->stream) {
		g_object_unref (jreader->stream);
		jreader->stream = NULL;
		g_object_unref (jreader->underlying_stream);
		jreader->underlying_stream = NULL;
		if (jreader->underlying_stream_info) {
			g_object_unref (jreader->underlying_stream_info);
			jreader->underlying_stream_info = NULL;
		}
	} else if (jreader->file) {
		g_mapped_file_unref (jreader->file);
		jreader->file = NULL;
	}

	g_free (jreader->filename);
	jreader->filename = NULL;

	jreader->last_success = NULL;
	jreader->start = NULL;
	jreader->current = NULL;
	jreader->end = NULL;
	jreader->entry_begin = NULL;
	jreader->entry_end = NULL;
	jreader->amount_of_triples = 0;
	jreader->type = TRACKER_DB_JOURNAL_START;
	jreader->uri = NULL;
	jreader->g_id = 0;
	jreader->s_id = 0;
	jreader->p_id = 0;
	jreader->o_id = 0;
	jreader->object = NULL;

	return TRUE;
}

void
tracker_db_journal_reader_free (TrackerDBJournalReader *reader)
{
	db_journal_reader_clear (reader);
	g_free (reader);
}

TrackerDBJournalEntryType
tracker_db_journal_reader_get_entry_type (TrackerDBJournalReader *reader)
{
	g_return_val_if_fail (reader->file != NULL || reader->stream != NULL, FALSE);

	return reader->type;
}

static gboolean
db_journal_reader_next (TrackerDBJournalReader  *jreader,
                        gboolean                 global_reader,
                        GError                 **error)
{
	GError *inner_error = NULL;
	static gboolean debug_unchecked = TRUE;
	static gboolean slow_down = FALSE;

	g_return_val_if_fail (jreader->file != NULL || jreader->stream != NULL, FALSE);

	/* reset struct */
	g_free (jreader->uri);
	jreader->uri = NULL;
	jreader->g_id = 0;
	jreader->s_id = 0;
	jreader->p_id = 0;
	jreader->o_id = 0;
	g_free (jreader->object);
	jreader->object = NULL;

	/*
	 * Visual layout of the data in the binary journal:
	 *
	 * [
	 *  [magic]
	 *  [version]
	 *  [
	 *   [entry
	 *    [size]
	 *    [amount]
	 *    [crc]
	 *    [time]
	 *    [id id id]
	 *    [id id string]
	 *    [id ...]
	 *    [size]
	 *   ]
	 *   [entry...]
	 *   [entry...]
	 *  ]
	 * ]
	 *
	 * Note: We automatically start at the first entry, upon init
	 * of the reader, we move past the [magic] and the [version].
	 */

	if (jreader->type == TRACKER_DB_JOURNAL_START ||
	    jreader->type == TRACKER_DB_JOURNAL_END_TRANSACTION) {
		/* Expect new transaction or end of file */
		guint32 entry_size;
		guint32 entry_size_check;
		guint32 crc;
		guint32 crc_check;
		TransactionFormat t_kind;


		if (G_UNLIKELY (debug_unchecked)) {
			const gchar *test;

			test = g_getenv ("TRACKER_DEBUG_MAKE_JOURNAL_READER_GO_VERY_SLOW");
			if (g_strcmp0 (test, "yes") == 0) {
				slow_down = TRUE;
			}
			debug_unchecked = FALSE;
		}

		if (G_UNLIKELY (slow_down)) {
			sleep (1);
		}

		/* Check the end is not where we currently are */
		if (journal_eof (jreader)) {
			/* Return FALSE as there is no further entry but
			 * do not set error as it's not an error case. */
			if (global_reader && jreader->current_file != 0) {
				if (reader_next_file (jreader, error)) {
					/* read first entry in next file */
					return db_journal_reader_next (jreader, global_reader, error);
				} else {
					/* no more files */
					return FALSE;
				}
			} else {
				return FALSE;
			}
		}

		jreader->entry_begin = jreader->current;

		/* Read the first uint32 which contains the size */
		entry_size = journal_read_uint32 (jreader, &inner_error);
		if (inner_error) {
			g_propagate_error (error, inner_error);
			return FALSE;
		}

		/* Check that entry is big enough for header and footer */
		if (entry_size < 5 * sizeof (guint32)) {
			g_set_error (error, TRACKER_DB_JOURNAL_ERROR,
			             TRACKER_DB_JOURNAL_ERROR_DAMAGED_JOURNAL_ENTRY,
			             "Damaged journal entry, size %d < 5 * sizeof(guint32)",
			             (gint) entry_size);
			return FALSE;
		}

		/* Check that entry is smaller than the rest of the file.
		   Very large entry_size could otherwise cause an overflow
		   in entry_begin + entry_size below. */
		if ((gint64) entry_size > (gint64) (jreader->end - jreader->entry_begin)) {
			g_set_error (error, TRACKER_DB_JOURNAL_ERROR,
			             TRACKER_DB_JOURNAL_ERROR_DAMAGED_JOURNAL_ENTRY,
			             "Damaged journal entry, size %u > %" G_GINT64_FORMAT " (rest of the file)",
			             entry_size, (gint64)(jreader->end - jreader->entry_begin));
			return FALSE;
		}

		if (!jreader->stream) {
			/* Set the bounds for the entry */
			jreader->entry_end = jreader->entry_begin + entry_size;

			/* Check the end of the entry does not exceed the end
			 * of the journal.
			 */
			if (jreader->end < jreader->entry_end) {
				g_set_error (error, TRACKER_DB_JOURNAL_ERROR,
				             TRACKER_DB_JOURNAL_ERROR_DAMAGED_JOURNAL_ENTRY,
				             "Damaged journal entry, end < entry end");
				return FALSE;
			}

			/* Read entry size check at the end of the entry */
			entry_size_check = read_uint32 (jreader->entry_end - 4);

			if (entry_size != entry_size_check) {
				/* damaged journal entry */
				g_set_error (error, TRACKER_DB_JOURNAL_ERROR,
				             TRACKER_DB_JOURNAL_ERROR_DAMAGED_JOURNAL_ENTRY,
				             "Damaged journal entry, %d != %d (entry size != entry size check)",
				             entry_size,
				             entry_size_check);
				return FALSE;
			}
		}

		/* Read the amount of triples */
		jreader->amount_of_triples = journal_read_uint32 (jreader, &inner_error);
		if (inner_error) {
			g_propagate_error (error, inner_error);
			return FALSE;
		}

		/* Read the crc */
		crc_check = journal_read_uint32 (jreader, &inner_error);
		if (inner_error) {
			g_propagate_error (error, inner_error);
			return FALSE;
		}

		if (!jreader->stream) {
			// Maybe read in whole transaction in one buffer, so we can do CRC even without mmap (when reading compressed journals)
			// might this be too problematic memory-wise

			/* Calculate the crc */
			crc = tracker_crc32 (jreader->entry_begin + (sizeof (guint32) * 3), entry_size - (sizeof (guint32) * 3));

			/* Verify checksum */
			if (crc != crc_check) {
				/* damaged journal entry */
				g_set_error (error, TRACKER_DB_JOURNAL_ERROR,
				             TRACKER_DB_JOURNAL_ERROR_DAMAGED_JOURNAL_ENTRY,
				             "Damaged journal entry, 0x%.8x != 0x%.8x (crc32 failed)",
				             crc,
				             crc_check);
				return FALSE;
			}
		}

		/* Read the timestamp */
		jreader->time = journal_read_uint32 (jreader, &inner_error);
		if (inner_error) {
			g_propagate_error (error, inner_error);
			return FALSE;
		}

		t_kind = journal_read_uint32 (jreader, &inner_error);
		if (inner_error) {
			g_propagate_error (error, inner_error);
			return FALSE;
		}

		if (t_kind == TRANSACTION_FORMAT_DATA)
			jreader->type = TRACKER_DB_JOURNAL_START_TRANSACTION;
		else
			jreader->type = TRACKER_DB_JOURNAL_START_ONTOLOGY_TRANSACTION;

		return TRUE;
	} else if (jreader->amount_of_triples == 0) {
		/* end of transaction */

		/* read redundant entry size at end of transaction */
		journal_read_uint32 (jreader, &inner_error);
		if (inner_error) {
			g_propagate_error (error, inner_error);
			return FALSE;
		}

		if (!jreader->stream) {
			if (jreader->current != jreader->entry_end) {
				/* damaged journal entry */
				g_set_error (error, TRACKER_DB_JOURNAL_ERROR,
				             TRACKER_DB_JOURNAL_ERROR_DAMAGED_JOURNAL_ENTRY,
				             "Damaged journal entry, %p != %p (end of transaction with 0 triples)",
				             jreader->current,
				             jreader->entry_end);
				return FALSE;
			}
		}

		jreader->type = TRACKER_DB_JOURNAL_END_TRANSACTION;
		jreader->last_success = jreader->current;

		return TRUE;
	} else {
		DataFormat df;

		df = journal_read_uint32 (jreader, &inner_error);
		if (inner_error) {
			g_propagate_error (error, inner_error);
			return FALSE;
		}

		if (df == DATA_FORMAT_RESOURCE_INSERT) {
			jreader->type = TRACKER_DB_JOURNAL_RESOURCE;

			jreader->s_id = journal_read_uint32 (jreader, &inner_error);
			if (inner_error) {
				g_propagate_error (error, inner_error);
				return FALSE;
			}

			jreader->uri = journal_read_string (jreader, &inner_error);
			if (inner_error) {
				g_propagate_error (error, inner_error);
				return FALSE;
			}
		} else {
			if (df & DATA_FORMAT_OPERATION_DELETE) {
				if (df & DATA_FORMAT_OBJECT_ID) {
					jreader->type = TRACKER_DB_JOURNAL_DELETE_STATEMENT_ID;
				} else {
					jreader->type = TRACKER_DB_JOURNAL_DELETE_STATEMENT;
				}
			} else if (df & DATA_FORMAT_OPERATION_UPDATE) {
				if (df & DATA_FORMAT_OBJECT_ID) {
					jreader->type = TRACKER_DB_JOURNAL_UPDATE_STATEMENT_ID;
				} else {
					jreader->type = TRACKER_DB_JOURNAL_UPDATE_STATEMENT;
				}
			} else {
				if (df & DATA_FORMAT_OBJECT_ID) {
					jreader->type = TRACKER_DB_JOURNAL_INSERT_STATEMENT_ID;
				} else {
					jreader->type = TRACKER_DB_JOURNAL_INSERT_STATEMENT;
				}
			}

			if (df & DATA_FORMAT_GRAPH) {
				/* named graph */
				jreader->g_id = journal_read_uint32 (jreader, &inner_error);
				if (inner_error) {
					g_propagate_error (error, inner_error);
					return FALSE;
				}
			} else {
				/* default graph */
				jreader->g_id = 0;
			}

			jreader->s_id = journal_read_uint32 (jreader, &inner_error);
			if (inner_error) {
				g_propagate_error (error, inner_error);
				return FALSE;
			}

			jreader->p_id = journal_read_uint32 (jreader, &inner_error);
			if (inner_error) {
				g_propagate_error (error, inner_error);
				return FALSE;
			}

			if (df & DATA_FORMAT_OBJECT_ID) {
				jreader->o_id = journal_read_uint32 (jreader, &inner_error);
				if (inner_error) {
					g_propagate_error (error, inner_error);
					return FALSE;
				}
			} else {
				jreader->object = journal_read_string (jreader, &inner_error);
				if (inner_error) {
					g_propagate_error (error, inner_error);
					return FALSE;
				}
			}
		}

		jreader->amount_of_triples--;
		return TRUE;
	}
}

gboolean
tracker_db_journal_reader_next (TrackerDBJournalReader  *reader,
                                GError                 **error)
{
	return db_journal_reader_next (reader, TRUE, error);
}

gboolean
tracker_db_journal_reader_verify_last (GFile   *data_location,
                                       GError **error)
{
	guint32 entry_size_check;
	gboolean success = FALSE;
	TrackerDBJournalReader jreader = { 0 };
	GError *n_error = NULL;
	gchar *filename;
	GFile *child;

	child = g_file_get_child (data_location, TRACKER_DB_JOURNAL_FILENAME);
	filename = g_file_get_path (child);
	g_object_unref (child);

	if (db_journal_reader_init (&jreader, FALSE, filename, data_location, &n_error)) {

		if (jreader.end != jreader.current) {
			entry_size_check = read_uint32 (jreader.end - 4);

			if (jreader.end - entry_size_check < jreader.current) {
				g_free (filename);
				g_set_error (error, TRACKER_DB_JOURNAL_ERROR,
				             TRACKER_DB_JOURNAL_ERROR_DAMAGED_JOURNAL_ENTRY,
				             "Damaged journal entry at end of journal");
				db_journal_reader_clear (&jreader);
				return FALSE;
			}

			jreader.current = jreader.end - entry_size_check;
			success = db_journal_reader_next (&jreader, FALSE, NULL);
		} else {
			success = TRUE;
		}

		db_journal_reader_clear (&jreader);
	}

	g_free (filename);

	if (n_error) {
		g_propagate_error (error, n_error);
	}

	return success;
}

gint64
tracker_db_journal_reader_get_time (TrackerDBJournalReader *reader)
{
	return reader->time;
}

gboolean
tracker_db_journal_reader_get_resource (TrackerDBJournalReader  *reader,
                                        gint                    *id,
                                        const gchar            **uri)
{
	g_return_val_if_fail (reader->file != NULL || reader->stream != NULL, FALSE);
	g_return_val_if_fail (reader->type == TRACKER_DB_JOURNAL_RESOURCE, FALSE);

	*id = reader->s_id;
	*uri = reader->uri;

	return TRUE;
}

gboolean
tracker_db_journal_reader_get_statement (TrackerDBJournalReader  *reader,
                                         gint                    *g_id,
                                         gint                    *s_id,
                                         gint                    *p_id,
                                         const gchar            **object)
{
	g_return_val_if_fail (reader->file != NULL || reader->stream != NULL, FALSE);
	g_return_val_if_fail (reader->type == TRACKER_DB_JOURNAL_INSERT_STATEMENT ||
	                      reader->type == TRACKER_DB_JOURNAL_DELETE_STATEMENT ||
	                      reader->type == TRACKER_DB_JOURNAL_UPDATE_STATEMENT,
	                      FALSE);

	if (g_id) {
		*g_id = reader->g_id;
	}
	*s_id = reader->s_id;
	*p_id = reader->p_id;
	*object = reader->object;

	return TRUE;
}

gboolean
tracker_db_journal_reader_get_statement_id (TrackerDBJournalReader *reader,
                                            gint                   *g_id,
                                            gint                   *s_id,
                                            gint                   *p_id,
                                            gint                   *o_id)
{
	g_return_val_if_fail (reader->file != NULL || reader->stream != NULL, FALSE);
	g_return_val_if_fail (reader->type == TRACKER_DB_JOURNAL_INSERT_STATEMENT_ID ||
	                      reader->type == TRACKER_DB_JOURNAL_DELETE_STATEMENT_ID ||
	                      reader->type == TRACKER_DB_JOURNAL_UPDATE_STATEMENT_ID,
	                      FALSE);

	if (g_id) {
		*g_id = reader->g_id;
	}
	*s_id = reader->s_id;
	*p_id = reader->p_id;
	*o_id = reader->o_id;

	return TRUE;
}

gdouble
tracker_db_journal_reader_get_progress (TrackerDBJournalReader *reader)
{
	gdouble chunk = 0, total = 0, ret = 0;
	guint current_file;
	guint total_chunks = reader->total_chunks;

	current_file = reader->current_file == 0 ? reader->total_chunks -1 : reader->current_file -1;

	if (reader->total_chunks == 0) {
		gchar *test;
		GFile *dest_dir;
		gboolean cont = TRUE;

		total_chunks = 0;

		test = g_path_get_basename (reader->filename);

		if (rotating_settings.rotate_to) {
			dest_dir = g_file_new_for_path (rotating_settings.rotate_to);
		} else {
			GFile *source;

			/* keep compressed journal files in same directory */
			source = g_file_new_for_path (test);
			dest_dir = g_file_get_parent (source);
			g_object_unref (source);
		}

		g_free (test);

		while (cont) {
			gchar *filename;
			GFile *possible;

			test = g_strdup_printf ("%s.%d", reader->filename, total_chunks + 1);
			filename = g_path_get_basename (test);
			g_free (test);
			test = filename;
			filename = g_strconcat (test, ".gz", NULL);
			g_free (test);
			possible = g_file_get_child (dest_dir, filename);
			g_free (filename);
			if (g_file_query_exists (possible, NULL)) {
				total_chunks++;
			} else {
				cont = FALSE;
			}
			g_object_unref (possible);
		}

		g_object_unref (dest_dir);
		reader->total_chunks = total_chunks;
	}

	if (total_chunks > 0) {
		total = ((gdouble) ((gdouble) current_file) / ((gdouble) total_chunks));
	}

	if (reader->start != 0) {
		/* When the last uncompressed part is being processed: */
		gdouble percent = ((gdouble)(reader->end - reader->start));
		ret = chunk = (((gdouble)(reader->current - reader->start)) / percent);
	} else if (reader->underlying_stream) {
		goffset size;

		/* When a compressed part is being processed: */

		if (!reader->underlying_stream_info) {
			reader->underlying_stream_info =
				g_file_input_stream_query_info (G_FILE_INPUT_STREAM (reader->underlying_stream),
				                                G_FILE_ATTRIBUTE_STANDARD_SIZE,
				                                NULL, NULL);
		}

		if (reader->underlying_stream_info) {
			size = g_file_info_get_size (reader->underlying_stream_info);
			ret = chunk = (gdouble) ((gdouble)g_seekable_tell (G_SEEKABLE (reader->underlying_stream))) / ((gdouble)size);
		}
	}

	if (total_chunks > 0) {
		ret = total + (chunk / (gdouble) total_chunks);
	}

	return ret;
}

static void
on_chunk_copied_delete (GObject      *source_object,
                        GAsyncResult *res,
                        gpointer      user_data)
{
	GOutputStream *ostream = G_OUTPUT_STREAM (source_object);
	GError *error = NULL;
	GFile *source = G_FILE (user_data);

	g_output_stream_splice_finish (ostream, res, &error);
	if (!error) {
		g_file_delete (G_FILE (source), NULL, &error);
	}

	g_object_unref (source);

	if (error) {
		g_critical ("Error compressing rotated journal chunk: '%s'", error->message);
		g_error_free (error);
	}
}

static gboolean
tracker_db_journal_rotate (TrackerDBJournal  *writer,
                           GError           **error)
{
	GFile *source, *destination;
	GFile *dest_dir;
	gchar *filename, *gzfilename;
	gchar *fullpath;
	GConverter *converter;
	GInputStream *istream;
	GOutputStream *ostream, *cstream;
	GError *n_error = NULL;
	gboolean ret;

#ifdef DISABLE_JOURNAL
	g_critical ("Journal is disabled, yet a journal function got called");
#endif

	if (writer->cur_journal_file == 0) {
		gchar *directory;
		GDir *journal_dir;
		const gchar *f_name;

		directory = g_path_get_dirname (writer->journal_filename);
		journal_dir = g_dir_open (directory, 0, NULL);

		f_name = g_dir_read_name (journal_dir);

		while (f_name) {
			const gchar *ptr;
			guint cur;

			if (f_name) {

				if (!g_str_has_prefix (f_name, TRACKER_DB_JOURNAL_FILENAME ".")) {
					f_name = g_dir_read_name (journal_dir);
					continue;
				}

				ptr = f_name + strlen (TRACKER_DB_JOURNAL_FILENAME ".");
				cur = atoi (ptr);
				writer->cur_journal_file = MAX (cur, writer->cur_journal_file);
			}

			f_name = g_dir_read_name (journal_dir);
		}

		g_dir_close (journal_dir);
		g_free (directory);
	}

	tracker_db_journal_fsync (writer);

	if (close (writer->journal) != 0) {
		g_set_error (error, TRACKER_DB_JOURNAL_ERROR,
		             TRACKER_DB_JOURNAL_ERROR_COULD_NOT_CLOSE,
		             "Could not close journal, %s",
		             g_strerror (errno));
		return FALSE;
	}

	fullpath = g_strdup_printf ("%s.%d", writer->journal_filename, ++writer->cur_journal_file);

	if (g_rename (writer->journal_filename, fullpath) < 0) {
		g_set_error (error, TRACKER_DB_JOURNAL_ERROR,
			     TRACKER_DB_JOURNAL_ERROR_COULD_NOT_WRITE,
		             "Could not rotate journal file %s: %s",
			     writer->journal_filename,
		             g_strerror (errno));
		return FALSE;
	}

	/* Recalculate progress next time */
	rotating_settings.rotate_progress_flag = FALSE;

	source = g_file_new_for_path (fullpath);
	if (rotating_settings.rotate_to) {
		dest_dir = g_file_new_for_path (rotating_settings.rotate_to);
	} else {
		/* keep compressed journal files in same directory */
		dest_dir = g_file_get_parent (source);
	}
	filename = g_path_get_basename (fullpath);
	gzfilename = g_strconcat (filename, ".gz", NULL);
	destination = g_file_get_child (dest_dir, gzfilename);
	g_object_unref (dest_dir);
	g_free (filename);
	g_free (gzfilename);

	istream = G_INPUT_STREAM (g_file_read (source, NULL, NULL));
	ostream = G_OUTPUT_STREAM (g_file_create (destination, 0, NULL, NULL));
	converter = G_CONVERTER (g_zlib_compressor_new (G_ZLIB_COMPRESSOR_FORMAT_GZIP, -1));
	cstream = g_converter_output_stream_new (ostream, converter);
	g_output_stream_splice_async (cstream, istream, 0, 0, NULL, on_chunk_copied_delete, source);
	g_object_unref (istream);
	g_object_unref (ostream);
	g_object_unref (converter);
	g_object_unref (cstream);

	g_object_unref (destination);

	g_free (fullpath);

	ret = db_journal_init_file (writer, TRUE, &n_error);

	if (n_error) {
		g_propagate_error (error, n_error);
		g_free (writer->journal_filename);
		writer->journal_filename = NULL;
	}

	return ret;
}

void
tracker_db_journal_remove (TrackerDBJournal *writer)
{
	gchar *path;
	gchar *directory;
	const gchar *dirs[3] = { NULL, NULL, NULL };
	guint i;
	GError *error = NULL;

	/* We duplicate the path here because later we shutdown the
	 * journal which frees this data. We want to survive that.
	 */
	path = g_strdup (writer->journal_filename);
	if (!path) {
		return;
	}

	g_info ("  Removing journal:'%s'", path);

	directory = g_path_get_dirname (path);
	tracker_db_journal_free (writer, &error);

	if (error) {
		/* TODO: propagate error */
		g_info ("Ignored error while shutting down journal during remove: %s",
		        error->message ? error->message : "No error given");
		g_error_free (error);
	}

	dirs[0] = directory;
	dirs[1] = rotating_settings.do_rotating ? rotating_settings.rotate_to : NULL;

	for (i = 0; dirs[i] != NULL; i++) {
		GDir *journal_dir;
		const gchar *f;

		journal_dir = g_dir_open (dirs[i], 0, NULL);
		if (!journal_dir) {
			continue;
		}

		/* Remove rotated chunks */
		while ((f = g_dir_read_name (journal_dir)) != NULL) {
			gchar *fullpath;

			if (!g_str_has_prefix (f, TRACKER_DB_JOURNAL_FILENAME ".")) {
				continue;
			}

			fullpath = g_build_filename (dirs[i], f, NULL);
			if (g_unlink (fullpath) == -1) {
				g_info ("Could not unlink rotated journal: %m");
			}
			g_free (fullpath);
		}

		g_dir_close (journal_dir);
	}

	g_free (directory);

	/* Remove active journal */
	if (g_unlink (path) == -1) {
		g_info ("%s", g_strerror (errno));
	}
	g_free (path);
}

#else /* DISABLE_JOURNAL */
void
tracker_db_journal_set_rotating (gboolean     do_rotating,
                                 gsize        chunk_size,
                                 const gchar *rotate_to)
{
	/* intentionally left blank, used for internal API compatibility */
}
#endif /* DISABLE_JOURNAL */
