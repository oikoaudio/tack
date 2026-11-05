/* SPDX-License-Identifier: GPL-3.0-only */
/* The engine and sequencer for the FM-1 app: no hardware and no plat.h here, so a desktop test can link it. */
#pragma once
#include <stdint.h>

#define TACK_VOICES 7          /* 3 drums, snare, cymbal, closed hat, open hat */
#define TACK_NOTE_CENTRE 60    /* the note a voice plays at its own pitch */
#define TACK_BLOCK 64          /* the engine's block, its control rate */
#define TACK_RATE 44100u       /* the FM-1's output; REAL_FS is set to match (fm1.toml) */
#define TACK_PATTERNS 16u
#define TACK_BARS 4u
#define TACK_LOCKS_MAX 480u            /* in a pattern, with its recurrences: what its record has room for beside its morph target */
#define TACK_STEP_LOCKS 8u             /* on a step: each costs two parameter updates when the step fires */

void tack_init(void);
/* n stereo frames, a multiple of TACK_BLOCK; lr are 24-bit samples */
void tack_render(int32_t *lr, uint32_t n, uint32_t gain_q12);

/* From the main loop: applied in order before the next engine block, so a parameter set before a note is heard on it.
 * Each returns 0 when the queue is full. */
int tack_post_note(uint8_t voice, uint8_t note, uint8_t vel);
int tack_post_note_off(uint8_t voice, uint8_t note);     /* MIDI out only: the voices are one-shots */
int tack_post_param(uint16_t param, uint8_t value);
int tack_post_step(uint8_t voice, uint8_t step, uint8_t on);
int tack_post_run(uint8_t on);
int tack_post_bpm(uint16_t bpm);
int tack_post_shuffle(uint8_t amount);
int tack_post_pattern(uint8_t pattern);                  /* one pattern: a chain or song playing ends */
/* A song is a list of patterns, a row a play of one, ended by TACK_SONG_STOP or TACK_SONG_LOOP. A chain is a list of
 * the same kind that SEQ held builds and SAVE leaves out, so that it cannot overwrite the song. Row 63 is always an end */
#define TACK_SONG_ROWS 64u
#define TACK_SONG_STOP 64u
#define TACK_SONG_LOOP 65u
#define TACK_CHAIN_MAX 16u
enum { TACK_SONG_OFF, TACK_SONG_CHAIN, TACK_SONG_ON };
int tack_post_chain(uint8_t index, uint8_t pattern);     /* after a tack_post_pattern, index 0 of the chain */
int tack_post_song_row(uint8_t row, uint8_t value);
int tack_post_song_on(uint8_t on);
#define TACK_SONG_REPS 8u                                 /* a row plays 1, 2, 3, 4, 6, 8, 12 or 16 times */
int tack_post_song_reps(uint8_t row, uint8_t reps);      /* reps an index, below TACK_SONG_REPS */
int tack_post_song_insert(uint8_t row);                  /* a copy of the row goes in after it */
int tack_post_song_delete(uint8_t row);
uint32_t tack_chain_len(void);
int tack_post_chain_to_song(uint8_t row);                /* the chain playing, written over the song's rows from this one */
int tack_post_mute(uint8_t voice, uint8_t muted);
int tack_post_record(uint8_t on);
int tack_post_ext_sync(uint8_t on);
int tack_post_stress(uint8_t on);
int tack_post_extend(uint8_t voice, uint8_t length);     /* longer, with its last bar copied into the new one */
int tack_post_length(uint8_t voice, uint8_t length);     /* the lane's length, 1-64, in the active pattern */
/* A parameter lock: the step sets param to value for itself, through the step's two automation slots, and the kit's
 * value returns at the lane's next step without one. Returns 0 if the queue is full */
