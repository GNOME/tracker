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

import atexit
import logging
import os
import signal

from . import dbusdaemon
from . import mainloop
from . import psutil_mini as psutil


log = logging.getLogger(__name__)


class GraphUpdateTimeoutException(RuntimeError):
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
    Helper for testing the tracker-store daemon.
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

    def __init__(self, dbus_connection):
        self.log = logging.getLogger(__name__)
        self.loop = mainloop.MainLoop()

        self.bus = dbus_connection
        self.graph_updated_handler_id = 0

        self.resources = Gio.DBusProxy.new_sync(
            self.bus, Gio.DBusProxyFlags.DO_NOT_AUTO_START_AT_CONSTRUCTION, None,
            self.TRACKER_BUSNAME, self.TRACKER_OBJ_PATH, self.RESOURCES_IFACE)

        self.backup_iface = Gio.DBusProxy.new_sync(
            self.bus, Gio.DBusProxyFlags.DO_NOT_AUTO_START_AT_CONSTRUCTION, None,
            self.TRACKER_BUSNAME, self.TRACKER_BACKUP_OBJ_PATH, self.BACKUP_IFACE)

        self.stats_iface = Gio.DBusProxy.new_sync(
            self.bus, Gio.DBusProxyFlags.DO_NOT_AUTO_START_AT_CONSTRUCTION, None,
            self.TRACKER_BUSNAME, self.TRACKER_STATS_OBJ_PATH, self.STATS_IFACE)

        self.status_iface = Gio.DBusProxy.new_sync(
            self.bus, Gio.DBusProxyFlags.DO_NOT_AUTO_START_AT_CONSTRUCTION, None,
            self.TRACKER_BUSNAME, self.TRACKER_STATUS_OBJ_PATH, self.STATUS_IFACE)

    def start_and_wait_for_ready(self):
        # The daemon is autostarted as soon as a method is called.
        #
        # We set a big timeout to avoid interfering when a daemon is being
        # interactively debugged.
        self.log.debug("Calling %s.Wait() method", self.STATUS_IFACE)
        self.status_iface.call_sync('Wait', None, Gio.DBusCallFlags.NONE, 1000000, None)
        self.log.debug("Ready")

    def start_watching_updates(self):
        assert self.graph_updated_handler_id == 0

        self.reset_graph_updates_tracking()

        def signal_handler(proxy, sender_name, signal_name, parameters):
            if signal_name == 'GraphUpdated':
                self._graph_updated_cb(*parameters.unpack())

        self.graph_updated_handler_id = self.resources.connect(
            'g-signal', signal_handler)
        self.log.debug("Watching for updates from Resources interface")

    def stop_watching_updates(self):
        if self.graph_updated_handler_id != 0:
            self.log.debug("No longer watching for updates from Resources interface")
            self.resources.disconnect(self.graph_updated_handler_id)
            self.graph_updated_handler_id = 0

    # A system to follow GraphUpdated and make sure all changes are tracked.
    # This code saves every change notification received, and exposes methods
    # to await insertion or deletion of a certain resource which first check
    # the list of events already received and wait for more if the event has
    # not yet happened.

    def reset_graph_updates_tracking(self):
        self.class_to_track = None
        self.inserts_list = []
        self.deletes_list = []
        self.inserts_match_function = None
        self.deletes_match_function = None

    def _graph_updated_timeout_cb(self):
        raise GraphUpdateTimeoutException()

    def _graph_updated_cb(self, class_name, deletes_list, inserts_list):
        """
        Process notifications from tracker-store on resource changes.
        """
        exit_loop = False

        if class_name == self.class_to_track:
            self.log.debug("GraphUpdated for %s: %i deletes, %i inserts", class_name, len(deletes_list), len(inserts_list))

            if inserts_list is not None:
                if self.inserts_match_function is not None:
                    # The match function will remove matched entries from the list
                    (exit_loop, inserts_list) = self.inserts_match_function(inserts_list)
                self.inserts_list += inserts_list

            if not exit_loop and deletes_list is not None:
                if self.deletes_match_function is not None:
                    (exit_loop, deletes_list) = self.deletes_match_function(deletes_list)
                self.deletes_list += deletes_list

            if exit_loop:
                GLib.source_remove(self.graph_updated_timeout_id)
                self.graph_updated_timeout_id = 0
                self.loop.quit()
        else:
            self.log.debug("Ignoring GraphUpdated for class %s, currently tracking %s", class_name, self.class_to_track)

    def _enable_await_timeout(self):
        self.graph_updated_timeout_id = GLib.timeout_add_seconds(REASONABLE_TIMEOUT,
                                                                 self._graph_updated_timeout_cb)

    def await_resource_inserted(self, rdf_class, url=None, title=None, required_property=None):
        """
        Block until a resource matching the parameters becomes available
        """
        assert (self.inserts_match_function == None)
        assert (self.class_to_track == None), "Already waiting for resource of type %s" % self.class_to_track
        assert (self.graph_updated_handler_id != 0), "You must call start_watching_updates() first."

        self.class_to_track = rdf_class

        self.matched_resource_urn = None
        self.matched_resource_id = None

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

        def match_cb(inserts_list):
            matched, remaining_events = find_resource_insertion(inserts_list)
            exit_loop = matched
            return exit_loop, remaining_events

        # Check the list of previously received events for matches
        (existing_match, self.inserts_list) = find_resource_insertion(self.inserts_list)

        if not existing_match:
            self._enable_await_timeout()
            self.inserts_match_function = match_cb
            # Run the event loop until the correct notification arrives
            try:
                self.loop.run_checked()
            except GraphUpdateTimeoutException:
                raise GraphUpdateTimeoutException("Timeout waiting for resource: class %s, URL %s, title %s" % (rdf_class, url, title)) from None
            self.inserts_match_function = None

        self.class_to_track = None
        return (self.matched_resource_id, self.matched_resource_urn)

    def await_resource_deleted(self, rdf_class, id):
        """
        Block until we are notified of a resources deletion
        """
        assert (self.deletes_match_function == None)
        assert (self.class_to_track == None)
        assert (self.graph_updated_handler_id != 0), "You must call start_watching_updates() first."

        def find_resource_deletion(deletes_list):
            self.log.debug("find_resource_deletion: looking for %i in %s", id, deletes_list)

            matched = False
            remaining_events = []

            for delete in deletes_list:
                if delete[1] == id:
                    matched = True
                else:
                    remaining_events += [delete]

            return matched, remaining_events

        def match_cb(deletes_list):
            matched, remaining_events = find_resource_deletion(deletes_list)
            exit_loop = matched
            return exit_loop, remaining_events

        self.log.debug("Await deletion of %i (%i existing)", id, len(self.deletes_list))

        (existing_match, self.deletes_list) = find_resource_deletion(self.deletes_list)

        if not existing_match:
            self._enable_await_timeout()
            self.class_to_track = rdf_class
            self.deletes_match_function = match_cb
            # Run the event loop until the correct notification arrives
            try:
                self.loop.run_checked()
            except GraphUpdateTimeoutException as e:
                raise GraphUpdateTimeoutException("Resource %i has not been deleted." % id) from e
            self.deletes_match_function = None
            self.class_to_track = None

        return

    def await_property_changed(self, rdf_class, subject_id, property_uri):
        """
        Block until a property of a resource is updated or inserted.
        """
        assert (self.inserts_match_function == None)
        assert (self.deletes_match_function == None)
        assert (self.class_to_track == None)
        assert (self.graph_updated_handler_id != 0), "You must call start_watching_updates() first."

        self.log.debug("Await change to %i %s (%i, %i existing)", subject_id, property_uri, len(self.inserts_list), len(self.deletes_list))

        self.class_to_track = rdf_class

        property_id = self.get_resource_id_by_uri(property_uri)

        def find_property_change(event_list):
            matched = False
            remaining_events = []

            for event in event_list:
                if event[1] == subject_id and event[2] == property_id:
                    self.log.debug("Matched property change: %s", str(event))
                    matched = True
                else:
                    remaining_events += [event]

            return matched, remaining_events

        def match_cb(event_list):
            matched, remaining_events = find_property_change(event_list)
            exit_loop = matched
            return exit_loop, remaining_events

        # Check the list of previously received events for matches
        (existing_match, self.inserts_list) = find_property_change(self.inserts_list)
        (existing_match, self.deletes_list) = find_property_change(self.deletes_list)

        if not existing_match:
            self._enable_await_timeout()
            self.inserts_match_function = match_cb
            self.deletes_match_function = match_cb
            # Run the event loop until the correct notification arrives
            try:
                self.loop.run_checked()
            except GraphUpdateTimeoutException:
                raise GraphUpdateTimeoutException(
                    "Timeout waiting for property change, subject %i property %s (%i)" % (subject_id, property_uri, property_id))
            self.inserts_match_function = None
            self.deletes_match_function = None
            self.class_to_track = None

    # Note: The methods below call the tracker-store D-Bus API directly. This
    # is useful for testing this API surface, but we recommand that all regular
    # applications use libtracker-sparql library to talk to the database.

    def query(self, query, **kwargs):
        return self.resources.SparqlQuery('(s)', query, **kwargs)

    def update(self, update_sparql, **kwargs):
        return self.resources.SparqlUpdate('(s)', update_sparql, **kwargs)

    def load(self, ttl_uri, **kwargs):
        return self.resources.Load('(s)', ttl_uri, **kwargs)

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


class TrackerDBusSandbox:
    """
    Private D-Bus session bus which executes a sandboxed Tracker instance.

    """
    def __init__(self, dbus_daemon_config_file, extra_env=None):
        self.dbus_daemon_config_file = dbus_daemon_config_file
        self.extra_env = extra_env or {}

        self.daemon = dbusdaemon.DBusDaemon()

    def start(self):
        env = os.environ
        env.update(self.extra_env)
        env['G_MESSAGES_PREFIXED'] = 'all'

        # Precreate runtime dir, to avoid this warning from dbus-daemon:
        #
        #    Unable to set up transient service directory: XDG_RUNTIME_DIR "/home/sam/tracker-tests/tmp_59i3ev1/run" not available: No such file or directory
        #
        xdg_runtime_dir = env.get('XDG_RUNTIME_DIR')
        if xdg_runtime_dir:
            os.makedirs(xdg_runtime_dir, exist_ok=True)

        log.info("Starting D-Bus daemon for sandbox.")
        log.debug("Added environment variables: %s", self.extra_env)
        self.daemon.start_if_needed(self.dbus_daemon_config_file, env=env)

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
