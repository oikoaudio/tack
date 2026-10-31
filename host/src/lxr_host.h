/*
 * lxr_host.h
 *
 * Runs the LXR sound engine on a desktop computer, without the codec, sequencer or front panel.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once
#include <stdint.h>
#include "Soundengine/config.h"

/** Number of voices that can be triggered: 3 drums, snare, cymbal, closed hi-hat, open hi-hat. */
#define LXR_HOST_NUM_VOICES 7

/** Initialises the engine and loads the factory init kit. */
void lxr_host_init(void);

void lxr_host_noteOn(uint8_t voice, uint8_t note, uint8_t vel);

/**
 * Renders one block of OUTPUT_DMA_SIZE frames through the full mixer and FX bus.
 * dac1 and dac2 are interleaved stereo (OUTPUT_DMA_SIZE*2 samples each), one per output pair on the device.
 */
void lxr_host_renderBlock(int16_t* dac1, int16_t* dac2);

/**
 * The engine always runs at REAL_FS: its filters, envelopes and delay lines are tuned to that rate.
 * lxr_host_process() resamples its output to the rate set here. Allocates, so call it outside the audio thread.
 * Returns 0 on success.
 */
int lxr_host_setOutputRate(double fs);

/**
 * Renders frames at the output rate as 4 interleaved channels: DAC1 L, DAC1 R, DAC2 L, DAC2 R, in -1..1.
 * Note ons take effect at the start of the next engine block (OUTPUT_DMA_SIZE samples at REAL_FS).
 */
void lxr_host_process(float* out, uint32_t frames);

/** Writes interleaved 16-bit PCM. Returns 0 on success. */
int lxr_host_writeWav(const char* path, const int16_t* data, uint32_t frames, uint16_t channels, uint32_t fs);
