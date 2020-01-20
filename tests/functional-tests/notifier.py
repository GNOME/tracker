# Copyright (C) 2010, Nokia <ivan.frade@nokia.com>
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
Test change notifications using TrackerNotifier.
"""

import gi
gi.require_version('Tracker', '3.0')
from gi.repository import GLib
from gi.repository import Tracker

import logging
import unittest as ut

import configuration
import fixtures
import trackertestutils.mainloop


class TrackerNotifierTests():
    """
    Test cases for TrackerNotifier.

    To allow testing with both local and D-Bus connections, this test suite is
    a mixin, which is combined with different fixtures below.
    """

    def base_setup(self):
        self.loop = trackertestutils.mainloop.MainLoop()
        self.timeout_id = 0

        self.results_deletes = []
        self.results_inserts = []
        self.results_updates = []

        self.notifier = self.conn.create_notifier(Tracker.NotifierFlags.QUERY_URN)
        self.notifier.connect('events', self.__signal_received_cb)

    def __wait_for_signal(self):
        """
        In the callback of the signals, there should be a self.loop.quit ()
        """
        self.timeout_id = GLib.timeout_add_seconds(
            configuration.DEFAULT_TIMEOUT, self.__timeout_on_idle)
        self.loop.run_checked()

    def __timeout_on_idle(self):
        self.loop.quit()
        self.fail("Timeout, the signal never came after %i seconds!" % configuration.DEFAULT_TIMEOUT)

    def __signal_received_cb(self, notifier, service, graph, events):
        """
        Save the content of the signal and disconnect the callback
        """
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

    def test_01_insert_contact(self):
        CONTACT = """
        INSERT {
        <test://signals-contact-add> a nco:PersonContact ;
                nco:nameGiven 'Contact-name added';
                nco:nameFamily 'Contact-family added';
                nie:generator 'test-14-signals' ;
                nco:contactUID '1321321312312';
                nco:hasPhoneNumber <tel:555555555> .
        }
        """
        self.tracker.update(CONTACT)
        self.__wait_for_signal()

        # validate results
        self.assertEqual(len(self.results_deletes), 0)
        self.assertEqual(len(self.results_inserts), 0)
        self.assertEqual(len(self.results_updates), 1)
        print(self.results_updates[0])
        assert self.results_updates[0].get_urn() == 'test://signals-contact-add'

    def test_02_remove_contact(self):
        CONTACT = """
        INSERT {
            <test://signals-contact-remove> a nco:PersonContact ;
                nco:nameGiven 'Contact-name removed';
                nco:nameFamily 'Contact-family removed'.
        }
        """
        self.tracker.update(CONTACT)
        self.__wait_for_signal()

        self.tracker.update ("""
            DELETE { <test://signals-contact-remove> a rdfs:Resource }
            """)
        self.__wait_for_signal()

        # Validate results:
        self.assertEqual(len(self.results_deletes), 1)
        self.assertEqual(len(self.results_updates), 1)
        self.assertEqual(len(self.results_inserts), 0)

    def test_03_update_contact(self):
        self.tracker.update(
            "INSERT { <test://signals-contact-update> a nco:PersonContact }")
        self.__wait_for_signal()

        self.tracker.update(
            "INSERT { <test://signals-contact-update> nco:fullname 'wohoo'}")
        self.__wait_for_signal()

        self.assertEqual(len(self.results_updates), 2)
        self.assertEqual(len(self.results_inserts), 0)
        self.assertEqual(len(self.results_deletes), 0)

    def test_04_fullupdate_contact(self):
        self.tracker.update(
            "INSERT { <test://signals-contact-fullupdate> a nco:PersonContact; nco:fullname 'first value' }")
        self.__wait_for_signal()

        self.tracker.update ("""
               DELETE { <test://signals-contact-fullupdate> nco:fullname ?x }
               WHERE { <test://signals-contact-fullupdate> a nco:PersonContact; nco:fullname ?x }
               
               INSERT { <test://signals-contact-fullupdate> nco:fullname 'second value'}
               """)
        self.__wait_for_signal()

        self.assertEqual(len(self.results_deletes), 0)
        self.assertEqual(len(self.results_inserts), 0)
        self.assertEqual(len(self.results_updates), 2)


class TrackerLocalNotifierTest (fixtures.TrackerSparqlDirectTest, TrackerNotifierTests):
    """
    Insert/update/remove instances from nco:PersonContact
    and check that the signals are emitted.
    """

    def setUp(self):
        self.base_setup()


class TrackerBusNotifierTest (fixtures.TrackerSparqlBusTest, TrackerNotifierTests):
    """
    Insert/update/remove instances from nco:PersonContact
    and check that the signals are emitted.
    """

    def setUp(self):
        self.base_setup()


if __name__ == "__main__":
    ut.main(verbosity=2)
