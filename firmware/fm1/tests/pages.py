#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The panel's map: which page each button leads to from each page, against tests/page_table.txt. A second press of a page's
button goes on to its next screen or does nothing, and never back.

    pages.py SIMULATOR OUTDIR           # check
    pages.py SIMULATOR OUTDIR --write   # write the table, to read through as a spec before committing it
"""
import os, re, subprocess, sys

START = {
    "HOME": [], "STEP": ["tap SEQ"], "NOTE": ["tap SEQ", "tap SEQ"], "EDIT": ["tap EDIT"], "EDIT2": ["tap EDIT", "tap EDIT"],
    "ENV": ["tap ENV"], "LFO": ["tap LFO"], "FX": ["tap FX"], "GLO": ["tap GLO"], "LANE": ["tap ARP"],
    "EUCLID": ["tap ARP", "tap ARP"], "VEL": ["tap ARP", "tap ARP", "tap ARP"],
    "SONG": ["press SEQ", "tap ARP", "release SEQ"], "SAVE": ["tap SAVE"],
}
BUTTONS = ["HOME", "SEQ", "EDIT", "ENV", "LFO", "FX", "GLO", "ARP", "SAVE"]
OVER = {1: "SONG", 2: "LANE", 3: "EUCLID", 4: "VEL", 5: "SAVE", 6: "FX", 7: "GLO"}


def name(code):
    over, group, sub, view = code // 1000, code // 100 % 10, code // 10 % 10, code % 10
    if over:
        return OVER[over]
    if group:
        return ["", "EDIT", "ENV", "LFO"][group] + (str(sub) if sub else "")
    return ["HOME", "STEP", "NOTE"][view]


def screen(sim, out, steps):
    path = os.path.join(out, "pages_script.txt")
    with open(path, "w") as f:
        f.write("wait 600\n" + "".join(f"{s}\nwait 40\n" for s in steps) + "expect screen -1\n")
    text = subprocess.run([sim, path, out], cwd=out, capture_output=True, text=True, timeout=30).stdout
    return name(int(re.search(r"expect screen == -1, got (-?\d+)", text).group(1)))


def main():
    sim, out = os.path.abspath(sys.argv[1]), os.path.abspath(sys.argv[2])
    table = os.path.join(os.path.dirname(os.path.abspath(__file__)), "page_table.txt")
    now = [f"{s:7} {b:5} {screen(sim, out, START[s] + [f'tap {b}'])}" for s in START for b in BUTTONS]
    if sys.argv[3:] == ["--write"]:
        with open(table, "w") as f:
            f.write("# from the page, the button, and the page it leads to: tests/pages.py\n" + "\n".join(now) + "\n")
        return 0
    with open(table) as f:
        want = [l.rstrip("\n") for l in f if not l.startswith("#")]
    bad = [f"FAIL {w} but now {n.split()[2]}" for w, n in zip(want, now) if w != n]
    print("\n".join(bad) if bad else f"pages passed, {len(now)} moves")
    return 1 if bad or len(want) != len(now) else 0


sys.exit(main())
