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
#include "lxr_host.h"
#include "lxr_files.h"
#include "lxr_params.h"
#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#endif

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
	// Linux: $XDG_DATA_HOME/Tack/Kits, by default ~/.local/share/Tack/Kits. Windows and macOS: Documents/Tack/Kits,
	// where people find it to copy kits from an SD card into
	static char path[1024];
	if(path[0]) return path;
#ifdef _WIN32
	PWSTR docs = NULL;
	char utf8[MAX_PATH * 4] = "";
	if(SUCCEEDED(SHGetKnownFolderPath(&FOLDERID_Documents, 0, NULL, &docs)))
		WideCharToMultiByte(CP_UTF8, 0, docs, -1, utf8, sizeof utf8, NULL, NULL);
	CoTaskMemFree(docs);
	snprintf(path, sizeof path, "%s/Tack/Kits", utf8);
	tack_slashes(path);
#elif defined(__APPLE__)
	const char* home = getenv("HOME");
	snprintf(path, sizeof path, "%s/Documents/Tack/Kits", home ? home : "");
#else
	const char* data = getenv("XDG_DATA_HOME");
	const char* home = getenv("HOME");
	if(data && data[0])
		snprintf(path, sizeof path, "%s/Tack/Kits", data);
	else
		snprintf(path, sizeof path, "%s/.local/share/Tack/Kits", home ? home : "");
#endif
	return path;
}

void tack_slashes(char* path)
{
#ifdef _WIN32
	for(char* p = path; *p; p++)
		if(*p == '\\') *p = '/';
#else
	(void)path;
#endif
}
//-----------------------------------------------------------
bool tack_isKitNameChar(char c)
{
	return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || (c && strchr(" _-.[]!?~+$@&#", c));
}

int tack_saveKit(const uint8_t* kit, const char* name, bool overwrite, char* path, size_t pathSize)
{
	// the file is named after the kit: spaces and dots, which would hide the file or confuse its extension, become _
	char base[9];
	size_t n = 0;
	for(; n < 8 && name[n]; n++) {
		if(!tack_isKitNameChar(name[n])) return TACK_SAVE_FAILED;
		base[n] = name[n] == ' ' || name[n] == '.' ? '_' : name[n];
	}
	base[n] = 0;
	if(!n) return TACK_SAVE_FAILED;

	// the kit folder and the pack folder in it may not exist yet
	char folder[1024];
	snprintf(folder, sizeof folder, "%s/" TACK_USER_PACK, tack_kitFolder());
	for(char* p = folder + 1; ; p++) {
		if(*p != '/' && *p) continue;
		const char c = *p;
		*p = 0;
		// a drive ("C:") is there already
		if(!(p > folder && p[-1] == ':') && lxr_makeFolder(folder)) return TACK_SAVE_FAILED;
		*p = c;
		if(!c) break;
	}

	if(snprintf(path, pathSize, "%s/%s.SND", folder, base) >= (int)pathSize) return TACK_SAVE_FAILED;
	if(!overwrite && (lxr_isFile(path) || lxr_isFolder(path))) return TACK_SAVE_EXISTS;
	return lxr_host_writeKitFile(path, kit, name) ? TACK_SAVE_FAILED : TACK_SAVE_OK;
}
//-----------------------------------------------------------
static const char* const voiceKeys[6] = { "drum1", "drum2", "drum3", "snare", "cymbal", "hihat" };

const char* tack_voiceKey(int voice)
{
	return voice >= 0 && voice < 6 ? voiceKeys[voice] : TACK_KIT_KEY;
}

int tack_voiceOfKey(const char* key)
{
	for(int v = 0; key && v < 6; v++)
		if(!strcmp(key, voiceKeys[v])) return v;
	return -1;
}

const char* tack_voicePluginId(int voice)
{
	static const char* const ids[6] = {
		TACK_PLUGIN_ID ".drum", TACK_PLUGIN_ID ".drum", TACK_PLUGIN_ID ".drum",
		TACK_PLUGIN_ID ".snare", TACK_PLUGIN_ID ".cymbal", TACK_PLUGIN_ID ".hihat"
	};
	return voice >= 0 && voice < 6 ? ids[voice] : TACK_PLUGIN_ID;
}

