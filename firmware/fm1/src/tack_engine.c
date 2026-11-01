/* SPDX-License-Identifier: GPL-3.0-only */
#include "tack_engine.h"
#include "lxr_core.h"
#include "kit_mutate.h"
#include "lfo_speed.h"
#include "Parameters.h"
#include "Soundengine/Sequencer/sequencer.h"
#include "Soundengine/Sequencer/clockSync.h"
#include "Soundengine/Sequencer/EuklidGenerator.h"
#include "Soundengine/MIDI/MidiMessages.h"
#include "seq_platform.h"
#include "Soundengine/DSPAudio/mixer.h"
#include "Soundengine/FxBus.h"
#include "Soundengine/DSPAudio/automationNode.h"
#include "Soundengine/MIDI/MidiParser.h"
#include <string.h>

_Static_assert(NUM_PATTERN == TACK_PATTERNS, "fm1.toml's NUM_PATTERN");
_Static_assert(LXR_HOST_NUM_PARAMS <= 248, "app.c keeps the kit's name after its parameters");

/* the hats are two voices on one engine: they share pitch, filter and volume, and have a decay each */
static const uint16_t VOICE_PARAM[TACK_VOICES][TACK_NP] = {
    {PAR_COARSE1, PAR_FILTER_FREQ_1, PAR_VELOD1, PAR_VOL1},
    {PAR_COARSE2, PAR_FILTER_FREQ_2, PAR_VELOD2, PAR_VOL2},
    {PAR_COARSE3, PAR_FILTER_FREQ_3, PAR_VELOD3, PAR_VOL3},
    {PAR_COARSE4, PAR_FILTER_FREQ_4, PAR_VELOD4, PAR_VOL4},
    {PAR_COARSE5, PAR_FILTER_FREQ_5, PAR_VELOD5, PAR_VOL5},
    {PAR_COARSE6, PAR_FILTER_FREQ_6, PAR_VELOD6_CLOSED, PAR_VOL6},
    {PAR_COARSE6, PAR_FILTER_FREQ_6, PAR_VELOD6_OPEN, PAR_VOL6},
};
uint16_t tack_voice_param(uint32_t voice, uint32_t slot) { return VOICE_PARAM[voice][slot]; }

typedef struct { const char *const *names; const uint8_t *fw; uint8_t count; } TackEnum;
typedef struct { uint8_t group; const char *title; uint8_t graph; TackSlot s[4]; } TackPage;
#include "tack_pages.h"
#include "tack_shapes.h"
_Static_assert(sizeof SHAPES / sizeof SHAPES[0] == TACK_SHAPE_N, "the tables");

static const TackPage *page_at(uint32_t voice, uint32_t group, uint32_t sub)
{
    for (uint32_t i = 0; i < TACK_PAGE_N[voice]; i++)
        if (TACK_PAGE[voice][i].group == group && sub-- == 0u)
            return &TACK_PAGE[voice][i];
    return 0;
}
int tack_pages(uint32_t voice, uint32_t group)
{
    int n = 0;
    for (uint32_t i = 0; i < TACK_PAGE_N[voice]; i++)
        n += TACK_PAGE[voice][i].group == group;
    return n;
}
const char *tack_page_title(uint32_t voice, uint32_t group, uint32_t sub)
{
    const TackPage *p = page_at(voice, group, sub);
    return p ? p->title : "";
}
const TackSlot *tack_slot(uint32_t voice, uint32_t group, uint32_t sub, uint32_t slot)
{
    const TackPage *p = page_at(voice, group, sub);
    return p && p->s[slot].label ? &p->s[slot] : 0;
}
int tack_page_graph(uint32_t voice, uint32_t group, uint32_t sub)
{
    const TackPage *p = page_at(voice, group, sub);
    return p ? p->graph : TACK_GRAPH_NONE;
}
const TackSlot *tack_graph_slot(uint32_t voice, int graph, uint32_t slot)
{
    for (uint32_t i = 0; i < TACK_PAGE_N[voice]; i++) {
        const TackPage *p = &TACK_PAGE[voice][i];
        if (p->group != TACK_G_MAIN && p->graph == graph && p->s[slot].label)
            return &p->s[slot];
    }
    return 0;
}
/* a modulation destination's value is an index with gaps: the enum lists the ones on offer, in order */
static int32_t enum_pos(const TackEnum *e, uint8_t value)
{
    if (!e->fw)
        return value;
    for (int32_t i = 0; i < e->count; i++)
        if (e->fw[i] == value)
            return i;
    return 0;
}
static int self_target(const char *name)
{
    for (; *name; name++)
        if (name[0] == 'V' && name[1] == 'E' && name[2] == 'L' && name[3] == ' ')
            return 1;
    return 0;
}

uint8_t tack_slot_step(const TackSlot *s, uint8_t value, int32_t e)
{
    int32_t top = s->max, v = value;
    if (!s->en) {
        const int32_t low = s->par >= PAR_PWM_1 && s->par <= PAR_PWM_6;   /* the engine plays 0 as 1: adjustValue */
        v += e;
        return (uint8_t)(v < low ? low : v > top ? top : v);
    }
    const TackEnum *en = &TACK_ENUM[s->en - 1];
    const int32_t from = enum_pos(en, value);
    top = en->count - 1;
    v = from + e;
    v = v < 0 ? 0 : v > top ? top : v;
    /* a target past velocity's own settings: a voice's hit would rewrite what its velocity does. A kit may still hold one */
    while (s->target && self_target(en->names[v]) && v != from) {
        const int32_t next = v + (e > 0 ? 1 : -1);
        if (next < 0 || next > top)
            return value;
        v = next;
    }
    return en->fw ? en->fw[v] : (uint8_t)v;
}
/* An LFO's two settings are one idea on the panel: SYNC is on or off, and FREQ is a rate or a fraction of the bar */
static uint8_t shadow[LXR_HOST_NUM_PARAMS];

static int lfo_freq_slot(const TackSlot *s) { return s->par >= PAR_FREQ_LFO1 && s->par <= PAR_FREQ_LFO6; }
static int lfo_sync_slot(const TackSlot *s) { return s->par >= PAR_SYNC_LFO1 && s->par <= PAR_SYNC_LFO6; }

/* RETRIG's value holds both: 1-6 a voice, 7-12 the same voice once, as once means nothing without a voice to start it */
static int lfo_retrig_par(const TackSlot *s) { return s->par >= PAR_RETRIGGER_LFO1 && s->par <= PAR_RETRIGGER_LFO6; }
static int lfo_once_slot(const TackSlot *s) { return lfo_retrig_par(s) && s->label[0] == 'O'; }

int tack_slot_toggles(const TackSlot *s) { return s->max == 1u || lfo_sync_slot(s) || lfo_once_slot(s); }

int tack_slot_turn(const TackSlot *s, int32_t e)
{
    if (lfo_freq_slot(s)) {
        const uint16_t sync = (uint16_t)(PAR_SYNC_LFO1 + (s->par - PAR_FREQ_LFO1));
        const int32_t now = shadow[sync], to = now + e;
        if (!now)
            return 0;
        tack_post_param(sync, (uint8_t)(to < 1 ? 1 : to >= (int32_t)LFO_SPEED_FRACTIONS ? (int32_t)LFO_SPEED_FRACTIONS - 1 : to));
        return 1;
    }
    if (lfo_once_slot(s)) {
        const uint8_t now = shadow[s->par];
        if (e > 0 && now <= 6u)                  /* with no voice to start it, its own */
            tack_post_param(s->par, (uint8_t)((now ? now : s->par - PAR_RETRIGGER_LFO1 + 1u) + 6u));
        else if (e < 0 && now > 6u)
            tack_post_param(s->par, (uint8_t)(now - 6u));
        return 1;
    }
    if (lfo_retrig_par(s)) {
        const int32_t now = shadow[s->par], once = now > 6, to = (once ? now - 6 : now) + e;
        const int32_t voice = to < 0 ? 0 : to > 6 ? 6 : to;
        tack_post_param(s->par, (uint8_t)(voice && once ? voice + 6 : voice));
        return 1;
    }
    if (lfo_sync_slot(s)) {
        const uint16_t freq = (uint16_t)(PAR_FREQ_LFO1 + (s->par - PAR_SYNC_LFO1));
        const uint8_t now = shadow[s->par];
        if (e > 0 && !now) {
            tack_post_param(s->par, lfo_sync_near(shadow[freq], (float)tack_bpm()));
        } else if (e < 0 && now) {
            tack_post_param(freq, lfo_freq_near(now, (float)tack_bpm()));
            tack_post_param(s->par, 0);
        }
        return 1;
    }
    return 0;
}

const char *tack_slot_text(const TackSlot *s, uint8_t value)
{
    if (lfo_freq_slot(s)) {
        const uint8_t sync = shadow[PAR_SYNC_LFO1 + (s->par - PAR_FREQ_LFO1)];
        return sync ? lfo_fraction_name(sync) : 0;
    }
    if (lfo_sync_slot(s))
        return value ? "ON" : "OFF";
    if (lfo_once_slot(s))
        return value > 6u ? "ON" : "OFF";
    if (lfo_retrig_par(s) && value > 6u)
        value = (uint8_t)(value - 6u);
    if (!s->en)
        return 0;
    const TackEnum *en = &TACK_ENUM[s->en - 1];
    const int32_t p = enum_pos(en, value);
    return en->names[p < en->count ? p : en->count - 1];
}

/* the LXR's FX types, FxBus.h: their settings, the two that change the sound most on the first page. Reverb is compiled
 * out, so a kit that asks for it (type 5) plays dry and reads as such. Slots 2 and 3 are the first page's, 4 to 6 the second's.
 * The delay's time stops at 126: with range at 127 too it reaches the buffer's length, and that is silence */
#define NO {"-", 0, 0}
typedef struct { const char *label; uint16_t par; uint8_t max; } FxSetting;
static const struct {
    const char *name;
    FxSetting s[5];
} FX_KIND[] = {
    {"OFF", {NO, NO, NO, NO, NO}},
    {"DIST", {{"DRIVE", PAR_FX_P1, 127}, {"TONE", PAR_FX_P3, 127}, {"COLOR", PAR_FX_P2, 127}, {"VOLUME", PAR_FX_P4, 127},
              {"MODE", PAR_FX_DIST_TYPE, 2}}},
    {"RING", {{"FREQ", PAR_FX_P1, 127}, {"WAVE", PAR_FX_WAVE, 5}, NO, NO, NO}},
    {"COMP", {{"RATIO", PAR_FX_RATIO, 5}, {"THRESH", PAR_FX_P3, 127}, {"ATTACK", PAR_FX_P1, 127}, {"RELEASE", PAR_FX_P2, 127},
              {"GAIN", PAR_FX_P4, 127}}},
    {"DELAY", {{"TIME", PAR_FX_P1, 126}, {"FEEDBK", PAR_FX_P4, 127}, {"RANGE", PAR_FX_P2, 127}, {"TONE", PAR_FX_P3, 127},
               {"MODE", PAR_FX_DELAY_TYPE, 3}}},
    {"VERB", {{"PRE", PAR_FX_P1, 127}, {"DECAY", PAR_FX_P2, 127}, {"TONE", PAR_FX_P3, 127}, {"EARLY", PAR_FX_P4, 127}, NO}},
};
#undef NO
#define FX_TYPES 6u                     /* what the FX page can choose: not the reverb row */
static uint32_t fx_kind(void)
{
    const uint32_t t = tack_get_param(PAR_FX_TYPE);
    return t < sizeof FX_KIND / sizeof FX_KIND[0] ? t : 0u;
}
uint16_t tack_fx_param(uint32_t slot)
{
    return slot == TACK_FX_TYPE ? PAR_FX_TYPE : slot == TACK_FX_MIX ? PAR_FX_DRY_WET : FX_KIND[fx_kind()].s[slot - 2u].par;
}
uint8_t tack_fx_max(uint32_t slot)
{
    return slot == TACK_FX_TYPE ? FX_TYPES - 1u : slot == TACK_FX_MIX ? 100 : FX_KIND[fx_kind()].s[slot - 2u].max;
}
const char *tack_fx_label(uint32_t slot)
{
    return slot == TACK_FX_TYPE ? "TYPE" : slot == TACK_FX_MIX ? "MIX" : FX_KIND[fx_kind()].s[slot - 2u].label;
}
int tack_fx_pages(void) { return FX_KIND[fx_kind()].s[2].max ? 2 : 1; }
/* a mode setting's name, 0 for a plain number */
/* TIME, on a delay in sync: its value is a fraction */
static int fx_time_sync(uint32_t slot)
{
    return fx_kind() == FX_DELAY && tack_get_param(PAR_FX_DELAY_TYPE) >= 2u && tack_fx_param(slot) == PAR_FX_P1;
}

