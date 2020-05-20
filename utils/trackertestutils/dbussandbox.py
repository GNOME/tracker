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

import atexit
import logging
import os
import signal

from . import dbusdaemon
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


class TrackerDBusSandbox:
    """
    Private D-Bus session bus which executes a sandboxed Tracker instance.

    """
    def __init__(self, dbus_daemon_config_file, extra_env=None):
        self.dbus_daemon_config_file = dbus_daemon_config_file
        self.extra_env = extra_env or {}

        self.daemon = dbusdaemon.DBusDaemon()

    def start(self, new_session=False):
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

        log.info("Starting D-Bus daemon for sandbox.")
        log.debug("Added environment variables: %s", self.extra_env)
        self.daemon.start(self.dbus_daemon_config_file, env=env, new_session=new_session)

    def stop(self):
        tracker_processes = []

        log.info("Looking for active Tracker processes on the bus")
        for busname in self.daemon.list_names_sync():
            if busname.startswith(TRACKER_DBUS_PREFIX):
                pid = self.daemon.get_connection_unix_process_id_sync(busname)
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

        log.info("Stopping D-Bus daemon for sandbox.")
        self.daemon.stop()

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

    def get_connection(self):
        return self.daemon.get_connection()
