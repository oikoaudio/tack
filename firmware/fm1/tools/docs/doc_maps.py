# SPDX-License-Identifier: GPL-3.0-only
"""One map per mode: the FM-1 panel (to scale with fm1-base's emulator) with callouts on leader lines around it"""
import base64, html
OX, OY, W, H = 380, 250, 1800, 1160
TOP = 110
ENTRY = set()                                   # the keys that enter the mode being drawn: their callouts are in its colour                                       # the drawing starts here: the callouts above the panel reach no higher
PITCH = (928 - 24) / 16
KW = PITCH * 0.78
BLACK_AFTER = [0, 1, 2, 4, 5, 7, 8, 9, 11, 12, 14]
ROW1 = ["FX", "SEL", "ENV", "LFO", "EDIT", "GLO"]
ROW2 = ["HOME", "SAVE", "ARP", "SEQ", "PLAY", "REC"]

def K(i): return (OX + 610 + 104 * i, OY + 92)
ENC = {"MASTER": (OX + 105, OY + 100), "SELECT": (OX + 215, OY + 100), "PRESETS": (OX + 105, OY + 205), "ALGORITHM": (OX + 215, OY + 205)}
def btn(name):
    if name in ROW1: return (OX + 609 + 63 * ROW1.index(name), OY + 198)
    return (OX + 609 + 63 * ROW2.index(name), OY + 260)
def bkey(i): return (OX + 48 + (BLACK_AFTER[i] + 1) * PITCH, OY + 340 + 270 * 0.06 + 270 * 0.33 / 2)
BK_TOP, BK_BOT = OY + 340 + 270 * 0.06, OY + 340 + 270 * 0.39
def wkey(i): return (OX + 48 + i * PITCH + PITCH / 2, OY + 340 + 270 * 0.43 + 270 * 0.26)
WK_TOP, WK_BOT = OY + 340 + 270 * 0.43, OY + 340 + 270 * 0.95
OCT = {"OCT-": (OX + 113, OY + 287), "OCT+": (OX + 211, OY + 287)}

