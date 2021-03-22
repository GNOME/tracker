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

import contextlib
import logging
import os
import pathlib
import multiprocessing
import threading
import shutil
import subprocess
import sys
import tempfile
import unittest as ut

import trackertestutils.helpers
import configuration as cfg

log = logging.getLogger(__name__)


def tracker_test_main():
    """Entry point that all functional-test modules must call."""

    if cfg.tests_verbose():
        # Output all logs to stderr
        logging.basicConfig(stream=sys.stderr, level=logging.DEBUG)
    else:
        # Output some messages from D-Bus daemon to stderr by default. In practice,
        # only errors and warnings should be output here unless the environment
        # contains G_MESSAGES_DEBUG=.
        handler_stderr = logging.StreamHandler(stream=sys.stderr)
        handler_stderr.addFilter(logging.Filter('sandbox-session-bus.stderr'))
        handler_stdout = logging.StreamHandler(stream=sys.stderr)
        handler_stdout.addFilter(logging.Filter('sandbox-session-bus.stdout'))
        logging.basicConfig(level=logging.INFO,
                            handlers=[handler_stderr, handler_stdout],
                            format='%(message)s')

    runner = None

    if cfg.tap_tests_enabled():
        try:
            from tap import TAPTestRunner
            runner = TAPTestRunner()
            runner.set_stream(True)
        except ImportError as e:
            log.error('No TAP test runner found: %s', e)
            raise

    ut.main(testRunner=runner, verbosity=2)


class TrackerSparqlDirectTest(ut.TestCase):
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
        except Exception:
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


class TrackerPortalTest(ut.TestCase):
    @classmethod
    def database_process_fn(self, service_name, in_queue, out_queue, dbus_address):
        # This runs in a separate process and provides a clean Tracker database
        # exported over D-Bus to the main test process.

        log.info("Started database thread")

        bus = Gio.DBusConnection.new_for_address_sync(
            dbus_address,
            Gio.DBusConnectionFlags.AUTHENTICATION_CLIENT |
            Gio.DBusConnectionFlags.MESSAGE_BUS_CONNECTION, None, None)

        conn = Tracker.SparqlConnection.new(
            Tracker.SparqlConnectionFlags.NONE,
            None,
            Gio.File.new_for_path(cfg.ontologies_dir()),
            None)

        endpoint = Tracker.EndpointDBus.new(conn, bus, None, None)

        bus.call_sync(
            'org.freedesktop.DBus',
            '/org/freedesktop/DBus',
            'org.freedesktop.DBus',
            'RequestName',
            GLib.Variant('(su)', (service_name, 0x4)),
            None, 0, -1, None)

        loop = GLib.MainLoop.new(None, False)

        def pop_update(message_queue):
            try:
                sparql = message_queue.get_nowait()
                if sparql is None:
                    loop.quit()
                conn.update(sparql, None)
                out_queue.put(None)
            except Exception:
                pass
            return GLib.SOURCE_CONTINUE

        GLib.timeout_add (50, pop_update, in_queue)
        out_queue.put(None)
        loop.run()

        bus.close(None)

    def setUp(self):
        extra_env = {}
        extra_env['TRACKER_TEST_PORTAL_FLATPAK_INFO'] = cfg.TEST_PORTAL_FLATPAK_INFO

        self.loop = trackertestutils.mainloop.MainLoop()
        self.message_queues = {}
        self.connections = {}
        self.sandbox = trackertestutils.helpers.TrackerDBusSandbox(
            session_bus_config_file=cfg.TEST_DBUS_DAEMON_CONFIG_FILE, extra_env=extra_env)

        self.sandbox.start()

        self.bus = self.sandbox.get_session_bus_connection()
        self.dbus_address = self.sandbox.get_session_bus_address()
        os.environ['DBUS_SESSION_BUS_ADDRESS'] = self.dbus_address

        try:
            log.info("Starting portal")
            self._portal_proxy = Gio.DBusProxy.new_sync(
                self.bus,
                Gio.DBusProxyFlags.NONE, None,
                'org.freedesktop.portal.Tracker',
                '/org/freedesktop/portal/Tracker',
                'org.freedesktop.portal.Tracker',
                None)

        except Exception:
            self.sandbox.stop()
            raise

    def tearDown(self):
        for service in self.message_queues:
            self.stop_service(service)
        self.sandbox.stop()

    def start_service(self, service_name):
        in_queue = multiprocessing.Queue()
        out_queue = multiprocessing.Queue()
        thread = threading.Thread(
            target=self.database_process_fn,
            args=(service_name, out_queue, in_queue, self.dbus_address))
        thread.start()
        in_queue.get()
        self.message_queues[service_name] = [ in_queue, out_queue ]

    def stop_service(self, service_name):
        queues = self.message_queues[service_name]
        if queues is not None:
            queues[1].put(None)

    def update(self, service_name, sparql):
        if sparql is not None:
            # Updates go through the message queue, bypassing the sandbox
            queues = self.message_queues[service_name]
            if queues is not None:
                queues[1].put(sparql)
                queues[0].get()

    def query(self, service_name, sparql):
        if service_name not in self.connections:
            conn = Tracker.SparqlConnection.bus_new(service_name, None, self.bus)
            store = trackertestutils.helpers.StoreHelper(conn)
            self.connections[service_name] = store
        else:
            store = self.connections[service_name]

        return store.query(sparql)

    def create_local_connection(self):
        return Tracker.SparqlConnection.new(
            Tracker.SparqlConnectionFlags.NONE,
            None,
            Gio.File.new_for_path(cfg.ontologies_dir()),
            None)


