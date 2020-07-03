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

"""Test helpers for libtracker-sparql data stores."""

import gi
gi.require_version('Tracker', '3.0')
from gi.repository import GLib
from gi.repository import GObject
from gi.repository import Tracker

import dataclasses
import logging

from . import mainloop

log = logging.getLogger(__name__)

DEFAULT_TIMEOUT = 10


class AwaitException(RuntimeError):
    pass


class AwaitTimeoutException(AwaitException):
    pass


class NoMetadataException (Exception):
    pass

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

        expected = 'a nfo:Document; nie:isStoredAs <test://url>'
        with self.tracker.await_update(DOCUMENTS_GRAPH, expected) as resource:
            # Create or update a file that's indexed by tracker-miner-fs.
            #
            # The context manager will not exit from the 'with' block until the
            # data has been inserted in the store.

        print(f"Inserted resource with urn: {resource.urn}")

    The function expects an insertion to happen, and will raise an error if the
    expected data is already present in the store. You can use
    ensure_resource() if you just want to ensure that some data is present.

    """
    def __init__(self, conn, graph, predicates, timeout=DEFAULT_TIMEOUT,
                 _check_inserted=True):
        self.conn = conn
        self.graph = graph
        self.predicates = predicates
        self.timeout = timeout
        self._check_inserted = _check_inserted

        self.loop = mainloop.MainLoop()
        self.notifier = self.conn.create_notifier()

        self.result = InsertedResource(None, 0)

    def __enter__(self):
        log.info("Awaiting insertion of resource with data %s", self.predicates)

        if self._check_inserted:
            query_check = ' '.join([
                'SELECT ?urn tracker:id(?urn) ',
                f' FROM NAMED <{self.graph}> ',
                ' WHERE { ',
                '   ?urn a rdfs:Resource ; ',
                self.predicates,
                '}'
            ])
            cursor = self.conn.query(query_check)
            if cursor.next():
                raise AwaitException("Expected data is already present in the store.")

        query_filtered = ' '.join([
            'SELECT ?urn tracker:id(?urn) ',
            f' FROM NAMED <{self.graph}> ' ,
            ' WHERE { ',
            '   ?urn a rdfs:Resource ; ',
            self.predicates,
            '   FILTER (tracker:id(?urn) = ~id) '
            '}'
        ])

        stmt = self.conn.query_statement(query_filtered, None)

        def match_cb(notifier, service, graph, events):
            for event in events:
                if event.get_event_type() in [Tracker.NotifierEventType.CREATE,
                                              Tracker.NotifierEventType.UPDATE]:
                    log.debug("Processing %s event for id %s %s", event.get_event_type().value_nick, event.get_id(), event.get_urn())
                    stmt.bind_int('id', event.get_id())
                    cursor = stmt.execute(None)

                    if cursor.next():
                        self.result.urn = cursor.get_string(0)[0]
                        self.result.id = cursor.get_integer(1)
                        log.debug("Query matched! Got urn %s", self.result.urn)

                        self.loop.quit()

        def timeout_cb():
            log.info("Timeout fired after %s seconds", self.timeout)
            raise AwaitTimeoutException(
                f"Timeout ({self.timeout}s) awaiting insert of resource matching: {self.predicates}")

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


class await_property_update():
    """Context manager to await data updates by Tracker miners & extractors.

    Use like this:

        before = 'nie:isStoredAs <test://url1>'
        after = 'nie:isStoredAs <test://url2>'
        with self.tracker.await_property_update(resource_id, before, after):
            # Trigger an update of the data.

    """
    def __init__(self, conn, graph, resource_id, before_predicates, after_predicates,
                 timeout=DEFAULT_TIMEOUT):
        self.conn = conn
        self.graph = graph
        self.resource_id = resource_id
        self.before_predicates = before_predicates
        self.after_predicates = after_predicates
        self.timeout = timeout

        self.loop = mainloop.MainLoop()
        self.notifier = self.conn.create_notifier()
        self.matched = False

    def __enter__(self):
        log.info("Awaiting update of resource id %s", self.resource_id)

        query_before = ' '.join([
            'SELECT ?urn tracker:id(?urn) ',
            f' FROM NAMED <{self.graph}> ',
            ' WHERE { ',
            '   ?urn a rdfs:Resource ; ',
            self.before_predicates,
            '   . FILTER (tracker:id(?urn) = ~id) '
            '}'
        ])

        stmt_before = self.conn.query_statement(query_before, None)
        stmt_before.bind_int('id', self.resource_id)
        cursor = stmt_before.execute(None)
        if not cursor.next():
            raise AwaitException("Expected data is not present in the store.")

        query_after = ' '.join([
            'SELECT ?urn tracker:id(?urn) '
            f' FROM NAMED <{self.graph}> ',
            ' WHERE { '
           '   ?urn a rdfs:Resource ; ',
            self.after_predicates,
            '   . FILTER (tracker:id(?urn) = ~id) '
            '}'
        ])

        stmt_after = self.conn.query_statement(query_after, None)
        stmt_after.bind_int('id', self.resource_id)

        def match_cb(notifier, service, graph, events):
            for event in events:
                if event.get_event_type() == Tracker.NotifierEventType.UPDATE and event.get_id() == self.resource_id:
                    log.debug("Processing %s event for id %s", event.get_event_type(), event.get_id())
                    cursor = stmt_after.execute(None)

                    if cursor.next():
                        log.debug("Query matched!")
                        self.matched = True
                        self.loop.quit()

        def timeout_cb():
            log.info("Timeout fired after %s seconds", self.timeout)
            raise AwaitTimeoutException(
                f"Timeout ({self.timeout}s) awaiting update of resource {self.resource_id} "
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


class await_content_update():
    """Context manager to await updates to file contents.

    When a file is updated, the old information it contained is deleted from
    the store, and the new information is inserted as a new resource.

    """
    def __init__(self, conn, graph, before_resource_id, before_predicates, after_predicates,
                 timeout=DEFAULT_TIMEOUT):
        self.conn = conn
        self.graph = graph
        self.before_resource_id = before_resource_id
        self.before_predicates = before_predicates
        self.after_predicates = after_predicates
        self.timeout = timeout

        self.loop = mainloop.MainLoop()
        self.notifier = self.conn.create_notifier()
        self.matched = False

        self.result = InsertedResource(None, 0)

    def __enter__(self):
        log.info("Awaiting delete of resource id %s and creation of a new one", self.before_resource_id)

        query_before = ' '.join([
            'SELECT nie:isStoredAs(?urn) ?urn tracker:id(?urn) '
            f' FROM NAMED <{self.graph}> ',
            ' WHERE { '
            '   ?urn a rdfs:Resource ; ',
            self.before_predicates,
            '   . FILTER (tracker:id(?urn) = ~id) '
            '}'
        ])
        stmt_before = self.conn.query_statement(query_before, None)
        stmt_before.bind_int('id', self.before_resource_id)
        cursor = stmt_before.execute(None)
        if not cursor.next():
            raise AwaitException("Expected data is not present in the store.")
        file_url = cursor.get_string(0)[0]

        query_after = ' '.join([
            'SELECT ?urn tracker:id(?urn) '
            f' FROM NAMED <{self.graph}> ',
            ' WHERE { '
            '   ?urn a rdfs:Resource ; ',
            f'      nie:isStoredAs <{file_url}> ; ',
            self.after_predicates,
            '}'
        ])
        stmt_after = self.conn.query_statement(query_after, None)

        # When a file is updated, the DataObject representing the file gets
        # an UPDATE notification. The InformationElement representing the
        # content gets a DELETE and CREATE notification, because it is
        # deleted and recreated. We detect the latter situation.

        self.matched_delete = False
        def match_cb(notifier, service, graph, events):
            for event in events:
                log.debug("Received %s event for id %s", event.get_event_type().value_nick, event.get_id())
                if event.get_id() == self.before_resource_id and event.get_event_type() == Tracker.NotifierEventType.DELETE:
                    log.debug("  Matched delete")
                    self.matched_delete = True

                # The after_predicates may match after the miner-fs creates
                # the new resource, or they may only match once the extractor
                # processes the resource. The latter will be an UPDATE event.
                elif self.matched_delete and event.get_event_type() in [Tracker.NotifierEventType.CREATE, Tracker.NotifierEventType.UPDATE]:
                    cursor = stmt_after.execute(None)

                    if cursor.next():
                        self.result.urn = cursor.get_string(0)[0]
                        self.result.id = cursor.get_integer(1)
                        log.debug("Query matched! Got new urn %s", self.result.urn)

                        self.matched = True
                        self.loop.quit()

        def timeout_cb():
            log.info("Timeout fired after %s seconds", self.timeout)
            raise AwaitTimeoutException(
                f"Timeout ({self.timeout}s) awaiting update of resource {self.before_resource_id} "
                f"with URL {file_url} matching: {self.after_predicates}")

        self.signal_id = self.notifier.connect('events', match_cb)
        self.timeout_id = GLib.timeout_add_seconds(self.timeout, timeout_cb)

        return self.result

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

    def __init__(self, conn, graph, resource_id, timeout=DEFAULT_TIMEOUT):
        self.conn = conn
        self.graph = graph
        self.resource_id = resource_id
        self.timeout = timeout

        self.loop = mainloop.MainLoop()
        self.notifier = self.conn.create_notifier()
        self.matched = False

    def __enter__(self):
        log.info("Awaiting deletion of resource id %s", self.resource_id)

        query_check = ' '.join([
            'SELECT ?urn tracker:id(?urn) ',
            f'FROM NAMED <{self.graph}> ',
            ' WHERE { ',
            '   ?urn a rdfs:Resource ; ',
            '   . FILTER (tracker:id(?urn) = ~id) '
            '}'
        ])
        stmt_check = self.conn.query_statement(query_check, None)
        stmt_check.bind_int('id', self.resource_id)
        cursor = stmt_check.execute(None)
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
                f"Timeout ({self.timeout}s) awaiting removal of resource {self.resource_id} ")

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

    def await_insert(self, graph, predicates, timeout=DEFAULT_TIMEOUT):
        """Context manager that blocks until a resource is inserted."""
        return await_insert(self.conn, graph, predicates, timeout)

    def await_property_update(self, graph, resource_id, before_predicates, after_predicates,
                     timeout=DEFAULT_TIMEOUT):
        """Context manager that blocks until a resource property is updated."""
        return await_property_update(self.conn, graph, resource_id, before_predicates,
                                     after_predicates, timeout)

    def await_content_update(self, graph, before_resource_id, before_predicates, after_predicates,
                     timeout=DEFAULT_TIMEOUT):
        """Context manager that blocks until a resource is deleted and recreated."""
        return await_content_update(self.conn, graph, before_resource_id, before_predicates,
                                    after_predicates, timeout)

    def await_delete(self, graph, resource_id, timeout=DEFAULT_TIMEOUT):
        """Context manager that blocks until a resource is deleted."""
        return await_delete(self.conn, graph, resource_id, timeout)

    def ensure_resource(self, graph, predicates, timeout=DEFAULT_TIMEOUT):
        """Ensure that a resource matching 'predicates' exists in 'graph'.

        This function will block if the resource is not yet created.

        """
        await_ctx_mgr = await_insert(self.conn, graph, predicates, timeout, _check_inserted=False)
        with await_ctx_mgr as resource:
            # Check if the data was committed *before* the function was called.
            query_initial = ' '.join([
                'SELECT ?urn tracker:id(?urn) '
                f' FROM NAMED <{graph}>',
                ' WHERE { '
                '   ?urn a rdfs:Resource ; ',
                predicates,
                '}'
            ])

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
        self.conn.update(update_sparql, None)

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

    def get_content_resource_id(self, url):
        """
        Gets the internal ID for an nie:InformationElement resource.

        The InformationElement represents data stored in a file, not
        the file itself. The 'url' parameter is the URL of the file
        that stores the given content.

        """
        result = self.query(
            'SELECT tracker:id(?r) WHERE { ?r a nie:InformationElement; nie:isStoredAs "%s" }' % url)
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