int tack_fx_step(uint32_t slot) { return fx_time_sync(slot) ? 13 : 1; }

const char *tack_fx_text(uint32_t slot, uint8_t value)
{
    static const char *const DIST[3] = {"TUBE", "FOLD", "RAT"}, *const DELAY[4] = {"NORMAL", "PING", "SYNC", "PP SYNC"};
    const uint16_t p = tack_fx_param(slot);
    if (slot < 2u)
        return 0;
    if (fx_time_sync(slot))
        return Delay_syncName((float)value / 127.0f);
    return p == PAR_FX_DIST_TYPE ? DIST[value % 3u] : p == PAR_FX_DELAY_TYPE ? DELAY[value % 4u] : 0;
}
/* calcDelayTime in Delay.c: range sets the span, and time is scaled by range as well, so at a low range the whole dial is
 * a comb. Milliseconds at this sample rate, 0 unless the type is delay */
int tack_fx_delay_ms(void)
{
    if (fx_kind() != FX_DELAY)
        return 0;
    if (tack_get_param(PAR_FX_DELAY_TYPE) >= 2u)
        return (int)(Delay_syncSamples(&fxBus.delay, tack_get_param(PAR_FX_P1) / 127.0f) * 1000.0f / TACK_RATE + 0.5f);
    const float r = tack_get_param(PAR_FX_P2) / 127.0f, t = tack_get_param(PAR_FX_P1) / 127.0f;
    const float top = tack_get_param(PAR_FX_DELAY_TYPE) ? FX_BUFFER_SIZE * 0.48f : (float)FX_BUFFER_SIZE;
    const float fs = TACK_RATE / 1000.0f;
    const float low = 0.6f * fs * (1.0f - r) + 20.0f * fs * r, high = 20.0f * fs * (1.0f - r) + top * r;
    const float cv = t * r;
    return (int)((low * (1.0f - cv) + high * cv) / fs + 0.5f);
}
const char *tack_fx_type_name(void) { return FX_KIND[fx_kind()].name; }
static uint16_t fx_route(uint32_t voice) { return (uint16_t)(PAR_AUDIO_OUT1 + (voice < TACK_FX_SENDS ? voice : TACK_FX_SENDS - 1u)); }
int tack_fx_send(uint32_t voice) { return tack_get_param(fx_route(voice)) == MIXER_ROUTING_FX_BUS; }
int tack_post_fx_send(uint32_t voice, uint8_t on)
{
    return tack_post_param(fx_route(voice), on ? MIXER_ROUTING_FX_BUS : MIXER_ROUTING_DAC1_STEREO);
}
/* the send as a dial on OUTPUT, which reads as OFF or ON: a route of 0 is the first name, and any other the last. A turn goes
 * through tack_post_fx_send, as its values are routes */
const TackSlot *tack_send_slot(uint32_t voice)
{
    static TackSlot send[TACK_FX_SENDS];
    const uint32_t i = voice < TACK_FX_SENDS ? voice : TACK_FX_SENDS - 1u;
    static const uint8_t OFF_ON = 6u;            /* en of tack_pages.h's {"OFF","ON"} */
    send[i] = (TackSlot){"FX SEND", fx_route(voice), 1, OFF_ON, 0, 0};
    return &send[i];
}
uint16_t tack_rate_param(void) { return PAR_VOICE_DECIMATION_ALL; }

/* one writer (the main loop), one reader (the audio interrupt): each index is written by one side only */
enum { EV_NOTE, EV_PARAM, EV_STEP, EV_RUN, EV_BPM, EV_SHUFFLE, EV_PATTERN, EV_MUTE, EV_RECORD, EV_EXT, EV_STRESS, EV_LENGTH,
       EV_LOCK, EV_LOCK_CLEAR, EV_KIT, EV_ROLL, EV_ROLL_RATE, EV_EUCLID, EV_COPY, EV_CLEAR, EV_STEP_EDIT, EV_LANE_NUDGE,
       EV_CHAIN, EV_SONG_ROW, EV_SONG_ON, EV_STEP_NOTE, EV_NOTE_OFF, EV_RECUR, EV_SHAPE, EV_EU_UNDO, EV_CHAIN_SONG, EV_LANE_MODE, EV_BITS, EV_MORPH, EV_MORPH_KIT, EV_KIT_HOME, EV_LANE_START, EV_MUTATE, EV_EXTEND };
#define EV_N 128u
typedef struct { uint8_t kind, a, b, c, d; } Ev;
static Ev ev[EV_N];
static volatile uint32_t ev_w, ev_r, ev_dropped;

static int post5(uint8_t kind, uint8_t a, uint8_t b, uint8_t c, uint8_t d)
{
    uint32_t w = ev_w;
    if (((w + 1u) & (EV_N - 1u)) == ev_r) {
        ev_dropped++;
        return 0;
    }
    ev[w] = (Ev){kind, a, b, c, d};
    ev_w = (w + 1u) & (EV_N - 1u);
    return 1;
}
static int post(uint8_t kind, uint8_t a, uint8_t b, uint8_t c) { return post5(kind, a, b, c, 0); }

/* the main loop's view of the kit: a value posted reads back at once, before the audio interrupt has applied it, so
 * the screen is never a step behind and a fast encoder's detents build on each other. The other values likewise */
static uint16_t ui_bpm = 120;
static uint8_t ui_shuffle, ui_record, ui_ext;
static uint8_t ui_roll = 5;              /* the sequencer's default, 1/16 */
static uint8_t ui_eu[TACK_VOICES][TACK_EU_SLOTS];
static volatile uint8_t mute_mask;
static volatile uint8_t stress;
static volatile uint32_t dirty;          /* patterns changed since they were saved */
static void kit_for_pattern(uint8_t kit);
static uint8_t morph_a[LXR_HOST_NUM_PARAMS];    /* the kit as it is, unblended: the engine's own array holds what is heard */
extern uint8_t activeParameters[];

static int apply_locks(uint8_t track, uint8_t step);
static void locks_init(void);
static int clock_pulse(void);
static float wheel_each;                        /* ticks between pulses */
static uint8_t wheel_on, wheel_over;            /* the flywheel below is running, and the pulses it ran past the clock: the
                                                 * next ones from outside are already played */
static uint8_t clock_w, clock_held;             /* external clock: the next slot, the times held */
static int recur_plays(uint8_t track, uint8_t step, int8_t position);
static uint8_t bits_of[NUM_PATTERN];            /* how far each pattern takes BITS: 0 not at all, 127 the fewest bits */
static uint8_t morph_of[NUM_PATTERN];           /* how far each pattern is morphed: 0 not at all, 127 all the way */
static uint8_t morph_kit_of[NUM_PATTERN];       /* the bank's kit it morphs to: slot + 1, 0 none */
_Static_assert(LXR_HOST_NUM_PARAMS <= 248u, "a kit's slot in the bank, and in a pattern");
typedef struct { uint8_t name[8], v[248]; } PatternKit;
static void morph_kinds(void);
static void morph_capture(void);
static void morph_after_kit(void);
static void lane_wrap(uint8_t track);
static void lanes_reset(void);

void tack_init(void)
{
    lxr_host_init();
    seq_init();
    locks_init();
    seq_platform_set_locks(apply_locks);
    seq_platform_set_clock_pulse(clock_pulse);
    seq_platform_set_recurrence(recur_plays, lane_wrap);
    lanes_reset();
    seq_copyPatternToBackupBuffer(0);   /* as the LXR at boot: a pattern change restores from this buffer */
    seq_platform_set_kit_load(kit_for_pattern);
    activeParameters[PAR_PATTERN_MODE] = 1;    /* a pattern loads its kit: seq_setPendingPatternActive */
    seq_setBpm(ui_bpm);
    euklid_init();
    for (uint32_t v = 0; v < TACK_VOICES; v++) {
        ui_eu[v][TACK_EU_LENGTH] = 16;
        ui_eu[v][TACK_EU_HITS] = 4;
    }
    for (uint32_t i = 0; i < LXR_HOST_NUM_PARAMS; i++)
        shadow[i] = lxr_host_getParam((uint16_t)i);
    morph_kinds();
    morph_capture();
}