class CliError(Exception):
    pass


class TrackerCommandLineTestCase(ut.TestCase):
    def setUp(self):
        self.env = os.environ.copy()

        path = self.env.get('PATH', []).split(':')
        self.env['PATH'] = ':'.join([cfg.cli_dir()] + path)
        self.env['TRACKER_CLI_SUBCOMMANDS_DIR'] = os.path.join(cfg.cli_dir(), 'subcommands')
        self.bg_processes = []

    def tearDown(self):
        for bg_process in self.bg_processes:
            bg_process.terminate()
            bg_process.wait()

    @contextlib.contextmanager
    def tmpdir(self):
        try:
            dirpath = tempfile.mkdtemp()
            yield pathlib.Path(dirpath)
        finally:
            shutil.rmtree(dirpath, ignore_errors=True)

    def data_path(self, filename):
        test_data = pathlib.Path(__file__).parent.joinpath('test-data')
        return test_data.joinpath(filename)

    def run_cli(self, command):
        command = [str(c) for c in command]
        log.info("Running: %s", ' '.join(command))
        result = subprocess.run(command, stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, env=self.env)

        if len(result.stdout) > 0:
            log.debug("stdout: %s", result.stdout)
        if len(result.stderr) > 0:
            log.debug("stderr: %s", result.stderr)

        if result.returncode != 0:
            raise CliError('\n'.join([
                "CLI command failed.",
                "Command: %s" % ' '.join(command),
                "Error: %s" % result.stderr.decode('utf-8')]))

        return result.stdout.decode('utf-8')

    def run_background(self, command, init_string=None):
        command = [str(c) for c in command]
        log.info("Running in background: %s", ' '.join(command))
        result = subprocess.Popen(
            command, stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL, env=self.env,
            encoding='UTF-8')
        initialized = False

        if result.returncode != None:
            raise CliError('\n'.join([
                "CLI command failed.",
                "Command: %s" % ' '.join(command),
                "Error: %s" % result.stderr.decode('utf-8')]))

        # Wait for the specified output
        while init_string and not initialized:
            txt = result.stdout.readline();
            initialized = txt.find(init_string) >= 0

        result.stdout.close()
        self.bg_processes.append(result);
