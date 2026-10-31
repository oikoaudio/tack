/*
 * lxr_host.c
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "lxr_host.h"
#include "lxr_files.h"
#include <stdio.h>
#include <string.h>
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

_Static_assert(LXR_HOST_BASE_NOTE == SEQ_DEFAULT_NOTE, "the base note is the firmware's");

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
// Tack's one change to the kit data: the PWM waveform subtracts two copies of a saw offset by the pulse width, so
// at width 0 it is silent. Pulse width only matters for PWM, so 1 is its minimum here.
static uint8_t adjustValue(uint16_t p, uint8_t value)
{
	if(p >= PAR_PWM_1 && p <= PAR_PWM_6 && value < 1) return 1;
	// one stereo output: a voice plays there directly or through the FX bus, and the bus returns there. The
	// device's second pair and mono jacks become that output.
	if(p >= PAR_AUDIO_OUT1 && p <= PAR_AUDIO_OUT6 && value != MIXER_ROUTING_FX_BUS) return MIXER_ROUTING_DAC1_STEREO;
	if(p == PAR_FX_ROUTING) return MIXER_ROUTING_DAC1_STEREO;
	return value;
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
	for(int i = 0; i < END_OF_SOUND_PARAMETERS; i++)
		activeParameters[i] = adjustValue(i, values[i]);

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
	FILE* f = lxr_fopen(path, "rb");
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
int lxr_host_writeKitFile(const char* path, const uint8_t* values, const char* name)
{
	char kitName[8];
	memset(kitName, ' ', 8);
	memcpy(kitName, name, strnlen(name, 8));

	char tmp[4096];
	if(snprintf(tmp, sizeof tmp, "%s.tmp", path) >= (int)sizeof tmp) return -1;
	FILE* f = lxr_fopen(tmp, "wb");
	if(!f) return -1;
	const bool ok = fwrite(kitName, 1, 8, f) == 8 && fwrite(values, 1, END_OF_SOUND_PARAMETERS, f) == END_OF_SOUND_PARAMETERS;
	if(fclose(f) || !ok || lxr_replaceFile(tmp, path)) {
		lxr_removeFile(tmp);
		return -1;
	}
	return 0;
}
//-----------------------------------------------------------
void lxr_host_initKit(uint8_t* values)
{
	// as preset_loadDrumset() for the init kit: initKit has an 8 byte header, and the firmware's loop leaves the
	// last 8 parameters at 0
	memset(values, 0, END_OF_SOUND_PARAMETERS);
	for(int i = 8; i < END_OF_SOUND_PARAMETERS; i++)
		values[i - 8] = initKit[i];

	// That includes FX parameters 2-4, which silence every FX type at 0 (the delay's range, the distortion's
	// volume, the compressor's threshold and gain). Init kits saved by the device itself have 27, 127 and 121 there,
	// so use those: picking an FX type on the init kit then does something.
	values[PAR_FX_P2] = 27;
	values[PAR_FX_P3] = 127;
	values[PAR_FX_P4] = 121;
}
//-----------------------------------------------------------
void lxr_host_setParam(uint16_t param, uint8_t value)
{
	if(param >= END_OF_SOUND_PARAMETERS) return;
	value = adjustValue(param, value);
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

static int soloVoice = -1;	// lxr_host_setSoloVoice()
static uint32_t outChannels = LXR_HOST_OUT_CHANNELS;

void lxr_host_renderBlockSeparate(int16_t voices[LXR_HOST_NUM_VOICE_OUTS][OUTPUT_DMA_SIZE * 2], int16_t* main)
{
	// mixer_calcNextSampleBlock() with each direct voice going to its own buffer instead of a DAC: same control-rate
	// updates, same order, same pan. The FX bus is a group bus with an insert: the voices routed to it are summed
	// and processed together, as on the device, and play on main.
	// with a solo voice, only that voice is computed, and the FX bus not at all
	#define ON(v) (soloVoice < 0 || soloVoice == (v))
	modNode_resetTargets();
	modNode_reassignVeloMod();

	if(ON(0)) lfo_dispatchNextValue(&voiceArray[0].lfo);
	if(ON(1)) lfo_dispatchNextValue(&voiceArray[1].lfo);
	if(ON(2)) lfo_dispatchNextValue(&voiceArray[2].lfo);
	if(ON(3)) lfo_dispatchNextValue(&snareVoice.lfo);
	if(ON(4)) lfo_dispatchNextValue(&cymbalVoice.lfo);
	if(ON(5)) lfo_dispatchNextValue(&hatVoice.lfo);

	if(ON(0)) SVF_recalcFreq(&voiceArray[0].filter);
	if(ON(1)) SVF_recalcFreq(&voiceArray[1].filter);
	if(ON(2)) SVF_recalcFreq(&voiceArray[2].filter);
	if(ON(3)) SVF_recalcFreq(&snareVoice.filter);
	if(ON(4)) SVF_recalcFreq(&cymbalVoice.filter);
	if(ON(5)) SVF_recalcFreq(&hatVoice.filter);

	if(ON(0)) calcDrumVoiceAsync(0);
	if(ON(1)) calcDrumVoiceAsync(1);
	if(ON(2)) calcDrumVoiceAsync(2);
	if(ON(3)) Snare_calcAsync();
	if(ON(4)) Cymbal_calcAsync();
	if(ON(5)) HiHat_calcAsync();

	int16_t sampleData[OUTPUT_DMA_SIZE];
	int16_t fxBusData[OUTPUT_DMA_SIZE * 2];
	bufferTool_clearBuffer(fxBusData, OUTPUT_DMA_SIZE * 2);

	for(int v = 0; v < LXR_HOST_NUM_VOICE_OUTS; v++)
	{
		if(!ON(v)) {
			memset(voices[v], 0, sizeof voices[v]);
			continue;
		}
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

		memset(voices[v], 0, sizeof voices[v]);
		addPanned(sampleData, panL, panR, mixer_audioRouting[v] == MIXER_ROUTING_FX_BUS ? fxBusData : voices[v]);
	}

	// the bus writes to the DAC its routing names: DAC1, the second pair of pointers (adjustValue())
	int16_t dac2[OUTPUT_DMA_SIZE * 2] = {0};
	memset(main, 0, sizeof(int16_t) * OUTPUT_DMA_SIZE * 2);
	if(soloVoice < 0) FxBus_process(fxBusData, &dac2[0], &dac2[1], &main[0], &main[1], OUTPUT_DMA_SIZE);
	#undef ON
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
	uint8_t note;
} Event;

static Event queue[QUEUE_SIZE];
static uint32_t queueRead, queueWrite;

static double resampleRatio;	// output rate / REAL_FS
static int64_t outFrames;		// output frames produced since lxr_host_setOutputRate()
static int64_t blocksRendered;
static int scheduleDelay;		// in engine blocks
static bool separateOutputs;
static LxrHostStats stats;

static void applyEvent(const Event* e)
{
	if(e->type == EVENT_NOTE_ON)
		lxr_host_noteOn(e->target, e->note, e->value);
	else if(e->type == EVENT_PARAM)
		lxr_host_setParam(e->target, e->value);
	else
		separateOutputs = e->value;
}

static void schedule(uint32_t frame, uint8_t type, uint16_t target, uint8_t value, uint8_t note)
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
	queue[queueWrite++ % QUEUE_SIZE] = (Event){ block, type, value, target, note };
}

void lxr_host_queueNoteOn(uint32_t frame, uint8_t voice, uint8_t note, uint8_t vel)
{
	schedule(frame, EVENT_NOTE_ON, voice, vel, note);
}

void lxr_host_queueParam(uint32_t frame, uint16_t param, uint8_t value)
{
	schedule(frame, EVENT_PARAM, param, value, 0);
}

void lxr_host_queueSeparateOutputs(uint32_t frame, bool on)
{
	schedule(frame, EVENT_SEPARATE_OUTPUTS, 0, on, 0);
}

void lxr_host_setSoloVoice(int voice)
{
	soloVoice = voice;
	outChannels = voice >= 0 ? 2 : LXR_HOST_OUT_CHANNELS;
}

uint32_t lxr_host_outChannels(void)
{
	return outChannels;
}

void lxr_host_setSeparateOutputs(bool on)
{
	separateOutputs = on;
}

//-----------------------------------------------------------
// Two resamplers: one for main (or the solo voice), always running, and one for the voices' own outputs, which only
// runs while they have something: with separate outputs, or until the last block rendered with them has come out.
// With everything on main that saves resampling twelve silent channels. Each pulls engine blocks through its
// callback; a block is rendered when the first of them needs it and waits in a ring for the other, since they read
// ahead independently.

#define VOICE_CHANNELS	(LXR_HOST_OUT_CHANNELS - 2)
#define RING_BLOCKS		16	// far more than either resampler reads ahead of the other
#define CHUNK			256	// output frames resampled at a time

static SRC_STATE* mainResampler;
static SRC_STATE* voiceResampler;	// NULL with a solo voice
static float mainRing[RING_BLOCKS][OUTPUT_DMA_SIZE * 2];
static float voiceRing[RING_BLOCKS][OUTPUT_DMA_SIZE * VOICE_CHANNELS];
static int64_t mainRead, voiceRead;	// the next block each resampler reads
static int voiceOffset;				// where in its next block the voice resampler starts, after a restart
static bool voicesRunning;
static int64_t lastSeparateBlock;	// the last block rendered with separate outputs, -1 for none

// copies an interleaved stereo engine buffer into two channels of frames with the given number of channels
static void putStereo(float* frames, int channels, int channel, const int16_t* stereo)
{
	for(int i = 0; i < OUTPUT_DMA_SIZE; i++)
	{
		frames[i*channels + channel + 0] = stereo[i*2 + 0] / 32768.f;
		frames[i*channels + channel + 1] = stereo[i*2 + 1] / 32768.f;
	}
}

static void renderNextBlock(void)
{
	while(queueRead != queueWrite && queue[queueRead % QUEUE_SIZE].block <= blocksRendered)
		applyEvent(&queue[queueRead++ % QUEUE_SIZE]);

	float* mainOut = mainRing[blocksRendered % RING_BLOCKS];
	float* voicesOut = voiceRing[blocksRendered % RING_BLOCKS];
	int16_t main[OUTPUT_DMA_SIZE * 2];
	if(soloVoice >= 0 || separateOutputs) {
		int16_t voices[LXR_HOST_NUM_VOICE_OUTS][OUTPUT_DMA_SIZE * 2];
		lxr_host_renderBlockSeparate(voices, main);
		if(soloVoice >= 0) {
			putStereo(mainOut, 2, 0, voices[soloVoice]);
			blocksRendered++;
			return;
		}
		for(int v = 0; v < LXR_HOST_NUM_VOICE_OUTS; v++)
			putStereo(voicesOut, VOICE_CHANNELS, 2*v, voices[v]);
		lastSeparateBlock = blocksRendered;
	} else {
		// everything is routed to DAC1 (adjustValue()); DAC2 stays silent
		int16_t dac2[OUTPUT_DMA_SIZE * 2];
		lxr_host_renderBlock(main, dac2);
		memset(voicesOut, 0, sizeof voiceRing[0]);
	}
	putStereo(mainOut, 2, 0, main);
	blocksRendered++;
}

// libsamplerate pulls engine output through these, one block at a time
static long mainCallback(void* data, float** frames)
{
	(void)data;
	if(mainRead >= blocksRendered) renderNextBlock();
	*frames = mainRing[mainRead++ % RING_BLOCKS];
	return OUTPUT_DMA_SIZE;
}

static long voiceCallback(void* data, float** frames)
{
	(void)data;
	while(voiceRead >= blocksRendered) renderNextBlock();
	const int offset = voiceOffset;
	voiceOffset = 0;
	*frames = voiceRing[voiceRead++ % RING_BLOCKS] + offset * VOICE_CHANNELS;
	return OUTPUT_DMA_SIZE - offset;
}

// resamples up to CHUNK frames of a resampler's channels into out, from the given channel on; out has stride
// channels per frame
static void readResampler(SRC_STATE* resampler, int channels, float* out, uint32_t stride, int channel, uint32_t frames)
{
	float buf[CHUNK * VOICE_CHANNELS];
	uint32_t done = 0;
	while(done < frames)
	{
		long n = src_callback_read(resampler, resampleRatio, frames - done, buf);
		if(n <= 0) {
			n = frames - done;
			memset(buf, 0, sizeof(float) * channels * n);
		}
		for(long i = 0; i < n; i++)
			memcpy(&out[(done + i)*stride + channel], &buf[i*channels], sizeof(float) * channels);
		done += n;
	}
}
//-----------------------------------------------------------
int lxr_host_setOutputRate(double fs)
{
	int err = 0;
	if(mainResampler) src_delete(mainResampler);
	if(voiceResampler) src_delete(voiceResampler);
	mainResampler = src_callback_new(mainCallback, SRC_SINC_MEDIUM_QUALITY, 2, &err, NULL);
	voiceResampler = soloVoice < 0 ? src_callback_new(voiceCallback, SRC_SINC_MEDIUM_QUALITY, VOICE_CHANNELS, &err, NULL) : NULL;
	resampleRatio = fs / REAL_FS;
	const double maxLead = 2 * OUTPUT_DMA_SIZE + OUTPUT_DMA_SIZE / fmin(resampleRatio, 1.0);
	scheduleDelay = (int)ceil((maxLead + OUTPUT_DMA_SIZE / 2) / OUTPUT_DMA_SIZE);
	outFrames = blocksRendered = 0;
	mainRead = voiceRead = 0;
	voiceOffset = 0;
	voicesRunning = false;
	lastSeparateBlock = -1;
	queueRead = queueWrite = 0;
	memset(&stats, 0, sizeof stats);
	return mainResampler && (soloVoice >= 0 || voiceResampler) ? 0 : -1;
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
		const uint32_t n = frames < CHUNK ? frames : CHUNK;
		readResampler(mainResampler, 2, out, outChannels, LXR_HOST_OUT_MAIN, n);

		if(soloVoice < 0) {
			// engine sample s comes out at output frame s*ratio, from either resampler
			const double start = outFrames / resampleRatio;
			if(!voicesRunning && separateOutputs) {
				// the blocks before the first one with separate outputs hold silence for the voices, so starting here
				// is in time for it: main has only just rendered it, reading ahead of this chunk
				const int64_t s = llround(start);
				src_reset(voiceResampler);
				voiceRead = s / OUTPUT_DMA_SIZE;
				voiceOffset = (int)(s % OUTPUT_DMA_SIZE);
				voicesRunning = true;
			} else if(voicesRunning && !separateOutputs
				&& start > (lastSeparateBlock + 1 + scheduleDelay) * (double)OUTPUT_DMA_SIZE) {
				// the last block with separate outputs is out, filter tail and all
				voicesRunning = false;
			}
			if(voicesRunning)
				readResampler(voiceResampler, VOICE_CHANNELS, out, outChannels, LXR_HOST_OUT_VOICE(0), n);
			else
				for(uint32_t i = 0; i < n; i++)
					memset(&out[i*outChannels + LXR_HOST_OUT_VOICE(0)], 0, sizeof(float) * VOICE_CHANNELS);
		}
		out += n * outChannels;
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
	if(mainResampler) src_delete(mainResampler);
	if(voiceResampler) src_delete(voiceResampler);
	mainResampler = voiceResampler = NULL;
}
//-----------------------------------------------------------
int lxr_host_writeWav(const char* path, const int16_t* data, uint32_t frames, uint16_t channels, uint32_t fs)
{
	FILE* f = lxr_fopen(path, "wb");
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
