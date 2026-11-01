/* SPDX-License-Identifier: GPL-3.0-only */
#include "lfo_speed.h"
#include "Soundengine/DSPAudio/lfo.h"

static const char *const FRACTION[LFO_SPEED_FRACTIONS] = {"OFF", "4/1", "2/1", "1/1", "1/2", "1/3", "1/4", "1/6", "1/8", "1/12", "1/16", "1/32"};
static const float PER_BAR[LFO_SPEED_FRACTIONS] = {0.0f, 0.25f, 0.5f, 1.0f, 2.0f, 3.0f, 4.0f, 6.0f, 8.0f, 12.0f, 16.0f, 32.0f};   /* lfo_calcPhaseInc */

const char *lfo_fraction_name(uint8_t sync) { return FRACTION[sync < LFO_SPEED_FRACTIONS ? sync : 0u]; }

static float freq_hz(uint8_t value)              /* lfo_setFreq */
{
    const float f = 0.0078125f + (float)value / 127.0f * 0.9921875f;
    return f * f * f * LFO_MAX_F;
}

static float sync_hz(uint8_t sync, float bpm) { return bpm / 240.0f * PER_BAR[sync]; }

static float gap(float a, float b) { return a > b ? a / b : b / a; }

uint8_t lfo_sync_near(uint8_t freq, float bpm)
{
    const float hz = freq_hz(freq);
    uint8_t best = 1u;
    for (uint8_t i = 2u; i < LFO_SPEED_FRACTIONS; i++)
        if (gap(sync_hz(i, bpm), hz) < gap(sync_hz(best, bpm), hz))
            best = i;
    return best;
}

uint8_t lfo_freq_near(uint8_t sync, float bpm)
{
    const float hz = sync_hz(sync < LFO_SPEED_FRACTIONS ? sync : 1u, bpm);
    uint8_t best = 0u;
    for (uint8_t v = 1u; v < 128u; v++)
        if (gap(freq_hz(v), hz) < gap(freq_hz(best), hz))
            best = v;
    return best;
}
