/* SPDX-License-Identifier: GPL-3.0-only */
/* What a dial or key does on each page: steps, locks, kits, the song. app.c includes this: it is one file to the compiler, and shares app.c's state */

/* what K1-K4 start from on the page and row showing: the step's locks, or the kit's values when a clear is on its way */
static void held_read(int locks)
{
    for (uint32_t i = 0; i < TACK_NP; i++) {
        const TackSlot *sl = slot(i);
        if (sl && !(locks && tack_lock_get((uint8_t)ui.voice, ui.held_step, sl->par, &ui.held_val[i])))
            ui.held_val[i] = tack_get_param(sl->par);
    }
    ui.row_seen = (int8_t)row_now();
}

static void hold_step(uint32_t k)
{
    ui.held = (int32_t)k;
    ui.held_used = 0;
    ui.held_at = plat_ms();
    ui.held_step = (uint8_t)step_of(k);
    ui.held_pattern = (uint8_t)tack_pattern();
    if (voice_page())
        ui.note_step = ui.held_step;
    held_read(1);
}

static void lock_turn(uint32_t i, int32_t e)
{
    const uint8_t s = ui.held_step;
    const TackSlot *sl = slot(i);
    ui.held_used = 1;
    if (!sl)
        return;
    if (!tack_step_on((uint8_t)ui.voice, s)) {
        app_say("NO STEP", "");
    } else if (sl == tack_send_slot((uint32_t)ui.voice)) {
        app_say("NO LOCK", "ON SEND");
    } else if (sl->target) {
        app_say("NO LOCK", "ON TARGET");
    } else if (tack_slot_no_lock(sl)) {
        app_say("NO LOCK", tack_slot_no_lock(sl));
    } else if (tack_lock_room((uint8_t)ui.voice, s, sl->par) == TACK_LOCK_STEP_FULL) {
        app_say("STEP FULL", "8 LOCKS");
    } else if (tack_lock_room((uint8_t)ui.voice, s, sl->par) == TACK_LOCK_PATTERN_FULL) {
        app_say("PATTERN", "FULL");   /* as STEP FULL: no room for another lock */
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
    if (tack_post_kit(kit_buf + 8, kit_buf, (uint8_t)i))
        mark('K');
}

/* the nearest kit at or below slot i, -1 none: a smaller bank can leave a kit number past its end, and turning back
 * must still lead into the bank */
static int32_t kit_at_or_below(int32_t i)
{
    while (i >= 0 && !kit_slot((uint32_t)i))
        i--;
    return i;
}

/* PRESET: the bank's kits, and below the first the init kit, a clean start for a pattern. From the init kit, or a kit in no
 * slot, forward is the bank's first */
static void kit_step(int32_t e)
{
    const int32_t i = e > 0 ? (tack_kit_now() ? ui.kit + 1 : 0) : kit_at_or_below(ui.kit - 1);
    if (e < 0 && i < 0) {
        if (!tack_kit_now() && !ui.kit_name[0])  /* already there: turning on would only throw its edits away */
            app_say("KITS", "START");
        else if (!tack_kit_busy() && tack_post_init_kit()) {
            mark('K');
            if (!on_home())
                app_say("KIT", "INIT KIT");
        }
    } else if (i >= 0 && i < (int32_t)KIT_SLOTS && kit_slot((uint32_t)i)) {
        kit_load(i);
        if (!on_home()) {                        /* HOME has the name */
            char t[9];
            kit_name_of(kit_slot((uint32_t)i), t);
            app_say("KIT", t);
        }
    } else
        app_say("KITS", kit_slot(0) ? "END" : "NONE LOADED");
}

static uint32_t fx_slot(uint32_t knob) { return row_at() == 2 ? TACK_FX_C + knob : knob; }

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
        const uint8_t was = tack_get_param(p), now = tack_fx_turn(s, e);
        tack_post_param(p, now);
        if (s == TACK_FX_TYPE && now == FX_TYPE_VERB && was != now)
            tack_fx_verb_enter();
        else if (s == TACK_FX_TYPE && was == FX_TYPE_VERB && was != now)
            tack_fx_verb_leave();
    }
}

/* the settings page. The tempo and shuffle are on SELECT too, where it has no rows to pick */
static void set_turn(uint32_t slot, int32_t e)
{
    if (ui.held >= 0) {
        ui.held_used = 1;
        app_say("LOCKS", "VOICE PAGE");
    } else if (row_at() == 2) {                  /* the row below */
        if (slot == 0u && tack_running())
            app_say("STOP FIRST", "");
        else if (slot == 0u)
            usb_audio_set(e > 0);
        else if (slot == 1u)
            tack_post_ext_sync(e > 0);
        else if (slot == 2u)
            ui.keys_quiet = e < 0;
        else
            tack_post_roll_rate((uint8_t)clamp((int32_t)tack_roll_rate() + e, 0, TACK_ROLL_RATES - 1));
    } else if (slot == 0u) {
        tack_post_bpm((uint16_t)clamp((int32_t)tack_bpm() + e, 30, 300));
    } else if (slot == 1u) {
        tack_post_shuffle((uint8_t)clamp((int32_t)tack_shuffle() + e, 0, 127));
    } else if (slot == 2u) {
        tack_post_param(tack_rate_param(), (uint8_t)clamp((int32_t)tack_get_param(tack_rate_param()) + e, 0, 127));
    } else {
        tack_post_bits((uint8_t)clamp(tack_bits() + e, 0, 127));
    }
}

