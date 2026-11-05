#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Writes the FM-1 mode maps and cheat sheet: as pages of their own, and with a checkout of the oikoaudio site, the data its
manual's pages are made from.

    make_docs.py OUTDIR [WEB_DIR]       # first: build.py firmware/fm1 --host

    OUTDIR/tack-fm1-mode-maps.html, tack-fm1-cheat-sheet.html   pages of their own
    OUTDIR/shots/                                               the screens, from tests/doc_shots.txt in the simulator
    WEB_DIR/app/manuals/tack-fm1/maps/maps-data.ts              the maps, for /manuals/tack-fm1/maps/
    WEB_DIR/app/manuals/tack-fm1/cheat-sheet/sheet-data.ts      the cheat sheet, for /manuals/tack-fm1/cheat-sheet/

The screens play a short beat on site/demo_kits, so they show a kit with a name and a pattern. The site's copies leave out the
NEW marks: the site describes the firmware it offers. Publishes nothing.
"""
import html
import json
import struct
import subprocess
import sys
import zlib
from pathlib import Path

HERE = Path(__file__).resolve().parent
APP = HERE.parents[1]
sys.path.insert(0, str(HERE))
import doc_maps    # noqa: E402
import doc_panel   # noqa: E402

SIM = APP / "build" / "host" / "tack_host"
SHOTS = APP / "tests" / "doc_shots.txt"
KITS = APP / "site" / "demo_kits"


def png_small(path):
    """the simulator's PNG, stored, deflated again: a seventh of the size in the page"""
    data = path.read_bytes()
    assert data[:8] == b"\x89PNG\r\n\x1a\n", path
    pos, chunks, idat = 8, [], b""
    while pos < len(data):
        n, kind = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + n]
        pos += 12 + n
        if kind == b"IDAT":
            idat += body
        elif kind != b"IEND":
            chunks.append((kind, body))
    chunks.append((b"IDAT", zlib.compress(zlib.decompress(idat), 9)))
    chunks.append((b"IEND", b""))
    out = data[:8] + b"".join(struct.pack(">I", len(b)) + k + b + struct.pack(">I", zlib.crc32(k + b)) for k, b in chunks)
    path.write_bytes(out)


def take_shots(out):
    """the bank holds the demo kits, as 256-byte slots: an .SND file's name and parameters"""
    out.mkdir(parents=True, exist_ok=True)
    bank = b"".join((p.read_bytes()[:255]).ljust(256, b"\0") for p in sorted(KITS.glob("*.snd")))
    (out / "kits.bin").write_bytes(bank)
    if not SIM.exists():
        raise SystemExit(f"make_docs: no simulator at {SIM}: build.py firmware/fm1 --host first")
    run = subprocess.run([str(SIM), str(SHOTS), str(out)], cwd=out, capture_output=True, text=True, timeout=120)
    if run.returncode or "FAIL" in run.stdout:
        raise SystemExit("make_docs: doc_shots.txt failed\n" + run.stdout[-2000:])
    for key, *_ in doc_maps.MODES:
        png_small(out / f"{key}.png")


def maps(shots):
    """each mode: its key, title, how to get there, intro, keycaps and map"""
    out = []
    for key, title, how, lit, draw, entry, caps in doc_maps.MODES:
        out.append({"key": key, "title": title, "how": how, "intro": doc_maps.INTRO[key], "caps": doc_maps.caps_html(caps),
                    "svg": doc_maps.svg(title, lit, draw, entry, shots / f"{key}.png", key)})
    return out


def maps_page(modes):
    figs = "\n".join(f'''<section class="mode-sec" id="{m["key"]}">
  <div class="mode-head"><div class="beacon" aria-hidden="true">{m["caps"]}</div><div class="titles"><h2>{html.escape(m["title"])}</h2><p class="how">Get there: {html.escape(m["how"])}</p></div></div>
  <p class="intro">{html.escape(m["intro"])}</p>
  <div class="figure">{m["svg"]}</div>
</section>''' for m in modes)
    return (HERE / "maps.html").read_text().replace("@@MODES@@", figs)


def sheet_parts():
    """the cheat sheet's page, the legend under its panel, and its reference panels, each as one piece of HTML"""
    page = (HERE / "cheat_sheet.html").read_text()
    start = page.index('<div class="legend">')
    legend = page[start:page.index("</div>", start) + 6]
    start = page.index('<div class="grid">')
    return page, legend, page[start:page.index("<footer>")].rstrip()


def ts_const(name, value):
    return f"export const {name} = {json.dumps(value, ensure_ascii=False, indent=1)};\n"


def main():
    if len(sys.argv) not in (2, 3):
        raise SystemExit(__doc__)
    out = Path(sys.argv[1]).resolve()
    shots = out / "shots"
    take_shots(shots)
    modes = maps(shots)
    (out / "tack-fm1-mode-maps.html").write_text(maps_page(modes))
    page, legend, sections = sheet_parts()
    panel = doc_panel.panel_svg()
    (out / "tack-fm1-cheat-sheet.html").write_text(page.replace("@@PANEL@@", panel))
    print(f"make_docs: {out}/tack-fm1-mode-maps.html, tack-fm1-cheat-sheet.html")
    if len(sys.argv) == 3:
        web = Path(sys.argv[2]).resolve()
        head = "// Written by tack/firmware/fm1/tools/docs/make_docs.py: change it there, not here\n"
        d = web / "app" / "manuals" / "tack-fm1" / "maps"
        d.mkdir(parents=True, exist_ok=True)
        (d / "maps-data.ts").write_text(head + ts_const("MODES", modes))
        d = web / "app" / "manuals" / "tack-fm1" / "cheat-sheet"
        d.mkdir(parents=True, exist_ok=True)
        plain = sections.replace('<span class="new">NEW</span>', "")
        (d / "sheet-data.ts").write_text(head + ts_const("PANEL", panel) + ts_const("LEGEND", legend) + ts_const("SECTIONS", plain))
        print(f"make_docs: {web}/app/manuals/tack-fm1/maps/maps-data.ts, cheat-sheet/sheet-data.ts")


main()
