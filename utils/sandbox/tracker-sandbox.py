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
import locale
import logging
import os
import shlex
import signal
import subprocess
import sys
import threading

import configparser

from gi.repository import GLib

# Script
script_name = 'tracker-sandbox'
script_version = '1.0'
script_about = "Tracker Sandbox developer tool."

index_location_abs = ''

default_prefix = '/usr'
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
dbuslog = logging.getLogger('dbus')


# Private DBus daemon

class DBusDaemon:
    """The private D-Bus instance that provides the sandbox's session bus.

    We support reading and writing the session information to a file. This
    means that if the user runs two sandbox instances on the same data
    directory at the same time, they will share the same message bus.
    """

    def __init__(self, session_file=None):
        self.session_file = session_file
        self.existing_session = False
        self.process = None

        try:
            self.address, self.pid = self.read_session_file(session_file)
            self.existing_session = True
        except FileNotFoundError:
            log.debug("No existing D-Bus session file was found.")

            self.address = None
            self.pid = None

    def get_session_file(self):
        """Returns the path to the session file if we created it, or None."""
        if self.existing_session:
            return None
        return self.session_file

    def get_address(self):
        return self.address

    @staticmethod
    def read_session_file(session_file):
        with open(session_file, 'r') as f:
            content = f.read()

        try:
            address = content.splitlines()[0]
            pid = int(content.splitlines()[1])
        except ValueError:
            raise RuntimeError(f"D-Bus session file {session_file} is not valid. "
                                "Remove this file to start a new session.")

        return address, pid

    @staticmethod
    def write_session_file(session_file, address, pid):
        os.makedirs(os.path.dirname(session_file), exist_ok=True)

        content = '%s\n%s' % (address, pid)
        with open(session_file, 'w') as f:
            f.write(content)

    def start_if_needed(self):
        if self.existing_session:
            log.debug('Using existing D-Bus session from file "%s" with address "%s"'
                      ' with PID %d' % (self.session_file, self.address, self.pid))
        else:
            dbus_command = ['dbus-daemon', '--session', '--print-address=1', '--print-pid=1']
            self.process = subprocess.Popen(dbus_command, stdout=subprocess.PIPE, stderr=subprocess.PIPE)

            try:
                self.address = self.process.stdout.readline().strip().decode('ascii')
                self.pid = int(self.process.stdout.readline().strip().decode('ascii'))
            except ValueError:
                error = self.process.stderr.read().strip().decode('unicode-escape')
                raise RuntimeError(f"Failed to start D-Bus daemon.\n{error}")

            log.debug("Using new D-Bus session with address '%s' with PID %d",
                      self.address, self.pid)

            self.write_session_file(self.session_file, self.address, self.pid)
            log.debug("Wrote D-Bus session file at %s", self.session_file)

            # We must read from the pipes continuously, otherwise the daemon
            # process will block.
            self._threads=[threading.Thread(target=self.pipe_to_log, args=(self.process.stdout, 'stdout'), daemon=True),
                           threading.Thread(target=self.pipe_to_log, args=(self.process.stderr, 'stderr'), daemon=True)]
            self._threads[0].start()
            self._threads[1].start()

    def stop(self):
        if self.process:
            log.debug("  Stopping DBus daemon")
            self.process.terminate()
            self.process.wait()

    def pipe_to_log(self, pipe, source):
        """This function processes the output from our dbus-daemon instance."""
        while True:
            line_raw = pipe.readline()

            if len(line_raw) == 0:
                break

            line = line_raw.decode('utf-8').rstrip()

            if line.startswith('(tracker-'):
                # We set G_MESSAGES_PREFIXED=all, meaning that all log messages
                # output by Tracker processes have a prefix. Note that
                # g_print() will NOT be captured here.
                dbuslog.info(line)
            else:
                # Log messages from other daemons, including the dbus-daemon
                # itself, go here. Any g_print() messages also end up here.
                dbuslog.debug(line)


# Environment / Clean up

def environment_unset(dbus):
    log.debug('Cleaning up files ...')

    if dbus.get_session_file():
        log.debug('  Removing DBus session file')
        os.unlink(dbus.get_session_file())

    log.debug('Cleaning up processes ...')

    dbus.stop()

    # FIXME: clean up tracker-store, can't use 'tracker daemon ...' for this,
    #        that kills everything it finds in /proc sadly.
    if store_pid > 0:
        log.debug('  Killing Tracker store')
        os.kill(store_pid, signal.SIGTERM)


def environment_set_and_add_path(env, prefix, suffix):
    new = os.path.join(prefix, suffix)

    if env in os.environ:
        existing = os.environ[env]
        full = '%s:%s' % (new, existing)
    else:
        full = new

    os.environ[env] = full


