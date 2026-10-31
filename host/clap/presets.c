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
#include <errno.h>
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
		if(mkdir(folder, 0755) && errno != EEXIST) return TACK_SAVE_FAILED;
		*p = c;
		if(!c) break;
	}

	if(snprintf(path, pathSize, "%s/%s.SND", folder, base) >= (int)pathSize) return TACK_SAVE_FAILED;
	struct stat st;
	if(!overwrite && !stat(path, &st)) return TACK_SAVE_EXISTS;
	return lxr_host_writeKitFile(path, kit, name) ? TACK_SAVE_FAILED : TACK_SAVE_OK;
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
	DIR* dir = opendir(dirPath);
	if(!dir) return;
	struct dirent* e;
	while((e = readdir(dir))) {
		if(e->d_name[0] == '.') continue;
		char rel[2048], path[4096];
		snprintf(rel, sizeof rel, "%s%s%s", folder, folder[0] ? "/" : "", e->d_name);
		snprintf(path, sizeof path, "%s/%s", dirPath, e->d_name);
		struct stat st;
		if(stat(path, &st)) continue;
		if(S_ISDIR(st.st_mode)) {
			if(depth < 4) collectKits(list, rel, depth + 1);
		} else if(S_ISREG(st.st_mode) && isKitFile(e->d_name)) {
			if(list->n == list->cap) {
				list->cap = list->cap ? 2 * list->cap : 256;
				list->paths = realloc(list->paths, list->cap * sizeof *list->paths);
			}
			list->paths[list->n++] = strdup(rel);
		}
	}
	closedir(dir);
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
	// the built-in Init Kit covers these
	if(tack_isEmptySlot(name)) return true;
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
