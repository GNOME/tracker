# Tracker

Tracker is a search engine for desktop and mobile.

The Tracker project is divided into two main repositories:

  * [Tracker core](https://gitlab.gnome.org/GNOME/tracker) contains the database
    (*tracker-store*), the database ontologies, the commandline user
    interface (`tracker`), and several support libraries.

  * [Tracker Miners](https://gitlab.gnome.org/GNOME/tracker-miners) contains
    the indexer daemon (*tracker-miner-fs*) and tools to extract metadata
    from many different filetypes.

More information on Tracker can be found at:

  * <https://wiki.gnome.org/Projects/Tracker>

Source code and issue tracking:

  * <https://gitlab.gnome.org/GNOME/tracker>

All discussion related to Tracker happens on:

  * <https://mail.gnome.org/mailman/listinfo/tracker-list>

IRC channel #tracker on:

  * [irc.gimp.net](irc://irc.gimp.net)

Related projects:

  * [GNOME Online Miners](https://gitlab.gnome.org/GNOME/gnome-online-miners/)
    extends Tracker to allow searching and indexing some kinds of online
    content.

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
