/* SPDX-License-Identifier: GPL-3.0-only */
/* Tack on the FM-1: the panel and the screen, part of fm1-base's unity build. The engine and sequencer are in
 * src/tack_engine.c; everything reaches them through its event queue, so the audio interrupt stays their one writer.
 *
 *   white keys   STEP mode: the 16 steps of the page, on release
 *                  a step held, K1-K4: lock those values on it (eight a step), except in the bars view; EDIT: clear its locks
 *                  SEL held: this step is the lane's last
 *                  SEQ held: pattern 1-16
 *   black keys   the first seven select a voice and play it, or on the FX page send it to the FX; with ARP held, mute it;
 *                  with SEQ held, select it silently. REC with the sequencer running records them
 *                the eighth: ROLL held. The ninth, MONO: FRANKEN. The tenth, POLY, held: the morph target, its values on
 *                K1-K4 and a bank kit on PRESET. The last: COPY held with a white key, this pattern onto that one. Each names
 *                itself in the top bar while held
 *   SEQ          tap: a voice in detail, its steps or NOTE, as last seen; tapped there, the other one
 *   OCT- OCT+    the bar               PLAY  start, stop     REC  live recording
 *                  SEL held: a bar more or less on the lane, 16 steps either way
 *   K1-K4        the page: pitch, filter, decay, volume of the voice unless a page is open
 *   EDIT ENV LFO the voice's sound pages, three rows of four. EDIT has two screens, and a press goes between them.
 *                  Every page's button works so: it opens the page, goes through its screens if it has more than one, and
 *                  otherwise does nothing. HOME, SEQ or another page's button leaves
 *                  On a drum or the snare the first screen's lower row is its pitch and the MOD EG that moves it
 *   OCT+ OCT-    on those, held: the dials reach the row above or below, and the middle one when let go
 *   HOME         the overview, every voice's steps, from anywhere. It ends NOTE, so the black keys there always pick a voice.
 *                  SEQ goes back to NOTE if that was the detail left
 *   ARP          tap: the song, tapped again the velocity page and back. There K1 picks a rhythm's shape, K2 and K3 its
 *                  centre and amount, K4 moves it: it writes new velocities into the lane, so the steps hold them. K1 at
 *                  OFF puts back the ones from before. The white keys are steps, and OCT the bar
 *   SEL          held a moment in the bars view, K1-K4 are the lane's: Euclidean length, hits, rotation, and its nudge.
 *                  HITS a click below 1, the dial at rest, puts the lane back.
 *                  Held, what it says below. Tapped in the bars view or NOTE, the next step
 *   a step held  ALGORITHM nudges it, PRESET sets its chance, and SELECT and the first eight black keys its recurrence, each in
 *                  a popup until the step is let go
 *   SEQ          one voice's view: its steps at a larger size, and what each holds. A key pressed or a tap of SEL selects a
 *                  step, and K1-K4 are its velocity, nudge, flam and chance, held or not. SELECT selects too, either way, and
 *                  ALGORITHM nudges the step, so tempo and shuffle are on the other pages.
 *                  In NOTE the same steps show their notes, and all the keys play the voice, OCT their range; held, a black key
 *                  is the voice. REC on writes them: running live, stopped step by step. SEL and SELECT select a step, K1 its
 *                  note, K2 its velocity, K3 how hard the keys write, K4 on or off, and ALGORITHM nudges it
 *   FX           the FX pages: type, mix, two settings, then the type's others
 *   GLO          the mixer: a strip a lane, and the voice's level, pan, FX send and drive; SEL and a black key solo.
 *                  Again, the settings page: tempo, shuffle, external MIDI clock, roll rate, the load
 *   ARP          the selected lane: LANE, EUCL and VEL in turn. SEQ held with it: SONG, where the white keys fill the rows
 *                  GLO held: PLAY starts the load test, and PLAY alone ends it; HOME zeroes PEAK and XRUN
 *   SELECT       tempo                 ALGORITHM  shuffle             PRESET  kit       (the first two on a screen of their own)
 *   SAVE         every changed pattern, the kit and tempo (stopped)     SEL and EDIT held a second  clear the lane
 *                                                                       SEL and HOME held a second  the kit as in the bank
 */
#include "src/tack_engine.h"
#include "src/seq_platform.h"
#include "../../engine/kit_name.h"

