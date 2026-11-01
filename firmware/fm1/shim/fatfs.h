/* SPDX-License-Identifier: GPL-3.0-only */
/* PresetManager.h names FatFS's types. The FM-1 has no SD card and links no preset code; the types are all it needs. */
#pragma once
typedef int FRESULT;
typedef struct { int unused; } FIL;
typedef struct { int unused; } FATFS;
typedef struct { int unused; } DIR;
typedef struct { int unused; } FILINFO;
typedef unsigned int UINT;
