# FAQ

## What is Tracker?

It's a search engine, and a database.

Tracker indexes content from your home directory automatically, so applications
can provide instant search results when you need them.

See the [overview](/overview) for more information.

## What files will Tracker index?

The default configuration of Tracker is to look at files and folders in your
XDG content directories such as `Documents`, `Music`, `Pictures` and `Videos`.
It also looks at files in your home directory and `Downloads` directory, but
it doesn't recurse into folders.

You might want to [control what Tracker indexes] so that it finds files in
other places too.

## How can I control what Tracker indexes?

In GNOME, you can use the Search Settings panel to control what Tracker
indexes. See [GNOME's documentation](https://help.gnome.org/users/gnome-help/unstable/files-search.html.en).

You can control Tracker's configuration directly using
[dconf-editor](https://wiki.gnome.org/Apps/DconfEditor) or the `gsettings` CLI
tool.
The relevant schemas are `org.freedesktop.Tracker.Miner.Files` and
`org.freedesktop.Tracker.Extract`.

## Why does Tracker consume resources on my PC?

When you add or edit files, Tracker will update its index. This should be very
quick, but if a huge number of files are added then it may cause noticably high
CPU and IO usage until the new files have been indexed. This is normal.

Tracker is not designed to index directories of source code or video game data.
If content like this appears in the locations Tracker is configured to index
then you might see unwanted high resource usage.

If you see high resource usage from Tracker even when no files have changed on
disk, this probably indicates a bug in Tracker or one of its dependencies.
We can [work together](/community) to find out what the problem is.

## How can I disable Tracker in GNOME?

Tracker is a core dependency of GNOME, and some things will not work as
expected if you disable it completely.

If you are experiencing performance problems, see [Why does Tracker consume
resources on my PC?](#why-does-tracker-consume-resources-on-my-pc).

In case of a bug you may need to temporarily stop Tracker indexing.
The simplest way is to [edit the
configuration](#how-can-i-control-what-tracker-indexes) so that no directories
are indexed. This should bring resource usage to zero.

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

[control what Tracker indexes]: #how-can-i-control-what-tracker-indexes
