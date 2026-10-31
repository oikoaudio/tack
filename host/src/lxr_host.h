/*
 * lxr_host.h
 *
 * Runs the LXR sound engine on a desktop computer, without the codec, sequencer or front panel.
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

/** Number of sound engines, each with its own output when outputs are separate: 3 drums, snare, cymbal, hi-hat. */
#define LXR_HOST_NUM_VOICE_OUTS 6

/**
 * Channels of lxr_host_process() output, as stereo pairs: main, then each voice. Main has the whole mix, or with
 * separate outputs only the FX bus, the voices routed through it processed together; the voice pairs are silent
 * unless outputs are separate.
 */
#define LXR_HOST_OUT_MAIN		0
#define LXR_HOST_OUT_VOICE(v)	(2 + 2*(v))
#define LXR_HOST_OUT_CHANNELS	14

/** Number of kit parameters, as stored in a kit (parameter numbers 0 .. LXR_HOST_NUM_PARAMS-1). */
#define LXR_HOST_NUM_PARAMS END_OF_SOUND_PARAMETERS

/** Initialises the engine and loads the factory init kit. */
void lxr_host_init(void);

/** Fills values[LXR_HOST_NUM_PARAMS] with the factory init kit. */
void lxr_host_initKit(uint8_t* values);

/**
 * Reads a kit file from the device's SD card (.SND): the 8-character kit name, then one byte per parameter. As on
 * the device, parameters missing from a short file are 0. name gets the trimmed name (at least 9 bytes) unless NULL.
 * Returns 0 on success.
 */
int lxr_host_readKitFile(const char* path, uint8_t* values, char* name);

/**
 * Writes a kit file as the device does (preset_saveDrumsetSD()): name, space-padded to 8 characters, then the
 * LXR_HOST_NUM_PARAMS values. Writes a temporary file and renames it, so readers never see a partial kit.
 * Returns 0 on success.
 */
int lxr_host_writeKitFile(const char* path, const uint8_t* values, const char* name);

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

/**
 * Renders one block with each voice that plays directly in its own interleaved stereo buffer, panned, instead of
 * mixed onto the DACs. Voices routed through the FX bus are processed together, as on the device, and the bus
 * plays on main. All outputs together are the device's mix.
 */
void lxr_host_renderBlockSeparate(int16_t voices[LXR_HOST_NUM_VOICE_OUTS][OUTPUT_DMA_SIZE * 2], int16_t* main);

/**
 * The engine always runs at REAL_FS: its filters, envelopes and delay lines are tuned to that rate.
 * lxr_host_process() resamples its output to the rate set here. Resets the event timeline. Allocates, so call it
 * outside the audio thread. Returns 0 on success.
 */
int lxr_host_setOutputRate(double fs);

/**
 * Schedule events at a frame offset into the next lxr_host_process() call. They take effect lxr_host_latency()
 * frames later, to within half an engine block (OUTPUT_DMA_SIZE samples at REAL_FS), the engine's control rate.
 * Events must be queued in time order.
 */
void lxr_host_queueNoteOn(uint32_t frame, uint8_t voice, uint8_t note, uint8_t vel);
void lxr_host_queueParam(uint32_t frame, uint16_t param, uint8_t value);
void lxr_host_queueSeparateOutputs(uint32_t frame, bool on);

/** Switches between everything on main and one output per voice plus the FX bus on main, now. Off by default. */
void lxr_host_setSeparateOutputs(bool on);

/** Constant delay in output frames between a queued event and its effect on the output. */
uint32_t lxr_host_latency(void);

/** Renders frames at the output rate as LXR_HOST_OUT_CHANNELS interleaved channels, in -1..1. */
void lxr_host_process(float* out, uint32_t frames);

typedef struct
{
	uint32_t lateEvents;	/**< events that arrived after their engine block was rendered; should stay 0 */
	uint32_t droppedEvents;	/**< events lost because the queue was full */
	double maxLead;			/**< furthest the engine has rendered ahead of the output, in engine samples */
} LxrHostStats;

LxrHostStats lxr_host_stats(void);

/** Frees the resampler. */
void lxr_host_shutdown(void);

/** Writes interleaved 16-bit PCM. Returns 0 on success. */
int lxr_host_writeWav(const char* path, const int16_t* data, uint32_t frames, uint16_t channels, uint32_t fs);
