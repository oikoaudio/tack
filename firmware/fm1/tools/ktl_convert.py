#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""One-off: a backup from before the list of locks (.tack version 1) as a backup for builds with it (version 2). The firmware
has no conversion of its own. Delete this once the old backups are converted.

    fm1 convert OLD.tack NEW.tack
"""
import os, struct, sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "third_party", "fm1-base", "tools"))
import fm1_backup as B

TRACKS, STEPS, BARS = 7, 64, 4
OLD_SIZE, NO_LOCK = 3650, 0xFF
MAGIC = 0x3254504B


def pattern(old):
    steps = old[:7 * TRACKS * STEPS]
    timing = old[len(steps):len(steps) + TRACKS * STEPS]
    rest = old[len(steps) + len(timing):]               # the main steps, the lengths, shuffle, kit
    tail = rest[:BARS * TRACKS * 2 + TRACKS + 2]
    new, locks = bytearray(), {}
    for t in range(TRACKS):
        for s in range(STEPS):
            vol, prob, note, n1, v1, n2, v2 = steps[(t * STEPS + s) * 7:(t * STEPS + s) * 7 + 7]
            new += bytes([vol, prob, note])
            for dest, value in ((n1, v1), (n2, v2)):
                if dest != NO_LOCK:
                    locks[(t << 6 | s, dest)] = value & 0x7F
    list_ = bytearray()
    for (key, dest), value in sorted(locks.items()):
        list_ += bytes([key & 0xFF, dest, value | (key >> 8) << 7])
    return struct.pack("<IH", MAGIC, len(locks)) + bytes(new) + timing + tail + bytes(list_), len(locks)


def main():
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    with open(sys.argv[1], "rb") as f:
        image = f.read()
    flags, objs = B.parse(image)
    magic, ver = B.magic_of(image), struct.unpack_from("<H", image, 4)[0]
    if ver != 1:
        raise SystemExit(f"{sys.argv[1]}: version {ver}, not 1: nothing to convert")
    out, count, locks = [], 0, 0
    for obj, data in objs:
        if len(data) == OLD_SIZE:
            data, n = pattern(data)
            count, locks = count + 1, locks + n
        elif data and obj != 0:
            raise SystemExit(f"{sys.argv[1]}: part {obj} is {len(data)} bytes, not a pattern")
        out.append((obj, data))
    with open(sys.argv[2], "wb") as f:
        f.write(B.build(out, flags, magic, 2))
    print(f"{sys.argv[2]}: {count} patterns, {locks} locks")


main()
