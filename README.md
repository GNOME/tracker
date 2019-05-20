# Tracker

Tracker is a search engine and that allows the user to find their
data as fast as possible. Users can search for their files and
search for content in their files too.  

Tracker is a semantic data storage for desktop and mobile devices.
Tracker uses W3C standards for RDF ontologies using Nepomuk with
SPARQL to query and update the data.

Tracker is a central repository of user information, that provides
two big benefits for the desktop; shared data between applications
and information which is relational to other information (for
example: mixing contacts with files, locations, activities and
etc.).

This central repository works with a well defined data model that
applications can rely on to store and recover their information.
That data model is defined using a semantic web artifact called
ontology. An ontology defines the relationships between the
information stored in the repository.

An EU-funded project called Nepomuk was started to define some of
the core ontologies to be modeled on the Desktop. Tracker uses this
to define the data's relationships in a database.

All discussion related to tracker happens on the
[Tracker mailing list](https://mail.gnome.org/mailman/listinfo/tracker-list).

IRC channel #tracker on: [irc.gimp.net](irc://irc.gimp.net)

Development happens on:
[gitlab.gnome.org](https://gitlab.gnome.org/GNOME/tracker).

More infomation on Tracker can be found on
[the project wiki](https://wiki.gnome.org/Projects/Tracker).

## Use Cases

Tracker is the most powerful open source metadata database and
indexer framework currently available and because it is built
around a combination indexer and SQL database and not a
dedicated indexer, it has much more powerful use cases:

  * Provide search and indexing facilities similar to those on
  other systems (Windows Vista and Mac OS X).

  * Common database storage for all first class objects (e.g. a
  common music/photo/contacts/email/bookmarks/history database)
  complete with additional metadata and tags/keywords.

  * Comprehensive one stop solution for all applications needing
  an object database, powerful search (via RDF Query), first class
  methods, related metadata and user-definable metadata/tags.

  * Can provide a full semantic desktop with metadata everywhere.

  * Can provide powerful criteria-based searching suitable for
  creating smart file dialogs and vfolder systems.

  * Can provide a more intelligent desktop using statistical
  metadata.

## Features

  * Desktop-neutral design (it's a freedesktop product built
  around other freedesktop technologies like D-Bus and XDGMime
  but contains no GNOME-specific dependencies besides GLib).

  * Very memory efficient. Unlike some other indexers, Tracker is
  designed and built to run well on mobile and desktop systems with
  lower memory (256MB or less).

  * Non-bloated and written in C for maximum efficiency.

  * Small size and minimal dependencies makes it easy to bundle
  into various distros, including live CDs.

  * Provides option to disable indexing when running on battery.

  * Provides option to index removable devices.

  * Implements the freedesktop specification for metadata
  (https://freedesktop.org/wiki/Standards/shared-filemetadata-spec/).

  * Extracts embedded File, Image, Document and Audio type
  metadata from files.

  * Supports the WC3's RDF Query syntax for querying metadata.

  * Provides support for both free text search (like Beagle/Google)
  as well as structured searches using RDF Query.

  * Responds in real time to file system changes to keep its
  metadata database up to date and in sync.

  * Fully extensible with custom metadata - you can store,
  retrieve, register and search via RDF Query all your own custom
  metadata.

  * Can extract a file's contents as plain text and index them.

  * Can provide thumbnailing on the fly.

  * It auto-pauses indexing when running low on diskspace.

## Compilation

To setup the project for compilation after checking it out from
the git repository, use:

        meson build --prefix=/usr --sysconfdir=/etc

To start compiling the project use:

        ninja -C build
        ninja install

If you install using any other prefix, you might have problems
with files not being installed correctly. (You may need to copy
and amend the dbus service file to the correct directory and/or
might need to update ld_conf if you install into non-standard
directories.)

## Running Tracker

### Usage

Tracker normally starts itself when users log in. You can indexing by running:

    $prefix/libexec/tracker-miner-fs

You can configure how this works using:

    $prefix/bin/tracker-preferences

You can monitor data miners using:

    $prefix/bin/tracker-status-icon

You can do simple searching using an applet:

    $prefix/libexec/tracker-search-bar

You can do more extensive searching using:

    $prefix/bin/tracker-search-tool

### Setting Inotify Watch Limit

When watching large numbers of folders, its possible to exceed
the default number of inotify watches. In order to get real time
updates when this value is exceeded it is necessary to increase
the number of allowed watches. This can be done as follows:

  1. Add this line to /etc/sysctl.conf:
     "fs.inotify.max_user_watches = (number of folders to be
      watched; default used to be 8192 and now is 524288)"

  2. Reboot the system OR (on a Debian-like system) run
     "sudo /etc/init.d/procps restart"

## Further Help

### Man pages

Every config file and every binary has a man page. If you start with
tracker-store, you should be able to find out about most other
commands on the SEE ALSO section.

### Utilities

There are a range of tracker utilities that help you query for data.

