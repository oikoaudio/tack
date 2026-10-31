/*
 * lxr_host.c
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "lxr_host.h"
#include <stdio.h>
#include <samplerate.h>
#include "stm32f7xx_hal.h"
#include "Soundengine/DSPAudio/DrumVoice.h"
#include "Soundengine/DSPAudio/Snare.h"
#include "Soundengine/DSPAudio/HiHat.h"
#include "Soundengine/DSPAudio/CymbalVoice.h"
#include "Soundengine/DSPAudio/mixer.h"
#include "Soundengine/DSPAudio/modulationNode.h"
#include "Soundengine/MIDI/MidiParser.h"
#include "Soundengine/MIDI/ParameterArray.h"
#include "Soundengine/MIDI/MidiVoiceControl.h"
#include "Preset/InitKit.h"
#include "Parameters.h"
#include "frontPanelParser_ui.h"

GPIO_TypeDef host_gpio[5];
uint8_t activeParameters[NUM_PARAMS];
extern uint8_t parameterOrder[END_OF_SOUND_PARAMETERS];

//-----------------------------------------------------------
static void loadInitKit(void)
{
	// initKit starts with an 8 byte header
	for(int i = 8; i < END_OF_SOUND_PARAMETERS; i++)
		activeParameters[i - 8] = initKit[i];

	// same send loop as preset_sendDrumsetParametersImmediate(), minus the front panel traffic
	MidiMsg msg = {0};
	for(uint32_t i = 0; i < END_OF_SOUND_PARAMETERS; i++)
	{
		const uint8_t p = parameterOrder[i];
		if(p < 128) {
			msg.status = MIDI_CC;
			msg.data1 = (p + 1) & 0x7f;
		} else {
			msg.status = CC_2;
			msg.data1 = (p - 128) & 0x7f;
		}
		msg.data2 = activeParameters[p];
		midiParser_ccHandler(msg, 1, 0, 0.f);
	}
}
//-----------------------------------------------------------
void lxr_host_init(void)
{
	// the parts of soundengine_init() that don't touch the codec or sequencer
	mixer_init();
	parameterArray_init();

	// "no modulation target" is a NULL ptr. On the STM32 that reads harmless flash at address 0,
	// on a PC it segfaults, so point those entries at a dummy
	static uint32_t nullParam;
	for(int i = 0; i < END_OF_SOUND_PARAMETERS; i++)
		if(!parameterArray[i].ptr) parameterArray[i].ptr = &nullParam;

	for(int i = 0; i < 6; i++)
		modNode_init(&velocityModulators[i]);

	initDrumVoice();
	Snare_init();
	HiHat_init();
	Cymbal_init();

	loadInitKit();
}
//-----------------------------------------------------------
void lxr_host_noteOn(uint8_t voice, uint8_t note, uint8_t vel)
{
	voiceControl_noteOn(voice, note, vel);
}
//-----------------------------------------------------------
void lxr_host_renderBlock(int16_t* dac1, int16_t* dac2)
{
	// mixer_calcNextSampleBlock(output, output2): output is DAC2, output2 is DAC1
	mixer_calcNextSampleBlock(dac2, dac1);
}
//-----------------------------------------------------------
static SRC_STATE* resampler;
static double resampleRatio;

// libsamplerate pulls engine output through this, one block at a time
static long resamplerCallback(void* data, float** frames)
{
	static float block[OUTPUT_DMA_SIZE * 4];
	int16_t dac1[OUTPUT_DMA_SIZE * 2], dac2[OUTPUT_DMA_SIZE * 2];
	(void)data;

	lxr_host_renderBlock(dac1, dac2);
	for(int i = 0; i < OUTPUT_DMA_SIZE; i++)
	{
		block[i*4 + 0] = dac1[i*2 + 0] / 32768.f;
		block[i*4 + 1] = dac1[i*2 + 1] / 32768.f;
		block[i*4 + 2] = dac2[i*2 + 0] / 32768.f;
		block[i*4 + 3] = dac2[i*2 + 1] / 32768.f;
	}
	*frames = block;
	return OUTPUT_DMA_SIZE;
}
//-----------------------------------------------------------
int lxr_host_setOutputRate(double fs)
{
	int err = 0;
	if(resampler) src_delete(resampler);
	resampler = src_callback_new(resamplerCallback, SRC_SINC_MEDIUM_QUALITY, 4, &err, NULL);
	resampleRatio = fs / REAL_FS;
	return resampler ? 0 : -1;
}
//-----------------------------------------------------------
void lxr_host_process(float* out, uint32_t frames)
{
	while(frames > 0)
	{
		const long n = src_callback_read(resampler, resampleRatio, frames, out);
		if(n <= 0) {
			memset(out, 0, sizeof(float) * 4 * frames);
			return;
		}
		out += n * 4;
		frames -= n;
	}
}
//-----------------------------------------------------------
int lxr_host_writeWav(const char* path, const int16_t* data, uint32_t frames, uint16_t channels, uint32_t fs)
{
	FILE* f = fopen(path, "wb");
	if(!f) return -1;

	const uint16_t pcm = 1, bits = 16, align = channels * 2;
	const uint32_t bytes = frames * align, riff = 36 + bytes, fmtLen = 16, byteRate = fs * align;
	fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f);
	fwrite(&fmtLen, 4, 1, f); fwrite(&pcm, 2, 1, f); fwrite(&channels, 2, 1, f);
	fwrite(&fs, 4, 1, f); fwrite(&byteRate, 4, 1, f); fwrite(&align, 2, 1, f); fwrite(&bits, 2, 1, f);
	fwrite("data", 1, 4, f); fwrite(&bytes, 4, 1, f);
	fwrite(data, align, frames, f);
	return fclose(f);
}
