# SPDX-License-Identifier: GPL-3.0-only
"""The cheat sheet's FM-1 panel as SVG, to scale with fm1-base's emulator (1000 x 640), labelled with what each control does in Tack"""


def panel_svg():
    out = []
    a = out.append
    a('<svg class="panel" viewBox="0 0 1000 640" role="img" aria-labelledby="panel-t panel-d">')
    a('<title id="panel-t">The FM-1 panel with Tack</title>')
    a('<desc id="panel-d">Encoders top left, the screen, K1 to K4 and the twelve buttons top right, and the keyboard below: seven voice keys, ROLL, MONO, POLY and COPY on the black keys, the sixteen steps on the white keys.</desc>')
    a('<rect x="4" y="4" width="992" height="632" rx="44" class="d-body"/>')
    def knob(x, y, r, label, fn):
        a(f'<circle cx="{x}" cy="{y}" r="{r+5}" class="d-tray"/><circle cx="{x}" cy="{y}" r="{r}" class="d-knob"/>')
        a(f'<line x1="{x}" y1="{y-r+5}" x2="{x}" y2="{y-r+14}" class="d-ind"/>')
        a(f'<text x="{x}" y="{y-r-12}" class="d-lab">{label}</text>')
        if fn:
            a(f'<text x="{x}" y="{y+r+24}" class="d-fn">{fn}</text>')
    knob(105, 100, 24, "MASTER", "volume")
    knob(215, 100, 24, "SELECT", "tempo · row")
    knob(105, 205, 24, "PRESETS", "kit")
    knob(215, 205, 24, "ALGORITHM", "morph")
    for i, fn in enumerate(["pitch", "filter", "decay", "volume"]):
        knob(610 + i * 104, 92, 22, f"K{i+1}", fn)
    def button(x, y, w, h, label, fn=None, two=None):
        a(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="9" class="d-btn"/>')
        if two:
            a(f'<text x="{x+w/2}" y="{y+h/2-1}" class="d-lab s">{label}</text><text x="{x+w/2}" y="{y+h/2+12}" class="d-lab s">{two}</text>')
        else:
            a(f'<text x="{x+w/2}" y="{y+h/2+5}" class="d-lab">{label}</text>')
    a('<rect x="58" y="258" width="210" height="58" rx="14" class="d-tray"/>')
    button(72, 268, 82, 38, "OCT-")
    button(170, 268, 82, 38, "OCT+")
    a('<text x="163" y="333" class="d-fn">bar · row · octave</text>')
    # the screen
    a('<rect x="300" y="44" width="236" height="236" rx="26" class="d-screen"/>')
    a('<text x="418" y="150" class="d-scr big">TACK</text><text x="418" y="176" class="d-scr">SEL held 1 s: help</text>')
    a('<rect x="568" y="160" width="400" height="140" rx="18" class="d-tray"/>')
    ROW1 = ["FX", "SEL", "ENV", "LFO", "EDIT", "GLO"]
    ROW2 = ["HOME", "SAVE", "ARP", "SEQ", "PLAY", "REC"]
    for i, l in enumerate(ROW1):
        button(582 + i * 63, 174, 54, 48, l, cls := None)
    for i, l in enumerate(ROW2):
        button(582 + i * 63, 236, 54, 48, l, None, "STOP" if l == "PLAY" else None)
    # keybed
    x0, y0, w, h = 36, 340, 928, 270
    a(f'<rect x="{x0}" y="{y0}" width="{w}" height="{h}" rx="18" class="d-tray"/>')
    pitch = (w - 24) / 16
    kw = pitch * 0.78
    BLACK_AFTER = [0, 1, 2, 4, 5, 7, 8, 9, 11, 12, 14]
    BLACK = [("D1", "v-drum"), ("D2", "v-drum"), ("D3", "v-drum"), ("SN", "v-snare"), ("CY", "v-cym"), ("CH", "v-hat"), ("OH", "v-hat"),
             ("ROLL", "k-fn"), ("MONO", "k-fn"), ("POLY", "k-fn"), ("COPY", "k-fn")]
    for k, at in enumerate(BLACK_AFTER):
        bx = x0 + 12 + (at + 1) * pitch - kw / 2
        by, bh = y0 + h * 0.06, h * 0.33
        lab, cls = BLACK[k]
        a(f'<rect x="{bx:.1f}" y="{by:.1f}" width="{kw:.1f}" height="{bh:.1f}" rx="{kw/2:.1f}" class="d-bkey {cls}"/>')
        if lab:
            a(f'<text x="{bx+kw/2:.1f}" y="{by+bh/2+5:.1f}" class="d-klab {cls}">{lab}</text>')
    for i in range(16):
        wx = x0 + 12 + i * pitch + (pitch - kw) / 2
        wy, wh = y0 + h * 0.43, h * 0.52
        a(f'<rect x="{wx:.1f}" y="{wy:.1f}" width="{kw:.1f}" height="{wh:.1f}" rx="{kw/2:.1f}" class="d-wkey{" beat" if i % 4 == 0 else ""}"/>')
        a(f'<text x="{wx+kw/2:.1f}" y="{wy+wh-18:.1f}" class="d-wlab">{i+1}</text>')
    a(f'<text x="{x0+w/2}" y="{y0+h-6}" class="d-fn">white keys: steps 1–16 of the bar · with SEQ held, patterns 1–16</text>')
    a('</svg>')
    return "\n".join(out)
