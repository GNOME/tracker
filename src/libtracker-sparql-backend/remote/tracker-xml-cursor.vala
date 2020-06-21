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

public class Tracker.Remote.XmlCursor : Tracker.Sparql.Cursor {
	Xml.Node *_results;
	Xml.Node *_cur_row;
	HashTable<string, Xml.Node*> _cur_row_map;
	string [] _vars;

	const string XSD_NS = "http://www.w3.org/2001/XMLSchema#";

	private Xml.Node * find_first_child_node (Xml.Node *node) {
		for (Xml.Node* iter = node->children; iter != null; iter = iter->next) {
			if (iter->type != Xml.ElementType.ELEMENT_NODE)
				continue;
			return iter;
		}

		return null;
	}

	private Xml.Node * find_next_node (Xml.Node *node) {
		for (Xml.Node* iter = node->next; iter != null; iter = iter->next) {
			if (iter->type != Xml.ElementType.ELEMENT_NODE)
				continue;
			return iter;
		}

		return null;
	}

	private Xml.Node * lookup_child_node (Xml.Node *node, string name) {
		for (Xml.Node* iter = node->children; iter != null; iter = iter->next) {
			if (iter->type != Xml.ElementType.ELEMENT_NODE)
				continue;
			if (iter->name == name)
				return iter;
		}

		return null;
	}

	private Xml.Attr * lookup_attribute (Xml.Node *node, string name) {
		for (Xml.Attr* iter = node->properties; iter != null; iter = iter->next) {
			if (iter->name == name)
				return iter;
		}

		return null;
	}

	private void parse_vars (Xml.Node *vars) {
		for (Xml.Node* iter = vars->children; iter != null; iter = iter->next) {
			if (iter->name != "variable" ||
			    iter->type != Xml.ElementType.ELEMENT_NODE)
				continue;

			var attr = lookup_attribute (iter, "name");
			if (attr == null)
				continue;

			_vars += attr->children->content;
		}
	}

	public XmlCursor (string document, long length) throws Sparql.Error {
		Object ();
		Xml.Parser.init ();
		var doc = Xml.Parser.parse_memory (document, (int) length);

		if (doc == null)
			throw new Sparql.Error.INTERNAL ("Could not parse XML document");

		var root = doc->get_root_element ();
		_results = lookup_child_node (root, "results");

		var vars = lookup_child_node (root, "head");
		parse_vars (vars);
		Xml.Parser.cleanup ();

		_cur_row_map = new HashTable<string, Xml.Node*> (str_hash, str_equal);
	}

	public override int n_columns {
		get { return (int) _vars.length; }
	}

	public override Sparql.ValueType get_value_type (int column) requires (_cur_row != null) {
		var variable = _vars[column];
		Xml.Node* node = (Xml.Node*) _cur_row_map.get (variable);
		if (node == null)
			return Sparql.ValueType.UNBOUND;

		switch (node->children->name) {
		case "uri":
			return Sparql.ValueType.URI;
		case "bnode":
			return Sparql.ValueType.BLANK_NODE;
		case "literal":
			var attr = lookup_attribute (node, "datatype");
			if (attr == null)
				return Sparql.ValueType.STRING;

			switch (attr->children->content) {
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
		default:
			return Sparql.ValueType.STRING;
		}
	}

	public override unowned string? get_variable_name (int column) {
		if (column < 0 || column > _vars.length)
			return null;
		return _vars[column];
	}

	public override unowned string? get_string (int column, out long length = null) requires (_cur_row != null) {
		length = 0;

		var variable = _vars[column];
		Xml.Node* node = (Xml.Node*) _cur_row_map.get (variable);
		if (node == null)
			return null;

		var child = find_first_child_node (node);
		if (child == null)
			return null;

		var text = child->children;
		if (text == null ||
		    text->type != Xml.ElementType.TEXT_NODE)
			return null;

		length = text->content.length;
		return text->content;
	}

	public override bool next (Cancellable? cancellable = null) throws IOError, GLib.Error {
		if (_cur_row == null)
			_cur_row = find_first_child_node (_results);
		else
			_cur_row = find_next_node (_cur_row);

		_cur_row_map.remove_all ();

		if (_cur_row == null)
			return false;

		for (Xml.Node* iter = _cur_row->children; iter != null; iter = iter->next) {
			if (iter->name != "binding")
				continue;

			var attr = lookup_attribute (iter, "name");
			if (attr == null)
				continue;

			var binding_name = attr->children->content;
			_cur_row_map.insert (binding_name, iter);
		}

		return true;
	}

	public async override bool next_async (Cancellable? cancellable = null) throws IOError, GLib.Error {
		return next (cancellable);
	}

	public override void rewind () {
		_cur_row = null;
	}

	public override void close () {
	}
}
