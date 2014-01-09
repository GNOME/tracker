/*
 * Copyright (C) 2013-2014 Jolla Ltd. <andrew.den.exter@jollamobile.com>
 * Author: Andrew den Exter <andrew.den.exter@jollamobile.com>
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


#include <glib.h>

#include <libtracker-common/tracker-ontologies.h>
#include <libtracker-common/tracker-utils.h>

#include <libtracker-extract/tracker-extract.h>

#include <tracker-media-art.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/mathematics.h>

static void
open_insert (TrackerSparqlBuilder *preupdate,
             const char           *graph,
             const gchar          *iri,
             const gchar          *type)
{
	tracker_sparql_builder_insert_open (preupdate, NULL);
	if (graph) {
		tracker_sparql_builder_graph_open (preupdate, graph);
	}

	tracker_sparql_builder_subject_iri (preupdate, iri);
	tracker_sparql_builder_predicate (preupdate, "a");
	tracker_sparql_builder_object (preupdate, type);
}

static void
close_insert (TrackerSparqlBuilder *preupdate, const char *graph)
{
	if (graph) {
		tracker_sparql_builder_graph_close (preupdate);
	}
	tracker_sparql_builder_insert_close (preupdate);
}

static void
delete_value (TrackerSparqlBuilder *preupdate,
              const gchar          *iri,
              const gchar          *predicate,
              const char           *value)
{
	tracker_sparql_builder_delete_open (preupdate, NULL);
	tracker_sparql_builder_subject_iri (preupdate, iri);
	tracker_sparql_builder_predicate (preupdate, predicate);
	tracker_sparql_builder_object_variable (preupdate, value);
	tracker_sparql_builder_delete_close (preupdate);

	tracker_sparql_builder_where_open (preupdate);
	tracker_sparql_builder_subject_iri (preupdate, iri);
	tracker_sparql_builder_predicate (preupdate, predicate);
	tracker_sparql_builder_object_variable (preupdate, value);
	tracker_sparql_builder_where_close (preupdate);
}

static void
set_value_iri (TrackerSparqlBuilder *metadata,
               const gchar          *predicate,
               const gchar          *iri)
{
	tracker_sparql_builder_predicate (metadata, predicate);
	tracker_sparql_builder_object_iri (metadata, iri);
}

static void
set_value_double (TrackerSparqlBuilder *metadata,
                  const gchar          *predicate,
                  gdouble               value)
{
	tracker_sparql_builder_predicate (metadata, predicate);
	tracker_sparql_builder_object_double (metadata, value);
}

static void
set_value_int64 (TrackerSparqlBuilder *metadata,
                 const gchar          *predicate,
                 gint64                value)
{
	tracker_sparql_builder_predicate (metadata, predicate);
	tracker_sparql_builder_object_int64 (metadata, value);
}

static void
set_value_string (TrackerSparqlBuilder *metadata,
                  const gchar          *predicate,
                  const gchar          *value)
{
	tracker_sparql_builder_predicate (metadata, predicate);
	tracker_sparql_builder_object_unvalidated (metadata, value);
}

static gchar *
create_artist (TrackerSparqlBuilder *preupdate,
               const gchar          *graph,
               const char           *name)
{
	gchar *uri = tracker_sparql_escape_uri_printf ("urn:artist:%s", name);

	open_insert (preupdate, graph, uri, "nmm:Artist");
	set_value_string (preupdate, "nmm:artistName", name);
	close_insert (preupdate, graph);

	return uri;
}

G_MODULE_EXPORT gboolean
tracker_extract_get_metadata (TrackerExtractInfo *info)
{
	GFile *file;
	TrackerSparqlBuilder *metadata;
	TrackerSparqlBuilder *preupdate;
	const gchar *graph;
	gchar *absolute_file_path;
	gchar *uri;
	AVFormatContext *format = NULL;
	AVStream *audio_stream = NULL;
	AVStream *video_stream = NULL;
	int audio_stream_index;
	int video_stream_index;
	AVDictionaryEntry *tag = NULL;
	const char *title = NULL;

	av_register_all ();

	file = tracker_extract_info_get_file (info);
	metadata = tracker_extract_info_get_metadata_builder (info);
	preupdate = tracker_extract_info_get_preupdate_builder (info);
	graph = tracker_extract_info_get_graph (info);

	uri = g_file_get_uri (file);

	absolute_file_path = g_file_get_path (file);
	if (avformat_open_input (&format, absolute_file_path, NULL, NULL)) {
		g_free (absolute_file_path);
		g_free (uri);
		return FALSE;
	}
	g_free (absolute_file_path);

	avformat_find_stream_info (format, NULL);

	audio_stream_index = av_find_best_stream (format, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
	if (audio_stream_index >= 0) {
		audio_stream = format->streams[audio_stream_index];
	}

	video_stream_index = av_find_best_stream (format, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
	if (video_stream_index >= 0) {
		video_stream = format->streams[video_stream_index];
	}

	if (!audio_stream && !video_stream) {
		avformat_free_context (format);
		g_free (uri);
		return FALSE;
	}

	if (audio_stream) {
		if (audio_stream->codec->sample_rate > 0) {
			set_value_int64 (metadata, "nfo:sampleRate", audio_stream->codec->sample_rate);
		}
		if (audio_stream->codec->channels > 0) {
			set_value_int64 (metadata, "nfo:channels", audio_stream->codec->channels);
		}
	}

	if (video_stream) {
		tracker_sparql_builder_predicate (metadata, "a");
		tracker_sparql_builder_object (metadata, "nmm:Video");

		if (video_stream->codec->width > 0 && video_stream->codec->height > 0) {
			set_value_int64 (metadata, "nfo:width", video_stream->codec->width);
			set_value_int64 (metadata, "nfo:height", video_stream->codec->height);
		}

		if (video_stream->avg_frame_rate.num > 0) {
			gdouble frame_rate = (gdouble) video_stream->avg_frame_rate.num
			                     / video_stream->avg_frame_rate.den;
			set_value_double (metadata, "nfo:frameRate", frame_rate);
		}

		if (video_stream->duration > 0) {
			gint64 duration = av_rescale(video_stream->duration, video_stream->time_base.num,
			                             video_stream->time_base.den);
			set_value_int64 (metadata, "nfo:duration", duration);
		}

		if (video_stream->sample_aspect_ratio.num > 0) {
			gdouble aspect_ratio = (gdouble) video_stream->sample_aspect_ratio.num
			                       / video_stream->sample_aspect_ratio.den;
			set_value_double (metadata, "nfo:aspectRatio", aspect_ratio);
		}

		if (video_stream->nb_frames > 0) {
			set_value_int64 (metadata, "nfo:frameCount", video_stream->nb_frames);
		}

		if ((tag = av_dict_get (format->metadata, "synopsis", NULL, 0))) {
			set_value_string (metadata, "nmm:synopsis", tag->value);
		}

		if ((tag = av_dict_get (format->metadata, "episode_sort", NULL, 0))) {
			set_value_int64 (metadata, "nmm:episodeNumber", atoi(tag->value));
		}

		if ((tag = av_dict_get (format->metadata, "season_number", NULL, 0))) {
			set_value_int64 (metadata, "nmm:season", atoi(tag->value));
		}

	} else if (audio_stream) {
		const char *album_title = NULL;
		const char *album_artist = NULL;
		gchar *album_artist_uri = NULL;
		gchar *performer_uri = NULL;

		tracker_sparql_builder_predicate (metadata, "a");
		tracker_sparql_builder_object (metadata, "nmm:MusicPiece");
		tracker_sparql_builder_object (metadata, "nfo:Audio");

		if (audio_stream->duration > 0) {
			gint64 duration = av_rescale(audio_stream->duration, audio_stream->time_base.num,
			                             audio_stream->time_base.den);
			set_value_int64 (metadata, "nfo:duration", duration);
		}

		if ((tag = av_dict_get (format->metadata, "track", NULL, 0))) {
			int track = atoi(tag->value);
			if (track > 0) {
				set_value_int64 (metadata, "nmm:trackNumber", track);
			}
		}

		if ((tag = av_dict_get (format->metadata, "album", NULL, 0))) {
			album_title = tag->value;
		}

		if (album_title && (tag = av_dict_get (format->metadata, "album_artist", NULL, 0))) {
			album_artist_uri = create_artist (preupdate, graph, tag->value);
			album_artist = tag->value;
		}

		if ((tag = av_dict_get (format->metadata, "artist", tag, 0))) {
			performer_uri = create_artist (preupdate, graph, tag->value);
			if (!album_artist) {
				album_artist = tag->value;
			}
		}

		if (!performer_uri && (tag = av_dict_get (format->metadata, "performer", tag, 0))) {
			performer_uri = create_artist (preupdate, graph, tag->value);
			if (!album_artist) {
				album_artist = tag->value;
			}
		}

		if (performer_uri) {
			set_value_iri (metadata, "nmm:performer", performer_uri);
		} else if (album_artist_uri) {
			set_value_iri (metadata, "nmm:performer", album_artist_uri);
		}

		if ((tag = av_dict_get (format->metadata, "composer", tag, 0))) {
			gchar *composer_uri = create_artist (preupdate, graph, tag->value);
			set_value_iri (metadata, "nmm:composer", composer_uri);
			g_free(composer_uri);
		}


		if (album_title) {
			int disc = 1;
			gchar *disc_uri;
			gchar *album_uri = tracker_sparql_escape_uri_printf ("urn:album:%s", album_title);

			open_insert (preupdate, graph, album_uri, "nmm:MusicAlbum");
			set_value_string (preupdate, "nmm:albumTitle", album_title);
			if (album_artist_uri) {
				set_value_iri (preupdate, "nmm:albumArtist", album_artist_uri);
			} else if (performer_uri) {
				set_value_iri (preupdate, "nmm:albumArtist", performer_uri);
			}
			close_insert (preupdate, graph);


			if ((tag = av_dict_get (format->metadata, "disc", NULL, 0))) {
				disc = atoi (tag->value);
			}

			disc_uri = tracker_sparql_escape_uri_printf ("urn:album-disc:%s:Disc%d",
			                                             album_title,
			                                             disc);

			delete_value (preupdate, disc_uri, "nmm:setNumber", "unknown");
			delete_value (preupdate, disc_uri, "nmm:albumDiscAlbum", "unknown");

			open_insert (preupdate, graph, disc_uri, "nmm:MusicAlbumDisc");
			set_value_int64 (preupdate, "nmm:setNumber", disc);
			set_value_iri (preupdate, "nmm:albumDiscAlbum", album_uri);
			close_insert (preupdate, graph);

			set_value_iri (metadata, "nmm:musicAlbumDisc", disc_uri);
			set_value_iri (metadata, "nmm:musicAlbum", album_uri);

			g_free (disc_uri);
			g_free (album_uri);
		}

		tracker_media_art_process (NULL,
		                           0,
		                           NULL,
		                           TRACKER_MEDIA_ART_ALBUM,
		                           album_artist,
		                           album_title,
		                           uri);

		g_free(performer_uri);
	}

	if (format->bit_rate > 0) {
		set_value_int64 (metadata, "nfo:averageBitrate", format->bit_rate);
	}


	if ((tag = av_dict_get (format->metadata, "comment", NULL, 0))) {
		set_value_string (metadata, "nie:comment", tag->value);
	}

	if ((tag = av_dict_get (format->metadata, "copyright", NULL, 0))) {
		set_value_string (metadata, "nie:copyright", tag->value);
	}

	if ((tag = av_dict_get (format->metadata, "creation_time", NULL, 0))) {
		gchar *content_created = tracker_date_guess (tag->value);
		if (content_created) {
			set_value_string (metadata, "nie:contentCreated", content_created);
			g_free (content_created);
		}
	}

	if ((tag = av_dict_get (format->metadata, "description", NULL, 0))) {
		set_value_string (metadata, "nie:description", tag->value);
	}

	if ((tag = av_dict_get (format->metadata, "genre", NULL, 0))) {
		set_value_string (metadata, "nfo:genre", tag->value);
	}

	if ((tag = av_dict_get (format->metadata, "language", NULL, 0))) {
		set_value_string (metadata, "nfo:language", tag->value);
	}

	if ((tag = av_dict_get (format->metadata, "title", NULL, 0))) {
		title = tag->value;
	}

	tracker_guarantee_title_from_file (metadata, "nie:title", title, uri, NULL);

	g_free (uri);

	avformat_free_context (format);

	return TRUE;
}
