/* SPDX-License-Identifier: GPL-3.0-only */
/* The full pages: FX, settings, song, kit, the bars view and NOTE. app.c includes this: it is one file to the compiler, and shares app.c's state */

/* FX, GLO and SONG, in the same columns and widgets */
static uint32_t rk(uint32_t k, uint32_t v) { return (k ^ v) * 16777619u; }

static void pg_column(uint32_t i, const char *label, int on)
{
    const int32_t x = PG_CW / 2 + (int32_t)i * PG_CW;
    cv_text(x - text_w(&FONT_B, label) / 2, PG_PH, &FONT_B, label, on ? C_WHITE : PG_TEXT);
}

/* a dial with its value in it, or a value alone */
static void wd_dial_text(uint32_t i, const char *t, uint32_t v, uint32_t max, int on)
{
    const int32_t x = PG_CW / 2 + (int32_t)i * PG_CW;
    wd_dial(x, 0, v, max, on ? C_HI : C_GRAY, on ? C_GRAY : C_DIM);
    cv_text(x - text_w(&FONT_S, t) / 2, 34, &FONT_S, t, on ? C_WHITE : PG_TEXT);
}

static void wd_value(uint32_t i, uint32_t v, uint32_t max, int on, int dial)
{
    const int32_t x = PG_CW / 2 + (int32_t)i * PG_CW;
    char t[8];
    int_text((int32_t)v, t);
    if (dial) {
        wd_dial_text(i, t, v, max, on);
    } else {
        cv_text(x - text_w(&FONT_M, t) / 2, 16, &FONT_M, t, on ? C_WHITE : PG_TEXT);
    }
}

static void pg_fx_sends(uint32_t *last)
{
    uint32_t key = 7u;
    for (uint32_t i = 0; i < TACK_FX_SENDS; i++)
        key = rk(key, (uint32_t)tack_fx_send(i));
    if (key == *last)
        return;
    *last = key;
    cv_begin(240, PG_RH, C_BLACK);
    for (uint32_t i = 0; i < TACK_FX_SENDS; i++) {
        const int32_t x = 20 + (int32_t)i * 40;
        if (tack_fx_send(i))
            cv_rect(x - 14, 12, 28, 30, VOICE_COLOUR[i]);
        else
            outline(x - 14, 12, 28, 30, 2, C_DIM);
        cv_text(x - text_w(&FONT_B, SEND_NAME[i]) / 2, PG_PH, &FONT_B, SEND_NAME[i], tack_fx_send(i) ? C_WHITE : PG_TEXT);
    }
    cv_blit(0, PG_Y);
}

static void pg_fx_row(uint32_t r, uint32_t *last)
{
    const int on = (r == 2u) == ((ui.btn >> B_OCTDN & 1u) != 0u);
    const uint32_t first = r == 1u ? (uint32_t)TACK_FX_TYPE : (uint32_t)TACK_FX_C, n = r == 1u ? 4u : 3u;
    const int echo = r == 2u ? tack_fx_delay_ms() : 0;
    uint32_t key = rk(r, (uint32_t)on);
    key = rk(key, tack_get_param(tack_fx_param(TACK_FX_TYPE)));
    key = rk(key, (uint32_t)echo);
    for (uint32_t i = 0; i < TACK_FX_ALL; i++)   /* all of them: MODE decides how the other row's TIME reads */
        key = rk(key, tack_get_param(tack_fx_param(i)));
    if (key == *last)
        return;
    *last = key;
    cv_begin(240, PG_RH, C_BLACK);
    if (on) {
        uint32_t dials = 0;
        for (uint32_t i = 0; i < n; i++)
            dials |= (uint32_t)(first + i == TACK_FX_TYPE || tack_fx_max(first + i)) << i;
        row_lines(dials, C_HI);
    }
    for (uint32_t i = 0; i < 4u; i++) {
        const uint32_t sl = first + i;
        const int32_t x = PG_CW / 2 + (int32_t)i * PG_CW;
        if (i >= n || !(sl == TACK_FX_TYPE || tack_fx_max(sl))) {
            if (r == 2u && i == 3u && echo) {
                wd_value(3, (uint32_t)echo, 0, on, 0);
                pg_column(3, "MS", on);
            } else {
                wd_empty(x, 0);
            }
            continue;
        }
        {
            const uint8_t v = tack_get_param(tack_fx_param(sl));
            const char *name = sl == TACK_FX_TYPE ? tack_fx_type_name() : tack_fx_text(sl, v);
            pg_column(i, tack_fx_label(sl), on);
            if (name)
                wd_box(x, 0, name, on ? C_HI : C_GRAY);
            else
                wd_value(i, v, tack_fx_max(sl), on, 1);
        }
    }
    cv_blit(0, PG_Y + r * PG_RH);
}

