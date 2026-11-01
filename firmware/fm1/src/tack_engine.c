/* SPDX-License-Identifier: GPL-3.0-only */
#include "tack_engine.h"
#include "lxr_host.h"
#include "Parameters.h"
#include "Soundengine/Sequencer/sequencer.h"
#include "Soundengine/Sequencer/clockSync.h"
#include "Soundengine/Sequencer/EuklidGenerator.h"
#include "Soundengine/MIDI/MidiMessages.h"
#include "seq_platform.h"
#include "Soundengine/DSPAudio/mixer.h"
#include "Soundengine/FxBus.h"
#include <string.h>

_Static_assert(NUM_PATTERN == TACK_PATTERNS, "fm1.toml's NUM_PATTERN");
_Static_assert(LXR_HOST_NUM_PARAMS <= 248, "app.c keeps the kit's name after its parameters");
_Static_assert(sizeof(PatternBackupBuffer) <= 3840u, "a pattern is one store object (PLAT_STORE_MAX)");

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
uint8_t tack_slot_step(const TackSlot *s, uint8_t value, int32_t e)
{
    int32_t top = s->max, v = value;
    if (!s->en) {
        v += e;
        return (uint8_t)(v < 0 ? 0 : v > top ? top : v);
    }
    const TackEnum *en = &TACK_ENUM[s->en - 1];
    top = en->count - 1;
    v = enum_pos(en, value) + e;
    v = v < 0 ? 0 : v > top ? top : v;
    return en->fw ? en->fw[v] : (uint8_t)v;
}
const char *tack_slot_text(const TackSlot *s, uint8_t value)
{
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
               {"MODE", PAR_FX_DELAY_TYPE, 1}}},
    {"NO VERB", {NO, NO, NO, NO, NO}},
};
#undef NO
#define FX_TYPES 5u                     /* what the FX page can choose: not the reverb row */
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
const char *tack_fx_text(uint32_t slot, uint8_t value)
{
    static const char *const DIST[3] = {"TUBE", "FOLD", "RAT"}, *const DELAY[2] = {"NORMAL", "PING"};
    const uint16_t p = tack_fx_param(slot);
    if (slot < 2u)
        return 0;
    return p == PAR_FX_DIST_TYPE ? DIST[value % 3u] : p == PAR_FX_DELAY_TYPE ? DELAY[value % 2u] : 0;
}
/* calcDelayTime in Delay.c: range sets the span, and time is scaled by range as well, so at a low range the whole dial is
 * a comb. Milliseconds at this sample rate, 0 unless the type is delay */