static const char *const VOICE_NAME[TACK_VOICES] = {"DRUM 1", "DRUM 2", "DRUM 3", "SNARE", "CYMBAL", "CL HAT", "OP HAT"};
static const char *const P_NAME[TACK_NP] = {"PITCH", "FILTER", "DECAY", "VOLUME"};
static const char *const SEND_NAME[TACK_FX_SENDS] = {"D1", "D2", "D3", "SN", "CY", "HH"};
static const uint8_t WK[16] = PLAT_WHITE_KEYS, BK[11] = PLAT_BLACK_KEYS;
/* each kind of voice has a colour, as in the desktop editor: red drums, orange snare, yellow cymbal, cream hi-hats.
 * It always means which voice; grey is structure, and the theme's colour is for values */
/* Type comes in three sizes. FONT_B is what is read at a glance or being changed: the kit, the pattern, the tempo, a page's title,
 * the value under a dial as it turns. FONT_S is a page's dial labels and values, and HOME's title. FONT_XS is state and help: REC,
 * SW, FX, NEXT and hints. Sizes on one line share a baseline: FONT_XS goes 1 pixel below FONT_B's y */
#define EDGE 4                                  /* the screen's margin, both sides */
#define TOP_LINE 4                              /* where a screen's top line of letters starts, as EDGE is the sides: FONT_B goes at 2 less,
                                                   FONT_XS at 3 less to start there too, or at 1 less to share FONT_B's baseline */
/* HOME's vertical rhythm, measured to the ink and not to a font's box: EDGE above the first line, SP_IN between the parts of
 * one section (the lanes and their bar markers, a label and its value), SP_SECTION between sections. Each strip's y comes
 * from these, so the gaps stay even when one strip changes */
#define SP_IN 4
#define SP_SECTION 8
#define HEADER_H 51                             /* the status line, SP_SECTION, the voice, SP_IN, the bar markers, SP_IN */
#define CELL_W 55                                /* the dials' four cells, EDGE to EDGE with gaps of 4 */
#define CELL_X(i) (EDGE + (int32_t)(i) * (CELL_W + 4))
static const uint16_t VOICE_COLOUR[TACK_VOICES] = {RGB(224, 66, 52), RGB(224, 66, 52), RGB(224, 66, 52), RGB(240, 125, 36),
                                                     RGB(242, 194, 61), RGB(237, 230, 212), RGB(237, 230, 212)};
enum { KEY_ROLL = TACK_VOICES, KEY_FRANKEN = TACK_VOICES + 1, KEY_MORPH = TACK_VOICES + 2, KEY_COPY = TACK_VOICES + 3 };   /* the black keys after the voices: ROLL, MONO, POLY, COPY */

#define SET_MAGIC 0x4B544C31u            /* "KTL1" */
#define KIT_SLOT 256u
typedef struct {
    uint32_t magic;
    uint16_t bpm;
    uint8_t kit_now;                     /* the bank's kit it came from: slot + 1, 0 none */
    uint8_t keys_quiet;                  /* the black keys only select a voice: older saves have 0, which plays it */
    uint8_t kit[KIT_SLOT];               /* the parameters, then the name at SET_KIT_NAME; older saves have it empty */
    uint8_t song[TACK_SONG_ROWS];      /* older saves stop here */
    uint8_t song_on, rsv2[3];
} settings_t;
#define SET_OLD_SIZE (sizeof(settings_t) - TACK_SONG_ROWS - 4u)
#define SET_KIT_NAME 248u
_Static_assert(sizeof(settings_t) <= PLAT_STORE_MAX, "one store object");
_Static_assert(sizeof(settings_t) == 332u && __builtin_offsetof(settings_t, song) == 264u, "what the store and a backup hold");
_Static_assert(SET_KIT_NAME + 8u == KIT_SLOT, "the name after the parameters");
static settings_t set;
static uint8_t pat_buf[3840];

/* the kit bank: uploaded by tools/fm1_kits.py from the user's own files, never shipped in the firmware. A slot is the
 * .SND file's 8-byte name and its parameters; an erased slot reads 0xFF */
#define KIT_BASE 0xB8000u
#define KIT_SLOTS 128u
#define KIT_SECTORS 8u
#define KIT_PART 64u                     /* a slot read back goes in parts: a SysEx frame fits the USB ring */
_Static_assert(KIT_BASE >= PAT_AT(16), "above the patterns");
_Static_assert(KIT_BASE + KIT_SLOTS * KIT_SLOT <= 0xDC000u, "below the panel calibration");
static uint8_t kit_buf[KIT_SLOT];

static const uint8_t *kit_slot(uint32_t i)
{
    const uint8_t *p = i < KIT_SLOTS ? plat_data(KIT_BASE + i * KIT_SLOT) : 0;
    return p && p[0] != 0xFFu ? p : 0;
}

