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
        app_say("NO LOCK", "ON THE SEND");
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
    if (i >= 0 && i < (int32_t)KIT_SLOTS && kit_slot((uint32_t)i)) {
        kit_load(i);
        if (!on_home()) {                        /* HOME has the name */
            char t[9];
            kit_name_of(kit_slot((uint32_t)i), t);
            app_say("KIT", t);
        }
    } else
        app_say("KITS", kit_slot(0) ? "END" : "NONE LOADED");
}

static uint32_t fx_slot(uint32_t knob) { return ui.btn >> B_OCTDN & 1u ? TACK_FX_C + knob : knob; }

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
    }
}

/* the settings page. The tempo and shuffle knobs stay on SELECT and ALGORITHM too */
static void set_turn(uint32_t slot, int32_t e)
{
    if (ui.held >= 0) {
        ui.held_used = 1;
        app_say("LOCKS", "VOICE PAGE");
    } else if (ui.btn >> B_OCTDN & 1u) {         /* the row below */
        if (slot == 0u && tack_running())
            app_say("STOP FIRST", "");
        else if (slot == 0u)
            usb_audio_set(e > 0);
        else if (slot == 1u)
            tack_post_param(tack_rate_param(), (uint8_t)clamp((int32_t)tack_get_param(tack_rate_param()) + e, 0, 127));
        else if (slot == 2u)
            tack_post_bits((uint8_t)clamp(tack_bits() + e, 0, 127));
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

/* the kit to morph to, with the POLY key held: the bank in order, from none */
static void morph_kit_step(int32_t e)
{
    const int32_t next = tack_morph_kit() + (e > 0 ? 1 : -1);
    if (next < 0)
        return;
    if (next && (next > (int32_t)KIT_SLOTS || !kit_slot((uint32_t)next - 1u)))
        app_say("KITS", kit_slot(0) ? "END" : "NONE LOADED");
    else
        tack_post_morph_kit((uint8_t)next);
}

#define MORPH_STEP 3                     /* a click, so that a good turn crosses the whole of it */

/* ALGORITHM: the name of the kit is not there to say where this comes from, so it says how to get one */
static void morph_turn(int32_t e)
{
    char t[4];
    const int32_t v = clamp(tack_morph() + e * MORPH_STEP, 0, 127);
    if (!tack_morph_kit()) {
        app_say(kit_slot(0) ? "HOLD POLY" : "KITS", kit_slot(0) ? "TURN PRESET" : "NONE LOADED");
        return;
    }
    tack_post_morph((uint8_t)v);
    if (!on_home()) {
        int_text(v, t);
        app_say("MORPH", t);
    }
}

/* ARP's first screen: how the lane plays. Nothing here rewrites a step */
static void lane_turn(uint32_t slot, int32_t e)
{
    const uint8_t v = (uint8_t)ui.voice;
    const int32_t dir = (int32_t)tack_lane_dir(v), speed = (int32_t)tack_lane_speed(v);
    if (slot == 0u) {
        const int32_t len = clamp(tack_length(v) + e, 1, TACK_BARS * 16);
        tack_post_length(v, (uint8_t)len);
        tack_euclid_length(v, (uint32_t)len);
    } else if (slot == 1u) {
        tack_post_lane(v, (uint32_t)clamp(dir + e, 0, TACK_LANE_DIRS - 1), (uint32_t)speed);
    } else if (slot == 2u) {                     /* clockwise is faster: 1X is the first of the speeds */
        tack_post_lane(v, (uint32_t)dir, (uint32_t)clamp(speed - e, 0, TACK_LANE_SPEEDS - 1));
    } else {
        tack_post_lane_start(v, (uint8_t)clamp(tack_lane_start(v) + e, 0, tack_length(v) - 1));
    }
}

/* ARP's second: K1 to K3 replace the lane's steps, and K4 puts back what the first turn replaced */
static void eu_turn(uint32_t slot, int32_t e)
{
    const uint8_t v = (uint8_t)ui.voice;
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
    const uint8_t s = (uint8_t)(ui.held >= 0 ? (uint32_t)step_of((uint32_t)ui.held) : note_cursor());
    ui.held_used = 1;
    if (tack_step_on((uint8_t)ui.voice, s))
        tack_step_turn((uint8_t)ui.voice, s, VV_SLOT[slot], e);
    else
        app_say("NO STEP", "");
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
static int save_can_blend(void) { return tack_morph_kit() && tack_morph(); }

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
    const uint8_t *to = ui.ks_morph ? kit_slot((uint32_t)tack_morph_kit() - 1u) : 0;
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
    ui.ks_slot = (uint8_t)(own && !blend ? own - 1 : free < KIT_SLOTS ? (int)free : (int)KIT_SLOTS - 1);
    save_name_load();
}

static void save_open(void)
{
    ui.ks_what = SAVE_PATTERN;
    ui.ks_pat = (uint8_t)tack_pattern();
    ui.song_page = PAGE_SAVE;
    ui.fx_page = ui.set_page = 0;
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
    ui.song_page = 0;
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
        ui.song_page = 0;
    } else if (!ui.save_todo && !ui.save_hold) {
        tack_post_copy(ui.ks_pat);
        tack_post_pattern(ui.ks_pat);
        ui.save_as = (uint8_t)(ui.ks_pat + 1u);
        ui.song_page = 0;
    }
}

/* the chain playing, over the rows from the one being edited, as the white keys would write them */
static void chain_write(void)
{
    const uint32_t n = tack_chain_len(), to = ui.song_row + n;
    char t[12];
    uint32_t c = 0;
    static const char WORD[] = " ROWS";
    if (tack_song_mode() != TACK_SONG_CHAIN)
        return;
    tack_post_chain_to_song(ui.song_row);
    ui.song_row = (uint8_t)(to < TACK_SONG_ROWS - 2u ? to : TACK_SONG_ROWS - 2u);
    int_text((int32_t)n, t);
    while (t[c])
        c++;
    memcpy(t + c, WORD, sizeof WORD);
    app_say("CHAIN", t);
}

/* K1 the shape, or OFF to put the lane's velocities back; the others set what it writes, and it writes at once */
static void vel_turn(uint32_t slot, int32_t e)
{
    const uint8_t v = (uint8_t)ui.voice;
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

/* A held step's popups: one job over the page, gone when the step is let go. ALGORITHM nudges it, SELECT and the black keys set
 * its recurrence, PRESET its chance */
enum { POP_NONE, POP_NUDGE, POP_RECUR, POP_CHANCE, POP_TEMPO };

static void step_turn(uint32_t slot, int popup, int32_t e)
{
    const uint8_t s = (uint8_t)step_of((uint32_t)ui.held);
    ui.held_used = 1;
    if (tack_step_on((uint8_t)ui.voice, s)) {
        tack_step_turn((uint8_t)ui.voice, s, slot, e);
        ui.popup = (uint8_t)popup;
    } else {
        app_say("NO STEP", "");
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
        ui.popup = POP_RECUR;
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
        ui.popup = POP_RECUR;
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
    tack_post_pattern((uint8_t)k);     /* stopped: now; running: at the end of the bar */
    int_text((int32_t)k + 1, t);
    app_say("PATTERN", t);
}

/* EDIT, ENV, LFO: this group's first page, then its next, then the main page */
static void page_press(uint8_t group)
{
    const uint8_t used = ui.held_used;
    const int covered = ui.song_page || ui.fx_page || ui.set_page;
    ui.song_page = ui.fx_page = ui.set_page = 0;
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
    if (ui.held >= 0) {                  /* a step held: the knobs start from the new page's values */
        hold_step((uint32_t)ui.held);
        ui.held_used = used;
    }
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
    if (bars > 0)
        tack_post_extend((uint8_t)ui.voice, (uint8_t)next);
    else
        tack_post_length((uint8_t)ui.voice, (uint8_t)next);
    note_cursor_to((uint32_t)(next - 1) / 16u * 16u);
    int_text(next, t);
    app_say("LENGTH", t);
}

static void tempo_show(void)
{
    ui.popup = POP_TEMPO;
    ui.tempo_until = plat_ms() + 1500u;
}

static void touch(uint32_t k)
{
    ui.touched = slot(k);
    ui.touched_at = k;
    ui.touched_until = plat_ms() + 1200u;
}

static int seq_in_view(void)                    /* the main page, showing a sequencer view */
{
    return ui.group == TACK_G_MAIN && !ui.fx_page && !ui.set_page && !ui.song_page;
}

static void to_main(void)
{
    ui.last_row[ui.group] = (uint8_t)ui.sub;
    ui.song_page = ui.fx_page = ui.set_page = 0;
    ui.group = TACK_G_MAIN;
    ui.sub = 0;
}
