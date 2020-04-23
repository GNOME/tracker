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
from gi.repository import GLib
from gi.repository import GObject

import atexit
import dataclasses
import logging
import os
import signal

from . import dbusdaemon
from . import mainloop
from . import psutil_mini as psutil


log = logging.getLogger(__name__)


class AwaitException(RuntimeError):
    pass


class AwaitTimeoutException(AwaitException):
    pass


class NoMetadataException (Exception):
    pass


DEFAULT_TIMEOUT = 10


_process_list = []


def _cleanup_processes():
    for process in _process_list:
        log.debug("helpers._cleanup_processes: stopping %s", process)
        process.stop()


atexit.register(_cleanup_processes)


@dataclasses.dataclass
class InsertedResource():
    """Wraps the 'urn' value returned by await_insert context manager.

    We can't return the value directly as we don't know it until the context
    manager exits.

    """
    urn: str
    id: int


class await_insert():
    """Context manager to await data insertion by Tracker miners & extractors.

    Use like this:

        expected = 'a nfo:Document; nie:url <test://url>'
        with self.tracker.await_update(expected) as resource:
            # Create or update a file that's indexed by tracker-miner-fs.
            #
            # The context manager will not exit from the 'with' block until the
            # data has been inserted in the store.

        print(f"Inserted resource with urn: {resource.urn}")

    The function expects an insertion to happen, and will raise an error if the
    expected data is already present in the store. You can use
    ensure_resource() if you just want to ensure that some data is present.

    """
    def __init__(self, conn, predicates, timeout=DEFAULT_TIMEOUT,
                 _check_inserted=True):
        self.conn = conn
        self.predicates = predicates
        self.timeout = timeout
        self._check_inserted = _check_inserted

        self.loop = mainloop.MainLoop()
        self.notifier = self.conn.create_notifier(Tracker.NotifierFlags.NONE)

        self.result = InsertedResource(None, 0)

    def __enter__(self):
        log.info("Awaiting insertion of resource with data %s", self.predicates)

        if self._check_inserted:
            query_check = ' '.join([
                'SELECT ?urn tracker:id(?urn) '
                ' WHERE { '
                '   ?urn a rdfs:Resource ; ',
                self.predicates,
                '}'
            ])
            cursor = self.conn.query(query_check)
            if cursor.next():
                raise AwaitException("Expected data is already present in the store.")

        query_filtered = ' '.join([
            'SELECT ?urn tracker:id(?urn) '
            ' WHERE { '
            '   ?urn a rdfs:Resource ; ',
            self.predicates,
            #'   FILTER (tracker:id(?urn) = ~id) '
            '   . FILTER (tracker:id(?urn) = %s) '
            '}'
        ])

        # FIXME: doesn't work with bus backend: https://gitlab.gnome.org/GNOME/tracker/issues/179
        #stmt = self.conn.query_statement(query, None)

        def match_cb(notifier, service, graph, events):
            for event in events:
                if event.get_event_type() in [Tracker.NotifierEventType.CREATE,
                                              Tracker.NotifierEventType.UPDATE]:
                    log.debug("Processing %s event for id %s", event.get_event_type(), event.get_id())
                    #stmt.bind_int('~id', event.get_id())
                    #cursor = stmt.execute(None)
                    stmt = query_filtered % event.get_id()
                    log.debug("Running %s", stmt)
                    cursor = self.conn.query(stmt)

                    if cursor.next():
                        self.result.urn = cursor.get_string(0)[0]
                        self.result.id = cursor.get_integer(1)
                        log.debug("Query matched! Got urn %s", self.result.urn)

                        self.loop.quit()

        def timeout_cb():
            log.info("Timeout fired after %s seconds", self.timeout)
            raise AwaitTimeoutException(
                f"Timeout awaiting insert of resource matching: {self.predicates}")

        self.signal_id = self.notifier.connect('events', match_cb)
        self.timeout_id = GLib.timeout_add_seconds(self.timeout, timeout_cb)

        return self.result

    def __exit__(self, etype, evalue, etraceback):
        if etype is not None:
            return False

        while self.result.urn is None:
            self.loop.run_checked()
            log.debug("Got urn %s", self.result.urn)

        GLib.source_remove(self.timeout_id)
        GObject.signal_handler_disconnect(self.notifier, self.signal_id)

        return True