int tack_post_note(uint8_t voice, uint8_t note, uint8_t vel) { return post(EV_NOTE, voice, note, vel); }
int tack_post_note_off(uint8_t voice, uint8_t note) { return post(EV_NOTE_OFF, voice, note, 0); }
int tack_post_recur(uint8_t voice, uint8_t step, uint8_t len, uint8_t mask) { return post5(EV_RECUR, voice, step, len, mask); }
int tack_post_param(uint16_t param, uint8_t value)
{
    if (param >= LXR_HOST_NUM_PARAMS || !post(EV_PARAM, (uint8_t)param, (uint8_t)(param >> 8), value))
        return 0;
    shadow[param] = value;
    return 1;
}
int tack_post_step(uint8_t voice, uint8_t step, uint8_t on) { return post(EV_STEP, voice, step, on); }
int tack_post_step_note(uint8_t voice, uint8_t step, uint8_t note, uint8_t vel)
{
    return post5(EV_STEP_NOTE, voice, step, note, vel);
}
int tack_post_run(uint8_t on) { return post(EV_RUN, on, 0, 0); }
int tack_post_bpm(uint16_t bpm)
{
    if (!post(EV_BPM, (uint8_t)bpm, (uint8_t)(bpm >> 8), 0))
        return 0;
    ui_bpm = bpm;
    return 1;
}
int tack_post_shuffle(uint8_t amount)
{
    if (!post(EV_SHUFFLE, amount, 0, 0))
        return 0;
    ui_shuffle = amount;
    return 1;
}
int tack_post_pattern(uint8_t pattern) { return post(EV_PATTERN, pattern, 0, 0); }
int tack_post_chain(uint8_t index, uint8_t pattern) { return post(EV_CHAIN, index, pattern, 0); }
int tack_post_song_row(uint8_t row, uint8_t value) { return post(EV_SONG_ROW, row, value, 0); }
int tack_post_song_on(uint8_t on) { return post(EV_SONG_ON, on, 0, 0); }
int tack_post_bits(uint8_t value) { return post(EV_BITS, value, 0, 0); }
int tack_post_kit_home(uint8_t slot) { return post(EV_KIT_HOME, slot, 0, 0); }
int tack_post_chain_to_song(uint8_t row) { return post(EV_CHAIN_SONG, row, 0, 0); }
int tack_post_mute(uint8_t voice, uint8_t muted) { return post(EV_MUTE, voice, muted, 0); }
int tack_post_record(uint8_t on)
{
    if (!post(EV_RECORD, on, 0, 0))
        return 0;
    ui_record = on;
    return 1;
}
int tack_post_ext_sync(uint8_t on)
{
    if (!post(EV_EXT, on, 0, 0))
        return 0;
    ui_ext = on;
    return 1;
}
int tack_post_stress(uint8_t on) { return post(EV_STRESS, on, 0, 0); }
int tack_post_length(uint8_t voice, uint8_t length) { return post(EV_LENGTH, voice, length, 0); }
int tack_post_extend(uint8_t voice, uint8_t length) { return post(EV_EXTEND, voice, length, 0); }
int tack_post_lock(uint8_t voice, uint8_t step, uint16_t param, uint8_t value)
{
    return post5(EV_LOCK, voice, step, (uint8_t)param, value);
}
int tack_post_lock_clear(uint8_t voice, uint8_t step) { return post(EV_LOCK_CLEAR, voice, step, 0); }
int tack_post_roll(uint8_t voice, uint8_t on) { return post(EV_ROLL, voice, on, 0); }
int tack_post_roll_rate(uint8_t rate)
{
    if (rate >= TACK_ROLL_RATES || !post(EV_ROLL_RATE, rate, 0, 0))
        return 0;
    ui_roll = rate;
    return 1;
}
uint8_t tack_roll_rate(void) { return ui_roll; }
const char *tack_roll_name(uint8_t rate)
{
    static const char *const NAME[TACK_ROLL_RATES] = {"ONE HIT", "1/1", "1/2", "1/4", "1/8", "1/16"};
    return NAME[rate < TACK_ROLL_RATES ? rate : 0];
}
uint8_t tack_euclid_get(uint8_t voice, uint32_t slot) { return ui_eu[voice][slot]; }
int tack_euclid_turn(uint8_t voice, uint32_t slot, int32_t e)
{
    uint8_t *u = ui_eu[voice];
    const int32_t top = slot == TACK_EU_LENGTH ? TACK_EU_MAX : slot == TACK_EU_HITS ? u[TACK_EU_LENGTH]
                                                                                         : u[TACK_EU_LENGTH] - 1;
    const int32_t bottom = slot == TACK_EU_ROTATE ? 0 : 1;
    const int32_t v = (int32_t)u[slot] + e;
    u[slot] = (uint8_t)(v < bottom ? bottom : v > top ? top : v);
    if (u[TACK_EU_HITS] > u[TACK_EU_LENGTH])
        u[TACK_EU_HITS] = u[TACK_EU_LENGTH];
    if (u[TACK_EU_ROTATE] >= u[TACK_EU_LENGTH])
        u[TACK_EU_ROTATE] = (uint8_t)(u[TACK_EU_LENGTH] - 1u);
    return post5(EV_EUCLID, voice, u[TACK_EU_LENGTH], u[TACK_EU_HITS], u[TACK_EU_ROTATE]);
}
int tack_post_euclid_undo(uint8_t voice) { return post(EV_EU_UNDO, voice, 0, 0); }
void tack_euclid_length(uint8_t voice, uint32_t length)
{
    uint8_t *u = ui_eu[voice];
    u[TACK_EU_LENGTH] = (uint8_t)(length > TACK_EU_MAX ? TACK_EU_MAX : length);
    if (u[TACK_EU_HITS] > u[TACK_EU_LENGTH])
        u[TACK_EU_HITS] = u[TACK_EU_LENGTH];
    if (u[TACK_EU_ROTATE] >= u[TACK_EU_LENGTH])
        u[TACK_EU_ROTATE] = (uint8_t)(u[TACK_EU_LENGTH] - 1u);
}
_Static_assert(sizeof(StepTiming) == 1u, "the backup buffer copies step timing as bytes");
#define SHIFT_ZERO STEP_SHIFT_ZERO
#define FLAMS 6
int tack_step_get(uint8_t voice, uint8_t step, uint32_t slot)
{
    const Step *p = &seq_patternSet.seq_subStepPattern[seq_activePattern][voice][step];
    const StepTiming *t = &seq_patternSet.seq_stepTiming[seq_activePattern][voice][step];
    switch (slot) {
    case TACK_ST_NUDGE: return seq_shiftPulses(t->shift);
    case TACK_ST_FLAM: return t->flam;
    case TACK_ST_PROB: return p->prob;
    case TACK_ST_NOTE: return p->note;
    default: return p->volume & STEP_VOLUME_MASK;
    }
}
int tack_step_turn(uint8_t voice, uint8_t step, uint32_t slot, int32_t e)
{
    static const int8_t TOP[TACK_ST_SLOTS] = {2 * SHIFT_ZERO, FLAMS - 1, 100, 127, 127};
    int32_t v = (slot == TACK_ST_NUDGE ? (int32_t)seq_patternSet.seq_stepTiming[seq_activePattern][voice][step].shift : tack_step_get(voice, step, slot)) + e;
    v = v < 0 ? 0 : v > TOP[slot] ? TOP[slot] : v;
    return post5(EV_STEP_EDIT, voice, step, (uint8_t)slot, (uint8_t)v);
}
const char *tack_flam_name(int flam)
{
    static const char *const NAME[FLAMS] = {"OFF", "X2", "X3", "X4", "X6", "X8"};
    return NAME[flam >= 0 && flam < FLAMS ? flam : 0];
}
int tack_lane_nudge_get(uint8_t voice)
{
    const int len = tack_length(voice);
    for (int s = 0; s < len; s++)
        if (tack_step_on(voice, (uint8_t)s))
            return tack_step_get(voice, (uint8_t)s, TACK_ST_NUDGE);
    return 0;
}
int tack_lane_nudge_turn(uint8_t voice, int32_t e)
{
    e = e < -2 * SHIFT_ZERO ? -2 * SHIFT_ZERO : e > 2 * SHIFT_ZERO ? 2 * SHIFT_ZERO : e;
    return post(EV_LANE_NUDGE, voice, (uint8_t)(e + 2 * SHIFT_ZERO), 0);
}
int tack_post_copy(uint8_t dst) { return post(EV_COPY, dst, 0, 0); }
int tack_post_clear(uint8_t voice) { return post(EV_CLEAR, voice, 0, 0); }
int tack_post_shape(uint8_t voice, uint32_t shape, uint32_t shift, uint32_t centre, uint32_t amount)
{
    return post5(EV_SHAPE, voice, (uint8_t)(shape | shift << 4), (uint8_t)centre, (uint8_t)amount);
}
const char *tack_shape_name(uint32_t shape) { return shape && shape <= TACK_SHAPE_N ? SHAPES[shape - 1u].name : "OFF"; }
uint32_t tack_shape_steps(uint32_t shape) { return shape && shape <= TACK_SHAPE_N ? SHAPES[shape - 1u].n : 1u; }

/* a kit in the audio interrupt: the engine has one writer. Its parameters take a block's time, as on the LXR */
static const uint8_t *volatile kit_pending;
static volatile uint32_t kit_applied;
static uint32_t kit_seen;
static volatile int kit_now;
static TackKitFind kit_find;
int tack_kit_now(void) { return kit_now; }
void tack_set_kit_now(int kit) { kit_now = kit; }
void tack_set_kit_find(TackKitFind fn) { kit_find = fn; }

/* Each pattern keeps its own copy of its kit, so that a sound comes back with the pattern, edits and all, whatever happens to the
 * bank. kitNr says which of the bank's kits it came from, for a revert. morph_a is the sound of kit_pattern */
static PatternKit kit_of[NUM_PATTERN];
static uint16_t kit_has;                         /* a bit each: the pattern has one */
static uint8_t kit_pattern;
static uint8_t kit_name_now[8];

static void kit_keep(uint32_t pattern)
{
    memcpy(kit_of[pattern].v, morph_a, sizeof morph_a);
    memcpy(kit_of[pattern].name, kit_name_now, sizeof kit_name_now);
    kit_has = (uint16_t)(kit_has | 1u << pattern);
}

static int pattern_has_steps(uint32_t pattern)
{
    for (uint32_t v = 0; v < NUM_TRACKS; v++)
        for (uint32_t b = 0; b < NUM_BARS; b++)
            if (seq_patternSet.seq_mainSteps[b][pattern][v])
                return 1;
    return 0;
}

const uint8_t *tack_kit_name(void) { return kit_name_now; }
void tack_set_kit_name(const uint8_t *name) { memcpy(kit_name_now, name, sizeof kit_name_now); }
const uint8_t *tack_pattern_kit(uint32_t pattern) { return kit_has >> pattern & 1u ? kit_of[pattern].v : 0; }
const uint8_t *tack_pattern_kit_name(uint32_t pattern) { return kit_of[pattern].name; }
int tack_pattern_kit_slot(uint32_t pattern) { return seq_patternSet.kitNr[pattern]; }

/* The sequencer is switching patterns, and has just turned the FX off to save cycles, as the LXR does around a kit load */
static void kit_for_pattern(uint8_t kit)
{
    const uint32_t was = kit_pattern, now = seq_activePattern;
    if (was != now && (kit_has >> was & 1u || dirty >> was & 1u || pattern_has_steps(was)))
        kit_keep(was);                           /* what is playing is the pattern's: its edits go with it */
    kit_pattern = (uint8_t)now;
    if (was != now && kit_has >> now & 1u) {
        lxr_host_loadKit(kit_of[now].v);
        morph_capture();
        memcpy(kit_name_now, kit_of[now].name, sizeof kit_name_now);
        kit_now = kit;
        kit_applied++;
    } else {                                     /* none of its own yet, as a new pattern: it plays on what is playing */
        fxBus.fxType = lxr_host_getParam(PAR_FX_TYPE);
    }
    morph_after_kit();                           /* before this step's hits read the parameters */
}

int tack_post_kit(const uint8_t *values, uint8_t slot)
{
    if (kit_pending)
        return 0;
    kit_pending = values;
    if (!post(EV_KIT, slot, 0, 0)) {
        kit_pending = 0;
        return 0;
    }
    return 1;
}
int tack_kit_busy(void)
{
    if (kit_seen != kit_applied) {             /* the kit is in: the main loop's view follows */
        kit_seen = kit_applied;
        memcpy(shadow, morph_a, sizeof shadow);
    }
    return kit_pending != 0;
}

uint8_t tack_get_param(uint16_t param) { return param < LXR_HOST_NUM_PARAMS ? shadow[param] : 0; }
uint32_t tack_dropped_events(void) { return ev_dropped; }
uint16_t tack_bpm(void) { return ui_ext ? seq_getBpm() : ui_bpm; }   /* on external clock, the clock's */
uint8_t tack_shuffle(void) { return ui_shuffle; }
int tack_recording(void) { return ui_record; }
int tack_ext_sync(void) { return ui_ext; }
int tack_running(void) { return seq_isRunning(); }
int tack_pattern(void) { return seq_activePattern; }
int tack_muted(uint8_t voice) { return (mute_mask >> voice) & 1u; }
int tack_playhead(uint8_t voice) { return seq_getStepIndex(voice); }
int tack_length(uint8_t voice)
{
    const uint8_t n = seq_patternSet.length[seq_activePattern][voice];
    return n ? n : 16;
}
/* A pattern's locks are a list, sorted by voice, step and destination, so that a step holds as many as it needs and a step
 * with none costs nothing. A lock is 3 bytes: a the step and the voice's low two bits, dest, b the value and the voice's
 * third bit. The sequencer's own two slots in a step stay unused.
 *
 * The engine's CC numbers are one above the parameter numbers (sendCC in lxr_core.c), and automation holds CC numbers. Above
 * 127 it holds the parameter, sent as CC2 (autoNode_updateValue) */
typedef struct { uint8_t a, dest, b; } Lock;
_Static_assert(sizeof(Lock) == 3u, "a lock is 3 bytes in the image");
typedef struct {
    uint16_t n;
    Lock l[TACK_LOCKS_MAX];
} LockList;
static INSEQRAM LockList lock_list[NUM_PATTERN];
static AutomationNode lock_node[NUM_TRACKS][TACK_STEP_LOCKS];
static uint8_t lock_node_n[NUM_TRACKS];

static uint8_t lock_dest(uint16_t param) { return (uint8_t)(param < 127u ? param + 1u : param); }
static uint32_t lock_key(const Lock *l) { return (uint32_t)(l->b >> 7) << 8 | l->a; }
static uint32_t step_key(uint8_t voice, uint8_t step) { return (uint32_t)voice << 6 | step; }

static void locks_init(void)
{
    for (uint32_t p = 0; p < NUM_PATTERN; p++)
        lock_list[p].n = 0;
    for (uint32_t t = 0; t < NUM_TRACKS; t++)
        for (uint32_t i = 0; i < TACK_STEP_LOCKS; i++)
            autoNode_init(&lock_node[t][i]);
}

/* the first lock at or after key */
static uint32_t recur_count(uint8_t pattern);

static uint32_t lock_lower(const LockList *p, uint32_t key)
{
    uint32_t lo = 0, hi = p->n;
    while (lo < hi) {
        const uint32_t mid = (lo + hi) / 2u;
        if (lock_key(&p->l[mid]) < key)
            lo = mid + 1u;
        else
            hi = mid;
    }
    return lo;
}

static uint32_t step_locks(const LockList *p, uint32_t first, uint32_t key)
{
    uint32_t i = first;
    while (i < p->n && lock_key(&p->l[i]) == key)
        i++;
    return i - first;
}

/* the sequencer, as a voice fires: as its two automation nodes, what the last step set goes back and this one's goes on */
static int apply_locks(uint8_t track, uint8_t step)
{
    const LockList *p = &lock_list[seq_activePattern];
    const uint32_t key = step_key(track, step), first = lock_lower(p, key), n = step_locks(p, first, key);
    AutomationNode *node = lock_node[track];
    for (uint32_t i = 0; i < lock_node_n[track]; i++)
        autoNode_setDestination(&node[i], NO_AUTOMATION);
    for (uint32_t i = 0; i < n && i < TACK_STEP_LOCKS; i++) {
        autoNode_setDestination(&node[i], p->l[first + i].dest);
        autoNode_updateValue(&node[i], p->l[first + i].b & 0x7Fu);
    }
    lock_node_n[track] = (uint8_t)(n < TACK_STEP_LOCKS ? n : TACK_STEP_LOCKS);
    return 1;
}

