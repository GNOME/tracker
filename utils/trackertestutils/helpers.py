#
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

from gi.repository import Gio
from gi.repository import GLib

import logging
import os
import re
import sys
import subprocess
import time


class NoMetadataException (Exception):
    pass

REASONABLE_TIMEOUT = 30


class Helper:
    """
    Abstract helper for Tracker processes. Launches the process
    and waits for it to appear on the session bus.

    The helper will fail if the process is already running. Use
    test-runner.sh to ensure the processes run inside a separate DBus
    session bus.

    The process is watched using a timed GLib main loop source. If the process
    exits with an error code, the test will abort the next time the main loop
    is entered (or straight away if currently running the main loop).
    """

    STARTUP_TIMEOUT = 200   # milliseconds
    SHUTDOWN_TIMEOUT = 200  #

    def __init__(self, helper_name, bus_name, process_path):
        self.name = helper_name
        self.bus_name = bus_name
        self.process_path = process_path

        self.log = logging.getLogger(f'{__name__}.{self.name}')

        self.process = None
        self.available = False

        self.loop = GLib.MainLoop()
        self.install_glib_excepthook(self.loop)

        self.bus = Gio.bus_get_sync(Gio.BusType.SESSION, None)

    def install_glib_excepthook(self, loop):
        """
        Handler to abort test if an exception occurs inside the GLib main loop.
        """
        old_hook = sys.excepthook

        def new_hook(etype, evalue, etb):
            old_hook(etype, evalue, etb)
            GLib.MainLoop.quit(loop)
            sys.exit(1)
        sys.excepthook = new_hook

    def _start_process(self, command_args=None, extra_env=None):
        command = [self.process_path] + (command_args or [])
        self.log.debug("Starting %s.", ' '.join(command))

        env = os.environ
        if extra_env:
            self.log.debug("  starting with extra environment: %s", extra_env)
            env.update(extra_env)

        try:
            return subprocess.Popen(command, env=env)
        except OSError as e:
            raise RuntimeError("Error starting %s: %s" % (path, e))

    def _bus_name_appeared(self, connection, name, owner):
        self.log.debug("%s appeared on the message bus, owned by %s", name, owner)
        self.available = True
        self.loop.quit()

    def _bus_name_vanished(self, connection, name):
        self.log.debug("%s vanished from the message bus", name)
        self.available = False
        self.loop.quit()

    def _process_watch_cb(self):
        if self.process_watch_timeout == 0:
            # GLib seems to call the timeout after we've removed it
            # sometimes, which causes errors unless we detect it.
            return False

        status = self.process.poll()

        if status is None:
            return True    # continue
        elif status == 0 and not self.abort_if_process_exits_with_status_0:
            return True    # continue
        else:
            self.process_watch_timeout = 0
            raise RuntimeError(f"{self.name} exited with status: {self.status}")

    def _process_startup_timeout_cb(self):
        self.log.debug(f"Process timeout of {self.STARTUP_TIMEOUT}ms was called")
        self.loop.quit()
        self.timeout_id = None
        return False

    def start(self, command_args=None, extra_env=None):
        """
        Start an instance of process and wait for it to appear on the bus.
        """
        if self.process is not None:
            raise RuntimeError("%s: already started" % self.name)

        self._bus_name_watch_id = Gio.bus_watch_name_on_connection(
            self.bus, self.bus_name, Gio.BusNameWatcherFlags.NONE,
            self._bus_name_appeared, self._bus_name_vanished)

        # We expect the _bus_name_vanished callback to be called here,
        # causing the loop to exit again.
        self.loop.run()

        if self.available:
            # It's running, but we didn't start it...
            raise RuntimeError("Unable to start test instance of %s: "
                               "already running" % self.name)

        self.process = self._start_process(command_args=command_args,
                                           extra_env=extra_env)
        self.log.debug('Started with PID %i', self.process.pid)

        self.process_startup_timeout = GLib.timeout_add(
            self.STARTUP_TIMEOUT, self._process_startup_timeout_cb)

        self.abort_if_process_exits_with_status_0 = True

        # Run the loop until the bus name appears, or the process dies.
        self.loop.run()

        self.abort_if_process_exits_with_status_0 = False

    def stop(self):
        if self.process is None:
            # Seems that it didn't even start...
            return

        start = time.time()
        if self.process.poll() == None:
            GLib.source_remove(self.process_startup_timeout)
            self.process_startup_timeout = 0

            self.process.terminate()
            returncode = self.process.wait(timeout=self.SHUTDOWN_TIMEOUT * 1000)
            if returncode is None:
                self.log.debug("Process failed to terminate in time, sending kill!")
                self.process.kill()
                self.process.wait()
            elif returncode > 0:
                self.log.warn("Process returned error code %s", returncode)

        self.log.debug("Process stopped.")

        # Run the loop to handle the expected name_vanished signal.
        self.loop.run()
        Gio.bus_unwatch_name(self._bus_name_watch_id)

        self.process = None

    def kill(self):
        if self.process_watch_timeout != 0:
            GLib.source_remove(self.process_watch_timeout)
            self.process_watch_timeout = 0

        self.process.kill()

        # Name owner changed callback should take us out from this loop
        self.loop.run()
        Gio.bus_unwatch_name(self._bus_name_watch_id)

        self.process = None

        self.log.debug("Process killed.")


