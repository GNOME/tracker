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
Send concurrent inserts and queries to the daemon to check the concurrency.
"""
import sys,os
import unittest
import time
import random
import commands
import signal
from gi.repository import GObject

from common.utils import configuration as cfg
import unittest as ut
#import unittest as ut
from common.utils.storetest import CommonTrackerStoreTest as CommonTrackerStoreTest

AMOUNT_OF_TEST_INSTANCES = 100
AMOUNT_OF_QUERIES = 10

class TestConcurrentQuery (CommonTrackerStoreTest):
    """
    Send a bunch of queries to the daemon asynchronously, to test the queue
    holding those queries
    """
    def setUp (self):
        self.main_loop = GObject.MainLoop ()
        
        self.mock_data_insert ()
        self.finish_counter = 0
        
    def mock_data_insert (self):
        query = "INSERT {\n"
        for i in range (0, AMOUNT_OF_TEST_INSTANCES):
            query += "<test-09:instance-%d> a nco:PersonContact ; nco:fullname 'moe %d'.\n" % (i, i)
        query += "}"
        self.tracker.update (query)
        
    def mock_data_delete (self):
        query = "DELETE {\n"
        for i in range (0, AMOUNT_OF_TEST_INSTANCES):
            query += "<test-09:instance-%d> a rdfs:Resource.\n" % (i)
        query += "}"
        self.tracker.update (query)

        query = "DELETE {\n"
        for i in range (0, AMOUNT_OF_QUERIES):
            query += "<test-09:picture-%d> a rdfs:Resource.\n" % (i)
        query += "}"
        self.tracker.update (query)


    def test_async_queries (self):
        QUERY = "SELECT ?u WHERE { ?u a nco:PersonContact. FILTER regex (?u, 'test-09:ins')}"
        UPDATE = "INSERT { <test-09:picture-%d> a nmm:Photo. }"
        for i in range (0, AMOUNT_OF_QUERIES):
            self.tracker.query(
                QUERY,
                result_handler=self.reply_cb,
                error_handler=self.error_handler)
            self.tracker.update(
                UPDATE % (i),
                result_handler=self.update_cb,
                error_handler=self.error_handler)

        # Safeguard of 60 seconds. The last reply should quit the loop
        GObject.timeout_add_seconds (60, self.timeout_cb)
        self.main_loop.run ()
        
    def reply_cb (self, obj, results, data):
        self.finish_counter += 1
        self.assertEquals (len (results), AMOUNT_OF_TEST_INSTANCES)
        if (self.finish_counter >= AMOUNT_OF_QUERIES):
            self.timeout_cb ()

    def update_cb (self, obj, results, data):
        self.assertTrue (True)

    def error_handler (self, obj, e, user_data):
        print "ERROR in DBus call: %s" % e
        raise(e)

    def timeout_cb (self):
        self.mock_data_delete ()
        self.main_loop.quit ()
        return False

if __name__ == "__main__":
    ut.main ()