int tack_lock_get(uint8_t voice, uint8_t step, uint16_t param, uint8_t *value)
{
    const LockList *p = &lock_list[seq_activePattern];
    const uint32_t key = step_key(voice, step), first = lock_lower(p, key), n = step_locks(p, first, key);
    const uint8_t d = lock_dest(param);
    for (uint32_t i = 0; i < n; i++)
        if (p->l[first + i].dest == d) {
            *value = p->l[first + i].b & 0x7Fu;
            return 1;
        }
    return 0;
}

int tack_lock_room(uint8_t voice, uint8_t step, uint16_t param)
{
    const LockList *p = &lock_list[seq_activePattern];
    const uint32_t key = step_key(voice, step), first = lock_lower(p, key), n = step_locks(p, first, key);
    const uint8_t d = lock_dest(param);
    for (uint32_t i = 0; i < n; i++)
        if (p->l[first + i].dest == d)
            return TACK_LOCK_ROOM;
    return n >= TACK_STEP_LOCKS                                       ? TACK_LOCK_STEP_FULL
           : p->n + recur_count(seq_activePattern) >= TACK_LOCKS_MAX ? TACK_LOCK_PATTERN_FULL
                                                                       : TACK_LOCK_ROOM;
}

int tack_locked(uint8_t voice, uint8_t step)
{
    const LockList *p = &lock_list[seq_activePattern];
    const uint32_t key = step_key(voice, step), first = lock_lower(p, key);
    return first < p->n && lock_key(&p->l[first]) == key;
}

int tack_lock_count(void) { return lock_list[seq_activePattern].n; }
int tack_step_locks(uint8_t voice, uint8_t step)
{
    const LockList *p = &lock_list[seq_activePattern];
    const uint32_t key = step_key(voice, step);
    return (int)step_locks(p, lock_lower(p, key), key);
}

/* Recurrence goes where the sequencer's first lock slot was, which is copied and cleared with the step: param1Nr the length,
 * or NO_AUTOMATION for none, and param1Val the mask. Each lane counts its own passes of each pattern, so polymetric lanes
 * keep their own cycle and a chain counts the plays of that pattern. 840 divides by every length, so the wrap of the count
 * never breaks a cycle */
#define LANE_PASSES 840u
static uint16_t lane_pass[NUM_PATTERN][NUM_TRACKS];

static void lanes_reset(void)                    /* the first step counts as a wrap: pass 0 */
{
    for (uint32_t p = 0; p < NUM_PATTERN; p++)
        for (uint32_t t = 0; t < NUM_TRACKS; t++)
            lane_pass[p][t] = LANE_PASSES - 1u;
}

static void lane_wrap(uint8_t track)
{
    uint16_t *n = &lane_pass[seq_activePattern][track];
    *n = (uint16_t)((*n + 1u) % LANE_PASSES);
}

static int recur_plays(uint8_t track, uint8_t step, int8_t position)
{
    const Step *st = &seq_patternSet.seq_subStepPattern[seq_activePattern][track][step];
    uint32_t pass;
    if (st->param1Nr == NO_AUTOMATION)
        return 1;
    pass = lane_pass[seq_activePattern][track];
    if (position > 0 && seq_laneAtEdge(track, 1))   /* nudged early, it belongs to the pass about to start */
        pass = (pass + 1u) % LANE_PASSES;
    else if (position < 0 && seq_laneAtEdge(track, -1))   /* nudged late, the one just ended */
        pass = (pass + LANE_PASSES - 1u) % LANE_PASSES;
    return (st->param1Val >> (pass % st->param1Nr)) & 1u;
}

int tack_recur_len(uint8_t voice, uint8_t step)
{
    const uint8_t n = seq_patternSet.seq_subStepPattern[seq_activePattern][voice][step].param1Nr;
    return n == NO_AUTOMATION ? 1 : n;
}

uint8_t tack_recur_mask(uint8_t voice, uint8_t step)
{
    const Step *st = &seq_patternSet.seq_subStepPattern[seq_activePattern][voice][step];
    return st->param1Nr == NO_AUTOMATION ? 1u : st->param1Val;
}

int tack_recur_slot(uint8_t voice, uint8_t step)
{
    const int len = tack_recur_len(voice, step);
    return len > 1 && seq_isRunning() ? (int)(lane_pass[seq_activePattern][voice] % (uint32_t)len) : -1;
}

int tack_recur_skips(uint8_t voice, uint8_t step)
{
    const int slot = tack_recur_slot(voice, step);
    return slot >= 0 && !((tack_recur_mask(voice, step) >> slot) & 1u);
}

/* how many of the steps of a pattern have recurrence */
static uint32_t recur_count(uint8_t pattern)
{
    uint32_t n = 0;
    for (uint32_t t = 0; t < NUM_TRACKS; t++)
        for (uint32_t s = 0; s < NUM_STEPS; s++)
            n += seq_patternSet.seq_subStepPattern[pattern][t][s].param1Nr != NO_AUTOMATION;
    return n;
}

int tack_recur_room(uint8_t voice, uint8_t step)
{
    return tack_recur_len(voice, step) > 1 ||
           lock_list[seq_activePattern].n + recur_count(seq_activePattern) < TACK_LOCKS_MAX;
}

int tack_step_on(uint8_t voice, uint8_t step)
{
    return (seq_patternSet.seq_mainSteps[step / 16u][seq_activePattern][voice] >> (step % 16u)) & 1u;
}

const uint8_t *tack_kit(void) { return shadow; }
uint32_t tack_kit_size(void) { return sizeof shadow; }

/* A pattern as the store holds it, little-endian: the magic, u16 the count of locks, then for each track and step volume,
 * chance and note, the steps' timing, the main steps, the lengths, shuffle, kit, the lanes' modes and starts, BITS, morph and its kit, the pattern's own kit, the locks, and in what is left the steps
 * with recurrence, 3 bytes each: the key's low byte, the mask, the length - 1 with the key's top bit. The lists are as long as
 * they need to be. Anything else is refused whole: patterns of older builds, and whatever a bad write or a bad file left */
#define IMAGE_MAGIC 0x3754504Bu          /* "KPT7" */
#define IMAGE_STEPS (NUM_TRACKS * NUM_STEPS)
#define IMAGE_SETTINGS 4u                /* BITS, how far morph goes, its kit, and whether a kit follows */
#define IMAGE_HEAD (6u + 3u * IMAGE_STEPS + IMAGE_STEPS + NUM_BARS * NUM_TRACKS * 2u + NUM_TRACKS + 2u + 2u * NUM_TRACKS + IMAGE_SETTINGS + \
                    sizeof(PatternKit))
#define IMAGE_MAX (IMAGE_HEAD + TACK_LOCKS_MAX * sizeof(Lock))
_Static_assert(IMAGE_MAX <= 3840u, "a pattern is one store object (PLAT_STORE_MAX)");
_Static_assert(NUM_TRACKS == TACK_VOICES && NUM_STEPS == 64, "the image's size and its keys");

uint32_t tack_pattern_export(uint8_t pattern, void *image)
{
    uint8_t *o = image;
    const LockList *p = &lock_list[pattern];
    o[0] = (uint8_t)IMAGE_MAGIC;
    o[1] = (uint8_t)(IMAGE_MAGIC >> 8);
    o[2] = (uint8_t)(IMAGE_MAGIC >> 16);
    o[3] = (uint8_t)(IMAGE_MAGIC >> 24);
    o[4] = (uint8_t)p->n;
    o[5] = (uint8_t)(p->n >> 8);
    o += 6;
    for (uint32_t t = 0; t < NUM_TRACKS; t++)
        for (uint32_t s = 0; s < NUM_STEPS; s++) {
            const Step *st = &seq_patternSet.seq_subStepPattern[pattern][t][s];
            *o++ = st->volume;
            *o++ = st->prob;
            *o++ = st->note;
        }
    memcpy(o, seq_patternSet.seq_stepTiming[pattern], IMAGE_STEPS);
    o += IMAGE_STEPS;
    for (uint32_t b = 0; b < NUM_BARS; b++) {
        memcpy(o, seq_patternSet.seq_mainSteps[b][pattern], NUM_TRACKS * 2u);
        o += NUM_TRACKS * 2u;
    }
    memcpy(o, seq_patternSet.length[pattern], NUM_TRACKS);
    o += NUM_TRACKS;
    *o++ = seq_patternSet.shuffle[pattern];
    *o++ = seq_patternSet.kitNr[pattern];
    memcpy(o, seq_laneMode[pattern], NUM_TRACKS);
    o += NUM_TRACKS;
    memcpy(o, seq_laneStart[pattern], NUM_TRACKS);
    o += NUM_TRACKS;
    *o++ = bits_of[pattern];
    *o++ = morph_of[pattern];
    *o++ = morph_kit_of[pattern];
    if (pattern == kit_pattern && (kit_has >> pattern & 1u || dirty >> pattern & 1u))   /* its kit as it is now */
        kit_keep(pattern);
    *o++ = kit_has >> pattern & 1u;
    memcpy(o, &kit_of[pattern], sizeof(PatternKit));
    o += sizeof(PatternKit);
    memcpy(o, p->l, p->n * sizeof(Lock));
    o += p->n * sizeof(Lock);
    {
        uint32_t c = 0;
        for (uint32_t t = 0; t < NUM_TRACKS; t++)
            for (uint32_t s = 0; s < NUM_STEPS; s++) {
                const Step *st = &seq_patternSet.seq_subStepPattern[pattern][t][s];
                const uint32_t key = t << 6 | s;
                if (st->param1Nr == NO_AUTOMATION)
                    continue;
                *o++ = (uint8_t)key;
                *o++ = st->param1Val;
                *o++ = (uint8_t)((st->param1Nr - 1u) | (key >> 8) << 7);
                c++;
            }
        return IMAGE_HEAD + (p->n + c) * sizeof(Lock);
    }
}

static int image_ok(const uint8_t *in, uint32_t len)
{
    const uint8_t *q;
    uint32_t n, c, last = 0, run = 0;
    if (len < IMAGE_HEAD || (uint32_t)in[0] << 0 != (IMAGE_MAGIC & 0xFFu) || in[1] != (uint8_t)(IMAGE_MAGIC >> 8) ||
        in[2] != (uint8_t)(IMAGE_MAGIC >> 16) || in[3] != (uint8_t)(IMAGE_MAGIC >> 24))
        return 0;
    n = (uint32_t)in[4] | (uint32_t)in[5] << 8;
    if (n > TACK_LOCKS_MAX || len < IMAGE_HEAD + n * sizeof(Lock) || (len - IMAGE_HEAD) % sizeof(Lock))
        return 0;
    c = (len - IMAGE_HEAD) / sizeof(Lock) - n;
    if (n + c > TACK_LOCKS_MAX)
        return 0;
    q = in + 6;
    for (uint32_t i = 0; i < IMAGE_STEPS; i++, q += 3)
        if (q[1] > 127u || q[2] > 127u)
            return 0;
    for (uint32_t i = 0; i < IMAGE_STEPS; i++, q++) {
        StepTiming t;
        memcpy(&t, q, 1);
        if (t.shift > 2u * SHIFT_ZERO || t.flam > 5u)
            return 0;
    }
    q += NUM_BARS * NUM_TRACKS * 2u;
    for (uint32_t i = 0; i < NUM_TRACKS; i++, q++)
        if (*q > NUM_STEPS)
            return 0;
    if (q[0] > 127u || q[1] > 128u)
        return 0;
    q += 2;
    for (uint32_t i = 0; i < NUM_TRACKS; i++, q++)
        if (LANE_DIR(*q) >= LANE_DIRECTIONS || *q >= LANE_MODE(0u, LANE_SPEEDS))
            return 0;
    for (uint32_t i = 0; i < NUM_TRACKS; i++, q++)
        if (*q >= NUM_STEPS)
            return 0;
    if (q[0] > 127u || q[1] > 127u || q[2] > TACK_MORPH_KITS || q[3] > 1u)
        return 0;
    q += IMAGE_SETTINGS + sizeof(PatternKit);
    for (uint32_t i = 0; i < n; i++, q += sizeof(Lock)) {
        Lock l;
        memcpy(&l, q, sizeof l);
        const uint32_t key = lock_key(&l), at = key << 8 | l.dest;
        if (key >> 6 >= NUM_TRACKS || l.dest >= LXR_HOST_NUM_PARAMS + 1u || (i && at <= last))
            return 0;
        run = i && key == last >> 8 ? run + 1u : 1u;
        if (run > TACK_STEP_LOCKS)
            return 0;
        last = at;
    }
    last = 0;
    for (uint32_t i = 0; i < c; i++, q += 3) {
        const uint32_t key = (uint32_t)(q[2] >> 7) << 8 | q[0], slots = (q[2] & 0x7Fu) + 1u;
        if (key >> 6 >= NUM_TRACKS || (i && key <= last) || slots < 2u || slots > TACK_RECUR_MAX || !q[1] || (q[1] >> slots))
            return 0;
        last = key;
    }
    return 1;
}

