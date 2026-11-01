/* SPDX-License-Identifier: GPL-3.0-only */
/* The overview: header colours, the grid of lanes, the dials' strip and the bottom strip. app.c includes this: it is one file to the compiler, and shares app.c's state */

#define REC_RED RGB(232, 58, 46)
static void outline(int32_t x, int32_t y, int32_t w, int32_t h, int32_t t, uint16_t c);

#define GRID_X 12
#define GRID_CELL 14
#define GRID_ROW 11
#define GRID_H 82                         /* to the knobs: it clears what a full page left there */

#define GRID_FLOOR 3                      /* the height of a step at the lowest velocity */
#define C_BEAT RGB(46, 48, 58)            /* a cell on the beat: the bar reads at a glance */

/* every lane on one page: a row each, marked in its voice's colour and the selected lane in full, the page's steps. A step's
 * height is its velocity. Past a lane's end there is nothing */
static void draw_grid(void)
{
    uint32_t v, i;
    cv_begin(240, GRID_H, C_BLACK);
    for (v = 0; v < TACK_VOICES; v++) {
        const int32_t y = (int32_t)v * GRID_ROW;
        const int muted = tack_muted((uint8_t)v);
        const int head = tack_running() ? tack_playhead((uint8_t)v) : -1;
        const int len = tack_length((uint8_t)v);
        cv_rect(2, y + 1, (int32_t)v == ui.voice ? 7 : 2, GRID_ROW - 3, muted ? C_GRAY : VOICE_COLOUR[v]);
        for (i = 0; i < 16u; i++) {
            const int s = ui.bar * 16 + (int)i;
            const int32_t x = GRID_X + (int32_t)i * GRID_CELL;
            if (s >= len) {                      /* past the lane's end: it stays a row */
                cv_rect(x + 5, y + 3, 2, 3, C_DIM);
                continue;
            }
            const int on = tack_step_on((uint8_t)v, (uint8_t)s);
            const int rest = on && (muted || tack_recur_skips((uint8_t)v, (uint8_t)s));   /* grey: not this pass */
            cv_rect(x, y, GRID_CELL - 2, GRID_ROW - 2, i % 4u ? C_LINE : C_BEAT);
            if (on) {
                const int32_t h = GRID_FLOOR + (GRID_ROW - 2 - GRID_FLOOR) * tack_step_get((uint8_t)v, (uint8_t)s, TACK_ST_VEL) / 127;
                cv_rect(x, y + GRID_ROW - 2 - h, GRID_CELL - 2, h, s == head ? C_WHITE : rest ? C_GRAY : VOICE_COLOUR[v]);
            } else if (s == head) {
                cv_rect(x, y + GRID_ROW - 4, GRID_CELL - 2, 2, C_WHITE);
            }
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
#define KNOBS_Y 134
#define KNOBS_H 36
#define BOTTOM_Y (KNOBS_Y + KNOBS_H)
#define BOTTOM_H (240 - BOTTOM_Y)
#define GRAPH_X 10
#define GRAPH_TOP 2
#define GRAPH_H 50
#define GRAPH_W 220
#define ROW_Y 54                         /* the name */
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
static void knob_num(int32_t x, const char *t, uint16_t colour) { cv_text(x, 16, &FONT_S, t, colour); }
static void knob_word(int32_t x, const char *t, uint16_t colour) { cv_text(x, 16, &FONT_XS, t, colour); }

/* how far a value is along its range */
static void value_bar(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t v, uint32_t max, uint16_t colour)
{
    cv_rect(x, y, w, h, C_LINE);
    if (max)
        cv_rect(x, y, (int32_t)((v > max ? max : v) * (uint32_t)w / max), h, colour);
}

/* Pan is stored 0-127 and the middle is 63.5, half a step from 63 and from 64, so both are the middle: L63, C, R63 */
static int is_pan(const TackSlot *sl)
{
    const char *l = sl->label;
    return l[0] == 'P' && l[1] == 'A' && l[2] == 'N' && !l[3];
}

static int32_t pan_side(uint32_t v)              /* -63 left to 63 right */
{
    return v < 63u ? (int32_t)v - 63 : v > 64u ? (int32_t)v - 64 : 0;
}

static void pan_text(uint32_t v, char *b)
{
    const int32_t s = pan_side(v);
    if (!s) {
        b[0] = 'C';
        b[1] = 0;
    } else {
        b[0] = s < 0 ? 'L' : 'R';
        int_text(s < 0 ? -s : s, b + 1);
    }
}

static int is_signed(const TackSlot *sl) { return is_pan(sl) || sl->centre; }

static int32_t slot_side(const TackSlot *sl, uint32_t v)   /* how far from the middle, below it negative */
{
    return is_pan(sl) ? pan_side(v) : (int32_t)v - sl->centre;
}

static int32_t slot_reach(const TackSlot *sl, int32_t s)   /* the most that side has */
{
    return is_pan(sl) ? 63 : s < 0 ? sl->centre : (int32_t)sl->max - sl->centre;
}

static void value_text(const TackSlot *sl, uint32_t v, char *b)
{
    if (is_pan(sl)) {
        pan_text(v, b);
    } else if (sl->centre) {
        const int32_t s = slot_side(sl, v);
        if (s > 0)
            *b++ = '+';
        int_text(s, b);
    } else {
        int_text((int32_t)v, b);
    }
}

static void slot_bar(int32_t x, int32_t y, int32_t w, int32_t h, const TackSlot *sl, uint32_t v, uint16_t colour)
{
    const int32_t mid = x + w / 2, s = is_signed(sl) ? slot_side(sl, v) : 0;
    int32_t n;
    if (!is_signed(sl)) {
        value_bar(x, y, w, h, v, sl->max, colour);
        return;
    }
    cv_rect(x, y, w, h, C_LINE);
    n = s ? (s < 0 ? -s : s) * (w / 2) / slot_reach(sl, s) : 0;
    cv_rect(s < 0 ? mid - n : mid, y, n, h, colour);
    cv_rect(mid - 1, y, 2, h, colour);           /* where the middle is */
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

static int32_t graph_w = GRAPH_W;        /* beside what the overview keeps there, narrower */

static int32_t log2_px(float t)          /* a width that grows with the log of the time */
{
    int32_t n = 0;
    while (t >= 2.0f) {
        t *= 0.5f;
        n++;
    }
    n = 12 + (int32_t)(12.0f * ((float)n + t - 1.0f));
    return n > graph_w ? graph_w : n;
}

static void graph_column(int32_t x, float y)    /* y 0-1 */
{
    const int32_t h = 1 + (int32_t)(y * (float)(GRAPH_H - 1));
    cv_rect(GRAPH_X + x, GRAPH_TOP + GRAPH_H - h, 1, h, (uint16_t)((VOICE_COLOUR[ui.voice] >> 2) & 0x39E7u));
    cv_rect(GRAPH_X + x, GRAPH_TOP + GRAPH_H - h, 1, 2, VOICE_COLOUR[ui.voice]);
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
    switch (wave) {
    case 0: return lfo_shape(0, p);
    case 1: return lfo_shape(1, p);
    case 2: return lfo_shape(2, p);
    case 3: return lfo_shape(4, p);
    case 4:                                          /* the same picture each time: a dial that moves it would look like it did something */
        return (float)(int32_t)((((uint32_t)x + 1u) * 2654435761u) >> 24) / 127.5f - 1.0f;
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

static const char *page_name(void)
{
    if (ui.held >= 0)
        return "STEP LOCKS";
    return tack_page_title((uint32_t)ui.voice, ui.group, (uint32_t)row_now());
}

/* a modulation target's name is too long for its column: it goes on the last row. The slot, or 0 */
static const TackSlot *target_slot(void)
{
    uint32_t i;
    if (ui.fx_page || ui.set_page || ui.song_page)
        return 0;
    for (i = 0; i < TACK_NP; i++)
        if (slot(i) && slot(i)->target)
            return slot(i);
    return 0;
}

#define SONG_CELLS 16
static uint32_t song_first(void) { return ui.song_row / SONG_CELLS * SONG_CELLS; }

/* The overview's bottom right, beside the envelope: the sixteen patterns as two rows of the white keys, and what picks the next */
#define HOME_X 128
#define HOME_CELL 11
#define HOME_PITCH 13
#define HOME_ENV_W 108

static int home_info(void) { return ui.group == TACK_G_MAIN && !ui.fx_page && !ui.set_page && !ui.song_page && ui.held < 0; }

static void draw_home_info(uint32_t used)
{
    const int now = tack_pattern(), next = tack_pattern_next(), mode = tack_song_mode();
    char t[12];
    uint32_t n = 0;
    for (uint32_t p = 0; p < TACK_PATTERNS; p++) {
        const int32_t x = HOME_X + (int32_t)(p % 8u) * HOME_PITCH, y = 3 + (int32_t)(p / 8u) * HOME_PITCH;
        cv_rect(x, y, HOME_CELL, HOME_CELL, (int)p == now ? C_HI : used >> p & 1u ? C_GRAY : C_LINE);
        if ((int)p == next)
            outline(x, y, HOME_CELL, HOME_CELL, 1, C_WHITE);
    }
    if (mode) {                                  /* the row playing of how many */
        memcpy(t, mode == TACK_SONG_ON ? "SONG " : "CHAIN ", 7u);
        n = mode == TACK_SONG_ON ? 5u : 6u;
        int_text(tack_song_pos() + 1, t + n);
        while (t[n])
            n++;
        t[n++] = '/';
        int_text(mode == TACK_SONG_ON ? (int32_t)song_len() : (int32_t)tack_chain_len(), t + n);
        cv_text(HOME_X, 32, &FONT_XS, t, C_HI);
    } else {
        memcpy(t, next >= 0 ? "NEXT P" : "LOOP P", 7u);
        int_text((next >= 0 ? next : now) + 1, t + 6);
        cv_text(HOME_X, 32, &FONT_XS, t, next >= 0 ? C_WHITE : C_GRAY);
    }
    memcpy(t, "SHUF ", 6u);
    int_text(tack_shuffle(), t + 5);
    cv_text(HOME_X, 48, &FONT_XS, t, tack_shuffle() ? C_WHITE : C_GRAY);
    cv_text(230 - text_w(&FONT_XS, "FX"), 48, &FONT_XS, "FX", tack_fx_send((uint32_t)ui.voice) ? C_HI : C_LINE);
}

/* a lane's direction, 9 wide and 7 high */
static void dir_arrow(int32_t x, int32_t y, int back, uint16_t c)
{
    for (int32_t i = 0; i < 4; i++)
        cv_rect(back ? x + 3 - i : x + i, y + i, 1, 7 - 2 * i, c);
}

static void dir_glyph(int32_t x, int32_t y, uint32_t dir, uint16_t c)
{
    if (dir == 4u) {                             /* random: a die's four */
        cv_rect(x, y, 3, 3, c);
        cv_rect(x + 5, y, 3, 3, c);
        cv_rect(x, y + 4, 3, 3, c);
        cv_rect(x + 5, y + 4, 3, 3, c);
    } else {
        if (dir != 0u)
            dir_arrow(x, y, 1, c);
        if (dir != 1u)
            dir_arrow(dir == 2u ? x + 5 : dir == 3u ? x + 3 : x, y, 0, c);
    }
}

/* drawn only when it would change: it is the largest strip on a screen that redraws on every step */
static uint32_t bottom_key;              /* the strip on the screen */

static void draw_bottom(void)
{
    const int overlay = ui.fx_page || ui.set_page || ui.song_page;
    const int graph = overlay ? TACK_GRAPH_NONE : tack_page_graph((uint32_t)ui.voice, ui.group, (uint32_t)row_now());
    const int pages = ui.fx_page ? tack_fx_pages() : overlay || screens() < 2 ? 0 : screens();
    const int bar = ui.fx_page ? ui.fx_page - 1 : screen_now();
    const int echo = ui.fx_page ? tack_fx_delay_ms() : 0;
    const TackSlot *target = target_slot();
    const uint32_t xruns = plat_xruns() - ui.xrun_base;
    const uint32_t used = home_info() ? tack_patterns_used() : 0u;
    uint32_t key = 2166136261u;
    int32_t i;

#define KEY(v) (key = (key ^ (uint32_t)(v)) * 16777619u)
    KEY(ui.voice);
    KEY(ui.fx_page);
    KEY(ui.set_page);
    KEY(ui.song_page);
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
    KEY(row_now());
    KEY(echo);
    if (home_info()) {
        KEY(tack_pattern());
        KEY(tack_pattern_next());
        KEY(used);
        KEY(tack_shuffle());
        KEY(tack_song_mode());
        KEY(tack_song_pos());
        KEY(song_len());
        KEY(tack_chain_len());
        KEY(tack_fx_send((uint32_t)ui.voice));
    }
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
    if (key == bottom_key)
        return;
    bottom_key = key;

    cv_begin(240, BOTTOM_H, C_BLACK);
    graph_w = home_info() ? HOME_ENV_W : GRAPH_W;
    if (0) {
    } else if (graph == TACK_GRAPH_LFO) {
        draw_lfo();
    } else if (graph >= TACK_GRAPH_OSC) {
        draw_osc(graph);
    } else if (graph) {
        draw_env(graph);
    }
    if (home_info())
        draw_home_info(used);
    cv_text(10, ROW_Y, &FONT_XS, page_name(), C_GRAY);
    if (ui.held >= 0) {          /* the step's recurrence, where the keys show it */
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
