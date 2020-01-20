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

import gi
gi.require_version('Tracker', '3.0')
from gi.repository import Tracker
from gi.repository import Gio
from gi.repository import GLib

import atexit
import logging
import os
import signal

from . import dbusdaemon
from . import mainloop
from . import psutil_mini as psutil


log = logging.getLogger(__name__)


class EventTimeoutException(RuntimeError):
    pass


class NoMetadataException (Exception):
    pass


REASONABLE_TIMEOUT = 30


_process_list = []


def _cleanup_processes():
    for process in _process_list:
        log.debug("helpers._cleanup_processes: stopping %s", process)
        process.stop()


atexit.register(_cleanup_processes)


class StoreHelper():
    """
    Helper for testing database access with libtracker-sparql.
    """

    def __init__(self, conn):
        self.log = logging.getLogger(__name__)
        self.loop = mainloop.MainLoop()

        self.conn = conn
        self.notifier = None

    def start_watching_updates(self):
        assert self.notifier is None
        self.notifier = self.conn.create_notifier(Tracker.NotifierFlags.NONE)
        self.notifier.connect('events', self._notifier_events_cb)
        self.log.debug("Watching for updates from TrackerNotifier interface")

    def stop_watching_updates(self):
        if self.notifier:
            del self.notifier
            self.log.debug("No longer watching for updates from Resources interface")

    def _notifier_events_timeout_cb(self):
        raise EventTimeoutException()

    def _notifier_events_cb(self, service, graph, events, user_data):
        """
        Process notifications from store on resource changes.
        """

        self.log("Got %i events from %s, %s", len(events), service, graph)
        # FIXME: This needs to change completely.

    def _enable_await_timeout(self):
        self.graph_updated_timeout_id = GLib.timeout_add_seconds(REASONABLE_TIMEOUT,
                                                                 self._graph_updated_timeout_cb)

    def await_resource_inserted(self, rdf_class, url=None, title=None, required_property=None):
        """
        Block until a resource matching the parameters becomes available
        """

        self.log.debug("Await new %s (%i existing inserts)", rdf_class, len(self.inserts_list))

        if required_property is not None:
            required_property_id = self.get_resource_id_by_uri(required_property)
            self.log.debug("Required property %s id %i", required_property, required_property_id)

        def find_resource_insertion(inserts_list):
            matched_creation = (self.matched_resource_id is not None)
            matched_required_property = False
            remaining_events = []

            # FIXME: this could be done in an easier way: build one query that filters
            # based on every subject id in inserts_list, and returns the id of the one
            # that matched :)
            for insert in inserts_list:
                id = insert[1]

                if not matched_creation:
                    where = "  ?urn a <%s> " % rdf_class

                    if url is not None:
                        where += "; nie:url \"%s\"" % url

                    if title is not None:
                        where += "; nie:title \"%s\"" % title

                    query = "SELECT ?urn WHERE { %s FILTER (tracker:id(?urn) = %s)}" % (where, insert[1])
                    result_set = self.query(query)

                    if len(result_set) > 0:
                        matched_creation = True
                        self.matched_resource_urn = result_set[0][0]
                        self.matched_resource_id = insert[1]
                        self.log.debug("Matched creation of resource %s (%i)",
                            self.matched_resource_urn,
                             self.matched_resource_id)
                        if required_property is not None:
                            self.log.debug("Waiting for property %s (%i) to be set",
                                required_property, required_property_id)

                if required_property is not None and matched_creation and not matched_required_property:
                    if id == self.matched_resource_id and insert[2] == required_property_id:
                        matched_required_property = True
                        self.log.debug("Matched %s %s", self.matched_resource_urn, required_property)

                if not matched_creation or id != self.matched_resource_id:
                    remaining_events += [insert]

            matched = matched_creation if required_property is None else matched_required_property
            return matched, remaining_events

        return (None, None)

    def await_resource_deleted(self, rdf_class, id):
        """
        Block until we are notified of a resources deletion
        """

        return

    def await_property_changed(self, rdf_class, subject_id, property_uri):
        """
        Block until a property of a resource is updated or inserted.
        """

    def query(self, query):
        cursor = self.conn.query(query, None)
        result = []
        while cursor.next():
            row = []
            for i in range(0, cursor.get_n_columns()):
                row.append(cursor.get_string(i)[0])
            result.append(row)
        return result

    def update(self, update_sparql):
        self.conn.update(update_sparql, 0, None)

    def count_instances(self, ontology_class):
        QUERY = """
        SELECT COUNT(?u) WHERE {
            ?u a %s .
        }
        """
        result = self.query(QUERY % ontology_class)

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
            if busname.startswith('org.freedesktop.Tracker1'):
                pid = self.daemon.get_connection_unix_process_id_sync(busname)
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

    def get_connection(self):
        return self.daemon.get_connection()
