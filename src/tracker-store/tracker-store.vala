/*
 * Copyright (C) 2009-2011, Nokia <ivan.frade@nokia.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 *
 * Author: Philip Van Hoof <philip@codeminded.be>
 */

public class Tracker.Store {
	const int MAX_CONCURRENT_QUERIES = 2;

	const int MAX_TASK_TIME = 30;
	const int GRAPH_UPDATED_IMMEDIATE_EMIT_AT = 50000;

	static int max_task_time;
	static bool active;

	static Tracker.Config config;
	static uint signal_timeout;
	static int n_updates;

	static HashTable<string, Cancellable> client_cancellables;

	public delegate void SignalEmissionFunc (HashTable<Tracker.Class, Tracker.Events.Batch>? graph_updated, HashTable<int, GLib.Array<int>>? writeback);
	static unowned SignalEmissionFunc signal_callback;

	public delegate void SparqlQueryInThread (Sparql.Cursor cursor) throws Error;

	public delegate void StateCallback ();
	static unowned StateCallback idle_cb;
	static unowned StateCallback busy_cb;
	static bool busy;

	class CursorTask {
		public Sparql.Cursor cursor;
		public unowned SourceFunc callback;
		public unowned SparqlQueryInThread thread_func;
		public Error error;

		public CursorTask (Sparql.Cursor cursor) {
			this.cursor = cursor;
		}
	}

	static ThreadPool<CursorTask> cursor_pool;

	private static void cursor_dispatch_cb (owned CursorTask task) {
		try {
			task.thread_func (task.cursor);
		} catch (Error e) {
			task.error = e;
		}

		Idle.add (() => {
			task.callback ();
			update_state ();
			return false;
		});
	}

	public static void init (Tracker.Config config_p, StateCallback idle, StateCallback busy) {
		string max_task_time_env = Environment.get_variable ("TRACKER_STORE_MAX_TASK_TIME");
		if (max_task_time_env != null) {
			max_task_time = int.parse (max_task_time_env);
		} else {
			max_task_time = MAX_TASK_TIME;
		}

		client_cancellables = new HashTable <string, Cancellable> (str_hash, str_equal);

		try {
			cursor_pool = new ThreadPool<CursorTask>.with_owned_data (cursor_dispatch_cb, 16, false);
		} catch (Error e) {
			// Ignore harmless error
		}

		/* as the following settings are global for unknown reasons,
		   let's use the same settings as gio, otherwise the used settings
		   are rather random */
		ThreadPool.set_max_idle_time (15 * 1000);
		ThreadPool.set_max_unused_threads (2);

		config = config_p;
		idle_cb = idle;
		busy_cb = busy;
		idle_cb ();
	}

	public static void shutdown () {
		if (signal_timeout != 0) {
			Source.remove (signal_timeout);
			signal_timeout = 0;
		}
	}

	private static Cancellable create_cancellable (string client_id) {
		var client_cancellable = client_cancellables.lookup (client_id);

		if (client_cancellable == null) {
			client_cancellable = new Cancellable ();
			client_cancellables.insert (client_id, client_cancellable);
		}

		var task_cancellable = new Cancellable ();
		client_cancellable.connect (() => {
			task_cancellable.cancel ();
		});

		return task_cancellable;
	}

	private static void do_emit_signals () {
		signal_callback (Tracker.Events.get_pending (), Tracker.Writeback.get_ready ());
	}

	private static void ensure_signal_timeout () {
		if (signal_timeout == 0) {
			signal_timeout = Timeout.add (config.graphupdated_delay, () => {
				do_emit_signals ();
				if (n_updates == 0) {
					signal_timeout = 0;
					return false;
				} else {
					return true;
				}
			});
		}
	}

	public static async void sparql_query (Tracker.Direct.Connection conn, string sparql, int priority, SparqlQueryInThread in_thread, string client_id) throws Error {
		var cancellable = create_cancellable (client_id);
		uint timeout_id = 0;

		if (max_task_time != 0) {
			timeout_id = Timeout.add_seconds (max_task_time, () => {
				cancellable.cancel ();
				timeout_id = 0;
				return false;
			});
		}

		var cursor = yield conn.query_async (sparql, cancellable);

		if (timeout_id != 0)
			GLib.Source.remove (timeout_id);

		var task = new CursorTask (cursor);
		task.thread_func = in_thread;
		task.callback = sparql_query.callback;

		try {
			cursor_pool.add (task);
		} catch (Error e) {
			// Ignore harmless error
		}

		update_state ();

		yield;

		if (task.error != null)
			throw task.error;
	}

