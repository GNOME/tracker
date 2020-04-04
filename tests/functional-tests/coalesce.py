# Copyright (C) 2010, Nokia <ivan.frade@nokia.com>
# Copyright (C) 2019, Sam Thursfield <sam@afuera.me.uk>
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
Test tracker:coalesce function in SPARQL.
"""

import unittest as ut
import fixtures


class TestCoalesce (fixtures.TrackerSparqlDirectTest):
    """
    Insert and instance with some values, and tracker coalesce of some of them
    with different combinations (first NULL, none NULL, all NULL...)
    """

    def setUp(self):
        self.resource_uri = "contact://test_group_concat"

        #
        # nco:nickname and nco:note are not set
        #
        insert = """
        INSERT { <%s> a nco:PersonContact;
                      nco:fullname \"full name\" ;
                      nco:nameFamily \"family name\" .
         }
        """ % (self.resource_uri)
        self.tracker.update(insert)

    def tearDown(self):
        delete = """
        DELETE { <%s> a rdfs:Resource. }
        """ % (self.resource_uri)
        self.tracker.update(delete)

    def test_coalesce_first_fine(self):
        """
        setUp: Insert a contact with only some text properties set
        1. TEST: run a query with coalesce with the valid value in first position
        tearDown: Remove the test contact inserted
        """

        query = """
        SELECT tracker:coalesce (?full, ?family, ?nickname, ?note, 'test_coalesce') WHERE {
           ?c a nco:PersonContact .
           OPTIONAL { ?c nco:fullname ?full }
           OPTIONAL { ?c nco:nameFamily ?family }
           OPTIONAL { ?c nco:nickname ?nickname }
           OPTIONAL { ?c nco:note ?note }
           FILTER (?c != nco:default-contact-me && ?c != nco:default-contact-emergency)
        }
        """
        results = self.tracker.query(query)
        assert len(results) == 1
        assert results[0][0] == "full name"

    def test_coalesce_second_fine(self):
        """
        setUp: Insert a contact with only some text properties set
        1. TEST: run a query with coalesce. First property NULL, second fine
        tearDown: Remove the test contact inserted
        """

        query = """
        SELECT tracker:coalesce (?nickname, ?family, ?full, ?note, 'test_coalesce') WHERE {
           ?c a nco:PersonContact .
           OPTIONAL { ?c nco:fullname ?full }
           OPTIONAL { ?c nco:nameFamily ?family }
           OPTIONAL { ?c nco:nickname ?nickname }
           OPTIONAL { ?c nco:note ?note }
           FILTER (?c != nco:default-contact-me && ?c != nco:default-contact-emergency)
        }
        """
        results = self.tracker.query(query)
        assert len(results) == 1
        assert results[0][0] == "family name"

    def test_coalesce_none_fine_default(self):
        """
        setUp: Insert a contact with only some text properties set
        1. TEST: run a query with coalesce. all variables NULL, return default value
        tearDown: Remove the test contact inserted
        """

        query = """
        SELECT tracker:coalesce (?nickname, ?note, 'test_coalesce') WHERE {
           ?c a nco:PersonContact .
           OPTIONAL { ?c nco:fullname ?full }
           OPTIONAL { ?c nco:nameFamily ?family }
           OPTIONAL { ?c nco:nickname ?nickname }
           OPTIONAL { ?c nco:note ?note }
           FILTER (?c != nco:default-contact-me && ?c != nco:default-contact-emergency)
        }
        """
        results = self.tracker.query(query)
        assert len(results) == 1
        assert results[0][0] == "test_coalesce"


if __name__ == '__main__':
    fixtures.tracker_test_main()
