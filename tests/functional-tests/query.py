# Copyright (C) 2020, Sam Thursfield <sam@afuera.me.uk>
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
Test queries using libtracker-sparql.
"""

import gi
gi.require_version('Tracker', '3.0')
from gi.repository import GLib
from gi.repository import Tracker

import logging
import unittest as ut

import fixtures


class TrackerQueryTests():
    """
    Query test cases for TrackerSparqlConnection.

    To allow testing with both local and D-Bus connections, this test suite is
    a mixin, which is combined with different fixtures below.
    """

    def base_setup(self):
        self.loop = GLib.MainLoop.new(None, False)
        self.timeout_id = 0

        self.notifier = self.conn.create_notifier(Tracker.NotifierFlags.QUERY_URN)
        self.notifier.connect('events', self.__signal_received_cb)

    def __wait_for_events_signal(self):
        """
        In the callback of the signals, there should be a self.loop.quit ()
        """
        self.timeout_id = GLib.timeout_add_seconds(
            REASONABLE_TIMEOUT, self.__timeout_on_idle)
        self.loop.run()

    def __timeout_on_idle(self):
        self.loop.quit()
        self.fail("Timeout, the signal never came after %i seconds!" % REASONABLE_TIMEOUT)

    def __signal_received_cb(self, notifier, service, graph, events):
        """
        Save the content of the signal and disconnect the callback
        """
        logging.debug("Received TrackerNotifier::events signal with %i events", len(events))
        for event in events:
            if event.get_event_type() == Tracker.NotifierEventType.CREATE:
                self.results_inserts.append(event)
            elif event.get_event_type() == Tracker.NotifierEventType.UPDATE:
                self.results_updates.append(event)
            elif event.get_event_type() == Tracker.NotifierEventType.DELETE:
                self.results_deletes.append(event)

        if self.timeout_id != 0:
            GLib.source_remove(self.timeout_id)
            self.timeout_id = 0
        self.loop.quit()


    def test_row_types(self):
        """Check the value types returned by TrackerSparqlCursor."""

        CONTACT = """
        INSERT {
        <test://test1> a nfo:FileDataObject ;
             nie:url <file://test.test> ;
             nfo:fileSize 1234 .
        }
        """
        self.tracker.update(CONTACT)
        self.__wait_for_events_signal()

        cursor = self.conn.query('SELECT ?url ?filesize { ?url a nfo:FileDataObject }')

        cursor.next()
        assert cursor.get_n_columns() == 2
        assert cursor.get_value_type(0) == Tracker.SparqlValue.STRING
        assert cursor.get_value_type(1) == Tracker.SparqlValue.INTEGER


class TrackerLocalQueryTest (fixtures.TrackerSparqlDirectTest, TrackerQueryTests):
    def setUp(self):
        self.base_setup()


class TrackerBusQueryTest (fixtures.TrackerSparqlBusTest, TrackerQueryTests):
    def setUp(self):
        self.base_setup()


if __name__ == "__main__":
    ut.main(verbosity=2)
