/* SPDX-License-Identifier: GPL-3.0-only */
/* A held step's popups, and the tempo screen. app.c includes this: it is one file to the compiler, and shares app.c's state */

#define POP_Y 62
#define POP_H 106                        /* above the main page's bottom strip, whose cache must not be crossed */

static void pop_centre(int32_t y, const felucca_font_t *f, const char *t, uint16_t c)
{
    cv_text(106 - text_w(f, t) / 2, y, f, t, c);
}

/* Tempo and shuffle: two values, each with a picture and the control that sets it. The second of three notes moves as
 * the shuffle does, as a step between beats does */
static void draw_tempo(void)
{
    char t[8];
    const int32_t swing = (int32_t)tack_shuffle() * 18 / 127;
    cv_begin(240, 140, C_BLACK);
    cv_rect(0, 0, 240, 1, C_DIM);
    cv_rect(0, 139, 240, 1, C_DIM);
    for (int32_t y = 8; y < 132; y += 10)
        cv_rect(120, y, 1, 5, C_DIM);
    cv_text(60 - text_w(&FONT_B, "TEMPO") / 2, 6, &FONT_B, "TEMPO", C_AMB);
    cv_text(180 - text_w(&FONT_B, "SHUFFLE") / 2, 6, &FONT_B, "SHUFFLE", C_AMB);
    int_text(tack_bpm(), t);
    cv_text(60 - text_w(&FONT_L, t) / 2, 26, &FONT_L, t, C_WHITE);
    int_text(tack_shuffle(), t);
    cv_text(180 - text_w(&FONT_L, t) / 2, 26, &FONT_L, t, C_WHITE);
    aa_line(46.0f, 110.0f, 74.0f, 110.0f, C_GRAY);
    aa_line(46.0f, 110.0f, 55.0f, 68.0f, C_GRAY);
    aa_line(74.0f, 110.0f, 65.0f, 68.0f, C_GRAY);
    aa_line(55.0f, 68.0f, 65.0f, 68.0f, C_GRAY);
    aa_line(60.0f, 104.0f, 71.0f, 76.0f, C_HI);
    for (int32_t k = 0; k < 3; k++) {
        const int32_t x = 150 + k * 28 + (k == 1 ? swing : 0);
        cv_rect(x, 102, 11, 8, C_WHITE);
        cv_rect(x + 9, 74, 2, 30, C_WHITE);
    }
    cv_rect(159, 72, 58, 4, C_WHITE);
    cv_text(60 - text_w(&FONT_B, "(SELECT)") / 2, 118, &FONT_B, "(SELECT)", C_GRAY);
    cv_text(180 - text_w(&FONT_B, "(GLO)") / 2, 118, &FONT_B, "(GLO)", C_GRAY);
    cv_blit(0, 52);
}

static void draw_popup(void)
{
    const uint8_t v = (uint8_t)ui.voice, st = (uint8_t)step_of((uint32_t)ui.held);
    const uint16_t c = VOICE_COLOUR[ui.voice];
    char t[28];
    uint32_t n = 0;
    cv_begin(212, POP_H, C_BLACK);
    outline(0, 0, 212, POP_H, 2, C_WHITE);
    if (ui.popup == POP_NUDGE) {
        const int nudge = tack_step_get(v, st, TACK_ST_NUDGE);   /* in pulses, 24 to a step. A tick of the ruler is 3 */
        const int32_t x = 106 + nudge * 13 / 3;
        pop_centre(8, &FONT_B, "MICRO TIMING", C_WHITE);
        for (int k = -7; k <= 7; k++) {
            const int32_t h = k == 0 ? 24 : k % 2 == 0 ? 16 : 10;
            cv_rect(106 + k * 13 - (k == 0), 54 - h, k == 0 ? 3 : 1, h, k == 0 ? C_WHITE : C_GRAY);
        }
        cv_rect(12, 54, 188, 1, C_GRAY);
        for (int32_t k = 0; k < 7; k++)
            cv_rect(x - k, 64 - k, 2 * k + 1, 1, c);
        if (!nudge) {
            pop_centre(74, &FONT_B, "ON GRID", C_WHITE);
        } else {
            int_text(nudge < 0 ? -nudge : nudge, t);
            n = 0;
            while (t[n])
                n++;
            const char *const rest = nudge < 0 ? "/24 STEP EARLY" : "/24 STEP LATE";
            for (uint32_t i = 0; rest[i]; i++)
                t[n++] = rest[i];
            t[n] = 0;
            pop_centre(74, &FONT_B, t, C_WHITE);
        }
    } else if (ui.popup == POP_RECUR) {
        const int len = tack_recur_len(v, st), mask = tack_recur_mask(v, st);
        pop_centre(8, &FONT_B, "RECURRENCE", C_WHITE);
        for (int k = 0; k < 8; k++) {
            const int32_t x = 14 + k * 24;
            t[0] = (char)('1' + k);
            t[1] = 0;
            if (k >= len) {
                outline(x, 32, 20, 26, 1, C_LINE);
            } else if (mask >> k & 1) {
                cv_rect(x, 32, 20, 26, c);
                cv_text(x + 10 - text_w(&FONT_B, t) / 2, 37, &FONT_B, t, C_BLACK);
            } else {
                cv_rect(x, 32, 20, 26, C_LINE);
                cv_text(x + 10 - text_w(&FONT_B, t) / 2, 37, &FONT_B, t, C_GRAY);
            }
        }
        if (len == 1) {
            pop_centre(72, &FONT_B, "EVERY PASS", C_WHITE);
        } else {
            n = 0;
            for (int k = 0; k < len; k++)
                if (mask >> k & 1) {
                    t[n++] = (char)('1' + k);
                    t[n++] = ' ';
                }
            t[n++] = 'O';
            t[n++] = 'F';
            t[n++] = ' ';
            t[n++] = (char)('0' + len);
            t[n] = 0;
            pop_centre(72, &FONT_B, t, C_WHITE);
        }
    } else {
        const int chance = tack_step_get(v, st, TACK_ST_PROB);
        pop_centre(8, &FONT_B, "CHANCE", C_WHITE);
        int_text(chance, t);
        n = 0;
        while (t[n])
            n++;
        t[n++] = '%';
        t[n] = 0;
        pop_centre(34, &FONT_M, t, C_WHITE);
        cv_rect(16, 72, 180, 10, C_LINE);
        cv_rect(16, 72, 180 * chance / 100, 10, c);
    }
    cv_blit(14, POP_Y);
}
