/* SPDX-License-Identifier: GPL-3.0-only */
/* Which page is showing, and what each dial is on it. app.c includes this: it is one file to the compiler, and shares app.c's state */

static uint32_t pressed(uint32_t now, uint32_t b) { return (now & ~ui.btn) >> b & 1u; }

static int step_of(uint32_t k) { return ui.bar * 16 + (int)k; }

static void toggle_step(uint8_t s)
{
    tack_post_step((uint8_t)ui.voice, s, (uint8_t)!tack_step_on((uint8_t)ui.voice, s));
}

/* A sound page is four dials and a picture, as the OP-1's: one row of the three a screen holds, each one of the LXR's menu
 * pages. The dials are on the middle row, its most used, and an OCT held swaps in the row above or below for as long as it is
 * held, so there is never a row to keep track of. The rows are in the same place on every voice, whichever pages it has.
 * ui.sub is the page of the middle row */
#define SCREEN_ROWS 3
#define NO_PAGE (-1)
#define PITCH_ROW 20                     /* a row of no page of its own: FINE and what moves the pitch, beside the oscillator */
static int sound_page(void)
{
    return ui.group != TACK_G_MAIN && !ui.over;
}

/* the main page as one voice at a larger size, with what each step holds */
static int voice_page(void)
{
    return ui.voice_view && !ui.note_mode && ui.group == TACK_G_MAIN && !ui.over;
}

/* a page with the screen to itself, which leaves no grid: the rest edit steps */
static int note_view(void)
{
    return ui.voice_view && ui.note_mode && ui.group == TACK_G_MAIN && !ui.over;
}

static int seq_detail(void) { return voice_page() || note_view(); }

/* ui.over: SONG, which SEQ held with ARP opens, and ARP's three screens over the selected lane's bars */
#define PAGE_SONG 1u
#define PAGE_LANE 2u
#define PAGE_EUCLID 3u
#define PAGE_VEL 4u
#define PAGE_SAVE 5u                     /* SAVE: what is saved and where, K4 and K1 or SELECT, and a second press writes it */
#define PAGE_FX 6u
#define PAGE_SET 7u
static int song_pages(void) { return ui.over && ui.over <= PAGE_SAVE; }
enum { SAVE_PATTERN, SAVE_BLEND, SAVE_KIT };
static int vel_page(void) { return ui.over == PAGE_VEL; }
static int arp_page(void) { return ui.over >= PAGE_LANE && ui.over <= PAGE_VEL; }

static int keys_notes(uint32_t btn)             /* the keys play the voice: not where the keys are that page's own */
{
    return ui.note_mode && !ui.over && !(btn >> B_SEQ & 1u) && !(btn >> B_ARP & 1u);
}

/* a bank kit's name, without the spaces that pad it */
static void kit_name_of(const uint8_t *k, char *t)
{
    uint32_t n = 8u;
    memcpy(t, k ? (const char *)k : "NONE    ", 8u);
    while (n && (t[n - 1u] == ' ' || !t[n - 1u]))
        n--;
    t[n] = 0;
}

/* the name of the kit morph goes to */
static void morph_name(char *t)
{
    const int kit = tack_morph_kit();
    kit_name_of(kit ? kit_slot((uint32_t)kit - 1u) : 0, t);
}

static int seq_in_view(void);

/* the overview: its header has room for what other pages say in a message */
static int on_home(void) { return seq_in_view() && !ui.voice_view; }

static int morph_held(void) { return !keys_notes(ui.btn) && ui.held < 0 && (ui.keys >> BK[KEY_MORPH] & 1u); }

/* SEL held a moment where it can set the lane's end: what the end is now. Not at once, or it would flash on each tap that walks
 * the steps */
#define SEL_SHOW_MS 250u
#define SEL_HELP_MS 1000u

/* A step key held: its locks and recurrence show once a dial turns, or once it has been held this long to see what it has.
 * Not at once, or a tap that adds a step would flash the dials, the bottom strip and the keys. It is held from the press all
 * the same: this is only what shows */
#define HOLD_SHOW_MS 500u
static int held_shown(void) { return ui.held >= 0 && (ui.held_used || plat_ms() - ui.held_at >= HOLD_SHOW_MS); }

/* A voice's key held a moment does as SEL does, once the lane shows on the keys: a quick voice key and a step can overlap, and that
 * must stay a step. Recording, the key is writing the part */
static int lane_key_held(void)
{
    return ui.lane_key && (ui.keys >> BK[ui.lane_key - 1u] & 1u) && ui.held < 0 && !tack_recording() && !keys_notes(ui.btn)
           && (seq_in_view() || arp_page()) && plat_ms() - ui.lane_at >= SEL_SHOW_MS;
}

static int sel_length(void)
{
    return lane_key_held() || ((ui.btn >> B_SEL & 1u) && ui.held < 0 && (seq_in_view() || arp_page()) && plat_ms() - ui.sel_at >= SEL_SHOW_MS);
}

/* SEL held on its own: anything touched with it is a chord being played, and the list stays away */
static int sel_help(void) { return (ui.btn >> B_SEL & 1u) && !ui.sel_used && ui.held < 0 && plat_ms() - ui.sel_at >= SEL_HELP_MS; }

/* a white key while it shows: 0 past the lane's end, 1 in the lane, 2 the last step */
static int sel_light(uint32_t k)
{
    const int s = step_of(k), len = tack_length((uint8_t)ui.voice);
    return s + 1 == len ? 2 : s < len;
}

