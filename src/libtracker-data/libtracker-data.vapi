/*
 * Copyright (C) 2008-2011, Nokia
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

namespace Tracker {
	[CCode (cheader_filename = "libtracker-data/tracker-db-manager.h")]
	public enum DB {
		UNKNOWN,
		COMMON,
		CACHE,
		METADATA,
		CONTENTS
	}

	[CCode (cprefix = "TRACKER_DB_", cheader_filename = "libtracker-data/tracker-db-interface.h")]
	public errordomain DBInterfaceError {
		QUERY_ERROR,
		CORRUPT,
		INTERRUPTED,
		OPEN_ERROR,
		NO_SPACE
	}

	[CCode (cheader_filename = "libtracker-data/tracker-db-journal.h")]
	public errordomain DBJournalError {
		UNKNOWN,
		DAMAGED_JOURNAL_ENTRY,
		COULD_NOT_WRITE,
		COULD_NOT_CLOSE,
		BEGIN_OF_JOURNAL
	}

	[CCode (cprefix = "TRACKER_DATA_BACKUP_ERROR_", cheader_filename = "libtracker-data/tracker-data-backup.h")]
	public errordomain DataBackupError {
		INVALID_URI
	}

	[CCode (cprefix = "TRACKER_DB_STATEMENT_CACHE_TYPE_", cheader_filename = "libtracker-data/tracker-db-interface.h")]
	public enum DBStatementCacheType {
		SELECT,
		UPDATE,
		NONE
	}

	[CCode (has_target = false, cheader_filename = "libtracker-data/tracker-db-interface-sqlite.h")]
	public delegate void DBWalCallback (DBInterface iface, int n_pages);

	[CCode (cheader_filename = "libtracker-data/tracker-db-interface.h")]
	public interface DBInterface : GLib.Object {
		[PrintfFormat]
		public DBStatement create_statement (DBStatementCacheType cache_type, ...) throws DBInterfaceError;
		[PrintfFormat]
		public void execute_query (...) throws DBInterfaceError;
		[CCode (cheader_filename = "libtracker-data/tracker-db-interface-sqlite.h")]
		public void sqlite_wal_hook (DBWalCallback callback);
		public void sqlite_wal_checkpoint (bool blocking) throws DBInterfaceError;
		public unowned GLib.Object get_user_data ();
	}

	[CCode (cheader_filename = "libtracker-data/tracker-data-update.h")]
	public delegate void BusyCallback (string status, double progress);

	[CCode (cprefix = "TRACKER_DB_MANAGER_", cheader_filename = "libtracker-data/tracker-db-manager.h")]
	public enum DBManagerFlags {
		FORCE_REINDEX,
		REMOVE_ALL,
		READONLY,
		DO_NOT_CHECK_ONTOLOGY,
		ENABLE_MUTEXES,
	}

	[CCode (cheader_filename = "libtracker-data/tracker-db-manager.h")]
	namespace DBManager {
		public void lock ();
		public bool trylock ();
		public void unlock ();
		public bool locale_changed () throws DBInterfaceError;
	}

	[CCode (cheader_filename = "libtracker-data/tracker-db-interface.h")]
	public class DBCursor : Sparql.Cursor {
	}

	[CCode (cheader_filename = "libtracker-data/tracker-db-interface.h")]
	public interface DBStatement : GLib.InitiallyUnowned {
		public abstract void bind_double (int index, double value);
		public abstract void bind_int (int index, int value);
		public abstract void bind_text (int index, string value);
		public abstract DBCursor start_cursor () throws DBInterfaceError;
		public abstract DBCursor start_sparql_cursor (PropertyType[] types, string[] variable_names) throws DBInterfaceError;
	}

	[CCode (cheader_filename = "libtracker-data/tracker-db-config.h")]
	public class DBConfig : ConfigFile {
		public DBConfig ();
		public bool save ();
		public int journal_chunk_size { get; set; }
		public string journal_rotate_destination { owned get; set; }
	}

	[CCode (cheader_filename = "libtracker-data/tracker-db-config.h")]
	namespace DBJournal {
		public void set_rotating (bool do_rotating, size_t chunk_size, string? rotate_to);
	}

	[CCode (cheader_filename = "libtracker-data/tracker-class.h")]
	public class Class : GLib.Object {
		public string name { get; set; }
		public string uri { get; set; }
		public int count { get; set; }
		[CCode (array_length = false, array_null_terminated = true)]
		public unowned Class[] get_super_classes ();
		public void transact_events ();
		public bool notify { get; set; }

		public bool has_insert_events ();
		public bool has_delete_events ();
		public void foreach_delete_event (EventsForeach func);
		public void foreach_insert_event (EventsForeach func);
		public void reset_ready_events ();
	}

	[CCode (cheader_filename = "libtracker-data/tracker-class.h")]
	public delegate void EventsForeach (int graph_id, int subject_id, int pred_id, int object_id);

	[CCode (cheader_filename = "libtracker-data/tracker-namespace.h")]
	public class Namespace : GLib.Object {
		public string prefix { get; set; }
		public string uri { get; set; }
	}

	[CCode (cheader_filename = "libtracker-data/tracker-property.h")]
	public class Property : GLib.Object {
		public string name { get; }
		public string table_name { get; }
		public string uri { get; set; }
		public PropertyType data_type { get; set; }
		public Class domain { get; set; }
		public Class range { get; set; }
		public bool multiple_values { get; set; }
		public bool is_inverse_functional_property { get; set; }
		[CCode (array_length = false, array_null_terminated = true)]
		public unowned Class[] get_domain_indexes ();
	}

	[CCode (cheader_filename = "libtracker-data/tracker-property.h")]
	public enum PropertyType {
		UNKNOWN,
		STRING,
		BOOLEAN,
		INTEGER,
		DOUBLE,
		DATE,
		DATETIME,
		RESOURCE
	}

	[CCode (cheader_filename = "libtracker-data/tracker-ontologies.h")]
	public class Ontologies : GLib.Object {
		public unowned Class get_class_by_uri (string class_uri);
		public unowned Property get_property_by_uri (string property_uri);
		public unowned Namespace[] get_namespaces ();
		public unowned Class[] get_classes ();
		public unowned Property[] get_properties ();
	}

	public delegate void StatementCallback (int graph_id, string? graph, int subject_id, string subject, int predicate_id, int object_id, string object, GLib.PtrArray rdf_types);
	public delegate void CommitCallback ();

	[CCode (lower_case_cprefix="tracker_data_", cname = "TrackerData", cheader_filename = "libtracker-data/tracker-data-query.h,libtracker-data/tracker-data-update.h")]
	public class Data.Update : GLib.Object {
		public void begin_db_transaction ();
		public void commit_db_transaction ();
		public void begin_transaction () throws DBInterfaceError;
		public void commit_transaction () throws DBInterfaceError;
		public void rollback_transaction ();
		public void update_sparql (string update) throws Sparql.Error;
		public GLib.Variant update_sparql_blank (string update) throws Sparql.Error;
		public void load_turtle_file (GLib.File file) throws Sparql.Error;
		public void delete_statement (string? graph, string subject, string predicate, string object) throws Sparql.Error, DateError;
		public void update_statement (string? graph, string subject, string predicate, string? object) throws Sparql.Error, DateError;
		public void insert_statement (string? graph, string subject, string predicate, string object) throws Sparql.Error, DateError;
		public void insert_statement_with_uri (string? graph, string subject, string predicate, string object) throws Sparql.Error;
		public void insert_statement_with_string (string? graph, string subject, string predicate, string object) throws Sparql.Error, DateError;
		public void update_buffer_flush () throws DBInterfaceError;
		public void update_buffer_might_flush () throws DBInterfaceError;
		public void sync ();

		public void add_insert_statement_callback (StatementCallback callback);
		public void add_delete_statement_callback (StatementCallback callback);
		public void add_commit_statement_callback (CommitCallback callback);
		public void add_rollback_statement_callback (CommitCallback callback);
		public void remove_insert_statement_callback (StatementCallback callback);
		public void remove_delete_statement_callback (StatementCallback callback);
		public void remove_commit_statement_callback (CommitCallback callback);
		public void remove_rollback_statement_callback (CommitCallback callback);
	}

	[CCode (cheader_filename = "libtracker-data/tracker-data-backup.h,libtracker-data/tracker-data-query.h")]
	namespace Data {
		public int query_resource_id (Data.Manager manager, DBInterface iface, string uri);
		public DBCursor query_sparql_cursor (Data.Manager manager, string query) throws Sparql.Error;

		public void backup_save (Data.Manager manager, GLib.File destination, GLib.File data_location, owned BackupFinished callback);
		public void backup_restore (Data.Manager manager, GLib.File journal, string? cache_location, string? data_location, GLib.File? ontology_location, BusyCallback busy_callback) throws GLib.Error;

		[CCode (cheader_filename = "libtracker-data/tracker-data-backup.h")]
		public delegate void BackupFinished (GLib.Error error);
	}

	[CCode (cheader_filename = "libtracker-data/tracker-data-manager.h", type_id = "TRACKER_TYPE_DATA_MANAGER")]
	public class Data.Manager : GLib.Object, GLib.Initable {
		public Manager (DBManagerFlags flags, GLib.File cache_location, GLib.File data_location, GLib.File ontology_location, bool journal_check, bool restoring_backup, uint select_cache_size, uint update_cache_size);
                public unowned Ontologies get_ontologies ();
		public unowned DBInterface get_db_interface ();
		public unowned DBInterface get_writable_db_interface ();
		public unowned DBInterface get_wal_db_interface ();
		public unowned Data.Update get_data ();
		public void shutdown ();
		public GLib.HashTable<string,string> get_namespaces ();
	}

	[CCode (cheader_filename = "libtracker-data/tracker-db-interface-sqlite.h")]
	public const string COLLATION_NAME;

	[CCode (cheader_filename = "libtracker-data/tracker-db-interface-sqlite.h")]
	public const string TITLE_COLLATION_NAME;

	[CCode (cheader_filename = "libtracker-data/tracker-collation.h")]
	public const unichar COLLATION_LAST_CHAR;
}
