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

	static Queue<Task> query_queues[3 /* TRACKER_STORE_N_PRIORITIES */];
	static Queue<Task> update_queues[3 /* TRACKER_STORE_N_PRIORITIES */];
	static int n_queries_running;
	static bool update_running;
	static ThreadPool<Task> update_pool;
	static ThreadPool<Task> query_pool;
	static ThreadPool<DBInterface> checkpoint_pool;
	static GenericArray<Task> running_tasks;
	static int max_task_time;
	static bool active;
	static SourceFunc active_callback;

	public enum Priority {
		HIGH,
		LOW,
		TURTLE,
		N_PRIORITIES
	}

	enum TaskType {
		QUERY,
		UPDATE,
		UPDATE_BLANK,
		TURTLE,
	}

	public delegate void SparqlQueryInThread (DBCursor cursor) throws Error;

	abstract class Task {
		public TaskType type;
		public string client_id;
		public Error error;
		public SourceFunc callback;
		public Tracker.Data.Manager data_manager;
	}

	class QueryTask : Task {
		public string query;
		public Cancellable cancellable;
		public uint watchdog_id;
		public unowned SparqlQueryInThread in_thread;

		~QueryTask () {
			if (watchdog_id > 0) {
				Source.remove (watchdog_id);
			}
		}
	}

	class UpdateTask : Task {
		public string query;
		public Variant blank_nodes;
		public Priority priority;
	}

	class TurtleTask : Task {
		public string path;
	}

	static void sched () {
		Task task = null;

		if (!active) {
			return;
		}

		while (n_queries_running < MAX_CONCURRENT_QUERIES) {
			for (int i = 0; i < Priority.N_PRIORITIES; i++) {
				task = query_queues[i].pop_head ();
				if (task != null) {
					break;
				}
			}
			if (task == null) {
				/* no pending query */
				break;
			}
			running_tasks.add (task);

			if (max_task_time != 0) {
				var query_task = (QueryTask) task;
				query_task.watchdog_id = Timeout.add_seconds (max_task_time, () => {
					query_task.cancellable.cancel ();
					query_task.watchdog_id = 0;
					return false;
				});
			}

			n_queries_running++;
			try {
				query_pool.add (task);
			} catch (Error e) {
				// ignore harmless thread creation error
			}
		}

		if (!update_running) {
			for (int i = 0; i < Priority.N_PRIORITIES; i++) {
				task = update_queues[i].pop_head ();
				if (task != null) {
					break;
				}
			}
			if (task != null) {
				update_running = true;
				try {
					update_pool.add (task);
				} catch (Error e) {
					// ignore harmless thread creation error
				}
			}
		}
	}

	static bool task_finish_cb (Task task) {
		var data = task.data_manager.get_data ();

		if (task.type == TaskType.QUERY) {
			var query_task = (QueryTask) task;

			if (task.error == null &&
			    query_task.cancellable != null &&
			    query_task.cancellable.is_cancelled ()) {
				task.error = new IOError.CANCELLED ("Operation was cancelled");
			}

			task.callback ();
			task.error = null;

			running_tasks.remove (task);
			n_queries_running--;
		} else if (task.type == TaskType.UPDATE || task.type == TaskType.UPDATE_BLANK) {
			if (task.error == null) {
				data.notify_transaction ();
			}

			task.callback ();
			task.error = null;

			update_running = false;
		} else if (task.type == TaskType.TURTLE) {
			if (task.error == null) {
				data.notify_transaction ();
			}

			task.callback ();
			task.error = null;

			update_running = false;
		}

		if (n_queries_running == 0 && !update_running && active_callback != null) {
			active_callback ();
		}

		sched ();

		return false;
	}

	static void pool_dispatch_cb (owned Task task) {
		try {
			if (task.type == TaskType.QUERY) {
				var query_task = (QueryTask) task;

				var cursor = Tracker.Data.query_sparql_cursor (task.data_manager, query_task.query);

				query_task.in_thread (cursor);
			} else {
				var data = task.data_manager.get_data ();
				var iface = task.data_manager.get_writable_db_interface ();
				iface.sqlite_wal_hook (wal_hook);

				if (task.type == TaskType.UPDATE) {
					var update_task = (UpdateTask) task;

					data.update_sparql (update_task.query);
				} else if (task.type == TaskType.UPDATE_BLANK) {
					var update_task = (UpdateTask) task;

					update_task.blank_nodes = data.update_sparql_blank (update_task.query);
				} else if (task.type == TaskType.TURTLE) {
					var turtle_task = (TurtleTask) task;

					var file = File.new_for_path (turtle_task.path);

					data.load_turtle_file (file);
				}
			}
		} catch (Error e) {
			task.error = e;
		}

		Idle.add (() => {
			task_finish_cb (task);
			return false;
		});
	}

	public static void wal_checkpoint (DBInterface iface, bool blocking) {
		try {
			debug ("Checkpointing database...");
			iface.sqlite_wal_checkpoint (blocking);
			debug ("Checkpointing complete...");
		} catch (Error e) {
			warning (e.message);
		}
	}

	static int checkpointing;

	static void wal_hook (DBInterface iface, int n_pages) {
		// run in update thread
		var manager = (Data.Manager) iface.get_user_data ();
		var wal_iface = manager.get_wal_db_interface ();

		debug ("WAL: %d pages", n_pages);

		if (n_pages >= 10000) {
			// do immediate checkpointing (blocking updates)
			// to prevent excessive wal file growth
			wal_checkpoint (wal_iface, true);
		} else if (n_pages >= 1000 && checkpoint_pool != null) {
			if (AtomicInt.compare_and_exchange (ref checkpointing, 0, 1)) {
				// initiate asynchronous checkpointing (not blocking updates)
				try {
					checkpoint_pool.push (wal_iface);
				} catch (Error e) {
					warning (e.message);
					AtomicInt.set (ref checkpointing, 0);
				}
			}
		}
	}

	static void checkpoint_dispatch_cb (DBInterface iface) {
		// run in checkpoint thread
		wal_checkpoint (iface, false);
		AtomicInt.set (ref checkpointing, 0);
	}

	public static void init () {
		string max_task_time_env = Environment.get_variable ("TRACKER_STORE_MAX_TASK_TIME");
		if (max_task_time_env != null) {
			max_task_time = int.parse (max_task_time_env);
		} else {
			max_task_time = MAX_TASK_TIME;
		}

		running_tasks = new GenericArray<Task> ();

		for (int i = 0; i < Priority.N_PRIORITIES; i++) {
			query_queues[i] = new Queue<Task> ();
			update_queues[i] = new Queue<Task> ();
		}

		try {
			update_pool = new ThreadPool<Task>.with_owned_data (pool_dispatch_cb, 1, true);
			query_pool = new ThreadPool<Task>.with_owned_data (pool_dispatch_cb, MAX_CONCURRENT_QUERIES, true);
			checkpoint_pool = new ThreadPool<DBInterface> (checkpoint_dispatch_cb, 1, true);
		} catch (Error e) {
			warning (e.message);
		}

		/* as the following settings are global for unknown reasons,
		   let's use the same settings as gio, otherwise the used settings
		   are rather random */
		ThreadPool.set_max_idle_time (15 * 1000);
		ThreadPool.set_max_unused_threads (2);
	}

	public static void shutdown () {
		query_pool = null;
		update_pool = null;
		checkpoint_pool = null;

		for (int i = 0; i < Priority.N_PRIORITIES; i++) {
			query_queues[i] = null;
			update_queues[i] = null;
		}
	}

	public static async void sparql_query (Tracker.Data.Manager manager, string sparql, Priority priority, SparqlQueryInThread in_thread, string client_id) throws Error {
		var task = new QueryTask ();
		task.type = TaskType.QUERY;
		task.query = sparql;
		task.cancellable = new Cancellable ();
		task.in_thread = in_thread;
		task.callback = sparql_query.callback;
		task.client_id = client_id;
		task.data_manager = manager;

		query_queues[priority].push_tail (task);

		sched ();

		yield;

		if (task.error != null) {
			throw task.error;
		}
	}

	public static async void sparql_update (Tracker.Data.Manager manager, string sparql, Priority priority, string client_id) throws Error {
		var task = new UpdateTask ();
		task.type = TaskType.UPDATE;
		task.query = sparql;
		task.priority = priority;
		task.callback = sparql_update.callback;
		task.client_id = client_id;
		task.data_manager = manager;

		update_queues[priority].push_tail (task);

		sched ();

		yield;

		if (task.error != null) {
			throw task.error;
		}
	}

	public static async Variant sparql_update_blank (Tracker.Data.Manager manager, string sparql, Priority priority, string client_id) throws Error {
		var task = new UpdateTask ();
		task.type = TaskType.UPDATE_BLANK;
		task.query = sparql;
		task.priority = priority;
		task.callback = sparql_update_blank.callback;
		task.client_id = client_id;
		task.data_manager = manager;

		update_queues[priority].push_tail (task);

		sched ();

		yield;

		if (task.error != null) {
			throw task.error;
		}

		return task.blank_nodes;
	}

	public static async void queue_turtle_import (Tracker.Data.Manager manager, File file, string client_id) throws Error {
		var task = new TurtleTask ();
		task.type = TaskType.TURTLE;
		task.path = file.get_path ();
		task.callback = queue_turtle_import.callback;
		task.client_id = client_id;
		task.data_manager = manager;

		update_queues[Priority.TURTLE].push_tail (task);

		sched ();

		yield;

		if (task.error != null) {
			throw task.error;
		}
	}

	public uint get_queue_size () {
		uint result = 0;

		for (int i = 0; i < Priority.N_PRIORITIES; i++) {
			result += query_queues[i].get_length ();
			result += update_queues[i].get_length ();
		}
		return result;
	}

	public static void unreg_batches (string client_id) {
		unowned List<Task> list, cur;
		unowned Queue<Task> queue;

		for (int i = 0; i < running_tasks.length; i++) {
			unowned QueryTask task = running_tasks[i] as QueryTask;
			if (task != null && task.client_id == client_id && task.cancellable != null) {
				task.cancellable.cancel ();
			}
		}

		for (int i = 0; i < Priority.N_PRIORITIES; i++) {
			queue = query_queues[i];
			list = queue.head;
			while (list != null) {
				cur = list;
				list = list.next;
				unowned Task task = cur.data;

				if (task != null && task.client_id == client_id) {
					queue.delete_link (cur);

					task.error = new DBusError.FAILED ("Client disappeared");
					task.callback ();
				}
			}

			queue = update_queues[i];
			list = queue.head;
			while (list != null) {
				cur = list;
				list = list.next;
				unowned Task task = cur.data;

				if (task != null && task.client_id == client_id) {
					queue.delete_link (cur);

					task.error = new DBusError.FAILED ("Client disappeared");
					task.callback ();
				}
			}
		}

		sched ();
	}

	public static async void pause () {
		Tracker.Store.active = false;

		if (n_queries_running > 0 || update_running) {
			active_callback = pause.callback;
			yield;
			active_callback = null;
		}

		if (AtomicInt.get (ref checkpointing) != 0) {
			// this will wait for checkpointing to finish
			checkpoint_pool = null;
			try {
				checkpoint_pool = new ThreadPool<DBInterface> (checkpoint_dispatch_cb, 1, true);
			} catch (Error e) {
				warning (e.message);
			}
		}

		if (active) {
			sched ();
		}
	}

	public static void resume () {
		Tracker.Store.active = true;

		sched ();
	}
}