/* POLY held and a dial turned: that parameter in the morph target */
static void target_turn(uint32_t i, int32_t e)
{
    const TackSlot *sl = slot(i);
    if (!sl)
        return;
    if (!morph_shown(sl) || sl == tack_send_slot((uint32_t)ui.voice))
        app_say("NO MORPH", "ON THIS");
    else
        tack_post_target(sl->par, tack_slot_step(sl, tack_target_value(sl->par), e));
}

/* the kit to morph to, with the POLY key held: the bank in order, from none */
static void morph_kit_step(int32_t e)
{
    const int32_t next = e > 0 ? tack_morph_kit() + 1 : kit_at_or_below(tack_morph_kit() - 2) + 1;
    if (next < 0)
        return;
    if (next && (next > (int32_t)KIT_SLOTS || !kit_slot((uint32_t)next - 1u)))
        app_say("KITS", kit_slot(0) ? "END" : "NONE LOADED");
    else
        tack_post_morph_kit((uint8_t)next);
}

/* Frankenkit's parents, in the order K1 to K4 step through them: a kit from the bank at random for each combo, none, this kit,
 * then the bank's kits that are there */
#define FK_RANDOM (-1)
#define FK_NONE 0
#define FK_THIS 1
#define FK_END (-2)
static int32_t fk_next(int32_t pos, int32_t dir)
{
    for (;;) {
        pos += dir;
        if (pos < FK_RANDOM || pos >= (int32_t)KIT_SLOTS + 2)
            return FK_END;
        if (pos < 2 || kit_slot((uint32_t)pos - 2u))
            return pos;
    }
}
static const uint8_t *fk_src(int32_t pos)
{
    const uint8_t *k = pos >= 2 ? kit_params((uint32_t)pos - 2u) : 0;
    return k ? k : ui.fk_before;
}
static void fk_name(int32_t pos, char *t)
{
    if (pos == FK_THIS)
        kit_name_of(ui.fk_before_name[0] ? ui.fk_before_name : (const uint8_t *)"INIT KIT", t);
    else
        kit_name_of(pos >= 2 ? kit_slot((uint32_t)pos - 2u) : (const uint8_t *)(pos == FK_RANDOM ? "RANDOM  " : "NONE    "), t);
}
static uint32_t fk_rand(void)
{
    uint32_t x = ui.fk_seed ? ui.fk_seed : 0x9E3779B9u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return ui.fk_seed = x;
}
static int fk_has(const int16_t *list, uint32_t n, int32_t pos)
{
    for (uint32_t i = 0; i < n; i++)
        if (list[i] == pos)
            return 1;
    return 0;
}
/* a kit from the bank for a RANDOM slot: not one already in the combo, nor with this kit in it the bank's kit it came from */
static int fk_drawable(uint32_t i, const int16_t *list, uint32_t n)
{
    return kit_slot(i) && !fk_has(list, n, (int32_t)i + 2) && !(fk_has(list, n, FK_THIS) && (int32_t)i + 1 == tack_kit_now());
}
static int16_t fk_draw(const int16_t *list, uint32_t n)
{
    uint32_t count = 0, k;
    for (uint32_t i = 0; i < KIT_SLOTS; i++)
        count += (uint32_t)fk_drawable(i, list, n);
    if (!count)
        return FK_NONE;
    k = fk_rand() % count;
    for (uint32_t i = 0; i < KIT_SLOTS; i++)
        if (fk_drawable(i, list, n) && !k--)
            return (int16_t)(i + 2u);
    return FK_NONE;
}

/* a combo's kit as it went in, once it has */
static void fk_settle(void)
{
    if (ui.fk_pending && !tack_kit_busy()) {
        memcpy(ui.fk_last, tack_kit(), tack_kit_size());
        ui.fk_pending = 0;
    }
}

/* where the combos start from: the kit as it is now */
static void fk_start(void)
{
    memcpy(ui.fk_before, tack_kit(), tack_kit_size());
    memcpy(ui.fk_before_name, tack_kit_name(), 8);
    ui.fk_roll = 0;
    if (!ui.fk_seed)
        ui.fk_seed = plat_ms() * 2246822519u + 1u;
}

/* a combo into the pattern's kit */
static int fk_play(const FkCombo *c)
{
    const uint8_t *cells[TACK_FRANKEN_CELLS];
    for (uint32_t i = 0; i < TACK_FRANKEN_CELLS; i++)
        cells[i] = fk_src(c->cell[i]);
    tack_franken_kit(cells, fk_src(c->base), ui.fk_kit);
    if (!tack_post_kit(ui.fk_kit, (const uint8_t *)"FRANKEN ", TACK_KIT_EDIT))
        return 0;
    ui.fk_pending = 1;
    return 1;
}

/* the kit changed since the last combo: an edit, a kit loaded, another pattern. The combos start again from it */
static int fk_moved(void)
{
    fk_settle();
    return ui.fk_roll && !ui.fk_pending && memcmp(tack_kit(), ui.fk_last, tack_kit_size());
}

