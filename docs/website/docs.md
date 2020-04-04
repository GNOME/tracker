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
using one or more of the shared libraries it provides:

 * [libtracker-sparql](https://developer.gnome.org/libtracker-sparql/stable/) is
   used to read and write data from the Tracker store using SPARQL.
 * [libtracker-control](https://developer.gnome.org/libtracker-control/stable/),
   is used to manage Tracker daemons.
 * [libtracker-miner](https://developer.gnome.org/libtracker-miner/stable/) can
   be used if you want to implement a new data provider while reusing existing
   Tracker code.

WARNING: The documention linked above is for out of date verions of Tracker 2.x.
This is due to an [infrastructure
issue](https://gitlab.gnome.org/Infrastructure/library-web/issues/50). See also
[this bug](https://gitlab.gnome.org/GNOME/tracker/-/issues/100).

The following documentation may be useful:

 * [Tracker ontology documentation](https://developer.gnome.org/ontology/stable/).
 * [Tracker documentation on wiki.gnome.org](https://wiki.gnome.org/Projects/Tracker).

## Preview Documentation

We provide an online version of the documentation for the latest in-development version
of Tracker. You can browse it here:

  * [libtracker-sparql](./api-preview/libtracker-sparql)
  * [ontology](./api-preview/ontology)

Be aware that some libraries from Tracker 2.0 will not be available for Tracker 3.0.