int tack_load_pattern(uint8_t pattern, const void *image, uint32_t len)
{
    const uint8_t *in = image;
    LockList *p = &lock_list[pattern];
    uint32_t n;
    if (!image_ok(in, len))
        return -1;
    n = (uint32_t)in[4] | (uint32_t)in[5] << 8;
    in += 6;
    for (uint32_t t = 0; t < NUM_TRACKS; t++)
        for (uint32_t s = 0; s < NUM_STEPS; s++) {
            Step *st = &seq_patternSet.seq_subStepPattern[pattern][t][s];
            st->volume = *in++;
            st->prob = *in++;
            st->note = *in++;
            st->param1Nr = st->param2Nr = NO_AUTOMATION;
            st->param1Val = st->param2Val = 0;
        }
    memcpy(seq_patternSet.seq_stepTiming[pattern], in, IMAGE_STEPS);
    in += IMAGE_STEPS;
    for (uint32_t b = 0; b < NUM_BARS; b++) {
        memcpy(seq_patternSet.seq_mainSteps[b][pattern], in, NUM_TRACKS * 2u);
        in += NUM_TRACKS * 2u;
    }
    memcpy(seq_patternSet.length[pattern], in, NUM_TRACKS);
    in += NUM_TRACKS;
    seq_patternSet.shuffle[pattern] = *in++;
    seq_patternSet.kitNr[pattern] = *in++;
    memcpy(seq_laneMode[pattern], in, NUM_TRACKS);
    in += NUM_TRACKS;
    memcpy(seq_laneStart[pattern], in, NUM_TRACKS);
    in += NUM_TRACKS;
    bits_of[pattern] = in[0];
    morph_of[pattern] = in[1];
    morph_kit_of[pattern] = in[2];
    kit_has = (uint16_t)(in[3] ? kit_has | 1u << pattern : kit_has & ~(1u << pattern));
    in += IMAGE_SETTINGS;
    memcpy(&kit_of[pattern], in, sizeof(PatternKit));
    in += sizeof(PatternKit);
    memcpy(p->l, in, n * sizeof(Lock));
    p->n = (uint16_t)n;
    in += n * sizeof(Lock);
    for (uint32_t i = (len - IMAGE_HEAD) / sizeof(Lock) - n; i; i--, in += 3) {
        const uint32_t key = (uint32_t)(in[2] >> 7) << 8 | in[0];
        Step *st = &seq_patternSet.seq_subStepPattern[pattern][key >> 6][key & 63u];
        st->param1Nr = (uint8_t)((in[2] & 0x7Fu) + 1u);
        st->param1Val = in[1];
    }
    seq_copyPatternToBackupBuffer(seq_activePattern);
    return 0;
}

void tack_clear_pattern(uint8_t pattern)
{
    seq_clearPattern(pattern);
    seq_patternSet.kitNr[pattern] = 0;
    memset(seq_laneMode[pattern], 0, NUM_TRACKS);
    memset(seq_laneStart[pattern], 0, NUM_TRACKS);
    bits_of[pattern] = morph_of[pattern] = morph_kit_of[pattern] = 0;
    kit_has = (uint16_t)(kit_has & ~(1u << pattern));
    memset(&kit_of[pattern], 0, sizeof(PatternKit));
    lock_list[pattern].n = 0;
    seq_copyPatternToBackupBuffer(seq_activePattern);
}

uint32_t tack_dirty(void) { return dirty; }
void tack_clean(uint8_t pattern) { dirty &= ~(1u << pattern); }

/* The song and the chain are two of the LXR's song slots: seq_activeSong is the one playing */
extern uint8_t seq_songModeEnabled, seq_pendingPattern;
#define SONG_ROWS seq_songData[0]
#define CHAIN_ROWS seq_songData[1]
_Static_assert(TACK_SONG_ROWS == MAX_SONG_LENGTH && TACK_SONG_STOP == SONG_END && TACK_SONG_LOOP == SONG_LOOP &&
               NUM_KIT_SLOTS >= 2, "the sequencer's song lists");
static volatile uint8_t song_mode;

int tack_song_mode(void) { return song_mode; }
int tack_pattern_next(void) { return seq_pendingPattern != seq_activePattern ? seq_pendingPattern : -1; }
int tack_song_pos(void) { return seq_songPositionPointer == 0xFFu ? -1 : seq_songPositionPointer; }
uint8_t tack_song_get(uint32_t row) { return row < TACK_SONG_ROWS ? SONG_ROWS[row] : TACK_SONG_STOP; }
const uint8_t *tack_song(void) { return SONG_ROWS; }

static void song_off(void)
{
    seq_songModeEnabled = 0;
    seq_activeSong = SONG_ROWS;
    seq_pendingPattern = seq_activePattern;
    song_mode = TACK_SONG_OFF;
}

static void song_on(void)
{
    if (SONG_ROWS[0] >= NUM_PATTERN)
        return;
    seq_activeSong = SONG_ROWS;
    seq_setSongModeEnabled(1);
    song_mode = TACK_SONG_ON;
}

static void song_row(uint8_t row, uint8_t value)
{
    const int end = value == TACK_SONG_STOP || value == TACK_SONG_LOOP;
    if (row >= TACK_SONG_ROWS || (value >= NUM_PATTERN && !end) || (row == TACK_SONG_ROWS - 1u && !end))
        return;
    SONG_ROWS[row] = value;
    if (!row && end && song_mode == TACK_SONG_ON)
        song_off();
}

/* The first pattern is on already: the sequencer steps on to the second at the end of a bar */
static void chain_add(uint8_t index, uint8_t pattern)
{
    if (!index || index >= TACK_CHAIN_MAX || pattern >= NUM_PATTERN)
        return;
    CHAIN_ROWS[index] = pattern;
    CHAIN_ROWS[index + 1u] = TACK_SONG_LOOP;
    seq_activeSong = CHAIN_ROWS;
    if (song_mode != TACK_SONG_CHAIN)
        seq_songPositionPointer = 0;
    seq_songModeEnabled = 1;
    song_mode = TACK_SONG_CHAIN;
}

uint32_t tack_chain_len(void)
{
    uint32_t n = 0;
    while (n < TACK_CHAIN_MAX && CHAIN_ROWS[n] < NUM_PATTERN)
        n++;
    return n;
}

/* The chain over the song's rows from this one, as the white keys write them. The song keeps its end, and rows past the chain */
static void chain_to_song(uint8_t row)
{
    uint32_t len = 0, n = tack_chain_len();
    uint8_t end;
    if (song_mode != TACK_SONG_CHAIN)
        return;
    while (len < TACK_SONG_ROWS - 1u && SONG_ROWS[len] < NUM_PATTERN)
        len++;
    end = SONG_ROWS[len];
    if (row > len)
        return;
    if (row + n > TACK_SONG_ROWS - 1u)
        n = TACK_SONG_ROWS - 1u - row;
    memcpy(&SONG_ROWS[row], CHAIN_ROWS, n);
    if (row + n > len)
        SONG_ROWS[row + n] = end;
}

/* one pattern, ending a chain or song. Stopped: now; running: at the end of the bar, as on the LXR */
void tack_pattern_now(uint8_t pattern)
{
    if (pattern >= NUM_PATTERN)
        return;
    song_off();
    CHAIN_ROWS[0] = pattern;
    CHAIN_ROWS[1] = TACK_SONG_LOOP;
    seq_setNextPattern(pattern);
}

extern volatile uint8_t voiceControl_fires[];
uint8_t tack_voice_fires(uint8_t voice) { return voice < TACK_VOICES ? voiceControl_fires[voice] : 0; }
int tack_lane_used(uint8_t voice)
{
    for (uint32_t b = 0; voice < TACK_VOICES && b < NUM_BARS; b++)
        if (seq_patternSet.seq_mainSteps[b][seq_activePattern][voice])
            return 1;
    return 0;
}

/* a bit each: the patterns with a step set */
int tack_bits(void) { return 127 - bits_of[seq_activePattern]; }

/* Morph blends the kit towards a second kit from the bank, as the LXR's does. The kit itself stays as it is, in morph_a and the
 * main loop's shadow, so a save writes it and the dials show it. What the voices hear is written a few parameters a block, and only
 * those that changed. As on the LXR a value between the two is a value, so a wave or a filter type steps through those between
 * them. An FX's type and modes, and which voice restarts an LFO, have nothing between them, and change at half way. Modulation
 * targets, routing and anything no page has stay the kit's. A lock's end hands back what was last written, so the blended value */
enum { MORPH_KEEP, MORPH_BLEND, MORPH_SWITCH };
#define MORPH_PER_BLOCK 16u
static uint8_t morph_kind[LXR_HOST_NUM_PARAMS], morph_b[LXR_HOST_NUM_PARAMS], morph_sent[LXR_HOST_NUM_PARAMS];
static uint8_t morph_kit_in, morph_kit_ok, morph_now, morph_dirty;
static uint32_t morph_at, morph_clean;

/* Mutate walks the kit, on the main loop as a step back replays the whole way. What it arrives at goes to the audio side as one event,
 * and from there out as morph's writes do, a few a block */
static KitWalk mutate_walk;
static uint8_t mutate_top[LXR_HOST_NUM_PARAMS], mutate_voice[LXR_HOST_NUM_PARAMS], mutate_next[LXR_HOST_NUM_PARAMS];
static volatile uint8_t mutate_waiting;          /* the audio side has mutate_next still to take */

static void morph_kinds(void)
{
    for (uint32_t v = 0; v < TACK_VOICES; v++)
        for (uint32_t g = TACK_G_MAIN; g <= TACK_G_LFO; g++)
            for (int sub = 0; sub < tack_pages(v, g); sub++)
                for (uint32_t n = 0; n < 4u; n++) {
                    const TackSlot *s = tack_slot(v, g, (uint32_t)sub, n);
                    if (s && s->label && !s->target && (s->par < PAR_VOICE_LFO1 || s->par > PAR_VOICE_LFO6)) {
                        morph_kind[s->par] = s->par >= PAR_RETRIGGER_LFO1 && s->par <= PAR_RETRIGGER_LFO6 ? MORPH_SWITCH : MORPH_BLEND;
                        mutate_top[s->par] = s->max;
                        mutate_voice[s->par] = (uint8_t)(v < 5u ? v : 5u);   /* the hats share their oscillators */
                    }
                }
    for (uint32_t k = 0; k < sizeof FX_KIND / sizeof FX_KIND[0]; k++)
        for (uint32_t n = 0; n < 5u; n++)
            if (FX_KIND[k].s[n].max)
                morph_kind[FX_KIND[k].s[n].par] = FX_KIND[k].s[n].max >= 126u ? MORPH_BLEND : MORPH_SWITCH;
    morph_kind[PAR_FX_TYPE] = MORPH_SWITCH;
    morph_kind[PAR_FX_DRY_WET] = MORPH_BLEND;
    morph_kind[PAR_VOICE_DECIMATION_ALL] = MORPH_BLEND;
}

static uint8_t morph_value(uint32_t p, uint8_t a)
{
    const int32_t m = morph_now, b = morph_b[p];
    if (!m || !morph_kit_ok || morph_kind[p] == MORPH_KEEP)
        return a;
    if (morph_kind[p] == MORPH_SWITCH)
        return m >= 64 ? (uint8_t)b : a;
    {
        const int32_t d = (b - (int32_t)a) * m;
        return (uint8_t)((int32_t)a + (d + (d < 0 ? -63 : 63)) / 127);
    }
}

/* something changed: a whole pass with nothing to write ends it */
static void morph_touch(void)
{
    morph_dirty = 1;
    morph_clean = 0;
}

/* what is in the engine is now the kit: at boot, and after a kit went in */
static void morph_capture(void)
{
    for (uint32_t i = 0; i < LXR_HOST_NUM_PARAMS; i++)
        morph_a[i] = morph_sent[i] = lxr_host_getParam((uint16_t)i);
    morph_touch();
}

static void morph_sync(void)
{
    const uint8_t kit = morph_kit_of[seq_activePattern], amount = morph_of[seq_activePattern];
    if (kit != morph_kit_in) {
        const uint8_t *k = kit && kit_find ? kit_find(kit - 1u) : 0;
        if (k)
            memcpy(morph_b, k, sizeof morph_b);
        morph_kit_ok = k != 0;
        morph_kit_in = kit;
        morph_touch();
    }
    if (amount != morph_now) {
        morph_now = amount;
        morph_touch();
    }
}