class await_update():
    """Context manager to await data updates by Tracker miners & extractors.

    Use like this:

        before = 'nie:url <test://url1>'
        after = 'nie:url <test://url2>'
        with self.tracker.await_update(resource_id, before, after):
            # Trigger an update of the data.

    """
    def __init__(self, conn, resource_id, before_predicates, after_predicates,
                 timeout=DEFAULT_TIMEOUT):
        self.conn = conn
        self.resource_id = resource_id
        self.before_predicates = before_predicates
        self.after_predicates = after_predicates
        self.timeout = timeout

        self.loop = mainloop.MainLoop()
        self.notifier = self.conn.create_notifier(Tracker.NotifierFlags.NONE)
        self.matched = False

    def __enter__(self):
        log.info("Awaiting update of resource id %s", self.resource_id)

        query_before = ' '.join([
            'SELECT ?urn tracker:id(?urn) '
            ' WHERE { '
            '   ?urn a rdfs:Resource ; ',
            self.before_predicates,
            '   . FILTER (tracker:id(?urn) = %s) '
            '}'
        ]) % self.resource_id
        cursor = self.conn.query(query_before)
        if not cursor.next():
            raise AwaitException("Expected data is not present in the store.")

        self.stored_as = cursor.get_string(0)[0]

        query_on_create = 'SELECT nie:isStoredAs(tracker:uri(%s)) { }'
        query_after = ' '.join([
            'SELECT ?urn tracker:id(?urn) '
            ' WHERE { '
            '   ?urn a rdfs:Resource ; ',
            self.after_predicates,
            '   . FILTER (tracker:id(?urn) = %s) ',
            '}'
        ])

        def match_cb(notifier, service, graph, events):
            for event in events:
                log.debug("Processing %s event for id %s", event.get_event_type(), event.get_id())
                if event.get_event_type() == Tracker.NotifierEventType.DELETE and event.get_id() == self.resource_id:
                    self.resource_deleted = True
                elif event.get_event_type() in [Tracker.NotifierEventType.CREATE,
                                                Tracker.NotifierEventType.UPDATE]:
                    if event.get_event_type() == Tracker.NotifierEventType.CREATE:
                        if not self.resource_deleted:
                            raise AwaitException("Received insert with no prior delete")
                        cursor = self.conn.query(query_on_create % event.get_id())
                        if cursor.next() and cursor.get_string(0)[0] == self.stored_as:
                            self.resource_id = event.get_id()

                    log.debug("Running %s", query_after % event.get_id())
                    cursor = self.conn.query(query_after % event.get_id())

                    if cursor.next():
                        log.debug("Query matched!")
                        self.matched = True
                        self.loop.quit()

        def timeout_cb():
            log.info("Timeout fired after %s seconds", self.timeout)
            raise AwaitTimeoutException(
                f"Timeout awaiting update of resource {self.resource_id} "
                f"matching: {self.after_predicates}")

        self.signal_id = self.notifier.connect('events', match_cb)
        self.timeout_id = GLib.timeout_add_seconds(self.timeout, timeout_cb)

    def __exit__(self, etype, evalue, etraceback):
        if etype is not None:
            return False

        while not self.matched:
            self.loop.run_checked()

        GLib.source_remove(self.timeout_id)
        GObject.signal_handler_disconnect(self.notifier, self.signal_id)

        return True


