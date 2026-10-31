/*
 * presets.h
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once
#include <clap/clap.h>

#define TACK_PLUGIN_ID "com.oikoaudio.tack"

/** load_key of the init kit, a preset at the plugin's own location */
#define TACK_INIT_KIT_KEY "init"

/** Where Tack looks for kit files. */
const char* tack_kitFolder(void);

const clap_preset_discovery_factory_t* tack_presetDiscoveryFactory(void);
