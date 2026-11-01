/*
 * lxr_params.h
 *
 * Describes the kit parameters a plugin can expose, built from the front panel's menu tables: which parameters
 * the menu shows, their names, ranges and value texts.
 *
 * A parameter has two value representations:
 * - firmware values, as stored in a kit and sent to the engine (lxr_host_setParam)
 * - plugin values, min..max. They are the same as firmware values except for modulation destinations, where
 *   the firmware stores an index into modTargets and the plugin counts 0 = off, 1.. = the targets on offer, and
 *   voice outputs, where the plugin has 0 = direct, 1 = through the FX bus.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
	uint16_t param;		/**< firmware parameter number; stable, so usable as a plugin parameter id */
	char name[64];		/**< e.g. "Drum 1 Filter Frequency" */
	char module[48];	/**< e.g. "Drum 1/Filter" */
	uint16_t min, max;	/**< plugin value range */
	bool isEnum;		/**< values are named options rather than amounts */

	// for laying out an editor the way the front panel pages are organised
	uint8_t voice;		/**< 0-5 the voices (both hi-hat pages are voice 5), LXR_PARAMS_FX_VOICE the FX bus and master */
	uint8_t group;		/**< the front panel sub-page the parameter is on, 0-7 */
	char label[48];		/**< the name within its group, e.g. "Resonance" */
} LxrParamInfo;

#define LXR_PARAMS_NUM_VOICES	6
#define LXR_PARAMS_FX_VOICE		6
#define LXR_PARAMS_NUM_GROUPS	8
/** FX tab groups: 0 is the FX bus' own settings, 1-4 one per FX type (distortion, ring mod, compressor, delay). */
#define LXR_PARAMS_FX_MASTER_GROUP	7

/** Number of exposed parameters. */
int lxr_params_count(void);
const LxrParamInfo* lxr_params_info(int index);
/** Index of a firmware parameter number in the list, or -1 if it isn't exposed. */
int lxr_params_indexOf(uint16_t param);

uint8_t lxr_params_toFirmware(uint16_t param, uint16_t value);
uint16_t lxr_params_fromFirmware(uint16_t param, uint8_t value);

/** Title of a group, e.g. "Filter", or NULL if the voice has no such group. */
const char* lxr_params_groupName(uint8_t voice, uint8_t group);

/**
 * The FX parameters 1-4 mean something different for each FX type: the label for an FX type (1-4), or NULL if the
 * parameter isn't used by that type. For other parameters, NULL.
 */
const char* lxr_params_fxLabel(uint16_t param, uint8_t fxType);

/** Plugin value of a parameter in the init kit. */
uint16_t lxr_params_default(uint16_t param);

/** Firmware parameter number of the FX type, whose value (0 off, 1-4) selects what the FX parameters mean. */
uint16_t lxr_params_fxTypeParam(void);

/**
 * What kit_walk() (kit_mutate.h) may move: top[param] its highest value, 0 if it stays, and voice[param] its voice. The voices' sound
 * parameters. The FX stay: their parameters mean something else for each FX type, and a delay's feedback can run away.
 */
void lxr_params_mutateTable(uint8_t* top, uint8_t* voice);

/** Whether a parameter is a modulation destination (velocity or LFO). */
bool lxr_params_isTarget(uint16_t param);

/**
 * For a modulation destination: the voice (0-5) whose parameter the plugin value drives, or -1 for off, the FX bus
 * or a global parameter.
 */
int lxr_params_targetVoice(uint16_t param, uint16_t value);

/**
 * Copies one voice of a kit onto a voice of another kit (both in firmware values): each parameter goes to the one
 * with the same place on the target voice (drums 1-3 have the same parameters; other voices only copy onto
 * themselves). Modulation targets on the source voice move to the same parameter on the target voice; targets on
 * other voices become off. An LFO retriggered by its own voice is retriggered by the target voice.
 */
void lxr_params_copyVoice(const uint8_t* fromKit, int fromVoice, uint8_t* toKit, int toVoice);

/** Whether a voice of a kit sounds like the init kit's: every parameter of it the same, its output aside. */
bool lxr_params_voiceIsInit(const uint8_t* kit, int voice);

/** Text for a plugin value, as the front panel would show it. */
void lxr_params_valueText(uint16_t param, uint16_t value, char* buf, size_t size);

#ifdef __cplusplus
}
#endif
