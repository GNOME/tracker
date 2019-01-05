#!/usr/bin/python
# -*- coding: utf-8 -*-
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
Stand-alone tests cases for the store, checking the collation is working
"""
import time
import random
import locale

from common.utils import configuration as cfg
import unittest as ut
#import unittest as ut
from common.utils.storetest import CommonTrackerStoreTest as CommonTrackerStoreTest

class TrackerStoreCollationTests (CommonTrackerStoreTest):
    """
    Insert few instances with a text field containing collation-problematic words.
    Ask for those instances order by the field and check the results.
    """
    def setUp (self):
        """
        Each test append to this list the used URIS, so they can be removed
        in the tearDown
        """
        self.clean_up_instances = []

    def tearDown (self):
        for uri in self.clean_up_instances:
            self.tracker.update ("DELETE { <%s> a rdfs:Resource. }" % (uri))
        self.clean_up_instances = []
        time.sleep (1)

    def __insert_text (self, text):
        uri = "test://collation-01-%d" % (random.randint (1, 1000))
        # There is a remote chance to get a duplicate int
        while (uri in self.clean_up_instances):
            uri = "test://collation-01-%d" % (random.randint (1, 1000))
        self.clean_up_instances.append (uri)
        
        self.tracker.update ("""
        INSERT {
            <%s> a nie:InformationElement ;
                nie:title "%s" ;
                nie:description "tracker-collation-test-instance" .
        }
         """ % (uri, text))

    def get_text_sorted_by_collation (self):
        return self.tracker.query ("""
         SELECT ?title WHERE {
            ?u a nie:InformationElement ;
               nie:title ?title ;
               nie:description 'tracker-collation-test-instance' .
         } ORDER BY ?title
        """)

    def __collation_test (self, input_list, expected_list):

        for i in input_list:
            self.__insert_text (i)

        results = [r[0].decode('utf-8') for r in self.get_text_sorted_by_collation ()]
        self.assertEquals (len (results), len (expected_list))

        for r in range (0, len (results)):
            self.assertEquals (results[r], expected_list [r],
                               """Error:
                                  Expected : *** %s
                                  Result   : *** %s
                                  Using locale (%s, %s)
                               """ % (expected_list,
                                      results,
                                      locale.getdefaultlocale ()[0],
                                      locale.getdefaultlocale ()[1]))

    def test_collation_01 (self):
        """
        Behaves as case-insensitive
        """
        input_dt = ["abb", "bb",  "Abc", "Ba"]
        expected = ["abb", "Abc", "Ba",  "bb"]
        self.__collation_test (input_dt, expected)

    def test_collation_02 (self):
        """
        In conflict, Capital letters go *after* small letters
        """
        input_dt = ["Bb", "bb", "aa", "Aa"]
        expected = ["aa", "Aa", "bb", "Bb"]
        self.__collation_test (input_dt, expected)

    def test_collation_03 (self):
        """
        Example from the unicode spec
        http://www.unicode.org/reports/tr10/#Main_Algorithm
        """
        input_dt = ["Cab", "cab", "dab", "cáb"]
        expected = ["cab", "Cab", u"cáb", "dab"]
        self.__collation_test (input_dt, expected)

    def test_collation_04 (self):
        """
        Spanish test in english locale
        """
        input_dt = ["ä", "ö", "a", "e", "i", "o", "u"]
        expected = ["a", u"ä", "e", "i", "o", u"ö", "u"]
        self.__collation_test (input_dt, expected)

if __name__ == "__main__":
    print """
    TODO:
      * Check what happens in non-english encoding
      * Dynamic change of collation (not implemented yet in tracker)
    """
    ut.main ()

    