static void franken_open(void)
{
    if (ui.over == PAGE_FRANKEN)                 /* a second press: nothing, as a page's button */
        return;
    if (!ui.fk_ready) {
        ui.fk_parent[0] = FK_THIS;
        ui.fk_parent[1] = ui.fk_parent[2] = ui.fk_parent[3] = FK_RANDOM;
        ui.fk_ready = 1;
    }
    if (!ui.fk_roll || fk_moved())
        fk_start();
    ui.over = PAGE_FRANKEN;
    ui.redraw = 1;
}

/* a new combo: the whole kit, or with a voice key held (lane >= 0) that voice's sections, the rest as they are */
static void fk_new(int32_t lane)
{
    const int voice = lane < 0 ? -1 : lane < 6 ? (int)lane : 5;   /* the hats are one voice */
    const FkCombo *prev = ui.fk_roll ? &ui.fk_hist[ui.fk_roll - 1] : 0;
    FkCombo next;
    int16_t list[4];
    uint8_t pick[TACK_FRANKEN_CELLS];
    uint32_t n = 0;
    for (uint32_t k = 0; k < 4u; k++) {
        const int16_t pos = ui.fk_parent[k] == FK_RANDOM ? fk_draw(list, n) : ui.fk_parent[k];
        next.parent[k] = pos;
        if (pos != FK_NONE && !fk_has(list, n, pos))
            list[n++] = pos;
    }
    if (n < 2u) {
        app_say(kit_slot(0) ? "PICK" : "NO KITS", kit_slot(0) ? "TWO KITS" : "IN THE BANK");
        return;
    }
    for (uint32_t c = 0; c < TACK_FRANKEN_CELLS; c++)
        next.cell[c] = prev ? prev->cell[c] : FK_THIS;
    next.base = prev ? prev->base : FK_THIS;
    tack_franken_pick(fk_rand(), n, voice, pick);
    for (uint32_t c = 0; c < TACK_FRANKEN_CELLS; c++)
        if (voice < 0 || c / TACK_FRANKEN_SECTIONS == (uint32_t)voice)
            next.cell[c] = list[pick[c]];
    if (voice < 0)
        next.base = list[0];
    if (!fk_play(&next))
        return;
    if (ui.fk_roll == FK_DEPTH) {                /* the oldest goes */
        for (uint32_t i = 0; i + 1u < FK_DEPTH; i++)
            ui.fk_hist[i] = ui.fk_hist[i + 1u];
        ui.fk_roll--;
    }
    ui.fk_hist[ui.fk_roll++] = next;
    if (lane >= 0)
        app_say(VOICE_NAME[lane], "NEW COMBO");
}

/* PRESETS: a new combo a click, back through them, and at 0 the kit from before */
static void franken_roll(int32_t e, int32_t lane)
{
    if (tack_kit_busy())
        return;
    if (!ui.fk_roll || fk_moved())
        fk_start();
    if (e > 0) {
        fk_new(lane);
    } else {
        const int32_t to = ui.fk_roll + e < 0 ? 0 : ui.fk_roll + e;
        if (to == ui.fk_roll)
            return;
        if (!to ? tack_post_kit(ui.fk_before, ui.fk_before_name, TACK_KIT_EDIT) : fk_play(&ui.fk_hist[to - 1]))
            ui.fk_roll = to;
    }
}

/* K1 to K4: the parents of the combos to come */
static void franken_turn(uint32_t slot, int32_t e)
{
    int32_t pos = ui.fk_parent[slot];
    for (int32_t i = 0; i < (e < 0 ? -e : e); i++) {
        const int32_t next = fk_next(pos, e > 0 ? 1 : -1);
        if (next == FK_END)
            break;
        pos = next;
    }
    ui.fk_parent[slot] = (int16_t)pos;
}

#define MORPH_STEP 3                     /* a click, so that a good turn crosses the whole of it */

/* ALGORITHM: the name of the kit is not there to say where this comes from, so it says how to get one */
static void morph_turn(int32_t e)
{
    char t[4];
    const int32_t v = clamp(tack_morph() + e * MORPH_STEP, 0, 127);
    if (!tack_has_target()) {
        app_say("HOLD POLY", "TURN A DIAL");
        return;
    }
    tack_post_morph((uint8_t)v);
    if (!on_home()) {
        int_text(v, t);
        app_say("MORPH", t);
    }
}

/* a lane's length, which the EUCLID page follows */
static void lane_length(uint8_t v, int32_t len)
{
    tack_post_length(v, (uint8_t)len);
    tack_euclid_length(v, (uint32_t)len);
}

/* START, as played: a lane cut short since it was set starts this far in */
static int32_t lane_start_now(uint8_t v) { return tack_lane_start(v) % tack_length(v); }

