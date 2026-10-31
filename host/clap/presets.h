/*
 * presets.h
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once
#include <clap/clap.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TACK_PLUGIN_ID "com.oikoaudio.tack"

/** load_key of the init kit, a preset at the plugin's own location */
#define TACK_INIT_KIT_KEY "init"

/**
 * To the host, a kit file is one preset: the whole kit, for Tack. The voice plugins offer the voices of kits in
 * their own browser, which loads them through the preset-load extension with a voice's key (tack_voiceKey());
 * TACK_KIT_KEY or no key is the whole kit.
 */
#define TACK_KIT_KEY "kit"

/** load_key of a voice (0-5: drum 1-3, snare, cymbal, hi-hat) in a kit file */
const char* tack_voiceKey(int voice);
/** The voice a load_key names, or -1 for the whole kit (TACK_KIT_KEY or none) or an unknown key. */
int tack_voiceOfKey(const char* key);

/** Id of the voice plugin that plays a voice (0-5): Tack Drum for drums 1-3. */
const char* tack_voicePluginId(int voice);

/**
 * A voice's preset name: the kit's, with the drum's number for drums 1-3 ("Lambo Drum 2"), unless it is the only
 * voice of the kit that isn't the init kit's (voices: tack_soundingVoices()), as in a voice saved on its own.
 */
void tack_voicePresetName(const char* kitName, int voice, uint8_t voices, char* name, size_t size);

/** Bit v set for each voice (0-5) of a kit (firmware values) that isn't the init kit's. */
uint8_t tack_soundingVoices(const uint8_t* kit);

/** Where Tack looks for kit files. */
const char* tack_kitFolder(void);

/** The sound pack, a subfolder of the kit folder, that Tack saves kits to. */
#define TACK_USER_PACK "User"

/** tack_saveKit() results */
enum { TACK_SAVE_OK, TACK_SAVE_EXISTS, TACK_SAVE_FAILED };

/**
 * Saves a kit file named after the kit (up to 8 characters from the device's set, see tack_isKitNameChar()) to
 * the user pack, creating the folders it needs. A kit file of that name is only replaced if overwrite is set.
 * path gets the file's path.
 */
int tack_saveKit(const uint8_t* kit, const char* name, bool overwrite, char* path, size_t pathSize);

/** Whether the device allows c in kit names (menu.c, characterOrder). */
bool tack_isKitNameChar(char c);

/** Whether a kit of this name is one of the copies of the init kit the device fills empty slots with. */
bool tack_isEmptySlot(const char* name);

/** A kit file in the kit folder, as the preset browser lists it. */
typedef struct
{
	char name[9];
	char* path;
	int folder;	// index into TackKitList.folders
	uint8_t voices;	// bit v set for each voice (0-5) that isn't the init kit's
} TackKit;

/**
 * The kit files under the kit folder, without empty slots, grouped by the folder they're in ("Factory/PROJ00") and
 * sorted by folder, then file name: the device's slot order.
 */
typedef struct
{
	char** folders;	// relative to the kit folder, sorted
	int numFolders;
	TackKit* kits;
	int numKits;
} TackKitList;

/** Reads the kit folder into list, replacing what it held. */
void tack_scanKits(TackKitList* list);
void tack_freeKits(TackKitList* list);

const clap_preset_discovery_factory_t* tack_presetDiscoveryFactory(void);

#ifdef __cplusplus
}
#endif
