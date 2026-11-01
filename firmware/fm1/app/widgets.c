/* SPDX-License-Identifier: GPL-3.0-only */
/* The sound pages' drawing: lines, dials, boxes and pictures. app.c includes this: it is one file to the compiler, and shares app.c's state */

/* ------------------------------------------------------------------ the grid ---
 * A sound page as Syntakt draws one: a top bar and every row of the screen, in four equal columns with everything centred. Standard
 * widgets by default, and one picture across the parameters that shape it, so the graph is the row. Each strip is its own
 * canvas, as the screen is larger than one, and a row is drawn only when something in it has changed. */
#define PG_Y 21
#define PG_CW 60
#define PG_RH 73
#define PG_PH 55                         /* a picture's height, its label under it */
#define PG_TEXT RGB(186, 186, 192)       /* a label on a row the dials are not on: as bright as still reads as quiet */
#define EN_OSC_WAVE 1                    /* a slot's en for the oscillators' waves and the LFOs', from tack_pages.h */
#define EN_LFO_WAVE 8

static uint16_t tint(uint16_t c) { return (uint16_t)((c >> 3) & 0x18E3u); }

/* a pixel a fifteenth at a time over what is on the canvas, as cv_text's anti-aliased faces */
static void cv_mix(int32_t x, int32_t y, uint16_t c, uint32_t a)
{
    uint32_t bg, nr, ng, nb;
    if ((uint32_t)x >= cv_w || (uint32_t)y >= cv_h || !a)
        return;
    bg = swap16(cv_px[(uint32_t)y * cv_w + (uint32_t)x]);
    nr = ((c >> 11) * a + (bg >> 11) * (15u - a)) / 15u;
    ng = (((c >> 5) & 63u) * a + ((bg >> 5) & 63u) * (15u - a)) / 15u;
    nb = ((c & 31u) * a + (bg & 31u) * (15u - a)) / 15u;
    cv_px[(uint32_t)y * cv_w + (uint32_t)x] = swap16((nr << 11) | (ng << 5) | nb);
}

/* a smooth line two pixels thick: along its long axis, each pixel across covered as far as the line covers it */
static void aa_line(float x0, float y0, float x1, float y1, uint16_t c)
{
    const float dx = x1 - x0, dy = y1 - y0;
    const int steep = (dy < 0 ? -dy : dy) > (dx < 0 ? -dx : dx);
    float a0 = steep ? y0 : x0, b0 = steep ? x0 : y0, a1 = steep ? y1 : x1, b1 = steep ? x1 : y1, t;
    if (a0 > a1) {
        t = a0, a0 = a1, a1 = t;
        t = b0, b0 = b1, b1 = t;
    }
    const float m = a1 > a0 ? (b1 - b0) / (a1 - a0) : 0.0f;
    for (int32_t i = (int32_t)a0; i <= (int32_t)a1; i++) {
        const float mid = b0 + ((float)i + 0.5f - a0) * m, top = mid - 1.0f, bot = mid + 1.0f;
        for (int32_t j = (int32_t)top - 1; j <= (int32_t)bot + 1; j++) {
            const float lo = (float)j > top ? (float)j : top, hi = (float)j + 1.0f < bot ? (float)j + 1.0f : bot;
            if (hi > lo)
                cv_mix(steep ? j : i, steep ? i : j, c, (uint32_t)((hi - lo) * 15.0f));
        }
    }
}

static void outline(int32_t x, int32_t y, int32_t w, int32_t h, int32_t t, uint16_t c)
{
    cv_rect(x, y, w, t, c);
    cv_rect(x, y + h - t, w, t, c);
    cv_rect(x, y, t, h, c);
    cv_rect(x + w - t, y, t, h, c);
}

