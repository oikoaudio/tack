/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include <stdint.h>

#define SEQ_TICKS_PER_SECOND 4000u   /* systick_ticks: the LXR's 0.25 ms timer */

/* call before each seq_tick(), with the frames since the last call */
void seq_platform_advance(uint32_t frames, uint32_t sample_rate);

typedef void (*SeqMidiOut)(uint8_t status, uint8_t data1, uint8_t data2);
void seq_platform_set_midi_out(SeqMidiOut fn);
