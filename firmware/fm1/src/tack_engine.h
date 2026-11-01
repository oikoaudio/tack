/* SPDX-License-Identifier: GPL-3.0-only */
/* The engine and sequencer for the FM-1 app: no hardware and no plat.h here, so a desktop test can link it. */
#pragma once
#include <stdint.h>

#define TACK_VOICES 7          /* 3 drums, snare, cymbal, closed hat, open hat */
#define TACK_BLOCK 64          /* the engine's block, its control rate */
#define TACK_RATE 44100u       /* the FM-1's output; REAL_FS is set to match (fm1.toml) */
#define TACK_PATTERNS 16u
#define TACK_BARS 4u

void tack_init(void);
/* n stereo frames, a multiple of TACK_BLOCK; lr are 24-bit samples */
void tack_render(int32_t *lr, uint32_t n, uint32_t gain_q12);

/* From the main loop: applied in order before the next engine block, so a parameter set before a note is heard on it.
 * Each returns 0 when the queue is full. */
int tack_post_note(uint8_t voice, uint8_t note, uint8_t vel);
int tack_post_param(uint16_t param, uint8_t value);
int tack_post_step(uint8_t voice, uint8_t step, uint8_t on);
int tack_post_run(uint8_t on);
int tack_post_bpm(uint16_t bpm);
int tack_post_shuffle(uint8_t amount);
int tack_post_pattern(uint8_t pattern);
int tack_post_mute(uint8_t voice, uint8_t muted);
int tack_post_record(uint8_t on);
int tack_post_ext_sync(uint8_t on);
int tack_post_stress(uint8_t on);

/* From the render's own context: MIDI in */
void tack_note_now(uint8_t voice, uint8_t note, uint8_t vel);
void tack_midi_realtime(uint8_t status);

/* The main loop's view. Bytes the interrupt writes, read whole: at worst a frame old */
uint8_t tack_get_param(uint16_t param);
int tack_step_on(uint8_t voice, uint8_t step);
int tack_playhead(uint8_t voice);
int tack_running(void);
int tack_pattern(void);
int tack_muted(uint8_t voice);
uint16_t tack_bpm(void);
uint8_t tack_shuffle(void);
int tack_recording(void);
int tack_ext_sync(void);
uint32_t tack_dropped_events(void);

/* the sound parameters the panel edits on each voice */
enum { TACK_P_PITCH, TACK_P_FILTER, TACK_P_DECAY, TACK_P_VOLUME, TACK_NP };
uint16_t tack_voice_param(uint32_t voice, uint32_t slot);

/* Saving: main loop, with the sequencer stopped. The image is the active pattern, the LXR's backup buffer */
const void *tack_pattern_image(void);
uint32_t tack_pattern_image_size(void);
const uint8_t *tack_kit(void);
uint32_t tack_kit_size(void);
/* Loading: before the audio starts */
void tack_load_pattern(uint8_t pattern, const void *image);
void tack_load_kit(const uint8_t *values);