static void pg_set_load(uint32_t *last)
{
    const uint32_t cpu = plat_cpu_pct(), xrun = plat_xruns() - ui.xrun_base;
    uint32_t key = rk(rk(rk(rk(11u, cpu), ui.cpu_peak), xrun), ui.xrun_act);
    if (key == *last)
        return;
    *last = key;
    cv_begin(240, PG_RH, C_BLACK);
    wd_value(0, cpu, 0, 1, 0);
    pg_column(0, "CPU", 0);
    wd_value(1, ui.cpu_peak, 0, 1, 0);
    pg_column(1, "PEAK", 0);
    wd_value(2, xrun, 0, 1, 0);
    pg_column(2, "XRUN", 0);
    if (ui.xrun_act) {
        char t[2] = {ui.xrun_act, 0};
        cv_text(PG_CW * 3 + PG_CW / 2 - text_w(&FONT_M, t) / 2, 16, &FONT_M, t, C_HI);
        pg_column(3, "LAST", 0);
    }
    cv_blit(0, PG_Y);
}

static void pg_set_row(uint32_t *last)
{
    const uint32_t ext = (uint32_t)tack_ext_sync();
    uint32_t key = rk(rk(rk(rk(13u + (ui.btn >> B_OCTDN & 1u), tack_bpm()), tack_shuffle()), ext), (uint32_t)tack_roll_rate());
    if (key == *last)
        return;
    *last = key;
    cv_begin(240, PG_RH, C_BLACK);
    row_lines(ui.btn >> B_OCTDN & 1u ? 0u : 0xFu, C_HI);
    wd_value(0, tack_bpm(), 0, 1, 0);
    pg_column(0, "TEMPO", 1);
    wd_value(1, tack_shuffle(), 127, 1, 1);
    pg_column(1, "SHUFFLE", 1);
    wd_toggle(PG_CW * 2 + PG_CW / 2, 0, (int)ext, C_HI);
    pg_column(2, "MIDI CLK", 1);
    wd_box(PG_CW * 3 + PG_CW / 2, 0, tack_roll_name(tack_roll_rate()), C_HI);
    pg_column(3, "ROLL", 1);
    cv_blit(0, PG_Y + PG_RH);
}

/* the row an OCT- held reaches: what USB audio is */
static void pg_set_usb(uint32_t *last)
{
    const int on = (ui.btn >> B_OCTDN & 1u) != 0u, diag = ui.usb10 && (ui.btn >> B_OCTUP & 1u);
    const uint32_t drops = usb_drops(), rate = tack_get_param(tack_rate_param()), bits = (uint32_t)tack_bits();
    uint32_t st[4], key;
    usb_pump_stats(st);
    key = rk(rk(rk(rk(rk(rk(rk(rk(rk(rk(rk(31u, (uint32_t)on), ui.usb10), drops), st[0]), st[1]), st[2]), st[3]), ui.save_gap), rate), bits), (uint32_t)diag);
    if (key == *last)
        return;
    *last = key;
    cv_begin(240, PG_RH, C_BLACK);
    row_lines(on ? 0x7u : 0u, C_HI);
    wd_box(PG_CW / 2, 0, ui.usb10 ? "10 CH" : "STEREO", on ? C_HI : C_GRAY);
    pg_column(0, "USB AUDIO", on);
    if (diag) {                                  /* OCT+ held: what USB audio has been doing */
        char t[12];
        wd_value(1, drops, 0, 1, 0);
        pg_column(1, "DROPS", on);
        t[0] = 'A';                              /* the save's silent packets, in a line each: armed, sent, gave up */
        int_text((int32_t)st[0], t + 1);
        cv_text(PG_CW * 2 + 4, 8, &FONT_S, t, on ? C_WHITE : PG_TEXT);
        t[0] = 'S';
        int_text((int32_t)st[1], t + 1);
        cv_text(PG_CW * 3 + 4, 8, &FONT_S, t, on ? C_WHITE : PG_TEXT);
        t[0] = 'Q';
        int_text((int32_t)st[2], t + 1);
        cv_text(PG_CW * 2 + 4, 28, &FONT_S, t, on ? C_WHITE : PG_TEXT);
        t[0] = 'E';                              /* the longest erase, and what a save held the tick up by, in ms */
        int_text((int32_t)st[3], t + 1);
        cv_text(PG_CW * 3 + 4, 28, &FONT_S, t, on ? C_WHITE : PG_TEXT);
        t[0] = 'G';
        int_text((int32_t)(ui.save_gap / 1000u), t + 1);
        cv_text(PG_CW * 2 + 4, 48, &FONT_S, t, on ? C_WHITE : PG_TEXT);
    } else {
        char t[4];
        wd_value(1, rate, 127, on, 1);
        pg_column(1, "RATE", on);
        int_text(2 + (int32_t)((bits * 14u + 63u) / 127u), t);
        wd_dial_text(2, t, bits, 127, on);
        pg_column(2, "BITS", on);
    }
    cv_blit(0, PG_Y + 2u * PG_RH);
}