int tack_post_lock(uint8_t voice, uint8_t step, uint16_t param, uint8_t value);
int tack_post_lock_clear(uint8_t voice, uint8_t step);
/* A whole kit from the bank: values stays untouched until tack_kit_busy() is 0. The active pattern takes it as its own */
#define TACK_KIT_NO_SLOT 0xFFu /* a kit from outside the bank: no pattern takes it */
#define TACK_KIT_INIT 0xFEu    /* the init kit: in no slot, and the pattern takes it */
#define TACK_KIT_EDIT 0xFDu    /* a kit made from the one playing (Frankenkit): the pattern takes it and its name, and its slot stays */
int tack_post_kit(const uint8_t *values, const uint8_t *name, uint8_t slot);
/* the init kit into the pattern, as a bank kit goes in: 0 when a kit is still on its way */
int tack_post_init_kit(void);
int tack_kit_busy(void);
void tack_view_sync(void);             /* once a frame: tack_get_param follows a kit that went in */
/* A pattern keeps its own kit with its edits, loaded as the pattern starts, so the bank is where kits come from. One with no steps
 * and no edits has none yet and plays on what is playing */
const uint8_t *tack_kit_name(void);                      /* the 8 characters of what is playing */
void tack_set_kit_name(const uint8_t *name);
const uint8_t *tack_pattern_kit(uint32_t pattern);       /* its kit's values, 0 none */
const uint8_t *tack_pattern_kit_name(uint32_t pattern);
int tack_pattern_kit_slot(uint32_t pattern);             /* the bank's slot + 1 it came from, 0 none */
int tack_post_kit_home(uint8_t slot);                    /* the kit playing is now that slot's: saved there from the panel */
/* where a pattern's kit is read from when the sequencer switches to it: the slot's parameters, in flash, 0 if empty. It
 * runs in the audio interrupt */
typedef const uint8_t *(*TackKitFind)(uint32_t slot);
void tack_set_kit_find(TackKitFind fn);
void tack_set_kit_name_find(TackKitFind fn);             /* the slot's 8-character name, for a morph target taken from it */
/* the bank's kit now playing: slot + 1, 0 none */
int tack_kit_now(void);
void tack_set_kit_now(int kit);

/* From the render's own context: MIDI in */
void tack_note_now(uint8_t voice, uint8_t note, uint8_t vel);
void tack_note_off_now(uint8_t voice, uint8_t note);     /* from the audio interrupt, as tack_note_now */
void tack_midi_realtime(uint8_t status);

/* The main loop's view. Bytes the interrupt writes, read whole: at worst a frame old */
uint8_t tack_get_param(uint16_t param);
int tack_step_on(uint8_t voice, uint8_t step);
int tack_playhead(uint8_t voice);
int tack_running(void);
int tack_pattern(void);
int tack_pattern_next(void);                             /* the pattern queued for the bar's end, -1 none */
void tack_pattern_now(uint8_t pattern);                  /* from the audio interrupt, as tack_post_pattern */
uint8_t tack_voice_fires(uint8_t voice);                 /* a count of its triggers, from any source */
int tack_lane_used(uint8_t voice);                       /* it has steps in the active pattern */
int tack_song_mode(void);
int tack_song_pos(void);                                 /* the row playing, -1 before the first */
uint8_t tack_song_get(uint32_t row);
uint8_t tack_song_reps(uint32_t row);
uint32_t tack_song_times(uint8_t reps);                  /* how many times an index plays */
int tack_song_pass(void);                                /* which time the row playing is on, from 0 */
void tack_song_save(uint8_t *rows);                      /* TACK_SONG_ROWS bytes, each row with its count */
void tack_song_load(const uint8_t *rows, int on);        /* stopped; rows 0: an empty song */
int tack_muted(uint8_t voice);
int tack_soloed(uint8_t voice);
int tack_audible(uint8_t voice);                         /* not muted, and soloed if any lane is */
int tack_post_solo(uint8_t voice);                       /* soloed, or not any more; not saved */
uint32_t tack_voice_peak(uint32_t voice);                /* the loudest sample since the last call, 0 to 32768; the hats share one */
uint16_t tack_bpm(void);
uint8_t tack_shuffle(void);
int tack_recording(void);
int tack_ext_sync(void);
int tack_length(uint8_t voice);
int tack_lock_get(uint8_t voice, uint8_t step, uint16_t param, uint8_t *value);   /* 1 = locked, *value its value */
enum { TACK_LOCK_ROOM, TACK_LOCK_STEP_FULL, TACK_LOCK_PATTERN_FULL };
int tack_lock_room(uint8_t voice, uint8_t step, uint16_t param);
int tack_lock_count(void);                               /* in the active pattern */
int tack_step_locks(uint8_t voice, uint8_t step);        /* on one step */
int tack_step_lock(uint8_t voice, uint8_t step, uint32_t i, uint16_t *param, uint8_t *value);   /* its i-th, by parameter: 0 past the last */
/* Recurrence: a step plays on some of the slots of a cycle of its lane's passes through the pattern. Length 1 is every pass,
 * and otherwise bit n of the mask is slot n + 1, at least one set */
