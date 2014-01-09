/*
 * Copyright (C) 2010, Nokia
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
 * Authors:
 * Philip Van Hoof <philip@codeminded.be>
 */

#include "config.h"

#include "tracker-main.h"

#include <QFile>
#include <QBuffer>
#include <QImageReader>
#include <QImageWriter>
#ifdef HAVE_QT5
#include <QGuiApplication>
#else
#include <QApplication>
#endif
#include <QColor>
#include <QPainter>

#include <glib.h>

#ifdef HAVE_NEMO
#include <stdlib.h>
#endif

#include "tracker-media-art-generic.h"

G_BEGIN_DECLS

#ifdef HAVE_QT5
static QGuiApplication *app = NULL;
#else /* HAVE_QT4 */
static QApplication *app = NULL
#endif

void
tracker_media_art_plugin_init (void)
{
	int argc = 1;
	char *argv[2] = { (char*) "tracker-extract", NULL };

#ifdef HAVE_QT5

#ifdef HAVE_NEMO
	g_setenv("QT_QPA_PLATFORM", "minimal", 1);
#endif

	app = new QGuiApplication (argc, argv);
#else /* HAVE_QT4 */
	app = new QApplication (argc, argv, QApplication::Tty);
#endif
}

void
tracker_media_art_plugin_shutdown (void)
{
	// Apparently isn't destructing a QApplication something you should do, as
	// QApplication is designed to work on stack of the main() function.

	// delete app;
}

gboolean
tracker_media_art_file_to_jpeg (const gchar *filename,
                                const gchar *target)
{
	TrackerConfig *config = tracker_main_get_config ();
	gint max_media_art_width = tracker_config_get_max_media_art_width (config);

	if (max_media_art_width < 0) {
		g_debug ("Not saving album art from file, disabled in config");
		return TRUE;
	}

	/* TODO: Add resizing support */

	QFile file (filename);

	if (!file.open (QIODevice::ReadOnly)) {
		g_message ("Could not get QFile from file: '%s'", filename);
		return FALSE;
	}

	QByteArray array = file.readAll ();
	QBuffer buffer (&array);

	buffer.open (QIODevice::ReadOnly);

	QImageReader reader (&buffer);

	if (!reader.canRead ()) {
		g_message ("Could not get QImageReader from file: '%s', reader.canRead was FALSE",
		           filename);
		return FALSE;
	}

	QImage image1;
	image1 = reader.read ();

	if (image1.hasAlphaChannel ()) {
		QImage image2 (image1.size(), QImage::Format_RGB32);
		image2.fill (QColor(Qt::black).rgb());
		QPainter painter (&image2);
		painter.drawImage (0, 0, image1);
		image2.save (QString (target), "jpeg");
	} else {
		image1.save (QString (target), "jpeg");
	}

	return TRUE;
}

gboolean
tracker_media_art_buffer_to_jpeg (const unsigned char *buffer,
                                  size_t               len,
                                  const gchar         *buffer_mime,
                                  const gchar         *target)
{
	TrackerConfig *config = tracker_main_get_config ();
	gint max_media_art_width = tracker_config_get_max_media_art_width (config);

	if (max_media_art_width < 0) {
		g_debug ("Not saving album art from buffer, disabled in config");
		return TRUE;
	}

	/* FF D8 FF are the three first bytes of JPeg images */
	if (max_media_art_width == 0 &&
	    (g_strcmp0 (buffer_mime, "image/jpeg") == 0 ||
	     g_strcmp0 (buffer_mime, "JPG") == 0) &&
	    (buffer && len > 2 && buffer[0] == 0xff && buffer[1] == 0xd8 && buffer[2] == 0xff)) {
		g_debug ("Saving album art using raw data as uri:'%s'",
		         target);
		g_file_set_contents (target, (const gchar*) buffer, (gssize) len, NULL);
	} else {
		QImageReader *reader = NULL;
		QByteArray array;

		/* TODO: Add resizing support */

		array = QByteArray ((const char *) buffer, (int) len);

		QBuffer qbuffer (&array);
		qbuffer.open (QIODevice::ReadOnly);

		if (buffer_mime != NULL) {
			reader = new QImageReader (&qbuffer, QByteArray (buffer_mime));
		} else {
			QByteArray format = QImageReader::imageFormat (&qbuffer);

			if (!format.isEmpty ()) {
				reader = new QImageReader (&qbuffer, format);
			}
		}

		if (!reader) {
			g_message ("Could not get QImageReader from buffer");
			return FALSE;
		}

		QImage image1;
		image1 = reader->read ();

		if (image1.hasAlphaChannel ()) {
			QImage image2 (image1.size(), QImage::Format_RGB32);
			image2.fill (QColor(Qt::black).rgb());
			QPainter painter (&image2);
			painter.drawImage (0, 0, image1);
			image2.save (QString (target), "jpeg");
		} else {
			image1.save (QString (target), "jpeg");
		}

		delete reader;
	}

	return TRUE;
}

G_END_DECLS
