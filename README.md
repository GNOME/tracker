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

# Developing Tracker

If you want to help develop and improve Tracker, great! Remember that Tracker
is a middleware component, designed to be integrated into larger codebases. To
fully test a change you may need to build and test Tracker as part of another
project.

For the GNOME desktop, consider using the documented [Building a System
Component](https://wiki.gnome.org/Newcomers/BuildSystemComponent) workflow.

It's also possible to build Tracker on its own and install it inside your home
directory for testing purposes.  Read on for instructions on how to do this.

## Compilation

Tracker uses the [Meson build system](http://mesonbuild.com), which you must
have installed in order to build Tracker.

We recommend that you build tracker core as a subproject of tracker-miners.
You can do this by cloning both repos, then creating a symlink in the
`subprojects/` directory of tracker-miners.git to the tracker.git checkout.

    git clone https://gitlab.gnome.org/GNOME/tracker.git
    git clone https://gitlab.gnome.org/GNOME/tracker-miners.git

    mkdir tracker-miners/subprojects
    ln -s ./tracker tracker-miners/subprojects/

Now you can run the commands below to build Tracker and install it in a
new, isolated prefix named `opt/tracker` inside your home folder.

    cd tracker-miners
    meson ./build --prefix=$HOME/opt/tracker -Dtracker_core=subproject
    cd build
    ninja install

## Running the testsuite

At this point you can run the Tracker test suite from the `build` directory:

    meson test --print-errorlogs

## Developing with tracker-sandbox

Tracker normally runs automatically, indexing content in the background so that
search results are available quickly when needed.

When developing and testing you usually want Tracker to run in the foreground
under your control. The `tracker-sandbox` tool in tracker.git exists to help
with this.

There are several modes of operation. You will always need to tell
`tracker-sandbox` where to find the Tracker programs, using the `--prefix` option,
and where to write internal state files, using the `--index` option. You may also
want to pass `--debug` to see detailed log output.

You can cause Tracker to index a directory of files, such as `~/Documents`:

    ./utils/sandbox/tracker-sandbox.py  --prefix ~/tracker-dev --index ~/tracker-content \
        --content ~/Documents --update

You can then list the files that have been indexed...

    ./utils/sandbox/tracker-sandbox.py  --prefix ~/tracker-dev --index ~/tracker-content \
        --list-files

... or run a SPARQL query on the content:

    ./utils/sandbox/tracker-sandbox.py  --prefix ~/tracker-dev --index ~/tracker-content \
        --query "SELECT ?url { ?resource a nfo:FileDataObject ; nie:url ?url }"

You can also open a shell inside the sandbox environment. From here you can run
the `tracker` commandline tool, and you can run the Tracker daemons manually
under a debugger such as GDB.

For more information about developing Tracker, look at
https://wiki.gnome.org/Projects/Tracker.
