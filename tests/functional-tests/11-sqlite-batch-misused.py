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
Test the query while running BatchSparqlUpdate at the same time. This was raising
some SQLITE_MISUSED errors before.
"""
import os
from gi.repository import GObject

from common.utils import configuration as cfg
import unittest as ut
#import unittest as ut
from common.utils.storetest import CommonTrackerStoreTest as CommonTrackerStoreTest

# Number of instances per batch
BATCH_SIZE = 3000

class TestSqliteBatchMisused (CommonTrackerStoreTest):
    """
    Send big batchSparqlUpdates and run queries at the same time
    Don't run this script directly, use the bash script "force-sqlite-misused.sh" instead
    to configure properly the environment
    """
    def setUp (self):
        self.main_loop = GObject.MainLoop ()
        self.batch_counter = 0

    def test_queries_while_batch_insert (self):
        self.assertTrue (os.path.exists (cfg.generated_ttl_dir()))

        for root, dirs, files in os.walk(cfg.generated_ttl_dir()):
            for ttl_file in filter (lambda f: f.endswith (".ttl"), files):
                full_path = os.path.abspath(os.path.join (root, ttl_file))
                print full_path

                counter = 0
                current_batch = ""
                for line in open(full_path):
                    if (line.startswith ("@prefix")):
                        continue
                    current_batch += line
                    if len(line) > 1 and line[:-1].endswith ('.'):
                        counter += 1

                    if counter >= BATCH_SIZE:
                        query = "INSERT {" + current_batch + "}"
                        token = self.tracker.batch_update(
                            query,
                            timeout=20000,
                            result_handler=self.batch_success_cb,
                            error_handler=self.batch_failed_cb)
                        self.run_a_query ()
                        counter = 0
                        current_batch = ""
                        self.batch_counter += 1

        GObject.timeout_add_seconds (2, self.run_a_query)
        # Safeguard of 60 seconds. The last reply should quit the loop
        GObject.timeout_add_seconds (60, self.timeout_cb)
        self.main_loop.run ()

    def run_a_query (self):
        QUERY = "SELECT ?u ?title WHERE { ?u a nie:InformationElement; nie:title ?title. }"
        self.tracker.query(
            QUERY, timeout=20000,
            reply_handler=self.reply_cb,
            error_handler=self.error_handler)
        return True

    def reply_cb (self, obj, results, data):
        print "Query replied correctly"

    def error_handler (self, error_msg):
        print "Query failed", error_msg
        raise error_msg

    def batch_success_cb (self, obj, result, user_data):
        self.batch_counter -= 1
        if (self.batch_counter == 0):
            print "Last batch was success"
            self.timeout_cb ()
        print "Success processing a batch"

    def batch_failed_cb (self, obj, error, user_data):
        print "Failed processing a batch: %s" % error
        raise error

    def timeout_cb (self):
        print "Forced timeout after 60 sec."
        self.main_loop.quit ()
        return False

if __name__ == "__main__":
    ut.main ()
