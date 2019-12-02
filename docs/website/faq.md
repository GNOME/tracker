# FAQ

## Why is Tracker consuming lots of resources?

Tracker automatically indexes content in your home directory.
Tracker aims to perform well even on large collections of content, but there is
a limit to how much it can index without causing noticably high CPU and IO
usage.

The problem may be:

  * A very large amount of content being indexed by Tracker. See [How can I control what Tracker indexes?].
  * A bug in Tracker or one of its dependencies

## How can I control what Tracker indexes?

In GNOME, you can use the Search Settings panel to control what Tracker
indexes. See [GNOME's documentation](https://help.gnome.org/users/gnome-help/unstable/files-search.html.en).

You can control Tracker's configuration directly using
[dconf-editor](https://wiki.gnome.org/Apps/DconfEditor) or the `gsettings` CLI
tool.
The relevant schemas are `org.freedesktop.Tracker.Miner.Files` and
`org.freedesktop.Tracker.Extract`.

## How can I disable Tracker in GNOME?

Tracker is a core dependency of GNOME, and some things will not work as
expected if you disable it completely.

If you are experiencing performance problems, you should first check that
Tracker is only indexing data that you care about.

In case you still see unwanted resource usage, you can tell Tracker not
to index any directories on your system at all.
This should bring resource usage close to zero. The Tracker daemons
will still be started, but they should remain idle -- if they don't,
this suggests there is a bug and we would appreciate if you report a bug.

If the Tracker store is using a lot of disk space and you are sure that
there is no unreplaceable data stored in the database, you can run `tracker
reset --hard` to delete everything stored in the database.  

## Can Tracker help me organize my music collection?

At present, Tracker simply reads the metadata stores in your music files (often
called 'tags').

You may be able to use Gnome Music to correct this metadata using the
Musicbrainz database.

Programs that fix tags and organize music collections on disk, such as
[Beets](http://beets.io/), work well with Tracker.

[How can I control what Tracker indexes?]: #how-can-i-control-what-tracker-indexes