#define TACK_RECUR_MAX 8u
int tack_recur_len(uint8_t voice, uint8_t step);
uint8_t tack_recur_mask(uint8_t voice, uint8_t step);
int tack_recur_room(uint8_t voice, uint8_t step);        /* the pattern can hold it: it shares the locks' 480 */
int tack_post_recur(uint8_t voice, uint8_t step, uint8_t len, uint8_t mask);
int tack_recur_slot(uint8_t voice, uint8_t step);        /* the slot this pass is on, -1 if it has no cycle or nothing runs */
int tack_recur_skips(uint8_t voice, uint8_t step);       /* it will not play on this pass */
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
    uint8_t centre;              /* a signed value: what reads as 0, as on the LXR's menu. 0: a plain number */
    uint8_t role;                /* what it is to the code, as a label can change */
} TackSlot;
enum { TACK_ROLE_NONE, TACK_ROLE_PAN, TACK_ROLE_ONCE, TACK_ROLE_RETRIG, TACK_ROLE_AMOUNT, TACK_ROLE_FREQ, TACK_ROLE_WAVE,
       TACK_ROLE_PITCH, TACK_ROLE_DECAY };
enum { TACK_PAGE_OTHER, TACK_PAGE_FILTER, TACK_PAGE_OUTPUT, TACK_PAGE_FM };
int tack_page_kind(uint32_t voice, uint32_t group, uint32_t sub);
enum { TACK_WAVE_NONE, TACK_WAVE_OSC, TACK_WAVE_LFO };
int tack_slot_wave(const TackSlot *s);       /* the wave it picks, which the screen can draw */
int tack_slot_switch(const TackSlot *s);     /* it reads OFF and ON. A toggle like FM and MIX says which */
int tack_pages(uint32_t voice, uint32_t group);
const char *tack_page_title(uint32_t voice, uint32_t group, uint32_t sub);
const TackSlot *tack_slot(uint32_t voice, uint32_t group, uint32_t sub, uint32_t slot);
/* What the screen draws for a page, and the parameters it draws from: slot i of the voice's page of that kind */
enum { TACK_GRAPH_NONE, TACK_GRAPH_AMP, TACK_GRAPH_MOD, TACK_GRAPH_LFO, TACK_GRAPH_OSC, TACK_GRAPH_FM, TACK_GRAPH_MODWAVE };
int tack_page_graph(uint32_t voice, uint32_t group, uint32_t sub);
const TackSlot *tack_graph_slot(uint32_t voice, int graph, uint32_t slot);
uint8_t tack_slot_step(const TackSlot *s, uint8_t value, int32_t e);  /* value after e detents */
const char *tack_slot_text(const TackSlot *s, uint8_t value);         /* its name, 0 for a plain number */
int tack_slot_turn(const TackSlot *s, int32_t e);                     /* a dial that doesn't just set its value: 1 if it took the turn */
int tack_slot_toggles(const TackSlot *s);                             /* it is on or off */
/* why a dial takes no lock on a step, 0 if it does. Sync and retrigger are settings of a whole LFO, and RETRIG's restore would cut a ONCE short */
const char *tack_slot_no_lock(const TackSlot *s);

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
void tack_euclid_length(uint8_t voice, uint32_t length);  /* the dials follow a lane cut to length by hand; no step changes */
int tack_euclid_can_undo(uint8_t voice);
int tack_post_euclid_undo(uint8_t voice);              /* the lane as it was before the first turn, once */
/* The active pattern onto another. Lanes, in the active pattern: back to empty, 16 steps, no locks */
int tack_post_copy(uint8_t dst);
int tack_post_clear(uint8_t voice);
/* Velocity shapes: a shape writes new velocities into the lane's steps, every step, on or off, so what is saved is the
 * velocities. Shape 0 puts back the velocities from before the first shape, on the last lane shaped, once */
