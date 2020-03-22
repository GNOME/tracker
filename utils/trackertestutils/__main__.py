#!/usr/bin/env python3
#
# Copyright (C) 2012-2013 Martyn Russell <martyn@lanedo.com>
# Copyright (C) 2012      Sam Thursfield <sam.thursfield@codethink.co.uk>
# Copyright (C) 2016,2019 Sam Thursfield <sam@afuera.me.uk>
#
# This is a tool for running development versions of Tracker.
#
# See README.md for usage information.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
#

import argparse
import contextlib
import locale
import logging
import os
import shlex
import shutil
import signal
import subprocess
import sys
import tempfile

from gi.repository import GLib

from . import dconf
from . import helpers

# Script
script_name = 'tracker-sandbox'
script_about = "Tracker Sandbox developer tool."

default_index_location = '/tmp/tracker-sandbox'

store_pid = -1
store_proc = None

original_xdg_data_home = GLib.get_user_data_dir()

log = logging.getLogger('sandbox')


# Environment / Clean up

def environment_unset(dbus):
    log.debug('Cleaning up processes ...')

    dbus.stop()

    # FIXME: clean up tracker-store, can't use 'tracker daemon ...' for this,
    #        that kills everything it finds in /proc sadly.
    if store_pid > 0:
        log.debug('  Killing Tracker store')
        os.kill(store_pid, signal.SIGTERM)


def environment_set_and_add_path(env, var, prefix, suffix):
    new = os.path.join(prefix, suffix)

    if var in os.environ:
        existing = os.environ[var]
        full = '%s:%s' % (new, existing)
    else:
        full = new

    env[var] = full


def create_sandbox(index_location, prefix=None, verbosity=0, dbus_config=None,
                   interactive=False):
    assert prefix is None or dbus_config is None

    extra_env = {}

    # Data
    extra_env['XDG_DATA_HOME'] = '%s/data/' % index_location
    extra_env['XDG_CONFIG_HOME'] = '%s/config/' % index_location
    extra_env['XDG_CACHE_HOME'] = '%s/cache/' % index_location
    extra_env['XDG_RUNTIME_DIR'] = '%s/run/' % index_location

    # Prefix - only set if non-standard
    if prefix and prefix != '/usr':
        environment_set_and_add_path(extra_env, 'PATH', prefix, 'bin')
        environment_set_and_add_path(extra_env, 'LD_LIBRARY_PATH', prefix, 'lib')
        environment_set_and_add_path(extra_env, 'XDG_DATA_DIRS', prefix, 'share')

    # Preferences
    extra_env['G_MESSAGES_PREFIXED'] = 'all'

    extra_env['TRACKER_VERBOSITY'] = str(verbosity)

    log.debug('Using prefix location "%s"' % prefix)
    log.debug('Using index location "%s"' % index_location)

    sandbox = helpers.TrackerDBusSandbox(dbus_config, extra_env=extra_env)
    sandbox.start(new_session=True)

    # Update our own environment, so when we launch a subprocess it has the
    # same settings as the Tracker daemons.
    os.environ.update(extra_env)
    os.environ['DBUS_SESSION_BUS_ADDRESS'] = sandbox.daemon.get_address()
    os.environ['TRACKER_SANDBOX'] = '1'

    return sandbox


def config_set(sandbox, content_locations_recursive=None,
               content_locations_single=None, applications=False):
    dconfclient = dconf.DConfClient(sandbox)

    if content_locations_recursive:
        log.debug("Using content locations: %s" %
              content_locations_recursive)
    if content_locations_single:
        log.debug("Using non-recursive content locations: %s" %
              content_locations_single)
    if applications:
        log.debug("Indexing applications")

    def locations_gsetting(locations):
        locations = [dir if dir.startswith('&') else os.path.abspath(dir)
                     for dir in locations]
        return GLib.Variant('as', locations)

    dconfclient.write('org.freedesktop.Tracker.Miner.Files',
                      'index-recursive-directories',
                      locations_gsetting(content_locations_recursive or []))
    dconfclient.write('org.freedesktop.Tracker.Miner.Files',
                      'index-single-directories',
                      locations_gsetting(content_locations_recursive or []))
    dconfclient.write('org.freedesktop.Tracker.Miner.Files',
                      'index-applications',
                      GLib.Variant('b', applications))

