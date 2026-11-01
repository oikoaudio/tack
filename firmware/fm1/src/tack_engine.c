/* SPDX-License-Identifier: GPL-3.0-only */
#include "tack_engine.h"
#include "lxr_host.h"
#include "Parameters.h"
#include "Soundengine/Sequencer/sequencer.h"
#include "Soundengine/Sequencer/clockSync.h"
#include "Soundengine/MIDI/MidiMessages.h"
#include "seq_platform.h"
#include <string.h>

_Static_assert(NUM_PATTERN == TACK_PATTERNS, "fm1.toml's NUM_PATTERN");
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

/* one writer (the main loop), one reader (the audio interrupt): each index is written by one side only */
enum { EV_NOTE, EV_PARAM, EV_STEP, EV_RUN, EV_BPM, EV_SHUFFLE, EV_PATTERN, EV_MUTE, EV_RECORD, EV_EXT, EV_STRESS };
#define EV_N 128u
typedef struct { uint8_t kind, a, b, c; } Ev;
static Ev ev[EV_N];
static volatile uint32_t ev_w, ev_r, ev_dropped;

static int post(uint8_t kind, uint8_t a, uint8_t b, uint8_t c)
{
    uint32_t w = ev_w;
    if (((w + 1u) & (EV_N - 1u)) == ev_r) {
        ev_dropped++;
        return 0;
    }
    ev[w] = (Ev){kind, a, b, c};
    ev_w = (w + 1u) & (EV_N - 1u);
    return 1;
}

/* the main loop's view of the kit: a value posted reads back at once, before the audio interrupt has applied it, so
 * the screen is never a step behind and a fast encoder's detents build on each other. The other values likewise */
static uint8_t shadow[LXR_HOST_NUM_PARAMS];
static uint16_t ui_bpm = 120;
static uint8_t ui_shuffle, ui_record, ui_ext;
static volatile uint8_t mute_mask;
static volatile uint8_t stress;

void tack_init(void)
{
    lxr_host_init();
    seq_init();
    seq_copyPatternToBackupBuffer(0);   /* as the LXR at boot: a pattern change restores from this buffer */
    seq_setBpm(ui_bpm);
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