def panel(lit, entry=(), shot=None, clip="screen"):
    o = []
    a = o.append
    a(f'<g transform="translate({OX},{OY})">')
    a('<rect x="4" y="4" width="992" height="632" rx="44" class="d-body"/>')
    def knob(x, y, r, label, on):
        if label in entry:
            a(f'<circle cx="{x}" cy="{y}" r="{r+11}" class="d-ring"/>')
        a(f'<circle cx="{x}" cy="{y}" r="{r+5}" class="d-tray"/><circle cx="{x}" cy="{y}" r="{r}" class="d-knob{" beacon" if label in entry else " on" if on else ""}"/>')
        a(f'<line x1="{x}" y1="{y-r+5}" x2="{x}" y2="{y-r+14}" class="d-ind"/>')
        a(f'<text x="{x}" y="{y-r-12}" class="d-lab">{label}</text>')
    for n, (x, y) in ENC.items():
        knob(x - OX, y - OY, 24, n, n in lit)
    for i in range(4):
        knob(610 + i * 104, 92, 22, f"K{i+1}", "K" in lit)
    def button(x, y, w, h, label, on, two=None):
        b = label in entry
        cls = " beacon" if b else " on" if on else ""
        if b:
            a(f'<rect x="{x-6}" y="{y-6}" width="{w+12}" height="{h+12}" rx="14" class="d-ring"/>')
        a(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="9" class="d-btn{cls}"/>')
        tc = " beacon" if b else ""
        if two:
            a(f'<text x="{x+w/2}" y="{y+h/2-1}" class="d-lab s{tc}">{label}</text><text x="{x+w/2}" y="{y+h/2+12}" class="d-lab s{tc}">{two}</text>')
        else:
            a(f'<text x="{x+w/2}" y="{y+h/2+5}" class="d-lab{tc}">{label}</text>')
    a('<rect x="58" y="258" width="210" height="58" rx="14" class="d-tray"/>')
    button(72, 268, 82, 38, "OCT-", "OCT-" in lit)
    button(170, 268, 82, 38, "OCT+", "OCT+" in lit)
    a('<rect x="300" y="44" width="236" height="236" rx="26" class="d-screen"/>')
    if shot:                                     # the simulator's screen in that mode, 240 x 240
        uri = "data:image/png;base64," + base64.b64encode(open(shot, "rb").read()).decode()
        a(f'<clipPath id="scr-{clip}"><rect x="312" y="56" width="212" height="212" rx="6"/></clipPath>')
        a(f'<image x="312" y="56" width="212" height="212" href="{uri}" clip-path="url(#scr-{clip})" preserveAspectRatio="none"/>')
    a('<rect x="568" y="160" width="400" height="140" rx="18" class="d-tray"/>')
    for i, l in enumerate(ROW1):
        button(582 + i * 63, 174, 54, 48, l, l in lit)
    for i, l in enumerate(ROW2):
        button(582 + i * 63, 236, 54, 48, l, l in lit, "STOP" if l == "PLAY" else None)
    a('<rect x="36" y="340" width="928" height="270" rx="18" class="d-tray"/>')
    KEYS = [("D1", "v-drum"), ("D2", "v-drum"), ("D3", "v-drum"), ("SN", "v-snare"), ("CY", "v-cym"), ("CH", "v-hat"), ("OH", "v-hat"),
            ("ROLL", "k-fn"), ("MONO", "k-fn"), ("POLY", "k-fn"), ("COPY", "k-fn")]
    for k, at in enumerate(BLACK_AFTER):
        bx = 48 + (at + 1) * PITCH - KW / 2
        lab, cls = KEYS[k]
        if lab and lab in entry:
            a(f'<rect x="{bx-6:.1f}" y="{340+270*0.06-6:.1f}" width="{KW+12:.1f}" height="{270*0.33+12:.1f}" rx="{KW/2+6:.1f}" class="d-ring"/>')
            cls += " beacon"
        a(f'<rect x="{bx:.1f}" y="{340+270*0.06:.1f}" width="{KW:.1f}" height="{270*0.33:.1f}" rx="{KW/2:.1f}" class="d-bkey {cls}"/>')
        if lab:
            a(f'<text x="{bx+KW/2:.1f}" y="{340+270*0.06+270*0.33/2+5:.1f}" class="d-klab {cls}">{lab}</text>')
    for i in range(16):
        wx = 48 + i * PITCH + (PITCH - KW) / 2
        a(f'<rect x="{wx:.1f}" y="{340+270*0.43:.1f}" width="{KW:.1f}" height="{270*0.52:.1f}" rx="{KW/2:.1f}" class="d-wkey{" on" if "W" in lit else ""}"/>')
        a(f'<text x="{wx+KW/2:.1f}" y="{340+270*0.95-18:.1f}" class="d-wlab">{i+1}</text>')
    a('</g>')
    return o

LH = 25          # a line's height
def callout(o, x, y, lines, align, kind="m"):
    """lines[0] the title. y is the title's baseline. Returns the box: x0, y0, x1, y1"""
    anchor = {"l": "start", "r": "end", "c": "middle"}[align]
    for n, t in enumerate(lines):
        cls = "c-title " + kind if n == 0 else "c-line"
        o.append(f'<text x="{x}" y="{y + n * LH}" text-anchor="{anchor}" class="{cls}">{html.escape(t)}</text>')
    return y - 18, y + (len(lines) - 1) * LH + 6

def leader(o, pts, kind="m", dot=True):
    d = "M" + " L".join(f"{x:.1f},{y:.1f}" for x, y in pts)
    o.append(f'<path d="{d}" class="ld {kind}"/>')
    if dot:
        x, y = pts[-1]
        o.append(f'<circle cx="{x:.1f}" cy="{y:.1f}" r="4.5" class="ldot {kind}"/>')

def bracket(o, x0, x1, y, up, kind="m"):
    t = -10 if up else 10
    o.append(f'<path d="M{x0:.1f},{y+t:.1f} L{x0:.1f},{y:.1f} L{x1:.1f},{y:.1f} L{x1:.1f},{y+t:.1f}" class="ld {kind}"/>')

# ---- placements shared by the modes
LX = OX - 40           # left labels end here
RX = OX + 1040         # right labels start here
def left(o, y, lines, to, via=None, kind="m"):
    """a label on the left, its leader along y to the control, or by way of points"""
    callout(o, LX, y, lines, "r", kind)
    pts = [(LX + 12, y - 7)] + (via or []) + [to]
    leader(o, pts, kind)
def right(o, y, lines, to, via=None, kind="m"):
    callout(o, RX, y, lines, "l", kind)
    pts = [(RX - 12, y - 7)] + (via or []) + [to]
    leader(o, pts, kind)
def below_key(o, x, y, lines, key_bottom, kind="m"):
    callout(o, x, y, lines, "c", kind)
    leader(o, [(x, y - 26), (x, key_bottom)], kind)

def k_bracket(o, lines, kind="m"):
    """K1-K4 under one bracket above them, the label above it"""
    x0, x1 = K(0)[0] - 30, K(3)[0] + 30
    yb = OY - 4
    bracket(o, x0, x1, yb, False, kind)
    xm = (x0 + x1) / 2
    n = len(lines)
    callout(o, xm, yb - 22 - (n - 1) * LH, lines, "c", kind)

def keys_bracket(o, lines, kind="m", x=None):
    """the white keys under a bracket below the panel, the label under it at x"""
    x0, x1 = wkey(0)[0] - 22, wkey(15)[0] + 22
    yb = OY + 652
    bracket(o, x0, x1, yb, True, kind)
    xl = x if x is not None else (x0 + x1) / 2
    leader(o, [(xl, yb), (xl, yb + 22)], kind, dot=False)
    callout(o, xl, yb + 48, lines, "c", kind)

def voices_bracket(o, y, lines, kind="m"):
    """the seven voice keys under a bracket just above them, the label on the left"""
    x0, x1 = bkey(0)[0] - 22, bkey(6)[0] + 22
    yb = BK_TOP - 9
    bracket(o, x0, x1, yb, False, kind)
    callout(o, LX, y, lines, "r", kind)
    leader(o, [(LX + 12, y - 7), (LX + 30, y - 7), (LX + 30, yb), (x0, yb)], kind, dot=False)

def svg(name, lit, draw, entry=(), shot=None, clip="screen"):
    o = [f'<svg class="mode" viewBox="0 {TOP} {W} {H - TOP}" role="img" aria-label="{html.escape(name)}">']
    ENTRY.clear()
    ENTRY.update(entry)
    o += panel(lit, entry, shot, clip)
    draw(o)
    o.append('</svg>')
    return "\n".join(o)

# ---- the modes
# the places around the panel, each leader along a free channel
def presets_left(o, lines):
    x, y = ENC["PRESETS"]
    left(o, y + 7, lines, (x - 29, y))
def oct_left(o, lines):
    y = OCT["OCT-"][1]
    left(o, y + 7 + 70, lines, (OX + 72, y), via=[(LX + 30, y + 70), (LX + 30, y)])
def voices_left(o, lines):
    voices_bracket(o, OY + 500, lines)
def select_top(o, lines):
    x, y = ENC["SELECT"]
    xl = OX + 60
    callout(o, xl, OY - 40 - (len(lines) - 1) * LH, lines, "c")
    leader(o, [(xl, OY - 32), (xl, OY - 12), (x, OY - 12), (x, y - 29)])
def algo_top(o, lines):
    x, y = ENC["ALGORITHM"]
    xl = OX + 410
    callout(o, xl, OY - 40 - (len(lines) - 1) * LH, lines, "c")
    leader(o, [(xl, OY - 32), (xl, OY - 22), (OX + 272, OY - 22), (OX + 272, y), (x + 29, y)])
def screen_top(o, lines):
    xl = OX + 418
    callout(o, xl, OY - 40 - (len(lines) - 1) * LH, lines, "c")
    leader(o, [(xl, OY - 32), (xl, OY + 44)])
# Leaders to the buttons: each its own lane. Lane 0 is nearest the row and the panel, and goes to the button furthest right,
# with its label nearest the row: then no two leaders cross
CH1 = [OY + 152, OY + 140, OY + 128]          # above the first row of buttons
CH2 = [OY + 310, OY + 320, OY + 330]          # below the second
def vx(ch): return RX - 24 - 14 * ch
def right_btn(o, name, y, lines, ch=0):
    bx, by = btn(name)
    k = "mode" if name in ENTRY else "m"
    if name in ROW1:
        c, end = CH1[ch], (bx, by - 24)
    else:
        c, end = CH2[ch], (bx, by + 24)
    callout(o, RX, y, lines, "l", k)
    leader(o, [(RX - 12, y - 7), (vx(ch), y - 7), (vx(ch), c), (bx, c), end], k)
def right_btns(o, names, y, lines, ch=0):
    """buttons side by side in the first row, under a bracket above them"""
    x0, x1 = btn(names[0])[0] - 27, btn(names[-1])[0] + 27
    c = CH1[ch]
    k = "mode" if set(names) & ENTRY else "m"
    bracket(o, x0, x1, OY + 166, False, k)
    callout(o, RX, y, lines, "l", k)
    leader(o, [(RX - 12, y - 7), (vx(ch), y - 7), (vx(ch), c), ((x0 + x1) / 2, c), ((x0 + x1) / 2, OY + 166)], k, dot=False)
def roll_poly_copy(o, roll=None, poly=None, copy=None, mono=None):
    e = []
    if roll: e.append((7, OX + 510, OY + 770, roll))
    if mono: e.append((8, OX + 680, OY + 770, mono))
    if poly: e.append((9, OX + 930, OY + 770, poly))
    if copy: e.append((10, OX + 1150, OY + 770, copy, 54))
    bottom_keys(o, e)

def home(o):
    k_bracket(o, ["K1–K4", "the voice's pitch, filter, decay, volume", "a step held: lock the value on that step"])
    select_top(o, ["SELECT", "tempo · SEL held: shuffle"])
    algo_top(o, ["ALGORITHM", "morph · SEL held: mutate voice", "a step held: nudge it"])
    presets_left(o, ["PRESETS", "a kit from the bank", "SEL held: mutate the kit", "a step held: its chance"])
    oct_left(o, ["OCT− OCT+", "the bar shown", "SEL held: a bar less, more"])
    voices_left(o, ["VOICE KEYS", "select and play the voice", "ARP held: mute", "SEQ held: select silently",
                    "a step held: its passes, 1–8"])
    keys_bracket(o, ["WHITE KEYS · STEPS 1–16", "tap: a step on or off", "hold: edit that step", "SEL held: the lane's last step"], x=wkey(4)[0])
    roll_poly_copy(o, ["ROLL", "held: roll", "the voice"], ["POLY", "held: the", "morph target"], ["COPY", "+ a white key:", "copy the pattern"], ["MONO", "Franken"])
    right_btn(o, "SEL", OY + 60, ["SEL", "held 1 s: help", "+ EDIT 1 s: clear the lane"])
    right_btn(o, "HOME", OY + 400, ["HOME", "this overview, from anywhere", "+ SEL 1 s: reload saved kit"])

def seq(o):
    k_bracket(o, ["K1–K4", "bars view: velocity · nudge · flam · chance", "note view: note · velocity · key strength · on"])
    select_top(o, ["SELECT", "walk the steps", "SEL held: a bar a turn"])
    algo_top(o, ["ALGORITHM", "nudge the selected step"])
    oct_left(o, ["OCT− OCT+", "bars view: the bar", "note view: the keys' octave"])
    voices_left(o, ["VOICE KEYS", "another voice", "note view: notes, as the rest"])
    keys_bracket(o, ["WHITE KEYS", "bars view: select a step", "note view: all 27 keys play the voice, F3–G5", "a key sets the selected step · SEL held: only listen"], x=wkey(6)[0])
    right_btn(o, "SEL", OY + 40, ["SEL", "tap: the next step"])
    right_btn(o, "REC", OY + 400, ["REC", "note view: the keys write,", "stopped step by step,", "running live"])
    right_btn(o, "SEQ", OY + 540, ["SEQ", "the bars view, again the note view", "HOME leaves"], ch=1)

def arp(o):
    k_bracket(o, ["K1–K4 ON EACH SCREEN", "Lane: length · direction · speed · start", "Euclid: length · hits · rotation · put back",
                  "Vel shape: rhythm · centre · amount · shift"])
    algo_top(o, ["ALGORITHM", "Lane: nudge the whole lane"])
    oct_left(o, ["OCT− OCT+", "the bar shown"])
    voices_left(o, ["VOICE KEYS", "another lane"])
    keys_bracket(o, ["WHITE KEYS", "the lane's steps, as on HOME"], x=wkey(4)[0])
    right_btn(o, "ARP", OY + 420, ["ARP", "Lane → Euclid → Vel shape", "held + a voice key: mute"])

def song(o):
    k_bracket(o, ["K1–K4 ON SONG", "row · pattern or end", "repeat ×1–16, or stop / loop on the end row", "song on or off"])
    keys_bracket(o, ["WHITE KEYS", "SEQ held: pattern 1–16, several: a chain", "on Song: fill the row, then the next"], x=wkey(5)[0])
    roll_poly_copy(o, copy=["COPY", "+ a white key: copy the pattern", "tapped on Song: the chain", "into the rows"])
    right_btn(o, "SEL", OY + 40, ["SEL + K1", "on Song: right copies the row,", "left removes it"])
    right_btn(o, "PLAY", OY + 380, ["PLAY", "the pattern, chain or song"])
    right_btn(o, "SEQ", OY + 470, ["SEQ HELD", "+ a white key: a pattern", "+ ARP: the song"], ch=1)
    right_btn(o, "SAVE", OY + 585, ["SAVE", "patterns, song, kit (stopped)", "SEL + SAVE: at once"], ch=2)

def glo(o):
    k_bracket(o, ["K1–K4", "mixer: level · pan · FX send · drive", "settings: tempo · shuffle · rate · bits"])
    screen_top(o, ["SCREEN", "Mixer: a strip a lane, with level,", "meter, pan, FX, M muted, S soloed"])
    select_top(o, ["SELECT", "settings: the row below,", "USB audio, MIDI clock, preview, roll"])
    oct_left(o, ["OCT− HELD", "settings: the row below,", "while held"])
    voices_left(o, ["VOICE KEYS", "mixer: pick the strip", "SEL held: solo", "ARP held: mute"])
    roll_poly_copy(o, roll=["ROLL", "its rate: K4, the settings' row below"])
    right_btn(o, "GLO", OY + 60, ["GLO", "the mixer, again the settings,", "again the mixer"])

def sound(o):
    k_bracket(o, ["K1–K4", "the row SELECT picked", "a step held: lock the value on it"])
    screen_top(o, ["SCREEN", "three rows of four,", "the one on the dials underlined"])
    select_top(o, ["SELECT", "picks the row on K1–K4,", "the middle one on a new page"])
    oct_left(o, ["OCT+ OCT− HELD", "the top or bottom row,", "while held"])
    voices_left(o, ["VOICE KEYS", "another voice, same page", "on FX: send it to the FX"])
    right_btn(o, "FX", OY - 70, ["FX", "type · mix · two settings", "the row below: the rest"], ch=1)
    right_btns(o, ["ENV", "LFO", "EDIT"], OY + 50, ["EDIT ENV LFO", "the voice's sound", "EDIT: two screens"], ch=0)

def kits(o):
    k_bracket(o, ["K1–K4", "Franken: the parent kits, random till set", "POLY held: the morph target's values"])
    algo_top(o, ["ALGORITHM", "morph, kit to target", "SEL held: mutate the voice"])
    presets_left(o, ["PRESETS", "a kit from the bank; back onto", "an edited one: its edits", "SEL held: mutate the kit",
                     "Franken: a new combo · left: back"])
    voices_left(o, ["VOICE KEYS", "Franken, held + PRESETS:", "a combo of that voice"])
    roll_poly_copy(o, mono=["MONO", "Franken: a kit made", "from up to four"], poly=["POLY HELD", "+ K1–K4: target values", "+ PRESETS: a bank kit", "as the target"])
    right_btn(o, "SAVE", OY + 380, ["SAVE", "K4: pattern, blend or kit", "blend keeps the morph", "as a kit"])
    right_btn(o, "HOME", OY + 520, ["SEL + HOME, 1 s", "reload the kit", "as last saved"], ch=1)

FN_KEYS = {7: "ROLL", 8: "MONO", 9: "POLY", 10: "COPY"}
def bottom_keys(o, entries):
    for i, x, y, lines, *rise in entries:
        kx, r = bkey(i)[0], (rise[0] if rise else 40)
        k = "mode" if FN_KEYS.get(i) in ENTRY else "m"
        callout(o, x, y, lines, "c", k)
        leader(o, [(x, y - 26), (x, y - r), (kx, y - r), (kx, BK_BOT)] if x != kx else [(x, y - 26), (kx, BK_BOT)], k)

# key, title, how to get there, the controls that matter, the drawing, the keys that enter the mode, and the header's keycaps
MODES = [
    ("home", "Steps and the overview", "HOME, from anywhere", {"K", "W", "SELECT", "PRESETS", "ALGORITHM", "SEL"}, home, {"HOME"}, [("HOME",)]),
    ("seq", "One voice: the bars view and the note view", "SEQ; again for the note view", {"K", "W", "SELECT", "ALGORITHM", "REC", "SEL"}, seq,
     {"SEQ"}, [("SEQ",)]),
    ("arp", "The lane: Lane, Euclid and Vel shape", "ARP; again for each screen", {"K", "W", "ALGORITHM"}, arp, {"ARP"}, [("ARP",)]),
    ("song", "Patterns, chains and the song", "SEQ held with a white key for a pattern, with ARP for the song", {"K", "W", "SEL", "SAVE", "PLAY"}, song,
     {"SEQ", "ARP"}, [("SEQ", "held"), ("ARP",)]),
    ("glo", "The mixer and the settings", "GLO; again for the settings, again for the mixer", {"K", "SELECT", "OCT-"}, glo, {"GLO"}, [("GLO",)]),
    ("sound", "The voice's sound and the FX", "EDIT, ENV, LFO or FX", {"K", "SELECT", "OCT-", "OCT+"}, sound, {"EDIT", "ENV", "LFO", "FX"},
     [("EDIT",), ("ENV",), ("LFO",), ("FX",)]),
    ("kits", "Kits, morph and Frankensound", "MONO for Franken; POLY held for the morph target", {"PRESETS", "ALGORITHM", "K", "HOME", "SAVE"}, kits,
     {"MONO", "POLY"}, [("MONO", None, "key"), ("POLY", "held", "key")]),
]

INTRO = {
    "home": "Where a pattern is made. Most of the panel does the same on every page: this is the map to learn first.",
    "seq": "One voice at a larger size. The bars view edits each step's details; the note view plays and writes notes from all the keys.",
    "arp": "The selected lane's own settings, on three screens. The steps stay on the white keys.",
    "song": "Patterns are picked with SEQ held. A chain loops until you pick a single pattern; the song plays its rows in order.",
    "glo": "Every lane's level at a glance, with mute and solo. The settings are the second screen.",
    "sound": "The selected voice's parameters, three rows of four, and the FX bus.",
    "kits": "Frankensound (Franken on the screen) makes the pattern's kit from up to four kits, a section of each voice from each: edit and save it as any other. Kits left on random are drawn from the bank for each combo, and a voice key held makes a combo of that voice alone. Morph blends the kit towards the pattern's own target.",
}


def caps_html(cs):
    """the header's keycaps for the keys that enter a mode"""
    out = []
    for c in cs:
        label, held, kind = (list(c) + [None, None])[:3]
        out.append(f'<kbd class="cap{" key" if kind == "key" else ""}">{html.escape(label)}</kbd>' + (f'<span class="held">{held}</span>' if held else ""))
    return '<span class="sep">or</span>'.join(out) if len(cs) == 4 else "".join(out)
