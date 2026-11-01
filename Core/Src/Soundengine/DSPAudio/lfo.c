/*
 * lfo.c
 *
 *  Created on: 14.01.2013
 *
 * Copyright (C) 2012-2026 Erica Synths
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * This file is part of the LXR firmware. It is licensed under the GNU General
 * Public License version 3, with the additional permission and terms in
 * NOTICE.md. See LICENSE and NOTICE.md in the root of the repository.
 */

//-------------------------------------------------------------
#include "Soundengine/FxBus.h"
#include "Soundengine/DSPAudio/lfo.h"
#include "Soundengine/DSPAudio/random.h"
#include <math.h>

#include "Soundengine/DSPAudio/DrumVoice.h"
#include "Soundengine/DSPAudio/CymbalVoice.h"
#include "Soundengine/DSPAudio/HiHat.h"
#include "Soundengine/DSPAudio/Snare.h"
#include "Soundengine/MIDI/valueShaper.h"
//-------------------------------------------------------------
void lfo_init(Lfo *lfo)
{
	lfo->phase 			= 0;
	lfo->phaseInc 		= 1;
	lfo->phaseOffset	= 0;
	lfo->retrigger		= 0;
	lfo->waveform		= SINE;
	lfo->sync			= 0;
	lfo->freq			= 1;
	lfo->modNodeValue	= 1;
	lfo->once			= 0;
	lfo->ended			= 0;

	modNode_init(&lfo->modTarget);
}
//-------------------------------------------------------------
IN_ITCM_RAM float lfo_calc(Lfo *lfo)
{
	uint32_t oldPhase 		= lfo->phase;
	float inc = lfo->phaseInc*lfo->modNodeValue;
	uint32_t incInt = inc;
	if(!lfo->ended)
		lfo->phase 		   += incInt;//lfo->phaseInc*lfo->modNodeValue;
	const uint8_t overflow 	= oldPhase>lfo->phase;
	if(overflow && lfo->once)
	{
		lfo->phase = 0xffffffff;
		lfo->ended = 1;
	}

	switch(lfo->waveform)
	{
	//---
		case LFO_SINE:
			return ((sine_table[(lfo->phase>>20)]/32767.f) + 1)/2.f;
		break;
	//---
		case LFO_TRI:
			return (1.f-fabsf( (lfo->phase/(float)0xffffffff)*2-1 ) );
			break;
	//---
		case LFO_SAW_UP:
		return lfo->phase/(float)0xffffffff ;
		break;
	//---
		case LFO_REC:
		if(lfo->phase > 0x7fffffff)
		{
			return 1.f;
		}
		else
		{
			return 0.f;
		}
		break;
	//---
		case LFO_NOISE:
		if(overflow)
		{
			lfo->rnd = GetRngValue();
			lfo->rnd = lfo->rnd  / (float)0xffffffff ;
		}

			return lfo->rnd;
		break;
	//---
		case LFO_SAW_DOWN:
		return (0xffffffff-(lfo->phase)) / (float)0xffffffff;
		break;
	//---
		case LFO_EXP_UP:
			{
				float x = lfo->phase/(float)0xffffffff;
				return x*x*x;
			}
			break;
	//---
		case LFO_EXP_DOWN:
			{
				float x = (0xffffffff-(lfo->phase)) / (float)0xffffffff;
				return x*x*x;//valueShaperF2F(x,-0.9f);
			}
			break;
	//---
		default:
		return 0;
		break;
	}
	return 0;
}
//-------------------------------------------------------------
// where a wave ends: the sine table starts and ends at its bottom, and random has no end to speak of
static float lfo_endValue(const Lfo* lfo)
{
	switch(lfo->waveform)
	{
		case LFO_SAW_UP:
		case LFO_REC:
		case LFO_EXP_UP:
			return 1.f;
		case LFO_SINE:
		case LFO_TRI:
		case LFO_SAW_DOWN:
		case LFO_EXP_DOWN:
			return 0.f;
		default:
			return 0.5f;
	}
}
//-------------------------------------------------------------
// Modulation moves a parameter both ways around where it is set, with 0.5 leaving it alone. Once, the wave is measured from its end
// instead, so it finishes on the value set: a rising wave dips and recovers, a falling one rises and returns
void lfo_dispatchNextValue(Lfo* lfo)
{
	float val = lfo_calc(lfo);
	if(lfo->once)
		val = lfo->ended ? 0.5f : (val - lfo_endValue(lfo) + 1.f) * 0.5f;
	modNode_updateValue(&lfo->modTarget,val);
}
//-------------------------------------------------------------
void lfo_setRetrigger(Lfo* lfo, uint8_t value)
{
	lfo->once = value > 6;
	lfo->ended = lfo->once;
	lfo->retrigger = lfo->once ? value - 6 : value;
}
//-------------------------------------------------------------
// another LFO aimed at RETRIG: which voice starts this one, and ONCE stays as it was set
void lfo_modRetrigger(Lfo* lfo, float value)
{
	const uint8_t v = value * 127.f;
	lfo->retrigger = v > 12 ? 6 : v > 6 ? v - 6 : v;
}
//-------------------------------------------------------------
IN_ITCM_RAM uint32_t lfo_calcPhaseInc(float freq, uint8_t sync)
{
	if(sync==0)//no sync
	{
		return freq/(LFO_SR) * 0xffffffff;
	}

	//sync is on

	float bpm =  seq_getBpm();

	float tempoAsFrequency = bpm > 0.0 ? bpm / 60.f /4: 0.0f; //bpm/60 = beat duration /4 = bar duration

	float scaler;

	switch(sync)
	{
		default: //no sync

		return freq/(LFO_SR) * 0xffffffff;
		break;

		case 1: // 4/1
		scaler = 0.25f;//NoteTypeScalers[Quadruple] * NoteDivisionScalers[Whole];
		break;

		case 2: // 2/1
		scaler = 0.5f;//NoteTypeScalers[Double] * NoteDivisionScalers[Whole];
		break;

		case 3: // 1/1
		scaler = 1;//NoteTypeScalers[Regular] * NoteDivisionScalers[Whole];
		break;

		case 4: // 1/2
		scaler = 2;// NoteTypeScalers[Regular] * NoteDivisionScalers[Half];
		break;

		case 5: //1/3
		scaler = 3;// NoteTypeScalers[Regular] * NoteDivisionScalers[Third];
		break;

		case 6: // 1/4
		scaler = 4;// NoteTypeScalers[Regular] * NoteDivisionScalers[Quarter];
		break;

		case 7: // 1/6
		scaler = 6;// NoteTypeScalers[Regular] * NoteDivisionScalers[Sixth];
		break;

		case 8: // 1/8
		scaler = 8;// NoteTypeScalers[Regular] * NoteDivisionScalers[Eighth];
		break;

		case 9: // 1/12
		scaler = 12;//NoteTypeScalers[Regular] * NoteDivisionScalers[Twelfth];
		break;

		case 10: // 1/16
		scaler = 16;//NoteTypeScalers[Regular] * NoteDivisionScalers[Sixteenth];
		break;

		case 11: // 1/32
		scaler = 32;//NoteTypeScalers[Regular] * NoteDivisionScalers[Sixteenth];
		break;
	}

	float lfoSyncfreq = tempoAsFrequency * scaler;
	return (lfoSyncfreq / (LFO_SR)) * 0xffffffff;
}
//-------------------------------------------------------------
IN_ITCM_RAM void lfo_setFreq(Lfo *lfo, float f)
{
	//f += 1;
	//f = f/128.f;
	f = 0.0078125f + f* (1.f-0.0078125f); //to prevent f from being zero
	f = f*f*f;
	lfo->freq = f*LFO_MAX_F;
	lfo->phaseInc = lfo_calcPhaseInc(lfo->freq,lfo->sync);
}
//-------------------------------------------------------------
IN_ITCM_RAM void lfo_setSync(Lfo* lfo, uint8_t sync)
{
	lfo->sync = sync;
	lfo->phaseInc = lfo_calcPhaseInc(lfo->freq,lfo->sync);
}
//-------------------------------------------------------------
void lfo_recalcSync()
{
	Delay_tempoChanged(&fxBus.delay);
	Lfo* lfo = &voiceArray[0].lfo;
	lfo->phaseInc = lfo_calcPhaseInc(lfo->freq,lfo->sync);

	lfo = &voiceArray[1].lfo;
	lfo->phaseInc = lfo_calcPhaseInc(lfo->freq,lfo->sync);

	lfo = &voiceArray[2].lfo;
	lfo->phaseInc = lfo_calcPhaseInc(lfo->freq,lfo->sync);

	lfo = &snareVoice.lfo;
	lfo->phaseInc = lfo_calcPhaseInc(lfo->freq,lfo->sync);

	lfo = &cymbalVoice.lfo;
	lfo->phaseInc = lfo_calcPhaseInc(lfo->freq,lfo->sync);

	lfo = &hatVoice.lfo;
	lfo->phaseInc = lfo_calcPhaseInc(lfo->freq,lfo->sync);
}
//-------------------------------------------------------------
#ifdef LXR_FM1_BUILD
// a random LFO sent by a voice's hit takes a new value with it: the value reaches its target at
// once, as velocity's does, before the voice starts, and holds until the next hit
static void lfo_retriggerOne(Lfo *lfo)
{
	lfo->phase = lfo->phaseOffset;
	lfo->ended = 0;
	if(lfo->waveform == LFO_NOISE)
	{
		lfo->rnd = GetRngValue() / (float)0xffffffff;
		modNode_updateValue(&lfo->modTarget, lfo->once ? lfo->rnd * 0.5f + 0.25f : lfo->rnd);	/* as lfo_dispatchNextValue will send it */
	}
}
#else
static void lfo_retriggerOne(Lfo *lfo)
{
	lfo->phase = lfo->phaseOffset;
	lfo->ended = 0;
}
#endif
void lfo_retrigger(uint8_t voice)
{
	if(voiceArray[0].lfo.retrigger == voice+1) lfo_retriggerOne(&voiceArray[0].lfo);
	if(voiceArray[1].lfo.retrigger == voice+1) lfo_retriggerOne(&voiceArray[1].lfo);
	if(voiceArray[2].lfo.retrigger == voice+1) lfo_retriggerOne(&voiceArray[2].lfo);
	if(snareVoice.lfo.retrigger == voice+1) lfo_retriggerOne(&snareVoice.lfo);
	if(cymbalVoice.lfo.retrigger == voice+1) lfo_retriggerOne(&cymbalVoice.lfo);
	if(hatVoice.lfo.retrigger == voice+1) lfo_retriggerOne(&hatVoice.lfo);
}
//-------------------------------------------------------------