/* a pattern's kit, as the sequencer switches to it: the audio interrupt, so the parameters in place in the flash */
static const uint8_t *kit_params(uint32_t i)
{
    const uint8_t *p = kit_slot(i);
    return p ? p + 8 : 0;
}

static struct {
    uint32_t keys, btn, redraw;
    int32_t voice, bar;
    uint8_t stress;
    uint8_t keys_quiet;                  /* PREVIEW off: settings_t's */
    uint8_t over;                        /* the page over the group's: 0 none, or a PAGE_ of view.c. One at a time, by being one value */
    uint8_t song_row, chain_n;           /* the row the SONG page edits; the keys taken while SEQ is held */
    uint8_t meter[TACK_FX_SENDS];        /* the mixer's meters, falling back from each voice's peaks */
    /* Frankenkit: K1 to K4's parents (0 none, 1 this kit, 2 on the bank's slots), the roll and its seed, each cell's parent, the
     * kit there was before the first roll, which is "this kit" and which roll 0 gives back, and the kit the last roll made, as
     * it went in (fk_settle), so that an edit since shows */
    int16_t fk_parent[4];
    int32_t fk_roll;
    uint32_t fk_seed;
    uint8_t fk_ready, fk_pending, fk_where[TACK_FRANKEN_CELLS], fk_before[256], fk_before_name[8], fk_kit[256], fk_last[256];
    uint8_t note_mode, note_step, note_vel;   /* NOTE: the keys are notes; the step it writes next, how hard */
    uint8_t note_last;                   /* the detail HOME left was NOTE: where SEQ goes back to */
    uint8_t vs_shape, vs_shift, vs_centre, vs_amount;   /* the velocity page's dials: a tool's settings, kept nowhere */
    uint32_t note_held;                  /* keys down as notes: they end as notes, whatever the page is by then */
    int8_t note_oct;
    uint8_t key_note[NKEYS];             /* what each key played, for its note-off */
    uint8_t key_voice[NKEYS];            /* and on which voice, as another may be picked before it comes up */
    uint8_t seq_voice;                   /* SEQ held picked a voice: no mode change on release */
    uint8_t bad_patterns;                /* patterns the store held that were refused */
    uint8_t roll_voice;                  /* the voice ROLL went down on */
    uint8_t roll_on;                     /* ROLL is down */
    uint8_t copy_used;                   /* COPY went on to a white key: not a tap */
    uint8_t ks_what, ks_pat;             /* the save page: what, and the pattern slot */
    uint8_t ks_slot, ks_pos;             /* the bank's slot, and the letter being changed */
    uint8_t ks_morph;                    /* it saves what morph makes, not the kit */
    uint8_t ks_typed;                    /* the name is the player's now: picking a slot leaves it alone */
    uint32_t ks_seed;                    /* the last name made up */
    uint8_t save_as;                     /* a pattern on its way to this slot + 1, to be written when it lands */
    char ks_name[9];
    uint32_t clear_at, revert_at;        /* when SEL and EDIT, SEL and HOME went down together, 0 not */
    uint8_t group, sub;                  /* the sound page */
    uint32_t save_todo;                  /* patterns a SAVE still has to write, one a frame */
    uint8_t saved, save_hold;            /* how many; a backup is reading the flash */
    uint32_t save_gap;                   /* the longest a save has held the tick up (us) */
    int kit_seen;                        /* the kit the name is of */
    uint8_t seq_used;                    /* SEQ was a modifier this press: no mode change on release */
    uint8_t glo_used;                    /* so was GLO: no page */
    uint8_t arp_used;                    /* so was ARP */
    uint8_t usb10;                       /* USB audio is 10 channels, not the stereo mix */
    uint32_t usb10_try;                  /* a host must have taken them by then */
    uint8_t voice_view;                  /* the main page shows a detail, not the overview: one voice's steps, or NOTE */
    uint8_t popup;                       /* what a held step is having changed: POP_ */
    uint32_t tempo_until;                /* the header shows the tempo or the shuffle until then, 0 not */
    uint8_t tempo_shuffle;               /* which: the shuffle, SEL held */
    const TackSlot *touched;           /* the parameter last turned, large in the header until touched_until */
    uint32_t touched_at, touched_until;
    int8_t touched_row;                  /* the row it was on: an OCT since shows another */
    uint8_t last_row[4];                 /* each group's middle row when it was left */
    int8_t row_seen;                     /* the row a held step's knobs started from */
    uint8_t sel_used;                    /* so was SEL */
    uint8_t sel_was;                     /* the lane's length was showing */
    uint32_t sel_at;                     /* when SEL went down */
    uint32_t lane_at;                    /* and the voice key below */
    uint8_t lane_key;                    /* the voice key that picked the voice last, + 1: held, it is the lane */
    uint8_t help_on;                     /* the list of what SEL does is on screen */
    int32_t held;                        /* the white key held (0-15), -1 none */
    uint8_t held_used;                   /* it did something else: no toggle on release */
    uint32_t held_at;                    /* when it went down */
    uint8_t held_was;                    /* held_shown() at the last frame */
    uint8_t held_step;                   /* the step it holds, which stays it when the bar moves */
    uint8_t held_pattern;                /* and in which pattern: another one ends the hold */
    uint8_t held_val[TACK_NP];
    int32_t kit;
    char kit_name[9];
    char msg[24];
    uint32_t msg_until;
    uint32_t cpu_peak;
    uint32_t xrun_base, xrun_seen;       /* HOME's zero, and the count the last frame saw */
    char act, xrun_act;                  /* what the panel last did, and what it was at the last xrun */
    uint32_t act_ms;
} ui;

