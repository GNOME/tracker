# Copyright (C) 2018,2019, Sam Thursfield <sam@afuera.me.uk>
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


import logging
import os
import subprocess
import threading

log = logging.getLogger(__name__)
dbuslog = logging.getLogger(__name__ + '.dbus')


class DBusDaemon:
    """The private D-Bus instance that provides the sandbox's session bus.

    We support reading and writing the session information to a file. This
    means that if the user runs two sandbox instances on the same data
    directory at the same time, they will share the same message bus.
    """

    def __init__(self, session_file=None):
        self.session_file = session_file
        self.existing_session = False
        self.process = None

        try:
            self.address, self.pid = self.read_session_file(session_file)
            self.existing_session = True
        except FileNotFoundError:
            log.debug("No existing D-Bus session file was found.")

            self.address = None
            self.pid = None

    def get_session_file(self):
        """Returns the path to the session file if we created it, or None."""
        if self.existing_session:
            return None
        return self.session_file

    def get_address(self):
        return self.address

    @staticmethod
    def read_session_file(session_file):
        with open(session_file, 'r') as f:
            content = f.read()

        try:
            address = content.splitlines()[0]
            pid = int(content.splitlines()[1])
        except ValueError:
            raise RuntimeError(f"D-Bus session file {session_file} is not valid. "
                                "Remove this file to start a new session.")

        return address, pid

    @staticmethod
    def write_session_file(session_file, address, pid):
        os.makedirs(os.path.dirname(session_file), exist_ok=True)

        content = '%s\n%s' % (address, pid)
        with open(session_file, 'w') as f:
            f.write(content)

    def start_if_needed(self):
        if self.existing_session:
            log.debug('Using existing D-Bus session from file "%s" with address "%s"'
                      ' with PID %d' % (self.session_file, self.address, self.pid))
        else:
            dbus_command = ['dbus-daemon', '--session', '--print-address=1', '--print-pid=1']
            self.process = subprocess.Popen(dbus_command, stdout=subprocess.PIPE, stderr=subprocess.PIPE)

            try:
                self.address = self.process.stdout.readline().strip().decode('ascii')
                self.pid = int(self.process.stdout.readline().strip().decode('ascii'))
            except ValueError:
                error = self.process.stderr.read().strip().decode('unicode-escape')
                raise RuntimeError(f"Failed to start D-Bus daemon.\n{error}")

            log.debug("Using new D-Bus session with address '%s' with PID %d",
                      self.address, self.pid)

            self.write_session_file(self.session_file, self.address, self.pid)
            log.debug("Wrote D-Bus session file at %s", self.session_file)

            # We must read from the pipes continuously, otherwise the daemon
            # process will block.
            self._threads=[threading.Thread(target=self.pipe_to_log, args=(self.process.stdout, 'stdout'), daemon=True),
                           threading.Thread(target=self.pipe_to_log, args=(self.process.stderr, 'stderr'), daemon=True)]
            self._threads[0].start()
            self._threads[1].start()

    def stop(self):
        if self.process:
            log.debug("  Stopping DBus daemon")
            self.process.terminate()
            self.process.wait()

    def pipe_to_log(self, pipe, source):
        """This function processes the output from our dbus-daemon instance."""
        while True:
            line_raw = pipe.readline()

            if len(line_raw) == 0:
                break

            line = line_raw.decode('utf-8').rstrip()

            if line.startswith('(tracker-'):
                # We set G_MESSAGES_PREFIXED=all, meaning that all log messages
                # output by Tracker processes have a prefix. Note that
                # g_print() will NOT be captured here.
                dbuslog.info(line)
            else:
                # Log messages from other daemons, including the dbus-daemon
                # itself, go here. Any g_print() messages also end up here.
                dbuslog.debug(line)


