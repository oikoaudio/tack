/*
 * engine.c
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#define _GNU_SOURCE
#include "engine.h"
#include <stdio.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include "lxr_host.h"

static const TackEngine engine =
{
	lxr_host_init, lxr_host_loadKit, lxr_host_setParam, lxr_host_getParam, lxr_host_setOutputRate, lxr_host_latency,
	lxr_host_queueNoteOn, lxr_host_queueParam, lxr_host_queueSeparateOutputs, lxr_host_setSeparateOutputs,
	lxr_host_process, lxr_host_shutdown,
};

__attribute__((visibility("default"))) const TackEngine* tack_engine(void)
{
	return &engine;
}
//-----------------------------------------------------------
static bool ownEngineInUse;

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

// Linux only for now. A memfd works even where /tmp doesn't allow executables. The dynamic loader treats a path it
// has already loaded as the same library, so the memfd stays open while its copy is loaded: that keeps its
// /proc/self/fd path unique among the live copies.
static void* loadCopy(const char* path, int* fdOut)
{
	const int fd = memfd_create("tack-engine", MFD_CLOEXEC);
	if(fd < 0) return NULL;
	void* handle = NULL;
	if(copyFile(path, fd)) {
		char fdPath[64];
		snprintf(fdPath, sizeof fdPath, "/proc/self/fd/%d", fd);
		handle = dlopen(fdPath, RTLD_NOW | RTLD_LOCAL);
	}
	if(handle && isLive(handle)) {
		// the loader handed out a copy that is in use; never share one
		dlclose(handle);
		handle = NULL;
	}
	if(!handle) {
		close(fd);
		return NULL;
	}
	*fdOut = fd;
	return handle;
}

bool tack_engineOpen(TackEngineCopy* e)
{
	Dl_info info;
	e->handle = NULL;
	e->fd = -1;
	if(dladdr((void*)tack_engine, &info) && info.dli_fname)
		e->handle = loadCopy(info.dli_fname, &e->fd);
	if(e->handle) {
		const TackEngine* (*get)(void) = (const TackEngine* (*)(void))dlsym(e->handle, "tack_engine");
		if(get && get() != &engine) {
			e->api = get();
			setLive(e->handle, true);
			return true;
		}
		dlclose(e->handle);
		close(e->fd);
		e->handle = NULL;
		e->fd = -1;
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
		dlclose(e->handle);
		close(e->fd);
	} else if(e->api == &engine) {
		ownEngineInUse = false;
	}
	e->api = NULL;
	e->handle = NULL;
	e->fd = -1;
}
