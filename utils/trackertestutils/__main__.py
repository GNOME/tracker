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
import collections
import configparser
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
import time

from gi.repository import Gio
from gi.repository import GLib

from . import dbusdaemon
from . import helpers
from . import mainloop

# Script
script_name = 'tracker-sandbox'
script_about = "Tracker Sandbox developer tool."

default_index_location = '/tmp/tracker-sandbox'

store_pid = -1
store_proc = None

original_xdg_data_home = GLib.get_user_data_dir()

# Template config file
config_template = """
[General]
verbosity=0
sched-idle=0
initial-sleep=0

[Monitors]
enable-monitors=false

[Indexing]
throttle=0
index-on-battery=true
index-on-battery-first-time=true
index-removable-media=false
index-optical-discs=false
low-disk-space-limit=-1
index-recursive-directories=;
index-single-directories=;
ignored-directories=;
ignored-directories-with-content=;
ignored-files=
crawling-interval=-1
removable-days-threshold=3

[Writeback]
enable-writeback=false
"""


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

    if var in env:
        existing = env[var]
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
    extra_env['TRACKER_USE_CONFIG_FILES'] = 'yes'

    extra_env['G_MESSAGES_PREFIXED'] = 'all'

    extra_env['TRACKER_VERBOSITY'] = str(verbosity)

    log.debug('Using prefix location "%s"' % prefix)
    log.debug('Using index location "%s"' % index_location)

    sandbox = helpers.TrackerDBusSandbox(dbus_config, extra_env=extra_env)
    sandbox.start(new_session=(interactive == True))

    # Update our own environment, so when we launch a subprocess it has the
    # same settings as the Tracker daemons.
    os.environ.update(extra_env)
    os.environ['DBUS_SESSION_BUS_ADDRESS'] = sandbox.daemon.get_address()
    os.environ['TRACKER_SANDBOX'] = '1'

    return sandbox


def config_set(content_locations_recursive=None, content_locations_single=None):
    # Make sure File System miner is configured correctly
    config_dir = os.path.join(os.environ['XDG_CONFIG_HOME'], 'tracker')
    config_filename = os.path.join(config_dir, 'tracker-miner-fs.cfg')

    log.debug('Using config file "%s"' % config_filename)

    # Only update config if we're updating the database
    os.makedirs(config_dir, exist_ok=True)

    if not os.path.exists(config_filename):
        f = open(config_filename, 'w')
        f.write(config_template)
        f.close()

        log.debug('  Miner config file written')

    # Set content path
    config = configparser.ConfigParser()
    config.optionxform = str
    config.read(config_filename)

    if content_locations_recursive:
        log.debug("Using content locations: %s" %
              content_locations_recursive)
    if content_locations_single:
        log.debug("Using non-recursive content locations: %s" %
              content_locations_single)

    def locations_gsetting(locations):
        locations = [dir if dir.startswith('&') else os.path.abspath(dir)
                     for dir in locations]
        return GLib.Variant('as', locations).print_(False)

    if not config.has_section('General'):
        config.add_section('General')

    config.set('General', 'index-recursive-directories',
               locations_gsetting(content_locations_recursive or []))
    config.set('General', 'index-single-directories',
               locations_gsetting(content_locations_single or []))

    with open(config_filename, 'w') as f:
        config.write(f)


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
    parser.add_argument('--wait-for-miner', type=str, action='append',
                        help="wait for one or more daemons to start, and "
                             "return to idle for at least 1 second, before "
                             "exiting. Usually used with `tracker index` where "
                             "you should pass --wait-for-miner=Files and "
                             "--wait-for-miner=Extract")
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


class MinerStatusWatch():
    """This class provides a way to block waiting for miners to finish.

    This is needed because of a deficiency in `tracker index`, see:
    https://gitlab.gnome.org/GNOME/tracker/issues/122

    """
    def __init__(self, sandbox, miner_name):
        self.dbus_name = 'org.freedesktop.Tracker1.Miner.' + miner_name
        self.object_path = '/org/freedesktop/Tracker1/Miner/' + miner_name

        self._sandbox = sandbox

        # Stores a list of (time, status) pairs. This is used to determine
        # if the miner has been idle continuously over a time peroid.
        self._status_log = collections.deque()

    def _log_status(self, time, status):
        self._status_log.append((time, status))
        if len(self._status_log) > 100:
            self._status_log.popleft()

    def setup(self):
        log.debug(f"Set up status watch on {self.dbus_name}")
        self._proxy = Gio.DBusProxy.new_sync(
            self._sandbox.get_connection(),
            Gio.DBusProxyFlags.NONE, None,
            self.dbus_name, self.object_path, 'org.freedesktop.Tracker1.Miner',
            None)

        # FIXME: this doesn't appear to work, so we have to use polling.
        #proxy.connect('g-signal', miner_signal_cb)

        # This call will raise GDBus.Error:org.freedesktop.DBus.Error.ServiceUnknown
        # if the miner name is invalid.
        status = self._proxy.GetStatus()
        self._log_status(time.time(), status)
        log.debug(f"{self.dbus_name}: Current status: {status}")

    def check_was_idle_for_time_period(self, period_seconds):
        now = time.time()

        status = self._proxy.GetStatus()
        self._log_status(now, status)
        log.debug(f"{self.dbus_name}: Current status: {status}")

        cursor = len(self._status_log) - 1
        previous_delta_from_now = 0
        while True:
            if cursor < 0 or self._status_log[cursor][1] != 'Idle':
                if previous_delta_from_now >= period_seconds:
                    return True
                else:
                    return False
            previous_delta_from_now = (now - self._status_log[cursor][0])
            cursor -= 1


def wait_for_miners(watches):
    # We wait 1 second after "Idle" status is seen before exiting, because the
    # extractor goes to/from Idle frequently.
    wait_for_idle_time = 1
    while True:
        status = [watch.check_was_idle_for_time_period(wait_for_idle_time) for watch in watches.values()]
        if all(status):
            break
        else:
            log.debug(f"Waiting for idle.")
            time.sleep(0.1)


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

    if args.command is None and args.wait_for_miner is not None:
        raise RuntimeError("--wait-for-miner cannot be used when opening an "
                           "interactive shell.")

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
    config_set()

    link_to_mime_data()

    miner_watches = {}
    for miner in (args.wait_for_miner or []):
        watch = MinerStatusWatch(sandbox, miner)
        watch.setup()
        miner_watches[miner] = watch

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
            result = subprocess.run(command)

            if len(miner_watches) > 0:
                wait_for_miners(miner_watches)

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