static void pg_song_top(uint32_t *last)
{
    const uint32_t len = song_len();
    const uint8_t v = tack_song_get(ui.song_row);
    const int mode = tack_song_mode();
    uint32_t key = rk(rk(rk(rk(17u, ui.song_row), v), tack_song_get(len)), (uint32_t)mode);
    if (key == *last)
        return;
    *last = key;
    cv_begin(240, PG_RH, C_BLACK);
    row_lines(0xFu, C_HI);
    wd_value(0, ui.song_row + 1u, 0, 1, 0);
    pg_column(0, "ROW", 1);
    if (v < TACK_PATTERNS)
        wd_value(1, v + 1u, 0, 1, 0);
    else
        wd_box(PG_CW + PG_CW / 2, 0, "END", C_GRAY);
    pg_column(1, "PATTERN", 1);
    wd_box(PG_CW * 2 + PG_CW / 2, 0, tack_song_get(len) == TACK_SONG_LOOP ? "LOOP" : "STOP", C_HI);
    pg_column(2, "AT END", 1);
    if (mode == TACK_SONG_CHAIN)
        wd_box(PG_CW * 3 + PG_CW / 2, 0, "CHAIN", C_HI);
    else
        wd_toggle(PG_CW * 3 + PG_CW / 2, 0, mode == TACK_SONG_ON, C_HI);
    pg_column(3, "SONG", 1);
    cv_blit(0, PG_Y);
}

/* SAVE: K4 says what, K1 or SELECT where, and the last row what a second press will do */
static const char *save_word(void)
{
    return ui.ks_what == SAVE_PATTERN ? "PTRN" : ui.ks_what == SAVE_BLEND ? "BLEND" : "KIT";
}

static void pg_save_top(uint32_t *last)
{
    const int pattern = ui.ks_what == SAVE_PATTERN;
    uint32_t key = rk(rk(rk(rk(43u, ui.ks_what), ui.ks_pat), ui.ks_slot), ui.ks_pos);
    key = rk(rk(key, (uint32_t)kit_letter(ui.ks_name[ui.ks_pos])), (uint32_t)save_can_blend());
    if (key == *last)
        return;
    *last = key;
    cv_begin(240, PG_RH, C_BLACK);
    row_lines(pattern ? 0x9u : 0xFu, C_HI);
    wd_value(0, (pattern ? ui.ks_pat : ui.ks_slot) + 1u, 0, 1, 0);
    pg_column(0, "SLOT", 1);
    if (!pattern) {
        const char t[2] = {ui.ks_name[ui.ks_pos], 0};
        wd_value(1, ui.ks_pos + 1u, 0, 1, 0);
        pg_column(1, "CURSOR", 1);
        wd_box(PG_CW * 2 + PG_CW / 2, 0, t[0] == ' ' ? "SPACE" : t, C_HI);
        pg_column(2, "LETTER", 1);
    } else {                                     /* the empty slot, as a dial with nothing to do looks anywhere else */
        wd_empty(PG_CW + PG_CW / 2, 0);
        wd_empty(PG_CW * 2 + PG_CW / 2, 0);
    }
    wd_box(PG_CW * 3 + PG_CW / 2, 0, save_word(), C_HI);
    pg_column(3, "SAVING", 1);
    cv_blit(0, PG_Y);
}

/* a pattern's slots, as the white keys are, the one picked lit. Grey: empty */
static void pg_save_slots(uint32_t *last)
{
    const uint32_t used = tack_patterns_used(), changed = tack_dirty();
    const uint32_t key = rk(rk(rk(61u, ui.ks_pat), used), changed);
    if (key == *last)
        return;
    *last = key;
    cv_begin(240, PG_RH, C_BLACK);
    for (uint32_t p = 0; p < TACK_PATTERNS; p++) {
        const int32_t x = EDGE + 1 + (int32_t)(p % 8u) * 29, y = 2 + (int32_t)(p / 8u) * 34;
        const int on = p == ui.ks_pat;
        char t[4];
        cv_rect(x, y, 26, 30, on ? C_HI : C_LINE);
        int_text((int32_t)p + 1, t);
        cv_text(x + 13 - text_w(&FONT_S, t) / 2, y + 7, &FONT_S, t, on ? C_BLACK : used >> p & 1u ? C_WHITE : C_GRAY);
    }
    cv_blit(0, PG_Y + PG_RH);
}

