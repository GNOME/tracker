# Documentation

## User Documentation

Tracker is a system component and most users will not need to interact with
it directly.

GNOME has documentation on how to
[search for files in the file manager](https://developer.gnome.org/libtracker-sparql/stable/).

The `tracker` commandline tool provides direct access to Tracker. Use the
`--help` option for documentation of this tool.

## Developer Documentation
    
Application and platform developers using Tracker will interact with Tracker
using one or more of the shared libraries at provides:

 * [libtracker-sparql](https://developer.gnome.org/libtracker-sparql/stable/) is
   used to read and write data from the Tracker store using SPARQL.
 * [libtracker-control](https://developer.gnome.org/libtracker-control/stable/),
   is used to manage Tracker daemons.
 * [libtracker-miner](https://developer.gnome.org/libtracker-miner/stable/) can
   be used if you want to implement a new data provider while reusing existing
   Tracker code.

Developers are expected to read and write data from the Tracker store using the
SPARQL.
The following documentation may be useful:

 * [Tracker ontology documentation](https://developer.gnome.org/ontology/stable/).
 * [Tracker documentation on wiki.gnome.org](https://wiki.gnome.org/Projects/Tracker).
