#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""How steady the steps are under external MIDI clock, from the audio: a closed hat on all 16 steps, and pulses as a DAW
sends them. Each scenario must play every step it was sent and, once locked, keep the hits on the engine's grid.
    grid.py TACK_HOST OUTDIR"""
import array, os, random, subprocess, sys, wave

BLOCK_MS = 64 / 44.1
HEAD = ["wait 600", "tapkey b5", "wait 40", "turn K3 -60", "wait 40"]
for k in (0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26):
    HEAD += [f"tapkey {k}", "wait 30"]
HEAD += ["tap GLO", "wait 40", "tap GLO", "wait 40", "press OCT-", "wait 40", "turn K2 1", "wait 40", "release OCT-", "wait 40", "tap GLO", "wait 40", "wav grid.wav"]


def pulses(n, ms, jitter=0.0):
    out, t, at = [], 0.0, 0
    for _ in range(n):
        out.append("midi USB F8")
        t += ms
        w = max(1, round(t + random.uniform(-jitter, jitter) - at))
        out.append(f"wait {w}")
        at += w
    return out


def onsets(path):
    w = wave.open(path)
    n, r = w.getnframes(), w.getframerate()
    a = array.array("h", w.readframes(n))
    s = [abs(a[i]) + abs(a[i + 1]) for i in range(0, len(a) - 1, 2)]
    thr, on, i, hold = max(s) * 0.04, [], 0, int(r * 0.045)
    while i < len(s):
        if s[i] > thr:
            on.append(i / r * 1000)
            i += hold
        else:
            i += 1
    return on


def run(host, out, name, body):
    path = os.path.join(out, name + ".txt")
    open(path, "w").write("\n".join(HEAD + body + ["wait 100", "wavstop"]) + "\n")
    subprocess.run([host, path, out], capture_output=True, text=True, check=True, timeout=120)
    return onsets(os.path.join(out, "grid.wav"))


def spread(on, first, last):
    g = [on[k + 1] - on[k] for k in range(first, min(last, len(on) - 1))]
    return max(g) - min(g), sum(g) / len(g)


def main():
    host, out = sys.argv[1], sys.argv[2]
    random.seed(7)
    bad = 0

    def check(name, ok, text):
        nonlocal bad
        bad += 0 if ok else 1
        print(("ok  " if ok else "FAIL") + f" {name}: {text}")

    bars = 24 * 4
    on = run(host, out, "steady", ["midi USB FA"] + pulses(bars * 8, 20) + ["midi USB FC"])
    d, m = spread(on, 16, 127)
    check("steady", len(on) == 128 and d < 2 * BLOCK_MS + 0.1, f"{len(on)} of 128 hits, spread {d:.2f} ms around {m:.1f}")
    on = run(host, out, "jitter", ["midi USB FA"] + pulses(bars * 8, 20, 3) + ["midi USB FC"])
    d, m = spread(on, 16, 127)
    check("jitter", len(on) == 128 and d < 2 * BLOCK_MS + 0.1, f"{len(on)} of 128 hits, spread {d:.2f} ms around {m:.1f}")
    on = run(host, out, "jump", ["midi USB FA"] + pulses(bars * 4, 20) + pulses(bars * 4, 14) + ["midi USB FC"])
    check("tempo jump", len(on) == 128, f"{len(on)} of 128 hits")
    d, m = spread(on, 96, 127)
    check("after the jump", d < 2 * BLOCK_MS + 0.1, f"spread {d:.2f} ms around {m:.1f}")
    ramp = []
    for i in range(16):
        ramp += pulses(24, 24 - i * 0.5)
    on = run(host, out, "ramp", ["midi USB FA"] + ramp * 2 + ["midi USB FC"])
    check("tempo ramp", len(on) == 128, f"{len(on)} of 128 hits")
    on = run(host, out, "dropout", ["midi USB FA"] + pulses(bars * 2, 20) + ["wait 60"] + pulses(bars * 2, 20) + ["midi USB FC"])
    check("60 ms without pulses", len(on) == 64, f"{len(on)} of 64 hits")
    on = run(host, out, "gap", ["midi USB FA"] + pulses(bars * 2, 20) + ["wait 400"] + pulses(bars * 2, 20) + ["midi USB FC"])
    check("400 ms without pulses", len(on) == 64, f"{len(on)} of 64 hits")
    d, m = spread(on, 44, 63)
    check("back on the beat", d < 2 * BLOCK_MS + 0.1, f"spread {d:.2f} ms around {m:.1f}")
    # Stop between steps: on a step, the hit the flywheel has just played goes with the sound
    on = run(host, out, "stop", ["midi USB FA"] + pulses(bars - 3, 20) + ["midi USB FC", "wait 200", "midi USB FB"] + pulses(bars + 3, 20) + ["midi USB FC"])
    check("stop and continue", len(on) == 32, f"{len(on)} of 32 hits")
    d, m = spread(on, 20, 31)
    check("settled after continue", d < 2 * BLOCK_MS + 0.1, f"spread {d:.2f} ms around {m:.1f}")
    on = run(host, out, "restart", pulses(bars, 20) + ["midi USB FA"] + pulses(bars, 20) + ["midi USB FC"])
    d, m = spread(on, 0, 15)
    check("start on a running clock", len(on) == 16 and d < 2 * BLOCK_MS + 0.1, f"{len(on)} of 16 hits, spread {d:.2f} ms around {m:.1f}")
    on = run(host, out, "shuffle", ["turn ALGO 40", "wait 60", "midi USB FA"] + pulses(bars * 4, 20) + ["midi USB FC"])
    check("shuffle", len(on) == 64, f"{len(on)} of 64 hits")
    sys.exit(1 if bad else 0)


main()