#define PG_NAME_W 20

/* the name as a word, the letter being changed lit and a line under each place that is free */
static void pg_save_name(uint32_t *last)
{
    uint32_t key = rk(47u, ui.ks_pos);
    for (uint32_t i = 0; i < 8u; i++)
        key = rk(key, (uint8_t)ui.ks_name[i]);
    if (key == *last)
        return;
    *last = key;
    cv_begin(240, PG_RH, C_BLACK);
    cv_text(EDGE, 2, &FONT_XS, "NAME", C_GRAY);
    cv_text(240 - EDGE - text_w(&FONT_XS, "OCT- CLEAR  OCT+ RND"), 2, &FONT_XS, "OCT- CLEAR  OCT+ RND", C_GRAY);
    for (uint32_t i = 0; i < 8u; i++) {
        const int32_t x = 120 - 4 * PG_NAME_W + (int32_t)i * PG_NAME_W;
        const char t[2] = {ui.ks_name[i], 0};
        if (i == ui.ks_pos)
            cv_rect(x, 22, PG_NAME_W, 40, C_HI);
        else if (t[0] == ' ')
            cv_rect(x + 3, 58, PG_NAME_W - 6, 2, C_GRAY);
        cv_text(x + PG_NAME_W / 2 - text_w(&FONT_M, t) / 2, 28, &FONT_M, t, i == ui.ks_pos ? C_BLACK : C_WHITE);
    }
    cv_blit(0, PG_Y + PG_RH);
}

static void save_note_pattern(char *a, uint32_t todo)
{
    uint32_t n = 0;
    if ((int)ui.ks_pat == tack_pattern()) {
        for (uint32_t p = 0; p < TACK_PATTERNS; p++)
            n += todo >> p & 1u;
        text_put(int_put(text_put(a, "SAVE "), (int32_t)n), n == 1u ? " PATTERN" : " PATTERNS");
    } else {
        const int used = tack_patterns_used() >> ui.ks_pat & 1u;
        int_text(ui.ks_pat + 1, text_put(a, used ? "OVERWRITE P" : "SAVE AS P"));
    }
}

static void save_note_kit(char *a, char *b, const uint8_t *now)
{
    char t[9];
    if (now) {
        kit_name_of(now, t);
        text_put(text_put(a, "OVERWRITE "), t);
    } else {
        text_put(a, "SAVE AS NEW KIT");
    }
    if (ui.ks_what == SAVE_BLEND) {
        char *e = text_put(int_put(text_put(b, "MORPH "), tack_morph()), " OF ");
        kit_name_of((const uint8_t *)(ui.kit_name[0] ? ui.kit_name : "INIT KIT"), t);
        morph_name(text_put(text_put(e, t), " > "));
    } else {
        text_put(b, "AS IT SOUNDS NOW");
    }
}

static void pg_save_note(uint32_t *last)
{
    const uint32_t todo = tack_dirty() | 1u << tack_pattern();
    const uint8_t *now = ui.ks_what == SAVE_PATTERN ? 0 : kit_slot(ui.ks_slot);
    const int run = tack_running();
    uint32_t key = rk(rk(rk(rk(rk(rk(67u, todo), (uint32_t)run), ui.ks_what), ui.ks_pat), ui.ks_slot), (uint32_t)tack_pattern());
    char a[40], b[40];
    key = rk(rk(rk(key, now != 0), (uint32_t)tack_morph()), (uint32_t)save_name_empty());
    if (key == *last)
        return;
    *last = key;
    a[0] = b[0] = 0;
    if (ui.ks_what == SAVE_PATTERN)
        save_note_pattern(a, todo);
    else
        save_note_kit(a, b, now);
    cv_begin(240, PG_RH, C_BLACK);
    cv_text(120 - text_w(&FONT_B, a) / 2, 2, &FONT_B, a, now || a[0] == 'O' ? C_HI : C_WHITE);
    cv_text(120 - text_w(&FONT_XS, b) / 2, 22, &FONT_XS, b, C_WHITE);
    {
        const int nameless = ui.ks_what != SAVE_PATTERN && save_name_empty();
        const char *const c = nameless ? "NAME IT FIRST" : run ? "STOP PLAY FIRST" : "PRESS SAVE";
        cv_text(120 - text_w(&FONT_XS, c) / 2, b[0] ? 38 : 22, &FONT_XS, c, run || nameless ? C_HI : C_WHITE);
    }
    cv_blit(0, PG_Y + 2u * PG_RH);
}