class await_delete():
    """Context manager to await removal of a resource."""

    def __init__(self, conn, resource_id, timeout=DEFAULT_TIMEOUT):
        self.conn = conn
        self.resource_id = resource_id
        self.timeout = timeout

        self.loop = mainloop.MainLoop()
        self.notifier = self.conn.create_notifier(Tracker.NotifierFlags.NONE)
        self.matched = False

    def __enter__(self):
        log.info("Awaiting deletion of resource id %s", self.resource_id)

        query_check = ' '.join([
            'SELECT ?urn tracker:id(?urn) '
            ' WHERE { '
            '   ?urn a rdfs:Resource ; ',
            '   . FILTER (tracker:id(?urn) = %s) '
            '}'
        ])
        cursor = self.conn.query(query_check % self.resource_id)
        if not cursor.next():
            raise AwaitException(
                "Resource with id %i isn't present in the store.", self.resource_id)

        def match_cb(notifier, service, graph, events):
            for event in events:
                if event.get_event_type() == Tracker.NotifierEventType.DELETE:
                    log.debug("Received %s event for id %s", event.get_event_type(), event.get_id())
                    if event.get_id() == self.resource_id:
                        log.debug("Matched expected id %s", self.resource_id)
                        self.matched = True
                        self.loop.quit()
                else:
                    log.debug("Received %s event for id %s", event.get_event_type(), event.get_id())

        def timeout_cb():
            log.info("Timeout fired after %s seconds", self.timeout)
            raise AwaitTimeoutException(
                f"Timeout awaiting removal of resource {self.resource_id} ")

        self.signal_id = self.notifier.connect('events', match_cb)
        self.timeout_id = GLib.timeout_add_seconds(self.timeout, timeout_cb)

        return None

    def __exit__(self, etype, evalue, etraceback):
        if etype is not None:
            return False

        while not self.matched:
            self.loop.run_checked()

        GLib.source_remove(self.timeout_id)
        GObject.signal_handler_disconnect(self.notifier, self.signal_id)

        return True


class StoreHelper():
    """
    Helper for testing database access with libtracker-sparql.
    """

    def __init__(self, conn):
        self.log = logging.getLogger(__name__)
        self.loop = mainloop.MainLoop()

        self.conn = conn

    def await_insert(self, predicates, timeout=DEFAULT_TIMEOUT):
        """Context manager that blocks until a resource is inserted."""
        return await_insert(self.conn, predicates, timeout)

    def await_update(self, resource_id, before_predicates, after_predicates,
                     timeout=DEFAULT_TIMEOUT):
        """Context manager that blocks until a resource is updated."""
        return await_update(self.conn, resource_id, before_predicates,
                            after_predicates, timeout)

    def await_delete(self, resource_id, timeout=DEFAULT_TIMEOUT):
        """Context manager that blocks until a resource is deleted."""
        return await_delete(self.conn, resource_id, timeout)

    def ensure_resource(self, predicates, timeout=DEFAULT_TIMEOUT):
        """Ensure that a resource matching 'predicates' exists.

        This function will block if the resource is not yet created.

        """
        await_ctx_mgr = await_insert(self.conn, predicates, timeout, _check_inserted=False)
        with await_ctx_mgr as resource:
            # Check if the data was committed *before* the function was called.
            query_initial = ' '.join([
                'SELECT ?urn tracker:id(?urn) '
                ' WHERE { '
                '   ?urn a rdfs:Resource ; ',
                predicates,
                '}'
            ])

            log.debug("Running: %s", query_initial)
            cursor = self.conn.query(query_initial)
            if cursor.next():
                resource.urn = cursor.get_string(0)[0]
                resource.id = cursor.get_integer(1)
                return resource
        return resource

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
            'SELECT tracker:id(<%s>) WHERE { }' % uri)
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
            if busname.startswith('org.freedesktop.Tracker3'):
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

    def stop_miner_fs(self):
        log.info("Stopping tracker-miner-fs process.")
        pid = self.daemon.get_connection_unix_process_id_sync('org.freedesktop.Tracker1.Miner.Files')
        os.kill(pid, signal.SIGTERM)
        psutil.wait_pid(pid)

    def get_connection(self):
        return self.daemon.get_connection()
