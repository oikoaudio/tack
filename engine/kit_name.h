/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Makes up a kit name for a roll: eight capitals or spaces, padded with spaces at the end. No NUL. */
void kit_name_make_up(uint32_t seed, char *t);

#ifdef __cplusplus
}
#endif
