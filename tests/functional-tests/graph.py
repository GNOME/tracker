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

"""
Tests graphs in SPARQL.
"""

import unittest as ut
import fixtures


class TestGraphs (fixtures.TrackerSparqlDirectTest):
    """
    Insert triplets in different graphs and check the query results asking in
    one specific graph, in all of them and so on.
    """

    def test_graph_filter(self):
        """
        1. Insert a contact with different phone numbers from different sources
        2. Query phone numbers of a single graph
           EXPECTED: Only return the phone number from the specified source
        3. Remove the created resources
        """
        insert_sparql = """
        INSERT {
            <tel:+1234567890> a nco:PhoneNumber ;
                nco:phoneNumber '+1234567890' .
            <tel:+1234567891> a nco:PhoneNumber ;
                nco:phoneNumber '+1234567891' .
            <tel:+1234567892> a nco:PhoneNumber ;
                nco:phoneNumber '+1234567892' .
            GRAPH <graph://test/graph/0> {
                <contact://test/graph/1> a nco:PersonContact ; nco:hasPhoneNumber <tel:+1234567890>
            }
            GRAPH <graph://test/graph/1> {
                <contact://test/graph/1> a nco:PersonContact ; nco:hasPhoneNumber <tel:+1234567891>
            }
            GRAPH <graph://test/graph/2> {
                <contact://test/graph/1> a nco:PersonContact ; nco:hasPhoneNumber <tel:+1234567892>
            }
        }
        """
        self.tracker.update(insert_sparql)

        query = """
        SELECT ?contact ?number WHERE {
            GRAPH <graph://test/graph/1> {
                ?contact a nco:PersonContact; nco:hasPhoneNumber ?number
            }
        } ORDER BY DESC (fts:rank(?contact))
        """
        results = self.tracker.query(query)

        self.assertEqual(len(results), 1)
        self.assertEqual(results[0][0], "contact://test/graph/1")
        self.assertEqual(results[0][1], "tel:+1234567891")

        delete_sparql = """
        DELETE {
            <tel:+1234567890> a rdf:Resource .
            <tel:+1234567891> a rdf:Resource .
            <tel:+1234567892> a rdf:Resource .
            GRAPH <graph://test/graph/0> {
              <contact://test/graph/1> a rdf:Resource .
            }
            GRAPH <graph://test/graph/1> {
              <contact://test/graph/1> a rdf:Resource .
            }
            GRAPH <graph://test/graph/2> {
              <contact://test/graph/1> a rdf:Resource .
            }
        }
        """

    def test_graph_insert_multiple(self):
        """
        1. Insert a contact with the same phone number from different sources
        2. Query graph uri of hasPhoneNumber statement
           EXPECTED: The uri of the first graph that inserted the phone number
        3. Remove the created resources
        """
        insert_sparql = """
        INSERT {
            <tel:+1234567890> a nco:PhoneNumber ;
                nco:phoneNumber '+1234567890' .
            <contact://test/graph/1> a nco:PersonContact .
            GRAPH <graph://test/graph/0> {
                <contact://test/graph/1> nco:hasPhoneNumber <tel:+1234567890>
            }
            GRAPH <graph://test/graph/1> {
                <contact://test/graph/1> nco:hasPhoneNumber <tel:+1234567890>
            }
            GRAPH <graph://test/graph/2> {
                <contact://test/graph/1> nco:hasPhoneNumber <tel:+1234567890>
            }
        }
        """
        self.tracker.update(insert_sparql)

        query = """
        SELECT ?contact ?g WHERE {
            GRAPH ?g {
                ?contact a nco:PersonContact ; nco:hasPhoneNumber <tel:+1234567890>
            }
        } ORDER BY ?g
        """
        results = self.tracker.query(query)
        self.assertEqual(len(results), 3)
        self.assertEqual(results[0][0], "contact://test/graph/1")
        self.assertEqual(results[0][1], "graph://test/graph/0")
        self.assertEqual(results[1][0], "contact://test/graph/1")
        self.assertEqual(results[1][1], "graph://test/graph/1")
        self.assertEqual(results[2][0], "contact://test/graph/1")
        self.assertEqual(results[2][1], "graph://test/graph/2")

        delete_sparql = """
        DELETE {
            <tel:+1234567890> a rdf:Resource .
            GRAPH <graph://test/graph/0> {
                <contact://test/graph/1> a rdf:Resource .
            }
            GRAPH <graph://test/graph/1> {
                <contact://test/graph/1> a rdf:Resource .
            }
            GRAPH <graph://test/graph/2> {
                <contact://test/graph/1> a rdf:Resource .
            }
        }
        """


if __name__ == '__main__':
    fixtures.tracker_test_main()