/* XRUN on the screen says what was just done when one came: K kit, P pattern, F FX, S save, U upload, ? nothing */
static void mark(char what)
{
    ui.act = what;
    ui.act_ms = plat_ms();
}

static volatile uint32_t midi_seen;

static void int_text(int32_t v, char *b)     /* signed, no padding */
{
    char t[12];
    int n = 0;
    uint32_t u = (uint32_t)(v < 0 ? -v : v);
    do
        t[n++] = (char)('0' + u % 10u);
    while ((u /= 10u) && n < 11);
    if (v < 0)
        *b++ = '-';
    while (n)
        *b++ = t[--n];
    *b = 0;
}

/* a part of a line, ended, and where the next part goes */
static char *text_put(char *to, const char *s)
{
    while (*s)
        *to++ = *s++;
    *to = 0;
    return to;
}

/* strcmp() == 0: the firmware's libc has only the mem functions */
static int same(const char *a, const char *b)
{
    while (*a && *a == *b)
        a++, b++;
    return *a == *b;
}

static char *int_put(char *to, int32_t v)
{
    int_text(v, to);
    while (*to)
        to++;
    return to;
}

/* audio interrupt: MIDI in, as Tack's channels: channel 1 plays the kit from note 60, 2-8 one voice each. On channel 1
 * notes 0-15 and Program Change 0-15 pick a pattern, which a DAW can draw on a lane of its own */
static void midi_in(void)
{
    uint32_t pkt;
    while (plat_midi_in(&pkt, 0)) {
        uint32_t st = (pkt >> 8) & 0xFFu, d1 = (pkt >> 16) & 0x7Fu, d2 = pkt >> 24;
        if (st >= 0xF8u) {
            tack_midi_realtime((uint8_t)st);
        } else if ((st & 0xF0u) == 0x90u && d2) {
            uint32_t ch = st & 15u;
            midi_seen++;
            if (ch == 0u && d1 < TACK_PATTERNS)
                tack_pattern_now((uint8_t)d1);
            else if (ch == 0u && d1 >= 60u && d1 < 60u + TACK_VOICES)
                tack_note_now((uint8_t)(d1 - 60u), 60, (uint8_t)d2);
            else if (ch >= 1u && ch <= TACK_VOICES)
                tack_note_now((uint8_t)(ch - 1u), (uint8_t)d1, (uint8_t)d2);
        } else if (st == 0xB0u && d1 == 1u) {    /* the mod wheel, or a DAW's lane: how far morph goes */
            midi_seen++;
            tack_morph_now((uint8_t)d2);
        } else if ((st & 0xF0u) == 0x80u || (st & 0xF0u) == 0x90u) {
            uint32_t ch = st & 15u;
            if (ch == 0u && d1 >= 60u && d1 < 60u + TACK_VOICES)
                tack_note_off_now((uint8_t)(d1 - 60u), 60);
            else if (ch >= 1u && ch <= TACK_VOICES)
                tack_note_off_now((uint8_t)(ch - 1u), (uint8_t)d1);
        } else if (st == 0xC0u) {
            midi_seen++;
            tack_pattern_now((uint8_t)d1);
        }
    }
}

void app_render(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    midi_in();
    tack_render(lr, n, master_q12);
}

void app_panic(void) { tack_post_run(0); }

static void seq_midi_out(uint8_t status, uint8_t d1, uint8_t d2)
{
    plat_midi_send((uint32_t)(status >> 4) | (uint32_t)status << 8 | (uint32_t)d1 << 16 | (uint32_t)d2 << 24, TO_USB);
}

/* USB audio as the stereo mix or the 10 channels. A change re-enumerates, and takes USB-MIDI with it for a moment. USB is how
 * an update reaches the FM-1, so the 10 channels are never saved: every power-on is stereo, and a host that has not taken them
 * within USB10_TRY_MS goes back to it too */