static const char *key_word(void)
{
    static char morph[20] = "MORPH TO ", length[12] = "LENGTH ";
    if (ui.roll_on)
        return "ROLL";
    if (sel_length()) {
        int_text(tack_length((uint8_t)ui.voice), length + 7);
        return length;
    }
    if (morph_held()) {
        morph_name(morph + 9);
        return morph;
    }
    return !keys_notes(ui.btn) && ui.held < 0 && (ui.keys >> BK[KEY_COPY] & 1u) ? "COPY TO" : 0;
}

static int full_page(void) { return sound_page() || ui.over || seq_detail(); }

/* a group's pages come in the order OSC, its modulator, MOD WAVE if any, FILTER, TRANSIENT, OUTPUT; AMP EG, MOD EG if any,
 * VELOCITY; LFO, LFO SYNC */
static int screens(void)
{
    return ui.group == TACK_G_EDIT ? 2 : 1;
}

static int screen_page(int screen, int row)
{
    static const int8_t EDIT5[2][SCREEN_ROWS] = {{1, 0, PITCH_ROW}, {3, 2, 4}}, EDIT6[2][SCREEN_ROWS] = {{1, 0, 2}, {4, 3, 5}};
    static const int8_t ENV3[SCREEN_ROWS] = {1, 0, 2}, ROWS2[SCREEN_ROWS] = {NO_PAGE, 0, 1};
    const int n = tack_pages((uint32_t)ui.voice, ui.group);
    if (ui.group == TACK_G_EDIT)
        return (n >= 6 ? EDIT6 : EDIT5)[screen][row];
    return n >= 3 ? ENV3[row] : ROWS2[row];
}

static int screen_now(void) { return ui.group == TACK_G_EDIT && ui.sub != 0; }

static int row_now(void)
{
    const int r = ui.btn >> B_OCTUP & 1u ? 0 : ui.btn >> B_OCTDN & 1u ? 2 : 1;
    const int page = sound_page() ? screen_page(screen_now(), r) : NO_PAGE;
    return page == NO_PAGE ? ui.sub : page;
}

static int same(const char *a, const char *b);

/* ENV keeps the voice's level by its envelope, so a slot may not be the page's own: the fourth of AMP EG is LEVEL, and REPEAT,
 * which some voices have there, moves to the fourth of the last row */
static const TackSlot *page_slot(int page, uint32_t i)
{
    if (page == PITCH_ROW) {                     /* the five page voices all have a MOD EG, ENV's second page */
        static const uint8_t MOD_EG[3] = {2u, 0u, 1u};
        return i ? tack_slot((uint32_t)ui.voice, TACK_G_ENV, 1, MOD_EG[i - 1u])
                 : tack_slot((uint32_t)ui.voice, TACK_G_EDIT, 0, 0);
    }
    if (ui.group == TACK_G_EDIT && page == 0 && i == 0u)
        return tack_slot((uint32_t)ui.voice, TACK_G_MAIN, 0, 0);   /* PITCH beside the wave, on every voice */
    if (ui.group == TACK_G_EDIT && page == 0 && i == 3u && tack_pages((uint32_t)ui.voice, ui.group) >= 6)
        return tack_slot((uint32_t)ui.voice, TACK_G_EDIT, 0, 0);   /* six pages have no row for FINE under it */
    if (ui.group == TACK_G_EDIT && i == 3u && tack_page_kind((uint32_t)ui.voice, ui.group, (uint32_t)page) == TACK_PAGE_OUTPUT)
        return tack_send_slot((uint32_t)ui.voice);
    if (ui.group == TACK_G_ENV && i == 3u) {
        if (page == 0)
            return tack_slot((uint32_t)ui.voice, TACK_G_MAIN, 0, 3);
        if (page == tack_pages((uint32_t)ui.voice, ui.group) - 1)
            return tack_slot((uint32_t)ui.voice, TACK_G_ENV, 0, 3);
    }
    return tack_slot((uint32_t)ui.voice, ui.group, (uint32_t)page, i);
}

static const TackSlot *slot(uint32_t i) { return page_slot(row_now(), i); }

/* a label that makes sense without its page's title beside it */
static const char *page_label(int page, const TackSlot *sl)
{
    if (sl->par == tack_voice_param((uint32_t)ui.voice, 3))
        return "LEVEL";
    if (ui.group == TACK_G_ENV && page == tack_pages((uint32_t)ui.voice, ui.group) - 1) {
        if (sl->target)
            return "VEL TO";
        if (sl->max == 1u)
            return "VEL>VOL";
        if (sl->max == 127u && sl != tack_slot((uint32_t)ui.voice, TACK_G_ENV, 0, 3))
            return "VEL AMT";
    }
    if (page == PITCH_ROW && sl != tack_slot((uint32_t)ui.voice, TACK_G_EDIT, 0, 0))
        return sl->role == TACK_ROLE_PITCH ? "SWP AMT" : sl->role == TACK_ROLE_DECAY ? "SWP DEC" : "SWP CRV";
    if (ui.group == TACK_G_EDIT && tack_page_kind((uint32_t)ui.voice, ui.group, (uint32_t)page) == TACK_PAGE_FM) {
        /* the second oscillator: it modulates the first, or MODE MIX crossfades the two and AMOUNT is that crossfade */
        const TackSlot *mode = tack_slot((uint32_t)ui.voice, TACK_G_EDIT, 0, 3);
        const int mix = same(tack_slot_text(mode, tack_get_param(mode->par)), "MIX");
        if (sl->role == TACK_ROLE_AMOUNT)
            return mix ? "MIX" : "FM AMT";
        if (sl->role == TACK_ROLE_FREQ)
            return mix ? "FREQ 2" : "FM FREQ";
        if (sl->role == TACK_ROLE_WAVE)
            return mix ? "WAVE 2" : "FM WAVE";
    }
    return sl->label;
}
