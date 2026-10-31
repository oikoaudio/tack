/*
 * lxr_render.c
 *
 * Renders voices of the init kit through the full mixer to a stereo WAV file.
 *
 *   lxr_render [-r rate] [-k kit.snd] [-m] out.wav [voice...]
 *
 * Voices are 0-2 drums, 3 snare, 4 cymbal, 5 closed hi-hat, 6 open hi-hat (default: all, in order).
 * Each listed voice is triggered at velocity 127, STEP_SECONDS apart. Both DAC pairs are summed into the file;
 * Tack routes everything to the first (adjustValue() in lxr_host.c).
 * Without -r the file is at the engine's own rate, REAL_FS; with it, the output goes through the resampler.
 * -k loads a kit file from the device's SD card instead of the init kit.
 * -m renders with separate outputs (needs -r); the file is still the sum of all of them.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
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

	float buf[LXR_HOST_OUT_CHANNELS * 512];
	uint32_t pos = 0;
	int next = 0;
	while(pos < numFrames)
	{
		const uint32_t n = numFrames - pos < 512 ? numFrames - pos : 512;
		for(; next < numVoices && preroll + next * step < pos + n; next++)
			lxr_host_queueNoteOn(preroll + next * step - pos, voices[next], SEQ_DEFAULT_NOTE, 127);
		lxr_host_process(buf, n);
		for(uint32_t i = 0; i < n; i++) {
			const float* f = &buf[i * LXR_HOST_OUT_CHANNELS];
			// the voice outputs are silent unless -m
			float l = 0, r = 0;
			for(int c = LXR_HOST_OUT_VOICE(0); c < LXR_HOST_OUT_CHANNELS; c += 2) {
				l += f[c];
				r += f[c + 1];
			}
			(*audio)[(pos + i)*2 + 0] = sumDacs(lrintf(f[LXR_HOST_OUT_MAIN] * 32768.f), lrintf(l * 32768.f));
			(*audio)[(pos + i)*2 + 1] = sumDacs(lrintf(f[LXR_HOST_OUT_MAIN + 1] * 32768.f), lrintf(r * 32768.f));
		}
		pos += n;
	}
	return numFrames;
}

int main(int argc, char** argv)
{
	double fs = 0;
	const char* kitPath = NULL;
	bool separate = false;
	int arg = 1;
	for(; arg < argc && argv[arg][0] == '-'; arg++) {
		if(!strcmp(argv[arg], "-m")) separate = true;
		else if(!strcmp(argv[arg], "-r") && arg + 1 < argc) fs = atof(argv[++arg]);
		else if(!strcmp(argv[arg], "-k") && arg + 1 < argc) kitPath = argv[++arg];
		else break;
	}
	if(arg >= argc || argv[arg][0] == '-' || (fs != 0 && fs < 8000) || (separate && !fs)) {
		fprintf(stderr, "usage: %s [-r rate] [-k kit.snd] [-m] out.wav [voice...]\n", argv[0]);
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
	if(kitPath) {
		uint8_t kit[LXR_HOST_NUM_PARAMS];
		char name[9];
		if(lxr_host_readKitFile(kitPath, kit, name)) {
			perror(kitPath);
			return 1;
		}
		lxr_host_loadKit(kit);
		printf("kit \"%s\"\n", name);
	}

	int16_t* audio;
	uint32_t frames;
	if(fs) {
		if(lxr_host_setOutputRate(fs)) {
			fprintf(stderr, "could not create resampler\n");
			return 1;
		}
		lxr_host_setSeparateOutputs(separate);
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