#define USB10_TRY_MS 8000u
#ifdef PLAT_HOST
static uint32_t usb_drops(void) { return 0; }
static int usb_host_there(void) { return 1; }
static void usb_switch(int on) { (void)on; }
static void usb_pump_stats(uint32_t *v) { v[0] = v[1] = v[2] = v[3] = 0; }
static uint32_t plat_tick_gap(void) { return 0; }
#else
uint32_t plat_tick_gap(void);
static void usb_pump_stats(uint32_t *v)   /* armed, packets, gave up, the longest erase (ms) */
{
    v[0] = fl_pump.armed;
    v[1] = fl_pump.sent;
    v[2] = fl_pump.quits;
    v[3] = fl_pump.erase_us / 1000u;
}
static uint32_t usb_drops(void) { return usb_uac10_drops(); }
static int usb_host_there(void) { return usb.config != 0; }
static void usb_switch(int on) { usb_uac10_switch((uint32_t)on); }
#endif

static void usb_audio_set(int on)
{
    ui.usb10 = (uint8_t)on;
    ui.usb10_try = on ? plat_ms() + USB10_TRY_MS : 0u;
    usb_switch(on);
}

/* the store into the engine, as at power-on. With a backup restored the sequencer is stopped, and what a pattern's
 * object no longer holds is cleared */
static void load_saved(int running_audio)
{
    uint32_t i;
    for (i = 0; i < TACK_PATTERNS; i++) {
        const int len = plat_store_load(OBJ_PAT0 + i, pat_buf, sizeof pat_buf);
        const int bad = len > 0 && tack_load_pattern((uint8_t)i, pat_buf, (uint32_t)len) != 0;
        ui.bad_patterns = (uint8_t)(ui.bad_patterns + bad);
        if (bad || (running_audio && len <= 0))
            tack_clear_pattern((uint8_t)i);
        tack_clean((uint8_t)i);
    }
    const int got = plat_store_load(OBJ_SETTINGS, &set, sizeof set);
    const int ok = got >= (int)SET_OLD_SIZE && set.magic == SET_MAGIC;
    tack_song_load(ok && got == (int)sizeof set ? set.song : 0, ok && got == (int)sizeof set && set.song_on);
    ui.keys_quiet = ok && set.keys_quiet == 1u;
    if (ok)
        tack_post_bpm(set.bpm);
    {                                    /* the pattern's own kit, or for one that has none the kit it was left on */
        const uint32_t pat = (uint32_t)tack_pattern();
        const uint8_t *own = tack_pattern_kit(pat), *values = own ? own : ok ? set.kit : 0;
        const int now = own ? tack_pattern_kit_slot(pat) : ok ? set.kit_now : 0;
        if (values) {
            if (running_audio) {
                while (tack_kit_busy())
                    ;
                tack_post_kit(values, 0, TACK_KIT_NO_SLOT);
            } else {
                tack_load_kit(values);
            }
            tack_set_kit_name(own ? tack_pattern_kit_name(pat) : set.kit + SET_KIT_NAME);
            tack_set_kit_now(now);
            ui.kit_seen = now;
            ui.kit = now ? now - 1 : 0;
        }
    }
}

void app_init(void)
{
    tack_init();
    seq_platform_set_midi_out(seq_midi_out);
    tack_set_kit_find(kit_params);
    tack_set_kit_name_find(kit_slot);
    load_saved(0);
}

void app_say(const char *a, const char *b)
{
    uint32_t i = 0;
    while (i < 11u && a && a[i]) {
        ui.msg[i] = a[i];
        i++;
    }
    if (b && b[0]) {
        ui.msg[i++] = ' ';
        for (uint32_t j = 0; j < 11u && b[j]; j++)
            ui.msg[i++] = b[j];
    }
    ui.msg[i] = 0;
    ui.msg_until = plat_ms() + 1000u;
    ui.redraw = 1;
}

#include "app/store.c"

#include "app/view.c"

#include "app/dials.c"

#include "app/input.c"

#include "app/kit_sysex.c"

#include "app/lights.c"

#include "app/home.c"

#include "app/widgets.c"

#include "app/sound_pages.c"

#include "app/popups.c"

#include "app/screens.c"

