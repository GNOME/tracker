#!/usr/bin/env python
#
# Copyright (C) 2009, Nokia <ivan.frade@nokia.com>
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

import gobject
import dbus
import dbus.service
from dbus.mainloop.glib import DBusGMainLoop
import time
import sys

TRACKER = 'org.freedesktop.Tracker3'
TRACKER_OBJ = '/org/freedesktop/Tracker3/Resources'

#
# Abstract class that does the job.
# Subclasses must implement some methods.
#
class AbstractTextEngine:

    def __init__ (self, name, period, msgsize, timeout):

        self.publicname = name
        self.msgsize = msgsize
        self.period = period

        # DBus connection
        bus = dbus.SessionBus ()
        self.tracker = bus.get_object (TRACKER, TRACKER_OBJ)
        self.iface = dbus.Interface (self.tracker,
                                     "org.freedesktop.Tracker3.Resources")
        if (timeout > 0):
            self.call = 0
            gobject.timeout_add (timeout * 1000,
                                 self.exit_cb)

    def exit_cb (self):
        sys.exit (0)

    def run (self):
        self.callback_id = gobject.timeout_add (self.period * 1000,
                                                self.send_data_cb)

    def stop (self):
        gobject.source_remove (self.callback_id)
        self.callback_id = 0

    def send_data_cb (self):
        sparql = self.get_insert_sparql ()
        self.iface.SparqlUpdate (sparql)
        print int(time.time()), self.msgsize, self.publicname
        return True


    def get_insert_sparql (self):
        print "Implement this method in a subclass!!!"
        assert False
        
    def get_running_label (self):
        print "Implement this method in a subclass!!!"
        assert False
