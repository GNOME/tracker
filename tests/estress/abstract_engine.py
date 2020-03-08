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
import gtk
import dbus
import dbus.service
from dbus.mainloop.glib import DBusGMainLoop
from abstract_text_engine import AbstractTextEngine
import time
import sys

TRACKER = 'org.freedesktop.Tracker3'
TRACKER_OBJ = '/org/freedesktop/Tracker3/Resources'

#
# Abstract class that does the job.
# Subclasses must implement some methods.
#
class AbstractEngine (gtk.Window, AbstractTextEngine):

    def __init__ (self, name, period, msgsize, timeout):
        gtk.Window.__init__ (self)
        AbstractTextEngine.__init__ (self, name, period, msgsize, timeout)
        
        self.publicname = name
        self.msgsize = msgsize
        self.period = period

        # UI, sweet UI
        vbox = gtk.VBox ()
    
        title_label = gtk.Label (name)
        freq_label = gtk.Label ("Period: ")
        freq_adj = gtk.Adjustment (period, 1, 100, 1, 10, 0)
        self.freq = gtk.SpinButton (freq_adj)
        size_label = gtk.Label ("# Items: ")
        size_adj = gtk.Adjustment (msgsize, 1, 100, 1, 10, 0)
        self.size = gtk.SpinButton (size_adj)

        conf = gtk.HBox ()
        conf.pack_start (title_label, padding=20)
        conf.pack_start (freq_label)
        conf.pack_start (self.freq)
        conf.pack_start (size_label)
        conf.pack_start (self.size)

        start = gtk.Button (stock=gtk.STOCK_MEDIA_PLAY)
        start.connect ("clicked", self.play_cb)
        stop = gtk.Button (stock=gtk.STOCK_MEDIA_STOP)
        stop.connect ("clicked", self.stop_cb)
        conf.pack_start (start)
        conf.pack_start (stop)
        
        vbox.pack_start (conf)
        
        self.desc_label = gtk.Label ("No running")
        vbox.pack_start (self.desc_label)

        self.add (vbox)
        self.connect ("destroy", gtk.main_quit)
        self.show_all ()

    def play_cb (self, widget):
        self.msgsize = int(self.size.get_value ())
        self.period = int(self.freq.get_value ())
        self.desc_label.set_label (self.get_running_label ())
        self.run ()

    def stop_cb (self, widget):
        self.desc_label.set_label ("No running")
        self.stop ()

    def run (self):
        self.desc_label.set_label (self.get_running_label ())
        self.callback_id = gobject.timeout_add (self.period * 1000,
                                                self.send_data_cb)

