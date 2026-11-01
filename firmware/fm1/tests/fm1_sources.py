#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""What fm1.toml builds the engine from, for the tests that link it themselves, so that a new source file or a flag has one home.

    fm1_sources.py sources      # one path a line, from the repository root
    fm1_sources.py flags        # the engine's compiler flags, one a line
"""
import glob, os, sys, tomllib

here = os.path.dirname(os.path.abspath(__file__))
fm1 = os.path.dirname(here)
root = os.path.dirname(os.path.dirname(fm1))
with open(os.path.join(fm1, "fm1.toml"), "rb") as f:
    config = tomllib.load(f)
if sys.argv[1:] == ["sources"]:
    for pattern in config["o2"]:
        for path in sorted(glob.glob(os.path.join(fm1, pattern))):
            print(os.path.relpath(path, root))
elif sys.argv[1:] == ["flags"]:
    print("\n".join(config["o2_cflags"]))
else:
    raise SystemExit(__doc__)
