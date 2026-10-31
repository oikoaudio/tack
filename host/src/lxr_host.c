/*
 * lxr_host.c
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "lxr_host.h"
#include <stdio.h>
#include <stdbool.h>
#include <math.h>
#include <samplerate.h>
#include "stm32f7xx_hal.h"
#include "Soundengine/DSPAudio/DrumVoice.h"
#include "Soundengine/DSPAudio/Snare.h"
#include "Soundengine/DSPAudio/HiHat.h"
#include "Soundengine/DSPAudio/CymbalVoice.h"
#include "Soundengine/DSPAudio/mixer.h"
#include "Soundengine/DSPAudio/BufferTools.h"
#include "Soundengine/FxBus.h"
#include "Soundengine/DSPAudio/modulationNode.h"
#include "Soundengine/MIDI/MidiParser.h"
#include "Soundengine/MIDI/ParameterArray.h"
#include "Soundengine/MIDI/MidiVoiceControl.h"
#include "Soundengine/Sequencer/sequencer.h"
#include "Preset/InitKit.h"
#include "Parameters.h"
#include "frontPanelParser_ui.h"
#include "menu/menu.h"
#include "menu/CcNr2Text.h"

// mixer.c defines this without declaring it in mixer.h
void mixer_decimateBlock(const uint8_t voiceNr, int16_t* buffer);

GPIO_TypeDef host_gpio[5];
uint8_t activeParameters[NUM_PARAMS];
extern uint8_t parameterOrder[END_OF_SOUND_PARAMETERS];

//-----------------------------------------------------------
// what the sound engine does when the front panel sets a modulation target (CortexParser.c)
static void setModTarget(uint16_t param, uint8_t modTargetIdx)
{
	const uint8_t dest = (uint8_t)modTargets[modTargetIdx].param;

	if(param >= PAR_VEL_DEST_1 && param < PAR_VEL_DEST_1 + 6)
	{
		modNode_setDestination(&velocityModulators[param - PAR_VEL_DEST_1], dest);
	}
	else
	{
		const uint8_t lfoNr = param - PAR_TARGET_LFO1;
		switch(lfoNr)
		{
		case 0:
		case 1:
		case 2:	modNode_setDestination(&voiceArray[lfoNr].lfo.modTarget, dest);	break;
		case 3:	modNode_setDestination(&snareVoice.lfo.modTarget, dest);		break;
		case 4:	modNode_setDestination(&cymbalVoice.lfo.modTarget, dest);		break;
		case 5:	modNode_setDestination(&hatVoice.lfo.modTarget, dest);			break;
		}
	}
}
//-----------------------------------------------------------
static bool isModTarget(uint16_t p)
{
	return (p >= PAR_VEL_DEST_1 && p < PAR_VEL_DEST_1 + 6) || (p >= PAR_TARGET_LFO1 && p < PAR_TARGET_LFO1 + 6);
}
//-----------------------------------------------------------
static void sendCC(uint16_t p, uint8_t value)
{
	MidiMsg msg = {0};
	if(p < 128) {
		// the sound engine's CC numbers are one higher than the parameter numbers
		msg.status = MIDI_CC;
		msg.data1 = (p + 1) & 0x7f;
	} else {
		msg.status = CC_2;
		msg.data1 = (p - 128) & 0x7f;
	}
	msg.data2 = value;
	midiParser_ccHandler(msg, 1, 0, 0.f);
}
//-----------------------------------------------------------
// what the front panel sends when a parameter is edited (menu.c): mod targets as a target message, then
// every parameter as a CC
static void sendParam(uint16_t p, uint8_t value)
{
	if(isModTarget(p)) setModTarget(p, value);
	sendCC(p, value);
}
//-----------------------------------------------------------
void lxr_host_loadKit(const uint8_t* values)
{
	memcpy(activeParameters, values, END_OF_SOUND_PARAMETERS);

	// as preset_sendDrumsetParametersImmediate(): mod targets first, then every parameter in parameterOrder
	const uint8_t nmt = getNumModTargets();
	for(int i = 0; i < 6; i++)
	{
		if(activeParameters[PAR_VEL_DEST_1 + i] >= nmt) activeParameters[PAR_VEL_DEST_1 + i] = 0;
		if(activeParameters[PAR_TARGET_LFO1 + i] >= nmt) activeParameters[PAR_TARGET_LFO1 + i] = 0;
		if(activeParameters[PAR_VOICE_LFO1 + i] < 1 || activeParameters[PAR_VOICE_LFO1 + i] > 6) activeParameters[PAR_VOICE_LFO1 + i] = 1;

		setModTarget(PAR_VEL_DEST_1 + i, activeParameters[PAR_VEL_DEST_1 + i]);
		setModTarget(PAR_TARGET_LFO1 + i, activeParameters[PAR_TARGET_LFO1 + i]);
	}

	for(uint32_t i = 0; i < END_OF_SOUND_PARAMETERS; i++)
		sendCC(parameterOrder[i], activeParameters[parameterOrder[i]]);
}
//-----------------------------------------------------------
int lxr_host_readKitFile(const char* path, uint8_t* values, char* name)
{
	// as preset_loadDrumsetSD() and preset_readDrumsetData()
	FILE* f = fopen(path, "rb");
	if(!f) return -1;
	char kitName[8];
	const size_t nameLen = fread(kitName, 1, 8, f);
	const size_t n = fread(values, 1, END_OF_SOUND_PARAMETERS, f);
	fclose(f);
	if(nameLen != 8) return -1;
	memset(values + n, 0, END_OF_SOUND_PARAMETERS - n);

	if(name) {
		memcpy(name, kitName, 8);
		name[8] = 0;
		for(int i = 7; i >= 0 && (name[i] == ' ' || name[i] == 0); i--) name[i] = 0;
	}
	return 0;
}
//-----------------------------------------------------------
void lxr_host_initKit(uint8_t* values)
{
	// as preset_loadDrumset() for the init kit: initKit has an 8 byte header, and like the firmware's loop this
	// leaves the last 8 parameters at 0
	memset(values, 0, END_OF_SOUND_PARAMETERS);
	for(int i = 8; i < END_OF_SOUND_PARAMETERS; i++)
		values[i - 8] = initKit[i];
}
//-----------------------------------------------------------
void lxr_host_setParam(uint16_t param, uint8_t value)
{
	if(param >= END_OF_SOUND_PARAMETERS) return;
	activeParameters[param] = value;
	sendParam(param, value);
}

uint8_t lxr_host_getParam(uint16_t param)
{
	return param < END_OF_SOUND_PARAMETERS ? activeParameters[param] : 0;
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

	uint8_t kit[END_OF_SOUND_PARAMETERS];
	lxr_host_initKit(kit);
	lxr_host_loadKit(kit);
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
// adds a voice block to an interleaved stereo buffer with the voice's pan, as mixer_addDataToOutput() does for
// stereo destinations
static void addPanned(const int16_t* data, float panL, float panR, int16_t* out)
{
	for(int i = 0; i < OUTPUT_DMA_SIZE; i++)
	{
		out[i*2 + 0] = sat16(out[i*2 + 0] + (int16_t)(data[i] * panL));
		out[i*2 + 1] = sat16(out[i*2 + 1] + (int16_t)(data[i] * panR));
	}
}

void lxr_host_renderBlockSeparate(int16_t voices[LXR_HOST_NUM_VOICE_OUTS][OUTPUT_DMA_SIZE * 2], int16_t* fx)
{
	// mixer_calcNextSampleBlock() with each voice going to its own buffer instead of a DAC: same control-rate
	// updates, same order, same pan.
	//
	// The FX bus is split like a send and return. Every effect blends dry + (wet - dry) * dryWet, and dryWet only
	// enters that blend, so a voice routed to the bus keeps its dry share (1 - dryWet) on its own output, and fx
	// gets the effect's share, wet * dryWet. The outputs still sum to the device's mix. With the FX off, the bus
	// passes its input through, so those voices stay whole on their own outputs.
	modNode_resetTargets();
	modNode_reassignVeloMod();

	lfo_dispatchNextValue(&voiceArray[0].lfo);
	lfo_dispatchNextValue(&voiceArray[1].lfo);
	lfo_dispatchNextValue(&voiceArray[2].lfo);
	lfo_dispatchNextValue(&snareVoice.lfo);
	lfo_dispatchNextValue(&cymbalVoice.lfo);
	lfo_dispatchNextValue(&hatVoice.lfo);

	SVF_recalcFreq(&voiceArray[0].filter);
	SVF_recalcFreq(&voiceArray[1].filter);
	SVF_recalcFreq(&voiceArray[2].filter);
	SVF_recalcFreq(&snareVoice.filter);
	SVF_recalcFreq(&cymbalVoice.filter);
	SVF_recalcFreq(&hatVoice.filter);

	calcDrumVoiceAsync(0);
	calcDrumVoiceAsync(1);
	calcDrumVoiceAsync(2);
	Snare_calcAsync();
	Cymbal_calcAsync();
	HiHat_calcAsync();

	int16_t sampleData[OUTPUT_DMA_SIZE];
	int16_t fxBusData[OUTPUT_DMA_SIZE * 2];
	bufferTool_clearBuffer(fxBusData, OUTPUT_DMA_SIZE * 2);
	const bool fxOn = fxBus.fxType != FX_OFF;
	const float dryWet = fxBus.dryWet;

	for(int v = 0; v < LXR_HOST_NUM_VOICE_OUTS; v++)
	{
		float panL, panR;
		switch(v)
		{
		case 0:
		case 1:
		case 2:
			calcDrumVoiceSyncBlock(v, sampleData, OUTPUT_DMA_SIZE);
			panL = voiceArray[v].triggeredPanL;
			panR = voiceArray[v].triggeredPanR;
			break;
		case 3:
			Snare_calcSyncBlock(sampleData, OUTPUT_DMA_SIZE);
			panL = snareVoice.triggeredPanL;
			panR = snareVoice.triggeredPanR;
			break;
		case 4:
			Cymbal_calcSyncBlock(sampleData, OUTPUT_DMA_SIZE);
			panL = cymbalVoice.triggeredPanL;
			panR = cymbalVoice.triggeredPanR;
			break;
		default:
			HiHat_calcSyncBlock(sampleData, OUTPUT_DMA_SIZE);
			panL = hatVoice.triggeredPanL;
			panR = hatVoice.triggeredPanR;
			break;
		}
		mixer_decimateBlock(v, sampleData);

		// every destination becomes the voice's own stereo output, panned
		memset(voices[v], 0, sizeof voices[v]);
		if(mixer_audioRouting[v] == MIXER_ROUTING_FX_BUS && fxOn) {
			addPanned(sampleData, panL, panR, fxBusData);
			addPanned(sampleData, panL * (1.f - dryWet), panR * (1.f - dryWet), voices[v]);
		} else {
			addPanned(sampleData, panL, panR, voices[v]);
		}
	}

	memset(fx, 0, sizeof(int16_t) * OUTPUT_DMA_SIZE * 2);
	if(!fxOn) return;

	// run the bus fully wet; it writes to whichever DAC its routing names, so collect both
	int16_t dacA[OUTPUT_DMA_SIZE * 2] = {0}, dacB[OUTPUT_DMA_SIZE * 2] = {0};
	fxBus.dryWet = 1.f;
	FxBus_process(fxBusData, &dacA[0], &dacA[1], &dacB[0], &dacB[1], OUTPUT_DMA_SIZE);
	fxBus.dryWet = dryWet;
	for(int i = 0; i < OUTPUT_DMA_SIZE * 2; i++)
		fx[i] = sat16(lrintf((dacA[i] + dacB[i]) * dryWet));
}
//-----------------------------------------------------------
// Events are timed in engine blocks. An event at output frame t is applied before the engine block nearest to
// t/ratio + scheduleDelay, so every event comes out the same time after it was sent, give or take half a block.
// scheduleDelay must cover how far ahead of the output the engine has rendered when the event is queued: about
// two blocks plus the resampler's filter, which widens when downsampling (measured up to 173 engine samples at
// 44.1-192 kHz and 214 at 22.05 kHz; timing_test checks it).

#define QUEUE_SIZE 1024

enum { EVENT_NOTE_ON, EVENT_PARAM, EVENT_SEPARATE_OUTPUTS };

typedef struct
{
	int64_t block;
	uint8_t type;
	uint8_t value;
	uint16_t target;
} Event;

static Event queue[QUEUE_SIZE];
static uint32_t queueRead, queueWrite;

static SRC_STATE* resampler;
static double resampleRatio;	// output rate / REAL_FS
static int64_t outFrames;		// output frames produced since lxr_host_setOutputRate()
static int64_t blocksRendered;
static int scheduleDelay;		// in engine blocks
static bool separateOutputs;
static LxrHostStats stats;

static void applyEvent(const Event* e)
{
	if(e->type == EVENT_NOTE_ON)
		lxr_host_noteOn(e->target, SEQ_DEFAULT_NOTE, e->value);
	else if(e->type == EVENT_PARAM)
		lxr_host_setParam(e->target, e->value);
	else
		separateOutputs = e->value;
}

static void schedule(uint32_t frame, uint8_t type, uint16_t target, uint8_t value)
{
	if(queueWrite - queueRead >= QUEUE_SIZE) {
		stats.droppedEvents++;
		return;
	}
	const double t = (outFrames + frame) / resampleRatio + scheduleDelay * OUTPUT_DMA_SIZE;
	int64_t block = (int64_t)floor(t / OUTPUT_DMA_SIZE + 0.5);
	if(block < blocksRendered) {
		stats.lateEvents++;
		block = blocksRendered;
	}
	queue[queueWrite++ % QUEUE_SIZE] = (Event){ block, type, value, target };
}

void lxr_host_queueNoteOn(uint32_t frame, uint8_t voice, uint8_t vel)
{
	schedule(frame, EVENT_NOTE_ON, voice, vel);
}

void lxr_host_queueParam(uint32_t frame, uint16_t param, uint8_t value)
{
	schedule(frame, EVENT_PARAM, param, value);
}

void lxr_host_queueSeparateOutputs(uint32_t frame, bool on)
{
	schedule(frame, EVENT_SEPARATE_OUTPUTS, 0, on);
}

void lxr_host_setSeparateOutputs(bool on)
{
	separateOutputs = on;
}

// copies an interleaved stereo engine buffer into two channels of the output frames
static void putStereo(float* block, int channel, const int16_t* stereo)
{
	for(int i = 0; i < OUTPUT_DMA_SIZE; i++)
	{
		block[i*LXR_HOST_OUT_CHANNELS + channel + 0] = stereo[i*2 + 0] / 32768.f;
		block[i*LXR_HOST_OUT_CHANNELS + channel + 1] = stereo[i*2 + 1] / 32768.f;
	}
}

// libsamplerate pulls engine output through this, one block at a time
static long resamplerCallback(void* data, float** frames)
{
	static float block[OUTPUT_DMA_SIZE * LXR_HOST_OUT_CHANNELS];
	(void)data;

	while(queueRead != queueWrite && queue[queueRead % QUEUE_SIZE].block <= blocksRendered)
		applyEvent(&queue[queueRead++ % QUEUE_SIZE]);

	memset(block, 0, sizeof block);
	if(separateOutputs) {
		int16_t voices[LXR_HOST_NUM_VOICE_OUTS][OUTPUT_DMA_SIZE * 2], fx[OUTPUT_DMA_SIZE * 2];
		lxr_host_renderBlockSeparate(voices, fx);
		for(int v = 0; v < LXR_HOST_NUM_VOICE_OUTS; v++)
			putStereo(block, LXR_HOST_OUT_VOICE(v), voices[v]);
		putStereo(block, LXR_HOST_OUT_FX, fx);
	} else {
		int16_t dac1[OUTPUT_DMA_SIZE * 2], dac2[OUTPUT_DMA_SIZE * 2];
		lxr_host_renderBlock(dac1, dac2);
		putStereo(block, LXR_HOST_OUT_DAC1, dac1);
		putStereo(block, LXR_HOST_OUT_DAC2, dac2);
	}
	blocksRendered++;
	*frames = block;
	return OUTPUT_DMA_SIZE;
}
//-----------------------------------------------------------
int lxr_host_setOutputRate(double fs)
{
	int err = 0;
	if(resampler) src_delete(resampler);
	resampler = src_callback_new(resamplerCallback, SRC_SINC_MEDIUM_QUALITY, LXR_HOST_OUT_CHANNELS, &err, NULL);
	resampleRatio = fs / REAL_FS;
	const double maxLead = 2 * OUTPUT_DMA_SIZE + OUTPUT_DMA_SIZE / fmin(resampleRatio, 1.0);
	scheduleDelay = (int)ceil((maxLead + OUTPUT_DMA_SIZE / 2) / OUTPUT_DMA_SIZE);
	outFrames = blocksRendered = 0;
	queueRead = queueWrite = 0;
	memset(&stats, 0, sizeof stats);
	return resampler ? 0 : -1;
}
//-----------------------------------------------------------
uint32_t lxr_host_latency(void)
{
	// libsamplerate's output is aligned with its input: engine sample n comes out at output frame n*ratio
	return (uint32_t)lrint(scheduleDelay * OUTPUT_DMA_SIZE * resampleRatio);
}
//-----------------------------------------------------------
void lxr_host_process(float* out, uint32_t frames)
{
	// how far ahead the engine already is when events for this call may be queued
	const double lead = blocksRendered * OUTPUT_DMA_SIZE - outFrames / resampleRatio;
	if(lead > stats.maxLead) stats.maxLead = lead;

	while(frames > 0)
	{
		const long n = src_callback_read(resampler, resampleRatio, frames, out);
		if(n <= 0) {
			memset(out, 0, sizeof(float) * LXR_HOST_OUT_CHANNELS * frames);
			outFrames += frames;
			return;
		}
		out += n * LXR_HOST_OUT_CHANNELS;
		frames -= n;
		outFrames += n;
	}
}
//-----------------------------------------------------------
LxrHostStats lxr_host_stats(void)
{
	return stats;
}
//-----------------------------------------------------------
void lxr_host_shutdown(void)
{
	if(resampler) src_delete(resampler);
	resampler = NULL;
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
