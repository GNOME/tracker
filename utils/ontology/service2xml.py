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

import configparser, os
import sys

config = configparser.ConfigParser()
config.readfp(sys.stdin)

print("<service>")
for section in config.sections():
   splitted = section.split (":")
   print(" <section prefix=\"" + splitted[0] + "\" name=\"" + splitted[1] + "\" fullname=\"" + section + "\">")
   for name, value in config.items (section):
     vsplit = value.split (":")
     print("\t<item name=\""+ name + "\" fullvalue=\"" + value+ "\">")
     if len (vsplit) > 1:
       print("\t\t<prefix>" + vsplit[0] + "</prefix>")
       print("\t\t<value>" + vsplit[1] + "</value>")
     else:
       print("\t\t<prefix />")
       print("\t\t<value>" + value + "</value>")
     print("\t</item>")
   print(" </section>")
print("</service>")