void tack_voicePresetName(const char* kitName, int voice, uint8_t voices, char* name, size_t size)
{
	if(voice < 3 && voices != 1 << voice)
		snprintf(name, size, "%s Drum %d", kitName, voice + 1);
	else
		snprintf(name, size, "%s", kitName);
}

uint8_t tack_soundingVoices(const uint8_t* kit)
{
	uint8_t voices = 0;
	for(int v = 0; v < 6; v++)
		if(!lxr_params_voiceIsInit(kit, v)) voices |= 1 << v;
	return voices;
}
//-----------------------------------------------------------
bool tack_isEmptySlot(const char* name)
{
	// "Initkit"; some are saved with the name's third and fourth characters garbled ("In\xfd\xafkit")
	return strlen(name) == 7 && !strncasecmp(name, "In", 2) && !strcasecmp(name + 4, "kit");
}

static bool isKitFile(const char* name)
{
	const size_t n = strlen(name);
	return n > 4 && !strcasecmp(name + n - 4, ".snd");
}

typedef struct { char** paths; int n, cap; } PathList;

// collects the kit files below folder (relative to the kit folder); packs are often symlinks, so follow them, but
// only a few levels deep in case they loop
static void collectKits(PathList* list, const char* folder, int depth)
{
	char dirPath[2048];
	snprintf(dirPath, sizeof dirPath, "%s%s%s", tack_kitFolder(), folder[0] ? "/" : "", folder);
	LxrFolder* dir = lxr_openFolder(dirPath);
	if(!dir) return;
	const char* name;
	while((name = lxr_readFolder(dir))) {
		if(name[0] == '.') continue;
		char rel[2048], path[4096];
		snprintf(rel, sizeof rel, "%s%s%s", folder, folder[0] ? "/" : "", name);
		snprintf(path, sizeof path, "%s/%s", dirPath, name);
		if(lxr_isFolder(path)) {
			if(depth < 4) collectKits(list, rel, depth + 1);
		} else if(isKitFile(name) && lxr_isFile(path)) {
			if(list->n == list->cap) {
				list->cap = list->cap ? 2 * list->cap : 256;
				list->paths = realloc(list->paths, list->cap * sizeof *list->paths);
			}
			list->paths[list->n++] = strdup(rel);
		}
	}
	lxr_closeFolder(dir);
}

// by folder, then file name, so that each folder's kits come together
static int comparePaths(const void* a, const void* b)
{
	const char* pa = *(char* const*)a;
	const char* pb = *(char* const*)b;
	const char* sa = strrchr(pa, '/');
	const char* sb = strrchr(pb, '/');
	const size_t la = sa ? (size_t)(sa - pa) : 0, lb = sb ? (size_t)(sb - pb) : 0;
	const int folder = strncasecmp(pa, pb, la < lb ? la : lb);
	if(folder) return folder;
	if(la != lb) return la < lb ? -1 : 1;
	return strcasecmp(sa ? sa + 1 : pa, sb ? sb + 1 : pb);
}

void tack_freeKits(TackKitList* list)
{
	for(int i = 0; i < list->numFolders; i++) free(list->folders[i]);
	for(int i = 0; i < list->numKits; i++) free(list->kits[i].path);
	free(list->folders);
	free(list->kits);
	*list = (TackKitList){0};
}

