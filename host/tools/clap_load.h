/*
 * clap_load.h
 *
 * Loads a CLAP plugin file for the test tools, as a host would: a shared library on Linux and Windows, a bundle
 * (Tack.clap/Contents/MacOS/Tack) on macOS.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once
#include <stdio.h>
#include <string.h>
#include <clap/clap.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

typedef struct { void* lib; const clap_plugin_entry_t* entry; } ClapFile;

// prints why and returns false if it can't be loaded
static bool clapLoad(ClapFile* f, const char* path)
{
	char binary[4096];
	snprintf(binary, sizeof binary, "%s", path);
#ifdef __APPLE__
	// the bundle's executable is named after the bundle
	const char* slash = strrchr(path, '/');
	const char* name = slash ? slash + 1 : path;
	const char* dot = strrchr(name, '.');
	snprintf(binary, sizeof binary, "%s/Contents/MacOS/%.*s", path, (int)(dot ? dot - name : (long)strlen(name)), name);
#endif
#ifdef _WIN32
	f->lib = LoadLibraryA(binary);
	f->entry = f->lib ? (const clap_plugin_entry_t*)(void*)GetProcAddress(f->lib, "clap_entry") : NULL;
	if(!f->entry) fprintf(stderr, "could not load %s (error %lu)\n", binary, GetLastError());
#else
	f->lib = dlopen(binary, RTLD_NOW | RTLD_LOCAL);
	f->entry = f->lib ? (const clap_plugin_entry_t*)dlsym(f->lib, "clap_entry") : NULL;
	if(!f->entry) fprintf(stderr, "%s\n", dlerror());
#endif
	return f->entry && f->entry->init(path);
}

static void clapUnload(ClapFile* f)
{
	f->entry->deinit();
#ifdef _WIN32
	FreeLibrary(f->lib);
#else
	dlclose(f->lib);
#endif
}
