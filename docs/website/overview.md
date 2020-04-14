# Overview

## What is Tracker?

Tracker is an efficient search engine and
[triplestore](https://en.wikipedia.org/wiki/Triplestore) for desktop, embedded
and mobile.

It is a middleware component aimed at desktop application developers who
want their apps to browse and search user content. It's not designed to be
used directly by desktop users, but it provides a commandline tool named
`tracker` for the adventurous.

Tracker allows your application to instantly perform full-text searches across
all documents. This feature is used by the 'search' bar in GNOME Files, for
example.

This is achieved by indexing the user's home directory in the background.

Tracker also allows your application to query and list content that the user
has stored. For example, GNOME Music displays all the music files that are
found by Tracker. This means that GNOME Music doesn't need to maintain a
database of its own.

If you need to go beyond simple searches, you'll be happy to know that
Tracker is also a [linked data](http://linkeddata.org/) endpoint and it
understands [SPARQL](https://www.w3.org/TR/2013/REC-sparql11-overview-20130321/).

Apps can also store their own data in the Tracker database, but this feature
isn't widely used yet. The [next major version of
Tracker](https://gitlab.gnome.org/GNOME/tracker/-/milestones/1) aims to bring
improvements in this regard.

There are several components that make up Tracker:

  * **tracker-store**, which stores the index.
  * **tracker-miner-fs**, a daemon which crawls and monitors the filesystem to find content
  * **tracker-extract**, a suite of modules to extract metadata and content
    from many different types of file.
  * the **ontologies**, which define the database schema and the linked data vocabulary.

## Who uses Tracker?

### GNOME

Tracker is a core dependency of the [GNOME desktop](https://www.gnome.org/).

Gnome's [Shell](https://wiki.gnome.org/Projects/GnomeShell) doesn't use Tracker directly.
Instead, the search results in the shell are provided by multiple apps on the system,
using the [SearchProvider](https://developer.gnome.org/SearchProvider/) API.
Some of these apps use Tracker internally, so they return search results
provided by Tracker to gnome-shell.

The following GNOME applications use Tracker:

 * [GNOME Books](https://wiki.gnome.org/Apps/Books) (uses Tracker to find ebooks)
 * [GNOME Boxes](https://wiki.gnome.org/Apps/Boxes) (uses Tracker to find VM images)
 * [GNOME Documents](https://wiki.gnome.org/Apps/Documents) (uses Tracker to find documents)
 * [GNOME Files](https://wiki.gnome.org/Apps/Files) (uses Tracker for full-text search within files)
 * [GNOME Games](https://wiki.gnome.org/Apps/Games) (uses Tracker to find games)
 * [GNOME Music](https://wiki.gnome.org/Apps/Music) (uses Tracker to find music and store playlist data)
 * [GNOME Photos](https://wiki.gnome.org/Apps/Photos) (uses Tracker to find photos and store album data)
 * [GNOME Videos](https://wiki.gnome.org/Apps/Videos) (uses Tracker to find video content)

Although Tracker is able to store contacts and calendar entries,
GNOME uses [Evolution Data Server](https://developer.gnome.org/platform-overview/stable/tech-eds.html)
for this.

### GTK

The file chooser dialog supplied by GTK has a search interface. There's
a [Tracker backend](https://gitlab.gnome.org/GNOME/gtk/blob/master/gtk/gtksearchenginetracker.c)
for this.

### Media tools

[Grilo](https://wiki.gnome.org/Projects/Grilo) is a library for finding and
fetching media content from many different sources. It uses Tracker to browse
and search local media content.

[Rygel](https://wiki.gnome.org/Projects/Rygel) is a home media solution that serves
content over UPnP. It uses Tracker to find your media files.

### Sailfish OS

[Sailfish OS](https://sailfishos.org) uses Tracker for [indexing media
content](https://sailfishos.org/wiki/Core_Areas_and_APIs).

## Related projects

[Xapian](https://xapian.org/) provides similar functionality to Tracker.
It focuses more on scalability and less on having a lightweight footprint.
Unlike Tracker, it doesn't support SPARQL or provide a Linked Data endpoint.

[Baloo](https://community.kde.org/Baloo) is a metadata and search framework by
KDE, implemented using Xapian.

[Apache Lucene + Solr](http://lucene.apache.org/) is a search engine which
targets very large-scale workloads. It has a much heavier footprint compared to
Tracker.
