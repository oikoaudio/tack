/*
 * lxr_core.c
 *
 * The engine's control surface: init, kits, parameters, notes and one block of audio. No libc beyond memset and no
 * files, so the same code runs in the desktop host and the FM-1 firmware.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "lxr_core.h"
#include <string.h>
#include <stdbool.h>
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
#include "Soundengine/Sequencer/sequencer.h"
#include "Preset/InitKit.h"
#include "Parameters.h"
#include "frontPanelParser_ui.h"
#include "menu/menu.h"
#include "menu/CcNr2Text.h"


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
	if(isModTarget(p) && value < getNumModTargets()) setModTarget(p, value);
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

	// three of a kit's bytes are MIDI's NRPN controllers and no sound's: sent in a row they would be a message
	for(uint32_t i = 0; i < END_OF_SOUND_PARAMETERS; i++)
	{
		const uint32_t p = parameterOrder[i];
		if(p != PAR_NRPN_DATA_ENTRY_COARSE && p != PAR_NRPN_FINE && p != PAR_NRPN_COARSE)
			sendCC(p, activeParameters[p]);
	}
}

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
