#!/usr/bin/env python
#
# Demo RSS provider simulator
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
import datetime, random

try:
    import barnum.gen_data as gen_data
    barnum_available = True
except ImportError:
    print "No barnum. Crappy random"
    barnum_available = False

TRACKER = 'org.freedesktop.Tracker3'
TRACKER_OBJ = '/org/freedesktop/Tracker3/Resources'

# We are not inserting content, nor contributor!
INSERT_SPARQL = """
INSERT {
<%s> a mfo:FeedMessage ;
 nie:contentLastModified "%s" ;
 nmo:communicationChannel <http://maemo.org/news/planet-maemo/atom.xml>;
 nie:plainTextContent "%s" ;
 nie:title "%s".
 }
"""

DELETE_SPARQL = """
  DELETE { <%s> a mfo:FeedMessage. }
"""

class SignalerUI (gtk.Window):

    def __init__ (self):
        gtk.Window.__init__ (self)

        bus = dbus.SessionBus ()
        tracker = bus.get_object (TRACKER, TRACKER_OBJ)
        self.iface = dbus.Interface (tracker,
                                     "org.freedesktop.Tracker3.Resources")

        vbox = gtk.VBox ()

        # Post frame
        post_frame = gtk.Frame ("Post")
        post_frame_vbox = gtk.VBox ()
        post_frame.add (post_frame_vbox)
        # Title
        title_label = gtk.Label ("Title")
        self.title_entry = gtk.Entry()
        title_hbox = gtk.HBox ()
        title_hbox.add (title_label)
        title_hbox.add (self.title_entry)
        post_frame_vbox.add (title_hbox)

        uri_label = gtk.Label ("Uri")
        self.uri_entry = gtk.Entry ()
        hbox_uri = gtk.HBox ()
        hbox_uri.add (uri_label)
        hbox_uri.add (self.uri_entry)
        self.uri_entry.set_property ("sensitive", False)
        post_frame_vbox.add (hbox_uri)

        date_label = gtk.Label ("Date")
        self.date_entry = gtk.Entry ()
        self.date_entry.set_property ("editable", False)
        date_hbox = gtk.HBox ()
        date_hbox.add (date_label)
        date_hbox.add (self.date_entry)
        post_frame_vbox.add (date_hbox)

        self.post_text = gtk.TextView ()
        post_frame_vbox.add (self.post_text)

        button_gen = gtk.Button (stock=gtk.STOCK_NEW)
        button_gen.connect ("clicked", self.gen_new_post_cb)
        post_frame_vbox.add (button_gen)

        vbox.add (post_frame)


        button_new = gtk.Button (stock=gtk.STOCK_ADD)
        button_new.connect ("clicked", self.clicked_add_cb)
        vbox.pack_start (button_new, expand=False)

        button_remove = gtk.Button (stock=gtk.STOCK_REMOVE)
        button_remove.connect ("clicked", self.clicked_remove_cb)
        vbox.pack_start (button_remove, expand=False)

        self.add (vbox)
        self.connect ("destroy", gtk.main_quit)
        self.show_all ()

        self.last_inserted = None

        gtk.main ()


    def clicked_add_cb (self, widget):
        date = self.date_entry.get_text ()
        uri = self.uri_entry.get_text ()
        title = self.title_entry.get_text ()
        buf = self.post_text.get_buffer()
        init, end = buf.get_bounds ()
        text = buf.get_text (init, end).replace ("\n", "\\n")
        if (not date or (len(date) == 0)):
            pass
        if (not uri or (len(uri) == 0)):
            pass
        if (not title or (len(title) == 0)):
            pass

        if (uri == self.last_inserted):
            print "Generate a new URI!"
            return

        sparql_insert = INSERT_SPARQL % (uri, date, text, title)
        print sparql_insert

        self.iface.SparqlUpdate (sparql_insert)
        self.last_inserted = uri

    def clicked_remove_cb (self, widget):
        uri = self.uri_entry.get_text ()
        if (not uri or (len(uri) == 0)):
            pass
        sparql_delete = DELETE_SPARQL % (uri)
        print sparql_delete

        self.iface.SparqlUpdate (sparql_delete)

    def gen_new_post_cb (self, widget):
        today = datetime.datetime.today ()
        self.date_entry.set_text (today.isoformat ().split('.')[0] + "+00:00")
        post_no = str(random.randint (100, 1000000))
        self.uri_entry.set_text ("http://test.maemo.org/feed/" + post_no)
        self.title_entry.set_text ("Title %s" % (post_no))
        if barnum_available :
            buf = gtk.TextBuffer ()
            buf.set_text (gen_data.create_paragraphs (2, 5, 5))
            self.post_text.set_wrap_mode (gtk.WRAP_WORD)
            self.post_text.set_buffer (buf)

if __name__ == "__main__":

    DBusGMainLoop(set_as_default=True)
    gobject.set_application_name ("Feeds engine/signals simulator")

    SignalerUI ()
