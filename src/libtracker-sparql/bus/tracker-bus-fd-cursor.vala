/*
 * Copyright (C) 2010, Nokia <ivan.frade@nokia.com>
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

class Tracker.Bus.FDCursor : Tracker.Sparql.Cursor {
	internal char* buffer;
	internal ulong buffer_index;
	internal ulong buffer_size;

	internal int _n_columns;
	internal int* offsets;
	internal int* types;
	internal char* data;
	internal string[] variable_names;
	internal bool cursor_finished;

	public FDCursor (char* buffer, ulong buffer_size, string[] variable_names) {
		Object ();
		this.buffer = buffer;
		this.buffer_size = buffer_size;
		this.variable_names = variable_names;
		this.cursor_finished = true;
		_n_columns = variable_names.length;
	}

	~FDCursor () {
		free (buffer);
	}

	inline int buffer_read_int () {
		int v = *((int*) (buffer + buffer_index));

		buffer_index += 4;

		return v;
	}

	public override int n_columns {
		get { return _n_columns; }
	}

	public override Sparql.ValueType get_value_type (int column)
	requires (types != null) {
		if (column >= n_columns) {
			return Sparql.ValueType.UNBOUND;
		}

		/* Cast from int to enum */
		return (Sparql.ValueType) types[column];
	}

	public override unowned string? get_variable_name (int column)
	requires (variable_names != null) {
		return variable_names[column];
	}

	public override unowned string? get_string (int column, out long length = null)
	requires (cursor_finished == false) {
		unowned string str = null;

		if (column >= n_columns) {
			length = 0;
			return null;
		}

		// return null instead of empty string for unbound values
		if (types[column] == Sparql.ValueType.UNBOUND) {
			length = 0;
			return null;
		}

		if (column == 0) {
			str = (string) data;
		} else {
			str = (string) (data + offsets[column - 1] + 1);
		}

		length = str.length;

		return str;
	}

	public override bool next (Cancellable? cancellable = null) throws GLib.Error {
		int last_offset;

		if (cancellable != null && cancellable.is_cancelled ()) {
			throw new IOError.CANCELLED ("Operation was cancelled");
		}

		if (buffer_index >= buffer_size) {
			cursor_finished = true;
			data = null;
			return false;
		}

		/* So, the make up on each cursor segment is:
		 *
		 * iteration = [4 bytes for number of columns,
		 *              columns x 4 bytes for types
		 *              columns x 4 bytes for offsets]
		 */

		_n_columns = buffer_read_int ();

		/* Storage of ints that will be cast to TrackerSparqlValueType enums,
		 * also see get_value_type */
		types = (int*) (buffer + buffer_index);
		buffer_index += sizeof (int) * n_columns;

		offsets = (int*) (buffer + buffer_index);
		buffer_index += sizeof (int) * (n_columns - 1);
		last_offset = buffer_read_int ();

		data = buffer + buffer_index;
		cursor_finished = false;

		buffer_index += last_offset + 1;

		return true;
	}

	public override async bool next_async (Cancellable? cancellable = null) throws GLib.Error {
		// next never blocks
		return next (cancellable);
	}

	public override void rewind () {
		buffer_index = 0;
		data = buffer;
		cursor_finished = false;
	}

        public override void close () {
        }
}
