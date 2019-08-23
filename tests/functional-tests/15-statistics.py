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
Stand-alone tests cases for the store, checking the statistics
are updated when different operations are executed on the store
"""
import time

import unittest as ut
from storetest import CommonTrackerStoreTest as CommonTrackerStoreTest

RDFS_RESOURCE = "rdfs:Resource"
NIE_IE = "nie:InformationElement"
RDFS_CLASS = "rdfs:Class"


class TrackerStoreStatisticsTests (CommonTrackerStoreTest):
    """
    Check initial statistics, add, remove, update content and check results stats
    """

    def __get_stats(self):
        results = {}
        for classname, count in self.tracker.get_stats():
            results[str(classname)] = int(count)
        return results

    def setUp(self):
        """
        Each test append to this list the used URIS, so they can be removed
        in the tearDown
        """
        self.clean_up_instances = []

    def tearDown(self):
        for uri in self.clean_up_instances:
            self.tracker.update("DELETE { <%s> a rdfs:Resource. }" % (uri))
        self.clean_up_instances = []
        time.sleep(1)

    def test_stats_01_insert_base_class(self):
        self.clean_up_instances.append("test://stats-01")

        old_stats = self.__get_stats()
        self.tracker.update(
            "INSERT { <test://stats-01> a nie:InformationElement. }")
        new_stats = self.__get_stats()

        increased_classes = [NIE_IE, RDFS_RESOURCE]

        for k, v in new_stats.items():
            if k in increased_classes:
                self.assertEqual(old_stats[k] + 1, new_stats[k])
            else:
                self.assertEqual(old_stats[k], new_stats[k],
                                 "Class %s should have the same instances" % k)

    def test_stats_02_insert_deep_class(self):
        self.clean_up_instances.append("test://stats-02")
        old_stats = self.__get_stats()
        self.tracker.update("INSERT { <test://stats-02> a nmm:Photo. }")
        new_stats = self.__get_stats()

        increased_classes = [NIE_IE, RDFS_RESOURCE]
        new_classes = ["nmm:Photo", "nfo:Visual", "nfo:Image", "nfo:Media"]

        # There were no instances of those classes before, check they are now
        for c in new_classes:
            self.assertIn(c, new_stats)

        for k, v in new_stats.items():
            if k in increased_classes:
                self.assertEqual(old_stats[k] + 1, new_stats[k])
            elif k in new_classes:
                # This classes could exists previous or not!
                if k in old_stats:
                    self.assertEqual(old_stats[k] + 1, new_stats[k])
                else:
                    self.assertEqual(new_stats[k], 1)
            else:
                self.assertEqual(old_stats[k], new_stats[k])

    def test_stats_03_delete_deep_class(self):
        self.clean_up_instances.append("test://stats-03")
        self.tracker.update("INSERT { <test://stats-03> a nmm:Photo. }")

        old_stats = self.__get_stats()
        self.tracker.update("DELETE { <test://stats-03> a rdfs:Resource. }")
        new_stats = self.__get_stats()

        decreased_classes = [NIE_IE, RDFS_RESOURCE]
        # These classes could have no instance
        no_instances_classes = ["nmm:Photo",
                                "nfo:Visual", "nfo:Image", "nfo:Media"]

        for c in no_instances_classes:
            if (old_stats[c] == 1):
                self.assertNotIn(c, new_stats)
            else:
                self.assertEqual(old_stats[c] - 1, new_stats[c])

        for k, v in new_stats.items():
            if k in decreased_classes:
                self.assertEqual(old_stats[k] - 1, new_stats[k])
            else:
                self.assertEqual(old_stats[k], new_stats[k])

if __name__ == "__main__":
    ut.main()
