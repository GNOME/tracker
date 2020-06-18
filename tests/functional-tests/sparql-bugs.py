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
Peculiar SPARQL behavour reported in bugs
"""

from gi.repository import GLib

import unittest as ut
import fixtures


class TrackerStoreSparqlBugsTests (fixtures.TrackerSparqlDirectTest):

    def test_01_NB217566_union_exists_filter(self):
        """
        NB217566: Use of UNION in EXISTS in a FILTER breaks filtering 
        """
        content = """
                INSERT {
                    <contact:affiliation> a nco:Affiliation ;
                             nco:hasPhoneNumber
                                  [ a nco:PhoneNumber ; nco:phoneNumber "98653" ] .
                    <contact:test> a nco:PersonContact ;
                             nco:hasAffiliation <contact:affiliation> .
                }
                """
        self.tracker.update(content)

        """ Check that these 3 queries return the same results """
        query1 = """
                SELECT  ?_contact ?n WHERE {
                   ?_contact a nco:PersonContact .
                   {
                     ?_contact nco:hasAffiliation ?a .
                     ?a nco:hasPhoneNumber ?p1 .
                     ?p1 nco:phoneNumber ?n
                   } UNION {
                     ?_contact nco:hasPhoneNumber ?p2 .
                     ?p2 nco:phoneNumber ?n
                   } .
                  FILTER (
                    EXISTS {
                        {
                          ?_contact nco:hasPhoneNumber ?auto81 .
                          ?auto81 nco:phoneNumber ?auto80
                        } UNION {
                          ?_contact nco:hasAffiliation ?auto83 .
                          ?auto83 nco:hasPhoneNumber ?auto84 .
                          ?auto84 nco:phoneNumber ?auto80
                        }
                        FILTER (?auto80 = '98653')
                     }
                  )
                }
                """

        query2 = """
                SELECT ?_contact ?n WHERE {
                    ?_contact a nco:PersonContact .
                    {
                        ?_contact nco:hasAffiliation ?a .
                        ?a nco:hasPhoneNumber ?p1 .
                        ?p1 nco:phoneNumber ?n
                    } UNION {
                        ?_contact nco:hasPhoneNumber ?p2 .
                        ?p2 nco:phoneNumber ?n
                    } .
                    FILTER(?n = '98653')
                }
                """

        query3 = """
                SELECT ?_contact ?n WHERE {
                    ?_contact a nco:PersonContact .
                    {
                        ?_contact nco:hasAffiliation ?a .
                        ?a nco:hasPhoneNumber ?p1 .
                        ?p1 nco:phoneNumber ?n
                    } UNION {
                        ?_contact nco:hasPhoneNumber ?p2 .
                        ?p2 nco:phoneNumber ?n
                    } .
                    FILTER(
                        EXISTS {
                            ?_contact nco:hasAffiliation ?auto83 .
                            ?auto83 nco:hasPhoneNumber ?auto84 .
                            ?auto84 nco:phoneNumber ?auto80
                            FILTER(?auto80 = "98653")
                        }
                    )
                }
                """

        results1 = self.tracker.query(query1)
        self.assertEqual(len(results1), 1)
        self.assertEqual(len(results1[0]), 2)
        self.assertEqual(results1[0][0], "contact:test")
        self.assertEqual(results1[0][1], "98653")

        results2 = self.tracker.query(query2)
        self.assertEqual(len(results2), 1)
        self.assertEqual(len(results2[0]), 2)
        self.assertEqual(results2[0][0], "contact:test")
        self.assertEqual(results2[0][1], "98653")

        results3 = self.tracker.query(query3)
        self.assertEqual(len(results3), 1)
        self.assertEqual(len(results3[0]), 2)
        self.assertEqual(results3[0][0], "contact:test")
        self.assertEqual(results3[0][1], "98653")

        """ Clean the DB """
        delete = """
                DELETE { <contact:affiliation> a rdfs:Resource .
                <contact:test> a rdfs:Resource .
                }
                """

    def test_02_NB217636_delete_statements(self):
        """
        Bug 217636 - Not able to delete contact using
        DELETE {<contact:556> ?p ?v} WHERE {<contact:556> ?p ?v}.
        """
        data = """ INSERT {
                   <contact:test-nb217636> a nco:PersonContact ;
                          nco:fullname 'Testing bug 217636'
                }
                """
        self.tracker.update(data)

        results = self.tracker.query ("""
                 SELECT ?u WHERE {
                    ?u a nco:PersonContact ;
                      nco:fullname 'Testing bug 217636' .
                      }
                      """)
        self.assertEqual(len(results), 1)
        self.assertEqual(len(results[0]), 1)
        self.assertEqual(results[0][0], "contact:test-nb217636")

        problematic_delete = """
                DELETE { <contact:test-nb217636> ?p ?v }
                WHERE  { <contact:test-nb217636> ?p ?v }
                """
        self.tracker.update(problematic_delete)

        results_after = self.tracker.query ("""
                 SELECT ?u WHERE {
                    ?u a nco:PersonContact ;
                      nco:fullname 'Testing bug 217636' .
                      }
                      """)
        self.assertEqual(len(results_after), 0)

        # Safe deletion
        delete = """
                DELETE { <contact:test-nb217636> a rdfs:Resource. }
                """
        self.tracker.update(delete)

    def test_03_NB222645_non_existing_class_resource(self):
        """
        NB222645 - Inserting a resource using an non-existing class, doesn't rollback completely
        """
        query = "SELECT nrl:modified (?u) ?u  WHERE { ?u a nco:Contact }"
        original_data = self.tracker.query(query)

        wrong_insert = "INSERT { <test://nb222645-wrong-class-contact> a nco:IMContact. } "
        self.assertRaises(GLib.Error,
                          self.tracker.update,
                          wrong_insert)

        new_data = self.tracker.query(query)
        self.assertEqual(len(original_data), len(new_data))
        # We could be more picky, but checking there are the same number of results
        # is enough to verify the problem described in the bug.

    def test_04_NB224760_too_long_filter(self):
        """
        NB#224760 - 'too many sql variables' when filter ?sth in (long list)
        """
        query = "SELECT tracker:id (?m) ?m WHERE { ?m a rdfs:Resource. FILTER (tracker:id (?m) in (%s)) }"
        numbers = ",".join([str(i) for i in range(1000, 2000)])

        results = self.tracker.query(query % (numbers))

        # The query will raise an exception is the bug is there
        # If we are here, everything is fine.
        self.assertIsNotNone(results)

    def test_05_NB281201_insert_replace_and_superproperties(self):
        """
        Bug 281201 - INSERT OR REPLACE does not delete previous values for superproperties
        """
        content = """INSERT { <test:resource:nb281201> a nie:InformationElement; 
                                               nie:contentLastModified '2011-09-27T11:11:11Z'. }"""
        self.tracker.update(content)

        query = """SELECT ?contentLM ?nieIEDate ?dcDate { 
                              <test:resource:nb281201> dc:date ?dcDate ;
                                                 nie:informationElementDate ?nieIEDate ;
                                                 nie:contentLastModified ?contentLM .
                           }"""
        result = self.tracker.query(query)
        # Only one row of results, and the 3 colums have the same value
        self.assertEqual(len(result), 1)
        self.assertEqual(result[0][0], result[0][1])
        self.assertEqual(result[0][1], result[0][2])

        problematic = """INSERT OR REPLACE {
                                   <test:resource:nb281201> nie:contentLastModified '2012-10-28T12:12:12'
                                 }"""

        self.tracker.update(problematic)

        result = self.tracker.query(query)
        # Only one row of results, and the 3 colums have the same value
        self.assertEqual(len(result), 1)
        self.assertEqual(result[0][0], result[0][1])
        self.assertEqual(result[0][1], result[0][2])


if __name__ == "__main__":
    fixtures.tracker_test_main()
