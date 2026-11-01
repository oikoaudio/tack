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
int tack_post_length(uint8_t voice, uint8_t length);     /* the lane's length, 1-64, in the active pattern */
/* A parameter lock: the step sets param to value for itself, through the step's two automation slots, and the kit's
 * value returns at the lane's next step without one. Returns 0 if the queue is full */
int tack_post_lock(uint8_t voice, uint8_t step, uint16_t param, uint8_t value);
int tack_post_lock_clear(uint8_t voice, uint8_t step);
/* A whole kit: values stays untouched until tack_kit_busy() is 0 */
int tack_post_kit(const uint8_t *values);
int tack_kit_busy(void);

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
int tack_length(uint8_t voice);
int tack_lock_get(uint8_t voice, uint8_t step, uint16_t param, uint8_t *value);   /* 1 = locked, *value its value */
int tack_lock_room(uint8_t voice, uint8_t step, uint16_t param);    /* 1 = locked already, or a slot is free */
int tack_locked(uint8_t voice, uint8_t step);
uint32_t tack_dropped_events(void);

/* the sound parameters the panel edits on each voice */
enum { TACK_P_PITCH, TACK_P_FILTER, TACK_P_DECAY, TACK_P_VOLUME, TACK_NP };
uint16_t tack_voice_param(uint32_t voice, uint32_t slot);

/* The sound pages: each voice's parameters, four to a page, from the LXR's own menu tables (src/tack_pages.h). Group 0
 * is the main page, 1 EDIT's, 2 ENV's, 3 LFO's. A slot is 0 where a page has nothing in that place */
enum { TACK_G_MAIN, TACK_G_EDIT, TACK_G_ENV, TACK_G_LFO };
typedef struct {
    const char *label;
    uint16_t par;
    uint8_t max, en, target;     /* en: 1 + its enum; target: a modulation destination, which no lock can reach */
} TackSlot;
int tack_pages(uint32_t voice, uint32_t group);
const char *tack_page_title(uint32_t voice, uint32_t group, uint32_t sub);
const TackSlot *tack_slot(uint32_t voice, uint32_t group, uint32_t sub, uint32_t slot);
/* What the screen draws for a page, and the parameters it draws from: slot i of the voice's page of that kind */
enum { TACK_GRAPH_NONE, TACK_GRAPH_AMP, TACK_GRAPH_MOD, TACK_GRAPH_LFO, TACK_GRAPH_OSC, TACK_GRAPH_FM, TACK_GRAPH_MODWAVE };
int tack_page_graph(uint32_t voice, uint32_t group, uint32_t sub);
const TackSlot *tack_graph_slot(uint32_t voice, int graph, uint32_t slot);
uint8_t tack_slot_step(const TackSlot *s, uint8_t value, int32_t e);  /* value after e detents */
const char *tack_slot_text(const TackSlot *s, uint8_t value);         /* its name, 0 for a plain number */

/* Roll: the voice repeats at the roll rate while on, and once if the sequencer is stopped */
#define TACK_ROLL_RATES 6
int tack_post_roll(uint8_t voice, uint8_t on);
int tack_post_roll_rate(uint8_t rate);
uint8_t tack_roll_rate(void);
const char *tack_roll_name(uint8_t rate);
/* The Euclidean generator, on a lane of the active pattern: it replaces the lane's steps and length, not their locks */
enum { TACK_EU_LENGTH, TACK_EU_HITS, TACK_EU_ROTATE, TACK_EU_SLOTS };
#define TACK_EU_MAX 16          /* as the LXR's menu: the generator recurses without end from length 18 with one hit */
uint8_t tack_euclid_get(uint8_t voice, uint32_t slot);
int tack_euclid_turn(uint8_t voice, uint32_t slot, int32_t e);
/* The active pattern onto another. Lanes, in the active pattern: back to empty, 16 steps, no locks */
int tack_post_copy(uint8_t dst);
int tack_post_clear(uint8_t voice);

/* One hit's timing and level. A nudge is -7 to +7 eighths of a step */
enum { TACK_ST_NUDGE, TACK_ST_FLAM, TACK_ST_PROB, TACK_ST_VEL, TACK_ST_SLOTS };
int tack_step_get(uint8_t voice, uint8_t step, uint32_t slot);
int tack_step_turn(uint8_t voice, uint8_t step, uint32_t slot, int32_t e);
const char *tack_flam_name(int flam);
/* The whole lane: every step's nudge moves by e, so a hit's own nudge stays relative. Get reads its first hit */
int tack_lane_nudge_get(uint8_t voice);
int tack_lane_nudge_turn(uint8_t voice, int32_t e);

/* The FX pages. Slots 0 and 1 are the FX type and its mix; 2 and 3 follow the type, and 4 to 6 are its second page. A slot
 * with a maximum of 0 has nothing behind it on this type */
enum { TACK_FX_TYPE, TACK_FX_MIX, TACK_FX_A, TACK_FX_B, TACK_FX_C, TACK_FX_D, TACK_FX_E, TACK_FX_ALL };
#define TACK_FX_SLOTS 4        /* knobs on a page */
int tack_fx_pages(void);
const char *tack_fx_text(uint32_t slot, uint8_t value);
int tack_fx_delay_ms(void);
#define TACK_FX_SENDS 6        /* the hats share one route */
uint16_t tack_fx_param(uint32_t slot);
uint8_t tack_fx_max(uint32_t slot);
const char *tack_fx_label(uint32_t slot);
const char *tack_fx_type_name(void);
int tack_fx_send(uint32_t voice);
int tack_post_fx_send(uint32_t voice, uint8_t on);

/* Saving: main loop, with the sequencer stopped. The image is the active pattern, the LXR's backup buffer */
const void *tack_pattern_image(void);
uint32_t tack_pattern_image_size(void);
const uint8_t *tack_kit(void);
uint32_t tack_kit_size(void);
/* Loading: before the audio starts */
void tack_load_pattern(uint8_t pattern, const void *image);
void tack_load_kit(const uint8_t *values);
