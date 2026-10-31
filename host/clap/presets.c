/*
 * presets.c
 *
 * Tack's preset discovery: kit files (.SND, as on the device's SD card) under the kit folder, one sound pack per
 * subfolder, plus the init kit built into the plugin.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "presets.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>
#include <sys/stat.h>
#include "lxr_host.h"

static const clap_universal_plugin_id_t pluginId = { "clap", TACK_PLUGIN_ID };

static const clap_preset_discovery_provider_descriptor_t providerDesc =
{
	.clap_version = CLAP_VERSION_INIT,
	.id = TACK_PLUGIN_ID ".kits",
	.name = "Tack kits",
	.vendor = "oikoaudio",
};

//-----------------------------------------------------------
const char* tack_kitFolder(void)
{
	// Linux only for now: $XDG_DATA_HOME/Tack/Kits, by default ~/.local/share/Tack/Kits
	static char path[1024];
	if(path[0]) return path;
	const char* data = getenv("XDG_DATA_HOME");
	const char* home = getenv("HOME");
	if(data && data[0])
		snprintf(path, sizeof path, "%s/Tack/Kits", data);
	else
		snprintf(path, sizeof path, "%s/.local/share/Tack/Kits", home ? home : "");
	return path;
}
//-----------------------------------------------------------
static bool providerInit(const clap_preset_discovery_provider_t* provider)
{
	const clap_preset_discovery_indexer_t* indexer = provider->provider_data;

	// the device writes upper case names, other tools may not
	static const clap_preset_discovery_filetype_t snd = { "LXR kit", "Kit file from the LXR's SD card", "snd" };
	static const clap_preset_discovery_filetype_t sndUpper = { "LXR kit", "Kit file from the LXR's SD card", "SND" };
	indexer->declare_filetype(indexer, &snd);
	indexer->declare_filetype(indexer, &sndUpper);

	const clap_preset_discovery_location_t builtIn = {
		CLAP_PRESET_DISCOVERY_IS_FACTORY_CONTENT, "Tack", CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN, NULL
	};
	indexer->declare_location(indexer, &builtIn);

	const clap_preset_discovery_location_t folder = {
		CLAP_PRESET_DISCOVERY_IS_USER_CONTENT, "Kits", CLAP_PRESET_DISCOVERY_LOCATION_FILE, tack_kitFolder()
	};
	indexer->declare_location(indexer, &folder);

	// each subfolder is a sound pack
	DIR* dir = opendir(tack_kitFolder());
	if(dir) {
		struct dirent* e;
		while((e = readdir(dir))) {
			if(e->d_name[0] == '.') continue;
			char path[2048];
			struct stat st;
			snprintf(path, sizeof path, "%s/%s", tack_kitFolder(), e->d_name);
			if(stat(path, &st) || !S_ISDIR(st.st_mode)) continue;
			const clap_preset_discovery_soundpack_t pack = {
				CLAP_PRESET_DISCOVERY_IS_USER_CONTENT, e->d_name, e->d_name, NULL, NULL, NULL, NULL,
				CLAP_TIMESTAMP_UNKNOWN
			};
			indexer->declare_soundpack(indexer, &pack);
		}
		closedir(dir);
	}
	return true;
}

static void providerDestroy(const clap_preset_discovery_provider_t* provider)
{
	free((void*)provider);
}

static bool providerGetMetadata(const clap_preset_discovery_provider_t* provider, uint32_t kind, const char* location,
	const clap_preset_discovery_metadata_receiver_t* receiver)
{
	if(kind == CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN) {
		if(!receiver->begin_preset(receiver, "Init Kit", TACK_INIT_KIT_KEY)) return true;
		receiver->add_plugin_id(receiver, &pluginId);
		receiver->set_flags(receiver, CLAP_PRESET_DISCOVERY_IS_FACTORY_CONTENT);
		return true;
	}

	uint8_t kit[LXR_HOST_NUM_PARAMS];
	char name[9];
	if(lxr_host_readKitFile(location, kit, name)) {
		receiver->on_error(receiver, 0, "Not a readable kit file");
		return false;
	}
	// the device fills empty kit slots with copies of the init kit; the built-in Init Kit covers those
	if(!strcasecmp(name, "Initkit")) return true;
	if(!name[0]) {
		const char* base = strrchr(location, '/');
		snprintf(name, sizeof name, "%s", base ? base + 1 : location);
	}
	if(!receiver->begin_preset(receiver, name, NULL)) return true;
	receiver->add_plugin_id(receiver, &pluginId);
	receiver->set_flags(receiver, CLAP_PRESET_DISCOVERY_IS_USER_CONTENT);

	// the first folder below the kit folder is the sound pack
	const char* root = tack_kitFolder();
	const size_t rootLen = strlen(root);
	if(!strncmp(location, root, rootLen) && location[rootLen] == '/') {
		char pack[256];
		snprintf(pack, sizeof pack, "%s", location + rootLen + 1);
		char* slash = strchr(pack, '/');
		if(slash) {
			*slash = 0;
			receiver->set_soundpack_id(receiver, pack);
		}
	}
	return true;
}

static const void* providerGetExtension(const clap_preset_discovery_provider_t* provider, const char* id)
{
	return NULL;
}
//-----------------------------------------------------------
static uint32_t factoryCount(const clap_preset_discovery_factory_t* factory)
{
	return 1;
}

static const clap_preset_discovery_provider_descriptor_t* factoryGetDescriptor(
	const clap_preset_discovery_factory_t* factory, uint32_t index)
{
	return index == 0 ? &providerDesc : NULL;
}

static const clap_preset_discovery_provider_t* factoryCreate(const clap_preset_discovery_factory_t* factory,
	const clap_preset_discovery_indexer_t* indexer, const char* id)
{
	if(strcmp(id, providerDesc.id)) return NULL;
	clap_preset_discovery_provider_t* p = malloc(sizeof *p);
	if(!p) return NULL;
	*p = (clap_preset_discovery_provider_t){
		&providerDesc, (void*)indexer, providerInit, providerDestroy, providerGetMetadata, providerGetExtension
	};
	return p;
}

static const clap_preset_discovery_factory_t factory = { factoryCount, factoryGetDescriptor, factoryCreate };

const clap_preset_discovery_factory_t* tack_presetDiscoveryFactory(void)
{
	return &factory;
}