def link_to_mime_data():
    '''Create symlink to $XDG_DATA_HOME/mime in our custom data home dir.

    Mimetype detection seems to break horribly if the $XDG_DATA_HOME/mime
    directory is missing. Since we have to override the normal XDG_DATA_HOME
    path, we need to work around this by linking back to the real mime data.

    '''
    new_xdg_data_home = os.environ['XDG_DATA_HOME']
    old_mime_dir = os.path.join(original_xdg_data_home, 'mime')
    if os.path.exists(old_mime_dir):
        new_mime_dir = os.path.join(new_xdg_data_home, 'mime')
        if (not os.path.exists(new_mime_dir)
                and not os.path.islink(new_mime_dir)):
            os.makedirs(new_xdg_data_home, exist_ok=True)
            os.symlink(
                os.path.join(original_xdg_data_home, 'mime'), new_mime_dir)


def argument_parser():
    class expand_path(argparse.Action):
        """Expand user- and relative-paths in filenames."""
        # From https://gist.github.com/brantfaircloth/1443543
        def __call__(self, parser, namespace, values, option_string=None):
            setattr(namespace, self.dest, os.path.abspath(os.path.expanduser(values)))

    parser = argparse.ArgumentParser(description=script_about)
    parser.add_argument('--dbus-config', metavar='FILE', action=expand_path,
                        help="use a custom D-Bus config file to locate the "
                             "Tracker daemons. This can be used to run Tracker "
                             "from a build tree of tracker-miners.git, by "
                             "using the generated file ./tests/test-bus.conf")
    parser.add_argument('-p', '--prefix', metavar='DIR', action=expand_path,
                        help="run Tracker from the given install prefix. You "
                             "can run the system version of Tracker by "
                             "specifying --prefix=/usr")
    parser.add_argument('-v', '--verbosity', default=None,
                        choices=['0', '1', '2', '3', 'errors', 'minimal', 'detailed', 'debug'],
                        help="show debugging info from Tracker processes")
    parser.add_argument('-i', '--index', metavar='DIR', action=expand_path,
                        default=default_index_location, dest='index_location',
                        help=f"directory to the index (default={default_index_location})")
    parser.add_argument('--index-tmpdir', action='store_true',
                        help="create index in a temporary directory and "
                             "delete it on exit (useful for automated testing)")
    parser.add_argument('--debug-dbus', action='store_true',
                        help="show stdout and stderr messages from every daemon "
                             "running on the sandbox session bus. By default we "
                             "only show messages logged by Tracker daemons.")
    parser.add_argument('--debug-sandbox', action='store_true',
                        help="show debugging info from tracker-sandbox")
    parser.add_argument('command', type=str, nargs='*', help="Command to run inside the shell")

    return parser


def verbosity_as_int(verbosity):
    verbosity_map = {
        'errors': 0,
        'minimal': 1,
        'detailed': 2,
        'debug': 3
    }
    return verbosity_map.get(verbosity, int(verbosity))


