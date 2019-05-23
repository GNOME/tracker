# Tracker

Tracker is an efficient search engine and
[triplestore](https://en.wikipedia.org/wiki/Triplestore) for desktop, embedded
and mobile.

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
    ln -s ../../tracker tracker-miners/subprojects/

Now you can run the commands below to build Tracker and install it in a
new, isolated prefix named `opt/tracker` inside your home folder.

> NOTE: If you see 'dependency not found' errors from Meson, that means there
> is a package missing on your computer that you need to install so you can
> compile Tracker. On Ubuntu/Debian, you can run `apt build-dep tracker-miners`
> and on Fedora `dnf build-dep tracker-miners` to install all the necessary
> packages.

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

When developing and testing Tracker you will normally want it to run in the
foreground instead. The `tracker-sandbox` tool exists to help with this.

You can run the tool directly from the tracker.git source tree. Ensure you are
in the top of the tracker source tree and type this to see the --help output: 

    ./utils/sandbox/tracker-sandbox.py --help

You should always pass the `--prefix` option, which should be the same as the
--prefix argument you passed to Meson. You also need to use `--index` which
controls where internal state files like the database are kept. You may also
want to pass `--debug` to see detailed log output.

Now you can index some files using `--update` mode. Here's how to index files
in `~/Documents` for example:

    ./utils/sandbox/tracker-sandbox.py  --prefix ~/opt/tracker --index ~/tracker-content \
        --update --content ~/Documents

You can then list the files that have been indexed...

    ./utils/sandbox/tracker-sandbox.py  --prefix ~/opt/tracker --index ~/tracker-content \
        --list-files

... run a full-text search ...

    ./utils/sandbox/tracker-sandbox.py  --prefix ~/opt/tracker --index ~/tracker-content \
        --search "bananas"

... or run a SPARQL query on the content:

    ./utils/sandbox/tracker-sandbox.py  --prefix ~/opt/tracker --index ~/tracker-content \
        --sparql "SELECT ?url { ?resource a nfo:FileDataObject ; nie:url ?url . }"

You can also open a shell inside the sandbox environment. From here you can run
the `tracker` commandline tool, and you can run the Tracker daemons manually
under a debugger such as GDB.

For more information about developing Tracker, look at
https://wiki.gnome.org/Projects/Tracker.
