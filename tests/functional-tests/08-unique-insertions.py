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
Replicate the behaviour of the miner inserting information in the store.
"""
import unittest
import random

from common.utils import configuration as cfg
import unittest as ut
#import unittest as ut
from common.utils.storetest import CommonTrackerStoreTest as CommonTrackerStoreTest

class TestMinerInsertBehaviour (CommonTrackerStoreTest):
    """
    Mimic the behaviour of the miner, removing the previous information of the resource
    and inserting a new one.
    """

    def test_miner_unique_insertion (self):
        """
        We actually can't test tracker-miner-fs, so we mimick its behavior in this test
        1. Insert one resource
        2. Update it like tracker-miner-fs does
        3. Check there isn't any duplicate
        4. Clean up
        """

        resource = 'graph://test/resource/1'

        insert_sparql = """
        DELETE { ?r a rdfs:Resource } WHERE { GRAPH <graph://test/resource/1> { ?r a rdfs:Resource } }
        INSERT INTO <graph://test/resource/1> {
           _:resource a nie:DataObject ;
                      nie:url "%s" .
        }
        """ % resource

        select_sparql = """
        SELECT ?u { ?u nie:url "%s" }
        """ % resource

        delete_sparql = """
        DELETE { ?r a rdfs:Resource } WHERE { GRAPH <graph://test/resource/1> { ?r a rdfs:Resource } }
        """

        ''' First insertion '''
        self.tracker.update (insert_sparql)

        results = self.tracker.query (select_sparql)
        self.assertEquals (len(results), 1)

        ''' Second insertion / update '''
        self.tracker.update (insert_sparql)

        results = self.tracker.query (select_sparql)
        self.assertEquals (len(results), 1)

        ''' Clean up '''
        self.tracker.update (delete_sparql)

        results = self.tracker.query (select_sparql)
        self.assertEquals (len(results), 0)


if __name__ == '__main__':
    ut.main()
