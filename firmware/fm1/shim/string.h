/* SPDX-License-Identifier: GPL-3.0-only */
/* What the engine uses of string.h; felucca-bsp's chip/libc.c defines these three. */
#pragma once
#include <stddef.h>
void *memset(void *d, int c, unsigned n);
void *memcpy(void *d, const void *s, unsigned n);
int memcmp(const void *a, const void *b, unsigned n);
