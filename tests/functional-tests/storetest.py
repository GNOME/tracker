#
# Copyright (C) 2010, Nokia <ivan.frade@nokia.com>
# Copyright (C) 2018, 2019, Sam Thursfield <sam@afuera.me.uk>
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
            extra_env = cfg.test_environment(self.tmpdir)
            extra_env['LANG'] = 'en_GB.utf8'
            extra_env['LC_COLLATE'] = 'en_GB.utf8'

            self.sandbox = trackertestutils.helpers.TrackerDBusSandbox(
                dbus_daemon_config_file=cfg.TEST_DBUS_DAEMON_CONFIG_FILE, extra_env=extra_env)
            self.sandbox.start()

            self.tracker = trackertestutils.helpers.StoreHelper(
                self.sandbox.get_connection())
            self.tracker.start_and_wait_for_ready()
            self.tracker.start_watching_updates()
        except Exception as e:
            shutil.rmtree(self.tmpdir, ignore_errors=True)
            raise

    @classmethod
    def tearDownClass(self):
        self.tracker.stop_watching_updates()
        self.sandbox.stop()

        shutil.rmtree(self.tmpdir, ignore_errors=True)
