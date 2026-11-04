/* SPDX-License-Identifier: GPL-3.0-only */
/* The sound pages: their rows and top bar. app.c includes this: it is one file to the compiler, and shares app.c's state */

static uint8_t pg_value(const TackSlot *sl, int on, uint32_t i, int *lit)
{
    uint8_t value = tack_get_param(sl->par);
    *lit = 0;
    if (held_shown()) {
        *lit = tack_lock_get((uint8_t)ui.voice, ui.held_step, sl->par, &value);
        if (on)
            value = ui.held_val[i];
    }
    return value;
}

/* how many of a page's first slots its picture takes in: the parameters that shape it */
enum { PIC_NONE, PIC_ENV, PIC_OSC, PIC_LFO, PIC_FILTER };

/* a page's shared picture, and which of its slots it takes in */
static int pg_picture(int page, uint32_t *first, uint32_t *n)
{
    const int g = tack_page_graph((uint32_t)ui.voice, ui.group, (uint32_t)page);
    *first = 0;
    *n = 0;
    if (g == TACK_GRAPH_AMP || g == TACK_GRAPH_MOD) {
        *n = g == TACK_GRAPH_AMP ? 3u : 2u;
        return PIC_ENV;
    }
    if (g == TACK_GRAPH_OSC) {
        *first = 1;
        *n = 2;
        return PIC_OSC;
    }
    if (g == TACK_GRAPH_LFO && page_slot(page, 0) && page_slot(page, 0)->target) {   /* (not LFO SYNC, whose graph it shares) */
        *first = 1;
        *n = 3;
        return PIC_LFO;
    }
    if (tack_page_kind((uint32_t)ui.voice, ui.group, (uint32_t)page) == TACK_PAGE_FILTER) {
        *n = 2;
        return PIC_FILTER;
    }
    return PIC_NONE;
}

static void pg_draw_picture(int page, int kind, uint32_t first, uint32_t n, uint16_t c)
{
    const int32_t x = (int32_t)first * PG_CW + 10, w = (int32_t)n * PG_CW - 20;
    const int g = tack_page_graph((uint32_t)ui.voice, ui.group, (uint32_t)page);
    float v[4];
    if (kind == PIC_ENV) {
        wd_env(x, 0, w, g, c);
    } else if (kind == PIC_OSC) {
        v[0] = graph_value(g, 1, 0);
        v[1] = (float)graph_value(g, 2, 127) / 127.0f;
        wd_curve(x, 0, w, osc_picture, v, c);
    } else if (kind == PIC_LFO) {
        v[0] = (float)graph_value(g, 1, 0) / 127.0f;
        v[1] = graph_value(g, 3, 0);
        v[2] = lfo_cycles();
        v[3] = (float)tack_lfo_once((uint32_t)ui.voice);
        wd_curve(x, 0, w, lfo_picture, v, c);
    } else {
        v[0] = (float)tack_get_param(page_slot(page, 0)->par) / 127.0f;
        v[1] = (float)tack_get_param(page_slot(page, 1)->par) / 127.0f;
        v[2] = tack_get_param(page_slot(page, 2)->par);
        wd_curve(x, 0, w, filter_shape, v, c);
    }
}

/* PULSE sets the width of the PWM wave alone: on any other it does nothing, and looks it */
static int pulse_idle(int page, uint32_t i)
{
    return i == 2u && tack_page_graph((uint32_t)ui.voice, ui.group, (uint32_t)page) == TACK_GRAPH_OSC &&
           graph_value(TACK_GRAPH_OSC, 1, 0) != 5u;
}

