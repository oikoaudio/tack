/* SPDX-License-Identifier: GPL-3.0-only */
/* Tack on the FM-1: the panel and the screen, part of fm1-base's unity build. The engine and sequencer are in
 * src/tack_engine.c; everything reaches them through its event queue, so the audio interrupt stays their one writer.
 *
 *   white keys   STEP mode: the 16 steps of the page. PLAY mode: the first seven play the voices
 *   black keys   the first seven select a voice; with ARP held, mute it
 *   SEQ          STEP / PLAY mode      OCT- OCT+  the bar, 1-4        PLAY  start, stop     REC  live recording
 *   K1-K4        pitch, filter, decay, volume of the voice
 *   SELECT       tempo                 ALGORITHM  shuffle             PRESET  pattern, 1-16
 *   GLO          external MIDI clock   SAVE       pattern and kit (stopped)     HOME  the load test */
#include "src/tack_engine.h"
#include "src/seq_platform.h"

static const char *const VOICE_NAME[TACK_VOICES] = {"DRUM 1", "DRUM 2", "DRUM 3", "SNARE", "CYMBAL", "CL HAT", "OP HAT"};
static const char *const P_NAME[TACK_NP] = {"PITCH", "FILTER", "DECAY", "VOLUME"};
static const uint8_t WK[16] = PLAT_WHITE_KEYS, BK[11] = PLAT_BLACK_KEYS;

#define SET_MAGIC 0x4B544C31u            /* "KTL1" */
typedef struct {
    uint32_t magic;
    uint16_t bpm;
    uint8_t rsv[2];
    uint8_t kit[256];
} settings_t;
_Static_assert(sizeof(settings_t) <= PLAT_STORE_MAX, "one store object");
static settings_t set;
static uint8_t pat_buf[3840];

static struct {
    uint32_t keys, btn, redraw;
    int32_t voice, bar;
    uint8_t step_mode, stress;
    char msg[24];
    uint32_t msg_until;
    uint32_t cpu_peak;
} ui;

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

/* audio interrupt: MIDI in, as Tack's channels: channel 1 plays the kit from note 60, 2-8 one voice each */
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
            if (ch == 0u && d1 >= 60u && d1 < 60u + TACK_VOICES)
                tack_note_now((uint8_t)(d1 - 60u), 60, (uint8_t)d2);
            else if (ch >= 1u && ch <= TACK_VOICES)
                tack_note_now((uint8_t)(ch - 1u), (uint8_t)d1, (uint8_t)d2);
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

