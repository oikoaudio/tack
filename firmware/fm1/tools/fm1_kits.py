#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Sends .SND kits from your own files to an FM-1 running Tack, over USB-MIDI, or reads its bank back into files. The
sequencer must be stopped.

    fm1_kits.py KITDIR [--port NAME]        # every kit under KITDIR, sorted by path, up to 128
    fm1_kits.py --save DIR [--port NAME]    # the FM-1's bank into DIR as .snd files, which KITDIR takes back

No kits come with Tack: they stay in the FM-1's flash, and the firmware has none in it. Needs mido with python-rtmidi.
"""
import argparse, hashlib, os, pathlib, re, sys, time

import mido

HEAD = [0x7D, ord("K"), ord("T")]
SLOTS, SLOT, PART = 128, 256, 64
BANK_FILE = re.compile(r"\d{3}-.*\.snd$", re.IGNORECASE)     # what --save writes
EMPTY_KIT = b"initkit"                  # an LXR project's unused slot. Their last few bytes differ, so copies don't catch them


def named(raw, stem):
    """A kit needs a name: a blank one can't be seen in the bank, and 0xFF first reads as an empty slot, which ends it."""
    if raw[:8].strip(b" \0") and raw[0] != 0xFF:
        return raw
    name = "".join(c for c in stem.upper() if c.isascii() and c.isalnum())[:8] or "KIT"
    return name.ljust(8).encode("ascii") + raw[8:]


def kits(root):
    seen, out, empties = set(), [], 0
    for f in sorted(pathlib.Path(root).rglob("*")):
        if f.suffix.lower() != ".snd" or f.name.startswith("._") or "__MACOSX" in f.parts or not f.is_file():
            continue
        raw = f.read_bytes()
        if not BANK_FILE.match(f.name) and raw[:8].rstrip(b" \0").lower() == EMPTY_KIT:
            empties += 1
            continue
        h = hashlib.sha256(raw).digest()
        if h in seen and not BANK_FILE.match(f.name):   # a bank's copies stay: its slot numbers are what patterns refer to
            continue
        seen.add(h)
        out.append((f, named((raw + bytes(SLOT))[:SLOT], f.stem)))
    if empties:
        print(f"{empties} empty Initkit slots left out")
    return out


def answer(inp, out, cmd, body=(), same=0, wait=5.0, tries=1):
    """The FM-1 drops a SysEx that arrives while it still holds the one before, about one read in 3000, so a request
    that can be repeated goes again. The first `same` bytes after the reply's status must match the body, unless the
    status says the FM-1 refused the request: those replies carry nothing else."""
    for _ in range(tries):
        out.send(mido.Message("sysex", data=HEAD + [cmd, *body]))
        end = time.time() + wait
        while time.time() < end:
            for m in inp.iter_pending():
                if (m.type == "sysex" and list(m.data[:5]) == HEAD + [0x7F, cmd]
                        and (m.data[5] not in (0, 4) or list(m.data[6:6 + same]) == list(body[:same]))):
                    return m.data
            time.sleep(0.002)
    raise SystemExit("no answer from the FM-1 (is it running this build of Tack?)")


def ask(inp, out, cmd, body=(), wait=5.0, tries=1):
    return answer(inp, out, cmd, body, 0, wait, tries)[5]


def save(inp, out, folder):
    folder = pathlib.Path(folder)
    folder.mkdir(parents=True, exist_ok=True)
    for old in folder.iterdir():            # a kit renamed since would stay beside its new name, and move every kit after it up a slot
        if BANK_FILE.match(old.name):
            old.unlink()
    count = 0
    for slot in range(SLOTS):
        raw = bytearray()
        for part in range(SLOT // PART):
            d = answer(inp, out, 3, (slot, part), 2, 0.25, 8)
            if d[5] == 1:
                raise SystemExit("stop the sequencer first")
            if d[5] == 4:
                break
            raw += bytes.fromhex(bytes(d[8:]).decode("ascii"))
        if not raw:
            continue
        name = "".join(c for c in raw[:8].decode("ascii", "replace") if c.isalnum())
        (folder / f"{slot:03d}-{name}.snd").write_bytes(bytes(raw[:255]))
        count += 1
        print(f"\r{count} {name:8}", end="", flush=True)
    print(f"\n{count} kits in {folder}")


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("kitdir", nargs="?")
    ap.add_argument("--save")
    ap.add_argument("--port", default="TACK")
    a = ap.parse_args()
    if bool(a.kitdir) == bool(a.save):
        ap.error("a KITDIR to send, or --save DIR")
    found = kits(a.kitdir) if a.kitdir else [None]
    if not found:
        raise SystemExit(f"no .SND files under {a.kitdir}")
    if len(found) > SLOTS:
        print(f"{len(found)} kits, sending the first {SLOTS}")
        found = found[:SLOTS]
    name = next((n for n in mido.get_input_names() if a.port in n), None)
    if not name:
        raise SystemExit(f"no MIDI port matching {a.port!r}")
    inp, out = mido.open_input(name), mido.open_output(name)
    time.sleep(0.2)                         # as fm1_install.py: a message sent just after opening can be lost
    if a.save:
        save(inp, out, a.save)
        return
    if st := ask(inp, out, 1):
        raise SystemExit("stop the sequencer first" if st == 1 else f"erase failed ({st})")
    for i, (f, raw) in enumerate(found):
        body = [i] + [ord(c) for c in raw.hex().upper()]
        if st := ask(inp, out, 2, body, 1.0, 4):
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
