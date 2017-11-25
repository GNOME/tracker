/*
 * Copyright (C) 2011, Nokia <ivan.frade@nokia.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.          See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 */

namespace Tracker {
	[CCode (array_null_terminated = true, has_target = false, cheader_filename = "tracker-store/tracker-writeback.h")]
	public delegate string[] WritebackGetPredicatesFunc ();

	[CCode (cheader_filename = "tracker-store/tracker-writeback.h")]
	namespace Writeback {
		public void init (Tracker.Data.Manager data_manager, WritebackGetPredicatesFunc callback);
		public void shutdown ();
		public void check (int graph_id, string graph, int subject_id, string subject, int pred_id, int object_id, string object, GLib.PtrArray rdf_types);
		public GLib.HashTable<int, GLib.Array<int>> get_ready ();
		public void reset_pending ();
		public void transact ();
	}
}