/* the rows around the one being edited, laid out as the white keys are; a line under the row playing */
static void pg_song_rows(uint32_t line, uint32_t *last)
{
    const uint32_t first = song_first() + line * 8u, len = song_len();
    const int playing = tack_running() && tack_song_mode() == TACK_SONG_ON ? tack_song_pos() : -1;
    uint32_t key = rk(rk(rk(19u, first), ui.song_row), (uint32_t)playing);
    for (uint32_t i = 0; i < 8u; i++)
        key = rk(key, tack_song_get(first + i));
    key = rk(key, len);
    if (key == *last)
        return;
    *last = key;
    cv_begin(240, PG_RH, C_BLACK);
    for (uint32_t i = 0; i < 8u && first + i <= len; i++) {
        const uint32_t row = first + i;
        const uint8_t v = tack_song_get(row);
        const int32_t x = 15 + (int32_t)i * 30;
        char t[5];
        int_text(v + 1, t);
        if (v >= TACK_PATTERNS) {
            t[0] = v == TACK_SONG_LOOP ? 'L' : 'S';
            t[1] = 0;
        }
        if (row == ui.song_row)
            cv_rect(x - 14, 16, 28, 34, C_HI);
        else
            cv_rect(x - 14, 16, 28, 34, C_LINE);
        cv_text(x - text_w(&FONT_B, t) / 2, 25, &FONT_B, t, row == ui.song_row ? C_BLACK : v < TACK_PATTERNS ? C_WHITE : C_GRAY);
        if ((int)row == playing)
            cv_rect(x - 14, 54, 28, 3, C_WHITE);
    }
    cv_blit(0, PG_Y + (line + 1u) * PG_RH);
}

/* One voice's bar, in the white keys' order: two rows of eight, split at the beat. Each step is a box, with its velocity as
 * the bar's height, and under it a ruler that its nudge moves along */
#define VV_H 78
#define VV_W 26
#define VV_BOX 62
#define VV_BAR 46
#define VV_FOOT (2 + VV_BOX)                     /* below the box */
#define C_STEP RGB(16, 17, 21)                   /* a box: only enough to say a step is there */

/* NOTE's steps stand on the octave of the lowest note in the bar, three octaves to the height of a box */
static int note_low(void)
{
    int low = 127;
    for (uint32_t i = 0; i < 16u; i++) {
        const uint8_t s = (uint8_t)(ui.bar * 16 + (int)i);
        if (tack_step_on((uint8_t)ui.voice, s) && tack_step_get((uint8_t)ui.voice, s, TACK_ST_NOTE) < low)
            low = tack_step_get((uint8_t)ui.voice, s, TACK_ST_NOTE);
    }
    return low == 127 ? 48 : low / 12 * 12;
}

static void vv_cell(int s, int32_t x, int len, int head, int low)
{
    const uint8_t v = (uint8_t)ui.voice, st = (uint8_t)s;
    const uint16_t c = VOICE_COLOUR[ui.voice];
    char t[6];
    if (s >= len) {
        cv_rect(x + VV_W / 2 - 1, VV_H / 2, 2, 3, C_DIM);
        return;
    }
    cv_rect(x, 2, VV_W, VV_BOX, C_STEP);
    if (tack_step_on(v, st)) {
        const int vel = tack_step_get(v, st, TACK_ST_VEL), nudge = tack_step_get(v, st, TACK_ST_NUDGE);
        const int chance = tack_step_get(v, st, TACK_ST_PROB), flam = tack_step_get(v, st, TACK_ST_FLAM);
        const int locks = tack_step_locks(v, st), rlen = tack_recur_len(v, st), mask = tack_recur_mask(v, st);
        const int32_t h = VV_BAR * vel / 127;
        const int skip = tack_recur_skips(v, st), slot_now = tack_recur_slot(v, st);
        const uint16_t tone = skip ? C_GRAY : chance < 100 ? (uint16_t)((c >> 1) & 0x7BEFu) : c;
        if (note_view()) {
            const int note = tack_step_get(v, st, TACK_ST_NOTE);
            const int32_t rise = clamp((note - low) * VV_BAR / 36, 0, VV_BAR - 4);
            cv_rect(x + 1, VV_FOOT - 8 - rise, VV_W - 2, 4, tone);
            note_text(note, t);
            cv_text(x + VV_W / 2 - text_w(&FONT_XS, t) / 2, 4, &FONT_XS, t, C_WHITE);
        } else {
            cv_rect(x, VV_FOOT - 4 - h, VV_W, h, tone);
        }
        cv_rect(x, VV_FOOT + 6, VV_W, 1, C_DIM);
        cv_rect(x + VV_W / 2 - 1, VV_FOOT + 3, 2, 7, C_GRAY);
        if (nudge)
            cv_rect(x + VV_W / 2 - 2 + (nudge * 10 + (nudge < 0 ? -20 : 20)) / 21, VV_FOOT + 2, 4, 9, C_WHITE);
        if (locks && !note_view()) {
            t[0] = 'L';
            int_text(locks, t + 1);
            cv_text(x + VV_W / 2 - text_w(&FONT_XS, t) / 2, 4, &FONT_XS, t, C_WHITE);
        }
        if (flam && !note_view()) {
            cv_text(x + VV_W / 2 - text_w(&FONT_XS, tack_flam_name(flam)) / 2, 26, &FONT_XS, tack_flam_name(flam), C_WHITE);
        }
        if (chance < 100 && !note_view()) {
            int_text(chance, t);
            cv_text(x + VV_W / 2 - text_w(&FONT_XS, t) / 2, 40, &FONT_XS, t, C_WHITE);
        }
        if (rlen > 1)
            for (int k = 0; k < rlen; k++)
                cv_rect(x + 1 + k * 3, k == slot_now ? VV_FOOT - 6 : VV_FOOT - 3, 2, k == slot_now ? 6 : 3, mask >> k & 1 ? C_WHITE : C_GRAY);
    } else if (vel_page()) {                     /* written here too: what a new step will play */
        const int32_t h = VV_BAR * tack_step_get(v, st, TACK_ST_VEL) / 127;
        cv_rect(x, VV_FOOT - 4 - h, VV_W, h, C_DIM);
    }
    if (s == head)
        outline(x, 2, VV_W, VV_BOX, 2, C_WHITE);
    else if (s == (int)note_cursor() && !arp_page())
        outline(x, 2, VV_W, VV_BOX, 2, C_HI);
}

