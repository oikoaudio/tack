/* SPDX-License-Identifier: GPL-3.0-only */
/* lock_test's view into the engine, compiled with its headers: the filter a voice is using right now. */
#include "Soundengine/DSPAudio/DrumVoice.h"
#include "Parameters.h"
#include "Soundengine/FxBus.h"
float probe_filter(int voice) { return voiceArray[voice].filter.f; }
int probe_param_filter1(void) { return PAR_FILTER_FREQ_1; }
int probe_param_decay1(void) { return PAR_VELOD1; }

int probe_filter_type(int voice) { return voiceArray[voice].filterType; }
int probe_param_filter_type1(void) { return PAR_FILTER_TYPE_1; }
int probe_fx_type(void) { return fxBus.fxType; }
int probe_param_vol1(void) { return PAR_VOL1; }
int probe_kit_size(void) { return END_OF_SOUND_PARAMETERS; }
/* the FX bus's settings as one number: a setting that reaches it moves this, whatever a short hit lets you hear */
float probe_fx_settings(void)
{
    return fxBus.parameter[0] + 3.f * fxBus.parameter[1] + 7.f * fxBus.parameter[2] + 13.f * fxBus.parameter[3] +
           100.f * fxBus.distortion.distortionType + 200.f * fxBus.delay.delayType;
}
