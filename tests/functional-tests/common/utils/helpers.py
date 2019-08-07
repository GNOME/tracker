#!/usr/bin/python3
#
# Copyright (C) 2010, Nokia <jean-luc.lamadon@nokia.com>
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

from common.utils import configuration as cfg


class NoMetadataException (Exception):
    pass

REASONABLE_TIMEOUT = 30

log = logging.getLogger(__name__)


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

    BUS_NAME = None
    PROCESS_NAME = None

    def __init__(self):
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

    def _start_process(self, env=None):
        path = self.PROCESS_PATH
        flags = getattr(self,
                        "FLAGS",
                        [])

        kws = {}

        if env:
            kws['env'] = env

        command = [path] + flags
        log.debug("Starting %s", ' '.join(command))
        try:
            return subprocess.Popen([path] + flags, **kws)
        except OSError as e:
            raise RuntimeError("Error starting %s: %s" % (path, e))

    def _bus_name_appeared(self, name, owner, data):
        log.debug("[%s] appeared in the bus as %s", self.PROCESS_NAME, owner)
        self.available = True
        self.loop.quit()

    def _bus_name_vanished(self, name, data):
        log.debug("[%s] disappeared from the bus", self.PROCESS_NAME)
        self.available = False
        self.loop.quit()

    def _process_watch_cb(self):
        if self.process_watch_timeout == 0:
            # The GLib seems to call the timeout after we've removed it
            # sometimes, which causes errors unless we detect it.
            return False

        status = self.process.poll()

        if status is None:
            return True    # continue
        elif status == 0 and not self.abort_if_process_exits_with_status_0:
            return True    # continue
        else:
            self.process_watch_timeout = 0
            raise RuntimeError("%s exited with status: %i" %
                               (self.PROCESS_NAME, status))

    def _timeout_on_idle_cb(self):
        log.debug("[%s] Timeout waiting... asumming idle.", self.PROCESS_NAME)
        self.loop.quit()
        self.timeout_id = None
        return False

    def start(self, env=None):
        """
        Start an instance of process and wait for it to appear on the bus.
        """
        if self.process is not None:
            raise RuntimeError(
                "%s process already started" % self.PROCESS_NAME)

        self._bus_name_watch_id = Gio.bus_watch_name_on_connection(
            self.bus, self.BUS_NAME, Gio.BusNameWatcherFlags.NONE,
            self._bus_name_appeared, self._bus_name_vanished)
        self.loop.run()

        if self.available:
            # It's running, but we didn't start it...
            raise Exception("Unable to start test instance of %s: "
                            "already running " % self.PROCESS_NAME)

        self.process = self._start_process(env=env)
        log.debug('[%s] Started process %i',
            self.PROCESS_NAME, self.process.pid)
        self.process_watch_timeout = GLib.timeout_add(
            200, self._process_watch_cb)

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
            GLib.source_remove(self.process_watch_timeout)
            self.process_watch_timeout = 0

            self.process.terminate()

            while self.process.poll() == None:
                time.sleep(0.1)

                if time.time() > (start + REASONABLE_TIMEOUT):
                    log.debug("[%s] Failed to terminate, sending kill!",
                        self.PROCESS_NAME)
                    self.process.kill()
                    self.process.wait()

        log.debug("[%s] stopped.", self.PROCESS_NAME)

        # Run the loop until the bus name appears, or the process dies.
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

        log.debug("[%s] killed.", self.PROCESS_NAME)


class StoreHelper (Helper):
    """
    Wrapper for the Store API

    Every method tries to reconnect once if there is a dbus exception
    (some tests kill the daemon and make the connection useless)
    """

    PROCESS_NAME = "tracker-store"
    PROCESS_PATH = cfg.TRACKER_STORE_PATH
    BUS_NAME = cfg.TRACKER_BUSNAME

    def start(self, env=None):
        Helper.start(self, env=env)

        self.resources = Gio.DBusProxy.new_sync(
            self.bus, Gio.DBusProxyFlags.DO_NOT_AUTO_START, None,
            cfg.TRACKER_BUSNAME, cfg.TRACKER_OBJ_PATH, cfg.RESOURCES_IFACE)

        self.backup_iface = Gio.DBusProxy.new_sync(
            self.bus, Gio.DBusProxyFlags.DO_NOT_AUTO_START, None,
            cfg.TRACKER_BUSNAME, cfg.TRACKER_BACKUP_OBJ_PATH, cfg.BACKUP_IFACE)

        self.stats_iface = Gio.DBusProxy.new_sync(
            self.bus, Gio.DBusProxyFlags.DO_NOT_AUTO_START, None,
            cfg.TRACKER_BUSNAME, cfg.TRACKER_STATS_OBJ_PATH, cfg.STATS_IFACE)

        self.status_iface = Gio.DBusProxy.new_sync(
            self.bus, Gio.DBusProxyFlags.DO_NOT_AUTO_START, None,
            cfg.TRACKER_BUSNAME, cfg.TRACKER_STATUS_OBJ_PATH, cfg.STATUS_IFACE)

        log.debug("[%s] booting...", self.PROCESS_NAME)
        self.status_iface.Wait()
        log.debug("[%s] ready.", self.PROCESS_NAME)

    def stop(self):
        Helper.stop(self)

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