int tack_fx_delay_ms(void)
{
    if (fx_kind() != FX_DELAY)
        return 0;
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

/* one writer (the main loop), one reader (the audio interrupt): each index is written by one side only */
enum { EV_NOTE, EV_PARAM, EV_STEP, EV_RUN, EV_BPM, EV_SHUFFLE, EV_PATTERN, EV_MUTE, EV_RECORD, EV_EXT, EV_STRESS, EV_LENGTH,
       EV_LOCK, EV_LOCK_CLEAR, EV_KIT, EV_ROLL, EV_ROLL_RATE, EV_EUCLID, EV_COPY, EV_CLEAR, EV_STEP_EDIT, EV_LANE_NUDGE };
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
static uint8_t shadow[LXR_HOST_NUM_PARAMS];
static uint16_t ui_bpm = 120;
static uint8_t ui_shuffle, ui_record, ui_ext;
static uint8_t ui_roll = 5;              /* the sequencer's default, 1/16 */
static uint8_t ui_eu[TACK_VOICES][TACK_EU_SLOTS];
static volatile uint8_t mute_mask;
static volatile uint8_t stress;

void tack_init(void)
{
    lxr_host_init();
    seq_init();
    seq_copyPatternToBackupBuffer(0);   /* as the LXR at boot: a pattern change restores from this buffer */
    seq_setBpm(ui_bpm);
    euklid_init();
    for (uint32_t v = 0; v < TACK_VOICES; v++) {
        ui_eu[v][TACK_EU_LENGTH] = 16;
        ui_eu[v][TACK_EU_HITS] = 4;
    }
    for (uint32_t i = 0; i < LXR_HOST_NUM_PARAMS; i++)
        shadow[i] = lxr_host_getParam((uint16_t)i);
}

int tack_post_note(uint8_t voice, uint8_t note, uint8_t vel) { return post(EV_NOTE, voice, note, vel); }
int tack_post_param(uint16_t param, uint8_t value)
{
    if (param >= LXR_HOST_NUM_PARAMS || !post(EV_PARAM, (uint8_t)param, (uint8_t)(param >> 8), value))
        return 0;
    shadow[param] = value;
    return 1;
}
int tack_post_step(uint8_t voice, uint8_t step, uint8_t on) { return post(EV_STEP, voice, step, on); }
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
_Static_assert(sizeof(StepTiming) == 1u, "the backup buffer copies step timing as bytes");
#define SHIFT_ZERO 7                     /* a stored shift of 7 is on the grid */
#define FLAMS 6
int tack_step_get(uint8_t voice, uint8_t step, uint32_t slot)
{
    const Step *p = &seq_patternSet.seq_subStepPattern[seq_activePattern][voice][step];
    const StepTiming *t = &seq_patternSet.seq_stepTiming[seq_activePattern][voice][step];
    switch (slot) {
    case TACK_ST_NUDGE: return (int)t->shift - SHIFT_ZERO;
    case TACK_ST_FLAM: return t->flam;
    case TACK_ST_PROB: return p->prob;
    default: return p->volume & STEP_VOLUME_MASK;
    }
}
int tack_step_turn(uint8_t voice, uint8_t step, uint32_t slot, int32_t e)
{
    static const int8_t LOW[TACK_ST_SLOTS] = {-SHIFT_ZERO, 0, 0, 0}, TOP[TACK_ST_SLOTS] = {SHIFT_ZERO, FLAMS - 1, 100, 127};
    int32_t v = tack_step_get(voice, step, slot) + e;
    v = v < LOW[slot] ? LOW[slot] : v > TOP[slot] ? TOP[slot] : v;
    return post5(EV_STEP_EDIT, voice, step, (uint8_t)slot, (uint8_t)(slot == TACK_ST_NUDGE ? v + SHIFT_ZERO : v));
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
    e = e < -14 ? -14 : e > 14 ? 14 : e;
    return post(EV_LANE_NUDGE, voice, (uint8_t)(e + 14), 0);
}
int tack_post_copy(uint8_t dst) { return post(EV_COPY, dst, 0, 0); }
int tack_post_clear(uint8_t voice) { return post(EV_CLEAR, voice, 0, 0); }

/* a kit in the audio interrupt: the engine has one writer. Its parameters take a block's time, as on the LXR */
static const uint8_t *volatile kit_pending;
static volatile uint32_t kit_applied;
static uint32_t kit_seen;
int tack_post_kit(const uint8_t *values)
{
    if (kit_pending)
        return 0;
    kit_pending = values;
    if (!post(EV_KIT, 0, 0, 0)) {
        kit_pending = 0;
        return 0;
    }
    return 1;
}
int tack_kit_busy(void)
{
    if (kit_seen != kit_applied) {             /* the kit is in: the main loop's view follows */
        kit_seen = kit_applied;
        for (uint32_t i = 0; i < LXR_HOST_NUM_PARAMS; i++)
            shadow[i] = lxr_host_getParam((uint16_t)i);
    }
    return kit_pending != 0;
}

uint8_t tack_get_param(uint16_t param) { return param < LXR_HOST_NUM_PARAMS ? shadow[param] : 0; }
uint32_t tack_dropped_events(void) { return ev_dropped; }
uint16_t tack_bpm(void) { return ui_bpm; }
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
/* the engine's CC numbers are one above the parameter numbers (sendCC in lxr_core.c), and automation holds CC numbers.
 * Above 127 it holds the parameter, sent as CC2 (autoNode_updateValue) */
static uint8_t lock_dest(uint16_t param) { return (uint8_t)(param < 127u ? param + 1u : param); }

int tack_lock_get(uint8_t voice, uint8_t step, uint16_t param, uint8_t *value)
{
    const Step *p = &seq_patternSet.seq_subStepPattern[seq_activePattern][voice][step];
    const uint8_t d = lock_dest(param);
    if (p->param1Nr == d) {
        *value = p->param1Val;
        return 1;
    }
    if (p->param2Nr == d) {
        *value = p->param2Val;
        return 1;
    }
    return 0;
}
int tack_lock_room(uint8_t voice, uint8_t step, uint16_t param)
{
    const Step *p = &seq_patternSet.seq_subStepPattern[seq_activePattern][voice][step];
    const uint8_t d = lock_dest(param);
    return p->param1Nr == NO_AUTOMATION || p->param2Nr == NO_AUTOMATION || p->param1Nr == d || p->param2Nr == d;
}
int tack_locked(uint8_t voice, uint8_t step)
{
    const Step *p = &seq_patternSet.seq_subStepPattern[seq_activePattern][voice][step];
    return p->param1Nr != NO_AUTOMATION || p->param2Nr != NO_AUTOMATION;
}
int tack_step_on(uint8_t voice, uint8_t step)
{
    return (seq_patternSet.seq_mainSteps[step / 16u][seq_activePattern][voice] >> (step % 16u)) & 1u;
}

const void *tack_pattern_image(void) { return &patternBackupBuffer; }
uint32_t tack_pattern_image_size(void) { return sizeof patternBackupBuffer; }
const uint8_t *tack_kit(void) { return shadow; }
uint32_t tack_kit_size(void) { return sizeof shadow; }

void tack_load_pattern(uint8_t pattern, const void *image)
{
    memcpy(&patternBackupBuffer, image, sizeof patternBackupBuffer);
    seq_copyBackupBufferToPattern(pattern);
    seq_copyPatternToBackupBuffer(0);
}

void tack_load_kit(const uint8_t *values)
{
    lxr_host_loadKit(values);
    for (uint32_t i = 0; i < LXR_HOST_NUM_PARAMS; i++)
        shadow[i] = lxr_host_getParam((uint16_t)i);
}

/* a pattern change restores the pattern it leaves from the backup buffer, discarding what is not in it: on the LXR
 * edits stay until a save or a reload. Here an edit is kept the moment it is made, so it is written there too */
static void commit_active(void) { seq_copyPatternToBackupBuffer(seq_activePattern); }

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
    Step *p = &seq_patternSet.seq_subStepPattern[seq_activePattern][voice][step];
    const uint8_t d = lock_dest(param);
    if (p->param1Nr == d || (p->param1Nr == NO_AUTOMATION && p->param2Nr != d)) {
        p->param1Nr = d;
        p->param1Val = value;
    } else if (p->param2Nr == d || p->param2Nr == NO_AUTOMATION) {
        p->param2Nr = d;
        p->param2Val = value;
    } else {
        return;
    }
    commit_active();
}

