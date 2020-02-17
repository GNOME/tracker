# Copyright (C) 2018, Sam Thursfield <sam@afuera.me.uk>
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


from gi.repository import GLib

import sys


class MainLoop():
    '''Wrapper for GLib.MainLoop that propagates any unhandled exceptions.

    PyGObject doesn't seem to provide any help with propagating exceptions from
    the GLib main loop to the main Python execution context. The default
    behaviour is to print a message and continue, which is useless for tests as
    it means tests appear to pass when in fact they are broken.

    '''

    def __init__(self):
        self._loop = GLib.MainLoop.new(None, 0)

    def quit(self):
        self._loop.quit()

    def run(self):
        raise NotImplemented("Use .run_checked() to ensure correct exception handling.")

    def run_checked(self):
        '''Run the loop until quit(), then raise any unhandled exception.'''
        self._exception = None

        old_hook = sys.excepthook

        def new_hook(etype, evalue, etb):
            self._loop.quit()
            self._exception = evalue
            old_hook(etype, evalue, etb)

        try:
            sys.excepthook = new_hook
            self._loop.run()
        finally:
            sys.excepthook = old_hook

        if self._exception:
            raise self._exception