static void pg_row(uint32_t r, int page, uint32_t *last)
{
    const uint8_t voice = (uint8_t)ui.voice;
    const int on = page == row_now();
    uint32_t first = 0, span = 0, key = 2166136261u;
    const int picture = page == NO_PAGE ? PIC_NONE : pg_picture(page, &first, &span);
#define RKEY(v) (key = (key ^ (uint32_t)(v)) * 16777619u)
    RKEY(page);
    RKEY(on);
    RKEY(voice);
    RKEY(held_shown());
    RKEY(tack_lfo_sync(voice));                /* the picture on the row above it */
    RKEY(tack_lfo_once(voice));
    for (uint32_t i = 0; page != NO_PAGE && i < TACK_NP; i++) {
        RKEY(pulse_idle(page, i));
        const TackSlot *sl = page_slot(page, i);
        int lit;
        if (sl) {
            RKEY(pg_value(sl, on, i, &lit));
            RKEY(lit);
            RKEY((uintptr_t)page_label(page, sl));
        }
    }
#undef RKEY
    if (key == *last)
        return;
    *last = key;
    cv_begin(240, PG_RH, C_BLACK);
    if (on) {
        uint32_t dials = 0;
        for (uint32_t i = 0; i < TACK_NP; i++)
            dials |= (uint32_t)(page_slot(page, i) != 0 && !pulse_idle(page, i)) << i;
        row_lines(dials, VOICE_COLOUR[voice]);
    }
    if (picture)
        pg_draw_picture(page, picture, first, span, on ? VOICE_COLOUR[voice] : C_GRAY);
    for (uint32_t i = 0; i < TACK_NP; i++) {
        const TackSlot *sl = page == NO_PAGE ? 0 : page_slot(page, i);
        const int32_t x = PG_CW / 2 + (int32_t)i * PG_CW;
        const uint16_t c = on ? VOICE_COLOUR[voice] : C_GRAY;
        const char *name, *label;
        int lit;
        uint8_t value;
        if (!sl) {
            wd_empty(x, 0);
            continue;
        }
        value = pg_value(sl, on, i, &lit);
        label = page_label(page, sl);
        cv_text(x - text_w(&FONT_B, label) / 2, PG_PH, &FONT_B, label, pulse_idle(page, i) ? C_DIM : on ? C_WHITE : PG_TEXT);
        if (i >= first && i < first + span)
            continue;
        name = tack_slot_text(sl, value);
        if (sl->target)
            wd_target(x, 0, name, lit ? C_HI : c);
        else if (tack_slot_wave(sl))
            wd_wave(x, 0, tack_slot_wave(sl), value, lit ? C_HI : c);
        else if (tack_slot_switch(sl))
            wd_toggle(x, 0, name[1] == 'N', lit ? C_HI : c);
        else if (name)
            wd_box(x, 0, name, lit ? C_HI : c);
        else if (is_signed(sl))
            wd_signed(x, 0, sl, value, lit ? C_HI : c, on ? C_GRAY : C_DIM, on ? C_WHITE : PG_TEXT);
        else
            wd_dial(x, 0, value, sl->max, lit ? C_HI : c, on ? C_GRAY : C_DIM);
    }
    cv_blit(0, PG_Y + r * PG_RH);
}

#define PG_TAB_GAP 3