	private static void pre_update () {
		n_updates++;
		update_state ();
		ensure_signal_timeout ();
	}

	private static void post_update () {
		n_updates--;
		update_state ();
	}

	public static async void sparql_update (Tracker.Direct.Connection conn, string sparql, int priority, string client_id) throws Error {
		if (!active)
			throw new Sparql.Error.UNSUPPORTED ("Store is not active");
		pre_update ();
		var cancellable = create_cancellable (client_id);
		yield conn.update_async (sparql, priority, cancellable);
		post_update ();
	}

	public static async Variant sparql_update_blank (Tracker.Direct.Connection conn, string sparql, int priority, string client_id) throws Error {
		if (!active)
			throw new Sparql.Error.UNSUPPORTED ("Store is not active");
		pre_update ();
		var cancellable = create_cancellable (client_id);
		var nodes = yield conn.update_blank_async (sparql, priority, cancellable);
		post_update ();

		return nodes;
	}

	public static async void queue_turtle_import (Tracker.Direct.Connection conn, File file, string client_id) throws Error {
		if (!active)
			throw new Sparql.Error.UNSUPPORTED ("Store is not active");
		pre_update ();
		var cancellable = create_cancellable (client_id);
		yield conn.load_async (file, cancellable);
		post_update ();
	}

	public static void unreg_batches (string client_id) {
		Cancellable cancellable = client_cancellables.lookup (client_id);

		if (cancellable != null) {
			cancellable.cancel ();
			client_cancellables.remove (client_id);
			update_state ();
		}
	}

	public static async void pause () {
		Tracker.Store.active = false;

		var sparql_conn = Tracker.Main.get_sparql_connection ();
		sparql_conn.sync ();
	}

	public static void resume () {
		Tracker.Store.active = true;
	}

	private static void update_state () {
		bool cur_busy;

		cur_busy = (!Tracker.Store.active ||          /* Keep busy while paused */
		            n_updates != 0 ||                 /* There are updates */
		            cursor_pool.unprocessed () > 0 || /* Select cursor pool is busy */
		            cursor_pool.get_num_threads () > 0);

		if (busy == cur_busy)
			return;

		busy = cur_busy;
		if (busy)
			busy_cb ();
		else
			idle_cb ();
	}

	private static void on_statements_committed () {
		Tracker.Events.transact ();
		Tracker.Writeback.transact ();
		check_graph_updated_signal ();
		update_state ();
	}

	private static void on_statements_rolled_back () {
		Tracker.Events.reset_pending ();
		Tracker.Writeback.reset_pending ();
	}

	private static void check_graph_updated_signal () {
		/* Check for whether we need an immediate emit */
		if (Tracker.Events.get_total () > GRAPH_UPDATED_IMMEDIATE_EMIT_AT) {
			// immediately emit signals for already committed transaction
			Idle.add (() => {
				do_emit_signals ();
				return false;
			});
		}
	}

	private static void on_statement_inserted (int graph_id, string? graph, int subject_id, string subject, int pred_id, int object_id, string? object, PtrArray rdf_types) {
		Tracker.Events.add_insert (graph_id, subject_id, subject, pred_id, object_id, object, rdf_types);
		Tracker.Writeback.check (graph_id, graph, subject_id, subject, pred_id, object_id, object, rdf_types);
	}

	private static void on_statement_deleted (int graph_id, string? graph, int subject_id, string subject, int pred_id, int object_id, string? object, PtrArray rdf_types) {
		Tracker.Events.add_delete (graph_id, subject_id, subject, pred_id, object_id, object, rdf_types);
		Tracker.Writeback.check (graph_id, graph, subject_id, subject, pred_id, object_id, object, rdf_types);
	}

	public static void enable_signals () {
		var data_manager = Tracker.Main.get_data_manager ();
		var data = data_manager.get_data ();
		data.add_insert_statement_callback (on_statement_inserted);
		data.add_delete_statement_callback (on_statement_deleted);
		data.add_commit_statement_callback (on_statements_committed);
		data.add_rollback_statement_callback (on_statements_rolled_back);
	}

	public static void disable_signals () {
		var data_manager = Tracker.Main.get_data_manager ();
		var data = data_manager.get_data ();
		data.remove_insert_statement_callback (on_statement_inserted);
		data.remove_delete_statement_callback (on_statement_deleted);
		data.remove_commit_statement_callback (on_statements_committed);
		data.remove_rollback_statement_callback (on_statements_rolled_back);
	}

	public static void set_signal_callback (SignalEmissionFunc? func) {
		signal_callback = func;
	}
}