static void morph_write(uint32_t budget)
{
    while (morph_dirty && budget) {
        const uint32_t p = morph_at;
        const uint8_t v = morph_value(p, morph_a[p]);
        morph_at = p + 1u < LXR_HOST_NUM_PARAMS ? p + 1u : 0u;
        if (morph_kind[p] != MORPH_KEEP && v != morph_sent[p]) {
            lxr_host_setParam((uint16_t)p, v);
            morph_sent[p] = v;
            morph_clean = 0;
            budget--;
        } else if (++morph_clean >= LXR_HOST_NUM_PARAMS) {
            morph_dirty = 0;
        }
    }
}

static void morph_after_kit(void)
{
    morph_sync();
    morph_write(LXR_HOST_NUM_PARAMS);
}

/* what the voices hear, as a kit: stopped, so that nothing moves as it is read */
void tack_kit_blended(uint8_t *values)
{
    for (uint32_t p = 0; p < LXR_HOST_NUM_PARAMS; p++)
        values[p] = morph_value(p, morph_a[p]);
}

uint8_t tack_lfo_sync(uint32_t voice) { return shadow[PAR_SYNC_LFO1 + (voice < 5u ? voice : 5u)]; }
int tack_lfo_once(uint32_t voice) { return shadow[PAR_RETRIGGER_LFO1 + (voice < 5u ? voice : 5u)] > 6u; }

int tack_mutate(uint32_t voice, int32_t steps, uint32_t seed)
{
    int step;
    if (mutate_waiting)
        return mutate_walk.step;
    memcpy(mutate_next, shadow, sizeof mutate_next);
    step = kit_walk(&mutate_walk, mutate_next, mutate_top, mutate_voice, voice < TACK_VOICES ? (uint8_t)(voice < 5u ? voice : 5u) : KIT_MUTATE_ALL, steps, seed);
    mutate_waiting = 1;
    if (!post(EV_MUTATE, 0, 0, 0)) {
        mutate_waiting = 0;
        return step;
    }
    for (uint32_t p = 0; p < LXR_HOST_NUM_PARAMS; p++)
        if (mutate_top[p])
            shadow[p] = mutate_next[p];
    return step;
}

int tack_morph(void) { return morph_of[seq_activePattern]; }
int tack_morph_kit(void) { return morph_kit_of[seq_activePattern]; }
int tack_post_morph(uint8_t amount) { return post(EV_MORPH, amount, 0, 0); }
void tack_morph_now(uint8_t amount)
{
    morph_of[seq_activePattern] = amount & 0x7Fu;
    dirty |= 1u << seq_activePattern;
}
int tack_post_morph_kit(uint8_t kit) { return post(EV_MORPH_KIT, kit, 0, 0); }
#ifdef PLAT_HOST
uint8_t tack_heard(uint16_t param) { return lxr_host_getParam(param); }
int tack_morph_kept(uint16_t param) { return morph_kind[param] == MORPH_KEEP; }
/* parameters away from the walk's start, and of those, how many are outside its voice */
int tack_mutate_moved(int outside)
{
    int n = 0;
    for (uint32_t i = 0; i < LXR_HOST_NUM_PARAMS; i++)
        n += shadow[i] != mutate_walk.from[i] && (!outside || (mutate_walk.scope != KIT_MUTATE_ALL && mutate_voice[i] != mutate_walk.scope));
    return n;
}
int tack_morph_differs(void)
{
    int n = 0;
    for (uint32_t i = 0; i < LXR_HOST_NUM_PARAMS; i++)
        n += lxr_host_getParam((uint16_t)i) != morph_a[i];
    return n;
}
#endif
uint32_t tack_patterns_used(void)
{
    uint32_t used = 0;
    for (uint32_t p = 0; p < TACK_PATTERNS; p++)
        for (uint32_t v = 0; v < NUM_TRACKS; v++)
            for (uint32_t b = 0; b < NUM_BARS; b++)
                if (seq_patternSet.seq_mainSteps[b][p][v])
                    used |= 1u << p;
    return used;
}

_Static_assert(TACK_LANE_DIRS == LANE_DIRECTIONS && TACK_LANE_SPEEDS == LANE_SPEEDS, "the panel's dials");
uint32_t tack_lane_dir(uint8_t voice) { return LANE_DIR(seq_laneMode[seq_activePattern][voice]); }
uint32_t tack_lane_speed(uint8_t voice) { return LANE_SPEED(seq_laneMode[seq_activePattern][voice]); }
const char *tack_lane_dir_name(uint32_t dir)
{
    static const char *const NAME[LANE_DIRECTIONS] = {"FWD", "REV", "PONG", "BOUNCE", "RND"};
    return NAME[dir < LANE_DIRECTIONS ? dir : 0];
}
const char *tack_lane_speed_name(uint32_t speed)
{
    static const char *const NAME[LANE_SPEEDS] = {"1X", "1/2", "1/4"};
    return NAME[speed < LANE_SPEEDS ? speed : 0];
}
int tack_lane_start(uint8_t voice) { return seq_laneStart[seq_activePattern][voice]; }
int tack_post_lane_start(uint8_t voice, uint8_t start) { return post(EV_LANE_START, voice, start, 0); }
int tack_post_lane(uint8_t voice, uint32_t dir, uint32_t speed)
{
    if (voice >= NUM_TRACKS || dir >= LANE_DIRECTIONS || speed >= LANE_SPEEDS)
        return 0;
    return post(EV_LANE_MODE, voice, LANE_MODE(dir, speed), 0);
}

/* Stopped, about to start: from the top of the list. The first step counts as a bar's end, which moves on a row */
static void song_rewind(void)
{
    if (!seq_songModeEnabled)
        return;
    if (seq_activeSong[0] >= NUM_PATTERN) {
        song_off();
        return;
    }
    seq_songPositionPointer = 0xFFu;
    seq_setNextPattern(seq_activeSong[0]);
}

void tack_song_load(const uint8_t *rows, int on)
{
    for (uint32_t i = 0; i < TACK_SONG_ROWS; i++) {
        const uint8_t v = rows ? rows[i] : TACK_SONG_STOP;
        const int end = v == TACK_SONG_STOP || v == TACK_SONG_LOOP;
        SONG_ROWS[i] = (v < NUM_PATTERN && i < TACK_SONG_ROWS - 1u) || end ? v : TACK_SONG_STOP;
    }
    song_off();
    if (on)
        song_on();
}

void tack_load_kit(const uint8_t *values)
{
    lxr_host_loadKit(values);
    for (uint32_t i = 0; i < LXR_HOST_NUM_PARAMS; i++)
        shadow[i] = lxr_host_getParam((uint16_t)i);
    morph_capture();
}

/* a pattern change restores the pattern it leaves from the backup buffer, discarding what is not in it: on the LXR
 * edits stay until a save or a reload. Here an edit is kept the moment it is made, so it is written there too */
static void commit_active(void)
{
    seq_copyPatternToBackupBuffer(seq_activePattern);
    dirty |= 1u << seq_activePattern;
}

/* What a generator is about to replace, for one lane of one pattern. Two lanes' worth would be two more of these */
static struct { uint8_t vol[NUM_STEPS]; int8_t voice, pattern; } vel_undo = {.voice = -1};

static struct { uint16_t bars[NUM_BARS]; uint8_t vol[NUM_STEPS], length; int8_t voice, pattern; } eu_undo = {.voice = -1};

int tack_euclid_can_undo(uint8_t voice) { return eu_undo.voice == (int8_t)voice && eu_undo.pattern == (int8_t)seq_activePattern; }

static void euclid_keep(uint8_t voice)
{
    const Step *p = seq_patternSet.seq_subStepPattern[seq_activePattern][voice];
    if (tack_euclid_can_undo(voice))
        return;
    for (uint32_t b = 0; b < NUM_BARS; b++)
        eu_undo.bars[b] = seq_patternSet.seq_mainSteps[b][seq_activePattern][voice];
    for (uint32_t s = 0; s < NUM_STEPS; s++)
        eu_undo.vol[s] = p[s].volume;
    eu_undo.length = seq_patternSet.length[seq_activePattern][voice];
    eu_undo.voice = (int8_t)voice;
    eu_undo.pattern = (int8_t)seq_activePattern;
}

static void euclid_undo(uint8_t voice)
{
    Step *p = seq_patternSet.seq_subStepPattern[seq_activePattern][voice];
    if (voice >= NUM_TRACKS || !tack_euclid_can_undo(voice))
        return;
    for (uint32_t b = 0; b < NUM_BARS; b++)
        seq_patternSet.seq_mainSteps[b][seq_activePattern][voice] = eu_undo.bars[b];
    for (uint32_t s = 0; s < NUM_STEPS; s++)     /* only whether it is on: a velocity set since stays */
        p[s].volume = (uint8_t)((p[s].volume & STEP_VOLUME_MASK) | (eu_undo.vol[s] & STEP_ACTIVE_MASK));
    seq_patternSet.length[seq_activePattern][voice] = eu_undo.length;
    eu_undo.voice = -1;
    commit_active();
}

int tack_shape_can_undo(uint8_t voice) { return vel_undo.voice == (int8_t)voice && vel_undo.pattern == (int8_t)seq_activePattern; }

/* aww's rule: centre + contour x room x amount, room what the centre leaves on that side, and never below velocity 1.
 * It reads no step's own velocity, so a turn never builds on the last one */
static void velocity_shape(uint8_t voice, uint32_t shape, uint32_t shift, uint32_t centre, uint32_t amount)
{
    Step *p;
    if (voice >= NUM_TRACKS || shape > TACK_SHAPE_N || centre > 127u || amount > 100u)
        return;
    p = seq_patternSet.seq_subStepPattern[seq_activePattern][voice];
    if (!shape) {
        if (tack_shape_can_undo(voice)) {
            for (uint32_t s = 0; s < NUM_STEPS; s++)
                p[s].volume = (uint8_t)((p[s].volume & STEP_ACTIVE_MASK) | vel_undo.vol[s]);
            vel_undo.voice = -1;
            commit_active();
        }
        return;
    }
    if (!tack_shape_can_undo(voice)) {
        for (uint32_t s = 0; s < NUM_STEPS; s++)
            vel_undo.vol[s] = p[s].volume & STEP_VOLUME_MASK;
        vel_undo.voice = (int8_t)voice;
        vel_undo.pattern = (int8_t)seq_activePattern;
    }
    {
        const TackShape *t = &SHAPES[shape - 1u];
        for (uint32_t s = 0; s < NUM_STEPS; s++) {
            const int32_t c = t->c[(s + t->n - shift % t->n) % t->n];
            const int32_t room = c >= 0 ? 127 - (int32_t)centre : (int32_t)centre;
            const int32_t top = 127 * 100, swing = c * room * (int32_t)amount;
            int32_t v = (int32_t)centre + (swing + (swing >= 0 ? top / 2 : -top / 2)) / top;
            v = v < 1 ? 1 : v > 127 ? 127 : v;
            p[s].volume = (uint8_t)((p[s].volume & STEP_ACTIVE_MASK) | v);
        }
    }
    commit_active();
}

static void set_step(uint8_t voice, uint8_t step, uint8_t on)
{
    if (voice >= NUM_TRACKS || step >= NUM_STEPS)
        return;
    Step *p = &seq_patternSet.seq_subStepPattern[seq_activePattern][voice][step];
    if (on)
        p->volume |= STEP_ACTIVE_MASK;
    else
        p->volume &= (uint8_t)~STEP_ACTIVE_MASK;
    seq_setMainStep(seq_activePattern, voice, step, on);
    commit_active();
}

static void set_lock(uint8_t voice, uint8_t step, uint16_t param, uint8_t value)
{
    LockList *p = &lock_list[seq_activePattern];
    const uint32_t key = step_key(voice, step), first = lock_lower(p, key), n = step_locks(p, first, key);
    const uint8_t d = lock_dest(param);
    const Lock l = {(uint8_t)key, d, (uint8_t)((value & 0x7Fu) | (key >> 8) << 7)};
    uint32_t i = first;
    while (i < first + n && p->l[i].dest < d)
        i++;
    if (i < first + n && p->l[i].dest == d) {
        p->l[i] = l;
    } else if (n < TACK_STEP_LOCKS && p->n + recur_count(seq_activePattern) < TACK_LOCKS_MAX) {
        for (uint32_t k = p->n; k > i; k--)
            p->l[k] = p->l[k - 1u];
        p->l[i] = l;
        p->n++;
    } else {
        return;
    }
    commit_active();
}