/* ARP's first screen: how the lane plays. Nothing here rewrites a step */
static void lane_turn(uint32_t slot, int32_t e)
{
    const uint8_t v = (uint8_t)ui.voice;
    const int32_t dir = (int32_t)tack_lane_dir(v), speed = (int32_t)tack_lane_speed(v);
    ui.held_used = 1;
    if (slot == 0u) {
        lane_length(v, clamp(tack_length(v) + e, 1, TACK_BARS * 16));
    } else if (slot == 1u) {
        tack_post_lane(v, (uint32_t)clamp(dir + e, 0, TACK_LANE_DIRS - 1), (uint32_t)speed);
    } else if (slot == 2u) {                     /* clockwise is faster: 1X is the first of the speeds */
        tack_post_lane(v, (uint32_t)dir, (uint32_t)clamp(speed - e, 0, TACK_LANE_SPEEDS - 1));
    } else {
        tack_post_lane_start(v, (uint8_t)clamp(lane_start_now(v) + e, 0, tack_length(v) - 1));
    }
}

/* ARP's second: K1 to K3 replace the lane's steps, and K4 puts back what the first turn replaced */
static void eu_turn(uint32_t slot, int32_t e)
{
    const uint8_t v = (uint8_t)ui.voice;
    ui.held_used = 1;
    if (slot < TACK_EU_SLOTS) {
        mark('E');
        tack_euclid_turn(v, slot, e);
    } else if (tack_euclid_can_undo(v)) {
        tack_post_euclid_undo(v);
        app_say("UNDONE", "");
    }
}

static uint32_t key_note(uint32_t k)             /* the keys are F3 to G5 at octave 0 */
{
    return (uint32_t)clamp(53 + (int32_t)k + 12 * ui.note_oct, 0, 127);
}

/* The step selected in the bars view and NOTE: the one place in the bar showing, as OCT moves that */
static uint32_t note_cursor(void)
{
    const uint32_t s = (uint32_t)ui.bar * 16u + ui.note_step % 16u, len = (uint32_t)tack_length((uint8_t)ui.voice);
    return s < len ? s : len - 1u;
}

static void note_cursor_to(uint32_t s)
{
    ui.note_step = (uint8_t)s;
    ui.bar = (int32_t)(s / 16u);
}

/* the bars view's dials: the step, with or without a key held. Locks are on the other pages */
static const uint8_t VV_SLOT[TACK_NP] = {TACK_ST_VEL, TACK_ST_NUDGE, TACK_ST_FLAM, TACK_ST_PROB};

/* SEL and SELECT walk it, round the lane */
static void vv_walk(int32_t e)
{
    const int32_t len = tack_length((uint8_t)ui.voice);
    note_cursor_to((uint32_t)((((int32_t)note_cursor() + e) % len + len) % len));
    ui.redraw = 1;
}

/* SEL held: the same place in another of the lane's bars */
static void vv_bar_jump(int32_t e)
{
    const int32_t len = tack_length((uint8_t)ui.voice), bar = clamp(ui.bar + e, 0, (len - 1) / 16);
    const int32_t s = bar * 16 + (int32_t)note_cursor() % 16;
    note_cursor_to((uint32_t)(s < len ? s : len - 1));
    ui.redraw = 1;
}

static void vv_turn(uint32_t slot, int32_t e)
{
    const uint8_t s = (uint8_t)(ui.held >= 0 ? (uint32_t)ui.held_step : note_cursor());
    ui.held_used = 1;
    if (tack_step_on((uint8_t)ui.voice, s))
        tack_step_turn((uint8_t)ui.voice, s, VV_SLOT[slot], e);
    else
        app_say("NO STEP", "");
}

/* The keys play the voice. On NOTE they also set the selected step, and stay on it to try another, with SEL held to only listen. On
 * the sound pages they only play, to tune by. REC on writes them: running, as the LXR records; stopped, step by step */
static void note_keys(uint32_t down, uint32_t up, int listen)
{
    for (uint32_t k = 0; k < NKEYS; k++) {
        if (up >> k & 1u)
            tack_post_note_off(ui.key_voice[k], ui.key_note[k]);
        if (!(down >> k & 1u))
            continue;
        const uint8_t note = (uint8_t)key_note(k);
        ui.key_note[k] = note;                   /* OCT may move before the key comes up, and SEQ pick another voice */
        ui.key_voice[k] = (uint8_t)ui.voice;
        const uint32_t s = note_cursor();
        const int on = tack_step_on((uint8_t)ui.voice, (uint8_t)s);
        const int edit = !listen && !tack_recording() && note_view();
        ui.redraw = 1;
        if (edit && on && tack_step_get((uint8_t)ui.voice, (uint8_t)s, TACK_ST_NOTE) == note) {   /* the lit key: gone, and silently */
            tack_post_step((uint8_t)ui.voice, (uint8_t)s, 0);
            continue;
        }
        tack_post_note((uint8_t)ui.voice, note, ui.note_vel);
        if (listen) {
            ;
        } else if (tack_recording()) {
            if (!tack_running()) {
                tack_post_step_note((uint8_t)ui.voice, (uint8_t)s, note, ui.note_vel);
                note_cursor_to((s + 1u) % (uint32_t)tack_length((uint8_t)ui.voice));
            }
        } else if (edit) {                       /* a step that is on keeps how hard it plays */
            tack_post_step_note((uint8_t)ui.voice, (uint8_t)s, note, on ? (uint8_t)tack_step_get((uint8_t)ui.voice, (uint8_t)s, TACK_ST_VEL) : ui.note_vel);
        }
    }
}

