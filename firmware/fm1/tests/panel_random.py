#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Random playing on the panel, the same every run. Nothing may crash, and when every key is up nothing may stay held.

    panel_random.py SIMULATOR OUTDIR [RUNS]
"""
import os, random, subprocess, sys

BUTTONS = "FX SEL ENV LFO EDIT GLO HOME SAVE ARP SEQ REC OCT- OCT+".split()
DIALS = "K1 K2 K3 K4 SELECT ALGO PRESET".split()
KEYS = [f"w{i}" for i in range(16)] + [f"b{i}" for i in range(11)]


def script(rng, playing, n=240):
    held_b, held_k = set(), set()
    lines = ["wait 600", "data kits.bin 770048", "wait 40"]
    for _ in range(n):
        r = rng.random()
        if r < .30:
            b = rng.choice(BUTTONS + (["PLAY"] if playing else []))
            if b in held_b:
                lines.append(f"release {b}")
                held_b.discard(b)
            elif rng.random() < .6:
                lines.append(f"tap {b}")
            else:
                lines.append(f"press {b}")
                held_b.add(b)
        elif r < .62:
            k = rng.choice(KEYS)
            if k in held_k:
                lines.append(f"key {k} up")
                held_k.discard(k)
            elif rng.random() < .55:
                lines.append(f"tapkey {k}")
            else:
                lines.append(f"key {k} down")
                held_k.add(k)
        elif r < .88:
            lines.append(f"turn {rng.choice(DIALS)} {rng.choice([-3, -2, -1, 1, 2, 3, 8])} 20")
        else:
            lines.append(f"wait {rng.choice([20, 60, 300, 1100])}")
        if rng.random() < .25 and held_b:            # a button is not held for ever
            b = rng.choice(sorted(held_b))
            lines.append(f"release {b}")
            held_b.discard(b)
        if rng.random() < .2 and held_k:
            k = rng.choice(sorted(held_k))
            lines.append(f"key {k} up")
            held_k.discard(k)
    lines += [f"release {b}" for b in sorted(held_b)] + [f"key {k} up" for k in sorted(held_k)]
    lines += ["wait 1800", "expect held -1", "expect rollon 0", "expect popup 0", "expect selhelp 0", "expect keyword 0", "expect dropped 0"]
    return lines


def main():
    sim, out = os.path.abspath(sys.argv[1]), os.path.abspath(sys.argv[2])
    runs = int(sys.argv[3]) if len(sys.argv) > 3 else 10
    bad = 0
    for seed in range(runs):
        path = os.path.join(out, f"random{seed}.txt")
        with open(path, "w") as f:
            f.write("\n".join(script(random.Random(seed), seed % 2 == 1)) + "\n")
        try:
            p = subprocess.run([sim, path, out], cwd=out, capture_output=True, text=True, timeout=60)
        except subprocess.TimeoutExpired:
            print(f"FAIL random run {seed} hung")
            bad += 1
            continue
        fails = [l for l in p.stdout.splitlines() if l.startswith("FAIL")]
        if p.returncode or fails:
            print(f"FAIL random run {seed}, exit {p.returncode}: {fails[:3]}")
            bad += 1
    print("FAILED" if bad else f"panel_random passed, {runs} runs")
    return 1 if bad else 0


sys.exit(main())
