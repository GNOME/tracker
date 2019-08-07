from gi.repository import GLib
from gi.repository import Gio

import logging
import os

log = logging.getLogger(__name__)


class DConfClient(object):
    """
    Allow changing Tracker configuration in DConf.

    Tests should be run with a separate DConf profile so that these changes do
    not affect the user's configuration. The 'trackertest' profile exists for
    this reason, and the constructor will fail if this isn't the profile in
    use, to avoid any risk of modifying or removing your real configuration.

    The constructor will fail if DConf is not the default backend, because this
    probably indicates that the memory backend is in use. Without DConf the
    required configuration changes will not take effect, causing many tests to
    break.
    """

    def __init__(self, schema):
        self._settings = Gio.Settings.new(schema)

        backend = self._settings.get_property('backend')
        self._check_settings_backend_is_dconf(backend)
        self._check_using_correct_dconf_profile()

    def _check_settings_backend_is_dconf(self, backend):
        typename = type(backend).__name__.split('.')[-1]
        if typename != 'DConfSettingsBackend':
            raise Exception(
                "The functional tests require DConf to be the default "
                "GSettings backend. Got %s instead." % typename)

    def _check_using_correct_dconf_profile(self):
        profile = os.environ["DCONF_PROFILE"]
        if not os.path.exists(profile):
            raise Exception(
                "Unable to find DConf profile '%s'. Check that Tracker and "
                "the test suite have been correctly installed (you must pass "
                "--enable-functional-tests to configure)." % profile)

        assert os.path.basename(profile) == "trackertest"

    def write(self, key, value):
        """
        Write a settings value.
        """
        self._settings.set_value(key, value)

    def read(self, schema, key):
        """
        Read a settings value.
        """
        return self._settings.get_value(key)

    def reset(self):
        """
        Remove all stored values, resetting configuration to the default.

        This can be done by removing the entire 'trackertest' configuration
        database.
        """

        self._check_using_correct_dconf_profile()

        # XDG_CONFIG_HOME is useless, so we use HOME. This code should not be
        # needed unless for some reason the test is not being run via the
        # 'test-runner.sh' script.
        dconf_db = os.path.join(os.environ["HOME"],
                                ".config",
                                "dconf",
                                "trackertest")
        if os.path.exists(dconf_db):
            log.debug("[Conf] Removing dconf database: %s", dconf_db)
            os.remove(dconf_db)
