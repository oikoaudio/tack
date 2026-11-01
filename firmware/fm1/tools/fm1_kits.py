#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Sends .SND kits from your own files to an FM-1 running Tack, over USB-MIDI. The sequencer must be stopped.

    fm1_kits.py KITDIR [--port NAME]        # every kit under KITDIR, sorted by path, up to 128

No kits come with Tack: they stay in the FM-1's flash, and the firmware has none in it. Needs mido with python-rtmidi.
"""
import argparse, hashlib, os, pathlib, sys, time

import mido

HEAD = [0x7D, ord("K"), ord("T")]
SLOTS, SLOT = 128, 256


def kits(root):
    seen, out = set(), []
    for f in sorted(pathlib.Path(root).rglob("*")):
        if f.suffix.lower() != ".snd" or f.name.startswith("._") or "__MACOSX" in f.parts or not f.is_file():
            continue
        raw = f.read_bytes()
        h = hashlib.sha256(raw).digest()
        if h in seen:
            continue
        seen.add(h)
        out.append((f, (raw + bytes(SLOT))[:SLOT]))
    return out


def ask(inp, out, cmd, body=()):
    out.send(mido.Message("sysex", data=HEAD + [cmd, *body]))
    end = time.time() + 5
    while time.time() < end:
        for m in inp.iter_pending():
            if m.type == "sysex" and list(m.data[:5]) == HEAD + [0x7F, ] + [cmd]:
                return m.data[5]
        time.sleep(0.002)
    raise SystemExit("no answer from the FM-1 (is it running Tack?)")


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("kitdir")
    ap.add_argument("--port", default="TACK")
    a = ap.parse_args()
    found = kits(a.kitdir)
    if not found:
        raise SystemExit(f"no .SND files under {a.kitdir}")
    if len(found) > SLOTS:
        print(f"{len(found)} kits, sending the first {SLOTS}")
        found = found[:SLOTS]
    name = next((n for n in mido.get_input_names() if a.port in n), None)
    if not name:
        raise SystemExit(f"no MIDI port matching {a.port!r}")
    inp, out = mido.open_input(name), mido.open_output(name)
    if st := ask(inp, out, 1):
        raise SystemExit("stop the sequencer first" if st == 1 else f"erase failed ({st})")
    for i, (f, raw) in enumerate(found):
        body = [i] + [ord(c) for c in raw.hex().upper()]
        if st := ask(inp, out, 2, body):
            raise SystemExit(f"{f.name}: refused ({st})")
        print(f"\r{i + 1}/{len(found)} {raw[:8].decode('ascii', 'replace').strip():8}", end="", flush=True)
    print(f"\n{len(found)} kits on the FM-1")


# Closing an input port while the FM-1 is sending can deadlock inside rtmidi, about one close in three. Leave the
# ports open and let process exit release them, on the error paths too.
code = 1
try:
    main()
    code = 0
except SystemExit as e:
    print(e, file=sys.stderr)
finally:
    sys.stdout.flush()
    sys.stderr.flush()
    os._exit(code)
