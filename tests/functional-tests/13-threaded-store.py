#!/usr/bin/python
#
# Copyright (C) 2010, Nokia <ivan.frade@nokia.com>
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
Test that the threads in the daemon are working:
 A very long query shouldn't block smaller queries.
"""
import os
from gi.repository import GObject
from gi.repository import GLib
import time

from common.utils import configuration as cfg
import unittest2 as ut
#import unittest as ut
from common.utils.storetest import CommonTrackerStoreTest as CommonTrackerStoreTest

MAX_TEST_TIME = 60 # seconds to finish the tests (to avoid infinite waitings)

AMOUNT_SIMPLE_QUERIES = 10 
COMPLEX_QUERY_TIMEOUT = 15000 # ms (How long do we wait for an answer to the complex query)
SIMPLE_QUERY_FREQ = 2 # seconds (How freq do we send a simple query to the daemon)

class TestThreadedStore (CommonTrackerStoreTest):
    """
    When the database is big, running a complex query takes ages.
    After cancelling the query, any following query is queued

    Reported in bug NB#183499
    """
    def setUp (self):
        self.main_loop = GObject.MainLoop ()
        self.simple_queries_counter = AMOUNT_SIMPLE_QUERIES
        self.simple_queries_answers = 0

    def __populate_database (self):

        self.assertTrue (os.path.exists (cfg.generated_ttl_dir()))
        for ttl_file in ["010-nco_EmailAddress.ttl",
                         "011-nco_PostalAddress.ttl",
                         "012-nco_PhoneNumber.ttl",
                         "014-nco_ContactEmail.ttl",
                         "015-nco_ContactCall.ttl",
                         "018-nco_PersonContact.ttl",
                         "012-nco_PhoneNumber.ttl",
                         "016-nco_ContactIM.ttl"]:
            full_path = os.path.abspath(os.path.join (cfg.generated_ttl_dir(), ttl_file))
            print full_path
            self.tracker.get_tracker_iface().Load(
                '(s)', "file://" + full_path, timeout=30000)

    @ut.skip("Test fails with 'GDBus.Error:org.freedesktop.Tracker1.SparqlError.Internal: parser stack overflow (36)'")
    def test_complex_query (self):
        start = time.time ()
        self.__populate_database ()
        end = time.time ()
        print "Loading: %.3f sec." % (end-start)

        COMPLEX_QUERY = """
        SELECT ?url nie:url(?photo) nco:imStatusMessage (?url)
                tracker:coalesce(nco:nameFamily (?url), nco:nameFamily (?url), nco:nameGiven (?org), ?email, ?phone, nco:blogUrl (?url))
        WHERE {
          { ?url a nco:PersonContact.
            ?url fts:match 'fami*'.
          } UNION {
            ?url a nco:PersonContact.
            ?url nco:hasEmailAddress ?add.
            ?add fts:match 'fami*'.
         } UNION {
            ?url a nco:PersonContact.
            ?url nco:hasPostalAddress ?post.
            ?post fts:match 'fami*'.
         }
         OPTIONAL { ?url nco:photo ?photo.}
         OPTIONAL { ?url nco:org ?org. }
         OPTIONAL { ?url maemo:relevance ?relevance.}
         OPTIONAL { ?url nco:hasPhoneNumber ?hasphone. ?hasPhone nco:phoneNumber ?phone.}
         OPTIONAL { ?url nco:hasEmailAddress ?hasemail. ?hasemail nco:emailAddress ?email.}
         } ORDER BY ?relevance LIMIT 100"""

        # Standard timeout
        print "Send complex query"
        self.complex_start = time.time ()
        self.tracker.query(
            COMPLEX_QUERY, timeout=COMPLEX_QUERY_TIMEOUT,
            response_handler=self.reply_complex,
            error_handler=self.error_handler_complex)

        self.timeout_id = GLib.timeout_add_seconds (MAX_TEST_TIME, self.__timeout_on_idle)
        GLib.timeout_add_seconds (SIMPLE_QUERY_FREQ, self.__simple_query)
        self.main_loop.run ()

    def __simple_query (self):
        print "Send simple query (%d)" % (self.simple_queries_counter)
        SIMPLE_QUERY = "SELECT ?name WHERE { ?u a nco:PersonContact; nco:fullname ?name. }"
        self.tracker.query(
            SIMPLE_QUERY,
            timeout=10000,
            response_handler=self.reply_simple,
            error_handler=self.error_handler)
        self.simple_queries_counter -= 1
        if (self.simple_queries_counter == 0):
            print "Stop sending queries (wait)"
            return False
        return True

    def reply_simple (self, obj, results, data):
        print "Simple query answered"
        self.assertNotEquals (len (results), 0)
        self.simple_queries_answers += 1
        if (self.simple_queries_answers == AMOUNT_SIMPLE_QUERIES):
            print "All simple queries answered"
            self.main_loop.quit ()

    def reply_complex (self, obj, results, data):
        print "Complex query: %.3f" % (time.time () - self.complex_start)

    def error_handler (self, error_msg):
        print "ERROR in dbus call", error_msg

    def error_handler_complex (self, error_msg):
        print "Complex query timedout in DBus (", error_msg, ")"

    def __timeout_on_idle (self):
        print "Timeout... asumming idle"
        self.main_loop.quit ()
        return False
        

if __name__ == "__main__":
    ut.main ()
