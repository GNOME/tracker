# Tracker HACKING guide

This file contains information useful for developers who want to improve
the tracker and tracker-miners projects.

## Automated testing

You can run the Tracker testsuite using the `meson test` command.

Tracker runs the tests automatically in GitLab CI. The .gitlab-ci.yml file
controls this. Here are the latest tests that were run:

 * https://gitlab.gnome.org/GNOME/tracker/pipelines
 * https://gitlab.gnome.org/GNOME/tracker-miners/pipelines

Some distros also run the test suite, including these:

  * Ubuntu autopkgtest:
      * https://autopkgtest.ubuntu.com/packages/tracker/
      * https://autopkgtest.ubuntu.com/packages/tracker-miners/
  * Debian builds:
      * https://buildd.debian.org/status/package.php?p=tracker
      * https://buildd.debian.org/status/package.php?p=tracker-miners
  * Debian reproducible-builds:
      * https://tests.reproducible-builds.org/debian/rb-pkg/unstable/amd64/tracker.html
      * https://tests.reproducible-builds.org/debian/rb-pkg/unstable/amd64/tracker-miners.html

OpenSuSE do automated whole-system testing that includes the GNOME desktop for their Tumbleweed rolling release distro: https://openqa.opensuse.org/group_overview/1

## Logging

The following environment variables control logging from Tracker daemons:

  * `G_MESSAGES_DEBUG`: controls log output from all GLib-based libraries
    in the current process. Use `G_MESSAGES_DEBUG=Tracker` to see Tracker
    related messages, or `G_MESSAGES_DEBUG=all` to see everything.
  * `TRACKER_DEBUG`: takes a comma-separated list of keywords to enable
    extra debugging output. Use the keyword 'help' for a list of keywords.

    Options for libtracker-sparql include:

        - collation
        - ontology-changes
        - sparql
        - sqlite
        - sql-statements

    See the relevant `man` page for options relevant to tracker-miner-fs.

You can set these variables when using `tracker-sandbox`, and when running the
Tracker test suite. Note that Meson will not print log output from tests by
default, use `meson test --verbose` or `meson test --print-errorlogs` to
enable.

The functional tests understand an additional variable, `TRACKER_TESTS_VERBOSE`
with can be set to `1` or `yes` to see detailed logging from the test harness
itself, and full log output from the internal D-Bus daemon. By default, these
tests filter output from the D-Bus daemon to only show log messages from
Tracker processes. Anything written directly to stdout, for example by
`g_print()` or by the dbus-daemon itself, will not be displayed unless
`TRACKER_TESTS_VERBOSE` is set.

When working with GitLab CI, you can use the
[Run Pipeline dialog](https://gitlab.gnome.org/GNOME/tracker/pipelines/new)
to set the values of these variables and increase the verbosity of the tests in
CI.

## Attaching a debugger to Tracker daemons

Tracker daemons are not started directly. Instead they are started by the D-Bus
daemon by request. When using the run-uninstalled script or the
functional-tests, it's difficult to start the daemon manually under `gdb`.

Instead, we recommend adding a 10 second timeout at the top of the daemon's
main() function. In Vala code, try this:

    print("Pausing to attach debugger. Run: gdb attach %i\n", Posix.getpid());
    Posix.usleep(10 * 1000 * 1000);
    print("Waking up again\n");

Run the test, using the `meson build --timeout-multiplier=10000`
option to avoid your process being killed by the test runner. When you see
the 'Pausing' message, run the `gdb attach``command in another terminal within
10 seconds.

## Running Tracker daemons under Valgrind

The Tracker daemons are launched using D-Bus autolaunch. When running them from
the source tree using the run-uninstalled script or the functional-tests, the
commandline is controlled by the D-Bus .service.in files stored in
`./tests/services`. Just change the `Exec=` line to add Valgrind, like this:

    Exec=/usr/bin/valgrind @abs_top_builddir@/src/tracker-store/tracker-store

By default the run-uninstalled script and the functional-tests will only show
output from Tracker code. For the functional-tests, set TRACKER_TESTS_VERBOSE=1
to see output from Valgrind. For tracker-sandbox use the `--debug-dbus` option.
