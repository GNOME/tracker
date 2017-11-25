/*
 * Copyright (C) 2010, Codeminded BVBA <abustany@gnome.org>
 * Copyright (C) 2011, Nokia <ivan.frade@nokia.com>
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

[DBus (name = "org.freedesktop.Tracker1.Steroids")]
public class Tracker.Steroids : Object {
	public const string PATH = "/org/freedesktop/Tracker1/Steroids";

	public const int BUFFER_SIZE = 65536;

	public async string[] query (BusName sender, string query, UnixOutputStream output_stream) throws Error {
		var request = DBusRequest.begin (sender, "Steroids.Query");
		request.debug ("query: %s", query);
		try {
			string[] variable_names = null;
			var sparql_conn = Tracker.Main.get_sparql_connection ();

			yield Tracker.Store.sparql_query (sparql_conn, query, Priority.HIGH, cursor => {
				var data_output_stream = new DataOutputStream (new BufferedOutputStream.sized (output_stream, BUFFER_SIZE));
				data_output_stream.set_byte_order (DataStreamByteOrder.HOST_ENDIAN);

				int n_columns = cursor.n_columns;

				int[] column_sizes = new int[n_columns];
				int[] column_offsets = new int[n_columns];
				string[] column_data = new string[n_columns];

				variable_names = new string[n_columns];
				for (int i = 0; i < n_columns; i++) {
					variable_names[i] = cursor.get_variable_name (i);
				}

				while (cursor.next ()) {
					int last_offset = -1;

					for (int i = 0; i < n_columns ; i++) {
						unowned string str = cursor.get_string (i);

						column_sizes[i] = str != null ? str.length : 0;
						column_data[i]  = str;

						last_offset += column_sizes[i] + 1;
						column_offsets[i] = last_offset;
					}

					data_output_stream.put_int32 (n_columns);

					for (int i = 0; i < n_columns ; i++) {
						/* Cast from enum to int */
						data_output_stream.put_int32 ((int) cursor.get_value_type (i));
					}

					for (int i = 0; i < n_columns ; i++) {
						data_output_stream.put_int32 (column_offsets[i]);
					}

					for (int i = 0; i < n_columns ; i++) {
						data_output_stream.put_string (column_data[i] != null ? column_data[i] : "");
						data_output_stream.put_byte (0);
					}
				}
			}, sender);

			request.end ();

			return variable_names;
		} catch (Error e) {
			request.end (e);
			if (e is Sparql.Error) {
				throw e;
			} else {
				throw new Sparql.Error.INTERNAL (e.message);
			}
		}
	}

	async Variant? update_internal (BusName sender, int priority, bool blank, UnixInputStream input_stream) throws Error {
		var request = DBusRequest.begin (sender,
			"Steroids.%sUpdate%s",
			priority != Priority.HIGH ? "Batch" : "",
			blank ? "Blank" : "");
		try {
			size_t bytes_read;

			var data_input_stream = new DataInputStream (input_stream);
			data_input_stream.set_buffer_size (BUFFER_SIZE);
			data_input_stream.set_byte_order (DataStreamByteOrder.HOST_ENDIAN);

			int query_size = data_input_stream.read_int32 (null);

			/* We malloc one more char to ensure string is 0 terminated */
			uint8[] query = new uint8[query_size + 1];

			data_input_stream.read_all (query[0:query_size], out bytes_read);

			data_input_stream = null;

			request.debug ("query: %s", (string) query);
			var sparql_conn = Tracker.Main.get_sparql_connection ();

			if (!blank) {
				yield Tracker.Store.sparql_update (sparql_conn, (string) query, priority, sender);

				request.end ();

				return null;
			} else {
				var variant = yield Tracker.Store.sparql_update_blank (sparql_conn, (string) query, priority, sender);

				request.end ();

				return variant;
			}
		} catch (DBInterfaceError.NO_SPACE ie) {
			throw new Sparql.Error.NO_SPACE (ie.message);
		} catch (Error e) {
			request.end (e);
			if (e is Sparql.Error) {
				throw e;
			} else {
				throw new Sparql.Error.INTERNAL (e.message);
			}
		}
	}

	public async void update (BusName sender, UnixInputStream input_stream) throws Error {
		yield update_internal (sender, Priority.HIGH, false, input_stream);
	}

	public async void batch_update (BusName sender, UnixInputStream input_stream) throws Error {
		yield update_internal (sender, Priority.LOW, false, input_stream);
	}

	[DBus (signature = "aaa{ss}")]
	public async Variant update_blank (BusName sender, UnixInputStream input_stream) throws Error {
		return yield update_internal (sender, Priority.HIGH, true, input_stream);
	}

	[DBus (signature = "aaa{ss}")]
	public async Variant batch_update_blank (BusName sender, UnixInputStream input_stream) throws Error {
		return yield update_internal (sender, Priority.LOW, true, input_stream);
	}

	[DBus (signature = "as")]
	public async Variant update_array (BusName sender, UnixInputStream input_stream) throws Error {
		var request = DBusRequest.begin (sender, "Steroids.UpdateArray");
		try {
			var data_input_stream = new DataInputStream (input_stream);
			data_input_stream.set_buffer_size (BUFFER_SIZE);
			data_input_stream.set_byte_order (DataStreamByteOrder.HOST_ENDIAN);

			int query_count = data_input_stream.read_int32 ();

			var combined_query = new StringBuilder ();
			string[] query_array = new string[query_count];

			int i;
			for (i = 0; i < query_count; i++) {
				size_t bytes_read;

				int query_size = data_input_stream.read_int32 ();

				/* We malloc one more char to ensure string is 0 terminated */
				query_array[i] = (string) new uint8[query_size + 1];

				data_input_stream.read_all (((uint8[]) query_array[i])[0:query_size], out bytes_read);

				request.debug ("query: %s", query_array[i]);
				combined_query.append (query_array[i]);
			}

			data_input_stream = null;

			var builder = new VariantBuilder ((VariantType) "as");
			var sparql_conn = Tracker.Main.get_sparql_connection ();

			// first try combined query for best possible performance
			try {
				yield Tracker.Store.sparql_update (sparql_conn, combined_query.str, Priority.LOW, sender);

				// combined query was successful
				for (i = 0; i < query_count; i++) {
					builder.add ("s", "");
					builder.add ("s", "");
				}

				request.end ();

				return builder.end ();
			} catch {
				// combined query was not successful
				combined_query = null;
			}

			// combined query was not successful, try queries one by one
			for (i = 0; i < query_count; i++) {
				request.debug ("query: %s", query_array[i]);

				try {
					yield Tracker.Store.sparql_update (sparql_conn, query_array[i], Priority.LOW, sender);
					builder.add ("s", "");
					builder.add ("s", "");
				} catch (Error e1) {
					builder.add ("s", "org.freedesktop.Tracker1.SparqlError.Internal");
					builder.add ("s", e1.message);
				}

			}

			request.end ();

			return builder.end ();
		} catch (Error e) {
			request.end (e);
			if (e is Sparql.Error) {
				throw e;
			} else {
				throw new Sparql.Error.INTERNAL (e.message);
			}
		}
	}
}
