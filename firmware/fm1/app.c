/* SPDX-License-Identifier: GPL-3.0-only */
/* Tack on the FM-1: the panel and the screen, part of fm1-base's unity build. The engine and sequencer are in
 * src/tack_engine.c; everything reaches them through its event queue, so the audio interrupt stays their one writer.
 *
 *   white keys   STEP mode: the 16 steps of the page, on release. PLAY mode: the first seven play the voices
 *                  a step held, K1-K4: lock those values on it (two a step); EDIT: clear its locks
 *                  SEL held: this step is the lane's last
 *                  SEQ held: pattern 1-16
 *   black keys   the first seven select a voice, or on the FX page send it to the FX; with ARP held, mute it
 *                the next four: ROLL held, the Euclidean page, COPY held with a white key: this pattern onto that one,
 *                the STEP page: a step held, K1-K4 its nudge, flam, chance, velocity; none held, K1 nudges the lane
 *   SEQ          tap: STEP / PLAY mode OCT- OCT+  the bar, 1-4        PLAY  start, stop     REC  live recording
 *   K1-K4        the page: pitch, filter, decay, volume of the voice unless a page is open
 *   EDIT ENV LFO the voice's sound pages: each press the next, and past the last back to the main page
 *   HOME         the main page, from anywhere
 *   a step held  the first eight black keys pick the passes it plays on, SELECT how many there are in its cycle
 *   SEQ          STEP, PLAY, NOTE. In NOTE all the keys play the voice, OCT their range; held, a black key is the voice.
 *                  REC on writes them: running live, stopped step by step, SEL going past a step. K1 step, K2 its note,
 *                  K3 velocity, K4 on or off
 *   FX           the FX pages: type, mix, two settings, then the type's others
 *   GLO          the settings page: tempo, shuffle, external MIDI clock, roll rate, the load
 *   ARP          the SONG page, where the white keys fill the rows
 *                  GLO held: PLAY starts the load test, and PLAY alone ends it; HOME zeroes PEAK and XRUN
 *   SELECT       tempo                 ALGORITHM  shuffle             PRESET  kit
 *   SAVE         every changed pattern, the kit and tempo (stopped)     SEL and EDIT held a second  clear the lane
 *                                                                       SEL and HOME held a second  the kit as in the bank
 */
#include "src/tack_engine.h"
#include "src/seq_platform.h"

static const char *const VOICE_NAME[TACK_VOICES] = {"DRUM 1", "DRUM 2", "DRUM 3", "SNARE", "CYMBAL", "CL HAT", "OP HAT"};
static const char *const P_NAME[TACK_NP] = {"PITCH", "FILTER", "DECAY", "VOLUME"};
static const char *const SEND_NAME[TACK_FX_SENDS] = {"D1", "D2", "D3", "SN", "CY", "HH"};
static const uint8_t WK[16] = PLAT_WHITE_KEYS, BK[11] = PLAT_BLACK_KEYS;
enum { KEY_ROLL = TACK_VOICES, KEY_EUCLID, KEY_COPY, KEY_STEP };   /* the black keys after the voices */

#define SET_MAGIC 0x4B544C31u            /* "KTL1" */
#define KIT_SLOT 256u
typedef struct {
    uint32_t magic;
    uint16_t bpm;
    uint8_t kit_now;                     /* the bank's kit it came from: slot + 1, 0 none */
    uint8_t rsv;
    uint8_t kit[KIT_SLOT];               /* the parameters, then the name at SET_KIT_NAME; older saves have it empty */
    uint8_t song[TACK_SONG_ROWS];      /* older saves stop here */
    uint8_t song_on, rsv2[3];
} settings_t;
#define SET_OLD_SIZE (sizeof(settings_t) - TACK_SONG_ROWS - 4u)
#define SET_KIT_NAME 248u
_Static_assert(sizeof(settings_t) <= PLAT_STORE_MAX, "one store object");
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
    uint8_t step_mode, stress, fx_page, set_page, eu_page, st_page, song_page;
    uint8_t song_row, chain_n;           /* the row the SONG page edits; the keys taken while SEQ is held */
    uint8_t note_mode, note_step, note_vel;   /* NOTE: the keys are notes; the step it writes next, how hard */
    int8_t note_oct;
    uint8_t key_note[NKEYS];             /* what each key played, for its note-off */
    uint8_t seq_voice;                   /* SEQ held picked a voice: no mode change on release */
    uint8_t bad_patterns;                /* patterns the store held that were refused */
    uint8_t roll_voice;                  /* the voice ROLL went down on */
    uint32_t clear_at, revert_at;        /* when SEL and EDIT, SEL and HOME went down together, 0 not */
    uint8_t group, sub;                  /* the sound page */
    uint32_t save_todo;                  /* patterns a SAVE still has to write, one a frame */
    uint8_t saved, save_hold;            /* how many; a backup is reading the flash */
    int kit_seen;                        /* the kit the name is of */
    uint8_t seq_used;                    /* SEQ was a modifier this press: no mode change on release */
    uint8_t glo_used;                    /* so was GLO: no page */
    uint8_t arp_used;                    /* so was ARP */
    uint8_t sel_used;                    /* so was SEL */
    int32_t held;                        /* the white key held (0-15), -1 none */
    uint8_t held_used;                   /* it did something else: no toggle on release */
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
    if (ok) {
        if (running_audio) {
            while (tack_kit_busy())
                ;
            tack_post_kit(set.kit, TACK_KIT_NO_SLOT);
        } else {
            tack_load_kit(set.kit);
        }
        tack_post_bpm(set.bpm);
        tack_set_kit_now(set.kit_now);
        ui.kit_seen = set.kit_now;
        ui.kit = set.kit_now ? set.kit_now - 1 : 0;
        memcpy(ui.kit_name, set.kit + SET_KIT_NAME, 8);
    }
}

