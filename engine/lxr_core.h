/*
 * lxr_core.h
 *
 * The engine's control surface, shared by the plugins and the FM-1 firmware: init, kits, parameters, notes and one block of
 * audio. What only a computer needs, resampling and files among it, is in host/src/lxr_host.h.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "Soundengine/config.h"
#include "Parameters.h"

/** Number of voices that can be triggered: 3 drums, snare, cymbal, closed hi-hat, open hi-hat. */
#define LXR_HOST_NUM_VOICES 7

/** The note that plays a voice at its tuned pitch (the firmware's SEQ_DEFAULT_NOTE); others transpose by semitones. */
#define LXR_HOST_BASE_NOTE 60

/** Number of kit parameters, as stored in a kit (parameter numbers 0 .. LXR_HOST_NUM_PARAMS-1). */
#define LXR_HOST_NUM_PARAMS END_OF_SOUND_PARAMETERS

/** Initialises the engine and loads the factory init kit. */
void lxr_host_init(void);

/** Fills values[LXR_HOST_NUM_PARAMS] with the factory init kit. */
void lxr_host_initKit(uint8_t* values);

/** The kit as loading would leave it: values the engine can't play brought into range. */
void lxr_host_kitAsLoaded(const uint8_t* values, uint8_t* out);

/** Sends a whole kit to the engine, as the device does when loading one. */
void lxr_host_loadKit(const uint8_t* values);

/**
 * Sets one kit parameter now, as the front panel does when it is edited. Values are in the firmware's own
 * representation (mod targets are indices into modTargets).
 */
void lxr_host_setParam(uint16_t param, uint8_t value);
uint8_t lxr_host_getParam(uint16_t param);

/**
 * Triggers a voice now. The note transposes it from its tuned pitch by (note - LXR_HOST_BASE_NOTE) semitones: both
 * oscillators of a voice, not the snare's noise or the transient samples.
 */
void lxr_host_noteOn(uint8_t voice, uint8_t note, uint8_t vel);

/**
 * Renders one block of OUTPUT_DMA_SIZE frames through the full mixer and FX bus.
 * dac1 and dac2 are interleaved stereo (OUTPUT_DMA_SIZE*2 samples each), one per output pair on the device.
 */
void lxr_host_renderBlock(int16_t* dac1, int16_t* dac2);
