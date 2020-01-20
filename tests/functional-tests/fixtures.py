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

"""
Fixtures used by the Tracker functional-tests.
"""

import gi
gi.require_version('Tracker', '3.0')
from gi.repository import Gio, GLib
from gi.repository import Tracker

import logging
import os
import multiprocessing
import shutil
import tempfile
import time
import unittest as ut

import trackertestutils.helpers
import configuration as cfg

log = logging.getLogger(__name__)


class TrackerSparqlDirectTest (ut.TestCase):
    """
    Fixture for tests using a direct (local) connection to a Tracker database.
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


class TrackerSparqlBusTest (ut.TestCase):
    """
    Fixture for tests using a D-Bus connection to a Tracker database.

    The database is managed by a separate subprocess, spawned during the
    fixture setup.
    """

    @classmethod
    def database_process_fn(self, message_queue):
        # This runs in a separate process and provides a clean Tracker database
        # exported over D-Bus to the main test process.

        log.info("Started database subprocess")
        bus = Gio.bus_get_sync(Gio.BusType.SESSION, None)

        conn = Tracker.SparqlConnection.new(
            Tracker.SparqlConnectionFlags.NONE,
            Gio.File.new_for_path(self.tmpdir),
            Gio.File.new_for_path(cfg.ontologies_dir()),
            None)

        endpoint = Tracker.EndpointDBus.new(conn, bus, None, None)

        message_queue.put(bus.get_unique_name())

        loop = GLib.MainLoop.new(None, False)
        loop.run()

    @classmethod
    def setUpClass(self):
        self.tmpdir = tempfile.mkdtemp(prefix='tracker-test-')

        message_queue = multiprocessing.Queue()
        self.process = multiprocessing.Process(target=self.database_process_fn,
                                               args=(message_queue,))
        try:
            self.process.start()
            service_name = message_queue.get()
            log.debug("Got service name: %s", service_name)

            self.conn = Tracker.SparqlConnection.bus_new(service_name, None, None)

            self.tracker = trackertestutils.helpers.StoreHelper(self.conn)
        except Exception as e:
            self.process.terminate()
            shutil.rmtree(self.tmpdir, ignore_errors=True)
            raise

    @classmethod
    def tearDownClass(self):
        self.conn.close()
        self.process.terminate()
        shutil.rmtree(self.tmpdir, ignore_errors=True)
