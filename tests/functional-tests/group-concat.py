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
Test the GROUP_CONCAT function in SPARQL.
"""

import unittest as ut
import fixtures


class TestGroupConcat (fixtures.TrackerSparqlDirectTest):
    """
    Insert a multivalued property and request the results in GROUP_CONCAT
    """

    def test_group_concat(self):
        """
        1. Insert 3 capabilities for a test contact
        2. Retrieve contact/capabilites without group_contact (3 results)
        2. TEST: contact with group_concat capabilities (1 result)
        3. Remove the test contact inserted
        """

        uri = "contact://test_group_concat"

        insert = """
        INSERT { <%s> a nco:IMAddress;
                      nco:imID \"test_group_concat\";
                      nco:imCapability nco:im-capability-text-chat ;
                      nco:imCapability nco:im-capability-media-calls ;
                      nco:imCapability nco:im-capability-file-transfers .
         }
        """ % (uri)
        self.tracker.update(insert)

        query = """
        SELECT ?c ?capability WHERE {
           ?c a nco:IMAddress ;
              nco:imID \"test_group_concat\";
              nco:imCapability ?capability .
        }
        """
        results = self.tracker.query(query)

        assert len(results) == 3
        group_concat_query = """
        SELECT ?c GROUP_CONCAT (?capability, '|') AS ?cap WHERE {
           ?c a nco:IMAddress ;
              nco:imID \"test_group_concat\";
              nco:imCapability ?capability .
        } GROUP BY (?c)
        """
        results = self.tracker.query(group_concat_query)
        assert len(results) == 1

        instances = results[0][1].split('|')
        assert len(instances) == 3

        TEXT_CHAT = "http://tracker.api.gnome.org/ontology/v3/nco#im-capability-text-chat"
        MEDIA_CALLS = "http://tracker.api.gnome.org/ontology/v3/nco#im-capability-media-calls"
        FILE_TRANSFERS = "http://tracker.api.gnome.org/ontology/v3/nco#im-capability-file-transfers"
        assert TEXT_CHAT in instances
        assert MEDIA_CALLS in instances
        assert FILE_TRANSFERS in instances

        #self.assertEquals (str(results[0][0]), "test_insertion_1")

        delete = """
        DELETE { <%s> a rdfs:Resource. }
        """ % (uri)
        self.tracker.update(delete)


if __name__ == '__main__':
    fixtures.tracker_test_main()
