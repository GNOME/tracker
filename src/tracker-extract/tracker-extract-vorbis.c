/*
 * Copyright (C) 2006, Jamie McCracken <jamiemcc@gnome.org>
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

#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>

#include <glib.h>

#include <vorbis/vorbisfile.h>

#include <libtracker-common/tracker-common.h>

#include <libtracker-extract/tracker-extract.h>

#include "tracker-media-art.h"

typedef struct {
	const gchar *creator;
	gchar *creator_uri;
} MergeData;

typedef struct {
	gchar *title;
	gchar *artist;
	gchar *album;
	gchar *album_artist;
	gchar *track_count;
	gchar *track_number;
	gchar *disc_number;
	gchar *performer;
	gchar *track_gain; 
	gchar *track_peak_gain;
	gchar *album_gain;
	gchar *album_peak_gain;
	gchar *date; 
	gchar *comment;
	gchar *genre; 
	gchar *codec;
	gchar *codec_version;
	gchar *sample_rate;
	gchar *channels; 
	gchar *mb_album_id; 
	gchar *mb_artist_id; 
	gchar *mb_album_artist_id;
	gchar *mb_track_id; 
	gchar *lyrics; 
	gchar *copyright; 
	gchar *license; 
	gchar *organization; 
	gchar *location;
	gchar *publisher;
} VorbisData;

static gchar *
ogg_get_comment (vorbis_comment *vc,
                 const gchar    *label)
{
	gchar *tag;
	gchar *utf_tag;

	if (vc && (tag = vorbis_comment_query (vc, label, 0)) != NULL) {
		utf_tag = g_locale_to_utf8 (tag, -1, NULL, NULL, NULL);
		/*g_free (tag);*/

		return utf_tag;
	} else {
		return NULL;
	}
}