/* the locks with keys from key_lo up to key_hi */
static void drop_locks(uint32_t key_lo, uint32_t key_hi)
{
    LockList *p = &lock_list[seq_activePattern];
    const uint32_t a = lock_lower(p, key_lo), b = lock_lower(p, key_hi);
    for (uint32_t k = b; k < p->n; k++)
        p->l[a + k - b] = p->l[k];
    p->n = (uint16_t)(p->n - (b - a));
}

/* A lane grows into the next bar with a copy of its last one, as a beat grows by varying what is there: the steps with their recurrence,
 * then their locks. A bar with steps in it, left from when the lane was longer, stays. What the pattern's room for locks can't take is
 * left out. The locks move as a block, as this can run with the pattern playing */
static void lane_extend(uint8_t v, uint8_t length)
{
    const uint8_t pat = seq_activePattern;
    const uint32_t old = seq_patternSet.length[pat][v], from = (old - 1u) / 16u * 16u, to = from + 16u;
    LockList *p = &lock_list[pat];
    seq_patternSet.length[pat][v] = length;
    if (to < NUM_STEPS && !seq_patternSet.seq_mainSteps[to / 16u][pat][v]) {
        const uint32_t end = old - from < 16u ? old : from + 16u;
        uint32_t used, a, m, at;
        drop_locks(step_key(v, (uint8_t)to), step_key(v, (uint8_t)to) + 16u);
        used = p->n + recur_count(pat);
        for (uint32_t s = from; s < end; s++) {
            Step st = seq_patternSet.seq_subStepPattern[pat][v][s];
            if (st.param1Nr != NO_AUTOMATION && used >= TACK_LOCKS_MAX)
                st.param1Nr = NO_AUTOMATION;
            used += st.param1Nr != NO_AUTOMATION;
            seq_patternSet.seq_subStepPattern[pat][v][s + 16u] = st;
            seq_patternSet.seq_stepTiming[pat][v][s + 16u] = seq_patternSet.seq_stepTiming[pat][v][s];
            seq_setMainStep(pat, v, (uint8_t)(s + 16u), (uint8_t)tack_step_on(v, (uint8_t)s));
        }
        a = lock_lower(p, step_key(v, (uint8_t)from));
        m = lock_lower(p, step_key(v, (uint8_t)end)) - a;
        m = m < TACK_LOCKS_MAX - used ? m : TACK_LOCKS_MAX - used;
        at = lock_lower(p, step_key(v, (uint8_t)to));
        for (uint32_t k = p->n; k > at; k--)
            p->l[k - 1u + m] = p->l[k - 1u];
        for (uint32_t i = 0; i < m; i++) {
            const Lock l = p->l[a + i];
            const uint32_t key = lock_key(&l) + 16u;
            p->l[at + i] = (Lock){(uint8_t)key, l.dest, (uint8_t)((l.b & 0x7Fu) | (key >> 8) << 7)};
        }
        p->n = (uint16_t)(p->n + m);
    }
    commit_active();
}

static void clear_locks(uint8_t voice, uint8_t step)
{
    drop_locks(step_key(voice, step), step_key(voice, step) + 1u);
    commit_active();
}

/* Played by hand or from MIDI. A voice sounds one note, so a new one ends the last on MIDI out, and an off for a note since
 * replaced is not the voice's */
extern void voiceControl_noteOff(uint8_t voice);
static uint8_t last_note[TACK_VOICES];

static void play_note(uint8_t voice, uint8_t note, uint8_t vel)
{
    if (voice >= TACK_VOICES)
        return;
    voiceControl_noteOff(voice);
    last_note[voice] = note;
    lxr_host_noteOn(voice, note, vel);
    seq_addNote(voice, vel, note);              /* it only records with REC on and the sequencer running */
}

void tack_note_off_now(uint8_t voice, uint8_t note)
{
    if (voice < TACK_VOICES && last_note[voice] == note)
        voiceControl_noteOff(voice);
}

static void drain(void)
{
    uint32_t r = ev_r;
    while (r != ev_w) {
        const Ev e = ev[r];
        switch (e.kind) {
        case EV_NOTE:
            play_note(e.a, e.b, e.c);
            break;
        case EV_NOTE_OFF:
            tack_note_off_now(e.a, e.b);
            break;
        case EV_PARAM: {
            const uint16_t p = (uint16_t)(e.a | e.b << 8);
            const uint8_t v = p < LXR_HOST_NUM_PARAMS ? morph_value(p, e.c) : e.c;
            lxr_host_setParam(p, v);
            if (p < LXR_HOST_NUM_PARAMS) {
                morph_a[p] = e.c;
                morph_sent[p] = v;
                dirty |= 1u << seq_activePattern;        /* the pattern's kit */
            }
            break;
        }
        case EV_MUTATE:
            for (uint32_t p = 0; p < LXR_HOST_NUM_PARAMS; p++)
                if (mutate_top[p])
                    morph_a[p] = mutate_next[p];
            mutate_waiting = 0;
            dirty |= 1u << seq_activePattern;
            morph_touch();
            break;
        case EV_STEP:
            set_step(e.a, e.b, e.c);
            break;
        case EV_RUN:
            if (e.a && !seq_isRunning()) {
                lanes_reset();
                song_rewind();
            }
            seq_setRunning(e.a);
            if (!e.a) {
                voiceControl_noteOff(0xFFu);    /* a step's off comes a moment after it: not after a stop */
                commit_active();                /* what REC took while it ran */
            }
            break;
        case EV_BPM:
            seq_setBpm((uint16_t)(e.a | e.b << 8));
            break;
        case EV_SHUFFLE:
            seq_patternSet.shuffle[seq_activePattern] = e.a;   /* a pattern change reads it from here */
            seq_setShuffle((float)e.a / 127.0f);
            commit_active();
            break;
        case EV_PATTERN:
            tack_pattern_now(e.a);
            break;
        case EV_CHAIN:
            chain_add(e.a, e.b);
            break;
        case EV_SONG_ROW:
            song_row(e.a, e.b);
            break;
        case EV_LANE_START:
            if (e.a < NUM_TRACKS && e.b < NUM_STEPS) {
                seq_laneStart[seq_activePattern][e.a] = e.b;
                commit_active();
            }
            break;
        case EV_KIT_HOME:
            kit_now = e.a + 1;
            seq_patternSet.kitNr[seq_activePattern] = (uint8_t)(e.a + 1u);
            dirty |= 1u << seq_activePattern;
            break;
        case EV_MORPH:
            tack_morph_now(e.a);
            break;
        case EV_MORPH_KIT:
            morph_kit_of[seq_activePattern] = e.a;
            dirty |= 1u << seq_activePattern;
            break;
        case EV_BITS:
            bits_of[seq_activePattern] = (uint8_t)(127u - e.a);
            commit_active();
            break;
        case EV_CHAIN_SONG:
            chain_to_song(e.a);
            break;
        case EV_LANE_MODE:
            if (e.a < NUM_TRACKS) {
                seq_laneMode[seq_activePattern][e.a] = e.b;
                commit_active();
            }
            break;
        case EV_SONG_ON:
            if (e.a)
                song_on();
            else
                song_off();
            break;
        case EV_MUTE:
            seq_setMute(e.a, e.b);
            mute_mask = (uint8_t)(e.b ? mute_mask | 1u << e.a : mute_mask & ~(1u << e.a));
            break;
        case EV_RECORD:
            seq_setRecordingMode(e.a);
            break;
        case EV_EXT:
            clock_held = 0;
            wheel_on = wheel_over = 0;
            seq_setExtSync(e.a ? 2u : 0u);      /* the LXR's 2 is MIDI clock; its 1 is the trigger input, which seq_sync ignores */
            if (!e.a)
                seq_setBpm(ui_bpm);             /* back from the clock's tempo */
            break;
        case EV_STRESS:
            stress = e.a;
            break;
        case EV_LENGTH:
            if (e.a < NUM_TRACKS && e.b >= 1u && e.b <= NUM_STEPS) {
                seq_patternSet.length[seq_activePattern][e.a] = e.b;
                commit_active();
            }
            break;
        case EV_EXTEND:
            if (e.a < NUM_TRACKS && e.b >= 1u && e.b <= NUM_STEPS)
                lane_extend(e.a, e.b);
            break;
        case EV_LOCK:
            if (e.a < NUM_TRACKS && e.b < NUM_STEPS)
                set_lock(e.a, e.b, e.c, e.d);
            break;
        case EV_LOCK_CLEAR:
            if (e.a < NUM_TRACKS && e.b < NUM_STEPS)
                clear_locks(e.a, e.b);
            break;
        case EV_ROLL:
            seq_setRoll(e.a, e.b);
            break;
        case EV_ROLL_RATE:
            seq_setRollRate(e.a);
            break;
        case EV_EUCLID:
            if (e.a < NUM_TRACKS) {
                euclid_keep(e.a);
                euklid_setLength(e.a, e.b);
                euklid_setSteps(e.a, e.c, seq_activePattern);
                euklid_setRotation(e.a, e.d, seq_activePattern);
                commit_active();
            }
            break;
        case EV_EU_UNDO:
            euclid_undo(e.a);
            break;
        case EV_SHAPE:
            velocity_shape(e.a, e.b & 15u, (uint32_t)e.b >> 4, e.c, e.d);
            break;
        case EV_COPY:
            if (e.a < NUM_PATTERN && e.a != seq_activePattern)
{
                kit_keep(seq_activePattern);
                kit_of[e.a] = kit_of[seq_activePattern];
                kit_has = (uint16_t)(kit_has | 1u << e.a);
                seq_copyPattern(seq_activePattern, e.a);
                memcpy(seq_laneMode[e.a], seq_laneMode[seq_activePattern], NUM_TRACKS);
                memcpy(seq_laneStart[e.a], seq_laneStart[seq_activePattern], NUM_TRACKS);
                bits_of[e.a] = bits_of[seq_activePattern];
                morph_of[e.a] = morph_of[seq_activePattern];
                morph_kit_of[e.a] = morph_kit_of[seq_activePattern];
                lock_list[e.a].n = lock_list[seq_activePattern].n;
                memcpy(lock_list[e.a].l, lock_list[seq_activePattern].l, lock_list[e.a].n * sizeof(Lock));
                dirty |= 1u << e.a;
            }
            break;
        case EV_CLEAR:
            if (e.a < NUM_TRACKS) {
                seq_clearTrack(e.a, seq_activePattern);
                drop_locks((uint32_t)e.a << 6, (uint32_t)(e.a + 1u) << 6);
                commit_active();
            }
            break;
        case EV_STEP_EDIT:
            if (e.a < NUM_TRACKS && e.b < NUM_STEPS) {
                Step *p = &seq_patternSet.seq_subStepPattern[seq_activePattern][e.a][e.b];
                StepTiming *t = &seq_patternSet.seq_stepTiming[seq_activePattern][e.a][e.b];
                if (e.c == TACK_ST_NUDGE)
                    t->shift = e.d;
                else if (e.c == TACK_ST_FLAM)
                    t->flam = e.d;
                else if (e.c == TACK_ST_PROB)
                    p->prob = e.d;
                else if (e.c == TACK_ST_NOTE)
                    p->note = e.d;
                else
                    p->volume = (uint8_t)((p->volume & STEP_ACTIVE_MASK) | e.d);
                commit_active();
            }
            break;
        case EV_RECUR:
            if (e.a < NUM_TRACKS && e.b < NUM_STEPS && e.c <= TACK_RECUR_MAX) {
                Step *p = &seq_patternSet.seq_subStepPattern[seq_activePattern][e.a][e.b];
                p->param1Nr = e.c > 1u ? e.c : NO_AUTOMATION;
                p->param1Val = e.c > 1u ? e.d : 0u;
                commit_active();
            }
            break;
        case EV_STEP_NOTE:
            if (e.a < NUM_TRACKS && e.b < NUM_STEPS) {
                Step *p = &seq_patternSet.seq_subStepPattern[seq_activePattern][e.a][e.b];
                if (!(p->volume & STEP_ACTIVE_MASK))
                    p->prob = 100;              /* as seq_addNote. One already on keeps its own chance */
                p->note = e.c;
                p->volume = (uint8_t)(STEP_ACTIVE_MASK | e.d);
                seq_setMainStep(seq_activePattern, e.a, e.b, 1);
                commit_active();
            }
            break;
        case EV_LANE_NUDGE:
            if (e.a < NUM_TRACKS) {
                const int32_t d = (int32_t)e.b - 2 * SHIFT_ZERO;
                for (uint32_t s = 0; s < NUM_STEPS; s++) {
                    StepTiming *t = &seq_patternSet.seq_stepTiming[seq_activePattern][e.a][s];
                    const int32_t v = (int32_t)t->shift + d;
                    t->shift = (uint8_t)(v < 0 ? 0 : v > 2 * SHIFT_ZERO ? 2 * SHIFT_ZERO : v);
                }
                commit_active();
            }
            break;
        case EV_KIT:
            if (kit_pending) {
                lxr_host_loadKit(kit_pending);
                morph_capture();
                kit_applied++;
                if (e.a != TACK_KIT_NO_SLOT) {         /* a bank slot is its name, then what was posted */
                    memcpy(kit_name_now, kit_pending - sizeof kit_name_now, sizeof kit_name_now);
                    kit_now = e.a + 1;
                    seq_patternSet.kitNr[seq_activePattern] = (uint8_t)(e.a + 1u);
                    kit_keep(seq_activePattern);
                    commit_active();
                }
                kit_pending = 0;
                morph_after_kit();
            }
            break;
        }
        r = (r + 1u) & (EV_N - 1u);
    }
    ev_r = r;
}

