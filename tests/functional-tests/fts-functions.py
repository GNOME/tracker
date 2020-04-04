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
Test the full-text search.
"""

import unittest as ut
import fixtures


class TestFTSFunctions (fixtures.TrackerSparqlDirectTest):
    """
    Insert data with text and check the fts:xxxx functions are returning the expected results
    """
    @ut.skip("Test currently fails.")
    def test_fts_rank(self):
        """
        1. Insert a Contact1 with 'abcdefxyz' as fullname and nickname
        2. Insert a Contact2 with 'abcdefxyz' as fullname
        2. Insert a Contact3 with 'abcdefxyz' as fullname and twice in nickname
        3. Query sorting by fts:rank
           EXPECTED: The 3 contacts in order: 3, 1, 2
        4. Remove the created resources
        """
        insert_sparql = """
        INSERT {
        <contact://test/fts-function/rank/1> a nco:PersonContact ;
                       nco:fullname 'abcdefxyz' ;
                       nco:nickname 'abcdefxyz' .

        <contact://test/fts-function/rank/2> a nco:PersonContact ;
                       nco:fullname 'abcdefxyz' .

        <contact://test/fts-function/rank/3> a nco:PersonContact ;
                       nco:fullname 'abcdefxyz' ;
                       nco:nickname 'abcdefxyz abcdefxyz' .
        }
        """
        self.tracker.update(insert_sparql)

        query = """
        SELECT ?contact WHERE {
           ?contact a nco:PersonContact ;
                fts:match 'abcdefxyz' .
        } ORDER BY DESC (fts:rank(?contact))
        """
        results = self.tracker.query(query)

        self.assertEqual(len(results), 3)
        self.assertEqual(results[0][0], "contact://test/fts-function/rank/1")
        self.assertEqual(results[1][0], "contact://test/fts-function/rank/2")
        self.assertEqual(results[2][0], "contact://test/fts-function/rank/3")

        delete_sparql = """
        DELETE {
        <contact://test/fts-function/rank/1> a rdfs:Resource .
        <contact://test/fts-function/rank/2> a rdfs:Resource .
        <contact://test/fts-function/rank/3> a rdfs:Resource .
        }
        """
        self.tracker.update(delete_sparql)

    def test_fts_offsets(self):
        """
        1. Insert a Contact1 with 'abcdefxyz' as fullname and nickname
        2. Insert a Contact2 with 'abcdefxyz' as fullname
        2. Insert a Contact3 with 'abcdefxyz' as fullname and twice in nickname
        3. Query fts:offsets for 'abcdefxyz'
           EXPECTED: The 3 contacts in insertion order, with 2, 1 and 3 pairs (prop, offset=1) each
        4. Remove the created resources
        """
        insert_sparql = """
        INSERT {
        <contact://test/fts-function/offset/1> a nco:PersonContact ;
                       nco:fullname 'abcdefxyz' ;
                       nco:nickname 'abcdefxyz' .

        <contact://test/fts-function/offset/2> a nco:PersonContact ;
                       nco:fullname 'abcdefxyz' .

        <contact://test/fts-function/offset/3> a nco:PersonContact ;
                       nco:fullname 'abcdefxyz' ;
                       nco:nickname 'abcdefxyz abcdefxyz' .
        }
        """
        self.tracker.update(insert_sparql)

        query = """
        SELECT fts:offsets (?contact) WHERE {
           ?contact a nco:PersonContact ;
                fts:match 'abcdefxyz' .
        }
        """
        results = self.tracker.query(query)

        self.assertEqual(len(results), 3)
        self.assertEqual(results[0][0], 'nco:fullname,0,nco:nickname,0')
        self.assertEqual(results[1][0], 'nco:fullname,0')
        self.assertEqual(
            results[2][0], 'nco:fullname,0,nco:nickname,0,nco:nickname,10')

        delete_sparql = """
        DELETE {
        <contact://test/fts-function/offset/1> a rdfs:Resource .
        <contact://test/fts-function/offset/2> a rdfs:Resource .
        <contact://test/fts-function/offset/3> a rdfs:Resource .
        }
        """
        self.tracker.update(delete_sparql)


if __name__ == '__main__':
    fixtures.tracker_test_main()
