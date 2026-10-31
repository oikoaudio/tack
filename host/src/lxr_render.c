/*
 * lxr_render.c
 *
 * Renders voices of the init kit through the full mixer to a stereo WAV file.
 *
 *   lxr_render [-r rate] out.wav [voice...]
 *
 * Voices are 0-2 drums, 3 snare, 4 cymbal, 5 closed hi-hat, 6 open hi-hat (default: all, in order).
 * Each listed voice is triggered at velocity 127, STEP_SECONDS apart. Both DAC pairs are summed into the file.
 * Without -r the file is at the engine's own rate, REAL_FS; with it, the output goes through the resampler.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "lxr_host.h"
#include "Soundengine/Sequencer/sequencer.h"

#define STEP_SECONDS	0.75f
#define TAIL_SECONDS	1.0f
#define PREROLL_BLOCKS	8

static uint8_t voices[32];
static int numVoices;
static int32_t peak1, peak2;

static int16_t sumDacs(int32_t l1, int32_t l2)
{
	peak1 = abs(l1) > peak1 ? abs(l1) : peak1;
	peak2 = abs(l2) > peak2 ? abs(l2) : peak2;
	return sat16(l1 + l2);
}

// engine blocks straight out of the mixer, bit-exact with the hardware's DSP
static uint32_t renderNative(int16_t** audio)
{
	const uint32_t stepBlocks = (uint32_t)(STEP_SECONDS * REAL_FS / OUTPUT_DMA_SIZE);
	const uint32_t numBlocks = PREROLL_BLOCKS + (numVoices - 1) * stepBlocks + (uint32_t)(TAIL_SECONDS * REAL_FS / OUTPUT_DMA_SIZE);
	*audio = malloc(sizeof(int16_t) * 2 * OUTPUT_DMA_SIZE * numBlocks);

	for(uint32_t b = 0; b < numBlocks; b++)
	{
		if(b >= PREROLL_BLOCKS && (b - PREROLL_BLOCKS) % stepBlocks == 0) {
			const uint32_t n = (b - PREROLL_BLOCKS) / stepBlocks;
			if(n < (uint32_t)numVoices) lxr_host_noteOn(voices[n], SEQ_DEFAULT_NOTE, 127);
		}

		int16_t dac1[OUTPUT_DMA_SIZE * 2], dac2[OUTPUT_DMA_SIZE * 2];
		lxr_host_renderBlock(dac1, dac2);

		int16_t* out = &(*audio)[b * OUTPUT_DMA_SIZE * 2];
		for(int i = 0; i < OUTPUT_DMA_SIZE * 2; i++)
			out[i] = sumDacs(dac1[i], dac2[i]);
	}
	return numBlocks * OUTPUT_DMA_SIZE;
}

// through lxr_host_process() at the given rate, as a plugin would run it
static uint32_t renderResampled(int16_t** audio, double fs)
{
	const uint32_t preroll = (uint32_t)(PREROLL_BLOCKS * OUTPUT_DMA_SIZE * fs / REAL_FS);
	const uint32_t step = (uint32_t)(STEP_SECONDS * fs);
	const uint32_t numFrames = preroll + (numVoices - 1) * step + (uint32_t)(TAIL_SECONDS * fs);
	*audio = malloc(sizeof(int16_t) * 2 * numFrames);

	float buf[4 * 512];
	uint32_t pos = 0;
	int next = 0;
	while(pos < numFrames)
	{
		const uint32_t nextTrigger = next < numVoices ? preroll + next * step : numFrames;
		if(pos == nextTrigger) {
			lxr_host_noteOn(voices[next++], SEQ_DEFAULT_NOTE, 127);
			continue;
		}

		uint32_t n = nextTrigger - pos;
		if(n > 512) n = 512;
		lxr_host_process(buf, n);
		for(uint32_t i = 0; i < n; i++) {
			(*audio)[(pos + i)*2 + 0] = sumDacs(lrintf(buf[i*4 + 0] * 32768.f), lrintf(buf[i*4 + 2] * 32768.f));
			(*audio)[(pos + i)*2 + 1] = sumDacs(lrintf(buf[i*4 + 1] * 32768.f), lrintf(buf[i*4 + 3] * 32768.f));
		}
		pos += n;
	}
	return numFrames;
}

int main(int argc, char** argv)
{
	double fs = 0;
	int arg = 1;
	if(argc > 2 && !strcmp(argv[1], "-r")) {
		fs = atof(argv[2]);
		arg = 3;
	}
	if(arg >= argc || (fs != 0 && fs < 8000)) {
		fprintf(stderr, "usage: %s [-r rate] out.wav [voice...]\n", argv[0]);
		return 1;
	}
	const char* path = argv[arg++];

	for(; arg < argc && numVoices < 32; arg++) {
		const int v = atoi(argv[arg]);
		if(v < 0 || v >= LXR_HOST_NUM_VOICES) {
			fprintf(stderr, "voice %d out of range 0..%d\n", v, LXR_HOST_NUM_VOICES - 1);
			return 1;
		}
		voices[numVoices++] = v;
	}
	if(numVoices == 0)
		for(; numVoices < LXR_HOST_NUM_VOICES; numVoices++) voices[numVoices] = numVoices;

	lxr_host_init();

	int16_t* audio;
	uint32_t frames;
	if(fs) {
		if(lxr_host_setOutputRate(fs)) {
			fprintf(stderr, "could not create resampler\n");
			return 1;
		}
		frames = renderResampled(&audio, fs);
	} else {
		fs = REAL_FS;
		frames = renderNative(&audio);
	}

	if(lxr_host_writeWav(path, audio, frames, 2, (uint32_t)fs)) {
		perror(path);
		return 1;
	}
	printf("wrote %u frames at %u Hz to %s (peak dac1 %d, dac2 %d)\n", frames, (uint32_t)fs, path, peak1, peak2);
	free(audio);
	return 0;
}
