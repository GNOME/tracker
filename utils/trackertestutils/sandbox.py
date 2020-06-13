# Copyright (C) 2010, Nokia <jean-luc.lamadon@nokia.com>
# Copyright (C) 2019, Sam Thursfield <sam@afuera.me.uk>
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
# 02110-1301, USA.
#

"""
Sandbox environment for running tests.

The sandbox is essentially a private D-Bus daemon.
"""

from gi.repository import Gio

import atexit
import logging
import os
import signal
import subprocess

from . import dbusdaemon
from . import dconf
from . import psutil_mini as psutil

log = logging.getLogger(__name__)

TRACKER_DBUS_PREFIX = 'org.freedesktop.Tracker3'
TRACKER_MINER_FS_BUSNAME = 'org.freedesktop.Tracker3.Miner.Files'

_process_list = []


def _cleanup_processes():
    for process in _process_list:
        log.debug("helpers._cleanup_processes: stopping %s", process)
        process.stop()


atexit.register(_cleanup_processes)


class TrackerSandbox:
    """
    Run Tracker daemons isolated from the real user session.

    The primary method of sandboxing is running one or more private D-Bus
    daemons, which take place of the host's session and system bus.

    """
    def __init__(self, session_bus_config_file, system_bus_config_file=None,
                 extra_env=None):
        self.extra_env = extra_env or {}

        self.session_bus = dbusdaemon.DBusDaemon(
            name='sandbox-session-bus', config_file=session_bus_config_file)
        if system_bus_config_file:
            self.system_bus = dbusdaemon.DBusDaemon(
                name='sandbox-system-bus', config_file=system_bus_config_file)
        else:
            self.system_bus = None

    def get_environment(self):
        env = os.environ
        env.update(self.extra_env)
        env['G_MESSAGES_PREFIXED'] = 'all'

        # This avoids an issue where gvfsd-fuse can start up while the bus is
        # shutting down. If it fails to connect to the bus, it continues to
        # run anyway which leads to our dbus-daemon failing to shut down.
        #
        # Since https://gitlab.gnome.org/GNOME/gvfs/issues/323 was implemented
        # in GVFS 1.42 this problem may have gone away.
        env['GVFS_DISABLE_FUSE'] = '1'

        # Precreate runtime dir, to avoid this warning from dbus-daemon:
        #
        #    Unable to set up transient service directory: XDG_RUNTIME_DIR "/home/sam/tracker-tests/tmp_59i3ev1/run" not available: No such file or directory
        #
        xdg_runtime_dir = env.get('XDG_RUNTIME_DIR')
        if xdg_runtime_dir:
            os.makedirs(xdg_runtime_dir, exist_ok=True)

    def start(self, new_session=False):
        if self.system_bus:
            log.info("Starting D-Bus system bus for sandbox.")
            log.debug("Added environment variables: %s", self.extra_env)
            self.system_bus.start(env=self.get_environment(), new_session=new_session)

            self.extra_env['DBUS_SYSTEM_BUS_ADDRESS'] = self.system_bus.get_address()

        log.info("Starting D-Bus session bus for sandbox.")
        log.debug("Added environment variables: %s", self.extra_env)
        self.session_bus.start(env=self.get_environment(), new_session=new_session)

    def stop(self):
        tracker_processes = []

        log.info("Looking for active Tracker processes on the session bus")
        for busname in self.session_bus.list_names_sync():
            if busname.startswith(TRACKER_DBUS_PREFIX):
                pid = self.session_bus.get_connection_unix_process_id_sync(busname)
                if pid is not None:
                    tracker_processes.append(pid)

        log.info("Terminating %i Tracker processes", len(tracker_processes))
        for pid in tracker_processes:
            os.kill(pid, signal.SIGTERM)

        log.info("Waiting for %i Tracker processes", len(tracker_processes))
        for pid in tracker_processes:
            psutil.wait_pid(pid)

        # We need to wait until Tracker processes have stopped before we
        # terminate the D-Bus daemon, otherwise lots of criticals like this
        # appear in the log output:
        #
        #  (tracker-miner-fs:14955): GLib-GIO-CRITICAL **: 11:38:40.386: Error  while sending AddMatch() message: The connection is closed

        log.info("Stopping D-Bus session bus for sandbox.")
        self.session_bus.stop()

        if self.system_bus:
            log.info("Stopping D-Bus system bus for sandbox.")
            self.system_bus.stop()

    def stop_daemon(self, busname):
        """Stops the daemon that owns 'busname'.

        This can be used if you want to force the miner-fs to exit, for
        example.

        """
        log.info("Stopping daemon process that owns %s.", busname)
        pid = self.daemon.get_connection_unix_process_id_sync(busname)
        if pid:
            os.kill(pid, signal.SIGTERM)
            psutil.wait_pid(pid)
            log.info("Process %i has stopped.", pid)
        else:
            log.info("Couldn't find a process owning %s.", busname)

    def get_session_bus_connection(self):
        """Return a Gio.BusConnection to the sandbox D-Bus session bus."""
        return self.session_bus.get_connection()

    def get_system_bus_connection(self):
        """Return a Gio.BusConnection to the sandbox D-Bus system bus."""
        return self.system_bus.get_connection()

    def get_session_bus_address(self):
        return self.session_bus.get_address()

    def set_config(self, schema_config_dict):
        """Set config values in multiple GSettings schemas.

        Example input:

            set_all({
                'org.freedesktop.Tracker3.Miner.Files': {
                    'enable-writeback': GLib.Variant.new_boolean(False),
                }
            })

        """

        for schema_name, contents in schema_config_dict.items():
            dconfclient = dconf.DConfClient(self)
            for key, value in contents.items():
                dconfclient.write(schema_name, key, value)
