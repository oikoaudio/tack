/*
 * lfo_test.c
 *
 * That an LFO in sync follows the tempo a host gives it, that moving between FREQ and SYNC finds the same speed both ways, and that a
 * random LFO started by a hit holds a new value until the next.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include <stdio.h>
#include <math.h>
#include "lxr_host.h"
#include "lfo_speed.h"
#include "lxr_params.h"
#include "Soundengine/DSPAudio/DrumVoice.h"
#include "Soundengine/FxBus.h"
#include "Soundengine/DSPAudio/TiltEq.h"

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
	// once on a sine, whose table starts and ends at its bottom: it leaves the value set, peaks, and comes back with no jump
	lxr_host_setParam(PAR_WAVE_LFO1, LFO_SINE);
	lxr_host_setParam(PAR_RETRIGGER_LFO1, 7);
	lfo_retrigger(0);
	lfo_dispatchNextValue(lfo);
	const float first = fxBus.dryWet;
	float high = first;
	for(blocks = 0; blocks < 100000 && !lfo->ended; blocks++) {
		lfo_dispatchNextValue(lfo);
		if(fxBus.dryWet > high) high = fxBus.dryWet;
	}
	lfo_dispatchNextValue(lfo);
	if(fabs(first - 0.5f) > 0.02f || high < 0.85f || fabs(fxBus.dryWet - 0.5f) > 0.02f) {
		printf("FAIL once on a sine: starts %.3f, peaks %.3f, ends %.3f\n", first, high, fxBus.dryWet);
		failed = 1;
	}
	// an LFO aimed at RETRIG: the voice follows, and ONCE stays as set
	lfo_modRetrigger(lfo, 3 / 127.f);
	if(lfo->retrigger != 3 || !lfo->once) {
		printf("FAIL a modulated RETRIG: voice %u, once %u\n", lfo->retrigger, lfo->once);
		failed = 1;
	}
	lfo_modRetrigger(lfo, 11 / 127.f);
	if(lfo->retrigger != 5) {
		printf("FAIL a modulated RETRIG of 11 is voice 5: %u\n", lfo->retrigger);
		failed = 1;
	}
	// random, started by its own voice at FREQ 0: each hit takes a new value at once, and it holds until the next hit
	lxr_host_setParam(PAR_WAVE_LFO1, LFO_NOISE);
	lxr_host_setParam(PAR_FREQ_LFO1, 0);
	lxr_host_setParam(PAR_RETRIGGER_LFO1, 1);
	lxr_host_setParam(PAR_AMOUNT_LFO1, 64);
	int differ = 0, moved = 0;
	float before = -1;
	for(int hit = 0; hit < 8; hit++) {
		lfo_retrigger(0);
		const float held = fxBus.dryWet;
		differ += held != before;
		before = held;
		for(blocks = 0; blocks < 2000; blocks++) {	/* about 3 s */
			lfo_dispatchNextValue(lfo);
			moved += fxBus.dryWet != held;
		}
	}
	if(differ < 7 || moved) {
		printf("FAIL random per hit: %d of 8 hits took a new value, %d blocks moved between hits\n", differ, moved);
		failed = 1;
	}
	// a change of mode or tempo during the delay's crossfade keeps the time still waiting for it
	Delay* d = &fxBus.delay;
	d->interpValue = 0;
	d->timePending = 0;
	Delay_setDelayTime(d, 0.1f);
	Delay_setDelayTime(d, 0.3f);
	Delay_setMode(d, 2);
	Delay_tempoChanged(d);
	Delay_setMode(d, 0);
	if(!d->timePending || fabs(d->pendingDelayTime - 0.3f) > 0.001f) {
		printf("FAIL the delay lost a time: pending %u, %.3f\n", d->timePending, d->pendingDelayTime);
		failed = 1;
	}
	// the reverb's tilt at its middle leaves nothing behind, whatever it was holding
	TiltEq eq;
	TiltEq_init(&eq);
	TiltEq_setParameter(&eq, -0.5f);
	for(int i = 0; i < 1000; i++) TiltEq_process(&eq, 0.5f);
	TiltEq_setParameter(&eq, 0.f);
	float rest = 1;
	for(int i = 0; i < 44100; i++) rest = TiltEq_process(&eq, 0.f);
	if(fabs(rest) > 0.001f) {
		printf("FAIL the tilt held a level: %f\n", rest);
		failed = 1;
	}
	// the editor shows a kit's reverb as a reverb, and all four of its dials
	if(lxr_params_fromFirmware(PAR_FX_TYPE, 5) != 5 || !lxr_params_groupName(LXR_PARAMS_FX_VOICE, 5)
			|| !lxr_params_fxLabel(PAR_FX_P1, 5) || !lxr_params_fxLabel(PAR_FX_P2, 5) || !lxr_params_fxLabel(PAR_FX_P3, 5) || !lxr_params_fxLabel(PAR_FX_P4, 5)) {
		puts("FAIL the editor has no reverb");
		failed = 1;
	}
	// the reverb's cuts: three octaves out is far down, three octaves in is left alone
	static const struct { float hz; uint8_t high; float low, high_gain; } CUT[2] = {{1000.f, 0, 125.f, 8000.f}, {1000.f, 1, 8000.f, 125.f}};
	for(int c = 0; c < 2; c++) {
		float gain[2];
		for(int side = 0; side < 2; side++) {
			ReverbCut cut = {0};
			ReverbCut_setHz(&cut, CUT[c].hz, CUT[c].high);
			float peak = 0, phase = 0;
			const float step = 6.2831853f * (side ? CUT[c].high_gain : CUT[c].low) / 44140.f;
			for(int i = 0; i < 88200; i++) {
				const float out = ReverbCut_process(&cut, sinf(phase));
				phase += step;
				if(phase > 6.2831853f) phase -= 6.2831853f;
				if(i > 44100 && fabsf(out) > peak) peak = fabsf(out);
			}
			gain[side] = peak;
		}
		if(gain[0] > 0.001f || gain[1] < 0.95f || gain[1] > 1.05f) {
			printf("FAIL reverb cut %d: %.5f out of band, %.3f in it\n", c, gain[0], gain[1]);
			failed = 1;
		}
	}
	puts(failed ? "FAILED" : "lfo passed");
	return failed;
}