/* the first screen at boot: the name on black, the voices' colours, and who made it */
void app_splash(void)
{
    static const uint8_t HEIGHT[TACK_VOICES] = {18, 6, 12, 4, 15, 7, 10};
    enum { BAR = 12, GAP = 4, BARS_W = TACK_VOICES * BAR + (TACK_VOICES - 1) * GAP, BARS_Y = 62, TRACK = 3 };
    int32_t x = 120 - BARS_W / 2;
    lcd_fill(0, 0, 240, 240, C_BLACK);
    cv_begin(240, 104, C_BLACK);
    cv_text(120 - text_w(&FONT_L, APP_NAME) / 2, 0, &FONT_L, APP_NAME, APP_SPLASH_RGB);
    for (uint32_t v = 0; v < TACK_VOICES; v++, x += BAR + GAP)
        cv_rect(x, BARS_Y - HEIGHT[v], BAR, HEIGHT[v], VOICE_COLOUR[v]);
    cv_text(120 - text_w(&FONT_S, APP_TAGLINE) / 2, BARS_Y + 10, &FONT_S, APP_TAGLINE, C_GRAY);
    cv_blit(0, 56);
    cv_begin(240, FONT_XS.h, C_BLACK);
    x = 120 - (text_w(&FONT_XS, APP_MAKER) + TRACK * ((int32_t)sizeof APP_MAKER - 2)) / 2;
    for (const char *c = APP_MAKER; *c; c++) {
        const char one[2] = {*c, 0};
        cv_text(x, 0, &FONT_XS, one, C_GRAY);
        x += text_w(&FONT_XS, one) + TRACK;
    }
    cv_blit(0, 212);
}