/* the ring lit from one of its 24 parts to another, and the pointer on the second */
static void wd_arc(int32_t x, int32_t y, uint32_t from, uint32_t end, uint16_t c, uint16_t rail)
{
    static const float RING[25][2] = {{-0.7071f, 0.7071f}, {-0.8315f, 0.5556f}, {-0.9239f, 0.3827f}, {-0.9808f, 0.1951f}, {-1.0000f, 0.0000f}, {-0.9808f, -0.1951f}, {-0.9239f, -0.3827f}, {-0.8315f, -0.5556f}, {-0.7071f, -0.7071f}, {-0.5556f, -0.8315f}, {-0.3827f, -0.9239f}, {-0.1951f, -0.9808f}, {-0.0000f, -1.0000f}, {0.1951f, -0.9808f}, {0.3827f, -0.9239f}, {0.5556f, -0.8315f}, {0.7071f, -0.7071f}, {0.8315f, -0.5556f}, {0.9239f, -0.3827f}, {0.9808f, -0.1951f}, {1.0000f, -0.0000f}, {0.9808f, 0.1951f}, {0.9239f, 0.3827f}, {0.8315f, 0.5556f}, {0.7071f, 0.7071f}};
    const float cx = (float)x, cy = (float)y + 27.0f, r = 21.0f;
    const uint32_t lo = from < end ? from : end, hi = from < end ? end : from;
    for (uint32_t i = 0; i < 24u; i++)
        aa_line(cx + RING[i][0] * r, cy + RING[i][1] * r, cx + RING[i + 1][0] * r, cy + RING[i + 1][1] * r, i >= lo && i < hi ? c : rail);
    aa_line(cx + RING[end][0] * 7.0f, cy + RING[end][1] * 7.0f, cx + RING[end][0] * 17.0f, cy + RING[end][1] * 17.0f, c);
}

static void wd_dial(int32_t x, int32_t y, uint32_t v, uint32_t max, uint16_t c, uint16_t rail)
{
    wd_arc(x, y, 0u, max ? (v > max ? max : v) * 24u / max : 0u, c, rail);
}

/* out from 12 o'clock, with its value in it */
static void wd_signed(int32_t x, int32_t y, const TackSlot *sl, uint32_t v, uint16_t c, uint16_t rail, uint16_t text)
{
    char t[6];
    const int32_t s = slot_side(sl, v);
    wd_arc(x, y, 12u, (uint32_t)(s ? 12 + s * 12 / slot_reach(sl, s) : 12), c, rail);
    value_text(sl, v, t);
    cv_text(x - text_w(&FONT_S, t) / 2, y + 34, &FONT_S, t, text);
}

static void wd_box(int32_t x, int32_t y, const char *text, uint16_t c)
{
    char t[7];
    uint32_t i = 0;
    while (i < 6u && text[i]) {
        t[i] = text[i];
        i++;
    }
    t[i] = 0;
    outline(x - 26, y + 12, 52, 30, 2, c);
    if (text_w(&FONT_B, t) > 46)
        cv_text(x - text_w(&FONT_XS, t) / 2, y + 20, &FONT_XS, t, c);
    else
        cv_text(x - text_w(&FONT_B, t) / 2, y + 19, &FONT_B, t, c);
}

/* a modulation target: which voice above, what of it below */
static void wd_target(int32_t x, int32_t y, const char *name, uint16_t c)
{
    char a[4], b[9];
    uint32_t i = 0, j = 0;
    while (i < 3u && name[i] && name[i] != ' ') {
        a[i] = name[i];
        i++;
    }
    a[i] = 0;
    if (!name[i]) {
        wd_box(x, y, name, c);
        return;
    }
    i += name[i] == ' ';
    while (j < 8u && name[i + j]) {
        b[j] = name[i + j];
        j++;
    }
    b[j] = 0;
    outline(x - 28, y + 8, 56, 40, 2, c);
    cv_text(x - text_w(&FONT_XS, a) / 2, y + 11, &FONT_XS, a, C_GRAY);
    cv_text(x - text_w(&FONT_XS, b) / 2, y + 27, &FONT_XS, b, c);
}

static void wd_wave(int32_t x, int32_t y, uint32_t en, uint32_t wave, uint16_t c)
{
    float lx = 0.0f, ly = 0.0f;
    for (int32_t i = 0; i <= 46; i += 2) {
        const float p = (float)i / 46.0f, v = en == EN_OSC_WAVE ? osc_shape(wave, p, 1.0f, i) : lfo_shape(wave, p);
        const float nx = (float)(x - 23 + i), ny = (float)y + 27.0f - v * 18.0f;
        if (i)
            aa_line(lx, ly, nx, ny, c);
        lx = nx, ly = ny;
    }
}

static void wd_toggle(int32_t x, int32_t y, int on, uint16_t c)
{
    outline(x - 21, y + 16, 42, 22, 2, c);
    cv_rect(on ? x + 1 : x - 17, y + 20, 16, 14, c);
}