def environment_set(index_location, prefix, verbosity=0):
    # Environment
    global index_location_abs

    index_location_abs = os.path.abspath(index_location)
    prefix = os.path.abspath(os.path.expanduser(prefix))

    # Data
    os.environ['XDG_DATA_HOME'] = '%s/data/' % index_location_abs
    os.environ['XDG_CONFIG_HOME'] = '%s/config/' % index_location_abs
    os.environ['XDG_CACHE_HOME'] = '%s/cache/' % index_location_abs
    os.environ['XDG_RUNTIME_DIR'] = '%s/run/' % index_location_abs

    # Prefix - only set if non-standard
    if prefix != default_prefix:
        environment_set_and_add_path('PATH', prefix, 'bin')
        environment_set_and_add_path('LD_LIBRARY_PATH', prefix, 'lib')
        environment_set_and_add_path('XDG_DATA_DIRS', prefix, 'share')

    # Preferences
    os.environ['TRACKER_USE_CONFIG_FILES'] = 'yes'

    # if opts.debug:
    #     os.environ['TRACKER_USE_LOG_FILES'] = 'yes'

    os.environ['G_MESSAGES_PREFIXED'] = 'all'
    os.environ['TRACKER_VERBOSITY'] = str(verbosity)

    log.debug('Using prefix location "%s"' % prefix)
    log.debug('Using index location "%s"' % index_location_abs)

    # Ensure directory exists
    # DBus specific instance
    dbus_session_file = os.path.join(
        os.environ['XDG_RUNTIME_DIR'], 'dbus-session')

    dbus = DBusDaemon(dbus_session_file)
    dbus.start_if_needed()

    # Important, other subprocesses must use our new bus
    os.environ['DBUS_SESSION_BUS_ADDRESS'] = dbus.get_address()

    return dbus


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
    parser = argparse.ArgumentParser(description=script_about)
    parser.add_argument('--version', action='store_true',
                        help="show version information")
    parser.add_argument('--debug-dbus', action='store_true',
                        help="show stdout and stderr messages from every daemon "
                             "running on the sandbox session bus. By default we "
                             "only show messages logged by Tracker daemons.")
    parser.add_argument('--debug-sandbox', action='store_true',
                        help="show debugging info from tracker-sandbox")
    parser.add_argument('-v', '--verbosity', default='0',
                        choices=['0', '1', '2', '3', 'errors', 'minimal', 'detailed', 'debug'],
                        help="show debugging info from Tracker processes")
    parser.add_argument('-p', '--prefix', metavar='DIR', type=str, default=default_prefix,
                        help=f"run Tracker from the given install prefix (default={default_prefix})")
    parser.add_argument('-i', '--index', metavar='DIR', type=str,
                        default=default_index_location, dest='index_location',
                        help=f"directory to the index (default={default_index_location})")
    parser.add_argument('command', type=str, nargs='*', help="Command to run inside the shell")

    return parser


def verbosity_as_int(verbosity):
    verbosity_map = {
        'errors': 0,
        'minimal': 1,
        'detailed': 2,
        'debug': 3
    }
    return verbosity_map.get(verbosity, int(args.verbosity))


def init_logging(debug_sandbox, debug_dbus):
    SANDBOX_FORMAT = "sandbox: %(message)s"
    DBUS_FORMAT = "|%(message)s"

    if debug_sandbox:
        sandbox_log_handler = logging.StreamHandler()
        sandbox_log_handler.setFormatter(logging.Formatter(SANDBOX_FORMAT))
        log.setLevel(logging.DEBUG)
        log.addHandler(sandbox_log_handler)

    dbus_log_handler = logging.StreamHandler()
    dbus_log_handler.setFormatter(logging.Formatter(DBUS_FORMAT))
    if debug_dbus:
        dbuslog.setLevel(logging.DEBUG)
    else:
        dbuslog.setLevel(logging.INFO)
    dbuslog.addHandler(dbus_log_handler)


# Entry point/start
if __name__ == "__main__":
    locale.setlocale(locale.LC_ALL, '')

    args = argument_parser().parse_args()

    if args.version:
        print(f"{script_name} {script_version}\n{script_about}\n")
        sys.exit(0)

    init_logging(args.debug_sandbox, args.debug_dbus)

    shell = os.environ.get('SHELL', '/bin/bash')

    verbosity = verbosity_as_int(args.verbosity)

    # Set up environment variables and foo needed to get started.
    dbus = environment_set(args.index_location, args.prefix, verbosity)
    config_set()

    link_to_mime_data()

    try:
        if args.command:
            command = [shell, '-c', ' '.join(shlex.quote(c) for c in args.command)]
            log.debug("Running: %s", command)
            subprocess.run(command)
        else:
            print('Starting shell... (type "exit" to finish)')
            print()

            os.system(shell)
    finally:
        environment_unset(dbus)
