#
# Copyright (C) 2010, Nokia <jean-luc.lamadon@nokia.com>
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

from gi.repository import GLib

import json
import logging
import os
import pathlib
import sys


DEFAULT_TIMEOUT = 10


if 'TRACKER_FUNCTIONAL_TEST_CONFIG' not in os.environ:
    raise RuntimeError("The TRACKER_FUNCTIONAL_TEST_CONFIG environment "
                       "variable must be set to point to the location of "
                       "the generated configuration.json file.")

with open(os.environ['TRACKER_FUNCTIONAL_TEST_CONFIG']) as f:
    config = json.load(f)


def cli_dir():
    return config['TEST_CLI_DIR']


def ontologies_dir():
    return config['TEST_ONTOLOGIES_DIR']


def tracker_version():
    return config['TRACKER_VERSION']


TRACKER_DEBUG_TESTS = 1

def tests_verbose():
    tracker_debug_tests = GLib.DebugKey()
    tracker_debug_tests.key = "tests"
    tracker_debug_tests.value = TRACKER_DEBUG_TESTS

    flags = GLib.parse_debug_string (os.environ.get('TRACKER_DEBUG', ''), [tracker_debug_tests])
    return (flags & TRACKER_DEBUG_TESTS)
