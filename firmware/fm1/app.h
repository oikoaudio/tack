/* SPDX-License-Identifier: GPL-3.0-only */
/* Tack's half of the contract with fm1-base (third_party/fm1-base/device/unity.c, host/plat_host.c). */
#pragma once
#include <stdint.h>
#include "plat.h"

#define APP_NAME "TACK"
#define APP_SPLASH_RGB RGB(232, 148, 62)
#define APP_TAGLINE "DRUM SYNTH"
#define APP_BLOCK 256u                   /* 4 engine blocks: 5.8 ms */

/* the store: the settings and kit, the panel calibration, and one object for each of the 16 patterns */
enum { OBJ_SETTINGS, OBJ_PANEL, OBJ_PAT0, OBJ_NOBJ = OBJ_PAT0 + 16 };
#define PAT_AT(i) (0x97000u + (i) * 0x2000u)
#define APP_STORE_MAP {0xFC000u, 0xDC000u, PAT_AT(0), PAT_AT(1), PAT_AT(2), PAT_AT(3), PAT_AT(4), PAT_AT(5), PAT_AT(6), \
                       PAT_AT(7), PAT_AT(8), PAT_AT(9), PAT_AT(10), PAT_AT(11), PAT_AT(12), PAT_AT(13), PAT_AT(14), PAT_AT(15)}

void app_init(void);
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12);
void app_panic(void);
void app_ui_init(void);
void app_frame(void);
void app_input_only(void);
void app_say(const char *a, const char *b);
#define APP_SYSEX                        /* kits from a computer (tools/fm1_kits.py) */
/* backup and restore over USB: tools/fm1 backup. The kit bank goes through fm1_kits.py */
#define APP_BACKUP_OBJS {OBJ_SETTINGS, OBJ_PAT0, OBJ_PAT0 + 1, OBJ_PAT0 + 2, OBJ_PAT0 + 3, OBJ_PAT0 + 4, OBJ_PAT0 + 5, \
                         OBJ_PAT0 + 6, OBJ_PAT0 + 7, OBJ_PAT0 + 8, OBJ_PAT0 + 9, OBJ_PAT0 + 10, OBJ_PAT0 + 11, \
                         OBJ_PAT0 + 12, OBJ_PAT0 + 13, OBJ_PAT0 + 14, OBJ_PAT0 + 15}
#define APP_BACKUP_MAGIC 0x424C544Bu     /* "KTLB" */
#define APP_BACKUP_VERSION 2u
#define APP_BACKUP_WHAT "replaces all patterns"
int app_backup_playing(void);
void app_backup_flush(void);
void app_backup_hold(int on);
void app_backup_restored(void);
int app_sysex(const uint8_t *p, uint32_t n);

#define APP_HOST_EXPECT
int app_expect(const char *what, int *got);