#define TACK_SHAPE_N 12
const char *tack_shape_name(uint32_t shape);             /* 1 to TACK_SHAPE_N */
uint32_t tack_shape_steps(uint32_t shape);
int tack_shape_can_undo(uint8_t voice);
int tack_post_shape(uint8_t voice, uint32_t shape, uint32_t shift, uint32_t centre, uint32_t amount);

/* One hit's timing and level. A nudge is -7 to +7 eighths of a step */
enum { TACK_ST_NUDGE, TACK_ST_FLAM, TACK_ST_PROB, TACK_ST_VEL, TACK_ST_NOTE, TACK_ST_SLOTS };
/* The step on, with this note: 60 is the kit's own tuning, and a voice plays others that many semitones from it */
int tack_post_step_note(uint8_t voice, uint8_t step, uint8_t note, uint8_t vel);
int tack_step_get(uint8_t voice, uint8_t step, uint32_t slot);
int tack_step_turn(uint8_t voice, uint8_t step, uint32_t slot, int32_t e);
const char *tack_flam_name(int flam);
/* The whole lane: every step's nudge moves by e, so a hit's own nudge stays relative. Get reads its first hit */
/* How a lane plays: a direction and a speed, saved with the pattern. A pass starts and ends on the same bar in every direction,
 * ping-pong turns at each pass's end, so each end plays twice, bounce turns on the end steps, so it drifts, and random plays every
 * step once a pass in a new order. Half and quarter speed hold each
 * step for 2 or 4 ticks. A pattern's length is its longest lane in steps at full speed: a slow lane runs on across the repeats
 * of its pattern, and a change of pattern cuts it off */
uint32_t tack_lane_dir(uint8_t voice);
uint32_t tack_lane_speed(uint8_t voice);
const char *tack_lane_dir_name(uint32_t dir);
const char *tack_lane_speed_name(uint32_t speed);
int tack_post_lane(uint8_t voice, uint32_t dir, uint32_t speed);
#define TACK_LANE_DIRS 5u
/* the step a pass starts from: the lane plays round from there, and no step moves */
int tack_lane_start(uint8_t voice);
int tack_post_lane_start(uint8_t voice, uint8_t start);
#define TACK_LANE_SPEEDS 3u
/* Morph blends the kit towards the pattern's own target, a second kit saved with it, as is how far it goes, 0 to 127. A kit from
 * the bank is copied in (kit: the bank's slot + 1, 0 none, which drops the target), and a parameter set on its own starts a target
 * from the kit as it is. The kit's own values, on the dials and in a save, do not change */
#define TACK_MORPH_KITS 128u
int tack_morph(void);
int tack_morph_kit(void);                                /* the slot the target was copied from: slot + 1, 0 none */
int tack_post_morph_kit(uint8_t kit);
int tack_has_target(void);
uint8_t tack_target_value(uint16_t param);               /* the target's, or the kit's while there is none */
const uint8_t *tack_target_name(void);                   /* 8 characters */
int tack_post_target(uint16_t param, uint8_t value);
int tack_morph_keeps(uint16_t param);                    /* morph leaves it as the kit has it: no target value of its own */
/* Frankenkit: a kit made of sections of other kits. A cell is one voice's section (OSC, its modulator, TRANSIENT, FILTER, OUTPUT,
 * the envelopes, the LFO), voice * TACK_FRANKEN_SECTIONS + section of the engine's six voices, and the last is the FX. The cymbal's
 * pitch, oscillator and modulators are one section: its three FM oscillators only sound right together.
 * tack_franken_pick: by seed, each cell's parent of n, or only voice's cells (voice < 0: all, then at least two parents).
 * tack_franken_kit: the kit, each cell's parameters from its own kit and the rest (the level, the other voices' pitch) from base */