void tack_note_now(uint8_t voice, uint8_t note, uint8_t vel)
{
    play_note(voice, note, vel);
}

/* The tempo of an external clock: the time across the last CLOCK_LONG pulses, 4 beats. MIDI in is read once a render, so a
 * pulse is up to 5.8 ms out, and a span of 2 s at 120 BPM brings that to 0.3%. The last CLOCK_SHORT pulses disagreeing by
 * CLOCK_JUMP is a new tempo, not noise, and the span starts again from them. A gap means the clock stopped. Steps follow
 * the flywheel below and the display, synced LFOs, shuffle and flams this tempo: it moves only when it is CLOCK_STILL away,
 * so the LFOs are not recalculated on each pulse */
#define CLOCK_LONG 96u
#define CLOCK_SHORT 24u
#define CLOCK_FIRST 12u                         /* the pulses before there is a tempo to believe */
#define CLOCK_GAP (SEQ_TICKS_PER_SECOND / 4u)   /* slower than 10 BPM */
#define CLOCK_JUMP 0.04f
#define CLOCK_STILL 0.6f
#define CLOCK_PAUSE 3.0f                        /* pulses' time without one */
static uint32_t clock_at[CLOCK_LONG + 1u];

static float clock_span(uint32_t pulses)        /* BPM over the last pulses, 0 if they took no time */
{
    const uint32_t size = CLOCK_LONG + 1u;
    const uint32_t ticks = clock_at[(clock_w + size - 1u) % size] - clock_at[(clock_w + size - 1u - pulses) % size];
    return ticks ? (float)pulses * (float)SEQ_TICKS_PER_SECOND * 2.5f / (float)ticks : 0.0f;
}

/* The flywheel. A pulse is read up to 5.8 ms late, so steps that followed the pulses moved by that much, and a groove with
 * them. Once there is a tempo the sequencer takes its pulses from here, counted in the audio's own time as with the internal
 * clock, on the 1.45 ms grid of the engine's blocks. A pulse from outside only corrects it, by a part of how far out it was,
 * and that part is larger when it is far out, as after a change of tempo. It goes on a little past the last pulse, in case
 * the next is late, and then waits. All in ticks from now, as a float cannot hold systick_ticks for long */
#define WHEEL_EASE 0.0625f
#define WHEEL_QUICK 0.25f
#define WHEEL_AHEAD 2                           /* pulses past the last from outside */
static float wheel_due;                         /* ticks to the next pulse */
static uint32_t wheel_in, wheel_out, wheel_now;

/* a pulse from outside with the wheel off */
static void wheel_past(void)
{
    if (wheel_over)
        wheel_over--;
    else
        sync_pulse();
}

/* it stops where the clock is: ahead by a pulse or two, or owing some */
static void wheel_stop(void)
{
    if (wheel_on) {
        int32_t out = (int32_t)(wheel_out - wheel_in);
        wheel_over = (uint8_t)(out > 0 ? out : 0);
        for (; out < 0; out++)
            sync_pulse();
    }
    wheel_on = 0;
}

static void wheel_time(void)
{
    wheel_due -= (float)(systick_ticks - wheel_now);
    wheel_now = systick_ticks;
}

static void wheel_take(void)                    /* a pulse from outside, with a tempo */
{
    float out;
    if (!seq_isRunning()) {
        wheel_stop();
        wheel_past();
        return;
    }
    if (!wheel_on) {                            /* this one as it comes, and the wheel from here */
        wheel_past();
        wheel_on = 1;
        wheel_in = wheel_out = 0;
        wheel_now = systick_ticks;
        wheel_due = wheel_each;
        return;
    }
    wheel_time();
    out = -(wheel_due + (float)(int32_t)(wheel_in - wheel_out) * wheel_each);   /* this pulse was due so long ago */
    wheel_due += out * (out > wheel_each * 0.5f || out < wheel_each * -0.5f ? WHEEL_QUICK : WHEEL_EASE);
    wheel_in++;
}

static void wheel_turn(void)                    /* each block, before the sequencer */
{
    if (!wheel_on)
        return;
    wheel_time();
    while (wheel_due <= 0.0f && (int32_t)(wheel_out - wheel_in) < WHEEL_AHEAD) {
        sync_pulse();
        wheel_out++;
        wheel_due += wheel_each;
    }
    if (wheel_due < -wheel_each)                /* waiting for the clock */
        wheel_due = -wheel_each;
}

static int clock_pulse(void)
{
    const uint32_t now = systick_ticks;
    const uint32_t since = now - clock_at[(clock_w + CLOCK_LONG) % (CLOCK_LONG + 1u)];
    float bpm, d;
    /* a gap, or a pause in a known tempo, which would otherwise count as a slower one */
    if (clock_held && (since > CLOCK_GAP || (clock_held > CLOCK_FIRST && (float)since > wheel_each * CLOCK_PAUSE))) {
        clock_held = 0;
        wheel_stop();
    }
    clock_at[clock_w] = now;
    clock_w = (uint8_t)((clock_w + 1u) % (CLOCK_LONG + 1u));
    if (clock_held <= CLOCK_LONG)
        clock_held++;
    if (clock_held <= CLOCK_FIRST) {
        wheel_past();
        return 1;
    }
    bpm = clock_span(clock_held - 1u);
    if (clock_held > CLOCK_SHORT + 1u) {
        const float recent = clock_span(CLOCK_SHORT);
        d = recent - bpm;
        if (d < 0.0f)
            d = -d;
        if (d > bpm * CLOCK_JUMP) {
            bpm = recent;
            clock_held = CLOCK_SHORT + 1u;
        }
    }
    d = bpm - (float)seq_getBpm();
    if (bpm > 0.0f && (d >= CLOCK_STILL || d <= -CLOCK_STILL))
        seq_setBpm((uint16_t)(bpm + 0.5f));
    if (bpm > 0.0f) {
        wheel_each = (float)SEQ_TICKS_PER_SECOND * 2.5f / bpm;
        wheel_take();
    } else {
        wheel_stop();
        wheel_past();
    }
    return 1;
}

void tack_midi_realtime(uint8_t status)
{
    if (!seq_getExtSync())
        return;
    switch (status) {                           /* as the LXR's MIDI parser */
    case MIDI_CLOCK:
        seq_sync();
        break;
    case MIDI_START:
        wheel_on = wheel_over = 0;
        lanes_reset();
        song_rewind();
        sync_midiResetPosition();
        sync_midiStartStop(1);
        break;
    case MIDI_CONTINUE:
        sync_midiStartStop(1);
        break;
    case MIDI_STOP:
        sync_midiStartStop(0);
        wheel_stop();
        voiceControl_noteOff(0xFFu);
        break;
    default:
        break;
    }
}

/* The worst a step can ask: every voice with all its locks, each put back and set, to the values they already have so that
 * the sound stays. Destination 1 and up are the voices' parameters, and the dearest are among them */
static void stress_locks(void)
{
    static AutomationNode node[TACK_VOICES][TACK_STEP_LOCKS];
    for (uint32_t v = 0; v < TACK_VOICES; v++)
        for (uint32_t i = 0; i < TACK_STEP_LOCKS; i++) {
            const uint8_t d = (uint8_t)(1u + v * TACK_STEP_LOCKS + i);
            node[v][i].destination = d;
            autoNode_setDestination(&node[v][i], d);
            autoNode_updateValue(&node[v][i], midiParser_originalCcValues[d]);
        }
}

/* the load test: every voice together, as often as 16ths at 300 BPM, on whatever kit is loaded */
static void stress_tick(void)
{
    static uint32_t n;
    if (!stress) {
        n = 0;
        return;
    }
    if (n++ % 35u == 0u) {
        stress_locks();
        for (uint8_t v = 0; v < TACK_VOICES; v++)
            lxr_host_noteOn(v, 60, 127);
    }
}

/* USB audio's 10 channels (fm1-base usb.c, FELUCCA_UAC10), frame by frame: the master, each voice as it leaves the voice, and
 * the FX return. Five words a frame */
#define UL_WORDS 5u
extern volatile uint8_t mixer_tapOn;
extern int16_t mixer_tapVoice[6][64], mixer_tapFx[128];
volatile uint8_t app_ul_want;
INSEQRAM uint32_t app_ul_out[256u * UL_WORDS];
_Static_assert(TACK_BLOCK == 64, "the mixer's taps");

static uint32_t pair(int32_t a, int32_t b) { return (uint32_t)(a & 0xFFFF) | (uint32_t)b << 16; }

/* BITS: the mix on steps of 2 to the 0 to 14, in a line between powers of two so that the dial has no jumps. It rounds to the
 * nearest step and a step is centred on 0, so silence stays silent and the wave stays centred */
static uint8_t bits_now;
static float bits_step = 1.0f, bits_inv = 1.0f;

static void bits_set(uint32_t amount)
{
    const uint32_t span = amount * 14u;
    bits_step = (float)(1u << (span / 127u)) * (1.0f + (float)(span % 127u) / 127.0f);
    bits_inv = 1.0f / bits_step;
    bits_now = (uint8_t)amount;
}

static int32_t bits_round(int32_t s)
{
    const float q = (float)s * bits_inv;
    const int32_t r = (int32_t)((float)(int32_t)(q + (q < 0.0f ? -0.5f : 0.5f)) * bits_step);
    return r > 32767 ? 32767 : r < -32768 ? -32768 : r;
}

void tack_render(int32_t *lr, uint32_t n, uint32_t gain_q12)
{
    int16_t dac1[TACK_BLOCK * 2], dac2[TACK_BLOCK * 2], mix[TACK_BLOCK * 2];
    uint32_t *ul = app_ul_out;
    while (n >= TACK_BLOCK) {
        int i;
        drain();
        stress_tick();
        /* as the LXR's audio interrupt: the sequencer, then the block it just triggered */
        seq_platform_advance(TACK_BLOCK, TACK_RATE);
        wheel_turn();
        seq_tick();
        mixer_tapOn = app_ul_want;
        lxr_host_renderBlock(dac1, dac2);
        morph_sync();
        morph_write(MORPH_PER_BLOCK);
        if (bits_now != bits_of[seq_activePattern])
            bits_set(bits_of[seq_activePattern]);
        for (i = 0; i < TACK_BLOCK * 2; i++) {
            /* every voice is routed to one pair (adjustValue in lxr_core.c); the other is silent, so the sum needs no
             * headroom. BITS comes after the FX and before MASTER, whose level then changes nothing. 16 bits to 24 */
            int32_t s = (int32_t)dac1[i] + dac2[i];
            s = s > 32767 ? 32767 : s < -32768 ? -32768 : s;
            if (bits_now)
                s = bits_round(s);
            mix[i] = (int16_t)s;
            lr[i] = (int32_t)(((int32_t)s * (int32_t)gain_q12) >> 4);
        }
        if (app_ul_want) {
            for (i = 0; i < TACK_BLOCK; i++, ul += UL_WORDS) {
                ul[0] = pair(mix[2 * i], mix[2 * i + 1]);
                ul[1] = pair(mixer_tapVoice[0][i], mixer_tapVoice[1][i]);
                ul[2] = pair(mixer_tapVoice[2][i], mixer_tapVoice[3][i]);
                ul[3] = pair(mixer_tapVoice[4][i], mixer_tapVoice[5][i]);
                ul[4] = pair(mixer_tapFx[2 * i], mixer_tapFx[2 * i + 1]);
            }
        }
        lr += TACK_BLOCK * 2;
        n -= TACK_BLOCK;
    }
}
