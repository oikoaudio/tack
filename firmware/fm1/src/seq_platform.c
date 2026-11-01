/* SPDX-License-Identifier: GPL-3.0-only */
/* What the LXR sequencer needs from its platform, for a build without the LXR's front panel: its LEDs, menu, SD presets
 * and trigger jacks are notifications nobody here listens to, so they do nothing, and its clock comes from the audio.
 * Hardware-independent: the firmware and the event-trace test (tests/seq_trace.c) link the same file. */
#include <stdint.h>
#include "seq_platform.h"
#include "Soundengine/Sequencer/sequencer.h"
#include "Soundengine/MIDI/MidiMessages.h"
#include "Preset/PresetManager.h"
#include "ledHandler.h"
#include "buttonHandler.h"
#include "menu/menu.h"

/* the clock: on the device a 0.25 ms timer. Counted from audio frames, in integers, so it cannot drift from the sound */
volatile uint32_t systick_ticks;
static uint32_t tick_rest;

void seq_platform_advance(uint32_t frames, uint32_t sample_rate)
{
    tick_rest += frames * SEQ_TICKS_PER_SECOND;
    systick_ticks += tick_rest / sample_rate;
    tick_rest %= sample_rate;
}

/* MIDI the sequencer sends: the app decides where it goes */
static SeqMidiOut midi_out;
void seq_platform_set_midi_out(SeqMidiOut fn) { midi_out = fn; }
void usb_sendMidi(MidiMsg msg) { if (midi_out) midi_out(msg.status, msg.data1, msg.data2); }
void uart_sendMidi(MidiMsg msg) { (void)msg; }             /* the FM-1 has no MIDI out jack */

/* the front panel's state: the sequencer reads and writes these, nobody else does */
uint8_t frontParser_activeTrack, frontParser_shownPattern;
uint8_t menu_activeBar, menu_activePage, menu_playedPattern, menu_shownPattern, menu_songMenuPosition;
uint8_t menu_loadedPresetNr[NUM_CURRENT], menu_selectedPresetNr[NUM_PRESET_LOCATIONS];
uint8_t trigger_clockReceived, midi_process_block;

uint8_t buttonHandler_getMode() { return 0; }
void led_blinkHandler() {}
void led_clearActive_step() {}
void led_clearAllBlinkLeds() {}
void led_clearSequencerLeds() {}
uint8_t led_idx2StepLed(uint8_t idx) { return idx; }
void led_initPatternSelectLeds() {}
void led_resetBlinkTimer() {}
void led_setActive_step(uint8_t stepNr) { (void)stepNr; }
void led_setBlinkLed(const uint8_t ledNr, const uint8_t onOff) { (void)ledNr; (void)onOff; }
void led_setValue(uint8_t val, uint8_t ledNr) { (void)val; (void)ledNr; }
void led_updateSeqLeds() {}
uint8_t menu_getActiveVoice() { return 0; }
uint8_t menu_getViewedPattern() { return 0; }
void menu_repaint() {}
void menu_repaintAll() {}
void menu_setShownPattern(uint8_t patternNr) { (void)patternNr; }
void preset_loadDrumsetRam(uint8_t presetNr, uint8_t isMorph, uint8_t sendImmediate)
{
    (void)presetNr; (void)isMorph; (void)sendImmediate;    /* per-pattern kits: not on the FM-1 yet */
}
void preset_touchTimer(PresetTimer type) { (void)type; }
void trigger_clockTick(uint8_t onOff, uint8_t cnt) { (void)onOff; (void)cnt; }
void trigger_reset(uint8_t value) { (void)value; }
void uart_sendFrontpanelSysExByte(uint8_t data) { (void)data; }
void uart_sendFrontpanelByte(uint8_t b) { (void)b; }
float adc_getFaderVolume(uint8_t idx) { (void)idx; return 1.0f; }
void menu_switchSubPage(uint8_t subPageNr, uint8_t toggle) { (void)subPageNr; (void)toggle; }
uint8_t menu_getSubPage(void) { return 0; }
