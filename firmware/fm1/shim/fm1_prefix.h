/* SPDX-License-Identifier: GPL-3.0-only */
/* Force-included before every engine source in the FM-1 build. The AC79's FPU is single precision and the image
 * links no soft-double routines, so the engine's doubles are floats on this target (literals too:
 * -Xclang -cl-single-precision-constant in fm1.toml). tools/compare_precision measures what that does to the sound. */
#pragma once
/* before the macro: clang's own headers spell long double */
#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#define double float
#define LXR_HOST_BUILD 1
#define LXR_FM1_BUILD 1

/* the simulator builds with this machine's compiler, where the half float is spelled _Float16 */
#ifdef PLAT_HOST
#define __fp16 _Float16
#endif
