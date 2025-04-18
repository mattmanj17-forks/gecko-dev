# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import os
import sys


class NullTerminal:
    """Replacement for `blessed.Terminal()` that does no formatting."""

    number_of_colors = 0
    width = 0
    height = 0

    def __init__(self, stream=None, **kwargs):
        self.stream = stream or sys.__stdout__
        try:
            self.is_a_tty = os.isatty(self.stream.fileno())
        except Exception:
            self.is_a_tty = False

    class NullCallableString(str):
        """A dummy callable Unicode stolen from blessings"""

        def __new__(cls):
            new = str.__new__(cls, "")
            return new

        def __call__(self, *args):
            if len(args) != 1 or isinstance(args[0], int):
                return ""
            return args[0]

    def __getattr__(self, attr):
        return self.NullCallableString()


def Terminal(raises=False, disable_styling=False, **kwargs):
    if disable_styling:
        return NullTerminal(**kwargs)
    try:
        import blessed
    except Exception:
        if raises:
            raise
        return NullTerminal(**kwargs)
    return blessed.Terminal(**kwargs)
