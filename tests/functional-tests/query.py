# Copyright (C) 2020, Sam Thursfield <sam@afuera.me.uk>
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
Test queries using libtracker-sparql.
"""

import gi
gi.require_version('Tracker', '3.0')
from gi.repository import Tracker

import unittest as ut

import fixtures
# We must import configuration to enable the default logging behaviour.
import configuration

import trackertestutils.helpers


class TrackerQueryTests():
    """
    Query test cases for TrackerSparqlConnection.

    To allow testing with both local and D-Bus connections, this test suite is
    a mixin, which is combined with different fixtures below.
    """

    def test_row_types(self):
        """Check the value types returned by TrackerSparqlCursor."""

        CONTACT = """
        INSERT {
        <test://test1> a nfo:Document, nfo:FileDataObject ;
             nie:url <file://test.test> ;
             nfo:fileSize 1234 .
        }
        """
        self.tracker.update(CONTACT)

        cursor = self.conn.query('SELECT ?url ?filesize { ?url a nfo:FileDataObject ; nfo:fileSize ?filesize }')

        cursor.next()
        assert cursor.get_n_columns() == 2
        self.assertEqual(cursor.get_value_type(0), Tracker.SparqlValueType.URI)
        self.assertEqual(cursor.get_value_type(1), Tracker.SparqlValueType.INTEGER)


class TrackerLocalQueryTest (fixtures.TrackerSparqlDirectTest, TrackerQueryTests):
    pass


class TrackerBusQueryTest (fixtures.TrackerSparqlBusTest, TrackerQueryTests):
    pass


if __name__ == "__main__":
    fixtures.tracker_test_main()
