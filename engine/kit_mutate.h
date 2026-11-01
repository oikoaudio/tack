/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * Moves a kit a step at a time, for the FM-1's panel and the plugin. Each step is a small random change, and a walk can be retraced:
 * going back replays the steps from where the walk began, so a sound that was passed can be found again and only that one kit is kept.
 */
#pragma once
#include <stdint.h>
#include "Parameters.h"

#ifdef __cplusplus
extern "C" {
#endif

#define KIT_MUTATE_ALL 0xFFu                     /* every voice */
#define KIT_MUTATE_MAX_STEPS 99

typedef struct {
    uint8_t from[END_OF_SOUND_PARAMETERS];       /* the kit the walk began on */
    uint32_t seed, seen;                         /* seen: a hash of the kit it left, or any other kit starts a new walk */
    uint8_t step, scope;
} KitWalk;

/**
 * Walks kit by steps, forward if positive, back if negative, and stops at the start. Returns the step reached.
 *
 * top[p] is the highest value of a parameter and 0 for one that stays. voice[p] says which voice it belongs to. Levels, pans,
 * routing and modulation targets stay whatever top says. scope is a voice or KIT_MUTATE_ALL. seed is only used for a new walk.
 */
int kit_walk(KitWalk *w, uint8_t *kit, const uint8_t *top, const uint8_t *voice, uint8_t scope, int32_t steps, uint32_t seed);

/** How far kit is along the walk, 0 if it isn't on it: an edit or another kit since. */
int kit_walk_step(const KitWalk *w, const uint8_t *kit);

#ifdef __cplusplus
}
#endif
