#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Prints the --port name for the FM-1: TACK, or the update loader's when that is what is connected.

The installer matches one name, and a unit left in update mode by a cut-off install shows up as Felucca Update, so looking
for TACK alone would not find the unit the install has to finish on.
"""
TACK = "TACK"
LOADER = "Felucca Update"


def pick(names):
    def has(word):
        return any(word.lower() in name.lower() for name in names)
    return LOADER if has(LOADER) and not has(TACK) else TACK


if __name__ == "__main__":
    try:
        import mido
        names = mido.get_input_names()
    except Exception:              # the installer says what is wrong better than this could
        names = []
    print(pick(names))
