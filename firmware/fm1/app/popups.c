/* SPDX-License-Identifier: GPL-3.0-only */
/* A held step's popups, and the tempo screen. app.c includes this: it is one file to the compiler, and shares app.c's state */

#define POP_Y 62
#define POP_H 106                        /* above the main page's bottom strip, whose cache must not be crossed */

static void pop_centre(int32_t y, const felucca_font_t *f, const char *t, uint16_t c)
{
    cv_text(106 - text_w(f, t) / 2, y, f, t, c);
}

/* What SEL does here, each line true of this page: it is the one list of what no page shows */
static void help_row(int32_t *y, const char *with, const char *does)
{
    cv_text(10, *y, &FONT_S, with, C_WHITE);
    cv_text(104, *y, &FONT_S, does, C_GRAY);
    *y += on_home() ? 13 : 15;           /* eight rows at most, and HOME has a line under them */
}

static void draw_sel_help(void)
{
    const int saving = ui.over == PAGE_SAVE;
    int32_t y = 18;
    cv_begin(240, 140, C_BLACK);
    cv_rect(0, 0, 240, 1, C_DIM);
    cv_rect(0, 139, 240, 1, C_DIM);
    cv_text(10, 3, &FONT_XS, "HOLD SEL AND", C_AMB);
    if (seq_detail())
        help_row(&y, "SELECT", "JUMP A BAR");
    else if (!saving && ui.over != PAGE_SONG)
        help_row(&y, "SELECT", "SHUFFLE");
    if (!saving && ui.over != PAGE_SONG && !keys_notes(ui.btn))
        help_row(&y, "WHITE KEY", "LANE LENGTH");
    if (note_view())
        help_row(&y, "KEY", "ONLY LISTEN");
    if (!full_page() || seq_detail() || arp_page())
        help_row(&y, "OCT+ OCT-", "ADD, REMOVE BAR");
    if (!saving) {
        help_row(&y, "PRESET", "MUTATE KIT");
        help_row(&y, "ALGORITHM", "MUTATE VOICE");
    }
    help_row(&y, "SAVE", "WRITE CHANGES");
    help_row(&y, "EDIT, HOLD", "CLEAR LANE");
    help_row(&y, "HOME, HOLD", "REVERT KIT");
    if (on_home())                               /* not SEL's, but the next thing to want on the page a new pattern starts from */
        cv_text(10, y + 2, &FONT_XS, "HOLD SEQ AND PRESS A KEY: PATTERN", C_AMB);
    cv_blit(0, HEADER_H);
}

static void draw_popup(void)
{
    const uint8_t v = (uint8_t)ui.voice, st = ui.held_step;
    const uint16_t c = VOICE_COLOUR[ui.voice];
    char t[28];
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
            text_put(int_put(t, nudge < 0 ? -nudge : nudge), nudge < 0 ? "/24 STEP EARLY" : "/24 STEP LATE");
            pop_centre(74, &FONT_B, t, C_WHITE);
        }
    } else {
        const int chance = tack_step_get(v, st, TACK_ST_PROB);
        pop_centre(8, &FONT_B, "CHANCE", C_WHITE);
        text_put(int_put(t, chance), "%");
        pop_centre(34, &FONT_M, t, C_WHITE);
        cv_rect(16, 72, 180, 10, C_LINE);
        cv_rect(16, 72, 180 * chance / 100, 10, c);
    }
    if (ui.msg_until) {                          /* there is no header to say it in: in place of the title */
        cv_rect(2, 4, 208, 24, C_BLACK);
        pop_centre(8, &FONT_B, ui.msg, C_HI);
    }
    cv_blit(14, POP_Y);
}
