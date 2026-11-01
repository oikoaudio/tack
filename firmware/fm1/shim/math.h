/* SPDX-License-Identifier: GPL-3.0-only */
/* The FM-1 links no libm. The engine's calls go to fm1-base's single-precision fastmath; double is float here
 * (fm1_prefix.h), so the names the engine spells in double take the same route. */
#pragma once
#include "fastmath.h"

#ifndef M_PI
#define M_PI 3.14159265358979f
#endif

#define sinf fm_sinf
#define cosf fm_cosf
#define tanf fm_tanf
#define expf fm_expf
#define logf fm_logf
#define powf fm_powf
#define sqrtf fm_sqrtf
#define fabsf fm_fabsf
#define floorf fm_floorf
#define tanhf fm_tanhf

#define sin fm_sinf
#define cos fm_cosf
#define tan fm_tanf
#define exp fm_expf
#define log fm_logf
#define pow fm_powf
#define sqrt fm_sqrtf
#define fabs fm_fabsf
#define floor fm_floorf
#define tanh fm_tanhf

static inline float fm1_fmodf(float x, float y) { return x - y * (float)(int32_t)(x / y); }
#define fmod fm1_fmodf
#define fmodf fm1_fmodf

/* no fused multiply-add on this FPU; the engine's fma calls become a multiply and an add */
#define fmaf(a, b, c) ((a) * (b) + (c))
#define fma(a, b, c) ((a) * (b) + (c))