void tack_scanKits(TackKitList* list)
{
	tack_freeKits(list);
	PathList files = {0};
	collectKits(&files, "", 0);
	qsort(files.paths, files.n, sizeof *files.paths, comparePaths);

	list->kits = malloc((files.n ? files.n : 1) * sizeof *list->kits);
	list->folders = malloc((files.n ? files.n : 1) * sizeof *list->folders);
	for(int i = 0; i < files.n; i++) {
		char path[4096];
		snprintf(path, sizeof path, "%s/%s", tack_kitFolder(), files.paths[i]);
		uint8_t kit[LXR_HOST_NUM_PARAMS];
		TackKit k = { .path = NULL };
		if(lxr_host_readKitFile(path, kit, k.name) || tack_isEmptySlot(k.name)) {
			free(files.paths[i]);
			continue;
		}
		// as the preset discovery: a kit without a name goes by its file name
		char* slash = strrchr(files.paths[i], '/');
		if(!k.name[0]) snprintf(k.name, sizeof k.name, "%s", slash ? slash + 1 : files.paths[i]);

		// files sorted by path come folder by folder
		const char* folder = slash ? files.paths[i] : "";
		if(slash) *slash = 0;
		if(!list->numFolders || strcmp(list->folders[list->numFolders - 1], folder))
			list->folders[list->numFolders++] = strdup(folder);
		k.folder = list->numFolders - 1;
		k.path = strdup(path);
		k.voices = tack_soundingVoices(kit);
		list->kits[list->numKits++] = k;
		free(files.paths[i]);
	}
	free(files.paths);
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

	// the kit folder only once it exists: hosts treat a missing location as an error. It appears with the first kit
	// pack, or the first kit saved, and hosts pick it up at their next scan
	if(!lxr_isFolder(tack_kitFolder())) return true;
	const clap_preset_discovery_location_t folder = {
		CLAP_PRESET_DISCOVERY_IS_USER_CONTENT, "Kits", CLAP_PRESET_DISCOVERY_LOCATION_FILE, tack_kitFolder()
	};
	indexer->declare_location(indexer, &folder);

	// each subfolder is a sound pack
	LxrFolder* dir = lxr_openFolder(tack_kitFolder());
	if(dir) {
		const char* name;
		while((name = lxr_readFolder(dir))) {
			if(name[0] == '.') continue;
			char path[2048];
			snprintf(path, sizeof path, "%s/%s", tack_kitFolder(), name);
			if(!lxr_isFolder(path)) continue;
			const clap_preset_discovery_soundpack_t pack = {
				CLAP_PRESET_DISCOVERY_IS_USER_CONTENT, name, name, NULL, NULL, NULL, NULL, CLAP_TIMESTAMP_UNKNOWN
			};
			indexer->declare_soundpack(indexer, &pack);
		}
		lxr_closeFolder(dir);
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
		// the init kit, and its voices for the voice plugins
		if(!receiver->begin_preset(receiver, "Init Kit", TACK_INIT_KIT_KEY)) return true;
		receiver->add_plugin_id(receiver, &pluginId);
		for(int v = 2; v < 6; v++) {
			const clap_universal_plugin_id_t id = { "clap", tack_voicePluginId(v) };
			receiver->add_plugin_id(receiver, &id);
		}
		receiver->set_flags(receiver, CLAP_PRESET_DISCOVERY_IS_FACTORY_CONTENT);
		return true;
	}

	// the host may hand the location back with \ on Windows; the kit folder has /
	char loc[4096];
	snprintf(loc, sizeof loc, "%s", location);
	tack_slashes(loc);
	location = loc;

	uint8_t kit[LXR_HOST_NUM_PARAMS];
	char name[9];
	if(lxr_host_readKitFile(location, kit, name)) {
		receiver->on_error(receiver, 0, "Not a readable kit file");
		return false;
	}
	// the built-in Init Kit covers these
	if(tack_isEmptySlot(name)) return true;
	if(!name[0]) {
		const char* base = strrchr(location, '/');
		snprintf(name, sizeof name, "%s", base ? base + 1 : location);
	}

	// the first folder below the kit folder is the sound pack
	char pack[256] = "";
	const char* root = tack_kitFolder();
	const size_t rootLen = strlen(root);
	if(!strncmp(location, root, rootLen) && location[rootLen] == '/') {
		snprintf(pack, sizeof pack, "%s", location + rootLen + 1);
		char* slash = strchr(pack, '/');
		if(slash) *slash = 0;
		else pack[0] = 0;
	}

	// one preset per file, the whole kit for Tack. Offering each voice as well, for the voice plugins, makes the
	// file a container (every preset with a load_key), which hosts didn't handle: Bitwig 6.1.6 listed no Tack
	// presets at all, and clap-validator 0.4.1 hung. The voice plugins offer kits' voices in their own browser.
	if(!receiver->begin_preset(receiver, name, NULL)) return true;
	receiver->add_plugin_id(receiver, &pluginId);
	receiver->set_flags(receiver, CLAP_PRESET_DISCOVERY_IS_USER_CONTENT);
	if(pack[0]) receiver->set_soundpack_id(receiver, pack);
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
