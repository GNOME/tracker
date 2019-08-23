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
import subprocess

from . import mainloop

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

        self.loop = mainloop.MainLoop()

        self.bus = Gio.bus_get_sync(Gio.BusType.SESSION, None)

    def _start_process(self, command_args=None, extra_env=None):
        global _process_list
        _process_list.append(self)

        command = [self.process_path] + (command_args or [])
        self.log.debug("Starting %s.", ' '.join(command))

        env = os.environ
        if extra_env:
            self.log.debug("  starting with extra environment: %s", extra_env)
            env.update(extra_env)

        try:
            return subprocess.Popen(command, env=env)
        except OSError as e:
            raise RuntimeError("Error starting %s: %s" % (self.process_path, e))

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
        self.loop.run_checked()

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
        self.loop.run_checked()

        self.abort_if_process_exits_with_status_0 = False

    def stop(self):
        global _process_list

        if self.process is None:
            # Seems that it didn't even start...
            return

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
        self.loop.run_checked()
        Gio.bus_unwatch_name(self._bus_name_watch_id)

        self.process = None
        _process_list.remove(self)

    def kill(self):
        global _process_list

        if self.process_watch_timeout != 0:
            GLib.source_remove(self.process_watch_timeout)
            self.process_watch_timeout = 0

        self.process.kill()

        # Name owner changed callback should take us out from this loop
        self.loop.run_checked()
        Gio.bus_unwatch_name(self._bus_name_watch_id)

        self.process = None
        _process_list.remove(self)

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

        self.reset_graph_updates_tracking()

        def signal_handler(proxy, sender_name, signal_name, parameters):
            if signal_name == 'GraphUpdated':
                self._graph_updated_cb(*parameters.unpack())

        self.graph_updated_handler_id = self.resources.connect(
            'g-signal', signal_handler)

    def stop(self):
        Helper.stop(self)

        if self.graph_updated_handler_id != 0:
            self.resources.disconnect(self.graph_updated_handler_id)

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
            except GraphUpdateTimeoutException:
                raise GraphUpdateTimeoutException("Resource %i has not been deleted." % id)
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
