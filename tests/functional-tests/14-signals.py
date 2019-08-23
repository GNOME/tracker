#!/usr/bin/python3
#
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
Test that after insertion/remove/updates in the store, the signals
are emitted. Theses tests are not extensive (only few selected signals
are tested)
"""

from gi.repository import Gio
from gi.repository import GLib

import time
import unittest as ut

from storetest import CommonTrackerStoreTest as CommonTrackerStoreTest


GRAPH_UPDATED_SIGNAL = "GraphUpdated"

CONTACT_CLASS_URI = "http://www.semanticdesktop.org/ontologies/2007/03/22/nco#PersonContact"

REASONABLE_TIMEOUT = 10  # Time waiting for the signal to be emitted


class TrackerStoreSignalsTests (CommonTrackerStoreTest):
    """
    Insert/update/remove instances from nco:PersonContact
    and check that the signals are emitted
    """

    def setUp(self):
        self.clean_up_list = []

        self.loop = GLib.MainLoop()
        self.timeout_id = 0

        self.bus = Gio.bus_get_sync(Gio.BusType.SESSION, None)

        self.results_classname = None
        self.results_deletes = None
        self.results_inserts = None

    def tearDown(self):
        for uri in self.clean_up_list:
            self.tracker.update("DELETE { <%s> a rdfs:Resource }" % uri)

        self.clean_up_list = []

    def __connect_signal(self):
        """
        After connecting to the signal, call self.__wait_for_signal.
        """
        self.cb_id = self.bus.signal_subscribe(
            sender=self.tracker.TRACKER_BUSNAME,
            interface_name=self.tracker.RESOURCES_IFACE,
            member=GRAPH_UPDATED_SIGNAL,
            object_path=self.tracker.TRACKER_OBJ_PATH,
            arg0=CONTACT_CLASS_URI,
            flags=Gio.DBusSignalFlags.NONE,
            callback=self.__signal_received_cb)

    def __wait_for_signal(self):
        """
        In the callback of the signals, there should be a self.loop.quit ()
        """
        self.timeout_id = GLib.timeout_add_seconds(
            REASONABLE_TIMEOUT, self.__timeout_on_idle)
        self.loop.run()

    def __timeout_on_idle(self):
        self.loop.quit()
        self.fail("Timeout, the signal never came!")

    def __pretty_print_array(self, array):
        for g, s, o, p in array:
            uri, prop, value = self.tracker.query(
                "SELECT tracker:uri (%s), tracker:uri (%s), tracker:uri (%s) WHERE {}" % (s, o, p))
            print(" - (", "-".join([g, uri, prop, value]), ")")

    def __signal_received_cb(self, connection, sender_name, object_path, interface_name, signal_name, parameters):
        """
        Save the content of the signal and disconnect the callback
        """
        classname, deletes, inserts = parameters.unpack()

        self.results_classname = classname
        self.results_deletes = deletes
        self.results_inserts = inserts

        if (self.timeout_id != 0):
            GLib.source_remove(self.timeout_id)
            self.timeout_id = 0
        self.loop.quit()
        self.bus.signal_unsubscribe(self.cb_id)

    def test_01_insert_contact(self):
        self.clean_up_list.append("test://signals-contact-add")
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
        self.__connect_signal()
        self.tracker.update(CONTACT)
        time.sleep(1)
        self.__wait_for_signal()

        # validate results
        self.assertEqual(len(self.results_deletes), 0)
        self.assertEqual(len(self.results_inserts), 6)

    def test_02_remove_contact(self):
        CONTACT = """
        INSERT {
         <test://signals-contact-remove> a nco:PersonContact ;
             nco:nameGiven 'Contact-name removed';
             nco:nameFamily 'Contact-family removed'.
        }
        """
        self.__connect_signal()
        self.tracker.update(CONTACT)
        self.__wait_for_signal()

        self.__connect_signal()
        self.tracker.update ("""
            DELETE { <test://signals-contact-remove> a rdfs:Resource }
            """)
        self.__wait_for_signal()

        # Validate results:
        self.assertEqual(len(self.results_deletes), 1)
        self.assertEqual(len(self.results_inserts), 0)

    def test_03_update_contact(self):
        self.clean_up_list.append("test://signals-contact-update")

        self.__connect_signal()
        self.tracker.update(
            "INSERT { <test://signals-contact-update> a nco:PersonContact }")
        self.__wait_for_signal()

        self.__connect_signal()
        self.tracker.update(
            "INSERT { <test://signals-contact-update> nco:fullname 'wohoo'}")
        self.__wait_for_signal()

        self.assertEqual(len(self.results_deletes), 0)
        self.assertEqual(len(self.results_inserts), 1)

    def test_04_fullupdate_contact(self):
        self.clean_up_list.append("test://signals-contact-fullupdate")

        self.__connect_signal()
        self.tracker.update(
            "INSERT { <test://signals-contact-fullupdate> a nco:PersonContact; nco:fullname 'first value' }")
        self.__wait_for_signal()

        self.__connect_signal()
        self.tracker.update ("""
               DELETE { <test://signals-contact-fullupdate> nco:fullname ?x }
               WHERE { <test://signals-contact-fullupdate> a nco:PersonContact; nco:fullname ?x }
               
               INSERT { <test://signals-contact-fullupdate> nco:fullname 'second value'}
               """)
        self.__wait_for_signal()

        self.assertEqual(len(self.results_deletes), 1)
        self.assertEqual(len(self.results_inserts), 1)


if __name__ == "__main__":
    ut.main()