static void draw(void)
{
    char t[16];
    uint32_t i;
    const int locking = held_shown();
    static int on_grid;
    if (on_grid && (ui.popup || ui.help_on || !full_page()))   /* a grid page covered the bottom strip, unchanged as its values may be */
        bottom_key = 0;
    if (ui.help_on) {
        on_grid = 0;
        draw_sel_help();
        return;
    }
    if (ui.popup) {                              /* the page waits behind it */
        on_grid = 0;
        draw_popup();
        return;
    }
    if (full_page()) {
        pg_screen(!on_grid);
        on_grid = 1;
        return;
    }
    on_grid = 0;
    cv_begin(240, HEADER_H, C_BLACK);
    if (tack_has_target() && !ui.msg_until && !key_word()) {   /* a crossfader between the two kits */
        char to[9];
        const char *from = ui.kit_name[0] ? ui.kit_name : "INIT KIT";
        morph_name(to);
        cv_text(EDGE, TOP_LINE - 3, &FONT_XS, from, tack_morph() < 64 ? C_WHITE : C_GRAY);   /* their tops level with P1's */
        cv_text(110 - text_w(&FONT_XS, to), TOP_LINE - 3, &FONT_XS, to, tack_morph() >= 64 ? C_WHITE : C_GRAY);
        cv_rect(EDGE, TOP_LINE + 13, 106, 2, C_LINE);
        cv_rect(EDGE + tack_morph() * 100 / 127, TOP_LINE + 11, 6, 6, C_HI);
    } else {
        cv_text(EDGE, TOP_LINE - 2, &FONT_B, ui.msg_until ? ui.msg : key_word() ? key_word() : ui.kit_name[0] ? ui.kit_name : "INIT KIT", C_WHITE);
    }
    t[0] = 'P';
    int_text(tack_pattern() + 1, t + 1);
    cv_text(146, TOP_LINE - 2, &FONT_B, t, tack_running() ? C_HI : C_GRAY);
    const int32_t after_p = 146 + text_w(&FONT_B, t) + 4;
    if (tack_running())
        dir_arrow(134, TOP_LINE + 2, 0, C_HI);
    else
        cv_rect(134, TOP_LINE + 3, 6, 6, C_GRAY);
    if (tack_recording())
        cv_text(112, TOP_LINE - 1, &FONT_XS, "REC", REC_RED);
    int_text(tack_bpm(), t);
    const int32_t bpm_x = 240 - EDGE - text_w(&FONT_B, t);
    cv_text(bpm_x, TOP_LINE - 2, &FONT_B, t, C_HI);
    if (tack_shuffle()) {                      /* none is nothing to say */
        /* next to the tempo it shapes, unless that would reach P10 to P16. Small text 1 lower sits on their baseline */
        int_text(tack_shuffle(), t);
        const int32_t sw_w = text_w(&FONT_XS, "SW") + 2 + text_w(&FONT_XS, t);
        const int32_t sw_x = bpm_x - 6 - sw_w > after_p ? bpm_x - 6 - sw_w : after_p;
        cv_text(sw_x, TOP_LINE - 1, &FONT_XS, "SW", C_GRAY);
        cv_text(sw_x + text_w(&FONT_XS, "SW") + 2, TOP_LINE - 1, &FONT_XS, t, C_WHITE);
    }
    if (tempo_shown()) {                         /* as a dial in hand: the tempo, or with SEL the shuffle */
        const int sh = ui.tempo_shuffle;
        int_text(sh ? tack_shuffle() : tack_bpm(), t);
        cv_text(EDGE, 20, &FONT_M, sh ? "SHUFFLE" : "TEMPO", C_HI);
        cv_text(240 - EDGE - text_w(&FONT_M, t), 20, &FONT_M, t, C_WHITE);
        if (sh)
            value_bar(EDGE, 44, 240 - 2 * EDGE, 4, tack_shuffle(), 127, C_HI);
        else                                     /* where the shuffle is, which nothing else says */
            cv_text(EDGE + text_w(&FONT_M, "TEMPO") + 8, 27, &FONT_XS, "SEL+ SHUFFLE", C_GRAY);
        cv_blit(0, 0);
        goto header_done;
    }
    if (ui.touched_until && ui.touched) {        /* the parameter in hand, large */
        const TackSlot *sl = ui.touched;
        const uint8_t v = ui.held >= 0 ? ui.held_val[ui.touched_at] : morph_shown(sl) ? tack_target_value(sl->par) : tack_get_param(sl->par);
        const char *name = tack_slot_text(sl, v);
        cv_text(EDGE, 20, &FONT_M, sl->label, VOICE_COLOUR[ui.voice]);
        if (name) {
            cv_text(112, 25, &FONT_S, name, C_WHITE);
        } else {
            value_text(sl, v, t);
            cv_text(240 - EDGE - text_w(&FONT_M, t), 20, &FONT_M, t, C_WHITE);
            slot_bar(EDGE, 44, 240 - 2 * EDGE, 4, sl, v, VOICE_COLOUR[ui.voice]);
        }
        cv_blit(0, 0);
        goto header_done;
    }
    cv_text(EDGE, 20, &FONT_M, VOICE_NAME[ui.voice], tack_muted((uint8_t)ui.voice) ? C_GRAY : VOICE_COLOUR[ui.voice]);
    if (tack_fx_send((uint32_t)ui.voice))
        cv_text(240 - EDGE - text_w(&FONT_XS, "FX"), 28, &FONT_XS, "FX", C_HI);
    for (int b = 0; b < (int)TACK_BARS; b++)   /* just over the grid, all of them: the one it shows, those in use, and the room for more */
        cv_rect(240 - EDGE - ((int)TACK_BARS - b) * 14 + 2, 44, 12, 3, b == ui.bar ? C_WHITE : b < bars_used() ? C_GRAY : C_LINE);
    cv_blit(0, 0);
header_done:
    draw_grid();
    if (ui.over)
        goto strip_done;
    cv_begin(240, KNOBS_H, C_BLACK);             /* nothing marks HOME's dials: they are always K1 to K4, and the value bars line them up */
    for (i = 0; i < TACK_NP; i++) {
        const TackSlot *sl = slot(i);
        const int32_t x = CELL_X(i) + 5;
        const char *name;
        uint8_t v;
        int lit = 0;
        if (!sl)
            continue;
        v = tack_get_param(sl->par);
        if (locking) {
            v = ui.held_val[i];
            lit = tack_lock_get((uint8_t)ui.voice, ui.held_step, sl->par, &v);
            if (lit)
                v = ui.held_val[i];
        } else if (morph_shown(sl)) {
            v = tack_target_value(sl->par);
            lit = 1;
        }
        cv_text(x, 2, &FONT_XS, sl->label, PG_TEXT);
        name = tack_slot_text(sl, v);
        if (sl->target) {                    /* too long for a column: draw_bottom */
            knob_word(x, "BELOW", C_GRAY);
        } else if (name) {
            knob_word(x, name, lit ? C_HI : C_WHITE);
        } else {
            value_text(sl, v, t);
            knob_num(x, t, lit ? C_HI : C_WHITE);
        }
        if (!sl->en && !sl->target)
            slot_bar(x, 31, CELL_W - 10, 3, sl, v, lit ? C_HI : VOICE_COLOUR[ui.voice]);
    }
    cv_blit(0, KNOBS_Y);
strip_done:
    draw_bottom();
}

void app_ui_init(void)
{
    if (ui.bad_patterns) {
        char t[4];
        int_text(ui.bad_patterns, t);
        app_say(t, "BAD PATTERNS");
    }
    ui.note_vel = 110;
    ui.vs_centre = 90;
    ui.vs_amount = 70;
    ui.held = -1;
    lcd_fill(0, 0, 240, 240, C_BLACK);
    ui.redraw = 1;
}

