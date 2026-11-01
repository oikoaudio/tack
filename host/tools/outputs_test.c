/*
 * outputs_test.c
 *
 * That the plugin's individual outputs are the device's mix: lxr_host_renderBlockSeparate() repeats the mixer's order by
 * hand, so all its outputs together must come to what lxr_host_renderBlock() renders.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lxr_host.h"

#define BLOCKS 400
#define VOICES 3				// the three drums: a voice with noise in it takes another path through the random numbers each run

static void start(void)
{
	lxr_host_init();
	for(uint8_t v = 0; v < VOICES; v++) lxr_host_noteOn(v, 48 + v, 100);
}

int main(void)
{
	static int16_t mixed[BLOCKS][OUTPUT_DMA_SIZE * 2];
	static int16_t voices[LXR_HOST_NUM_VOICE_OUTS][OUTPUT_DMA_SIZE * 2];
	int16_t a[OUTPUT_DMA_SIZE * 2], main_out[OUTPUT_DMA_SIZE * 2], b[OUTPUT_DMA_SIZE * 2];
	int worst = 0, peak = 0;

	start();
	for(int i = 0; i < BLOCKS; i++) {
		memset(a, 0, sizeof a);
		memset(b, 0, sizeof b);
		lxr_host_renderBlock(a, b);
		memcpy(mixed[i], a, sizeof a);
	}
	start();
	for(int i = 0; i < BLOCKS; i++) {
		memset(voices, 0, sizeof voices);
		memset(main_out, 0, sizeof main_out);
		lxr_host_renderBlockSeparate(voices, main_out);
		for(int s = 0; s < OUTPUT_DMA_SIZE * 2; s++) {
			int sum = main_out[s];
			for(int v = 0; v < LXR_HOST_NUM_VOICE_OUTS; v++) sum += voices[v][s];
			const int d = abs(sum - mixed[i][s]);
			if(d > worst) worst = d;
			if(abs(mixed[i][s]) > peak) peak = abs(mixed[i][s]);
		}
	}
	// a voice's rounding, in each of the seven buffers
	if(peak < 1000 || worst > 8) {
		printf("FAIL the outputs do not sum to the mix: peak %d, worst difference %d\n", peak, worst);
		puts("FAILED");
		return 1;
	}
	puts("outputs passed");
	return 0;
}