void app_init(void)
{
    tack_init();
    seq_platform_set_midi_out(seq_midi_out);
    tack_set_kit_find(kit_params);
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

static int save_settings(void)
{
    set.magic = SET_MAGIC;
    set.bpm = tack_bpm();
    set.kit_now = (uint8_t)tack_kit_now();
    memcpy(set.kit, tack_kit(), tack_kit_size());
    memcpy(set.kit + SET_KIT_NAME, ui.kit_name, 8);
    memcpy(set.song, tack_song(), sizeof set.song);
    set.song_on = tack_song_mode() == TACK_SONG_ON;
    return plat_store_save(OBJ_SETTINGS, &set, sizeof set);
}

/* SAVE: the flash stops everything for a moment, sound too, so never while the sequencer runs. Every changed pattern, and
 * the active one so that SAVE always does something: one a frame (save_frame), as each is an erase and a write */
static void save(void)
{
    if (tack_running()) {
        app_say("STOP FIRST", "");
        return;
    }
    if (ui.save_hold || ui.save_todo)
        return;
    mark('S');
    if (save_settings() != 0) {
        app_say("SAVE", "FAILED");
        return;
    }
    ui.save_todo = tack_dirty() | 1u << tack_pattern();
    ui.saved = 0;
}

static int save_one(uint32_t i)
{
    if (plat_store_save(OBJ_PAT0 + i, pat_buf, tack_pattern_export((uint8_t)i, pat_buf)) != 0)
        return -1;
    tack_clean((uint8_t)i);
    return 0;
}

static void save_frame(void)
{
    uint32_t i;
    char t[4];
    if (!ui.save_todo)
        return;
    if (tack_running()) {              /* started from MIDI: what is left stays changed, for the next SAVE */
        ui.save_todo = 0;
        app_say("SAVE", "STOPPED");
        return;
    }
    for (i = 0; !(ui.save_todo >> i & 1u); i++)
        ;
    if (save_one(i) != 0) {
        ui.save_todo = 0;
        app_say("SAVE", "FAILED");
        return;
    }
    ui.save_todo &= ~(1u << i);
    ui.saved++;
    mark('S');
    if (!ui.save_todo) {
        int_text(ui.saved, t);
        app_say("SAVED", t);
    }
}

/* a backup or restore of the patterns and settings: fm1-base's device/backup.c */
int app_backup_playing(void) { return tack_running(); }
void app_backup_hold(int on) { ui.save_hold = (uint8_t)on; }
void app_backup_flush(void)              /* the backup holds what is saved: so save, the kit and tempo too */
{
    uint32_t i, todo = tack_dirty();
    int bad = save_settings();
    for (i = 0; i < TACK_PATTERNS; i++)
        if (todo >> i & 1u)
            bad |= save_one(i);
    if (bad)
        app_say("SAVE", "FAILED");
}
void app_backup_restored(void) { load_saved(1); }

static uint32_t pressed(uint32_t now, uint32_t b) { return (now & ~ui.btn) >> b & 1u; }

static int step_of(uint32_t k) { return ui.bar * 16 + (int)k; }

static void toggle_step(uint32_t k)
{
    const uint8_t s = (uint8_t)step_of(k);
    tack_post_step((uint8_t)ui.voice, s, (uint8_t)!tack_step_on((uint8_t)ui.voice, s));
}

static const TackSlot *slot(uint32_t i) { return tack_slot((uint32_t)ui.voice, ui.group, ui.sub, i); }

/* a step is held: what K1-K4 start from */
static void hold_step(uint32_t k)
{
    uint32_t i;
    const uint8_t s = (uint8_t)step_of(k);
    ui.held = (int32_t)k;
    ui.held_used = 0;
    for (i = 0; i < TACK_NP; i++) {
        const TackSlot *sl = slot(i);
        if (sl && !tack_lock_get((uint8_t)ui.voice, s, sl->par, &ui.held_val[i]))
            ui.held_val[i] = tack_get_param(sl->par);
    }
}

static void lock_turn(uint32_t i, int32_t e)
{
    const uint8_t s = (uint8_t)step_of((uint32_t)ui.held);
    const TackSlot *sl = slot(i);
    ui.held_used = 1;
    if (!sl)
        return;
    if (!tack_step_on((uint8_t)ui.voice, s)) {
        app_say("NO STEP", "");
    } else if (sl->target) {
        app_say("NO LOCK", "ON TARGETS");
    } else if (tack_lock_room((uint8_t)ui.voice, s, sl->par) == TACK_LOCK_STEP_FULL) {
        app_say("STEP FULL", "8 LOCKS");
    } else if (tack_lock_room((uint8_t)ui.voice, s, sl->par) == TACK_LOCK_PATTERN_FULL) {
        app_say("PATTERN", "LOCKS FULL");
    } else {
        ui.held_val[i] = tack_slot_step(sl, ui.held_val[i], e);
        tack_post_lock((uint8_t)ui.voice, s, sl->par, ui.held_val[i]);
    }
}

static void kit_load(int32_t i)
{
    const uint8_t *k = kit_slot((uint32_t)i);
    if (!k || tack_kit_busy())
        return;
    memcpy(kit_buf, k, KIT_SLOT);
    if (tack_post_kit(kit_buf + 8, (uint8_t)i))
        mark('K');
}

static void kit_step(int32_t e)
{
    int32_t i = ui.kit + (e > 0 ? 1 : -1);
    if (i >= 0 && i < (int32_t)KIT_SLOTS && kit_slot((uint32_t)i))
        kit_load(i);
    else
        app_say("KITS", kit_slot(0) ? "END" : "NONE LOADED");
}

static uint32_t fx_slot(uint32_t knob) { return ui.fx_page > 1u ? TACK_FX_C + knob : knob; }

static void fx_turn(uint32_t slot, int32_t e)
{
    const uint32_t s = fx_slot(slot);
    const uint8_t top = s < TACK_FX_ALL ? tack_fx_max(s) : 0;
    if (ui.held >= 0) {                  /* locks are for the voice page: nothing here may look like one */
        ui.held_used = 1;
        app_say("LOCKS", "VOICE PAGE");
    } else if (top) {
        const uint16_t p = tack_fx_param(s);
        mark('F');
        tack_post_param(p, (uint8_t)clamp((int32_t)tack_get_param(p) + e, 0, top));
        if (ui.fx_page > (uint8_t)tack_fx_pages())      /* a type with one page */
            ui.fx_page = 1;
    }
}

/* the settings page. The tempo and shuffle knobs stay on SELECT and ALGORITHM too */
static void set_turn(uint32_t slot, int32_t e)
{
    if (ui.held >= 0) {
        ui.held_used = 1;
        app_say("LOCKS", "VOICE PAGE");
    } else if (slot == 0u) {
        tack_post_bpm((uint16_t)clamp((int32_t)tack_bpm() + e, 30, 300));
    } else if (slot == 1u) {
        tack_post_shuffle((uint8_t)clamp((int32_t)tack_shuffle() + e, 0, 127));
    } else if (slot == 2u) {
        tack_post_ext_sync(e > 0);
    } else {
        tack_post_roll_rate((uint8_t)clamp((int32_t)tack_roll_rate() + e, 0, TACK_ROLL_RATES - 1));
    }
}

static void eu_turn(uint32_t slot, int32_t e)
{
    if (ui.held >= 0) {
        ui.held_used = 1;
        app_say("LOCKS", "VOICE PAGE");
    } else if (slot < TACK_EU_SLOTS) {
        mark('E');
        tack_euclid_turn((uint8_t)ui.voice, slot, e);
    }
}

static void st_turn(uint32_t slot, int32_t e)
{
    if (ui.st_page == 2u && slot) {
        return;
    } else if (ui.held >= 0) {
        const uint8_t s = (uint8_t)step_of((uint32_t)ui.held);
        ui.held_used = 1;
        if (tack_step_on((uint8_t)ui.voice, s))
            tack_step_turn((uint8_t)ui.voice, s, ui.st_page == 2u ? (uint32_t)TACK_ST_NOTE : slot, e);
        else
            app_say("NO STEP", "");
    } else if (slot == TACK_ST_NUDGE && ui.st_page == 1u) {
        mark('N');
        tack_lane_nudge_turn((uint8_t)ui.voice, e);
    } else {
        app_say("HOLD", "A STEP");
    }
}

/* NOTE mode's own screen: on the main page, as a sound page opened from it keeps the keys and has its knobs */
static int note_view(void)
{
    return ui.note_mode && ui.group == TACK_G_MAIN && !ui.fx_page && !ui.set_page && !ui.eu_page && !ui.st_page &&
           !ui.song_page;
}

static uint32_t key_note(uint32_t k)             /* the keys are F3 to G5 at octave 0 */
{
    return (uint32_t)clamp(53 + (int32_t)k + 12 * ui.note_oct, 0, 127);
}

static uint32_t note_cursor(void) { return ui.note_step % (uint32_t)tack_length((uint8_t)ui.voice); }

static void note_cursor_to(uint32_t s)
{
    ui.note_step = (uint8_t)s;
    ui.bar = (int32_t)(s / 16u);
}

/* The keys play the voice. REC on writes them: running, as the LXR records; stopped, step by step */
static void note_keys(uint32_t down, uint32_t up)
{
    for (uint32_t k = 0; k < NKEYS; k++) {
        if (up >> k & 1u)
            tack_post_note_off((uint8_t)ui.voice, ui.key_note[k]);
        if (!(down >> k & 1u))
            continue;
        const uint8_t note = (uint8_t)key_note(k);
        ui.key_note[k] = note;                   /* OCT may move before the key comes up */
        tack_post_note((uint8_t)ui.voice, note, ui.note_vel);
        if (tack_recording() && !tack_running()) {
            const uint32_t s = note_cursor();
            tack_post_step_note((uint8_t)ui.voice, (uint8_t)s, note, ui.note_vel);
            note_cursor_to((s + 1u) % (uint32_t)tack_length((uint8_t)ui.voice));
        }
        ui.redraw = 1;
    }
}

static void note_turn(uint32_t slot, int32_t e)
{
    const uint32_t s = note_cursor();
    const int on = tack_step_on((uint8_t)ui.voice, (uint8_t)s);
    if (slot == 0u) {
        note_cursor_to((uint32_t)clamp((int32_t)s + e, 0, tack_length((uint8_t)ui.voice) - 1));
    } else if (slot == 1u) {
        if (on) {
            tack_step_turn((uint8_t)ui.voice, (uint8_t)s, TACK_ST_NOTE, e);
            if (!tack_running())               /* to tune by ear */
                tack_post_note((uint8_t)ui.voice,
                                 (uint8_t)clamp(tack_step_get((uint8_t)ui.voice, (uint8_t)s, TACK_ST_NOTE) + e, 0, 127),
                                 ui.note_vel);
        } else {
            app_say("NO STEP", "");
        }
    } else if (slot == 2u) {
        ui.note_vel = (uint8_t)clamp((int32_t)ui.note_vel + e, 1, 127);
    } else {
        tack_post_step((uint8_t)ui.voice, (uint8_t)s, e > 0);
    }
}

/* the rows before the song's end */
static uint32_t song_len(void)
{
    uint32_t n = 0;
    while (n < TACK_SONG_ROWS - 1u && tack_song_get(n) < TACK_PATTERNS)
        n++;
    return n;
}

/* a row, a pattern or the end. A pattern on the end row moves the end along, unless the rows it cut off are behind it */
static void song_write(uint32_t row, uint8_t value)
{
    const uint32_t len = song_len();
    const uint8_t end = tack_song_get(len);
    tack_post_song_row((uint8_t)row, value);
    if (row == len && value < TACK_PATTERNS && tack_song_get(row + 1u) >= TACK_PATTERNS)
        tack_post_song_row((uint8_t)(row + 1u), end);
}

static void song_key(uint32_t k)
{
    song_write(ui.song_row, (uint8_t)k);
    if (ui.song_row < TACK_SONG_ROWS - 2u)
        ui.song_row++;
}

static void song_turn(uint32_t slot, int32_t e)
{
    const uint32_t len = song_len();
    if (ui.held >= 0) {
        ui.held_used = 1;
    } else if (slot == 0u) {
        ui.song_row = (uint8_t)clamp((int32_t)ui.song_row + e, 0, (int32_t)(len < TACK_SONG_ROWS - 2u ? len : TACK_SONG_ROWS - 2u));
    } else if (slot == 1u) {
        const uint8_t now = tack_song_get(ui.song_row);
        const int32_t v = clamp((now < TACK_PATTERNS ? (int32_t)now : (int32_t)TACK_PATTERNS) + e, 0, TACK_PATTERNS);
        song_write(ui.song_row, v == TACK_PATTERNS ? tack_song_get(len) : (uint8_t)v);
    } else if (slot == 2u) {
        tack_post_song_row((uint8_t)len, e > 0 ? TACK_SONG_LOOP : TACK_SONG_STOP);
    } else if (e > 0 && !len) {
        app_say("SONG", "EMPTY");
    } else {
        tack_post_song_on(e > 0);
    }
}

/* a step held: the first eight black keys are the slots of its recurrence, and SELECT how many there are */
static void recur_key(uint32_t k)
{
    const uint8_t s = (uint8_t)step_of((uint32_t)ui.held), v = (uint8_t)ui.voice;
    const int len = tack_recur_len(v, s);
    uint8_t mask = tack_recur_mask(v, s);
    ui.held_used = 1;
    if (!tack_step_on(v, s)) {
        app_say("NO STEP", "");
    } else if ((int)k >= len) {
        app_say("SELECT", "ADDS SLOTS");
    } else if ((mask ^= (uint8_t)(1u << k)) == 0) {
        app_say("ONE SLOT", "AT LEAST");
    } else {
        tack_post_recur(v, s, (uint8_t)len, mask);
    }
}

static void recur_turn(int32_t e)
{
    const uint8_t s = (uint8_t)step_of((uint32_t)ui.held), v = (uint8_t)ui.voice;
    const int32_t n = clamp(tack_recur_len(v, s) + e, 1, TACK_RECUR_MAX);
    uint8_t mask = (uint8_t)(tack_recur_mask(v, s) & ((1u << n) - 1u));
    ui.held_used = 1;
    if (!tack_step_on(v, s)) {
        app_say("NO STEP", "");
    } else if (!tack_recur_room(v, s)) {
        app_say("PATTERN", "LOCKS FULL");
    } else {
        tack_post_recur(v, s, (uint8_t)n, mask ? mask : 1u);
    }
}

/* two buttons held a second, for what is too much to lose to one press. 1 once when the second is up */
static int held_second(uint32_t btn, uint32_t both, uint32_t *at, const char *word)
{
    if ((btn & both) != both) {
        *at = 0;
    } else if (!*at) {
        *at = plat_ms() | 1u;
        app_say("HOLD", word);
    } else if (*at != 1u && plat_ms() - *at >= 1000u) {
        *at = 1;                         /* done: not again until they are let go */
        return 1;
    }
    return 0;
}

/* the kit as it is in the bank: every edit to it goes, saved or not */
static void kit_revert(void)
{
    const int now = tack_kit_now();
    if (now && kit_slot((uint32_t)now - 1u)) {
        kit_load(now - 1);
        app_say("KIT", "REVERTED");
    } else {
        app_say("NO KIT", "IN THE BANK");
    }
}

static void pattern_copy(uint32_t k)
{
    char t[4];
    if ((int)k == tack_pattern())
        return;
    mark('P');
    tack_post_copy((uint8_t)k);
    int_text((int32_t)k + 1, t);
    app_say("COPIED TO", t);
}

/* SEL and EDIT held a second: a lane is too much to lose to one key */
static void clear_hold(uint32_t btn)
{
    if (held_second(btn, 1u << B_SEL | 1u << B_EDIT, &ui.clear_at, "TO CLEAR")) {
        tack_post_clear((uint8_t)ui.voice);
        app_say("LANE", "CLEARED");
    }
}

static void pattern_pick(uint32_t k)
{
    char t[4];
    mark('P');
    tack_post_pattern((uint8_t)k);     /* stopped: now; running: at the end of the bar */
    int_text((int32_t)k + 1, t);
    app_say("PATTERN", t);
}

/* EDIT, ENV, LFO: this group's first page, then its next, then the main page */
static void page_press(uint8_t group)
{
    const uint8_t used = ui.held_used;
    ui.song_page = ui.fx_page = ui.set_page = ui.eu_page = ui.st_page = 0;
    if (ui.group != group) {
        ui.group = group;
        ui.sub = 0;
    } else if (++ui.sub >= tack_pages((uint32_t)ui.voice, group)) {
        ui.group = TACK_G_MAIN;
        ui.sub = 0;
    }
    if (ui.held >= 0) {                  /* a step held: the knobs start from the new page's values */
        hold_step((uint32_t)ui.held);
        ui.held_used = used;
    }
    ui.redraw = 1;
}

static void input(void)
{
    uint32_t keys = plat_keys(), btn = plat_buttons(), k;
    uint32_t down = keys & ~ui.keys, up = ui.keys & ~keys;
    int32_t e;

    if (ui.note_mode && !(btn >> B_SEQ & 1u) && !(btn >> B_ARP & 1u)) {   /* held, the keys do what they always do */
        note_keys(down, up);
        down = up = 0;
    }
    if (ui.step_mode && ui.held >= 0) {          /* a step held: the black keys are its slots */
        for (k = 0; k < TACK_RECUR_MAX; k++)
            if (down >> BK[k] & 1u) {
                recur_key(k);
                ui.redraw = 1;
            }
        for (k = 0; k < TACK_RECUR_MAX; k++)
            down &= ~(1u << BK[k]);
    }
    for (k = 0; k < TACK_VOICES; k++)          /* the black keys: voice, or with ARP held its mute */
        if (down >> BK[k] & 1u) {
            if (btn >> B_ARP & 1u) {
                ui.arp_used = 1;
                tack_post_mute((uint8_t)k, (uint8_t)!tack_muted((uint8_t)k));
            } else if (ui.fx_page) {
                mark('F');
                tack_post_fx_send(k, (uint8_t)!tack_fx_send(k));
            } else {
                ui.voice = (int32_t)k;
                ui.seq_voice |= (uint8_t)(btn >> B_SEQ & 1u);
                if (ui.sub >= tack_pages((uint32_t)ui.voice, ui.group))
                    ui.sub = 0;
            }
            ui.redraw = 1;
        }
    if (down >> BK[KEY_ROLL] & 1u) {
        ui.roll_voice = (uint8_t)ui.voice;
        tack_post_roll(ui.roll_voice, 1);
    }
    if (up >> BK[KEY_ROLL] & 1u)
        tack_post_roll(ui.roll_voice, 0);
    if (down >> BK[KEY_EUCLID] & 1u) {
        ui.eu_page = !ui.eu_page;
        ui.song_page = ui.fx_page = ui.set_page = ui.st_page = 0;
        ui.redraw = 1;
    }
    if (down >> BK[KEY_STEP] & 1u) {
        ui.st_page = ui.st_page >= 2u ? 0 : (uint8_t)(ui.st_page + 1u);
        ui.song_page = ui.fx_page = ui.set_page = ui.eu_page = 0;
        ui.redraw = 1;
    }
    for (k = 0; k < 16u; k++) {                  /* the white keys: steps, or the voices */
        if (down >> WK[k] & 1u) {
            if (btn >> B_SEQ & 1u) {             /* SEQ held: a pattern, in either mode; more of them a chain */
                if (!ui.seq_used) {
                    ui.seq_used = 1;
                    ui.chain_n = 1;
                    pattern_pick(k);
                } else if (ui.chain_n < TACK_CHAIN_MAX) {
                    char t[4];
                    tack_post_chain(ui.chain_n++, (uint8_t)k);
                    int_text(ui.chain_n, t);
                    app_say("CHAIN", t);
                }
            } else if (keys >> BK[KEY_COPY] & 1u) {
                pattern_copy(k);
            } else if (ui.song_page) {
                song_key(k);
            } else if (!ui.step_mode) {
                if (k < TACK_VOICES) {
                    ui.voice = (int32_t)k;
                    if (ui.sub >= tack_pages((uint32_t)ui.voice, ui.group))
                        ui.sub = 0;
                    tack_post_note((uint8_t)k, 60, 110);
                }
            } else if (btn >> B_SEL & 1u) {      /* SEL held: the lane ends here */
                tack_post_length((uint8_t)ui.voice, (uint8_t)(step_of(k) + 1));
                app_say("LENGTH", "");
            } else if (ui.held >= 0) {           /* a second key: a step at once */
                toggle_step(k);
            } else {
                hold_step(k);
            }
            ui.redraw = 1;
        }
        if ((up >> WK[k] & 1u) && !ui.step_mode && k < TACK_VOICES)
            tack_post_note_off((uint8_t)k, 60);
        if ((up >> WK[k] & 1u) && ui.held == (int32_t)k) {
            if (ui.step_mode && !ui.held_used)
                toggle_step(k);
            ui.held = -1;
            ui.redraw = 1;
        }
    }
    ui.keys = keys;

    if (pressed(btn, B_SEQ)) {
        ui.seq_used = ui.seq_voice = 0;
        ui.redraw = 1;
    }
    if (((ui.btn & ~btn) >> B_SEQ & 1u) && !ui.seq_used && !ui.seq_voice) {   /* STEP, PLAY, NOTE */
        ui.note_mode = !ui.step_mode && !ui.note_mode;
        ui.step_mode = !ui.step_mode && !ui.note_mode;
        ui.held = -1;
        ui.redraw = 1;
    }
    if (pressed(btn, B_FX)) {
        ui.fx_page = ui.fx_page >= (uint8_t)tack_fx_pages() ? 0 : (uint8_t)(ui.fx_page + 1u);
        ui.song_page = ui.set_page = ui.eu_page = ui.st_page = 0;
        ui.redraw = 1;
    }
    if (pressed(btn, B_GLO))
        ui.glo_used = 0;
    if (((ui.btn & ~btn) >> B_GLO & 1u) && !ui.glo_used) {
        ui.set_page = !ui.set_page;
        ui.song_page = ui.fx_page = ui.eu_page = ui.st_page = 0;
        ui.redraw = 1;
    }
    if (pressed(btn, B_ARP))
        ui.arp_used = 0;
    if (((ui.btn & ~btn) >> B_ARP & 1u) && !ui.arp_used) {
        ui.song_page = !ui.song_page;
        ui.fx_page = ui.set_page = ui.eu_page = ui.st_page = 0;
        ui.redraw = 1;
    }
    if (pressed(btn, B_SEL))
        ui.sel_used = 0;
    if ((btn >> B_SEL & 1u) && ((btn & ~(1u << B_SEL)) || keys))
        ui.sel_used = 1;
    if (((ui.btn & ~btn) >> B_SEL & 1u) && !ui.sel_used && note_view() && !tack_running()) {   /* past a step, leaving it */
        note_cursor_to((note_cursor() + 1u) % (uint32_t)tack_length((uint8_t)ui.voice));
        ui.redraw = 1;
    }
    if (ui.note_mode) {                          /* the range of the keys; the knob walks the bars */
        ui.note_oct = (int8_t)clamp((int32_t)ui.note_oct + (int32_t)pressed(btn, B_OCTUP) - (int32_t)pressed(btn, B_OCTDN), -3, 3);
        if (pressed(btn, B_OCTUP) || pressed(btn, B_OCTDN))
            ui.redraw = 1;
    } else if (pressed(btn, B_OCTDN) && ui.bar > 0) {
        ui.bar--;
        ui.redraw = 1;
    } else if (pressed(btn, B_OCTUP) && ui.bar < (int32_t)TACK_BARS - 1) {
        ui.bar++;
        ui.redraw = 1;
    }
    if (pressed(btn, B_PLAY) && !ui.save_todo) {
        if ((btn >> B_GLO & 1u) || ui.stress) {  /* the load test: not a thing to find by accident */
            ui.glo_used = 1;
            ui.stress = !ui.stress;
            ui.cpu_peak = 0;
            ui.xrun_base = plat_xruns();
            ui.xrun_act = 0;
            tack_post_stress(ui.stress);
            app_say("LOAD TEST", ui.stress ? "ON" : "OFF");
        } else if (!tack_ext_sync()) {
            tack_post_run((uint8_t)!tack_running());
            ui.redraw = 1;
        }
    }
    if (pressed(btn, B_REC)) {
        tack_post_record((uint8_t)!tack_recording());
        ui.redraw = 1;
    }
    if (pressed(btn, B_ENV))
        page_press(TACK_G_ENV);
    if (pressed(btn, B_LFO))
        page_press(TACK_G_LFO);
    clear_hold(btn);
    if (held_second(btn, 1u << B_SEL | 1u << B_HOME, &ui.revert_at, "TO REVERT"))
        kit_revert();
    if (pressed(btn, B_EDIT) && ui.held < 0 && !(btn >> B_SEL & 1u))
        page_press(TACK_G_EDIT);
    if (pressed(btn, B_EDIT) && ui.held >= 0) {  /* a step held: its locks go */
        tack_post_lock_clear((uint8_t)ui.voice, (uint8_t)step_of((uint32_t)ui.held));
        ui.held_used = 1;
        hold_step((uint32_t)ui.held);
        ui.held_used = 1;
        app_say("LOCKS", "CLEARED");
    }
    if (pressed(btn, B_SAVE))
        save();
    if (pressed(btn, B_HOME) && (btn >> B_GLO & 1u)) {
        ui.glo_used = 1;
        ui.cpu_peak = 0;
        ui.xrun_base = plat_xruns();
        ui.xrun_act = 0;
        app_say("LOAD", "ZEROED");
    } else if (pressed(btn, B_HOME) && !(btn >> B_SEL & 1u)) {
        ui.song_page = ui.fx_page = ui.set_page = ui.eu_page = ui.st_page = 0;
        ui.group = TACK_G_MAIN;
        ui.sub = 0;
        if (ui.held >= 0)
            hold_step((uint32_t)ui.held);
        ui.redraw = 1;
    }
    ui.btn = btn;

    for (k = 0; k < TACK_NP; k++)
        if ((e = plat_enc(EN_K1 + (int)k)) != 0) {
            if (ui.set_page) {
                set_turn(k, e);
            } else if (ui.song_page) {
                song_turn(k, e);
            } else if (note_view()) {
                note_turn(k, e);
            } else if (ui.eu_page) {
                eu_turn(k, e);
            } else if (ui.st_page) {
                st_turn(k, e);
            } else if (ui.fx_page) {
                fx_turn(k, e);
            } else if (ui.held >= 0) {
                lock_turn(k, e);
            } else {
                const TackSlot *sl = slot(k);
                if (sl)
                    tack_post_param(sl->par, tack_slot_step(sl, tack_get_param(sl->par), e));
            }
            ui.redraw = 1;
        }
    if ((e = plat_enc(EN_SELECT)) != 0) {
        if (ui.step_mode && ui.held >= 0)
            recur_turn(e);
        else
            tack_post_bpm((uint16_t)clamp((int32_t)tack_bpm() + e, 30, 300));
        ui.redraw = 1;
    }
    if ((e = plat_enc(EN_ALGO)) != 0) {
        char t[4];
        const uint8_t v = (uint8_t)clamp((int32_t)tack_shuffle() + e, 0, 127);
        tack_post_shuffle(v);
        int_text(v, t);
        app_say("SHUFFLE", t);
    }
    if ((e = plat_enc(EN_PRESET)) != 0) {
        kit_step(e);
        ui.redraw = 1;
    }
}

/* kits over USB: F0 7D 'K' 'T' cmd ... F7. 1 erases the bank; 2 slot hex..., one slot as hex, in the main loop. Both
 * refuse while the sequencer runs, because a flash write stops the sound */
static int hex_val(uint8_t c) { return c <= '9' ? c - '0' : c - 'A' + 10; }

static void kit_reply(uint8_t cmd, uint8_t status)
{
    const uint8_t f[] = {0xF0, 0x7D, 'K', 'T', 0x7F, cmd, status, 0xF7};
    plat_sysex_send(f, sizeof f);
}

int app_sysex(const uint8_t *p, uint32_t n)
{
    uint32_t i;
    if (n < 4u || p[0] != 0x7Du || p[1] != 'K' || p[2] != 'T')
        return 0;
    mark('U');
    if (p[3] == 3u && n == 6u && !tack_running()) {     /* 3 slot part: that much of a slot, as hex */
        static const char HEX[] = "0123456789ABCDEF";
        const uint8_t *k = p[4] < KIT_SLOTS ? kit_slot(p[4]) : 0;
        uint8_t f[9 + 2u * KIT_PART + 1u] = {0xF0, 0x7D, 'K', 'T', 0x7F, 3, k ? 0 : 4, p[4], p[5]};
        uint32_t len = 9;
        if (k && p[5] < KIT_SLOT / KIT_PART)
            for (i = 0; i < KIT_PART; i++) {
                const uint8_t b = k[p[5] * KIT_PART + i];
                f[len++] = (uint8_t)HEX[b >> 4];
                f[len++] = (uint8_t)HEX[b & 15u];
            }
        f[len++] = 0xF7;
        plat_sysex_send(f, len);
        return 1;
    }
    if (tack_running()) {
        kit_reply(p[3], 1);
    } else if (p[3] == 1u) {
        int bad = 0;
        for (i = 0; i < KIT_SECTORS; i++)
            bad |= plat_data_erase(KIT_BASE + i * 0x1000u);
        kit_reply(1, bad ? 2 : 0);
    } else if (p[3] == 2u && n == 5u + 2u * KIT_SLOT && p[4] < KIT_SLOTS) {
        for (i = 0; i < KIT_SLOT; i++)
            kit_buf[i] = (uint8_t)(hex_val(p[5 + 2 * i]) << 4 | hex_val(p[6 + 2 * i]));
        kit_reply(2, plat_data_write(KIT_BASE + p[4] * KIT_SLOT, kit_buf, KIT_SLOT) ? 2 : 0);
    } else {
        kit_reply(p[3], 3);
    }
    return 1;
}

#define FLASH_MS 50u

/* A voice's key flashes as it fires: the others to lit, the selected one, lit already, to dark. Which of its keys */
static uint32_t flashing(uint32_t v, uint32_t now)
{
    static uint8_t seen[TACK_VOICES];
    static uint32_t at[TACK_VOICES];
    const uint8_t n = tack_voice_fires((uint8_t)v);
    if (n != seen[v]) {
        seen[v] = n;
        at[v] = now;
    }
    return at[v] && now - at[v] < FLASH_MS;
}

/* the keys light the page's steps of the voice, the playhead dim-to-bright against them. The black keys: the voice lit, a
 * voice with steps in this pattern between that and the low glow of an empty lane, a muted one dark */
static void lights(void)
{
    uint32_t keys = 0, dim = 0, btn = 0, k;
    const int head = tack_running() ? tack_playhead((uint8_t)ui.voice) : -1;
    const uint32_t now = plat_ms();
    const int as_notes = ui.note_mode && !(ui.btn >> B_SEQ & 1u) && !(ui.btn >> B_ARP & 1u);
    const int as_slots = ui.step_mode && ui.held >= 0;
    uint32_t mid = 0;
    if (as_slots) {                              /* lit: it plays on that slot. Mid: a slot it skips */
        const uint8_t s = (uint8_t)step_of((uint32_t)ui.held);
        const uint32_t len = (uint32_t)tack_recur_len((uint8_t)ui.voice, s), mask = tack_recur_mask((uint8_t)ui.voice, s);
        for (k = 0; k < len; k++) {
            if (mask >> k & 1u)
                keys |= 1u << BK[k];
            else
                mid |= 1u << BK[k];
        }
    }
    for (k = 0; k < TACK_VOICES; k++) {
        const uint32_t fire = flashing(k, now);
        if (as_notes || as_slots) {
            continue;
        } else if (ui.fx_page) {
            if (tack_fx_send(k))
                keys |= 1u << BK[k];
        } else if ((int32_t)k == ui.voice) {
            if (!fire)
                keys |= 1u << BK[k];
        } else if (fire) {
            keys |= 1u << BK[k];
        } else if (!tack_muted((uint8_t)k)) {
            if (tack_lane_used((uint8_t)k))
                mid |= 1u << BK[k];
            else
                dim |= 1u << BK[k];
        }
    }
    if (as_notes) {                              /* the step's note on the keys: the one playing, or the one to write */
        const uint32_t s = head >= 0 ? (uint32_t)head : note_cursor();
        dim = (1u << NKEYS) - 1u;
        if (tack_step_on((uint8_t)ui.voice, (uint8_t)s))
            for (k = 0; k < NKEYS; k++)
                if ((int)key_note(k) == tack_step_get((uint8_t)ui.voice, (uint8_t)s, TACK_ST_NOTE))
                    keys |= 1u << k;
    } else if ((ui.btn >> B_SEQ & 1u) || (ui.keys >> BK[KEY_COPY] & 1u) || ui.song_page) {   /* the keys are the patterns */
        for (k = 0; k < 16u; k++) {
            if ((int)k == tack_pattern())
                keys |= 1u << WK[k];
            else
                dim |= 1u << WK[k];
        }
    } else if (ui.step_mode) {
        for (k = 0; k < 16u; k++) {
            const uint32_t s = (uint32_t)ui.bar * 16u + k;
            if (tack_step_on((uint8_t)ui.voice, (uint8_t)s) != ((int)s == head))
                keys |= 1u << WK[k];
        }
    } else {
        for (k = 0; k < TACK_VOICES; k++)
            dim |= 1u << WK[k];
    }
    if (ui.step_mode || ui.note_mode)
        btn |= 1u << B_SEQ;
    if (ui.fx_page)
        btn |= 1u << B_FX;
    else if (ui.group == TACK_G_EDIT)
        btn |= 1u << B_EDIT;
    else if (ui.group == TACK_G_ENV)
        btn |= 1u << B_ENV;
    else if (ui.group == TACK_G_LFO)
        btn |= 1u << B_LFO;
    if (tack_recording())
        btn |= 1u << B_REC;
    if (tack_ext_sync() || ui.set_page)
        btn |= 1u << B_GLO;
    if (ui.song_page)
        btn |= 1u << B_ARP;
    if (tack_running())
        btn |= 1u << B_PLAY | LED_PLAY_GREEN;
    plat_lights(btn, keys, 0, dim, 1);
    plat_lights_mid((1u << NB) - 1u, mid);               /* the unlit buttons: the glow is too low to read a label by */
}

void app_input_only(void) { input(); }

#define GRID_X 12
#define GRID_CELL 14
#define GRID_ROW 11
#define GRID_H 82                         /* to the knobs: it clears what a full page left there */

/* every lane on one page: a row each, the selected lane marked, the page's steps. Past a lane's end there is nothing */
static void draw_grid(void)
{
    uint32_t v, i;
    cv_begin(240, GRID_H, C_BLACK);
    for (v = 0; v < TACK_VOICES; v++) {
        const int32_t y = (int32_t)v * GRID_ROW;
        const int muted = tack_muted((uint8_t)v);
        const int head = tack_running() ? tack_playhead((uint8_t)v) : -1;
        const int len = tack_length((uint8_t)v);
        if ((int32_t)v == ui.voice)
            cv_rect(2, y + 1, 7, GRID_ROW - 3, C_HI);
        for (i = 0; i < 16u; i++) {
            const int s = ui.bar * 16 + (int)i;
            const int32_t x = GRID_X + (int32_t)i * GRID_CELL;
            if (s >= len)
                continue;
            const int on = tack_step_on((uint8_t)v, (uint8_t)s);
            cv_rect(x, y, GRID_CELL - 2, GRID_ROW - 2, s == head ? C_WHITE : on ? (muted ? C_GRAY : C_HI) : C_LINE);
            if (on && tack_locked((uint8_t)v, (uint8_t)s))
                cv_rect(x + 8, y + 1, 3, 3, C_BLACK);
            if (on && tack_recur_len((uint8_t)v, (uint8_t)s) > 1)
                cv_rect(x + 1, y + 5, 3, 3, C_BLACK);
        }
    }
    cv_blit(0, 52);
}

/* The screen under the grid: the knobs' strip, then the bottom. There the envelope or wave of the page, and a last row with
 * the page's name on the left and its place among the group's pages on the right. The curves are the engine's: SlopeEg2.c's
 * slope and rates */
/* A page of sound or settings has the screen below the header, as the keys show the steps. The pages that edit steps keep
 * the grid */
static int tall(void)
{
    return !ui.eu_page && !ui.st_page &&
           (ui.fx_page || ui.set_page || ui.song_page || ui.group != TACK_G_MAIN);
}

#define KNOBS_Y (tall() ? 52 : 134)
#define KNOBS_H (tall() ? 52 : 36)
#define BOTTOM_Y (KNOBS_Y + KNOBS_H)
#define BOTTOM_H (240 - BOTTOM_Y)
#define GRAPH_X 10
#define GRAPH_TOP 2
#define GRAPH_H (tall() ? 112 : 50)
#define GRAPH_W 220
#define ROW_Y (tall() ? 118 : 54)        /* the name */
#define BAR_W 14
#define BAR_PITCH 18

static void note_text(int n, char *b)            /* MIDI 60 is C4 */
{
    static const char NAME[12][3] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    const char *p = NAME[n % 12];
    while (*p)
        *b++ = *p++;
    int_text(n / 12 - 1, b);
}

/* a knob's value under its label, a number or a word */
static void knob_num(int32_t x, const char *t, uint16_t colour)
{
    if (tall())
        cv_text(x, 18, &FONT_M, t, colour);
    else
        cv_text(x, 16, &FONT_S, t, colour);
}

static void knob_word(int32_t x, const char *t, uint16_t colour)
{
    if (tall())
        cv_text(x, 22, &FONT_S, t, colour);
    else
        cv_text(x, 16, &FONT_XS, t, colour);
}

static float eg_shape(float v, float slope)
{
    return (1.0f + slope) * v / (1.0f + slope * v);
}

static float eg_slope(uint8_t param)
{
    const float a = ((float)param / 127.0f - 0.5f) * 1.999f;
    return 2.0f * a / (1.0f - a);
}

/* ticks a stage lasts: slopeEg2_calcTime, whose amount is k. Value 127 has a rate of 0, which never ends: held to a long one */
static float eg_time(uint8_t param, float k)
{
    const float x = (float)param / 127.0f;
    const float r = 1.0f - (1.0f + k) * x / (1.0f + k * x);
    return 1.0f / (r < 1e-5f ? 1e-5f : r);
}

static int32_t log2_px(float t)          /* a width that grows with the log of the time */
{
    int32_t n = 0;
    while (t >= 2.0f) {
        t *= 0.5f;
        n++;
    }
    n = 12 + (int32_t)(12.0f * ((float)n + t - 1.0f));
    return n > GRAPH_W ? GRAPH_W : n;
}

static void graph_column(int32_t x, float y)    /* y 0-1 */
{
    const int32_t h = 1 + (int32_t)(y * (float)(GRAPH_H - 1));
    cv_rect(GRAPH_X + x, GRAPH_TOP + GRAPH_H - h, 1, h, C_LINE);
    cv_rect(GRAPH_X + x, GRAPH_TOP + GRAPH_H - h, 1, 2, C_HI);
}

static uint8_t graph_value(int graph, uint32_t slot, uint8_t none)
{
    const TackSlot *s = tack_graph_slot((uint32_t)ui.voice, graph, slot);
    return s ? tack_get_param(s->par) : none;
}

static void draw_env(int graph)
{
    const int amp = graph == TACK_GRAPH_AMP;
    const uint8_t attack = amp ? graph_value(graph, 0, 0) : 0;
    const uint8_t decay = graph_value(graph, amp ? 1 : 0, 0), slope = graph_value(graph, amp ? 2 : 1, 63);
    const float ta = amp && attack ? eg_time(attack, 198.0f) : 0.0f, td = eg_time(decay, 1998.0f);
    const float s = eg_slope(slope), inv = eg_slope((uint8_t)(127u - slope));
    const int32_t w = log2_px(ta + td);
    int32_t up = (int32_t)((float)w * ta / (ta + td)), x;
    if (attack && up < 2)
        up = 2;
    for (x = 0; x < w; x++) {
        const float y = x < up ? eg_shape((float)x / (float)up, inv)
                               : eg_shape(1.0f - (float)(x - up) / (float)(w - up), s);
        graph_column(x, y);
    }
}

static float lfo_shape(uint32_t wave, float p)   /* -1 to 1, as lfo.c's waves in the menu's order */
{
    static const int8_t RND[8] = {3, -7, 5, -2, 8, -5, 1, -9};
    switch (wave) {
    case 0: return (p < 0.5f ? 1.0f : -1.0f) * 16.0f * (p < 0.5f ? p * (0.5f - p) : (p - 0.5f) * (1.0f - p));
    case 1: return p < 0.5f ? 4.0f * p - 1.0f : 3.0f - 4.0f * p;
    case 2: return 2.0f * p - 1.0f;
    case 3: return 1.0f - 2.0f * p;
    case 4: return p < 0.5f ? 1.0f : -1.0f;
    case 5: return (float)RND[(uint32_t)(p * 8.0f) & 7u] / 10.0f;
    case 6: return 2.0f * p * p * p - 1.0f;
    default: return 2.0f * (1.0f - p) * (1.0f - p) * (1.0f - p) - 1.0f;
    }
}

static void draw_lfo(void)
{
    const float amount = (float)graph_value(TACK_GRAPH_LFO, 1, 0) / 127.0f;
    const uint32_t wave = graph_value(TACK_GRAPH_LFO, 3, 0);
    int32_t x;
    for (x = 0; x < GRAPH_W; x++) {
        const float p = (float)(x % 110) / 110.0f;
        graph_column(x, 0.5f + 0.5f * amount * lfo_shape(wave, p));
    }
}

/* the oscillator waves, in the menu's order: 0 SIN, 1 TRI, 2 SAW, 3 REC, 4 NOI, 5 PWM. Pulse width offsets a second saw
 * by up to half a cycle (Oscillator.c), so it is the pulse's share of the cycle. Noise is a picture of noise, not the voice's */
static float osc_shape(uint32_t wave, float p, float pwm, int32_t x)
{
    static uint32_t rnd;
    switch (wave) {
    case 0: return lfo_shape(0, p);
    case 1: return lfo_shape(1, p);
    case 2: return lfo_shape(2, p);
    case 3: return lfo_shape(4, p);
    case 4:
        rnd = rnd * 1664525u + 1013904223u + (uint32_t)x;
        return (float)(int32_t)(rnd >> 24) / 127.5f - 1.0f;
    default: return p < pwm * 0.5f ? 1.0f : -1.0f;
    }
}

/* two cycles of a wave, or one each of the two mod oscillators */
static void draw_osc(int graph)
{
    const uint32_t a = graph_value(graph, graph == TACK_GRAPH_OSC ? 1 : graph == TACK_GRAPH_FM ? 2 : 0, 0);
    const uint32_t b = graph == TACK_GRAPH_MODWAVE ? graph_value(graph, 1, 0) : a;
    const float pwm = graph == TACK_GRAPH_OSC ? (float)graph_value(graph, 2, 127) / 127.0f : 1.0f;
    int32_t x;
    for (x = 0; x < GRAPH_W; x++)
        graph_column(x, 0.5f + 0.5f * osc_shape(x < 110 ? a : b, (float)(x % 110) / 110.0f, pwm, x));
}

static void draw_load(void)
{
    char t[16];
    cv_text(10, 4, &FONT_XS, "CPU", C_GRAY);
    int_text((int32_t)plat_cpu_pct(), t);
    cv_text(36, 4, &FONT_XS, t, C_HI);
    cv_text(68, 4, &FONT_XS, "PEAK", C_GRAY);
    int_text((int32_t)ui.cpu_peak, t);
    cv_text(102, 4, &FONT_XS, t, C_HI);
    cv_text(140, 4, &FONT_XS, "XRUN", C_GRAY);
    int_text((int32_t)(plat_xruns() - ui.xrun_base), t);
    cv_text(174, 4, &FONT_XS, t, C_HI);
    if (ui.xrun_act) {
        t[0] = ui.xrun_act;
        t[1] = 0;
        cv_text(204, 4, &FONT_XS, t, C_HI);
    }
}

static const char *page_name(void)
{
    if (ui.fx_page)
        return ui.fx_page > 1u ? "FX 2" : "FX";
    if (ui.set_page)
        return "SETTINGS";
    if (ui.song_page)
        return "SONG";
    if (note_view())
        return "NOTE";
    if (ui.eu_page)
        return "EUCLID";
    if (ui.st_page)
        return ui.held >= 0 ? "STEP" : "LANE";
    if (ui.held >= 0)
        return "STEP LOCKS";
    return tack_page_title((uint32_t)ui.voice, ui.group, ui.sub);
}

/* a modulation target's name is too long for its column: it goes on the last row. The slot, or 0 */
static const TackSlot *target_slot(void)
{
    uint32_t i;
    if (ui.fx_page || ui.set_page || ui.eu_page || ui.st_page || ui.song_page)
        return 0;
    for (i = 0; i < TACK_NP; i++)
        if (slot(i) && slot(i)->target)
            return slot(i);
    return 0;
}

/* the rows around the one being edited, laid out as the white keys are; a line under the row playing */
#define SONG_CELLS 16
static uint32_t song_first(void) { return ui.song_row / SONG_CELLS * SONG_CELLS; }

static void draw_song_rows(void)
{
    const uint32_t first = song_first(), len = song_len();
    const int playing = tack_running() && tack_song_mode() == TACK_SONG_ON ? tack_song_pos() : -1;
    for (uint32_t i = 0; i < SONG_CELLS && first + i <= len; i++) {
        const uint32_t row = first + i;
        const uint8_t v = tack_song_get(row);
        const int32_t x = 10 + (int32_t)(i % 8u) * 28, y = 2 + (int32_t)(i / 8u) * 24;
        char t[4];
        int_text(v + 1, t);
        cv_text(x, y, &FONT_S, v < TACK_PATTERNS ? t : v == TACK_SONG_LOOP ? "LOOP" : "STOP",
                row == ui.song_row ? C_HI : v < TACK_PATTERNS ? C_WHITE : C_GRAY);
        if ((int)row == playing)
            cv_rect(x, y + 18, 20, 2, C_HI);
    }
}

/* drawn only when it would change: it is the largest strip on a screen that redraws on every step */
static void draw_bottom(void)
{
    static uint32_t last;
    const int overlay = ui.fx_page || ui.set_page || ui.eu_page || ui.st_page || ui.song_page;
    const int graph = overlay ? TACK_GRAPH_NONE : tack_page_graph((uint32_t)ui.voice, ui.group, ui.sub);
    const int pages = ui.fx_page ? tack_fx_pages() : ui.st_page ? 2 : overlay || !ui.group ? 0 : tack_pages((uint32_t)ui.voice, ui.group);
    const int bar = ui.fx_page ? ui.fx_page - 1 : ui.st_page ? ui.st_page - 1 : ui.sub;
    const int echo = ui.fx_page ? tack_fx_delay_ms() : 0;
    const TackSlot *target = target_slot();
    const uint32_t xruns = plat_xruns() - ui.xrun_base;
    uint32_t key = 2166136261u;
    int32_t i;

#define KEY(v) (key = (key ^ (uint32_t)(v)) * 16777619u)
    KEY(ui.voice);
    KEY(ui.fx_page);
    KEY(ui.set_page);
    KEY(ui.eu_page);
    KEY(ui.st_page);
    KEY(ui.song_page);
    KEY(note_view());
    KEY(ui.note_oct);
    KEY(tack_recording());
    KEY(ui.held >= 0);
    if (ui.held >= 0) {
        const uint8_t s = (uint8_t)step_of((uint32_t)ui.held);
        KEY(tack_recur_len((uint8_t)ui.voice, s));
        KEY(tack_recur_mask((uint8_t)ui.voice, s));
    }
    KEY(graph);
    KEY(pages);
    KEY(ui.group);
    KEY(ui.sub);
    KEY(echo);
    KEY(xruns);
    KEY(ui.xrun_act);
    if (target)
        KEY(tack_get_param(target->par));
    if (graph == TACK_GRAPH_LFO) {
        KEY(graph_value(graph, 1, 0));
        KEY(graph_value(graph, 3, 0));
    } else if (graph) {
        for (i = 0; i < 3; i++)
            KEY(graph_value(graph, (uint32_t)i, 0));
    }
    if (ui.fx_page)
        for (i = 0; i < TACK_FX_SENDS; i++)
            KEY(tack_fx_send((uint32_t)i));
    if (ui.set_page) {
        KEY(plat_cpu_pct());
        KEY(ui.cpu_peak);
    }
    if (ui.song_page) {
        KEY(ui.song_row);
        KEY(tack_song_mode());
        KEY(tack_song_pos());
        KEY(tack_running());
        for (i = 0; i < SONG_CELLS; i++)
            KEY(tack_song_get(song_first() + (uint32_t)i));
    }
#undef KEY
    if (key == last)
        return;
    last = key;

    cv_begin(240, BOTTOM_H, C_BLACK);
    if (ui.set_page) {
        draw_load();
    } else if (ui.fx_page) {
        cv_text(10, 4, &FONT_XS, "SEND", C_GRAY);
        for (i = 0; i < TACK_FX_SENDS; i++)
            cv_text(52 + i * 30, 3, &FONT_S, SEND_NAME[i], tack_fx_send((uint32_t)i) ? C_HI : C_GRAY);
        if (echo) {
            char t[16];
            cv_text(10, 26, &FONT_XS, "ECHO", C_GRAY);
            int_text(echo, t);
            cv_text(52, 22, &FONT_S, t, C_WHITE);
            cv_text(92, 26, &FONT_XS, "MS", C_GRAY);
        }
    } else if (ui.song_page) {
        draw_song_rows();
    } else if (note_view()) {
        char t[8];
        cv_text(10, 4, &FONT_XS, "OCT", C_GRAY);
        int_text(ui.note_oct, t);
        cv_text(40, 3, &FONT_S, t, C_WHITE);
        cv_text(10, 26, &FONT_XS, tack_recording() ? (tack_running() ? "RECORDING" : "KEYS WRITE THE STEPS") : "REC TO WRITE",
                tack_recording() ? C_HI : C_LINE);
    } else if (ui.eu_page) {
        cv_text(10, 4, &FONT_XS, "A TURN REWRITES THE LANE", C_LINE);
    } else if (ui.st_page && ui.held < 0) {
        cv_text(10, 4, &FONT_XS, "HOLD A STEP FOR ONE HIT", C_GRAY);
    } else if (graph == TACK_GRAPH_LFO) {
        draw_lfo();
    } else if (graph >= TACK_GRAPH_OSC) {
        draw_osc(graph);
    } else if (graph) {
        draw_env(graph);
    }
    cv_text(10, ROW_Y, &FONT_XS, page_name(), C_GRAY);
    if (target)
        cv_text(84, ROW_Y, &FONT_XS, tack_slot_text(target, tack_get_param(target->par)), C_HI);
    if (ui.step_mode && ui.held >= 0) {          /* the step's recurrence, where the keys show it */
        const uint8_t s = (uint8_t)step_of((uint32_t)ui.held);
        const int len = tack_recur_len((uint8_t)ui.voice, s), mask = tack_recur_mask((uint8_t)ui.voice, s);
        for (i = 0; i < len; i++)
            cv_rect(230 - (8 - i) * BAR_PITCH + (BAR_PITCH - BAR_W), ROW_Y + 3, BAR_W, 9, mask >> i & 1 ? C_HI : C_LINE);
    } else {
        for (i = 0; i < pages; i++)
            cv_rect(230 - (pages - i) * BAR_PITCH + (BAR_PITCH - BAR_W), ROW_Y + 5, BAR_W, 5, i == bar ? C_HI : C_LINE);
    }
    if (xruns && !ui.set_page) {                 /* never hidden: a soak test reads this from any page */
        char t[8];
        t[0] = 'X';
        int_text((int32_t)xruns, t + 1);
        cv_text(206, 2, &FONT_XS, t, C_HI);
    }
    cv_blit(0, BOTTOM_Y);
}

/* the strip the knobs edit, on the FX page, and which voices go through the FX */
static void draw_fx(void)
{
    char t[8];
    uint32_t i;
    cv_begin(240, KNOBS_H, C_BLACK);
    for (i = 0; i < TACK_FX_SLOTS; i++) {
        const uint32_t sl = fx_slot(i);
        const int32_t x = 10 + (int32_t)i * 56;
        if (sl >= TACK_FX_ALL || !(tack_fx_max(sl) || sl == TACK_FX_TYPE))
            continue;
        cv_text(x, 2, &FONT_XS, tack_fx_label(sl), C_GRAY);
        if (sl == TACK_FX_TYPE) {
            knob_word(x, tack_fx_type_name(), C_HI);
        } else if (tack_fx_text(sl, tack_get_param(tack_fx_param(sl)))) {
            knob_word(x, tack_fx_text(sl, tack_get_param(tack_fx_param(sl))), C_WHITE);
        } else {
            int_text(tack_get_param(tack_fx_param(sl)), t);
            knob_num(x, t, C_WHITE);
        }
    }
    cv_blit(0, KNOBS_Y);
}

/* the voice's bar as a piano roll in the grid's place: three octaves from the lowest note in it, the step to write marked */
static void draw_roll(void)
{
    const uint8_t v = (uint8_t)ui.voice;
    const int head = tack_running() ? tack_playhead(v) : -1, len = tack_length(v);
    const int cursor = (int)note_cursor();
    int low = 127;
    uint32_t i;
    for (i = 0; i < 16u; i++) {
        const uint8_t s = (uint8_t)(ui.bar * 16 + (int)i);
        if (tack_step_on(v, s) && tack_step_get(v, s, TACK_ST_NOTE) < low)
            low = tack_step_get(v, s, TACK_ST_NOTE);
    }
    low = low == 127 ? 48 : low / 12 * 12;
    cv_begin(240, GRID_H, C_BLACK);
    for (i = 0; i < 16u; i++) {
        const int s = ui.bar * 16 + (int)i;
        const int32_t x = GRID_X + (int32_t)i * GRID_CELL;
        if (s >= len)
            continue;
        cv_rect(x, 0, GRID_CELL - 2, 76, s == cursor ? C_GRAY : C_LINE);
        cv_rect(x + 1, 1, GRID_CELL - 4, 74, C_BLACK);
        if (tack_step_on(v, (uint8_t)s)) {
            int y = 74 - (tack_step_get(v, (uint8_t)s, TACK_ST_NOTE) - low) * 2;
            y = y < 1 ? 1 : y;
            cv_rect(x + 1, y - 3 < 1 ? 1 : y - 3, GRID_CELL - 4, 4, s == head ? C_WHITE : C_HI);
        }
        if (s == head)
            cv_rect(x, 76, GRID_CELL - 2, 2, C_WHITE);
    }
    cv_blit(0, 52);
}

/* the strip of NOTE mode: the step it would write, what it holds, how hard the keys are */
static void draw_note(void)
{
    static const char *const LABEL[TACK_NP] = {"STEP", "NOTE", "VELOCITY", "HIT"};
    const uint8_t s = (uint8_t)note_cursor();
    const int on = tack_step_on((uint8_t)ui.voice, s);
    char t[8];
    uint32_t i;
    cv_begin(240, KNOBS_H, C_BLACK);
    for (i = 0; i < TACK_NP; i++)
        cv_text(10 + (int32_t)i * 56, 2, &FONT_XS, LABEL[i], C_GRAY);
    int_text(s + 1, t);
    knob_num(10, t, C_WHITE);
    if (on) {
        note_text(tack_step_get((uint8_t)ui.voice, s, TACK_ST_NOTE), t);
        knob_num(66, t, C_HI);
    } else {
        knob_word(66, "-", C_GRAY);
    }
    int_text(ui.note_vel, t);
    knob_num(122, t, C_WHITE);
    knob_word(178, on ? "ON" : "OFF", on ? C_HI : C_WHITE);
    cv_blit(0, KNOBS_Y);
}

static void draw_song(void)
{
    static const char *const LABEL[TACK_NP] = {"ROW", "PATTERN", "AT END", "SONG"};
    const uint32_t len = song_len();
    const uint8_t v = tack_song_get(ui.song_row);
    const int mode = tack_song_mode();
    char t[8];
    uint32_t i;
    cv_begin(240, KNOBS_H, C_BLACK);
    for (i = 0; i < TACK_NP; i++)
        cv_text(10 + (int32_t)i * 56, 2, &FONT_XS, LABEL[i], C_GRAY);
    int_text(ui.song_row + 1, t);
    knob_num(10, t, C_WHITE);
    if (v < TACK_PATTERNS) {
        int_text(v + 1, t);
        knob_num(66, t, C_WHITE);
    } else {
        knob_word(66, "END", C_GRAY);
    }
    knob_word(122, tack_song_get(len) == TACK_SONG_LOOP ? "LOOP" : "STOP", C_WHITE);
    knob_word(178, mode == TACK_SONG_ON ? "ON" : mode == TACK_SONG_CHAIN ? "CHAIN" : "OFF",
            mode ? C_HI : C_WHITE);
    cv_blit(0, KNOBS_Y);
}

static void draw_set(void)
{
    char t[8];
    cv_begin(240, KNOBS_H, C_BLACK);
    cv_text(10, 2, &FONT_XS, "TEMPO", C_GRAY);
    int_text(tack_bpm(), t);
    knob_num(10, t, C_WHITE);
    cv_text(66, 2, &FONT_XS, "SHUFFLE", C_GRAY);
    int_text(tack_shuffle(), t);
    knob_num(66, t, C_WHITE);
    cv_text(122, 2, &FONT_XS, "MIDI CLK", C_GRAY);
    knob_word(122, tack_ext_sync() ? "ON" : "OFF", tack_ext_sync() ? C_HI : C_WHITE);
    cv_text(178, 2, &FONT_XS, "ROLL", C_GRAY);
    knob_word(178, tack_roll_name(tack_roll_rate()), C_WHITE);
    cv_blit(0, KNOBS_Y);
}

static void draw_st(void)
{
    static const char *const NAME[TACK_ST_NOTE] = {"NUDGE", "FLAM", "CHANCE", "VELOCITY"};
    const int held = ui.held >= 0;
    const uint8_t s = (uint8_t)(held ? step_of((uint32_t)ui.held) : 0);
    char t[8];
    uint32_t i;
    cv_begin(240, KNOBS_H, C_BLACK);
    if (ui.st_page == 2u) {
        cv_text(10, 2, &FONT_XS, "NOTE", C_GRAY);
        if (held) {
            note_text(tack_step_get((uint8_t)ui.voice, s, TACK_ST_NOTE), t);
            knob_num(10, t, C_HI);
        }
        cv_blit(0, KNOBS_Y);
        return;
    }
    for (i = 0; i < (held ? (uint32_t)TACK_ST_NOTE : 1u); i++) {
        const int32_t x = 10 + (int32_t)i * 56;
        const int v = held ? tack_step_get((uint8_t)ui.voice, s, i) : tack_lane_nudge_get((uint8_t)ui.voice);
        cv_text(x, 2, &FONT_XS, NAME[i], C_GRAY);
        if (i == TACK_ST_FLAM) {
            knob_word(x, tack_flam_name(v), C_WHITE);
        } else {
            int_text(v, t);
            knob_num(x, t, C_WHITE);
        }
    }
    cv_blit(0, KNOBS_Y);
}

static void draw_eu(void)
{
    static const char *const NAME[TACK_EU_SLOTS] = {"LENGTH", "HITS", "ROTATE"};
    char t[8];
    uint32_t i;
    cv_begin(240, KNOBS_H, C_BLACK);
    for (i = 0; i < TACK_EU_SLOTS; i++) {
        const int32_t x = 10 + (int32_t)i * 56;
        cv_text(x, 2, &FONT_XS, NAME[i], C_GRAY);
        int_text(tack_euclid_get((uint8_t)ui.voice, i), t);
        knob_num(x, t, C_WHITE);
    }
    cv_blit(0, KNOBS_Y);
}

static void draw(void)
{
    char t[16];
    uint32_t i;
    const int locking = ui.held >= 0;
    cv_begin(240, 52, C_BLACK);
    cv_text(10, 4, &FONT_B, ui.msg_until ? ui.msg : ui.kit_name[0] ? ui.kit_name : "INIT KIT", C_WHITE);
    t[0] = 'P';
    int_text(tack_pattern() + 1, t + 1);
    cv_text(150, 4, &FONT_B, t, tack_running() ? C_HI : C_GRAY);
    int_text(tack_bpm(), t);
    cv_text(196, 4, &FONT_B, t, C_HI);
    cv_text(10, 22, &FONT_M, VOICE_NAME[ui.voice], tack_muted((uint8_t)ui.voice) ? C_GRAY : C_HI);
    if (tack_song_mode())
        cv_text(86, 28, &FONT_XS, tack_song_mode() == TACK_SONG_ON ? "SONG" : "CHN", C_HI);
    cv_text(120, 28, &FONT_XS, ui.note_mode ? "NOTE" : ui.step_mode ? "STEP" : "PLAY", C_WHITE);
    t[0] = 'B';
    int_text(ui.bar + 1, t + 1);
    cv_text(152, 28, &FONT_XS, t, C_GRAY);
    t[0] = 'L';
    int_text(tack_length((uint8_t)ui.voice), t + 1);
    cv_text(174, 28, &FONT_XS, t, C_GRAY);
    if (tack_recording())
        cv_text(210, 28, &FONT_XS, "REC", C_HI);
    cv_blit(0, 0);
    if (note_view())
        draw_roll();
    else if (!tall())
        draw_grid();
    if (note_view()) {
        draw_note();
        goto strip_done;
    }
    if (ui.fx_page || ui.set_page || ui.eu_page || ui.st_page || ui.song_page)
        goto strip_done;
    cv_begin(240, KNOBS_H, C_BLACK);
    for (i = 0; i < TACK_NP; i++) {
        const TackSlot *sl = slot(i);
        const int32_t x = 10 + (int32_t)i * 56;
        const char *name;
        uint8_t v;
        int lit = 0;
        if (!sl)
            continue;
        v = tack_get_param(sl->par);
        if (locking) {
            v = ui.held_val[i];
            lit = tack_lock_get((uint8_t)ui.voice, (uint8_t)step_of((uint32_t)ui.held), sl->par, &v);
            if (lit)
                v = ui.held_val[i];
        }
        cv_text(x, 2, &FONT_XS, sl->label, C_GRAY);
        name = tack_slot_text(sl, v);
        if (sl->target) {                    /* too long for a column: draw_bottom */
            knob_word(x, "BELOW", C_GRAY);
        } else if (name) {
            knob_word(x, name, lit ? C_HI : C_WHITE);
        } else {
            int_text(v, t);
            knob_num(x, t, lit ? C_HI : C_WHITE);
        }
    }
    cv_blit(0, KNOBS_Y);
strip_done:
    if (ui.fx_page)
        draw_fx();
    if (ui.set_page)
        draw_set();
    if (ui.song_page)
        draw_song();
    if (ui.eu_page)
        draw_eu();
    if (ui.st_page)
        draw_st();
    draw_bottom();
}

void app_ui_init(void)
{
    if (ui.bad_patterns) {
        char t[4];
        int_text(ui.bad_patterns, t);
        app_say(t, "BAD PATTERNS");
    }
    ui.step_mode = 1;
    ui.note_vel = 110;
    ui.held = -1;
    lcd_fill(0, 0, 240, 240, C_BLACK);
    ui.redraw = 1;
}

void app_frame(void)
{
    static uint32_t last, seen;
    static int head;
    uint32_t now = plat_ms(), pct = plat_cpu_pct(), v, x = plat_xruns();
    int h = 0;
    if (x != ui.xrun_seen) {
        ui.xrun_seen = x;
        ui.xrun_act = now - ui.act_ms < 500u && ui.act ? ui.act : '?';
        ui.redraw = 1;
    }
    for (v = 0; v < TACK_VOICES; v++)          /* any lane's playhead moved */
        h = h * 67 + tack_playhead((uint8_t)v);
    h = h * 67 + tack_song_mode() * 256 + tack_song_pos();
    if (ui.note_mode || ui.held >= 0) {          /* a step's edit too */
        const uint8_t s = (uint8_t)(ui.held >= 0 ? step_of((uint32_t)ui.held) : (int)note_cursor());
        for (v = 0; v < TACK_ST_SLOTS; v++)
            h = h * 67 + tack_step_get((uint8_t)ui.voice, s, v);
        h = h * 67 + tack_step_on((uint8_t)ui.voice, s) + tack_recur_len((uint8_t)ui.voice, s) * 2 +
            tack_recur_mask((uint8_t)ui.voice, s) * 32;
    }
    if (ui.song_page)                            /* a row turned reaches the engine after the frame that drew it */
        h = h * 67 + (int)song_len() * 256 + tack_song_get(song_len()) + tack_song_get(ui.song_row) * 4;
    input();
    save_frame();
    (void)tack_kit_busy();                     /* a kit that went in shows, whichever way it came */
    if (tack_kit_now() != ui.kit_seen) {
        const uint8_t *k = tack_kit_now() ? kit_slot((uint32_t)tack_kit_now() - 1u) : 0;
        ui.kit_seen = tack_kit_now();
        ui.kit = ui.kit_seen ? ui.kit_seen - 1 : 0;
        if (k)
            memcpy(ui.kit_name, k, 8);
        ui.redraw = 1;
    }
    lights();
    if (pct > ui.cpu_peak)
        ui.cpu_peak = pct;
    if (ui.msg_until && now >= ui.msg_until) {
        ui.msg_until = 0;
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

#ifdef PLAT_HOST
int app_expect(const char *what, int *got)
{
    if (!strcmp(what, "voice"))
        *got = ui.voice;
    else if (!strcmp(what, "setpage"))
        *got = ui.set_page;
    else if (!strncmp(what, "fires", 5))     /* firesN: voice N's triggers */
        *got = tack_voice_fires((uint8_t)atoi(what + 5));
    else if (!strncmp(what, "used", 4))      /* usedN: voice N has steps here */
        *got = tack_lane_used((uint8_t)atoi(what + 4));
    else if (!strcmp(what, "tall"))
        *got = tall();
    else if (!strcmp(what, "badpatterns"))
        *got = ui.bad_patterns;
    else if (!strcmp(what, "lockcount"))
        *got = tack_lock_count();
    else if (!strncmp(what, "recurlen", 8))  /* recurlenN, recurmaskN: the selected voice's step N */
        *got = tack_recur_len((uint8_t)ui.voice, (uint8_t)atoi(what + 8));
    else if (!strncmp(what, "recurmask", 9))
        *got = tack_recur_mask((uint8_t)ui.voice, (uint8_t)atoi(what + 9));
    else if (!strcmp(what, "songpage"))
        *got = ui.song_page;
    else if (!strcmp(what, "songmode"))
        *got = tack_song_mode();
    else if (!strcmp(what, "songpos"))
        *got = tack_song_pos();
    else if (!strcmp(what, "songedit"))
        *got = ui.song_row;
    else if (!strcmp(what, "songlen"))
        *got = (int)song_len();
    else if (!strncmp(what, "songrow", 7))   /* songrowN: what the row holds */
        *got = tack_song_get((uint32_t)atoi(what + 7));
    else if (!strcmp(what, "fxpage"))
        *got = ui.fx_page;
    else if (!strcmp(what, "eupage"))
        *got = ui.eu_page;
    else if (!strcmp(what, "stpage"))
        *got = ui.st_page;
    else if (!strncmp(what, "st", 2) && what[2] == '_')   /* st_N: step 0's slot N on the selected voice */
        *got = tack_step_get((uint8_t)ui.voice, 0, (uint32_t)atoi(what + 3));
    else if (!strcmp(what, "lanenudge"))
        *got = tack_lane_nudge_get((uint8_t)ui.voice);
    else if (!strcmp(what, "roll"))
        *got = tack_roll_rate();
    else if (!strncmp(what, "eu", 2))        /* euN: the selected voice's Euclidean setting N */
        *got = tack_euclid_get((uint8_t)ui.voice, (uint32_t)atoi(what + 2));
    else if (!strcmp(what, "page"))          /* group * 10 + page */
        *got = ui.group * 10 + ui.sub;
    else if (!strncmp(what, "sl", 2))        /* slN: the value of the open page's slot N */
        *got = slot((uint32_t)atoi(what + 2)) ? tack_get_param(slot((uint32_t)atoi(what + 2))->par) : -1;
    else if (!strcmp(what, "echo"))
        *got = tack_fx_delay_ms();
    else if (!strcmp(what, "stress"))
        *got = ui.stress;
    else if (!strcmp(what, "dirty"))
        *got = (int)tack_dirty();
    else if (!strcmp(what, "saved"))
        *got = ui.saved;
    else if (!strncmp(what, "fxv", 3))       /* fxvN: the FX page's slot N */
        *got = tack_get_param(tack_fx_param((uint32_t)atoi(what + 3)));
    else if (!strncmp(what, "fxp", 3))       /* fxpN: the parameter behind it */
        *got = tack_fx_param((uint32_t)atoi(what + 3));
    else if (!strncmp(what, "send", 4))      /* sendN: voice N goes through the FX */
        *got = tack_fx_send((uint32_t)atoi(what + 4));
    else if (!strncmp(what, "len", 3))       /* lenN: voice N's lane */
        *got = tack_length((uint8_t)atoi(what + 3));
    else if (!strncmp(what, "locks", 5))     /* locksN: the selected voice's step N has a lock */
        *got = tack_locked((uint8_t)ui.voice, (uint8_t)atoi(what + 5));
    else if (!strcmp(what, "kit"))
        *got = ui.kit;
    else if (!strcmp(what, "bar"))
        *got = ui.bar;
    else if (!strcmp(what, "notemode"))
        *got = ui.note_mode;
    else if (!strcmp(what, "notestep"))
        *got = ui.note_step;
    else if (!strcmp(what, "noteoct"))
        *got = ui.note_oct;
    else if (!strncmp(what, "note", 4))      /* noteN: the selected voice's step N */
        *got = tack_step_get((uint8_t)ui.voice, (uint8_t)atoi(what + 4), TACK_ST_NOTE);
    else if (!strcmp(what, "stepmode"))
        *got = ui.step_mode;
    else if (!strcmp(what, "running"))
        *got = tack_running();
    else if (!strcmp(what, "pattern"))
        *got = tack_pattern();
    else if (!strcmp(what, "bpm"))
        *got = tack_bpm();
    else if (!strcmp(what, "shuffle"))
        *got = tack_shuffle();
    else if (!strcmp(what, "record"))
        *got = tack_recording();
    else if (!strcmp(what, "ext"))
        *got = tack_ext_sync();
    else if (!strncmp(what, "muted", 5))     /* mutedN: voice N */
        *got = tack_muted((uint8_t)atoi(what + 5));
    else if (!strncmp(what, "step", 4))      /* stepN: the selected voice's step N */
        *got = tack_step_on((uint8_t)ui.voice, (uint8_t)atoi(what + 4));
    else if (!strncmp(what, "param", 5))     /* paramN: the selected voice's slot N */
        *got = tack_get_param(tack_voice_param((uint32_t)ui.voice, (uint32_t)atoi(what + 5) % TACK_NP));
    else if (!strcmp(what, "dropped"))
        *got = (int)tack_dropped_events();
    else if (!strcmp(what, "midi"))
        *got = (int)midi_seen;
    else
        return 0;
    return 1;
}
#endif