static void clear_locks(uint8_t voice, uint8_t step)
{
    Step *p = &seq_patternSet.seq_subStepPattern[seq_activePattern][voice][step];
    p->param1Nr = p->param2Nr = NO_AUTOMATION;
    p->param1Val = p->param2Val = 0;
    commit_active();
}

static void drain(void)
{
    uint32_t r = ev_r;
    while (r != ev_w) {
        const Ev e = ev[r];
        switch (e.kind) {
        case EV_NOTE:
            lxr_host_noteOn(e.a, e.b, e.c);
            seq_addNote(e.a, e.c, e.b);         /* it only records with REC on and the sequencer running */
            break;
        case EV_PARAM:
            lxr_host_setParam((uint16_t)(e.a | e.b << 8), e.c);
            break;
        case EV_STEP:
            set_step(e.a, e.b, e.c);
            break;
        case EV_RUN:
            seq_setRunning(e.a);
            if (!e.a)
                commit_active();                /* what REC took while it ran */
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
            seq_setNextPattern(e.a);            /* stopped: now; running: at the end of the bar, as on the LXR */
            break;
        case EV_MUTE:
            seq_setMute(e.a, e.b);
            mute_mask = (uint8_t)(e.b ? mute_mask | 1u << e.a : mute_mask & ~(1u << e.a));
            break;
        case EV_RECORD:
            seq_setRecordingMode(e.a);
            break;
        case EV_EXT:
            seq_setExtSync(e.a);
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
                euklid_setLength(e.a, e.b);
                euklid_setSteps(e.a, e.c, seq_activePattern);
                euklid_setRotation(e.a, e.d, seq_activePattern);
                commit_active();
            }
            break;
        case EV_COPY:
            if (e.a < NUM_PATTERN && e.a != seq_activePattern)
                seq_copyPattern(seq_activePattern, e.a);
            break;
        case EV_CLEAR:
            if (e.a < NUM_TRACKS) {
                seq_clearTrack(e.a, seq_activePattern);
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
                else
                    p->volume = (uint8_t)((p->volume & STEP_ACTIVE_MASK) | e.d);
                commit_active();
            }
            break;
        case EV_LANE_NUDGE:
            if (e.a < NUM_TRACKS) {
                const int32_t d = (int32_t)e.b - 14;
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
                kit_applied++;
                kit_pending = 0;
            }
            break;
        }
        r = (r + 1u) & (EV_N - 1u);
    }
    ev_r = r;
}

void tack_note_now(uint8_t voice, uint8_t note, uint8_t vel)
{
    lxr_host_noteOn(voice, note, vel);
    seq_addNote(voice, vel, note);
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
        sync_midiResetPosition();
        sync_midiStartStop(1);
        break;
    case MIDI_CONTINUE:
        sync_midiStartStop(1);
        break;
    case MIDI_STOP:
        sync_midiStartStop(0);
        break;
    default:
        break;
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
    if (n++ % 35u == 0u)
        for (uint8_t v = 0; v < TACK_VOICES; v++)
            lxr_host_noteOn(v, 60, 127);
}

void tack_render(int32_t *lr, uint32_t n, uint32_t gain_q12)
{
    int16_t dac1[TACK_BLOCK * 2], dac2[TACK_BLOCK * 2];
    while (n >= TACK_BLOCK) {
        int i;
        drain();
        stress_tick();
        /* as the LXR's audio interrupt: the sequencer, then the block it just triggered */
        seq_platform_advance(TACK_BLOCK, TACK_RATE);
        seq_tick();
        lxr_host_renderBlock(dac1, dac2);
        for (i = 0; i < TACK_BLOCK * 2; i++) {
            /* every voice is routed to one pair (adjustValue in lxr_core.c); the other is silent, so the sum needs no
             * headroom. 16 bits to 24, then MASTER */
            int32_t s = (int32_t)dac1[i] + dac2[i];
            s = s > 32767 ? 32767 : s < -32768 ? -32768 : s;
            lr[i] = (int32_t)(((int32_t)s * (int32_t)gain_q12) >> 4);
        }
        lr += TACK_BLOCK * 2;
        n -= TACK_BLOCK;
    }
}
