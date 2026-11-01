# SPDX-License-Identifier: GPL-3.0-only
"""Compares compare_seq.sh's traces, and checks the absolutes a comparison cannot: two builds can share a bug."""
import sys


def load(p):
    s, cur = {}, None
    for line in open(p):
        f = line.split()
        if f[0] == "S":
            cur = " ".join(f[1:])
            s[cur] = []
        else:
            s[cur].append((int(f[0]), int(f[1]), f[2], int(f[3]), int(f[4]), int(f[5])))
    return s


def main():
    d = sys.argv[1]
    O, F, G = (load(f"{d}/{n}.txt") for n in "OFG")
    fails = 0
    print(f"{'scenario':30} {'events':>7} {'O=F':>5} {'O=G seq':>8} {'F ms':>6} {'G ms':>6}")
    for name in O:
        o, f, g = O[name], F[name], G[name]
        key = lambda t: t[2:]
        same_f = list(map(key, o)) == list(map(key, f))
        same_g = list(map(key, o)) == list(map(key, g))
        # each scenario from its own first event: scenarios start at whatever time the last one's rounding left
        music = lambda x: [e for e in x if e[2] in "NP"]
        def rel(x, y):
            x, y = music(x), music(y)
            return max((abs((a[1] - x[0][1]) - (b[1] - y[0][1])) / 4 for a, b in zip(x, y)), default=0)
        tf, tg = rel(o, f), rel(o, g)
        print(f"{name:30} {len(o):7} {str(same_f):>5} {str(same_g):>8} {tf:6.2f} {tg:6.2f}")
        fails += (not same_f) + (not same_g) + (len(o) == 0)

    print("\nabsolute checks, on the FM-1 configuration at 44100 Hz")

    def check(name, ok, detail=""):
        nonlocal fails
        print(f"  {'ok  ' if ok else 'FAIL'} {name} {detail}")
        fails += not ok

    ev = [e for e in G["internal tempo"] if e[2] == "N"]
    dt = [(b[1] - a[1]) / 4 for a, b in zip(ev, ev[1:])]
    first = [x for e, x in zip(ev, dt) if e[1] < 2800 * 4]
    avg = sum(first) / len(first)
    check("120 BPM: a 16th is 125 ms", abs(avg - 125) < 0.5, f"mean {avg:.2f} ms, range {min(first):.2f}..{max(first):.2f}")
    late = [x for e, x in zip(ev, dt) if e[1] > 3400 * 4]
    avg = sum(late) / len(late)
    check("90 BPM: a 16th is 166.7 ms", abs(avg - 166.67) < 0.7, f"mean {avg:.2f} ms")

    lens = {60: 16, 61: 12, 62: 7, 63: 5}
    ev = [e for e in G["track lengths"] if e[2] == "N"]
    for note, n in lens.items():
        c = sum(1 for e in ev if e[4] == note)
        check(f"track of length {n} plays every step", c > 50, f"{c} notes")

    ev = G["automation"]
    bad = 0
    for i, e in enumerate(ev):
        if e[2] == "N" and (i < 2 or ev[i - 1][2] != "P" or ev[i - 2][2] != "P" or ev[i - 1][1] != e[1]):
            bad += 1
    check("automation reaches the engine before its note, same tick", bad == 0, f"{bad} out of order")

    ev = G["probability"]
    n = sum(1 for e in ev if e[2] == "N")
    total = 16000 / 125
    check("probability 50 plays some of the steps", 0.25 * total < n < 0.75 * total, f"{n} of {total:.0f}")

    print("  cursor left by playback:", [e[3] for e in G["song"] if e[2] == "M"])
    check("the song is stored as added", [e[4] for e in G["song"] if e[2] == "L"] == [0, 1, 2, 1, 64],
          str([e[4] for e in G["song"] if e[2] == "L"]))
    print("  active pattern every 500 ms:", [e[3] for e in G["song"] if e[2] == "A"])
    notes = [e[4] for e in G["song"] if e[2] == "N"]
    check("song plays its patterns in order, then stops", notes == [60, 62, 64, 62], str(notes))
    check("shifts move notes", [e[1] for e in G["shifts"]] != [e[1] for e in G["internal tempo"]][: len(G["shifts"])])
    fl = [e for e in G["flams"] if e[2] == "N"]
    check("flams x2 x3 x4 x6 over 2 bars", len(fl) == 30, f"{len(fl)} notes")
    rl = sum(1 for e in G["rolls"] if e[2] == "N" and e[3] == 3)
    check("a 1/8 roll for 1.5 s", 5 <= rl <= 7, f"{rl} notes")
    rec = [e for e in G["live recording"] if e[2] == "N"]
    check("recorded notes play back", {e[3] for e in rec} >= {2, 3}, str(sorted({e[3] for e in rec})))
    pc = [e for e in G["pattern change"] if e[2] == "N"]
    seen = [e[4] for e in pc]
    last62 = max(i for i, n in enumerate(seen) if n == 62)
    check("a pattern survives a change away and back", 60 in seen[last62:] and 62 in seen,
          f"{seen.count(60)} from pattern 0, {seen.count(62)} from pattern 1")
    z = [e for e in G["zero length track"] if e[2] == "N"]
    check("a length of 0 plays as 16", len(z) >= 2 and all(e[4] == 60 for e in z), f"{len(z)} notes")
    print("\nFAILED" if fails else "\nall passed")
    sys.exit(1 if fails else 0)


main()
