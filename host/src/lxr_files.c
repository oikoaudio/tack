/*
 * lxr_files.c
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "lxr_files.h"
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#include <wchar.h>

// a UTF-8 path as UTF-16, in a buffer the caller frees; NULL if it isn't valid UTF-8
static wchar_t* wide(const char* s)
{
	const int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, NULL, 0);
	if(n <= 0) return NULL;
	wchar_t* w = malloc(n * sizeof *w);
	if(w) MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, w, n);
	return w;
}

FILE* lxr_fopen(const char* path, const char* mode)
{
	wchar_t* p = wide(path);
	wchar_t* m = wide(mode);
	FILE* f = p && m ? _wfopen(p, m) : NULL;
	free(p);
	free(m);
	return f;
}

int lxr_replaceFile(const char* from, const char* to)
{
	wchar_t* f = wide(from);
	wchar_t* t = wide(to);
	const bool ok = f && t && MoveFileExW(f, t, MOVEFILE_REPLACE_EXISTING);
	free(f);
	free(t);
	return ok ? 0 : -1;
}

int lxr_removeFile(const char* path)
{
	wchar_t* p = wide(path);
	const bool ok = p && DeleteFileW(p);
	free(p);
	return ok ? 0 : -1;
}

int lxr_makeFolder(const char* path)
{
	wchar_t* p = wide(path);
	const bool ok = p && (CreateDirectoryW(p, NULL) || GetLastError() == ERROR_ALREADY_EXISTS);
	free(p);
	return ok ? 0 : -1;
}

static DWORD attributes(const char* path)
{
	wchar_t* p = wide(path);
	const DWORD a = p ? GetFileAttributesW(p) : INVALID_FILE_ATTRIBUTES;
	free(p);
	return a;
}

bool lxr_isFolder(const char* path)
{
	const DWORD a = attributes(path);
	return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

bool lxr_isFile(const char* path)
{
	const DWORD a = attributes(path);
	return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

struct LxrFolder
{
	HANDLE find;
	WIN32_FIND_DATAW data;
	bool pending;	// data holds an entry not returned yet
	char name[MAX_PATH * 4];
};

LxrFolder* lxr_openFolder(const char* path)
{
	const size_t n = strlen(path);
	char* pattern = malloc(n + 3);
	if(!pattern) return NULL;
	snprintf(pattern, n + 3, "%s/*", path);
	wchar_t* p = wide(pattern);
	free(pattern);
	LxrFolder* f = calloc(1, sizeof *f);
	if(f && p) f->find = FindFirstFileW(p, &f->data);
	free(p);
	if(!f || f->find == INVALID_HANDLE_VALUE || !f->find) {
		free(f);
		return NULL;
	}
	f->pending = true;
	return f;
}

const char* lxr_readFolder(LxrFolder* f)
{
	for(;;) {
		if(!f->pending && !FindNextFileW(f->find, &f->data)) return NULL;
		f->pending = false;
		if(!wcscmp(f->data.cFileName, L".") || !wcscmp(f->data.cFileName, L"..")) continue;
		if(WideCharToMultiByte(CP_UTF8, 0, f->data.cFileName, -1, f->name, sizeof f->name, NULL, NULL) > 0)
			return f->name;
	}
}

void lxr_closeFolder(LxrFolder* f)
{
	if(!f) return;
	FindClose(f->find);
	free(f);
}

#else
#include <dirent.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>

FILE* lxr_fopen(const char* path, const char* mode) { return fopen(path, mode); }
int lxr_replaceFile(const char* from, const char* to) { return rename(from, to); }
int lxr_removeFile(const char* path) { return unlink(path); }

int lxr_makeFolder(const char* path)
{
	return mkdir(path, 0755) && errno != EEXIST ? -1 : 0;
}

// both follow symlinks: sound packs are often links to a backup somewhere else
bool lxr_isFolder(const char* path)
{
	struct stat st;
	return !stat(path, &st) && S_ISDIR(st.st_mode);
}

bool lxr_isFile(const char* path)
{
	struct stat st;
	return !stat(path, &st) && S_ISREG(st.st_mode);
}

struct LxrFolder { DIR* dir; };

LxrFolder* lxr_openFolder(const char* path)
{
	DIR* dir = opendir(path);
	if(!dir) return NULL;
	LxrFolder* f = malloc(sizeof *f);
	if(!f) {
		closedir(dir);
		return NULL;
	}
	f->dir = dir;
	return f;
}

const char* lxr_readFolder(LxrFolder* f)
{
	struct dirent* e;
	while((e = readdir(f->dir)))
		if(strcmp(e->d_name, ".") && strcmp(e->d_name, "..")) return e->d_name;
	return NULL;
}

void lxr_closeFolder(LxrFolder* f)
{
	if(!f) return;
	closedir(f->dir);
	free(f);
}
#endif
