[CCode (cprefix = "Tracker", gir_namespace = "Tracker", gir_version = "2.0", lower_case_cprefix = "tracker_")]
namespace Tracker {
        namespace Direct {
                [CCode (cheader_filename = "libtracker-direct/tracker-direct.h")]
                public class Connection : Tracker.Sparql.Connection, GLib.Initable, GLib.AsyncInitable {
                        public Connection (Tracker.Sparql.ConnectionFlags connection_flags, GLib.File loc, GLib.File? journal, GLib.File? ontology) throws Tracker.Sparql.Error, GLib.IOError, GLib.DBusError;
                        public Tracker.Data.Manager get_data_manager ();
			public void sync ();
			public static void set_default_flags (Tracker.DBManagerFlags flags);
                }
        }
}