#define TACK_FRANKEN_SECTIONS 7u
#define TACK_FRANKEN_CELLS (6u * TACK_FRANKEN_SECTIONS + 1u)
void tack_franken_pick(uint32_t seed, uint32_t n, int voice, uint8_t *pick);
void tack_franken_kit(const uint8_t *const *cells, const uint8_t *base, uint8_t *values);
void tack_kit_blended(uint8_t *values);                  /* the kit as morph has it now */
/* Walks the kit, or one voice of it, by steps; negative goes back the same way. Returns the step reached, 0 the walk's start. The main
 * loop only. An edit or another kit starts a new walk, and voice TACK_VOICES or above is the whole kit */
int tack_mutate(uint32_t voice, int32_t steps, uint32_t seed);
uint8_t tack_lfo_sync(uint32_t voice);          /* its clock sync, 0 free */
int tack_lfo_once(uint32_t voice);              /* it plays once on a retrigger */
int tack_post_morph(uint8_t amount);
void tack_morph_now(uint8_t amount);                     /* from the audio interrupt: MIDI in */
uint32_t tack_patterns_used(void);                       /* a bit each: the patterns with a step set */
int tack_lane_nudge_get(uint8_t voice);
int tack_lane_nudge_turn(uint8_t voice, int32_t e);

/* The FX pages. Slots 0 and 1 are the FX type and its mix; 2 and 3 follow the type, and 4 to 6 are its second page. A slot
 * with a maximum of 0 has nothing behind it on this type */
#define FX_TYPE_VERB 5u
enum { TACK_FX_TYPE, TACK_FX_MIX, TACK_FX_A, TACK_FX_B, TACK_FX_C, TACK_FX_D, TACK_FX_E, TACK_FX_ALL };
#define TACK_FX_SLOTS 4        /* knobs on a page */
int tack_fx_pages(void);
void tack_fx_verb_enter(void);                                         /* picked on the panel: the reverb's dials, from a start for drums */
void tack_fx_verb_leave(void);                                         /* and what they held before it */
int tack_fx_idle(uint32_t slot);                                        /* it does nothing as the FX is set now */
uint8_t tack_fx_turn(uint32_t slot, int32_t e);                         /* what a turn makes of a slot */
const char *tack_fx_text(uint32_t slot, uint8_t value);
int tack_fx_delay_ms(void);
#define TACK_FX_SENDS 6        /* the hats share one route */
uint16_t tack_fx_param(uint32_t slot);
uint8_t tack_fx_max(uint32_t slot);
const char *tack_fx_label(uint32_t slot);
const char *tack_fx_type_name(void);
int tack_fx_send(uint32_t voice);
int tack_post_fx_send(uint32_t voice, uint8_t on);
const TackSlot *tack_send_slot(uint32_t voice);
uint16_t tack_rate_param(void);          /* every voice's sample rate before the FX, 127 no change: a kit's parameter */
/* the bit depth of the final mix, after the FX and before MASTER, saved with the pattern: 127 no change, 0 about 2 bits */
int tack_bits(void);
int tack_post_bits(uint8_t value);

/* Saving: main loop, with the sequencer stopped. The image is the active pattern, the LXR's backup buffer */
const uint8_t *tack_kit(void);
uint32_t tack_kit_size(void);
/* Patterns changed since they were saved, a bit each. Any pattern's image, for the store: pat_image_size bytes */
uint32_t tack_dirty(void);
#define TACK_IMG_LENGTHS 1854u         /* where a pattern image holds the lanes' lengths, and its locks */
#define TACK_IMG_LOCKS 2394u
void tack_clean(uint8_t pattern);
void tack_mark_dirty(uint8_t pattern);
uint32_t tack_pattern_export(uint8_t pattern, void *image);   /* its size */
/* Loading: before the audio starts, or stopped */
int tack_load_pattern(uint8_t pattern, const void *image, uint32_t len);   /* 0, or -1 and nothing changed */
void tack_clear_pattern(uint8_t pattern);
void tack_load_kit(const uint8_t *values);
