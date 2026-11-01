/* SPDX-License-Identifier: GPL-3.0-only */
#include "kit_mutate.h"
#include <string.h>

#define MUTATE_REACH 12u                         /* of 127: a step is a variation of the kit, and many steps wander off */
#define MUTATE_CHOICE_BELOW 16u                  /* fewer values than this: a wave, a filter type. Numbers between mean nothing */
#define MUTATE_CHOICE_ODDS 24u                   /* one step in this moves a choice, as it can change a voice completely */
#define MUTATE_FLOOR 10u                         /* an envelope or a filter pushed to 0 is a voice that has gone */

/* What belongs to the mix or the kit's wiring. A filter's type too: a high pass on a low pass's frequency takes the whole voice away */
static int mutate_stays(uint32_t p)
{
    return (p >= PAR_VOL1 && p <= PAR_PAN6) || p == PAR_VOICE_DECIMATION_ALL
        || (p >= PAR_MIX_MOD_1 && p <= PAR_VOLUME_MOD_ON_OFF6)
        || (p >= PAR_VEL_DEST_1 && p <= PAR_VEL_DEST_6)
        || (p >= PAR_VOICE_LFO1 && p <= PAR_RETRIGGER_LFO6)
        || (p >= PAR_FILTER_TYPE_1 && p <= PAR_FILTER_TYPE_6)
        || (p >= PAR_AUDIO_OUT1 && p <= PAR_AUDIO_OUT6);
}

static int mutate_has_floor(uint32_t p)
{
    return (p >= PAR_FILTER_FREQ_1 && p <= PAR_FILTER_FREQ_6) || (p >= PAR_VELOA1 && p <= PAR_VELOD6_CLOSED && ((p - PAR_VELOA1) & 1u)) || p == PAR_VELOD6_OPEN;
}

static uint32_t mutate_roll(uint32_t *s)
{
    *s = *s * 1664525u + 1013904223u;
    return *s >> 8;
}

static void mutate_step(uint8_t *kit, const uint8_t *top, const uint8_t *voice, uint8_t scope, uint32_t seed)
{
    for (uint32_t p = 0; p < END_OF_SOUND_PARAMETERS; p++) {
        const uint32_t max = top[p];
        const uint32_t a = mutate_roll(&seed), b = mutate_roll(&seed), c = mutate_roll(&seed);
        if (!max || mutate_stays(p) || (scope != KIT_MUTATE_ALL && voice[p] != scope))
            continue;
        if (max < MUTATE_CHOICE_BELOW) {
            if (a % MUTATE_CHOICE_ODDS == 0u) {
                const int32_t v = (int32_t)kit[p] + (b & 1u ? 1 : -1);
                kit[p] = (uint8_t)(v < 0 ? 1 : v > (int32_t)max ? (int32_t)max - 1 : v);
            }
        } else if (a & 1u) {                     /* half of them, so that a step has a direction */
            const uint32_t span = (p >= PAR_COARSE1 && p <= PAR_FINE6 && !((p - PAR_COARSE1) & 1u) ? 3u : MUTATE_REACH * (max + 1u) / 128u) + 1u;
            const int32_t v = (int32_t)kit[p] + (int32_t)(b % span) - (int32_t)(c % span);
            const int32_t low = p >= PAR_PWM_1 && p <= PAR_PWM_6 ? 1 : mutate_has_floor(p) && kit[p] >= MUTATE_FLOOR ? (int32_t)MUTATE_FLOOR : 0;
            kit[p] = (uint8_t)(v < low ? low : v > (int32_t)max ? (int32_t)max : v);
        }
    }
}

static uint32_t mutate_hash(const uint8_t *kit)
{
    uint32_t h = 2166136261u;
    for (uint32_t p = 0; p < END_OF_SOUND_PARAMETERS; p++)
        h = (h ^ kit[p]) * 16777619u;
    return h;
}

int kit_walk(KitWalk *w, uint8_t *kit, const uint8_t *top, const uint8_t *voice, uint8_t scope, int32_t steps, uint32_t seed)
{
    int32_t step;
    if (w->seen != mutate_hash(kit) || w->scope != scope) {
        memcpy(w->from, kit, END_OF_SOUND_PARAMETERS);
        w->seed = seed;
        w->scope = scope;
        w->step = 0;
    }
    step = (int32_t)w->step + steps;
    step = step < 0 ? 0 : step > KIT_MUTATE_MAX_STEPS ? KIT_MUTATE_MAX_STEPS : step;
    memcpy(kit, w->from, END_OF_SOUND_PARAMETERS);
    for (int32_t i = 0; i < step; i++)
        mutate_step(kit, top, voice, scope, ((w->seed ^ (uint32_t)i) * 2654435761u ^ w->seed >> 15) * 16777619u);
    w->step = (uint8_t)step;
    w->seen = mutate_hash(kit);
    return step;
}

int kit_walk_step(const KitWalk *w, const uint8_t *kit)
{
    return w->seen == mutate_hash(kit) ? w->step : 0;
}
