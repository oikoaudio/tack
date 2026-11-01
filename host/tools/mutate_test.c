/*
 * mutate_test.c
 *
 * Whether a mutated kit still sounds. Walks the init kit from many seeds, renders every voice and counts the voices that went quiet.
 * What moves is what the plugin moves, so this also checks that levels and routing stay, and that back returns to the start.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lxr_host.h"
#include "lxr_params.h"
#include "kit_mutate.h"

#define SEEDS 60u
#define WALK 12
#define BLOCKS 400u

static int peak(const uint8_t *kit, uint8_t voice)
{
	static int16_t a[OUTPUT_DMA_SIZE * 2], b[OUTPUT_DMA_SIZE * 2];
	int top = 0;
	lxr_host_init();
	lxr_host_loadKit(kit);
	lxr_host_noteOn(voice, LXR_HOST_BASE_NOTE, 127);
	for(unsigned n = 0; n < BLOCKS; n++) {
		lxr_host_renderBlock(a, b);
		for(unsigned i = 0; i < OUTPUT_DMA_SIZE * 2u; i++) {
			if(abs(a[i]) > top) top = abs(a[i]);
			if(abs(b[i]) > top) top = abs(b[i]);
		}
	}
	return top;
}

int main(void)
{
	static uint8_t top[LXR_HOST_NUM_PARAMS], voice[LXR_HOST_NUM_PARAMS], start[LXR_HOST_NUM_PARAMS], kit[LXR_HOST_NUM_PARAMS];
	static KitWalk walk;
	int failed = 0;
	unsigned voices = 0, silent = 0;

	lxr_params_mutateTable(top, voice);
	lxr_host_init();
	lxr_host_initKit(start);

	for(uint32_t seed = 1; seed <= SEEDS; seed++) {
		memcpy(kit, start, sizeof kit);
		memset(&walk, 0, sizeof walk);
		kit_walk(&walk, kit, top, voice, KIT_MUTATE_ALL, WALK, seed * 2654435761u);
		for(uint32_t p = 0; p < LXR_HOST_NUM_PARAMS; p++)
			if(kit[p] != start[p] && (p == PAR_VOL1 || p == PAR_PAN1 || p == PAR_AUDIO_OUT1 || p == PAR_FX_TYPE)) {
				printf("FAIL parameter %u moved\n", p);
				failed = 1;
			}
		for(uint8_t v = 0; v < 6; v++) {
			const int before = peak(start, v);
			if(!kit[PAR_PWM_1 + v] && start[PAR_PWM_1 + v]) {
				puts("FAIL a walk took a pulse width to 0");
				failed = 1;
			}
			if(before < 200) continue;
			voices++;
			silent += peak(kit, v) < before / 50;
		}
		kit_walk(&walk, kit, top, voice, KIT_MUTATE_ALL, -WALK, 0);
		if(memcmp(kit, start, sizeof kit)) {
			puts("FAIL back did not return to the start");
			failed = 1;
		}
	}
	printf("%u voices after %d steps, %u gone quiet\n", voices, WALK, silent);
	if(silent * 50u > voices) {
		puts("FAIL more than one voice in 50 went quiet");
		failed = 1;
	}
	puts(failed ? "FAILED" : "mutate passed");
	return failed;
}