static void pg_voice_line(uint32_t line, uint32_t *last)
{
    const uint8_t v = (uint8_t)ui.voice;
    const int head = tack_running() ? tack_playhead(v) : -1, len = tack_length(v);
    const int low = note_low();
    uint32_t key = rk(rk(rk(rk(rk(23u, v), (uint32_t)ui.bar), (uint32_t)len), note_cursor()), (uint32_t)low + 1000u * ui.over);
    for (uint32_t i = 0; i < 8u; i++) {
        const uint8_t st = (uint8_t)(ui.bar * 16 + (int)(line * 8u + i));
        key = rk(key, st == head);
        key = rk(key, (uint32_t)tack_step_on(v, st));
        for (uint32_t k = 0; k < TACK_ST_SLOTS; k++)
            key = rk(key, (uint32_t)tack_step_get(v, st, k));
        key = rk(key, (uint32_t)tack_step_locks(v, st));
        key = rk(key, (uint32_t)tack_recur_len(v, st) * 256u + tack_recur_mask(v, st));
        key = rk(key, (uint32_t)(tack_recur_slot(v, st) + 1));
    }
    if (key == *last)
        return;
    *last = key;
    cv_begin(240, VV_H + 2, C_BLACK);
    for (uint32_t i = 0; i < 8u; i++)
        vv_cell(ui.bar * 16 + (int)(line * 8u + i), EDGE + (int32_t)i * 28 + (i >= 4u ? 10 : 0), len, head, low);
    cv_blit(0, PG_Y + line * (VV_H + 2u));
}

/* one column of what is under the steps; a max of 0 has no bar */
static void vv_dial(uint32_t i, const char *name, const char *word, int lit, int value, int max)
{
    const int32_t x = CELL_X(i), mid = x + CELL_W / 2;
    cv_text(mid - text_w(&FONT_XS, name) / 2, 4, &FONT_XS, name, PG_TEXT);
    cv_text(mid - text_w(&FONT_B, word) / 2, 20, &FONT_B, word, lit ? C_WHITE : C_GRAY);
    if (max)
        value_bar(x + 5, 40, CELL_W - 10, 4, (uint32_t)value, (uint32_t)max, VOICE_COLOUR[ui.voice]);
}

static void vv_nudge(uint32_t i, int value)
{
    const int32_t x = CELL_X(i), mid = x + CELL_W / 2;
    cv_rect(x + 5, 41, CELL_W - 10, 2, C_LINE);
    cv_rect(mid - 1, 38, 2, 8, C_GRAY);
    if (value)
        cv_rect(mid - 2 + (value * 22 + (value < 0 ? -20 : 20)) / 21, 38, 4, 8, VOICE_COLOUR[ui.voice]);
}

