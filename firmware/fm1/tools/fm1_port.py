#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Prints the --port name for the FM-1: TACK, or the name of whatever else it is running.

The installer matches one name, so looking for TACK alone would miss a unit left in update mode by a cut-off install,
which shows up as Felucca Update, and a unit on M-VAVE's firmware, which shows up as FM-1.
"""
TACK = "TACK"
LOADER = "Felucca Update"
STOCK = "FM-1"


def pick(names):
    def has(word):
        return any(word.lower() in name.lower() for name in names)
    if has(TACK):
        return TACK
    return LOADER if has(LOADER) else STOCK if has(STOCK) else TACK


if __name__ == "__main__":
    try:
        import mido
        names = mido.get_input_names()
    except Exception:              # the installer says what is wrong better than this could
        names = []
    print(pick(names))
