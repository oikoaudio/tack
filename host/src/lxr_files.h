/*
 * lxr_files.h
 *
 * The few file system operations Tack needs, with UTF-8 paths on every platform: Windows' C library takes paths
 * in the local code page, so a name outside it (a user folder in Cyrillic, say) needs the wide-character calls.
 * Paths may use / on Windows too.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once
#include <stdio.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

FILE* lxr_fopen(const char* path, const char* mode);

/** Renames from to to, replacing to if it exists (POSIX rename(); Windows' rename() refuses). 0 on success. */
int lxr_replaceFile(const char* from, const char* to);

int lxr_removeFile(const char* path);

/** Creates a folder; 0 on success or if it exists already. */
int lxr_makeFolder(const char* path);

bool lxr_isFolder(const char* path);
bool lxr_isFile(const char* path);

/** Lists a folder's entries, without . and .. */
typedef struct LxrFolder LxrFolder;
LxrFolder* lxr_openFolder(const char* path);
/** The next entry's name, or NULL at the end. Valid until the next call. */
const char* lxr_readFolder(LxrFolder* folder);
void lxr_closeFolder(LxrFolder* folder);

#ifdef __cplusplus
}
#endif
