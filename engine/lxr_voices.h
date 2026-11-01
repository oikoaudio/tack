/* SPDX-License-Identifier: GPL-3.0-only */
/* What the firmware's panel and the plugin both have to agree on about voices, in one place. */
#pragma once
#include <stdint.h>
#include "Parameters.h"

#define LXR_VOICES 7                     /* 3 drums, snare, cymbal, closed hat, open hat */
#define LXR_VOICES_WITH_PARAMS 6         /* the two hats share their oscillators, filter and LFO */
#define LXR_NOTE_CENTRE 60               /* the note a voice plays at its own pitch */

/* the voice whose parameters a voice uses */
static inline uint32_t lxr_param_voice(uint32_t voice) { return voice < LXR_VOICES_WITH_PARAMS ? voice : LXR_VOICES_WITH_PARAMS - 1u; }

/* An LFO's RETRIG holds two things: 1-6 the voice that starts it, 7-12 the same voice with the LFO playing once */
#define LXR_RETRIG_ONCE LXR_VOICES_WITH_PARAMS
static inline int lxr_retrig_once(uint32_t value) { return value > LXR_RETRIG_ONCE; }
static inline uint32_t lxr_retrig_voice(uint32_t value) { return lxr_retrig_once(value) ? value - LXR_RETRIG_ONCE : value; }
static inline uint32_t lxr_retrig_value(uint32_t voice, int once) { return voice && once ? voice + LXR_RETRIG_ONCE : voice; }

/* the lowest value a parameter means anything at: a pulse of no width is no sound, and the engine plays 0 as 1 */
static inline uint32_t lxr_param_min(uint32_t param) { return param >= PAR_PWM_1 && param <= PAR_PWM_6 ? 1u : 0u; }