class StoreHelper (Helper):
    """
    Helper for starting and testing the tracker-store daemon.
    """

    TRACKER_BUSNAME = 'org.freedesktop.Tracker1'
    TRACKER_OBJ_PATH = '/org/freedesktop/Tracker1/Resources'
    RESOURCES_IFACE = "org.freedesktop.Tracker1.Resources"

    TRACKER_BACKUP_OBJ_PATH = "/org/freedesktop/Tracker1/Backup"
    BACKUP_IFACE = "org.freedesktop.Tracker1.Backup"

    TRACKER_STATS_OBJ_PATH = "/org/freedesktop/Tracker1/Statistics"
    STATS_IFACE = "org.freedesktop.Tracker1.Statistics"

    TRACKER_STATUS_OBJ_PATH = "/org/freedesktop/Tracker1/Status"
    STATUS_IFACE = "org.freedesktop.Tracker1.Status"

    def __init__(self, process_path):
        Helper.__init__(self, "tracker-store", self.TRACKER_BUSNAME, process_path)

    def start(self, command_args=None, extra_env=None):
        Helper.start(self, command_args, extra_env)

        self.resources = Gio.DBusProxy.new_sync(
            self.bus, Gio.DBusProxyFlags.DO_NOT_AUTO_START, None,
            self.TRACKER_BUSNAME, self.TRACKER_OBJ_PATH, self.RESOURCES_IFACE)

        self.backup_iface = Gio.DBusProxy.new_sync(
            self.bus, Gio.DBusProxyFlags.DO_NOT_AUTO_START, None,
            self.TRACKER_BUSNAME, self.TRACKER_BACKUP_OBJ_PATH, self.BACKUP_IFACE)

        self.stats_iface = Gio.DBusProxy.new_sync(
            self.bus, Gio.DBusProxyFlags.DO_NOT_AUTO_START, None,
            self.TRACKER_BUSNAME, self.TRACKER_STATS_OBJ_PATH, self.STATS_IFACE)

        self.status_iface = Gio.DBusProxy.new_sync(
            self.bus, Gio.DBusProxyFlags.DO_NOT_AUTO_START, None,
            self.TRACKER_BUSNAME, self.TRACKER_STATUS_OBJ_PATH, self.STATUS_IFACE)

        self.log.debug("Calling %s.Wait() method", self.STATUS_IFACE)
        self.status_iface.Wait()
        self.log.debug("Ready")

    def stop(self):
        Helper.stop(self)

    # Note: The methods below call the tracker-store D-Bus API directly. This
    # is useful for testing this API surface, but we recommand that all regular
    # applications use libtracker-sparql library to talk to the database.

    def query(self, query, timeout=5000, **kwargs):
        return self.resources.SparqlQuery('(s)', query, timeout=timeout, **kwargs)

    def update(self, update_sparql, timeout=5000, **kwargs):
        return self.resources.SparqlUpdate('(s)', update_sparql, timeout=timeout, **kwargs)

    def load(self, ttl_uri, timeout=5000, **kwargs):
        return self.resources.Load('(s)', ttl_uri, timeout=timeout, **kwargs)

    def batch_update(self, update_sparql, **kwargs):
        return self.resources.BatchSparqlUpdate('(s)', update_sparql, **kwargs)

    def batch_commit(self, **kwargs):
        return self.resources.BatchCommit(**kwargs)

    def backup(self, backup_file, **kwargs):
        return self.backup_iface.Save('(s)', backup_file, **kwargs)

    def restore(self, backup_file, **kwargs):
        return self.backup_iface.Restore('(s)', backup_file, **kwargs)

    def get_stats(self, **kwargs):
        return self.stats_iface.Get(**kwargs)

    def get_tracker_iface(self):
        return self.resources

    def count_instances(self, ontology_class):
        QUERY = """
        SELECT COUNT(?u) WHERE {
            ?u a %s .
        }
        """
        result = self.resources.SparqlQuery('(s)', QUERY % (ontology_class))

        if (len(result) == 1):
            return int(result[0][0])
        else:
            return -1

    def get_resource_id_by_uri(self, uri):
        """
        Get the internal ID for a given resource, identified by URI.
        """
        result = self.query(
            'SELECT tracker:id(%s) WHERE { }' % uri)
        if len(result) == 1:
            return int(result[0][0])
        elif len(result) == 0:
            raise Exception("No entry for resource %s" % uri)
        else:
            raise Exception("Multiple entries for resource %s" % uri)

    # FIXME: rename to get_resource_id_by_nepomuk_url !!
    def get_resource_id(self, url):
        """
        Get the internal ID for a given resource, identified by URL.
        """
        result = self.query(
            'SELECT tracker:id(?r) WHERE { ?r nie:url "%s" }' % url)
        if len(result) == 1:
            return int(result[0][0])
        elif len(result) == 0:
            raise Exception("No entry for resource %s" % url)
        else:
            raise Exception("Multiple entries for resource %s" % url)

    def ask(self, ask_query):
        assert ask_query.strip().startswith("ASK")
        result = self.query(ask_query)
        assert len(result) == 1
        if result[0][0] == "true":
            return True
        elif result[0][0] == "false":
            return False
        else:
            raise Exception("Something fishy is going on")
