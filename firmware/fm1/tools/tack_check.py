#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Stops `fm1 restore` from sending a backup whose patterns this firmware would refuse and clear. The firmware reads KPT7, the
format before morph targets, and no older one, and a restore replaces what the FM-1 holds.

    tack_check.py NAME.tack
"""
import os, sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "third_party", "fm1-base", "tools"))
import fm1_backup as B

PATTERN_MAGICS = (b"KPT8", b"KPT7")      # IMAGE_MAGIC and IMAGE_MAGIC_7 in src/tack_engine.c


def main():
    if len(sys.argv) != 2:
        raise SystemExit(__doc__)
    with open(sys.argv[1], "rb") as f:
        _, objs = B.parse(f.read())
    old = sum(1 for _, data in objs if data[:3] == b"KPT" and data[:4] not in PATTERN_MAGICS)
    if old:
        raise SystemExit(f"{sys.argv[1]}: {old} patterns in an older format, which this firmware would clear. Nothing was sent.")


main()
