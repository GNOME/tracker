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
Test `tracker` commandline tool
"""

import unittest

import configuration
import fixtures


class TestCli(fixtures.TrackerCommandLineTestCase):
    def test_version(self):
        """Check we're testing the correct version of the CLI"""
        output = self.run_cli(
            ['tracker3', '--version'])

        version_line = output.splitlines()[0]
        expected_version_line = 'Tracker %s' % configuration.tracker_version()
        self.assertEqual(version_line, expected_version_line)

    def test_create_local_database(self):
        """Create a database using `tracker endpoint` for local testing"""

        with self.tmpdir() as tmpdir:
            ontology_path = configuration.ontologies_dir()

            # Create the database
            self.run_cli(
                ['tracker3', 'endpoint', '--database', tmpdir,
                 '--ontology-path', ontology_path])

            # Sanity check that it works.
            self.run_cli(
                ['tracker3', 'sparql', '--database', tmpdir,
                 '--query', 'ASK { ?u a rdfs:Resource }'])


if __name__ == '__main__':
    unittest.main(verbosity=2)
