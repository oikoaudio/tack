/*
 * engine.c
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifdef __linux__
#define _GNU_SOURCE
#endif
#include "engine.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lxr_host.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#include <fcntl.h>
#include <unistd.h>
#ifdef __linux__
#include <sys/mman.h>
#endif
#endif

static const TackEngine engine =
{
	lxr_host_init, lxr_host_loadKit, lxr_host_setParam, lxr_host_getParam, lxr_host_setOutputRate, lxr_host_latency,
	lxr_host_queueNoteOn, lxr_host_queueParam, lxr_host_queueSeparateOutputs, lxr_host_setSeparateOutputs,
	lxr_host_setSoloVoice, lxr_host_setTempo, lxr_host_outChannels, lxr_host_process, lxr_host_shutdown,
};

TACK_EXPORT const TackEngine* tack_engine(void)
{
	return &engine;
}

// the engine library inside the bundle, on macOS: Contents/Frameworks, next to Contents/MacOS with the plugin
#define MAC_ENGINE_LIBRARY "../Frameworks/TackEngine.dylib"

#ifndef TACK_ENGINE_ONLY
//-----------------------------------------------------------
static bool ownEngineInUse;

// handles of the copies in use, to make sure no two instances ever get the same one
#define MAX_COPIES 256
static void* liveCopies[MAX_COPIES];

static bool isLive(void* handle)
{
	for(int i = 0; i < MAX_COPIES; i++)
		if(liveCopies[i] == handle) return true;
	return false;
}

static void setLive(void* handle, bool live)
{
	for(int i = 0; i < MAX_COPIES; i++)
		if(liveCopies[i] == (live ? NULL : handle)) {
			liveCopies[i] = live ? handle : NULL;
			return;
		}
}

#ifdef _WIN32
//-----------------------------------------------------------
// Windows: a copy of this DLL in the temp folder. The loader keeps DLLs apart by path, and a loaded DLL can't be
// deleted, so the file stays until its copy is unloaded. Copies a crash left behind are removed the next time.

static void* loadLibrary(const wchar_t* path) { return LoadLibraryExW(path, NULL, LOAD_WITH_ALTERED_SEARCH_PATH); }
static void unloadLibrary(void* handle) { FreeLibrary(handle); }
static void* librarySymbol(void* handle, const char* name) { return (void*)GetProcAddress(handle, name); }

static void removeStaleCopies(const wchar_t* folder)
{
	wchar_t pattern[MAX_PATH + 32];
	_snwprintf(pattern, MAX_PATH + 32, L"%lstack-engine-*.dll", folder);
	WIN32_FIND_DATAW data;
	const HANDLE find = FindFirstFileW(pattern, &data);
	if(find == INVALID_HANDLE_VALUE) return;
	do {
		// fails for the copies other processes have loaded
		wchar_t path[MAX_PATH + 32];
		_snwprintf(path, MAX_PATH + 32, L"%ls%ls", folder, data.cFileName);
		DeleteFileW(path);
	} while(FindNextFileW(find, &data));
	FindClose(find);
}

static void* loadCopy(TackEngineCopy* e)
{
	HMODULE self;
	wchar_t src[MAX_PATH], folder[MAX_PATH], dst[MAX_PATH + 64];
	if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
		(LPCWSTR)(void*)tack_engine, &self)) return NULL;
	const DWORD n = GetModuleFileNameW(self, src, MAX_PATH);
	if(!n || n == MAX_PATH || !GetTempPathW(MAX_PATH, folder)) return NULL;

	static bool cleaned;
	if(!cleaned) removeStaleCopies(folder);
	cleaned = true;

	static unsigned counter;
	_snwprintf(dst, MAX_PATH + 64, L"%lstack-engine-%lu-%u.dll", folder, GetCurrentProcessId(), counter++);
	if(!CopyFileW(src, dst, FALSE)) return NULL;
	void* handle = loadLibrary(dst);
	if(!handle) {
		DeleteFileW(dst);
		return NULL;
	}
	char utf8[MAX_PATH * 4];
	WideCharToMultiByte(CP_UTF8, 0, dst, -1, utf8, sizeof utf8, NULL, NULL);
	e->path = strdup(utf8);
	return handle;
}

static void removeCopyFile(const char* path)
{
	wchar_t w[MAX_PATH + 64];
	if(MultiByteToWideChar(CP_UTF8, 0, path, -1, w, MAX_PATH + 64)) DeleteFileW(w);
}

#else
//-----------------------------------------------------------
static void* loadLibrary(const char* path) { return dlopen(path, RTLD_NOW | RTLD_LOCAL); }
static void unloadLibrary(void* handle) { dlclose(handle); }
static void* librarySymbol(void* handle, const char* name) { return dlsym(handle, name); }
static void removeCopyFile(const char* path) { unlink(path); }

static bool copyFile(const char* src, int dst)
{
	const int in = open(src, O_RDONLY | O_CLOEXEC);
	if(in < 0) return false;
	char buf[65536];
	ssize_t n;
	bool ok = true;
	while(ok && (n = read(in, buf, sizeof buf)) > 0)
		for(ssize_t done = 0; ok && done < n; ) {
			const ssize_t w = write(dst, buf + done, n - done);
			ok = w > 0;
			done += w;
		}
	close(in);
	return ok && n == 0;
}

// the binary to copy: this one on Linux, the bundle's engine library on macOS
static bool sourcePath(char* path, size_t size)
{
	Dl_info info;
	if(!dladdr((void*)tack_engine, &info) || !info.dli_fname) return false;
#ifdef __APPLE__
	const char* slash = strrchr(info.dli_fname, '/');
	if(!slash) return false;
	return snprintf(path, size, "%.*s/" MAC_ENGINE_LIBRARY, (int)(slash - info.dli_fname), info.dli_fname) < (int)size;
#else
	return snprintf(path, size, "%s", info.dli_fname) < (int)size;
#endif
}

// Linux: a memfd works even where /tmp doesn't allow executables. The dynamic loader treats a path it has already
// loaded as the same library, so the memfd stays open while its copy is loaded: that keeps its /proc/self/fd path
// unique among the live copies. macOS: a file in the per-user temp folder, kept until its copy is unloaded for the
// same reason.
static void* loadCopy(TackEngineCopy* e)
{
	char src[4096], copy[4096];
	if(!sourcePath(src, sizeof src)) return NULL;
#ifdef __linux__
	const int fd = memfd_create("tack-engine", MFD_CLOEXEC);
	snprintf(copy, sizeof copy, "/proc/self/fd/%d", fd);
#else
	const char* tmp = getenv("TMPDIR");
	snprintf(copy, sizeof copy, "%s/tack-engine-XXXXXX.dylib", tmp && tmp[0] ? tmp : "/tmp");
	const int fd = mkstemps(copy, 6);
#endif
	if(fd < 0) return NULL;
	void* handle = copyFile(src, fd) ? loadLibrary(copy) : NULL;
#ifdef __linux__
	if(handle) e->fd = fd;
	else close(fd);
#else
	close(fd);
	if(handle) e->path = strdup(copy);
	else unlink(copy);
#endif
	return handle;
}
#endif
//-----------------------------------------------------------
static void closeCopy(TackEngineCopy* e)
{
	if(e->handle) unloadLibrary(e->handle);
#ifndef _WIN32
	if(e->fd >= 0) close(e->fd);
#endif
	if(e->path) removeCopyFile(e->path);
	free(e->path);
	e->handle = NULL;
	e->fd = -1;
	e->path = NULL;
}

bool tack_engineOpen(TackEngineCopy* e)
{
	*e = (TackEngineCopy){ .fd = -1 };
	e->handle = loadCopy(e);
	if(e->handle && isLive(e->handle)) {
		// the loader handed out a copy that is in use; never share one
		unloadLibrary(e->handle);
		e->handle = NULL;
		closeCopy(e);
	}
	if(e->handle) {
		const TackEngine* (*get)(void) = (const TackEngine* (*)(void))librarySymbol(e->handle, "tack_engine");
		if(get && get() != &engine) {
			e->api = get();
			setLive(e->handle, true);
			return true;
		}
		closeCopy(e);
	}

	if(ownEngineInUse) return false;
	ownEngineInUse = true;
	e->api = &engine;
	return true;
}

void tack_engineClose(TackEngineCopy* e)
{
	if(e->handle) {
		setLive(e->handle, false);
		closeCopy(e);
	} else if(e->api == &engine) {
		ownEngineInUse = false;
	}
	e->api = NULL;
}
#endif
