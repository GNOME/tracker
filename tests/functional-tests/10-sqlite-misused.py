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
Test the query while importing at the same time. This was raising
some SQLITE_MISUSED errors before.
"""
import os
from gi.repository import GObject

from common.utils import configuration as cfg
import unittest as ut
#import unittest as ut
from common.utils.storetest import CommonTrackerStoreTest as CommonTrackerStoreTest

class TestSqliteMisused (CommonTrackerStoreTest):
    """
    Send queries while importing files (in .ttl directory)
    """
    def setUp (self):
        self.main_loop = GObject.MainLoop ()
        self.files_counter = 0

    def test_queries_while_import (self):
        assert os.path.isdir(cfg.generated_ttl_dir())

        for root, dirs, files in os.walk(cfg.generated_ttl_dir()):
            for ttl_file in filter (lambda f: f.endswith (".ttl"), files):
                full_path = os.path.abspath(os.path.join (root, ttl_file))
                self.files_counter += 1

                self.tracker.load(
                    "file://" + full_path, timeout=30000,
                    result_handler=self.loaded_success_cb,
                    error_handler=self.loaded_failed_cb,
                    user_data = full_path)

        GObject.timeout_add_seconds (2, self.run_a_query)
        # Safeguard of 60 seconds. The last reply should quit the loop
        # It doesn't matter if we didn't import all of the files yet.
        GObject.timeout_add_seconds (60, self.timeout_cb)
        self.main_loop.run ()

    def run_a_query (self):
        QUERY = "SELECT ?u ?title WHERE { ?u a nie:InformationElement; nie:title ?title. }"
        self.tracker.query(
            QUERY, timeout=20000,
            result_handler=self.reply_cb,
            error_handler=self.error_handler)
        return True

    def reply_cb (self, obj, results, data):
        print "Query replied correctly"

    def error_handler (self, obj, error, data):
        print "ERROR in DBus call: %s" % error

    def loaded_success_cb (self, obj, results, user_data):
        self.files_counter -= 1
        if (self.files_counter == 0):
            print "Last file loaded"
            self.timeout_cb ()
        print "Success loading %s" % user_data

    def loaded_failed_cb (self, obj, error, user_data):
        raise RuntimeError("Failed loading %s: %s" % (user_data, error))

    def timeout_cb (self):
        print "Forced timeout after 60 sec."
        self.main_loop.quit ()
        return False

if __name__ == "__main__":
    ut.main ()