/* the velocity page: the same bars, and what will be written into them */
static void pg_vel_dials(uint32_t *last)
{
    const int on = ui.vs_shape != 0;
    const char *name = tack_shape_name(ui.vs_shape);
    char t[8];
    const uint32_t key = rk(rk(rk(rk(31u, ui.vs_shape), ui.vs_shift), ui.vs_centre), ui.vs_amount);
    if (key == *last)
        return;
    *last = key;
    cv_begin(240, 59, C_BLACK);
    row_cells(0xFu, 0, 59);
    cv_text(CELL_X(0) + CELL_W / 2 - text_w(&FONT_XS, "SHAPE") / 2, 4, &FONT_XS, "SHAPE", PG_TEXT);
    cv_text(CELL_X(0) + CELL_W / 2 - text_w(&FONT_S, name) / 2, 22, &FONT_S, name, on ? C_WHITE : C_GRAY);
    int_text(ui.vs_centre, t);
    vv_dial(1u, "CENTRE", t, on, ui.vs_centre, 127);
    int_text(ui.vs_amount, t);
    vv_dial(2u, "AMOUNT", t, on, ui.vs_amount, 100);
    int_text(ui.vs_shift, t);
    vv_dial(3u, "SHIFT", t, on, 0, 0);
    cv_blit(0, PG_Y + 2u * (VV_H + 2u));
}

/* how the selected lane plays */
static void pg_lane_dials(uint32_t *last)
{
    const uint8_t v = (uint8_t)ui.voice;
    const int nudge = tack_lane_nudge_get(v), len = tack_length(v);
    const uint32_t dir = tack_lane_dir(v), speed = tack_lane_speed(v);
    char t[8];
    const int start = lane_start_now(v);
    const uint32_t key = rk(rk(rk(rk(rk(rk(37u, v), (uint32_t)(nudge + 32)), (uint32_t)len), dir), speed), (uint32_t)start);
    if (key == *last)
        return;
    *last = key;
    cv_begin(240, 59, C_BLACK);
    row_cells(0xFu, 0, 59);
    int_text(len, t);
    vv_dial(0, "LENGTH", t, 1, len, TACK_BARS * 16);
    vv_dial(1, "DIRECTION", tack_lane_dir_name(dir), 1, 0, 0);
    vv_dial(2, "SPEED", tack_lane_speed_name(speed), 1, 0, 0);
    int_text(start + 1, t);
    vv_dial(3, "START", t, 1, 0, 0);
    if (nudge) {                                 /* ALGORITHM's: most lanes have none */
        t[0] = nudge < 0 ? '-' : '+';
        int_text(nudge < 0 ? -nudge : nudge, t + 1);
        cv_text(5, 47, &FONT_XS, "NUDGE", C_GRAY);
        cv_text(5 + text_w(&FONT_XS, "NUDGE") + 5, 47, &FONT_XS, t, C_WHITE);
    }
    cv_blit(0, PG_Y + 2u * (VV_H + 2u));
}

/* the Euclidean generator */
static void pg_eu_dials(uint32_t *last)
{
    static const char *const NAME[TACK_EU_SLOTS] = {"LENGTH", "HITS", "ROTATE"};
    const uint8_t v = (uint8_t)ui.voice;
    const int back = tack_euclid_can_undo(v);
    char t[8];
    uint32_t key = rk(rk(41u, v), (uint32_t)back);
    for (uint32_t i = 0; i < TACK_EU_SLOTS; i++)
        key = rk(key, (uint32_t)tack_euclid_get(v, i));
    if (key == *last)
        return;
    *last = key;
    cv_begin(240, 59, C_BLACK);
    row_cells(0xFu, 0, 59);
    for (uint32_t i = 0; i < TACK_EU_SLOTS; i++) {
        int_text(tack_euclid_get(v, i), t);
        vv_dial(i, NAME[i], t, 1, tack_euclid_get(v, i), TACK_EU_MAX);
    }
    vv_dial(3, "UNDO", back ? "BACK" : "-", back, 0, 0);
    cv_blit(0, PG_Y + 2u * (VV_H + 2u));
}

/* the selected step, under the steps */
static void pg_voice_dials(uint32_t *last)
{
    static const char *const NAME[TACK_NP] = {"VELOCITY", "NUDGE", "FLAM", "CHANCE"};
    static const uint8_t TOP[TACK_NP] = {127, 0, 5, 100};
    const uint8_t v = (uint8_t)ui.voice, st = (uint8_t)note_cursor();
    const int on = tack_step_on(v, st);
    uint32_t key = rk(rk(rk(29u, v), st), (uint32_t)on);
    for (uint32_t i = 0; i < TACK_NP; i++)
        key = rk(key, (uint32_t)(tack_step_get(v, st, VV_SLOT[i]) + 8));
    if (key == *last)
        return;
    *last = key;
    cv_begin(240, 59, C_BLACK);
    row_cells(0xFu, 0, 59);
    for (uint32_t i = 0; i < TACK_NP; i++) {
        const int value = on ? tack_step_get(v, st, VV_SLOT[i]) : 0;
        const char *word = "-";
        char t[8];
        if (on && VV_SLOT[i] == TACK_ST_FLAM) {
            word = tack_flam_name(value);
        } else if (on) {
            t[0] = value < 0 ? '-' : '+';
            int_text(value < 0 ? -value : value, VV_SLOT[i] == TACK_ST_NUDGE && value ? t + 1 : t);
            word = t;
        }
        vv_dial(i, NAME[i], word, on, value, TOP[i]);
        if (VV_SLOT[i] == TACK_ST_NUDGE)
            vv_nudge(i, value);
    }
    cv_blit(0, PG_Y + 2u * (VV_H + 2u));
}

