/*
 * timing_test.c
 *
 * Checks that queued notes come out a constant time after they were sent: triggers drum 1 at random output frames,
 * processes in random chunk sizes and measures each onset against note time + lxr_host_latency().
 *
 *   timing_test [rate]
 *
 * Exits non-zero if any event was late or the onset spread exceeds one engine block.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "lxr_host.h"

#define HITS 50
#define THRESHOLD 0.01f

// onset of a hit in the native engine, in engine samples from the start of the block it was triggered in.
// The first hit after init starts later than all others, so idle for a while first, as the test does.
static double nativeOnset(void)
{
	int16_t dac1[OUTPUT_DMA_SIZE * 2], dac2[OUTPUT_DMA_SIZE * 2];
	lxr_host_init();
	for(int b = 0; b < 400; b++) lxr_host_renderBlock(dac1, dac2);
	lxr_host_noteOn(0, 0, 127);
	for(int b = 0; b < 16; b++) {
		lxr_host_renderBlock(dac1, dac2);
		for(int i = 0; i < OUTPUT_DMA_SIZE; i++)
			if(fabsf(dac1[i*2] / 32768.f) > THRESHOLD) return b * OUTPUT_DMA_SIZE + i;
	}
	return -1;
}

int main(int argc, char** argv)
{
	const double fs = argc > 1 ? atof(argv[1]) : 48000;
	const double native = nativeOnset();

	lxr_host_init();
	lxr_host_setOutputRate(fs);
	const uint32_t latency = lxr_host_latency();
	const uint32_t spacing = (uint32_t)(0.75 * fs);
	srand(1);

	uint32_t noteAt[HITS];
	for(int h = 0; h < HITS; h++) noteAt[h] = (h + 1) * spacing + rand() % (spacing / 3);	// hits at least 0.5 s apart, past the kick's tail

	const uint32_t total = (HITS + 2) * spacing;
	float* out = malloc(sizeof(float) * LXR_HOST_OUT_CHANNELS * total);
	uint32_t pos = 0;
	int next = 0;
	while(pos < total) {
		uint32_t n = 1 + rand() % 1024;
		if(n > total - pos) n = total - pos;
		while(next < HITS && noteAt[next] < pos + n)
			lxr_host_queueNoteOn(noteAt[next] - pos, 0, 127), next++;
		lxr_host_process(&out[pos * LXR_HOST_OUT_CHANNELS], n);
		pos += n;
	}

	double minErr = 1e9, maxErr = -1e9, sum = 0;
	for(int h = 0; h < HITS; h++) {
		uint32_t i = noteAt[h];
		while(i < total && fabsf(out[i * LXR_HOST_OUT_CHANNELS]) <= THRESHOLD) i++;
		const double err = (double)i - noteAt[h] - latency;
		minErr = fmin(minErr, err); maxErr = fmax(maxErr, err); sum += err;
	}

	const LxrHostStats st = lxr_host_stats();
	const double block = OUTPUT_DMA_SIZE * fs / REAL_FS;
	printf("%.0f Hz: latency %u frames (%.2f ms); onset - note - latency: mean %.1f, min %.1f, max %.1f frames "
		"(spread %.2f ms, one block = %.1f frames); expected mean %.1f\n",
		fs, latency, 1000.0 * latency / fs, sum / HITS, minErr, maxErr, 1000.0 * (maxErr - minErr) / fs, block,
		native * fs / REAL_FS);
	printf("late events %u, dropped %u, resampler lead up to %.1f engine samples\n", st.lateEvents, st.droppedEvents, st.maxLead);
	free(out);
	return (st.lateEvents || maxErr - minErr > block + 1) ? 1 : 0;
}
