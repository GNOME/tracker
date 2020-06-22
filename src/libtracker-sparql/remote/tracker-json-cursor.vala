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

public class Tracker.Remote.JsonCursor : Tracker.Sparql.Cursor {
	internal Json.Parser _parser;
	internal Json.Array _vars;
	internal Json.Array _results;
	internal Json.Object _cur_row;
	internal uint _cur_idx = 0;
	internal bool _started_iterating = false;

	const string XSD_NS = "http://www.w3.org/2001/XMLSchema#";

	public JsonCursor (string document, long length) throws GLib.Error {
		Object ();
		var parser = new Json.Parser ();
		parser.load_from_data (document, length);

		var root = parser.get_root ().get_object ();
		var head = root.get_object_member ("head");
		var results = root.get_object_member ("results");

		_parser = parser;
		_vars = head.get_array_member ("vars");
		_results = results.get_array_member ("bindings");
		_started_iterating = false;
	}

	public override int n_columns {
		get { return (int) _vars.get_length (); }
	}

	public override Sparql.ValueType get_value_type (int column) requires (_cur_row != null) {
		var col_node = _cur_row.get_member (get_variable_name (column));

		if (col_node == null)
			return Sparql.ValueType.UNBOUND;

		var object = col_node.get_object ();//_cur_row.get_object_member (get_variable_name (column));
		unowned string str = object.get_string_member ("type");

		switch (str) {
		case "uri":
			return Sparql.ValueType.URI;
		case "bnode":
			return Sparql.ValueType.BLANK_NODE;
		case "literal": {
			var node = object.get_member ("datatype");

			if (node == null)
				return Sparql.ValueType.STRING;

			str = node.get_string ();

			switch (str) {
			case XSD_NS + "byte":
			case XSD_NS + "int":
			case XSD_NS + "integer":
			case XSD_NS + "long":
				return Sparql.ValueType.INTEGER;
			case XSD_NS + "decimal":
			case XSD_NS + "double":
				return Sparql.ValueType.DOUBLE;
			case XSD_NS + "dateTime":
				return Sparql.ValueType.DATETIME;
			default:
				return Sparql.ValueType.STRING;
			}
		}

		default:
			return Sparql.ValueType.STRING;
		}
	}

	public override unowned string? get_variable_name (int column) {
		return _vars.get_string_element (column);
	}

	public override unowned string? get_string (int column, out long length = null) requires (_cur_row != null) {
		var col_node = _cur_row.get_member (get_variable_name (column));
		length = 0;

		if (col_node == null)
			return null;

		var object = col_node.get_object ();

		if (object == null)
			return null;

		unowned string str = object.get_string_member ("value");
		length = str.length;
		return str;
	}

	public override bool next (Cancellable? cancellable = null) throws IOError, GLib.Error {
		if (_started_iterating)
			_cur_idx++;

		if (_cur_idx >= _results.get_length ())
			return false;

		if (cancellable != null && cancellable.is_cancelled ())
			throw new IOError.CANCELLED ("Operation was cancelled");

		_started_iterating = true;
		_cur_row = _results.get_object_element (_cur_idx);
		return true;
	}

	public async override bool next_async (Cancellable? cancellable = null) throws IOError, GLib.Error {
		return next (cancellable);
	}

	public override void rewind () {
		_started_iterating = false;
	}

	public override void close () {
	}
}
