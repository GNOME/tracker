using Tracker;
using Tracker.Sparql;

int
main( string[] args )
{
	try {
		TestApp app = new TestApp (new  Tracker.Bus.Connection ("org.freedesktop.Tracker1", null, true));

		return app.run ();
	} catch (GLib.Error e) {
		warning ("Couldn't perform test: %s", e.message);
		return 1;
	}
}
