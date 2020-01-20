#
# Copyright (C) 2010, Nokia <ivan.frade@nokia.com>
# Copyright (C) 2018-2020, Sam Thursfield <sam@afuera.me.uk>
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

import gi
gi.require_version('Tracker', '3.0')
from gi.repository import Gio
from gi.repository import Tracker

import os
import shutil
import tempfile
import time
import unittest as ut

import trackertestutils.helpers
import configuration as cfg


class CommonTrackerStoreTest (ut.TestCase):
    """
    Common superclass for tests that just require a fresh store running

    Note that the store is started per test-suite, not per test.
    """

    @classmethod
    def setUpClass(self):
        self.tmpdir = tempfile.mkdtemp(prefix='tracker-test-')

        try:
            self.conn = Tracker.SparqlConnection.new(
                Tracker.SparqlConnectionFlags.NONE,
                Gio.File.new_for_path(self.tmpdir),
                Gio.File.new_for_path(cfg.ontologies_dir()),
                None)

            self.tracker = trackertestutils.helpers.StoreHelper(self.conn)
        except Exception as e:
            shutil.rmtree(self.tmpdir, ignore_errors=True)
            raise

    @classmethod
    def tearDownClass(self):
        self.conn.close()
        shutil.rmtree(self.tmpdir, ignore_errors=True)
