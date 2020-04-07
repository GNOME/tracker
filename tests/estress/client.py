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
import time
import dbus
import getopt
import sys

TRACKER = 'org.freedesktop.Tracker3'
TRACKER_OBJ = '/org/freedesktop/Tracker3/Resources'

SPARQL_QUERY = """
    SELECT ?entry ?title ?date ?isRead WHERE {
      ?entry a nmo:FeedMessage ;
         nie:title ?title ;
         nie:contentLastModified ?date .
    OPTIONAL {
       ?entry nmo:isRead ?isRead.
    }
    } ORDER BY DESC(?date) LIMIT %s
"""

bus = dbus.SessionBus ()
obj = bus.get_object (TRACKER, TRACKER_OBJ)
iface = dbus.Interface (obj, "org.freedesktop.Tracker3.Resources")

def run_query ():
    start = time.time ()
    results = iface.SparqlQuery (SPARQL_QUERY % ("10"))
    end = time.time ()
    print int (time.time()), "%f" % (end - start)
    return True

def exit_cb ():
    sys.exit (0)

def usage ():
    print "Usage:"
    print "  client.py [OPTION...] - Run periodically a query on tracker"
    print ""
    print "Help Options:"
    print "  -h, --help             Show help options"
    print ""
    print "Application Options:"
    print "  -p, --period=NUM       Time (in sec) between queries"
    print "  -t, --timeout=NUM      Switch off the program after NUM seconds"
    print ""


if __name__ == "__main__":

    opts, args = getopt.getopt(sys.argv[1:],
                               "p:t:h",
                               ["period", "timeout", "help"])
    period = 1
    timeout = 0

    for o, a in opts:
        if o in ["-p", "--period"]:
            period = int (a)
        if o in ["-t", "--timeout"]:
            timeout = int (a)
        if o in ["-h", "--help"]:
            usage ()
            sys.exit (0)
            
    
    gobject.timeout_add (period * 1000, run_query)
    if (timeout > 0):
        gobject.timeout_add (timeout *1000, exit_cb)
    gtk.main ()

    
    
