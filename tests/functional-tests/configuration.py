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

import json
import logging
import os
import pathlib
import sys


if 'TRACKER_FUNCTIONAL_TEST_CONFIG' not in os.environ:
    raise RuntimeError("The TRACKER_FUNCTIONAL_TEST_CONFIG environment "
                       "variable must be set to point to the location of "
                       "the generated configuration.json file.")

with open(os.environ['TRACKER_FUNCTIONAL_TEST_CONFIG']) as f:
    config = json.load(f)


TEST_DBUS_DAEMON_CONFIG_FILE = config['TEST_DBUS_DAEMON_CONFIG_FILE']
TEST_ONTOLOGIES_DIR = str(pathlib.Path(__file__).parent.joinpath('test-ontologies'))
disableJournal = bool(config['disableJournal'])


def test_environment(tmpdir):
    return {
        'DCONF_PROFILE': config['TEST_DCONF_PROFILE'],
        'GSETTINGS_SCHEMA_DIR': config['TEST_GSETTINGS_SCHEMA_DIR'],
        'TRACKER_DB_ONTOLOGIES_DIR': config['TEST_ONTOLOGIES_DIR'],
        'TRACKER_LANGUAGE_STOP_WORDS_DIR': config['TEST_LANGUAGE_STOP_WORDS_DIR'],
        'TRACKER_TEST_DOMAIN_ONTOLOGY_RULE': config['TEST_DOMAIN_ONTOLOGY_RULE'],
        'XDG_CACHE_HOME': os.path.join(tmpdir, 'cache'),
        'XDG_CONFIG_HOME': os.path.join(tmpdir, 'config'),
        'XDG_DATA_HOME': os.path.join(tmpdir, 'data'),
        'XDG_RUNTIME_DIR': os.path.join(tmpdir, 'run'),
    }


def get_environment_boolean(variable):
    '''Parse a yes/no boolean passed through the environment.'''

    value = os.environ.get(variable, 'no').lower()
    if value in ['no', '0', 'false']:
        return False
    elif value in ['yes', '1', 'true']:
        return True
    else:
        raise RuntimeError('Unexpected value for %s: %s' %
                           (variable, value))

def get_environment_int(variable, default=0):
    try:
        return int(os.environ.get(variable))
    except (TypeError, ValueError):
        return default


if get_environment_boolean('TRACKER_TESTS_VERBOSE'):
    # Output all logs to stderr
    logging.basicConfig(stream=sys.stderr, level=logging.DEBUG)
else:
    # Output some messages from D-Bus daemon to stderr by default. In practice,
    # nothing should be output here unless the environment contains
    # G_MESSAGES_DEBUG= and/or TRACKER_VERBOSITY=1 or more.
    handler = logging.StreamHandler(stream=sys.stderr)
    handler.addFilter(logging.Filter('trackertestutils.dbusdaemon.stderr'))
    handler.addFilter(logging.Filter('trackertestutils.dbusdaemon.stdout'))
    logging.basicConfig(level=logging.INFO, handlers=[handler],
                        format='%(message)s')