G_MODULE_EXPORT gboolean
tracker_extract_get_metadata (TrackerExtractInfo *info)
{
	TrackerSparqlBuilder *preupdate, *metadata;
	VorbisData vd = { 0 };
	MergeData md = { 0 };
	FILE *f;
	gchar *filename, *uri;
	OggVorbis_File vf;
	vorbis_comment *comment;
	vorbis_info *vi;
	unsigned int bitrate;
	gint time;
	GFile *file;
	const gchar *graph;

	file = tracker_extract_info_get_file (info);
	filename = g_file_get_path (file);
	f = tracker_file_open (filename);
	g_free (filename);

	preupdate = tracker_extract_info_get_preupdate_builder (info);
	metadata = tracker_extract_info_get_metadata_builder (info);
	graph = tracker_extract_info_get_graph (info);

	if (!f) {
		return FALSE;
	}

	if (ov_open (f, &vf, NULL, 0) < 0) {
		tracker_file_close (f, FALSE);
		return FALSE;
	}

	tracker_sparql_builder_predicate (metadata, "a");
	tracker_sparql_builder_object (metadata, "nmm:MusicPiece");
	tracker_sparql_builder_object (metadata, "nfo:Audio");

	if ((comment = ov_comment (&vf, -1)) != NULL) {
		gchar *date;

		vd.title = ogg_get_comment (comment, "title");
		vd.artist = ogg_get_comment (comment, "artist");
		vd.album = ogg_get_comment (comment, "album");
		vd.album_artist = ogg_get_comment (comment, "albumartist");
		vd.track_count = ogg_get_comment (comment, "trackcount");
		vd.track_number = ogg_get_comment (comment, "tracknumber");
		vd.disc_number = ogg_get_comment (comment, "DiscNo");
		vd.performer = ogg_get_comment (comment, "Performer");
		vd.track_gain = ogg_get_comment (comment, "TrackGain");
		vd.track_peak_gain = ogg_get_comment (comment, "TrackPeakGain");
		vd.album_gain = ogg_get_comment (comment, "AlbumGain");
		vd.album_peak_gain = ogg_get_comment (comment, "AlbumPeakGain");

		date = ogg_get_comment (comment, "date");
		vd.date = tracker_date_guess (date);
		g_free (date);

		vd.comment = ogg_get_comment (comment, "comment");
		vd.genre = ogg_get_comment (comment, "genre");
		vd.codec = ogg_get_comment (comment, "Codec");
		vd.codec_version = ogg_get_comment (comment, "CodecVersion");
		vd.sample_rate = ogg_get_comment (comment, "SampleRate");
		vd.channels = ogg_get_comment (comment, "Channels");
		vd.mb_album_id = ogg_get_comment (comment, "MBAlbumID");
		vd.mb_artist_id = ogg_get_comment (comment, "MBArtistID");
		vd.mb_album_artist_id = ogg_get_comment (comment, "MBAlbumArtistID");
		vd.mb_track_id = ogg_get_comment (comment, "MBTrackID");
		vd.lyrics = ogg_get_comment (comment, "Lyrics");
		vd.copyright = ogg_get_comment (comment, "Copyright");
		vd.license = ogg_get_comment (comment, "License");
		vd.organization = ogg_get_comment (comment, "Organization");
		vd.location = ogg_get_comment (comment, "Location");
		vd.publisher = ogg_get_comment (comment, "Publisher");

		vorbis_comment_clear (comment);
	}

	md.creator = tracker_coalesce_strip (3, vd.artist, vd.album_artist, vd.performer);

	if (md.creator) {
		/* NOTE: This must be created before vd.album is evaluated */
		md.creator_uri = tracker_sparql_escape_uri_printf ("urn:artist:%s", md.creator);

		tracker_sparql_builder_insert_open (preupdate, NULL);
		if (graph) {
			tracker_sparql_builder_graph_open (preupdate, graph);
		}

		tracker_sparql_builder_subject_iri (preupdate, md.creator_uri);
		tracker_sparql_builder_predicate (preupdate, "a");
		tracker_sparql_builder_object (preupdate, "nmm:Artist");
		tracker_sparql_builder_predicate (preupdate, "nmm:artistName");
		tracker_sparql_builder_object_unvalidated (preupdate, md.creator);

		if (graph) {
			tracker_sparql_builder_graph_close (preupdate);
		}
		tracker_sparql_builder_insert_close (preupdate);

		tracker_sparql_builder_predicate (metadata, "nmm:performer");
		tracker_sparql_builder_object_iri (metadata, md.creator_uri);
	}

	if (vd.album) {
                gchar *uri;
                if (vd.album_artist) {
                        uri = tracker_sparql_escape_uri_printf ("urn:album:%s:%s", vd.album, vd.album_artist);
                } else {
                        uri = tracker_sparql_escape_uri_printf ("urn:album:%s", vd.album);
                }
		gchar *album_disc_uri;

		tracker_sparql_builder_insert_open (preupdate, NULL);
		if (graph) {
			tracker_sparql_builder_graph_open (preupdate, graph);
		}

		tracker_sparql_builder_subject_iri (preupdate, uri);
		tracker_sparql_builder_predicate (preupdate, "a");
		tracker_sparql_builder_object (preupdate, "nmm:MusicAlbum");
		/* FIXME: nmm:albumTitle is now deprecated
		 * tracker_sparql_builder_predicate (preupdate, "nie:title");
		 */
		tracker_sparql_builder_predicate (preupdate, "nmm:albumTitle");
		tracker_sparql_builder_object_unvalidated (preupdate, vd.album);

		if (md.creator_uri) {
			tracker_sparql_builder_predicate (preupdate, "nmm:albumArtist");
			tracker_sparql_builder_object_iri (preupdate, md.creator_uri);
		}

		if (graph) {
			tracker_sparql_builder_graph_close (preupdate);
		}
		tracker_sparql_builder_insert_close (preupdate);

		if (vd.track_count) {
			tracker_sparql_builder_delete_open (preupdate, NULL);
			tracker_sparql_builder_subject_iri (preupdate, uri);
			tracker_sparql_builder_predicate (preupdate, "nmm:albumTrackCount");
			tracker_sparql_builder_object_variable (preupdate, "unknown");
			tracker_sparql_builder_delete_close (preupdate);

			tracker_sparql_builder_where_open (preupdate);
			tracker_sparql_builder_subject_iri (preupdate, uri);
			tracker_sparql_builder_predicate (preupdate, "nmm:albumTrackCount");
			tracker_sparql_builder_object_variable (preupdate, "unknown");
			tracker_sparql_builder_where_close (preupdate);

			tracker_sparql_builder_insert_open (preupdate, NULL);
			if (graph) {
				tracker_sparql_builder_graph_open (preupdate, graph);
			}

			tracker_sparql_builder_subject_iri (preupdate, uri);
			tracker_sparql_builder_predicate (preupdate, "nmm:albumTrackCount");
			tracker_sparql_builder_object_unvalidated (preupdate, vd.track_count);

			if (graph) {
				tracker_sparql_builder_graph_close (preupdate);
			}
			tracker_sparql_builder_insert_close (preupdate);
		}

		if (vd.album_gain) {
			tracker_sparql_builder_delete_open (preupdate, NULL);
			tracker_sparql_builder_subject_iri (preupdate, uri);
			tracker_sparql_builder_predicate (preupdate, "nmm:albumGain");
			tracker_sparql_builder_object_variable (preupdate, "unknown");
			tracker_sparql_builder_delete_close (preupdate);

			tracker_sparql_builder_where_open (preupdate);
			tracker_sparql_builder_subject_iri (preupdate, uri);
			tracker_sparql_builder_predicate (preupdate, "nmm:albumGain");
			tracker_sparql_builder_object_variable (preupdate, "unknown");
			tracker_sparql_builder_where_close (preupdate);

			tracker_sparql_builder_insert_open (preupdate, NULL);
			if (graph) {
				tracker_sparql_builder_graph_open (preupdate, graph);
			}

			tracker_sparql_builder_subject_iri (preupdate, uri);
			tracker_sparql_builder_predicate (preupdate, "nmm:albumGain");
			tracker_sparql_builder_object_double (preupdate, atof (vd.album_gain));

			if (graph) {
				tracker_sparql_builder_graph_close (preupdate);
			}
			tracker_sparql_builder_insert_close (preupdate);
		}

		if (vd.album_peak_gain) {
			tracker_sparql_builder_delete_open (preupdate, NULL);
			tracker_sparql_builder_subject_iri (preupdate, uri);
			tracker_sparql_builder_predicate (preupdate, "nmm:albumPeakGain");
			tracker_sparql_builder_object_variable (preupdate, "unknown");
			tracker_sparql_builder_delete_close (preupdate);

			tracker_sparql_builder_where_open (preupdate);
			tracker_sparql_builder_subject_iri (preupdate, uri);
			tracker_sparql_builder_predicate (preupdate, "nmm:albumPeakGain");
			tracker_sparql_builder_object_variable (preupdate, "unknown");
			tracker_sparql_builder_where_close (preupdate);

			tracker_sparql_builder_insert_open (preupdate, NULL);
			if (graph) {
				tracker_sparql_builder_graph_open (preupdate, graph);
			}

			tracker_sparql_builder_subject_iri (preupdate, uri);
			tracker_sparql_builder_predicate (preupdate, "nmm:albumPeakGain");
			tracker_sparql_builder_object_double (preupdate, atof (vd.album_peak_gain));

			if (graph) {
				tracker_sparql_builder_graph_close (preupdate);
			}
			tracker_sparql_builder_insert_close (preupdate);
		}

                if (vd.album_artist) {
                        album_disc_uri = tracker_sparql_escape_uri_printf ("urn:album-disc:%s:%s:Disc%d",
                                                                           vd.album, vd.album_artist,
                                                                           vd.disc_number ? atoi(vd.disc_number) : 1);
                } else {
                        album_disc_uri = tracker_sparql_escape_uri_printf ("urn:album-disc:%s:Disc%d",
                                                                           vd.album,
                                                                           vd.disc_number ? atoi(vd.disc_number) : 1);
                }

		tracker_sparql_builder_delete_open (preupdate, NULL);
		tracker_sparql_builder_subject_iri (preupdate, album_disc_uri);
		tracker_sparql_builder_predicate (preupdate, "nmm:setNumber");
		tracker_sparql_builder_object_variable (preupdate, "unknown");
		tracker_sparql_builder_delete_close (preupdate);
		tracker_sparql_builder_where_open (preupdate);
		tracker_sparql_builder_subject_iri (preupdate, album_disc_uri);
		tracker_sparql_builder_predicate (preupdate, "nmm:setNumber");
		tracker_sparql_builder_object_variable (preupdate, "unknown");
		tracker_sparql_builder_where_close (preupdate);

		tracker_sparql_builder_delete_open (preupdate, NULL);
		tracker_sparql_builder_subject_iri (preupdate, album_disc_uri);
		tracker_sparql_builder_predicate (preupdate, "nmm:albumDiscAlbum");
		tracker_sparql_builder_object_variable (preupdate, "unknown");
		tracker_sparql_builder_delete_close (preupdate);
		tracker_sparql_builder_where_open (preupdate);
		tracker_sparql_builder_subject_iri (preupdate, album_disc_uri);
		tracker_sparql_builder_predicate (preupdate, "nmm:albumDiscAlbum");
		tracker_sparql_builder_object_variable (preupdate, "unknown");
		tracker_sparql_builder_where_close (preupdate);

		tracker_sparql_builder_insert_open (preupdate, NULL);
		if (graph) {
			tracker_sparql_builder_graph_open (preupdate, graph);
		}

		tracker_sparql_builder_subject_iri (preupdate, album_disc_uri);
		tracker_sparql_builder_predicate (preupdate, "a");
		tracker_sparql_builder_object (preupdate, "nmm:MusicAlbumDisc");
		tracker_sparql_builder_predicate (preupdate, "nmm:setNumber");
		tracker_sparql_builder_object_int64 (preupdate, vd.disc_number ? atoi (vd.disc_number) : 1);
		tracker_sparql_builder_predicate (preupdate, "nmm:albumDiscAlbum");
		tracker_sparql_builder_object_iri (preupdate, uri);

		if (graph) {
			tracker_sparql_builder_graph_close (preupdate);
		}
		tracker_sparql_builder_insert_close (preupdate);

		tracker_sparql_builder_predicate (metadata, "nmm:musicAlbumDisc");
		tracker_sparql_builder_object_iri (metadata, album_disc_uri);

	        g_free (album_disc_uri);

		tracker_sparql_builder_predicate (metadata, "nmm:musicAlbum");
		tracker_sparql_builder_object_iri (metadata, uri);
		g_free (uri);
	}

	g_free (vd.track_count);
	g_free (vd.album_peak_gain);
	g_free (vd.album_gain);
	g_free (vd.disc_number);

	if (vd.title) {
		tracker_sparql_builder_predicate (metadata, "nie:title");
		tracker_sparql_builder_object_unvalidated (metadata, vd.title);
		g_free (vd.title);
	}

	if (vd.track_number) {
		tracker_sparql_builder_predicate (metadata, "nmm:trackNumber");
		tracker_sparql_builder_object_unvalidated (metadata, vd.track_number);
		g_free (vd.track_number);
	}

	if (vd.track_gain) {
		/* TODO */
		g_free (vd.track_gain);
	}

	if (vd.track_peak_gain) {
		/* TODO */
		g_free (vd.track_peak_gain);
	}

	if (vd.comment) {
		tracker_sparql_builder_predicate (metadata, "nie:comment");
		tracker_sparql_builder_object_unvalidated (metadata, vd.comment);
		g_free (vd.comment);
	}

	if (vd.date) {
		tracker_sparql_builder_predicate (metadata, "nie:contentCreated");
		tracker_sparql_builder_object_unvalidated (metadata, vd.date);
		g_free (vd.date);
	}

	if (vd.genre) {
		tracker_sparql_builder_predicate (metadata, "nfo:genre");
		tracker_sparql_builder_object_unvalidated (metadata, vd.genre);
		g_free (vd.genre);
	}

	if (vd.codec) {
		tracker_sparql_builder_predicate (metadata, "nfo:codec");
		tracker_sparql_builder_object_unvalidated (metadata, vd.codec);
		g_free (vd.codec);
	}

	if (vd.codec_version) {
		/* TODO */
		g_free (vd.codec_version);
	}

	if (vd.sample_rate) {
		tracker_sparql_builder_predicate (metadata, "nfo:sampleRate");
		tracker_sparql_builder_object_unvalidated (metadata, vd.sample_rate);
		g_free (vd.sample_rate);
	}

	if (vd.channels) {
		tracker_sparql_builder_predicate (metadata, "nfo:channels");
		tracker_sparql_builder_object_unvalidated (metadata, vd.channels);
		g_free (vd.channels);
	}

	if (vd.mb_album_id) {
		/* TODO */
		g_free (vd.mb_album_id);
	}

	if (vd.mb_artist_id) {
		/* TODO */
		g_free (vd.mb_artist_id);
	}

	if (vd.mb_album_artist_id) {
		/* TODO */
		g_free (vd.mb_album_artist_id);
	}

	if (vd.mb_track_id) {
		/* TODO */
		g_free (vd.mb_track_id);
	}

	if (vd.lyrics) {
		tracker_sparql_builder_predicate (metadata, "nie:plainTextContent");
		tracker_sparql_builder_object_unvalidated (metadata, vd.lyrics);
		g_free (vd.lyrics);
	}

	if (vd.copyright) {
		tracker_sparql_builder_predicate (metadata, "nie:copyright");
		tracker_sparql_builder_object_unvalidated (metadata, vd.copyright);
		g_free (vd.copyright);
	}

	if (vd.license) {
		tracker_sparql_builder_predicate (metadata, "nie:license");
		tracker_sparql_builder_object_unvalidated (metadata, vd.license);
		g_free (vd.license);
	}

	if (vd.organization) {
		/* TODO */
		g_free (vd.organization);
	}

	if (vd.location) {
		/* TODO */
		g_free (vd.location);
	}

	if (vd.publisher) {
		tracker_sparql_builder_predicate (metadata, "dc:publisher");

		tracker_sparql_builder_object_blank_open (metadata);
		tracker_sparql_builder_predicate (metadata, "a");
		tracker_sparql_builder_object (metadata, "nco:Contact");

		tracker_sparql_builder_predicate (metadata, "nco:fullname");
		tracker_sparql_builder_object_unvalidated (metadata, vd.publisher);
		tracker_sparql_builder_object_blank_close (metadata);
		g_free (vd.publisher);
	}

	if ((vi = ov_info (&vf, 0)) != NULL ) {
		bitrate = vi->bitrate_nominal / 1000;

		tracker_sparql_builder_predicate (metadata, "nfo:averageBitrate");
		tracker_sparql_builder_object_int64 (metadata, (gint64) bitrate);
	}

	/* Duration */
	if ((time = ov_time_total (&vf, -1)) != OV_EINVAL) {
		tracker_sparql_builder_predicate (metadata, "nfo:duration");
		tracker_sparql_builder_object_int64 (metadata, (gint64) time);
	}

	uri = g_file_get_uri (file);
	tracker_media_art_process (NULL,
				0,
				NULL,
				TRACKER_MEDIA_ART_ALBUM,
				vd.album_artist ? vd.album_artist : vd.artist,
				vd.album,
				uri);
	g_free (uri);

	g_free (vd.artist);
	g_free (vd.album);
	g_free (vd.album_artist);
	g_free (vd.performer);

	g_free (md.creator_uri);

#ifdef HAVE_POSIX_FADVISE
	posix_fadvise (fileno (f), 0, 0, POSIX_FADV_DONTNEED);
#endif /* HAVE_POSIX_FADVISE */

	/* NOTE: This calls fclose on the file */
	ov_clear (&vf);

	return TRUE;
}
