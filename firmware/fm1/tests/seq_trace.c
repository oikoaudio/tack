/* SPDX-License-Identifier: GPL-3.0-only */
/* Runs the LXR sequencer through fixed scenarios and writes every event it sends the engine. Built in the original
 * configuration and the FM-1's (compare_seq.sh), from the same sources: the engine is replaced here, at link level, so
 * the trace is the sequencer's output and nothing else. Random numbers are seeded, so probability repeats. */
#include <stdint.h>
#include "seq_platform.h"
#include "Soundengine/Sequencer/sequencer.h"
#include "Soundengine/Sequencer/clockSync.h"
#include "Soundengine/DSPAudio/automationNode.h"
#include "Soundengine/DSPAudio/lfo.h"
#include "Soundengine/DSPAudio/random.h"
#include "Soundengine/FxBus.h"
#include "Soundengine/MIDI/MidiVoiceControl.h"
#include "Soundengine/MIDI/MidiParser.h"
#include "Parameters.h"

#define BLOCK 64u
void trace_scenario(const char *name);
void trace_event(uint32_t block, uint32_t ticks, char kind, int a, int b, int c);
extern volatile uint32_t systick_ticks;
extern uint8_t seq_stepIndex[];
extern uint8_t seq_songPositionPointer;
extern uint8_t menu_songMenuPosition;

static uint32_t rate, block;