static void pg_top(void)
{
    static const char *const RETRIG_NAME[6] = {"DRUM 1", "DRUM 2", "DRUM 3", "SNARE", "CYMBAL", "HI-HAT"};
    char t[16];
    cv_begin(240, 20, C_BLACK);
    cv_rect(0, 19, 240, 1, C_DIM);
    if (ui.msg_until) {
        cv_text(EDGE, TOP_LINE - 2, &FONT_B, ui.msg, C_WHITE);
    } else if (key_word()) {
        cv_text(EDGE, TOP_LINE - 2, &FONT_B, key_word(), C_HI);
    } else if (ui.touched_until && ui.touched) {     /* the exact value, while it turns */
        const TackSlot *sl = ui.touched;
        const uint8_t v = ui.held >= 0 ? ui.held_val[ui.touched_at] : tack_get_param(sl->par);
        const char *name = tack_slot_text(sl, v);
        cv_text(EDGE, TOP_LINE - 2, &FONT_B, page_label(ui.touched_row, sl), C_WHITE);
        if (name && v && sl->role == TACK_ROLE_RETRIG)  /* the box has room for D1, and here there is for the voice */
            name = RETRIG_NAME[(v - 1u) % 6u];
        if (!name)
            value_text(sl, v, t);
        cv_text(240 - EDGE - text_w(&FONT_B, name ? name : t), TOP_LINE - 2, &FONT_B, name ? name : t, C_WHITE);
        if (!name && is_pan(sl))
            slot_bar(0, 18, 240, 2, sl, v, VOICE_COLOUR[ui.voice]);
        else if (!name && sl->max)
            cv_rect(0, 18, 240 * (int32_t)v / sl->max, 2, VOICE_COLOUR[ui.voice]);
    } else if (held_shown()) {                       /* the step's recurrence, where the keys show it */
        const uint8_t st = ui.held_step;
        const int len = tack_recur_len((uint8_t)ui.voice, st), mask = tack_recur_mask((uint8_t)ui.voice, st);
        t[0] = 'S';
        int_text(st + 1, t + 1);
        cv_text(EDGE, TOP_LINE - 2, &FONT_B, t, C_WHITE);
        for (int k = 0; k < len; k++)
            cv_rect(240 - (8 - k) * 16, 4, 12, 11, mask >> k & 1 ? C_HI : C_DIM);
    } else {
        if (ui.over == PAGE_FX || ui.over == PAGE_SET || ui.over == PAGE_SONG || ui.over == PAGE_SAVE)
            cv_text(EDGE, TOP_LINE - 2, &FONT_B, ui.over == PAGE_FX ? "FX" : ui.over == PAGE_SET ? "SETTINGS" : ui.over == PAGE_SAVE ? "SAVE" : "SONG", C_HI);
        else
            cv_text(EDGE, TOP_LINE - 2, &FONT_B, VOICE_NAME[ui.voice], VOICE_COLOUR[ui.voice]);
        t[0] = 'P';
        int_text(tack_pattern() + 1, t + 1);
        cv_text(92, TOP_LINE - 2, &FONT_B, t, tack_running() ? C_HI : C_WHITE);
        if (arp_page())
            cv_text(122, TOP_LINE - 2, &FONT_B, vel_page() ? "VEL SHAPE" : ui.over == PAGE_EUCLID ? "EUCLID" : "LANE", C_HI);
        if (seq_detail()) {
            if (tack_song_mode())                  /* what picks the pattern: the steps show whichever is playing */
                cv_text(EDGE + 4 + text_w(&FONT_B, VOICE_NAME[ui.voice]), TOP_LINE - 1, &FONT_XS, tack_song_mode() == TACK_SONG_ON ? "SONG" : "CHN", C_HI);
            cv_text(122, TOP_LINE - 2, &FONT_B, ui.note_mode ? "NOTE" : "STEP", ui.note_mode ? C_HI : C_WHITE);
            t[0] = 'B';
            int_text(ui.bar + 1, t + 1);
            cv_text(162, TOP_LINE - 1, &FONT_XS, t, C_GRAY);
            t[0] = 'S';
            int_text((int32_t)note_cursor() + 1, t + 1);
            cv_text(182, TOP_LINE - 1, &FONT_XS, t, C_WHITE);
        }
        int_text(tack_bpm(), t);
        cv_text(240 - EDGE - text_w(&FONT_B, t), TOP_LINE - 2, &FONT_B, t, C_HI);
    }
    {                                                /* a page with more than one mode: the line under the bar in as many parts */
        const int n = arp_page() ? 3 : seq_detail() || (sound_page() && screens() > 1) ? 2 : 0;
        const int at = arp_page() ? (int)ui.over - PAGE_LANE : seq_detail() ? ui.note_mode != 0 : screen_now();
        if (n && !(ui.touched_until && ui.touched)) {
            const int32_t w = (240 - (n - 1) * PG_TAB_GAP) / n;
            for (int i = 0; i < n; i++)
                cv_rect(i * (w + PG_TAB_GAP), 18, w, 2, i == at ? C_HI : C_GRAY);
        }
    }
    cv_blit(0, 0);
}
