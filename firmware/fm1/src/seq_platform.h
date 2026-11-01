/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include <stdint.h>

#define SEQ_TICKS_PER_SECOND 4000u   /* systick_ticks: the LXR's 0.25 ms timer */

/* call before each seq_tick(), with the frames since the last call */
void seq_platform_advance(uint32_t frames, uint32_t sample_rate);

typedef void (*SeqMidiOut)(uint8_t status, uint8_t data1, uint8_t data2);
void seq_platform_set_midi_out(SeqMidiOut fn);

/* the sequencer asks for a pattern's kit as it switches to it, before that pattern's first step: the LXR's own moment */
typedef void (*SeqKitLoad)(uint8_t kit);
void seq_platform_set_kit_load(SeqKitLoad fn);

/* a voice fires: it applies the step's parameter locks and answers 1. Unset, the sequencer uses the step's own two slots */
typedef int (*SeqLocks)(uint8_t track, uint8_t step);
void seq_platform_set_locks(SeqLocks fn);

/* A step that is on and may fire: 0 keeps it silent. position: -1 the step before the current one, 1 the one after, as a
 * nudge can bring either into this step's time. Wrap: a lane has come round to its first step. Unset, every step plays */
typedef int (*SeqStepPlays)(uint8_t track, uint8_t step, int8_t position);
typedef void (*SeqLaneWrap)(uint8_t track);
void seq_platform_set_recurrence(SeqStepPlays plays, SeqLaneWrap wrap);