void app_init(void)
{
    uint32_t i;
    tack_init();
    seq_platform_set_midi_out(seq_midi_out);
    for (i = 0; i < TACK_PATTERNS; i++)
        if (plat_store_load(OBJ_PAT0 + i, pat_buf, sizeof pat_buf) == (int)tack_pattern_image_size())
            tack_load_pattern((uint8_t)i, pat_buf);
    if (plat_store_load(OBJ_SETTINGS, &set, sizeof set) == (int)sizeof set && set.magic == SET_MAGIC) {
        tack_load_kit(set.kit);
        tack_post_bpm(set.bpm);
    }
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

/* SAVE: the flash stops everything for a moment, sound too, so never while the sequencer runs */
static void save(void)
{
    if (tack_running()) {
        app_say("STOP FIRST", "");
        return;
    }
    set.magic = SET_MAGIC;
    set.bpm = tack_bpm();
    memcpy(set.kit, tack_kit(), tack_kit_size());
    if (plat_store_save(OBJ_SETTINGS, &set, sizeof set) == 0 &&
        plat_store_save(OBJ_PAT0 + (uint32_t)tack_pattern(), tack_pattern_image(), tack_pattern_image_size()) == 0)
        app_say("SAVED", "");
    else
        app_say("SAVE", "FAILED");
}

static uint32_t pressed(uint32_t now, uint32_t b) { return (now & ~ui.btn) >> b & 1u; }

static void input(void)
{
    uint32_t keys = plat_keys(), btn = plat_buttons(), k;
    uint32_t down = keys & ~ui.keys;
    int32_t e;

    for (k = 0; k < TACK_VOICES; k++)          /* the black keys: voice, or with ARP held its mute */
        if (down >> BK[k] & 1u) {
            if (btn >> B_ARP & 1u) {
                tack_post_mute((uint8_t)k, (uint8_t)!tack_muted((uint8_t)k));
            } else {
                ui.voice = (int32_t)k;
            }
            ui.redraw = 1;
        }
    for (k = 0; k < 16u; k++)                    /* the white keys: steps, or the voices */
        if (down >> WK[k] & 1u) {
            if (ui.step_mode) {
                uint8_t s = (uint8_t)((uint32_t)ui.bar * 16u + k);
                tack_post_step((uint8_t)ui.voice, s, (uint8_t)!tack_step_on((uint8_t)ui.voice, s));
            } else if (k < TACK_VOICES) {
                ui.voice = (int32_t)k;
                tack_post_note((uint8_t)k, 60, 110);
            }
            ui.redraw = 1;
        }
    ui.keys = keys;

    if (pressed(btn, B_SEQ)) {
        ui.step_mode = !ui.step_mode;
        ui.redraw = 1;
    }
    if (pressed(btn, B_OCTDN) && ui.bar > 0) {
        ui.bar--;
        ui.redraw = 1;
    }
    if (pressed(btn, B_OCTUP) && ui.bar < (int32_t)TACK_BARS - 1) {
        ui.bar++;
        ui.redraw = 1;
    }
    if (pressed(btn, B_PLAY) && !tack_ext_sync()) {
        tack_post_run((uint8_t)!tack_running());
        ui.redraw = 1;
    }
    if (pressed(btn, B_REC)) {
        tack_post_record((uint8_t)!tack_recording());
        ui.redraw = 1;
    }
    if (pressed(btn, B_GLO)) {
        tack_post_ext_sync((uint8_t)!tack_ext_sync());
        app_say("MIDI CLOCK", tack_ext_sync() ? "ON" : "OFF");
    }
    if (pressed(btn, B_SAVE))
        save();
    if (pressed(btn, B_HOME)) {                  /* the load test, and its peak from zero */
        ui.stress = !ui.stress;
        ui.cpu_peak = 0;
        tack_post_stress(ui.stress);
        app_say("LOAD TEST", ui.stress ? "ON" : "OFF");
    }
    ui.btn = btn;

    for (k = 0; k < TACK_NP; k++)
        if ((e = plat_enc(EN_K1 + (int)k)) != 0) {
            uint16_t p = tack_voice_param((uint32_t)ui.voice, k);
            tack_post_param(p, (uint8_t)clamp((int32_t)tack_get_param(p) + e, 0, 127));
            ui.redraw = 1;
        }
    if ((e = plat_enc(EN_SELECT)) != 0) {
        tack_post_bpm((uint16_t)clamp((int32_t)tack_bpm() + e, 30, 300));
        ui.redraw = 1;
    }
    if ((e = plat_enc(EN_ALGO)) != 0) {
        tack_post_shuffle((uint8_t)clamp((int32_t)tack_shuffle() + e, 0, 127));
        ui.redraw = 1;
    }
    if ((e = plat_enc(EN_PRESET)) != 0) {
        tack_post_pattern((uint8_t)clamp(tack_pattern() + e, 0, (int32_t)TACK_PATTERNS - 1));
        ui.redraw = 1;
    }
}

/* the keys light the page's steps of the voice, the playhead dim-to-bright against them; the black keys the voice */
static void lights(void)
{
    uint32_t keys = 0, dim = 0, btn = 0, k;
    const int head = tack_running() ? tack_playhead((uint8_t)ui.voice) : -1;
    for (k = 0; k < TACK_VOICES; k++) {
        if ((int32_t)k == ui.voice)
            keys |= 1u << BK[k];
        else if (!tack_muted((uint8_t)k))
            dim |= 1u << BK[k];
    }
    if (ui.step_mode) {
        for (k = 0; k < 16u; k++) {
            const uint32_t s = (uint32_t)ui.bar * 16u + k;
            if (tack_step_on((uint8_t)ui.voice, (uint8_t)s) != ((int)s == head))
                keys |= 1u << WK[k];
        }
    } else {
        for (k = 0; k < TACK_VOICES; k++)
            dim |= 1u << WK[k];
    }
    if (ui.step_mode)
        btn |= 1u << B_SEQ;
    if (tack_recording())
        btn |= 1u << B_REC;
    if (tack_ext_sync())
        btn |= 1u << B_GLO;
    if (tack_running())
        btn |= 1u << B_PLAY | LED_PLAY_GREEN;
    plat_lights(btn, keys, 0, dim, 1);
}

void app_input_only(void) { input(); }

static void draw(void)
{
    char t[16];
    uint32_t i;
    cv_begin(240, 120, C_BLACK);
    cv_text(10, 6, &FONT_B, ui.msg_until ? ui.msg : APP_NAME, C_WHITE);
    t[0] = 'P';
    int_text(tack_pattern() + 1, t + 1);
    cv_text(150, 6, &FONT_B, t, tack_running() ? C_HI : C_GRAY);
    int_text(tack_bpm(), t);
    cv_text(196, 6, &FONT_B, t, C_HI);
    cv_text(10, 28, &FONT_M, VOICE_NAME[ui.voice], tack_muted((uint8_t)ui.voice) ? C_GRAY : C_HI);
    cv_text(150, 34, &FONT_XS, ui.step_mode ? "STEP" : "PLAY", C_WHITE);
    t[0] = 'B';
    int_text(ui.bar + 1, t + 1);
    cv_text(190, 34, &FONT_XS, t, C_GRAY);
    if (tack_recording())
        cv_text(214, 34, &FONT_XS, "REC", C_HI);
    for (i = 0; i < TACK_NP; i++) {
        cv_text(10 + (int32_t)i * 56, 74, &FONT_XS, P_NAME[i], C_GRAY);
        int_text(tack_get_param(tack_voice_param((uint32_t)ui.voice, i)), t);
        cv_text(10 + (int32_t)i * 56, 90, &FONT_S, t, C_WHITE);
    }
    cv_blit(0, 0);
    cv_begin(240, 28, C_BLACK);                  /* the 16 steps of the page */
    for (i = 0; i < 16u; i++) {
        const uint32_t s = (uint32_t)ui.bar * 16u + i;
        const int on = tack_step_on((uint8_t)ui.voice, (uint8_t)s);
        const int here = tack_running() && tack_playhead((uint8_t)ui.voice) == (int)s;
        cv_rect(8 + (int32_t)i * 14, 4, 12, 18, here ? C_WHITE : on ? C_HI : C_LINE);
    }
    cv_blit(0, 150);
    cv_begin(240, 24, C_BLACK);                  /* the load: what the hardware test reads */
    cv_text(10, 4, &FONT_XS, "CPU", C_GRAY);
    int_text((int32_t)plat_cpu_pct(), t);
    cv_text(36, 4, &FONT_XS, t, C_HI);
    cv_text(68, 4, &FONT_XS, "PEAK", C_GRAY);
    int_text((int32_t)ui.cpu_peak, t);
    cv_text(102, 4, &FONT_XS, t, C_HI);
    cv_text(140, 4, &FONT_XS, "XRUN", C_GRAY);
    int_text((int32_t)plat_xruns(), t);
    cv_text(174, 4, &FONT_XS, t, C_HI);
    cv_blit(0, 206);
}

void app_ui_init(void)
{
    ui.step_mode = 1;
    lcd_fill(0, 0, 240, 240, C_BLACK);
    ui.redraw = 1;
}

void app_frame(void)
{
    static uint32_t last, seen;
    static int head;
    uint32_t now = plat_ms(), pct = plat_cpu_pct();
    int h = tack_playhead((uint8_t)ui.voice);
    input();
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
    else if (!strcmp(what, "bar"))
        *got = ui.bar;
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