static void wd_empty(int32_t x, int32_t y)
{
    for (int32_t k = 0; k < 40; k += 6) {
        cv_rect(x - 21 + k, y + 8, 3, 1, C_DIM);
        cv_rect(x - 21 + k, y + 46, 3, 1, C_DIM);
    }
    for (int32_t k = 0; k < 36; k += 6) {
        cv_rect(x - 21, y + 9 + k, 1, 3, C_DIM);
        cv_rect(x + 21, y + 9 + k, 1, 3, C_DIM);
    }
}

/* an envelope as the row's graph, the engine's curves as draw_env's */
static void wd_env(int32_t x, int32_t y, int32_t w, int graph, uint16_t c)
{
    const int amp = graph == TACK_GRAPH_AMP;
    const uint8_t attack = amp ? graph_value(graph, 0, 0) : 0;
    const uint8_t decay = graph_value(graph, amp ? 1 : 0, 0), slope = graph_value(graph, amp ? 2 : 1, 63);
    const float ta = amp && attack ? eg_time(attack, 198.0f) : 0.0f, td = eg_time(decay, 1998.0f);
    const float s = eg_slope(slope), inv = eg_slope((uint8_t)(127u - slope));
    const float top = (float)y + 6.0f, h = (float)PG_PH - 10.0f;
    int32_t span = log2_px(ta + td) * w / GRAPH_W, up;
    float lx = (float)x + 1.0f, ly = top + h;
    span = span < 24 ? 24 : span;
    up = (int32_t)((float)span * ta / (ta + td));
    if (attack && up < 3)
        up = 3;
    for (int32_t i = 0; i <= span; i += 2) {
        const float v = i < up ? eg_shape((float)i / (float)up, inv)
                               : eg_shape(1.0f - (float)(i - up) / (float)(span - up), s);
        const float nx = (float)x + 1.0f + (float)i, ny = top + h - v * h;
        if (i)
            aa_line(lx, ly, nx, ny, c);
        lx = nx, ly = ny;
    }
    cv_rect(x + up - 1, y + 4, 5, 5, C_WHITE);
}

/* a curve in a picture from the next point's value, 0 to 1 */
typedef float (*PicShape)(float p, const float *v);

static void wd_curve(int32_t x, int32_t y, int32_t w, PicShape f, const float *v, uint16_t c)
{
    const float top = (float)y + 6.0f, h = (float)PG_PH - 12.0f;
    float lx = 0.0f, ly = 0.0f;
    for (int32_t i = 0; i <= w; i += 2) {
        float g = f((float)i / (float)w, v);
        g = g < 0.0f ? 0.0f : g > 1.0f ? 1.0f : g;
        const float nx = (float)(x + i), ny = top + h - g * h;
        if (i)
            aa_line(lx, ly, nx, ny, c);
        lx = nx, ly = ny;
    }
}

/* a filter's response: not its exact curve, the shape FREQ, RES and TYPE give it. v: frequency, resonance, type */
static float filter_shape(float p, const float *v)
{
    const float d = (p - v[0]) * 7.0f, bp = 1.0f / (1.0f + d * d * 2.0f);
    const float lp = d <= 0.0f ? 1.0f : 1.0f / (1.0f + d * d * 2.0f), hp = d >= 0.0f ? 1.0f : 1.0f / (1.0f + d * d * 2.0f);
    const float peak = v[1] * 0.3f * bp;
    switch ((uint32_t)v[2]) {
    case 0: return lp * 0.68f + peak;
    case 1: return hp * 0.68f + peak;
    case 2:
    case 3: return bp * 0.68f + peak;
    case 4: return (1.0f - bp) * 0.8f;
    case 5: return 0.4f + bp * 0.3f + peak;
    case 6: return lp * lp * 0.68f + peak;
    default: return 0.6f;
    }
}

/* two cycles. v: the oscillator's wave, its pulse width */
static float osc_picture(float p, const float *v)
{
    const float q = p * 2.0f;
    return 0.5f + 0.5f * osc_shape((uint32_t)v[0], q - (float)(int32_t)q, v[1], (int32_t)(p * 200.0f));
}

/* two cycles of the LFO, as far as its amount. v: amount, wave */
static float lfo_picture(float p, const float *v)
{
    const float q = p * 2.0f;
    return 0.5f + 0.5f * v[0] * lfo_shape((uint32_t)v[1], q - (float)(int32_t)q);
}