/* ------ the engine, replaced: what the sequencer calls ends up in the trace ------ */
uint8_t activeParameters[NUM_PARAMS];
FxBus fxBus;
uint8_t midi_MidiChannels[8];
uint8_t midiParser_txRxFilter;
static uint32_t rng;
uint32_t GetRngValue(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
void lfo_recalcSync() {}
void midiParser_checkMtc() {}
void voiceControl_noteOn(uint8_t voice, uint8_t note, uint8_t vel) { trace_event(block, systick_ticks, 'N', voice, note, vel); }
void voiceControl_noteOff(uint8_t voice) { (void)voice; }
void autoNode_init(AutomationNode *node) { node->destination = NO_AUTOMATION; }
void autoNode_setDestination(AutomationNode *node, uint16_t dest) { node->destination = dest; }
void autoNode_updateValue(AutomationNode *node, uint8_t val)
{
    if (node->destination != NO_AUTOMATION)
        trace_event(block, systick_ticks, 'P', node->destination, val, 0);
}

/* ------ the clock ------ */
static void run_blocks(uint32_t n)
{
    while (n--) {
        seq_platform_advance(BLOCK, rate);
        seq_tick();
        block++;
    }
}
static void run_ms(uint32_t ms) { run_blocks((uint32_t)((uint64_t)ms * rate / 1000u / BLOCK)); }

/* external clock at bpm: 24 pulses a quarter, delivered on the block they fall in, as MIDI in reaches the render */
static void run_midi_clock(uint32_t pulses, uint32_t bpm)
{
    const uint64_t frames_per_pulse_x1000 = (uint64_t)rate * 60000u / (bpm * 24u);
    uint64_t next = 0, now = 0;
    while (pulses) {
        if (now * 1000u >= next) {
            sync_tick();
            next += frames_per_pulse_x1000;
            pulses--;
        }
        run_blocks(1);
        now += BLOCK;
    }
}

static void fresh(const char *name)
{
    trace_scenario(name);
    seq_setRunning(0);
    seq_activePattern = seq_pendingPattern = 0;
    seq_init();
    seq_copyPatternToBackupBuffer(0);
    seq_setExtSync(0);
    seq_setShuffle(0.f);
    seq_setSongModeEnabled(0);
    seq_setRecordingMode(0);
    seq_setRollRate(0xff);
    seq_setBpm(120);
    for (uint8_t v = 0; v < 7; v++) {
        seq_setRoll(v, 0);
        seq_setMute(v, 0);
    }
    rng = 2463534242u;
}

/* a pattern change restores the pattern it leaves from the backup buffer, so what a scenario wrote is committed first */
static void go(void)
{
    seq_copyPatternToBackupBuffer(0);
    seq_setRunning(1);
}

static void step(uint8_t pat, uint8_t track, uint8_t s, uint8_t vol, uint8_t note)
{
    Step *p = &seq_patternSet.seq_subStepPattern[pat][track][s];
    p->volume = (uint8_t)(vol | STEP_ACTIVE_MASK);
    p->note = note;
    p->prob = 127;
    seq_setMainStep(pat, track, s, 1);
}

int seq_trace_run(uint32_t r)
{
    rate = r;

    fresh("internal tempo");
    for (uint8_t s = 0; s < 16; s++)
        step(0, 0, s, 100, 60);
    go();
    run_ms(3000);
    seq_setBpm(90);
    run_ms(3000);
    seq_setRunning(0);
    run_ms(200);

    fresh("external clock and transport");
    seq_setExtSync(1);
    for (uint8_t s = 0; s < 16; s += 2)
        step(0, 1, s, 100, 62);
    sync_midiStartStop(1);
    run_midi_clock(24 * 8, 125);
    sync_midiStartStop(0);
    run_ms(300);
    sync_midiResetPosition();
    sync_midiStartStop(1);
    run_midi_clock(24 * 4, 140);
    sync_midiStartStop(0);

    fresh("track lengths");
    seq_patternSet.length[0][0] = 16;
    seq_patternSet.length[0][1] = 12;
    seq_patternSet.length[0][2] = 7;
    seq_patternSet.length[0][3] = 5;
    for (uint8_t s = 0; s < 16; s++) {
        step(0, 0, s, 100, 60);
        step(0, 1, s, 90, 61);
        step(0, 2, s, 80, 62);
        step(0, 3, s, 70, 63);
    }
    go();
    run_ms(8000);
    seq_setRunning(0);

    for (int amount = 0; amount < 2; amount++) {
        fresh(amount ? "shuffle full" : "shuffle mid");
        seq_setShuffle(amount ? 1.f : 0.5f);
        for (uint8_t s = 0; s < 16; s++)
            step(0, 0, s, 100, 60);
        go();
        run_ms(4000);
        seq_setRunning(0);
    }

    fresh("shifts");
    for (uint8_t s = 0; s < 16; s += 2) {
        step(0, 0, s, 100, 60);
#ifdef LXR_FM1_BUILD                            /* the LXR's positions, on the FM-1's scale */
        {
            static const uint8_t WHERE[8] = {0, 3, 6, 9, 10, 11, 12, 13};
            const int n = (int)(s & 15u) - 7;
            seq_patternSet.seq_stepTiming[0][0][s].shift = (uint8_t)(STEP_SHIFT_ZERO + (n < 0 ? -WHERE[-n] : WHERE[n]));
        }
#else
        seq_patternSet.seq_stepTiming[0][0][s].shift = (uint8_t)(s & 15u);
#endif
    }
    go();
    run_ms(4000);
    seq_setRunning(0);

    fresh("flams");
    for (uint8_t s = 0; s < 16; s += 4) {
        step(0, 2, s, 100, 60);
        seq_patternSet.seq_stepTiming[0][2][s].flam = (uint8_t)(1u + s / 4u);
    }
    go();
    run_ms(4000);
    seq_setRunning(0);

    fresh("rolls");
    step(0, 0, 0, 100, 60);
    seq_setRollRate(4);                    /* 1/8 */
    go();
    run_ms(500);
    seq_setRoll(3, 1);
    run_ms(1500);
    seq_setRoll(3, 0);
    run_ms(500);
    seq_setRunning(0);

    fresh("probability");
    for (uint8_t s = 0; s < 16; s++) {
        step(0, 4, s, 100, 60);
        seq_patternSet.seq_subStepPattern[0][4][s].prob = 50;
    }
    go();
    run_ms(16000);
    seq_setRunning(0);

    fresh("automation");
    for (uint8_t s = 0; s < 16; s++) {
        Step *p = &seq_patternSet.seq_subStepPattern[0][0][s];
        step(0, 0, s, 100, 60);
        p->param1Nr = PAR_FILTER_FREQ_1 + 1;   /* automation holds CC numbers, one above the parameter's */
        p->param1Val = (uint8_t)(s * 8u);
        p->param2Nr = PAR_VOL1 + 1;
        p->param2Val = (uint8_t)(127u - s);
    }
    go();
    run_ms(4000);
    seq_setRunning(0);

    fresh("live recording");
    step(0, 0, 0, 100, 60);
    step(0, 0, 8, 100, 60);
    seq_setRecordingMode(1);
    go();
    run_ms(330);
    seq_addNote(2, 90, 64);
    seq_recordAutomation(0, PAR_VOL1 + 1, 77);
    run_ms(471);
    seq_addNote(3, 80, 65);
    run_ms(1500);
    seq_setRecordingMode(0);
    run_ms(4000);
    seq_setRunning(0);

    fresh("pattern change");
    step(0, 0, 0, 100, 60);
    step(0, 0, 8, 100, 60);
    step(1, 1, 0, 100, 62);
    step(1, 1, 4, 100, 62);
    go();
    run_ms(1300);
    seq_setNextPattern(1);
    run_ms(4000);
    seq_setNextPattern(0);
    run_ms(4000);
    seq_setRunning(0);

    fresh("song");
    step(0, 0, 0, 100, 60);
    step(1, 1, 0, 100, 62);
    step(2, 2, 0, 100, 64);
    trace_event(block, systick_ticks, 'M', menu_songMenuPosition, 0, 0);    /* where earlier playback left it */
    menu_songMenuPosition = 0;             /* the song page's cursor: adding writes here */
    seq_addPatternToSong(0);
    seq_addPatternToSong(1);
    seq_addPatternToSong(2);
    seq_addPatternToSong(1);
    for (int i = 0; i < 5; i++)
        trace_event(block, systick_ticks, 'L', i, seq_activeSong[i], 0);
    seq_setSongModeEnabled(1);
    go();
    for (int i = 0; i < 28; i++) {         /* where it is, twice a second: 'A' pattern, song position, running */
        run_ms(500);
        trace_event(block, systick_ticks, 'A', seq_activePattern, seq_songPositionPointer, seq_isRunning());
    }
    seq_setRunning(0);

    fresh("zero length track");            /* a stored 0 means 16; with a flam it used to divide by it */
    step(0, 5, 0, 100, 60);
    step(0, 5, 4, 100, 60);
    seq_patternSet.seq_stepTiming[0][5][4].flam = 2;
    seq_patternSet.length[0][5] = 0;
    go();
    run_ms(2500);
    seq_setRunning(0);
    return 0;
}
