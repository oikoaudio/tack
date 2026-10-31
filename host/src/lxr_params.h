/*
 * lxr_params.h
 *
 * Describes the kit parameters a plugin can expose, built from the front panel's menu tables: which parameters
 * the menu shows, their names, ranges and value texts.
 *
 * A parameter has two value representations:
 * - firmware values, as stored in a kit and sent to the engine (lxr_host_setParam)
 * - plugin values, min..max. They are the same as firmware values except for modulation destinations, where
 *   the firmware stores an index into modTargets and the plugin counts 0 = off, 1.. = the targets on offer.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef struct
{
	uint16_t param;		/**< firmware parameter number; stable, so usable as a plugin parameter id */
	char name[64];		/**< e.g. "Drum 1 Filter Frequency" */
	char module[48];	/**< e.g. "Drum 1/Filter" */
	uint16_t min, max;	/**< plugin value range */
	bool isEnum;		/**< values are named options rather than amounts */
} LxrParamInfo;

/** Number of exposed parameters. */
int lxr_params_count(void);
const LxrParamInfo* lxr_params_info(int index);
/** Index of a firmware parameter number in the list, or -1 if it isn't exposed. */
int lxr_params_indexOf(uint16_t param);

uint8_t lxr_params_toFirmware(uint16_t param, uint16_t value);
uint16_t lxr_params_fromFirmware(uint16_t param, uint8_t value);

/** Text for a plugin value, as the front panel would show it. */
void lxr_params_valueText(uint16_t param, uint16_t value, char* buf, size_t size);
