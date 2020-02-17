/*
 * Copyright (C) 2016 Carlos Garnacho <carlosg@gnome.org>
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
[CCode (cname = "PACKAGE_VERSION")]
extern const string PACKAGE_VERSION;

public class Tracker.Remote.Connection : Tracker.Sparql.Connection {

	internal Soup.Session _session;
	internal string _base_uri;

	const string XML_TYPE = "application/sparql-results+xml";
	const string JSON_TYPE = "application/sparql-results+json";
	const string USER_AGENT = "Tracker/" + PACKAGE_VERSION + " (https://gitlab.gnome.org/GNOME/tracker/issues/; tracker-list@lists.gnome.org) Tracker/" + PACKAGE_VERSION;

	public Connection (string base_uri) {
		Object ();
		_base_uri = base_uri;
		_session = new Soup.Session ();
	}

	private Soup.Message create_request (string sparql) {
		var uri = _base_uri + "?query=" + sparql;
		var message = new Soup.Message ("GET", uri);
		var headers = message.request_headers;

		headers.append ("User-Agent", USER_AGENT);
		headers.append ("Accept", JSON_TYPE);
		headers.append ("Accept", XML_TYPE);

		return message;
	}

	private Sparql.Cursor create_cursor (Soup.Message message) throws GLib.Error, Sparql.Error {
		string document = (string) message.response_body.flatten ().data;

		if (message.status_code != Soup.Status.OK) {
			throw new Sparql.Error.UNSUPPORTED ("Unhandled status code %u, document is: %s",
			                                    message.status_code, document);
		}

		var headers = message.response_headers;
		var content_type = headers.get_content_type (null);
		long length = document.length;

		if (content_type == JSON_TYPE) {
			return new Tracker.Remote.JsonCursor (document, length);
		} else if (content_type == XML_TYPE) {
			return new Tracker.Remote.XmlCursor (document, length);
		} else {
			throw new Sparql.Error.UNSUPPORTED ("Unknown content type '%s', document is: %s", content_type, document);
		}
	}

	public override Sparql.Cursor query (string sparql, Cancellable? cancellable) throws GLib.Error, Sparql.Error, IOError {
		var message = create_request (sparql);

		_session.send_message (message);

		if (cancellable != null && cancellable.is_cancelled ())
			throw new IOError.CANCELLED ("Operation was cancelled");

		return create_cursor (message);
	}

	public async override Sparql.Cursor query_async (string sparql, Cancellable? cancellable) throws GLib.Error, Sparql.Error, IOError {
		var message = create_request (sparql);

		yield _session.send_async (message, cancellable);

		return create_cursor (message);
	}

	public override void close () {
	}

	public async override bool close_async () throws GLib.IOError {
		return true;
	}
}
