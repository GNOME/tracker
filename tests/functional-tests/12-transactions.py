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
Make sure that when COMMIT returns, the data is in the DB
"""
import time

from common.utils import configuration as cfg
from common.utils.helpers import StoreHelper as StoreHelper
import unittest2 as ut
#import unittest as ut
from common.utils.storetest import CommonTrackerStoreTest as CommonTrackerStoreTest

TEST_INSTANCE_PATTERN = "test://12-transactions-%d"

class TrackerTransactionsTest (CommonTrackerStoreTest):
    """
    In a loop:
       1. Inserts a Batch of instances
       2. Wait for batch commit
       3. Abort (SIGKILL) the daemon
       4. Restart daemon a query # of instances

    If the commit was real, all the inserted instances should be there.
    """

    def setUp (self):
        self.instance_counter = 0

    def tearDown (self):
        print "Tear down (will take some time to remove all resources)"
        delete_sparql = "DELETE { ?u a rdfs:Resource } WHERE { ?u a nmo:Email} \n"
        self.tracker.update (delete_sparql,
                             timeout=60000)
        self.instance_counter = 0

    def insert_and_commit (self, number):
        insert_sparql = "INSERT {\n"
        for i in range (0, number):
            insert_sparql += "  <" + TEST_INSTANCE_PATTERN % (self.instance_counter) + ">"
            insert_sparql += " a nmo:Email.\n "
            self.instance_counter += 1

        insert_sparql += "}"
        self.tracker.batch_update (insert_sparql)
        #print "Waiting for commit (", number," instances)"
        #start = time.time ()
        self.tracker.batch_commit ()
        #end = time.time ()
        #print "BatchCommit returned (after %d s.)" % (end - start)


    def test_commit_and_abort (self):

        for i in range (0, 20):
            NUMBER_OF_INSTANCES = 1000
            self.insert_and_commit (NUMBER_OF_INSTANCES)

            self.tracker.kill()
            self.tracker.start()
            try:
                results = self.tracker.count_instances ("nmo:Email")
            except:
                print "Timeout, probably replaying journal or something (wait 20 sec.)"
                time.sleep (20)
                results = self.tracker.count_instances ()

            # Every iteration we are adding new instances in the store!
            self.assertEquals (results, NUMBER_OF_INSTANCES * (i+1))

if __name__ == "__main__":
    ut.main ()