void app_frame(void)
{
    static uint32_t last, seen;
    static uint32_t head;
    uint32_t now = plat_ms(), pct = plat_cpu_pct(), v, x = plat_xruns();
    uint32_t h = 0;
    if (x != ui.xrun_seen) {
        ui.xrun_seen = x;
        ui.xrun_act = now - ui.act_ms < 500u && ui.act ? ui.act : '?';
        ui.redraw = 1;
    }
    for (v = 0; v < TACK_VOICES; v++)          /* any lane's playhead moved */
        h = h * 67u + (uint32_t)tack_playhead((uint8_t)v);
    h = h * 67u + (uint32_t)(tack_song_mode() * 256 + tack_song_pos());
    h = h * 67u + (uint32_t)(tack_bits() + tack_morph() * 128 + tack_morph_kit() * 16384 + tack_has_target() * 4194304);
    for (v = 0; v < TACK_NP; v++)               /* POLY held: the target's values */
        h = h * 67u + (morph_held() && slot((uint32_t)v) ? tack_target_value(slot((uint32_t)v)->par) + 256u : 0u);
    h = h * 67u + (uint32_t)(tack_bpm() * 128u + tack_shuffle());   /* a turn reaches the engine after the frame that drew it */
    h = h * 67u + (uint32_t)(tack_lane_start((uint8_t)ui.voice) + tack_lane_nudge_get((uint8_t)ui.voice) * 64);
    h = h * 67u + (uint32_t)(tack_length((uint8_t)ui.voice) * 64) + tack_lane_dir((uint8_t)ui.voice) * 4u + tack_lane_speed((uint8_t)ui.voice);
    if (sel_length() != ui.sel_was) {
        ui.sel_was = (uint8_t)sel_length();
        ui.redraw = 1;
    }
    if (held_shown() != ui.held_was) {          /* the hold has lasted long enough to show the step */
        ui.held_was = (uint8_t)held_shown();
        ui.redraw = 1;
    }
    if (ui.note_mode || ui.held >= 0) {          /* a step's edit too */
        const uint8_t s = (uint8_t)(ui.held >= 0 ? (int)ui.held_step : (int)note_cursor());
        for (v = 0; v < TACK_ST_SLOTS; v++)
            h = h * 67u + (uint32_t)tack_step_get((uint8_t)ui.voice, s, v);
        h = h * 67u + (uint32_t)(tack_step_on((uint8_t)ui.voice, s) + tack_recur_len((uint8_t)ui.voice, s) * 2 +
                                 tack_recur_mask((uint8_t)ui.voice, s) * 32);
    }
    if (song_pages())                            /* a row turned reaches the engine after the frame that drew it */
        h = h * 67u + song_len() * 256u + tack_song_get(song_len()) + tack_song_get(ui.song_row) * 4u;
    input();
    if (sel_help() != ui.help_on) {
        ui.help_on = (uint8_t)sel_help();
        bottom_key = 0;                          /* it covers the main page as a popup does */
        ui.redraw = 1;
    }
    save_frame();
    tack_view_sync();                          /* a kit that went in shows, whichever way it came */
    if (tack_kit_now() != ui.kit_seen) {
        ui.kit_seen = tack_kit_now();
        ui.kit = ui.kit_seen ? ui.kit_seen - 1 : 0;
        ui.redraw = 1;
    }
    if (memcmp(ui.kit_name, tack_kit_name(), 8u)) {   /* the pattern's own, which need be in no slot */
        memcpy(ui.kit_name, tack_kit_name(), 8u);
        ui.redraw = 1;
    }
    lights();
    if (pct > ui.cpu_peak)
        ui.cpu_peak = pct;
    if (ui.msg_until && now >= ui.msg_until) {
        ui.msg_until = 0;
        ui.redraw = 1;
    }
    if (ui.tempo_until && now - ui.tempo_until < 0x80000000u) {
        ui.tempo_until = 0;
        ui.redraw = 1;
    }
    if (ui.usb10_try && now - ui.usb10_try < 0x80000000u) {
        ui.usb10_try = 0;
        if (!usb_host_there()) {
            usb_audio_set(0);
            app_say("USB AUDIO", "STEREO");
        }
    }
    if (ui.touched_until && now >= ui.touched_until) {
        ui.touched_until = 0;
        ui.redraw = 1;
    }
    if (now - last >= 500u || seen != midi_seen || h != head) {   /* the load twice a second, and the playhead */
        last = now;
        seen = midi_seen;
        head = h;
        ui.redraw = 1;
    }
    if (ui.redraw) {
        ui.redraw = 0;
        draw();
    }
}

#include "app/test_hooks.c"