def init_logging(debug_sandbox, debug_dbus):
    SANDBOX_FORMAT = "%(name)s: %(message)s"
    DBUS_FORMAT = "%(message)s"

    if debug_sandbox:
        sandbox_log_handler = logging.StreamHandler()
        sandbox_log_handler.setFormatter(logging.Formatter(SANDBOX_FORMAT))

        root = logging.getLogger()
        root.setLevel(logging.DEBUG)
        root.addHandler(sandbox_log_handler)
    else:
        dbus_stderr = logging.getLogger('trackertestutils.dbusdaemon.stderr')
        dbus_stdout = logging.getLogger('trackertestutils.dbusdaemon.stdout')

        dbus_handler = logging.StreamHandler(stream=sys.stderr)
        dbus_handler.setFormatter(logging.Formatter(DBUS_FORMAT))

        if debug_dbus:
            dbus_stderr.setLevel(logging.DEBUG)
            dbus_stdout.setLevel(logging.DEBUG)
        else:
            dbus_stderr.setLevel(logging.INFO)
            dbus_stdout.setLevel(logging.INFO)

        dbus_stderr.addHandler(dbus_handler)
        dbus_stdout.addHandler(dbus_handler)


@contextlib.contextmanager
def ignore_sigint():
    handler = signal.signal(signal.SIGINT, signal.SIG_IGN)
    yield
    signal.signal(signal.SIGINT, handler)


def main():
    locale.setlocale(locale.LC_ALL, '')

    parser = argument_parser()
    args = parser.parse_args()

    init_logging(args.debug_sandbox, args.debug_dbus)

    shell = os.environ.get('SHELL', '/bin/bash')

    if args.prefix is None and args.dbus_config is None:
        parser.print_help()
        print("\nYou must specify either --dbus-config (to run Tracker from "
              "a build tree) or --prefix (to run an installed Tracker).")
        sys.exit(1)

    if args.prefix is not None and args.dbus_config is not None:
        raise RuntimeError(
            "You cannot specify --dbus-config and --prefix at the same time. "
            "Note that running Tracker from the build tree implies "
            "--dbus-config.")

    if args.verbosity is None:
        verbosity = verbosity_as_int(os.environ.get('TRACKER_VERBOSITY', 0))
    else:
        verbosity = verbosity_as_int(args.verbosity)
        if 'TRACKER_VERBOSITY' in os.environ:
            if verbosity != int(os.environ['TRACKER_VERBOSITY']):
                raise RuntimeError("Incompatible values for TRACKER_VERBOSITY "
                                   "from environment and from --verbosity "
                                   "parameter.")

    index_location = None
    index_tmpdir = None

    if args.index_location != default_index_location and args.index_tmpdir:
        raise RuntimeError("The --index-tmpdir flag is enabled, but --index= was also passed.")
    if args.index_tmpdir:
        index_location = index_tmpdir = tempfile.mkdtemp(prefix='tracker-sandbox')
    else:
        index_location = args.index_location

    interactive = not (args.command)

    # Set up environment variables and foo needed to get started.
    sandbox = create_sandbox(index_location, args.prefix, verbosity,
                             dbus_config=args.dbus_config,
                             interactive=interactive)
    config_set(sandbox)

    link_to_mime_data()

    try:
        if interactive:
            if args.dbus_config:
                print(f"Using Tracker daemons from build tree with D-Bus config {args.dbus_config}")
            else:
                print(f"Using Tracker daemons from prefix {args.prefix}")
            print("Starting interactive Tracker sandbox shell... (type 'exit' to finish)")
            print()

            with ignore_sigint():
                subprocess.run(shell)
        else:
            command = [shell, '-c', ' '.join(shlex.quote(c) for c in args.command)]

            log.debug("Running: %s", command)
            interrupted = False
            try:
                result = subprocess.run(command)
            except KeyboardInterrupt:
                interrupted = True

            if interrupted:
                log.debug("Process exited due to SIGINT")
                sys.exit(0)
            else:
                log.debug("Process finished with returncode %i", result.returncode)
                sys.exit(result.returncode)
    finally:
        sandbox.stop()
        if index_tmpdir:
            shutil.rmtree(index_tmpdir, ignore_errors=True)


# Entry point/start
if __name__ == "__main__":
    try:
        main()
    except RuntimeError as e:
        sys.stderr.write(f"ERROR: {e}\n")
        sys.exit(1)
