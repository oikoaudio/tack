#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Writes src/tack_shapes.h, the velocity shapes on the ARP button's second page: gen_shapes.py > src/tack_shapes.h

The levels and how they are read are aww's (crates/aww-contracts/src/velocity.rs): a table's steps as their distance
from its average, scaled so that the furthest reaches 1. Meter ranks a 16 step bar by metric strength. A table is
stored as that contour, -127 to 127, so the firmware only looks a step up. See tack_velocity_shape() for the rule.
"""
from math import gcd

# name on the screen, levels
TABLES = [
    ("PHRASE", [18, -9, 12, -16, 14, -7, 9, -13, 16, -11, 10, -8, 13, -15, 15, -6]),
    ("SAMBA", [64, 19, 59, 19, 88, 100, 19, 59, 19, 59, 19, 64, 19, 100, 88, 19]),
    ("PAILA", [33, 19, 100, 42, 33, 28, 56, 33, 82, 42, 28, 33, 68, 42, 19, 28]),
    ("MARCHA", [27, 36, 65, 23, 36, 36, 100, 31, 23, 27, 74, 46, 86, 36, 100, 18]),
    ("BOSSA", [29, 63, 34, 41, 87, 23, 29, 63, 23, 41, 52, 100, 41, 47, 100, 23]),
    ("MARTILLO", [90, 40, 80, 30, 90, 40, 100, 30]),
    ("TUMBAO", [30, 20, 80, 20, 30, 20, 100, 85]),
    ("CASCARA", [100, 20, 70, 100, 20, 70, 20, 70, 70, 20, 100, 20, 100, 70, 20, 70]),
    ("SON", [85, 15, 15, 100, 15, 15, 100, 15, 15, 15, 85, 15, 85, 15, 15, 15]),
    ("RUMBA", [85, 15, 15, 100, 15, 15, 15, 100, 15, 15, 85, 15, 85, 15, 15, 15]),
    # The bell of bembe over a 12/8 bar, from the Groove MIDI Dataset 1.0.0 (Gillick, Roberts, Engel, Eck and Bamman,
    # "Learning to Groove with Inverse Sequence Transformations", ICML 2019), CC BY 4.0: drummer1/session2/112
    ("BEMBE", [44.69, 4.19, 56.05, 0.55, 47.11, 48.64, 7.01, 48.82, 4.41, 49.31, 5.36, 32.75]),
]


def profile(levels):
    mean = sum(levels) / len(levels)
    far = max(abs(x - mean) for x in levels)
    return [(x - mean) / far if far else 0.0 for x in levels]


def meter(n):
    """a position is as strong as its gcd with the bar, and between equals the one that leads into a stronger is stronger"""
    weight = [n if i == 0 else gcd(i, n) for i in range(n)]
    strength = [(weight[i], weight[(i + 1) % n]) for i in range(n)]
    out = []
    for s in strength:
        above = sum(other > s for other in strength)
        equal = sum(other == s for other in strength)
        place = 1.0 - (above + (equal - 1) / 2.0) / (n - 1)
        out.append(2.0 * place - 1.0)
    return out


def row(name, contour):
    vals = [round(c * 127) for c in contour] + [0] * (16 - len(contour))
    return f'    {{"{name}", {len(contour)}, {{{", ".join(str(v) for v in vals)}}}}},'


print("/* SPDX-License-Identifier: GPL-3.0-only */")
print("/* Written by tools/gen_shapes.py. Each table is a contour of -127 to 127, one value a step, repeating every n steps */")
print("typedef struct { const char *name; uint8_t n; int8_t c[16]; } TackShape;")
print("static const TackShape SHAPES[] = {")
print(row("METER", meter(16)))
for name, levels in TABLES:
    print(row(name, profile(levels)))
print("};")
