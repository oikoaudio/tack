/*
 * lfo_test.c
 *
 * That an LFO in sync follows the tempo a host gives it, and that moving between FREQ and SYNC finds the same speed both ways.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include <stdio.h>
#include <math.h>
#include "lxr_host.h"
#include "lfo_speed.h"
#include "Soundengine/DSPAudio/DrumVoice.h"
#include "Soundengine/FxBus.h"

extern DrumVoice voiceArray[];

int main(void)
{
	int failed = 0;
	lxr_host_init();
	lxr_host_setParam(PAR_SYNC_LFO1, 6);	// 1/4
	lxr_host_setTempo(120);
	const double at120 = voiceArray[0].lfo.phaseInc;
	lxr_host_setTempo(60);
	const double at60 = voiceArray[0].lfo.phaseInc;
	lxr_host_setTempo(174);
	const double at174 = voiceArray[0].lfo.phaseInc;
	if(fabs(at60 / at120 - 0.5) > 0.01 || fabs(at174 / at120 - 1.45) > 0.01) {
		printf("FAIL the LFO does not follow the tempo: %f %f %f\n", at120, at60, at174);
		failed = 1;
	}
	for(uint8_t s = 1; s < LFO_SPEED_FRACTIONS; s++)
		for(int bpm = 40; bpm <= 240; bpm += 20) {
			const uint8_t f = lfo_freq_near(s, (float)bpm);
			// 4/1 at 40 BPM is 0.04 Hz, and FREQ's first steps are coarser than a fraction there
			if(s >= 3 && lfo_sync_near(f, (float)bpm) != s) {
				printf("FAIL %s at %d: FREQ %u comes back as %s\n", lfo_fraction_name(s), bpm, f, lfo_fraction_name(lfo_sync_near(f, (float)bpm)));
				failed = 1;
			}
		}
	// once: a rising wave on the FX's mix dips, recovers, and finishes on the value set, from a kit's value of 7
	lxr_host_setParam(PAR_FX_DRY_WET, 50);
	lxr_host_setParam(PAR_SYNC_LFO1, 0);
	lxr_host_setParam(PAR_FREQ_LFO1, 40);
	lxr_host_setParam(PAR_WAVE_LFO1, 2);
	lxr_host_setParam(PAR_AMOUNT_LFO1, 127);
	lxr_host_setParam(PAR_RETRIGGER_LFO1, 7);
	lxr_host_setParam(PAR_TARGET_LFO1, 229);
	Lfo* lfo = &voiceArray[0].lfo;
	if(!lfo->once || lfo->retrigger != 1) {
		printf("FAIL RETRIG 7 is once on voice 1: %u %u\n", lfo->once, lfo->retrigger);
		failed = 1;
	}
	lfo_retrigger(0);
	float low = 2, last = 0;
	int blocks = 0;
	for(; blocks < 100000 && !lfo->ended; blocks++) {
		lfo_dispatchNextValue(lfo);
		if(fxBus.dryWet < low) low = fxBus.dryWet;
	}
	lfo_dispatchNextValue(lfo);
	last = fxBus.dryWet;
	if(low > 0.15f || fabs(last - 0.5f) > 0.02f || !lfo->ended) {
		printf("FAIL once on FX mix: low %.3f, end %.3f, ended %u after %d\n", low, last, lfo->ended, blocks);
		failed = 1;
	}
	lfo_retrigger(0);
	if(lfo->ended) {
		puts("FAIL a hit starts it again");
		failed = 1;
	}
	lxr_host_setParam(PAR_RETRIGGER_LFO1, 1);
	if(lfo->once) {
		puts("FAIL RETRIG 1 loops");
		failed = 1;
	}
	puts(failed ? "FAILED" : "lfo passed");
	return failed;
}
