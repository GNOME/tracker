# Tracker

Tracker is a search engine for desktop and mobile.

More information on Tracker can be found at:

  * <https://wiki.gnome.org/Projects/Tracker>

Source code and issue tracking:

  * <https://gitlab.gnome.org/GNOME/tracker>

All discussion related to Tracker happens on:

  * <https://mail.gnome.org/mailman/listinfo/tracker-list>

IRC channel #tracker on:

  * [irc.gimp.net](irc://irc.gimp.net)

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