/* NOTE's four, and under them the keys' range and what they do */
static void pg_note_dials(uint32_t *last)
{
    const uint8_t v = (uint8_t)ui.voice, st = (uint8_t)note_cursor();
    const int on = tack_step_on(v, st), rec = tack_recording(), run = tack_running();
    const int note = on ? tack_step_get(v, st, TACK_ST_NOTE) : 0, vel = on ? tack_step_get(v, st, TACK_ST_VEL) : 0;
    const char *const say = rec ? (run ? "RECORDING" : "REC ON") : "REC TO WRITE";
    char t[8];
    uint32_t key = rk(rk(rk(rk(31u, v), st), (uint32_t)on), (uint32_t)note);
    key = rk(rk(rk(key, (uint32_t)vel), ui.note_vel), (uint32_t)(ui.note_oct + 8));
    key = rk(rk(key, (uint32_t)rec), (uint32_t)run);
    if (key == *last)
        return;
    *last = key;
    cv_begin(240, 59, C_BLACK);
    row_cells(0xFu, 0, 59);
    note_text(note, t);
    vv_dial(0, "NOTE", on ? t : "-", on, 0, 0);
    int_text(vel, t);
    vv_dial(1, "VELOCITY", on ? t : "-", on, vel, 127);
    int_text(ui.note_vel, t);
    vv_dial(2, "KEY VEL", t, 1, ui.note_vel, 127);
    vv_dial(3, "HIT", on ? "ON" : "OFF", on, 0, 0);
    t[0] = 'O', t[1] = 'C', t[2] = 'T', t[3] = ' ';
    int_text(ui.note_oct, t + 4);
    cv_text(5, 47, &FONT_XS, t, C_GRAY);
    cv_text(240 - EDGE - 3 - text_w(&FONT_XS, say), 47, &FONT_XS, say, rec ? C_HI : C_GRAY);
    cv_blit(0, PG_Y + 2u * (VV_H + 2u));
}

/* the whole screen; invalid: all of it, as it comes from another page */
static void pg_screen(int invalid)
{
    static uint32_t rows[SCREEN_ROWS];
    static int was;
    const int now = ui.over == PAGE_FX ? 1 : ui.over == PAGE_SET ? 2 : vel_page() ? 6 : ui.over == PAGE_LANE ? 7 : ui.over == PAGE_EUCLID ? 8
                    : ui.over == PAGE_SAVE ? (ui.ks_what == SAVE_PATTERN ? 10 : 9) : song_pages() ? 3 : voice_page() ? 4 : note_view() ? 5 : 0;
    if (invalid || now != was) {
        lcd_fill(0, 0, 240, 240, C_BLACK);
        rows[0] = rows[1] = rows[2] = 0xFFFFFFFFu;
        was = now;
    }
    pg_top();
    if (now == 10) {
        pg_save_top(&rows[0]);
        pg_save_slots(&rows[1]);
        pg_save_note(&rows[2]);
    } else if (now == 9) {
        pg_save_top(&rows[0]);
        pg_save_name(&rows[1]);
        pg_save_note(&rows[2]);
    } else if (now == 1) {
        pg_fx_sends(&rows[0]);
        pg_fx_row(1, &rows[1]);
        pg_fx_row(2, &rows[2]);
    } else if (now == 2) {
        pg_set_load(&rows[0]);
        pg_set_row(&rows[1]);
        pg_set_usb(&rows[2]);
    } else if (now >= 4) {
        pg_voice_line(0, &rows[0]);
        pg_voice_line(1, &rows[1]);
        if (now == 6)
            pg_vel_dials(&rows[2]);
        else if (now == 7)
            pg_lane_dials(&rows[2]);
        else if (now == 8)
            pg_eu_dials(&rows[2]);
        else if (now == 5)
            pg_note_dials(&rows[2]);
        else
            pg_voice_dials(&rows[2]);
    } else if (now == 3) {
        pg_song_top(&rows[0]);
        pg_song_rows(0, &rows[1]);
        pg_song_rows(1, &rows[2]);
    } else {
        for (uint32_t r = 0; r < SCREEN_ROWS; r++)
            pg_row(r, screen_page(screen_now(), (int)r), &rows[r]);
    }
}