static void note_turn(uint32_t slot, int32_t e)
{
    const uint8_t s = (uint8_t)note_cursor();
    if (slot == 2u) {
        ui.note_vel = (uint8_t)clamp((int32_t)ui.note_vel + e, 1, 127);
    } else if (slot == 3u) {
        tack_post_step((uint8_t)ui.voice, s, e > 0);
    } else if (!tack_step_on((uint8_t)ui.voice, s)) {
        app_say("NO STEP", "");
    } else if (slot == 1u) {
        tack_step_turn((uint8_t)ui.voice, s, TACK_ST_VEL, e);
    } else {
        tack_step_turn((uint8_t)ui.voice, s, TACK_ST_NOTE, e);
        if (!tack_running())                   /* to tune by ear */
            tack_post_note((uint8_t)ui.voice, (uint8_t)clamp(tack_step_get((uint8_t)ui.voice, s, TACK_ST_NOTE) + e, 0, 127),
                             ui.note_vel);
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

/* The kit page: a letter at a time, from what can be read on the screen. Anything else in a name shows as a space */
static const char KIT_LETTERS[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_";
#define KIT_LETTER_N ((int32_t)sizeof KIT_LETTERS - 1)

static int32_t kit_letter(char c)
{
    if (c >= 'a' && c <= 'z')
        c = (char)(c - 'a' + 'A');
    for (int32_t i = 0; i < KIT_LETTER_N; i++)
        if (KIT_LETTERS[i] == c)
            return i;
    return 0;
}

/* the first empty slot: the bank has no gaps, as PRESET walks it to its first empty one */
static uint32_t kit_free(void)
{
    uint32_t i = 0;
    while (i < KIT_SLOTS && kit_slot(i))
        i++;
    return i;
}

/* what morph makes can be saved: there is a blend to save */
static int save_can_blend(void) { return tack_has_target() && tack_morph(); }

static int save_name_empty(void)
{
    for (uint32_t i = 0; i < 8u; i++)
        if (ui.ks_name[i] != ' ')
            return 0;
    return 1;
}

/* The name of what a save replaces. An empty slot has none, so the kit's, or for a blend a start from both */
static void save_name_load(void)
{
    const uint8_t *now = kit_slot(ui.ks_slot);
    const uint8_t *to = ui.ks_morph && tack_has_target() ? tack_target_name() : 0;
    const char *own = ui.kit_name[0] ? ui.kit_name : "INIT KIT";
    for (uint32_t i = 0; i < 8u; i++)
        ui.ks_name[i] = KIT_LETTERS[kit_letter(now ? (char)now[i] : to && i >= 4u ? (char)to[i - 4u] : own[i])];
    ui.ks_name[8] = 0;
    ui.ks_pos = 0;
    ui.ks_typed = 0;
}

static void save_name_clear(void)
{
    memset(ui.ks_name, ' ', 8u);
    ui.ks_pos = 0;
    ui.ks_typed = 1;
}

/* SEL with PRESET the kit, with ALGORITHM the voice: forward is further from where it began, and back returns the same way */
static void mutate_turn(uint32_t voice, int32_t e)
{
    static uint32_t rolls;
    char t[12] = "START";
    const int step = tack_mutate(voice, e, plat_ms() * 2654435761u + ++rolls);
    if (step)
        int_text(step, t);
    app_say(voice < TACK_VOICES ? VOICE_NAME[voice] : "KIT", step ? t : "START");
}

static void save_name_make_up(void)
{
    ui.ks_seed = ui.ks_seed * 1664525u + 1013904223u + plat_ms();
    kit_name_make_up(ui.ks_seed, ui.ks_name);
    ui.ks_pos = 0;
    ui.ks_typed = 1;
}

static void save_kit_setup(int blend)
{
    const uint32_t free = kit_free();
    const int own = tack_kit_now();
    ui.ks_morph = (uint8_t)blend;                /* a sound between two kits: named after both, for a start */
    /* a kit from the bank goes back where it came from, and the page says it replaces it. A blend is a new kit */
    ui.ks_slot = (uint8_t)(own && !blend && (uint32_t)own - 1u <= free ? own - 1 : free < KIT_SLOTS ? (int)free : (int)KIT_SLOTS - 1);
    if (!ui.ks_typed)
        save_name_load();
}

static void save_open(void)
{
    ui.ks_what = SAVE_PATTERN;
    ui.ks_pat = (uint8_t)tack_pattern();
    ui.ks_typed = 0;
    ui.over = PAGE_SAVE;
}

/* K4: pattern, a blend if there is one, kit */
static void save_what(int32_t e)
{
    int32_t w = (int32_t)ui.ks_what + (e > 0 ? 1 : -1);
    if (w == SAVE_BLEND && !save_can_blend())
        w += e > 0 ? 1 : -1;
    if (w < SAVE_PATTERN || w > SAVE_KIT)
        return;
    ui.ks_what = (uint8_t)w;
    if (w != SAVE_PATTERN)
        save_kit_setup(w == SAVE_BLEND);
}

/* SELECT and K1: the slot, of the patterns or the bank */
static void save_slot(int32_t e)
{
    if (ui.ks_what == SAVE_PATTERN) {
        ui.ks_pat = (uint8_t)clamp((int32_t)ui.ks_pat + e, 0, TACK_PATTERNS - 1);
    } else {
        const uint32_t free = kit_free();
        ui.ks_slot = (uint8_t)clamp((int32_t)ui.ks_slot + e, 0, (int32_t)(free < KIT_SLOTS ? free : KIT_SLOTS - 1u));
        if (!ui.ks_typed)
            save_name_load();
    }
}

static void save_turn(uint32_t slot, int32_t e)
{
    if (slot == 0u) {
        save_slot(e);
    } else if (slot == 3u) {
        save_what(e);
    } else if (ui.ks_what == SAVE_PATTERN) {
        ;
    } else if (slot == 1u) {
        ui.ks_pos = (uint8_t)clamp((int32_t)ui.ks_pos + e, 0, 7);
    } else {                                     /* round: the end of the list is a turn back from A */
        ui.ks_name[ui.ks_pos] = KIT_LETTERS[((kit_letter(ui.ks_name[ui.ks_pos]) + e) % KIT_LETTER_N + KIT_LETTER_N) % KIT_LETTER_N];
        ui.ks_typed = 1;
    }
}

/* Writes the slot. Sixteen slots share a flash sector, so replacing one rewrites the other fifteen: a power cut in that moment
 * would lose them from the bank, though never from a pattern, and a filled slot goes in with no erase at all */
static void kit_bank_save(void)
{
    static uint8_t sector[0x1000], blended[KIT_SLOT];
    const uint32_t slot = ui.ks_slot, base = KIT_BASE + slot / 16u * 0x1000u, at = slot % 16u * KIT_SLOT;
    const uint8_t *now = plat_data(base);
    uint8_t t[4];
    int bad, fresh = 1;
    if (tack_running()) {
        app_say("STOP FIRST", "");
        return;
    }
    if (save_name_empty()) {
        app_say("NAME", "IS EMPTY");
        return;
    }
    if (!now || ui.save_todo || ui.save_hold || tack_kit_busy())
        return;
    memcpy(sector, now, sizeof sector);
    for (uint32_t i = 0; i < KIT_SLOT; i++)
        fresh &= sector[at + i] == 0xFFu;
    memset(sector + at, 0, KIT_SLOT);
    memcpy(sector + at, ui.ks_name, 8u);
    if (ui.ks_morph)
        tack_kit_blended(blended);
    memcpy(sector + at + 8u, ui.ks_morph ? blended : tack_kit(), tack_kit_size());
    mark('K');
    bad = fresh ? plat_data_write(base + at, sector + at, KIT_SLOT) : plat_data_erase(base) || plat_data_write(base, sector, sizeof sector);
    if (bad) {
        app_say("KIT SAVE", "FAILED");
        return;
    }
    if (!ui.ks_morph) {                          /* a blend is a new kit: this pattern stays on its two */
        tack_set_kit_name((const uint8_t *)ui.ks_name);
        tack_post_kit_home((uint8_t)slot);
    }
    ui.over = 0;
    int_text((int32_t)slot + 1, (char *)t);
    app_say("KIT SAVED", (char *)t);
}

/* SAVE on the page. On the pattern's own slot, everything changed. On another, the pattern goes there and carries on there: the copy
 * and the change are events, so save_frame writes it once they have landed */
static void save_commit(void)
{
    if (ui.ks_what != SAVE_PATTERN) {
        kit_bank_save();
    } else if (tack_running()) {
        app_say("STOP FIRST", "");
    } else if ((int)ui.ks_pat == tack_pattern()) {
        save();
        ui.over = 0;
    } else if (!ui.save_todo && !ui.save_hold) {
        tack_post_copy(ui.ks_pat);
        tack_post_pattern(ui.ks_pat);
        ui.save_as = (uint8_t)(ui.ks_pat + 1u);
        ui.over = 0;
    }
}

/* the chain playing, over the rows from the one being edited, as the white keys would write them */
static void chain_write(void)
{
    const uint32_t n = tack_chain_len(), to = ui.song_row + n;
    char t[12];
    if (tack_song_mode() != TACK_SONG_CHAIN)
        return;
    tack_post_chain_to_song(ui.song_row);
    ui.song_row = (uint8_t)(to < TACK_SONG_ROWS - 2u ? to : TACK_SONG_ROWS - 2u);
    text_put(int_put(t, (int32_t)n), " ROWS");
    app_say("CHAIN", t);
}

/* K1 the shape, or OFF to put the lane's velocities back; the others set what it writes, and it writes at once */
static void vel_turn(uint32_t slot, int32_t e)
{
    const uint8_t v = (uint8_t)ui.voice;
    ui.held_used = 1;
    if (slot == 0u) {
        const uint8_t was = ui.vs_shape;
        ui.vs_shape = (uint8_t)clamp((int32_t)ui.vs_shape + e, 0, TACK_SHAPE_N);
        if (ui.vs_shape == was)
            return;
        if (!ui.vs_shape) {
            app_say(tack_shape_can_undo(v) ? "UNDONE" : "NOTHING", tack_shape_can_undo(v) ? "" : "TO UNDO");
            tack_post_shape(v, 0u, 0u, 0u, 0u);
            return;
        }
        ui.vs_shift = (uint8_t)(ui.vs_shift % tack_shape_steps(ui.vs_shape));
    } else if (slot == 1u) {
        ui.vs_centre = (uint8_t)clamp((int32_t)ui.vs_centre + e, 1, 127);
    } else if (slot == 2u) {
        ui.vs_amount = (uint8_t)clamp((int32_t)ui.vs_amount + e, 0, 100);
    } else {
        const int32_t n = (int32_t)tack_shape_steps(ui.vs_shape);
        ui.vs_shift = (uint8_t)((((int32_t)ui.vs_shift + e) % n + n) % n);
    }
    if (ui.vs_shape)
        tack_post_shape(v, ui.vs_shape, ui.vs_shift, ui.vs_centre, ui.vs_amount);
}

/* SEL and K1: a turn right doubles the row, left takes it out */
static void song_edit(int32_t e)
{
    const uint32_t len = song_len();
    if (ui.song_row >= len) {
        app_say("END", "STAYS");
    } else if (e > 0 && len >= TACK_SONG_ROWS - 1u) {
        app_say("SONG", "FULL");
    } else if (e > 0) {
        tack_post_song_insert(ui.song_row);
        app_say("ROW", "COPIED");
    } else {
        tack_post_song_delete(ui.song_row);
        app_say("ROW", "REMOVED");
    }
}

static void song_turn(uint32_t slot, int32_t e, int sel)
{
    const uint32_t len = song_len();
    if (ui.held >= 0) {
        ui.held_used = 1;
    } else if (slot == 0u && sel) {
        song_edit(e);
    } else if (slot == 0u) {
        ui.song_row = (uint8_t)clamp((int32_t)ui.song_row + e, 0, (int32_t)(len < TACK_SONG_ROWS - 2u ? len : TACK_SONG_ROWS - 2u));
    } else if (slot == 1u) {
        const uint8_t now = tack_song_get(ui.song_row);
        const int32_t v = clamp((now < TACK_PATTERNS ? (int32_t)now : (int32_t)TACK_PATTERNS) + e, 0, TACK_PATTERNS);
        song_write(ui.song_row, v == TACK_PATTERNS ? tack_song_get(len) : (uint8_t)v);
    } else if (slot == 2u && ui.song_row < len) {
        tack_post_song_reps(ui.song_row, (uint8_t)clamp((int32_t)tack_song_reps(ui.song_row) + e, 0, TACK_SONG_REPS - 1));
    } else if (slot == 2u) {
        tack_post_song_row((uint8_t)len, e > 0 ? TACK_SONG_LOOP : TACK_SONG_STOP);
    } else if (e > 0 && !len) {
        app_say("SONG", "EMPTY");
    } else {
        tack_post_song_on(e > 0);
    }
}

/* A held step's popups: one job over the page, gone when the step is let go. ALGORITHM nudges it, SELECT and the black keys set
 * its recurrence, PRESET its chance */
enum { POP_NONE, POP_NUDGE, POP_CHANCE };   /* recurrence and tempo are said where they are, with no popup */

static void step_turn(uint32_t slot, int popup, int32_t e)
{
    const uint8_t s = ui.held_step;
    ui.held_used = 1;
    if (tack_step_on((uint8_t)ui.voice, s)) {
        tack_step_turn((uint8_t)ui.voice, s, slot, e);
        ui.popup = (uint8_t)popup;
    } else {
        app_say("NO STEP", "");
    }
}

/* a step held: the first eight black keys are the slots of its recurrence, which they take out and put back, and SELECT how many
 * there are, as a lane's end */
static void recur_key(uint32_t k)
{
    const uint8_t s = ui.held_step, v = (uint8_t)ui.voice;
    const int len = recur_len(v, s);
    int mask = recur_mask(v, s);
    ui.held_used = 1;
    if (!tack_step_on(v, s)) {
        app_say("NO STEP", "");
    } else if ((int)k >= len) {
        app_say("SELECT", "ADDS SLOTS");
    } else if ((mask ^= 1 << k) == 0) {
        app_say("ONE SLOT", "AT LEAST");
    } else {
        recur_set(v, s, len, mask);
        ui.popup = POP_NONE;                     /* the step's view says it, or on a full page the header */
        ui.redraw = 1;
    }
}

/* fewer slots keep what still fits, and more come in playing, as a new step plays every pass */
static void recur_turn(int32_t e)
{
    const uint8_t s = ui.held_step, v = (uint8_t)ui.voice;
    const int len = recur_len(v, s), n = (int)clamp(len + e, 2, TACK_RECUR_MAX);
    const int grown = ((1 << n) - 1) & ~((1 << len) - 1);    /* the slots it gains, playing */
    const int mask = (recur_mask(v, s) | grown) & ((1 << n) - 1);
    ui.held_used = 1;
    if (!tack_step_on(v, s)) {
        app_say("NO STEP", "");
    } else if (!tack_recur_room(v, s)) {
        app_say("PATTERN", "FULL");   /* as STEP FULL: no room for another lock */
    } else {
        recur_set(v, s, n, mask ? mask : 1);
        ui.popup = POP_NONE;                     /* the step's view says it, or on a full page the header */
        ui.redraw = 1;
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
/* SEL and HOME held a second: the kit as the pattern was last saved with it. A pattern never saved, or saved without a kit of its
 * own, goes back to the bank's kit it came from */
static void kit_revert(void)
{
    const int now = tack_kit_now();
    const int len = tack_kit_busy() ? -1 : plat_store_load(OBJ_PAT0 + (uint32_t)tack_pattern(), pat_buf, sizeof pat_buf);
    const int saved = len > 0 ? tack_image_kit(pat_buf, (uint32_t)len, kit_buf + 8, kit_buf) : 0;
    if (saved) {
        if (tack_post_kit(kit_buf + 8, kit_buf, saved > 1 ? (uint8_t)(saved - 2) : TACK_KIT_EDIT))
            app_say("KIT", "RELOADED");
    } else if (now && kit_slot((uint32_t)now - 1u)) {
        kit_load(now - 1);
        app_say("KIT", "REVERTED");
    } else {
        app_say("NO KIT", "IN THE BANK");
    }
}

static void pattern_copy(uint32_t k)
{
    ui.copy_used = 1;
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
    ui.held_used = 1;
    tack_post_pattern((uint8_t)k);     /* stopped: now; running: at the end of the bar */
    int_text((int32_t)k + 1, t);
    app_say("PATTERN", t);
}

/* EDIT, ENV, LFO: this group's first page, then its next, then the main page */
static void page_press(uint8_t group)
{
    const int covered = ui.over;
    ui.over = 0;
    if (covered && ui.group == group) {  /* back on the screen it covered */
        ;
    } else if (ui.group != group) {      /* the screen it was left on */
        ui.last_row[ui.group] = (uint8_t)ui.sub;
        ui.group = group;
        ui.sub = ui.last_row[group];
        if (ui.sub >= tack_pages((uint32_t)ui.voice, group) || screen_page(screen_now(), 1) != ui.sub)
            ui.sub = screen_page(0, 1);
    } else if (screens() > 1) {          /* a page's button goes through its screens, and with one does nothing */
        ui.sub = screen_page(!screen_now(), 1);
    }
    if (ui.held >= 0)                    /* a step held: the knobs start from the new page's values */
        held_read(1);
    ui.redraw = 1;
}

/* how many bars the longest lane of this pattern reaches */
static int bars_used(void)
{
    int n = 1;
    for (uint32_t v = 0; v < TACK_VOICES; v++) {
        const int b = (tack_length((uint8_t)v) + 15) / 16;
        n = b > n ? b : n;
    }
    return n;
}

/* the bars OCT walks: a detail view shows the selected lane, and the overview them all */
static int bars_shown(void) { return seq_detail() ? (tack_length((uint8_t)ui.voice) + 15) / 16 : bars_used(); }

/* SEL held with OCT: a bar more or less on the selected lane, and on to its last bar. Steps past the end are kept */
static void lane_bar(int32_t bars)
{
    const int32_t len = tack_length((uint8_t)ui.voice);
    const int32_t next = clamp(len + bars * 16, len < 16 ? len : 16, TACK_BARS * 16);
    char t[4];
    if (next == len) {
        app_say(bars > 0 ? "MAX 4 BARS" : "MIN 1 BAR", "");
        return;
    }
    if (bars > 0) {
        tack_post_extend((uint8_t)ui.voice, (uint8_t)next);
        tack_euclid_length((uint8_t)ui.voice, (uint32_t)next);
    } else {
        lane_length((uint8_t)ui.voice, next);
    }
    note_cursor_to((uint32_t)(next - 1) / 16u * 16u);
    int_text(next, t);
    app_say("LENGTH", t);
}

/* SELECT on a page with rows: a row down for each step right, up for each left, and no further than the last */
static void row_step(int32_t e)
{
    const uint32_t m = row_choices();
    const int dir = e > 0 ? 1 : -1;
    int r = row_picked();
    for (; e; e -= dir) {
        int n = r + dir;
        while (n >= 0 && n < SCREEN_ROWS && !(m >> n & 1u))
            n += dir;
        if (n < 0 || n >= SCREEN_ROWS)
            break;
        r = n;
    }
    ui.row_move = (int8_t)(r - 1);
    ui.touched_until = 0;                        /* the header named a dial of the row left */
    if (ui.held >= 0)
        held_read(1);
    ui.redraw = 1;
}

static void tempo_show(int shuffle)
{
    ui.tempo_until = (plat_ms() + 1500u) | 1u;   /* never 0, which is none */
    ui.tempo_shuffle = (uint8_t)shuffle;
    ui.touched_until = 0;
    ui.redraw = 1;
}

static void touch(uint32_t k)
{
    ui.touched = slot(k);
    ui.touched_at = k;
    ui.touched_row = (int8_t)row_now();
    ui.touched_until = plat_ms() + 1200u;
    ui.tempo_until = 0;
}

static int seq_in_view(void)                    /* the main page, showing a sequencer view */
{
    return ui.group == TACK_G_MAIN && !ui.over;
}

static void to_main(void)
{
    ui.last_row[ui.group] = (uint8_t)ui.sub;
    ui.over = 0;
    ui.group = TACK_G_MAIN;
    ui.sub = 0;
}
