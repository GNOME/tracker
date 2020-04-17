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
Test the distance-calculation functions in SPARQL.
"""

import unittest as ut
import fixtures

POINT_COORDS = [
    (0, 0), (1, 1), (2, 2), (3, 3), (4, 4)
]


class TestDistanceFunctions (fixtures.TrackerSparqlDirectTest):
    """
    Insert some points and get the distance between them.
    """

    def setUp(self):
        self.counter = 0
        for lat, log in POINT_COORDS:
            insert = """
            INSERT {
            <%s> a slo:GeoLocation ;
                slo:longitude %d ;
                slo:latitude %d .
            }
            """ % ("point://test/point/" + str(self.counter), log, lat)
            self.tracker.update(insert)
            self.counter += 1

    def tearDown(self):
        for i in range(0, self.counter):
            delete = """
            DELETE {
            <%s> a rdfs:Resource.
            }
            """ % ("point://test/point/" + str (i))
            self.tracker.update(delete)

    def get_distance_between_points(self, sum_func, id1, id2):

        assert 0 <= id1 <= len(POINT_COORDS)
        assert 0 <= id2 <= len(POINT_COORDS)
        assert sum_func == "cartesian" or sum_func == "haversine"

        query_1_to_2 = """
        SELECT xsd:integer(tracker:%s-distance(?lat1,?lat2,?lon1,?lon2))
        WHERE {
          <point://test/point/%d> a slo:GeoLocation ;
             slo:latitude ?lat1 ;
             slo:longitude ?lon1 .

          <point://test/point/%d> a slo:GeoLocation ;
             slo:latitude ?lat2 ;
             slo:longitude ?lon2 .
        }
        """ % (sum_func, id1, id2)
        result = self.tracker.query(query_1_to_2)
        return int(result[0][0])

    def test_distance_cartesian_symmetry(self):
        """
        setUp: Insert 5 points in the pre-defined coordinates
        1. TEST: Check cartesian distance from point A to B, and from B to A
                 (should be the same :P)
        tearDown: Remove the test points inserted before
        """
        a_to_b = self.get_distance_between_points("cartesian", 1, 2)
        assert a_to_b == 204601

        b_to_a = self.get_distance_between_points("cartesian", 2, 1)
        assert b_to_a == 204601

        assert a_to_b == b_to_a

    def test_distance_haversine_symmetry(self):
        """
        setUp: Insert 5 points in the pre-defined coordinates
        1. TEST: Check cartesian distance from point A to B, and from B to A
                 (should be the same :P)
        tearDown: Remove the test points inserted before
        """
        a_to_b = self.get_distance_between_points("haversine", 1, 2)
        assert a_to_b == 157225

        b_to_a = self.get_distance_between_points("haversine", 2, 1)
        assert b_to_a == 157225

        assert a_to_b == b_to_a

    def test_distance_cartesian_proportion(self):
        d_1_to_2 = self.get_distance_between_points("cartesian", 1, 2)
        d_2_to_3 = self.get_distance_between_points("cartesian", 2, 3)
        d_3_to_4 = self.get_distance_between_points("cartesian", 3, 4)
        assert d_1_to_2 > d_2_to_3 > d_3_to_4

    def test_distance_haversine_proportion(self):
        d_1_to_2 = self.get_distance_between_points("haversine", 1, 2)
        d_2_to_3 = self.get_distance_between_points("haversine", 2, 3)
        d_3_to_4 = self.get_distance_between_points("haversine", 3, 4)
        assert d_1_to_2 > d_2_to_3 > d_3_to_4

    def test_distance_different(self):
        d_2_to_3h = self.get_distance_between_points("haversine", 2, 3)
        d_2_to_3c = self.get_distance_between_points("cartesian", 2, 3)
        assert d_2_to_3h < d_2_to_3c


if __name__ == '__main__':
    fixtures.tracker_test_main()
