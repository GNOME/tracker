#
# Demo RSS client using tracker as backend
# Copyright (C) 2009 Nokia <ivan.frade@nokia.com>
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

import dbus
import os

TRACKER = 'org.freedesktop.Tracker3'
TRACKER_OBJ = '/org/freedesktop/Tracker3/Resources'

FALSE = "false"
TRUE = "true"

QUERY_FIRST_ENTRIES = """
    SELECT ?entry ?title ?date ?isRead WHERE {
      ?entry a mfo:FeedMessage ;
         nie:title ?title ;
         nie:contentLastModified ?date .
    OPTIONAL {
       ?entry nmo:isRead ?isRead.
    }
    } ORDER BY DESC(?date) LIMIT %s
    """

SET_URI_AS_READED = """
    DELETE {<%s> nmo:isRead "%s".}
    INSERT {<%s> nmo:isRead "%s".}
    """

QUERY_ALL_SUBSCRIBED_FEEDS ="""
    SELECT ?feeduri ?title COUNT (?entries) AS e WHERE {
       ?feeduri a mfo:FeedChannel ;
                nie:title ?title.
       ?entries a mfo:FeedMessage ;
                nmo:communicationChannel ?feeduri.
    } GROUP BY ?feeduri
"""

QUERY_FOR_URI = """
    SELECT ?title ?date ?isRead ?channel WHERE {
      <%s> a mfo:FeedMessage ;
             nie:title ?title ;
             nie:contentLastModified ?date ;
             nmo:communicationChannel ?channel .
      OPTIONAL {
      <%s> nmo:isRead ?isRead.
      }
    }
"""

QUERY_FOR_TEXT = """
    SELECT ?text WHERE {
    <%s> nie:plainTextContent ?text .
    }
"""

CONF_FILE = os.path.expanduser ("~/.config/rss_tracker/rss.conf")

class TrackerRSS:

    def __init__ (self):
        bus = dbus.SessionBus ()
        self.tracker = bus.get_object (TRACKER, TRACKER_OBJ)
        self.iface = dbus.Interface (self.tracker,
                                     "org.freedesktop.Tracker3.Resources")
        self.invisible_feeds = []
        self.load_config ()
        

    def load_config (self):
        if (os.path.exists (CONF_FILE)):
            print "Loading %s" % (CONF_FILE)
            for line in open (CONF_FILE):
                line = line.replace ('\n','')
                if (len (line) > 0):
                    self.invisible_feeds.append (line)
            print "Hiding feeds from:", self.invisible_feeds
        else:
            if (not os.path.exists (os.path.dirname (CONF_FILE))):
                os.makedirs (os.path.dirname (CONF_FILE))
            f = open (CONF_FILE, 'w')
            f.close ()
        
    def get_post_sorted_by_date (self, amount):
        results = self.iface.SparqlQuery (QUERY_FIRST_ENTRIES % (amount))
        return results

    def set_is_read (self, uri, value):
        if (value):
            dbus_value = TRUE
            anti_value = FALSE
        else:
            dbus_value = FALSE
            anti_value = TRUE

        print "Sending ", SET_URI_AS_READED % (uri, anti_value, uri, dbus_value)
        self.iface.SparqlUpdate (SET_URI_AS_READED % (uri, anti_value, uri, dbus_value))

    def get_all_subscribed_feeds (self):
        """ Returns [(uri, feed channel name, entries, visible)]
        """
        componed = []
        results = self.iface.SparqlQuery (QUERY_ALL_SUBSCRIBED_FEEDS)
        for result in results:
            print "Looking for", result[0]
            if (result[0] in self.invisible_feeds):
                visible = False
            else:
                visible = True
            componed.insert (0, result + [visible])

        componed.reverse ()
        return componed

    def get_info_for_entry (self, uri):
        """  Returns (?title ?date ?isRead)
        """
        details = self.iface.SparqlQuery (QUERY_FOR_URI % (uri, uri))
        if (len (details) < 1):
            print "No details !??!!"
            return None
        if (len (details) > 1):
            print "OMG what are you asking for?!?!?!"
            return None

        info = details [0]
        if (info[3] in self.invisible_feeds):
            print "That feed is not visible"
            return None
        else:
            if (info[2] == TRUE):
                return (info[0], info[1], True)
            else:
                return (info[0], info[1], False)

    def get_text_for_uri (self, uri):
        text = self.iface.SparqlQuery (QUERY_FOR_TEXT % (uri))
        if (text[0]):
            text = text[0][0].replace ("\\n", "\n")
        else:
            text = ""
        return text

    def mark_as_invisible (self, uri):
        self.invisible_feeds.append (uri)

    def mark_as_visible (self, uri):
        self.invisible_feeds.remove (uri)

    def flush_to_file (self):
        f = open (CONF_FILE, 'w')
        for line in self.invisible_feeds:
            f.write (line + "\n")
        f.close ()
