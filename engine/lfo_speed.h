/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * An LFO's speed, for the FM-1's panel and the plugin. The kit keeps the LXR's two parameters: FREQ's rate, and SYNC's fraction of the
 * bar, which replaces the rate while it isn't 0. Both controls show one idea, so moving between them takes the nearest speed.
 */
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LFO_SPEED_FRACTIONS 12u                  /* SYNC's values, 0 off */

const char *lfo_fraction_name(uint8_t sync);

/** The fraction nearest a FREQ value at a tempo. */
uint8_t lfo_sync_near(uint8_t freq, float bpm);

/** The FREQ value nearest a fraction at a tempo. */
uint8_t lfo_freq_near(uint8_t sync, float bpm);

#ifdef __cplusplus
}
#endif
