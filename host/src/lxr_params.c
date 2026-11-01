/*
 * lxr_params.c
 *
 * The parameter list is read from the front panel's menu tables: menuPages (which parameter is shown where and
 * under which name), valueNames and parameter_dtypes (copied out of menu.c at build time) and modTargets.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "lxr_params.h"
#include "lxr_host.h"
#include "lxr_voices.h"
_Static_assert(LXR_HOST_NUM_VOICES == LXR_VOICES && LXR_HOST_NUM_VOICE_OUTS == LXR_VOICES_WITH_PARAMS && LXR_PARAMS_NUM_VOICES == LXR_VOICES_WITH_PARAMS &&
               LXR_HOST_BASE_NOTE == LXR_NOTE_CENTRE, "one count of voices");
#include <stdio.h>
#include <string.h>
#include "Parameters.h"
#include "Soundengine/FxBus.h"
#include "Soundengine/DSPAudio/mixer.h"
#include "menu/menu.h"
#include "menu/MenuText.h"
#include "menu/menuPages.h"
#include "menu/CcNr2Text.h"
#include "menu_tables.inc"

static const char* const pageNames[] = { "Drum 1", "Drum 2", "Drum 3", "Snare", "Cymbal", "Hi-hat", "Hi-hat" };
// FX sub-page n shows the parameters of FX type n
static const char* const fxTypeNames[] = { "", "Distortion", "Ring Mod", "Compressor", "Delay", "Reverb" };
// see adjustValue() in lxr_host.c: a voice plays directly or through the FX bus
static const char* const audioOutNames[] = { "Direct", "FX" };

// the LCD has room for 8 characters; spell the abbreviations out. "Veloc EG" is the voice's amplitude envelope,
// unrelated to velocity, which has its own parameters: Tack calls it Amp EG.
static const char* const spellings[][2] =
{
	{"Oscilltr", "Oscillator"}, {"Transnt", "Transient"}, {"Nois/Osc", "Noise/Osc"}, {"PitchMod", "Pitch Mod"},
	{"Veloc EG", "Amp EG"}, {"Fx", "FX"}, {"ClockSnc", "Clock Sync"}, {"Dcy Clsd", "Decay Closed"},
	{"Dcy Open", "Decay Open"}, {"DstParam", "Destination"}, {"DstVoice", "Dest Voice"}, {"Freqcy 1", "Frequency 1"},
	{"Freqcy 2", "Frequency 2"}, {"Frequncy", "Frequency"}, {"Overdriv", "Overdrive"}, {"PreDelay", "Pre-delay"},
	{"PulsWdth", "Pulse Width"}, {"Reflxion", "Reflection"}, {"RepeatCt", "Repeat Count"}, {"Resnance", "Resonance"},
	{"Retriggr", "Retrigger"}, {"SampleRt", "Sample Rate"}, {"Threshld", "Threshold"}, {"Vol mod", "Volume Mod"},
	{"AudioOut", "Audio Out"},
};

typedef struct
{
	uint16_t dtype;		// parameter_dtypes entry: type in the low byte, menu id in the high byte
	uint8_t page, subPage;
} Extra;

static LxrParamInfo infos[END_OF_SOUND_PARAMETERS];
static Extra extras[END_OF_SOUND_PARAMETERS];
static char groupNames[LXR_PARAMS_NUM_VOICES + 1][LXR_PARAMS_NUM_GROUPS][24];
// labels of the FX page parameters per FX sub-page
static char fxLabels[END_OF_SOUND_PARAMETERS][5][24];
static int16_t indexOfParam[END_OF_SOUND_PARAMETERS];
static int count = -1;

//-----------------------------------------------------------
static const char* spell(const char* s, char* buf, size_t size)
{
	// the tables are fixed-width; drop trailing padding
	snprintf(buf, size, "%s", s);
	for(size_t n = strlen(buf); n > 0 && buf[n - 1] == ' '; n--) buf[n - 1] = 0;
	for(size_t i = 0; i < sizeof spellings / sizeof spellings[0]; i++)
		if(!strcmp(buf, spellings[i][0])) {
			snprintf(buf, size, "%s", spellings[i][1]);
			break;
		}
	return buf;
}

static const char* categoryOf(uint8_t nameIdx, char* buf, size_t size)
{
	return spell(catNames[valueNames[nameIdx].category], buf, size);
}

static const char* longNameOf(uint8_t nameIdx, char* buf, size_t size)
{
	return spell(longNames[valueNames[nameIdx].longName], buf, size);
}

static uint8_t menuEntries(uint8_t menuId)
{
	// as getMaxEntriesForMenu() in menu.c, for the menus sound parameters use; no user samples on the host
	switch(menuId)
	{
	case MENU_TRANS:		return transientNames[0][0];
	case MENU_AUDIO_OUT:	return 2;
	case MENU_FILTER:		return filterTypes[0][0];
	case MENU_SYNC_RATES:	return syncRateNames[0][0];
	case MENU_LFO_WAVES:	return lfoWaveNames[0][0];
	case MENU_RETRIGGER:	return retriggerNames[0][0];
	case MENU_WAVEFORM:		return waveformNames[0][0];
	case MENU_MIX_FM:		return mixFmNames[0][0];
	case MENU_FX:			return fxNames[0][0];
	case MENU_DIST:			return distNames[0][0];
	case MENU_RATIO:		return ratioNames[0][0];
	case MENU_DELAY:		return delayNames[0][0];
	case MENU_REVERB:		return reverbNames[0][0];
	default:				return 1;
	}
}

static const char* menuEntryName(uint8_t menuId, uint8_t value)
{
	if(value >= menuEntries(menuId)) return "?";
	switch(menuId)
	{
	case MENU_TRANS:		return transientNames[value + 1];
	case MENU_AUDIO_OUT:	return audioOutNames[value];
	case MENU_FILTER:		return filterTypes[value + 1];
	case MENU_SYNC_RATES:	return syncRateNames[value + 1];
	case MENU_LFO_WAVES:	return lfoWaveNames[value + 1];
	case MENU_RETRIGGER:	return retriggerNames[value + 1];
	case MENU_WAVEFORM:		return waveformNames[value + 1];
	case MENU_MIX_FM:		return mixFmNames[value + 1];
	case MENU_FX:			return fxNames[value + 1];
	case MENU_DIST:			return distNames[value + 1];
	case MENU_RATIO:		return ratioNames[value + 1];
	case MENU_DELAY:		return delayNames[value + 1];
	case MENU_REVERB:		return reverbNames[value + 1];
	default:				return "?";
	}
}
//-----------------------------------------------------------
static bool isVeloDest(uint16_t p) { return (parameter_dtypes[p] & 0xff) == DTYPE_TARGET_SELECTION_VELO; }

// LFO destinations by plugin value: 0 off, then the modTargets indices on offer. The LFO menu offers every target
// except index 1, the global sample rate; the table lists the FX parameters once per voice, so keep the first of
// each parameter.
static uint8_t lfoTargets[256];
static int numLfoTargets;

static void buildLfoTargets(void)
{
	numLfoTargets = 1;
	lfoTargets[0] = 0;
	for(int t = 2; t < getNumModTargets() && numLfoTargets < 256; t++) {
		if(modTargets[t].param == PAR_NONE) continue;
		bool listed = false;
		for(int j = 1; j < numLfoTargets && !listed; j++) listed = modTargets[lfoTargets[j]].param == modTargets[t].param;
		if(!listed) lfoTargets[numLfoTargets++] = t;
	}
}
static bool isMenu(uint16_t p, uint8_t menuId)
{
	return (parameter_dtypes[p] & 0xff) == DTYPE_MENU && (parameter_dtypes[p] >> 8) == menuId;
}
static bool isLfoDest(uint16_t p) { return (parameter_dtypes[p] & 0xff) == DTYPE_TARGET_SELECTION_LFO; }

static void setRange(LxrParamInfo* info, Extra* x)
{
	const uint16_t p = info->param;
	info->min = 0;
	switch(x->dtype & 0xff)
	{
	case DTYPE_0B255:	info->max = 255;	break;
	case DTYPE_PM100:
	case DTYPE_0B100:	info->max = 100;	break;
	case DTYPE_1B16:	info->min = 1; info->max = 16;	break;
	case DTYPE_1B64:	info->min = 1; info->max = 64;	break;
	case DTYPE_0B15:	info->max = 15;		break;
	case DTYPE_ON_OFF:
	case DTYPE_0b1:		info->max = 1;		info->isEnum = true;	break;
	case DTYPE_MENU:	info->max = menuEntries(x->dtype >> 8) - 1;	info->isEnum = true;	break;
	case DTYPE_TARGET_SELECTION_VELO: {
		const ModTargetVoiceOffset* o = &modTargetVoiceOffsets[p - PAR_VEL_DEST_1];
		info->max = o->end - o->start + 1;
		info->isEnum = true;
		break;
	}
	case DTYPE_TARGET_SELECTION_LFO:	info->max = numLfoTargets - 1;	info->isEnum = true;	break;
	default:			info->max = 127;	break;
	}
	// see adjustValue() in lxr_host.c: PWM is silent at pulse width 0
	info->min = (uint16_t)lxr_param_min(p) > info->min ? (uint16_t)lxr_param_min(p) : info->min;
}

static void buildName(LxrParamInfo* info, const Extra* x, uint8_t nameIdx, const char* fxNamesJoined)
{
	char cat[32], name[32];
	categoryOf(nameIdx, cat, sizeof cat);
	longNameOf(nameIdx, name, sizeof name);

	if(x->page == FX_MODE_PAGE) {
		if(fxNamesJoined) {
			snprintf(info->name, sizeof info->name, "FX %s", fxNamesJoined);
			snprintf(info->module, sizeof info->module, "FX");
		} else if(x->subPage == 0) {
			snprintf(info->name, sizeof info->name, "FX %s", name);
			snprintf(info->module, sizeof info->module, "FX");
		} else {
			snprintf(info->name, sizeof info->name, "FX %s %s", fxTypeNames[x->subPage], name);
			snprintf(info->module, sizeof info->module, "FX/%s", fxTypeNames[x->subPage]);
		}
		return;
	}

	const char* voice = x->page < sizeof pageNames / sizeof pageNames[0] ? pageNames[x->page] : "Master";
	if(!strcmp(cat, "Voice"))
		snprintf(info->name, sizeof info->name, "%s %s", voice, name);
	else
		snprintf(info->name, sizeof info->name, "%s %s %s", voice, cat, name);
	snprintf(info->module, sizeof info->module, "%s/%s", voice, cat);
}

static void build(void)
{
	static uint8_t nameIdxOf[END_OF_SOUND_PARAMETERS];
	static char fxJoined[END_OF_SOUND_PARAMETERS][64];
	static uint8_t fxSubPages[END_OF_SOUND_PARAMETERS];

	count = 0;
	memset(indexOfParam, -1, sizeof indexOfParam);
	buildLfoTargets();

	for(int pg = 0; pg < NUM_PAGES; pg++)
		for(int sp = 0; sp < NUM_SUB_PAGES; sp++)
		{
			// the FX page has a sub-page per FX type
			if(pg == FX_MODE_PAGE && sp >= fxNames[0][0]) continue;

			const Page* page = &menuPages[pg][sp];
			const uint8_t* tops = &page->top1;
			const uint16_t* bots = &page->bot1;
			for(int i = 0; i < 8; i++)
			{
				const uint16_t p = bots[i];
				if(p == PAR_NONE || p >= END_OF_SOUND_PARAMETERS) continue;
				// the reverb has one type, so there is nothing to choose
				if(p == PAR_FX_REVERB_TYPE) continue;
				// the LFO's destination voice only narrows the front panel's target list; the destination
				// parameter covers every target
				if((parameter_dtypes[p] & 0xff) == DTYPE_VOICE_LFO) continue;
				// the FX bus always returns to the main output (adjustValue() in lxr_host.c)
				if(isMenu(p, MENU_AUDIO_OUT_FX)) continue;

				char name[32];
				longNameOf(tops[i], name, sizeof name);
				if(pg == FX_MODE_PAGE) {
					snprintf(fxLabels[p][sp], sizeof fxLabels[p][sp], "%s", name);
					// a page may add no parameter of its own, as the reverb's four are all shared
					if(sp) snprintf(groupNames[LXR_PARAMS_FX_VOICE][sp], sizeof groupNames[0][0], "%s", fxTypeNames[sp]);
				}
				if(indexOfParam[p] >= 0) {
					// the FX parameters 1-4 mean something different for each FX type
					if(pg == FX_MODE_PAGE && !strstr(fxJoined[p], name)) {
						const size_t n = strlen(fxJoined[p]);
						snprintf(fxJoined[p] + n, sizeof fxJoined[p] - n, "%s%s", n ? "/" : "", name);
						fxSubPages[p]++;
					}
					continue;
				}

				indexOfParam[p] = count;
				LxrParamInfo* info = &infos[count];
				Extra* x = &extras[count];
				memset(info, 0, sizeof *info);
				info->param = p;
				x->dtype = parameter_dtypes[p];
				x->page = pg;
				x->subPage = sp;
				nameIdxOf[count] = tops[i];

				// where it goes in an editor: tabs per voice, a box per front panel sub-page
				char cat[32];
				categoryOf(tops[i], cat, sizeof cat);
				if(pg < (int)(sizeof pageNames / sizeof pageNames[0])) {
					info->voice = pg < LXR_PARAMS_NUM_VOICES ? pg : LXR_PARAMS_NUM_VOICES - 1;
					info->group = sp;
					// the snare has no FM page; its noise, its second sound source, gets that place instead of
					// sharing the oscillator's
					if(pg == 3 && (!strcmp(cat, "Noise") || !strcmp(cat, "Noise/Osc"))) {
						info->group = 3;
						snprintf(groupNames[3][3], sizeof groupNames[0][0], "Noise");
					}
				} else {
					info->voice = LXR_PARAMS_FX_VOICE;
					info->group = pg == FX_MODE_PAGE ? sp : LXR_PARAMS_FX_MASTER_GROUP;
				}
				char* title = groupNames[info->voice][info->group];
				if(!title[0]) {
					if(pg == FX_MODE_PAGE)
						snprintf(title, sizeof groupNames[0][0], "%s", sp ? fxTypeNames[sp] : "FX");
					else if(info->voice == LXR_PARAMS_FX_VOICE)
						snprintf(title, sizeof groupNames[0][0], "Master");
					else {
						char cat[32];
						categoryOf(page->top1, cat, sizeof cat);
						snprintf(title, sizeof groupNames[0][0], "%s", strcmp(cat, "Voice") ? cat : "Mix");
					}
				}
				// some sub-pages mix sections (a drum's modulation page has pitch and velocity modulation, the snare's
				// oscillator page its noise): name those. Other category labels are too generic to help (the pitch
				// EG's slope is filed under the amp EG), so leave them out.
				static const char* const mixedIn[] = { "Velocity", "Pitch Mod", "Noise", "Noise/Osc" };
				bool prefix = false;
				for(size_t m = 0; m < sizeof mixedIn / sizeof mixedIn[0]; m++)
					prefix |= !strcmp(cat, mixedIn[m]) && strcmp(cat, title);
				if(prefix)
					snprintf(info->label, sizeof info->label, "%s %s", cat, name);
				else
					snprintf(info->label, sizeof info->label, "%s", name);
				if(pg == FX_MODE_PAGE) snprintf(fxJoined[p], sizeof fxJoined[p], "%s", name);
				fxSubPages[p] = 1;
				setRange(info, x);
				count++;
			}
		}

	for(int i = 0; i < count; i++)
	{
		const uint16_t p = infos[i].param;
		buildName(&infos[i], &extras[i], nameIdxOf[i], fxSubPages[p] > 1 ? fxJoined[p] : NULL);
		if(fxSubPages[p] > 1) snprintf(infos[i].label, sizeof infos[i].label, "%s", fxJoined[p]);

		// the front panel sometimes reuses a label within a voice (the pitch EG slope is labelled like the amp
		// EG slope). Take the category from the first entry on its sub-page instead.
		for(int j = 0; j < i; j++)
			if(!strcmp(infos[j].name, infos[i].name)) {
				const Page* page = &menuPages[extras[i].page][extras[i].subPage];
				char cat[32], name[32];
				categoryOf(page->top1, cat, sizeof cat);
				longNameOf(nameIdxOf[i], name, sizeof name);
				snprintf(infos[i].name, sizeof infos[i].name, "%s %s %s", pageNames[extras[i].page], cat, name);
				snprintf(infos[i].module, sizeof infos[i].module, "%s/%s", pageNames[extras[i].page], cat);
				break;
			}
	}

	// two of a kind in one group (the cymbal's and hi-hat's mod oscillator waveforms): number their labels
	for(int i = 0; i < count; i++)
	{
		int n = 1;
		for(int j = i + 1; j < count; j++)
			if(infos[j].voice == infos[i].voice && infos[j].group == infos[i].group && !strcmp(infos[j].label, infos[i].label)) {
				const size_t len = strlen(infos[j].label);
				snprintf(infos[j].label + len, sizeof infos[j].label - len, " %d", ++n);
			}
		if(n > 1) {
			const size_t len = strlen(infos[i].label);
			snprintf(infos[i].label + len, sizeof infos[i].label - len, " 1");
		}
	}

	// still the same name: two of a kind on one sub-page (the cymbal's and hi-hat's mod oscillator waveforms), so
	// number them
	for(int i = 0; i < count; i++)
	{
		int n = 1;
		for(int j = i + 1; j < count; j++)
			if(!strcmp(infos[j].name, infos[i].name)) {
				const size_t len = strlen(infos[j].name);
				snprintf(infos[j].name + len, sizeof infos[j].name - len, " %d", ++n);
			}
		if(n > 1) {
			const size_t len = strlen(infos[i].name);
			snprintf(infos[i].name + len, sizeof infos[i].name - len, " 1");
		}
	}
}
//-----------------------------------------------------------
int lxr_params_count(void)
{
	if(count < 0) build();
	return count;
}

const LxrParamInfo* lxr_params_info(int index)
{
	return index >= 0 && index < lxr_params_count() ? &infos[index] : NULL;
}

int lxr_params_indexOf(uint16_t param)
{
	lxr_params_count();
	return param < END_OF_SOUND_PARAMETERS ? indexOfParam[param] : -1;
}
//-----------------------------------------------------------
const char* lxr_params_groupName(uint8_t voice, uint8_t group)
{
	lxr_params_count();
	if(voice > LXR_PARAMS_FX_VOICE || group >= LXR_PARAMS_NUM_GROUPS) return NULL;
	return groupNames[voice][group][0] ? groupNames[voice][group] : NULL;
}

const char* lxr_params_fxLabel(uint16_t param, uint8_t fxType)
{
	lxr_params_count();
	if(param >= END_OF_SOUND_PARAMETERS || fxType < 1 || fxType > 5) return NULL;
	return fxLabels[param][fxType][0] ? fxLabels[param][fxType] : NULL;
}
uint16_t lxr_params_default(uint16_t param)
{
	static uint8_t kit[END_OF_SOUND_PARAMETERS];
	static bool haveKit;
	if(!haveKit) {
		lxr_host_initKit(kit);
		haveKit = true;
	}
	return param < END_OF_SOUND_PARAMETERS ? lxr_params_fromFirmware(param, kit[param]) : 0;
}

uint16_t lxr_params_fxTypeParam(void)
{
	return PAR_FX_TYPE;
}
//-----------------------------------------------------------
uint8_t lxr_params_toFirmware(uint16_t param, uint16_t value)
{
	const int i = lxr_params_indexOf(param);
	if(i < 0) return 0;
	if(value > infos[i].max) value = infos[i].max;
	if(value < infos[i].min) value = infos[i].min;

	if(isVeloDest(param))
		return value ? modTargetVoiceOffsets[param - PAR_VEL_DEST_1].start + value - 1 : 0;
	if(isLfoDest(param))
		return value < numLfoTargets ? lfoTargets[value] : 0;
	if(isMenu(param, MENU_AUDIO_OUT))
		return value ? MIXER_ROUTING_FX_BUS : MIXER_ROUTING_DAC1_STEREO;
	return value;
}

uint16_t lxr_params_fromFirmware(uint16_t param, uint8_t value)
{
	const int i = lxr_params_indexOf(param);
	if(i < 0) return 0;

	if(isVeloDest(param)) {
		const ModTargetVoiceOffset* o = &modTargetVoiceOffsets[param - PAR_VEL_DEST_1];
		return value >= o->start && value <= o->end ? value - o->start + 1 : 0;
	}
	if(isLfoDest(param)) {
		if(value < 2 || value >= getNumModTargets() || modTargets[value].param == PAR_NONE) return 0;
		for(int j = 1; j < numLfoTargets; j++)
			if(modTargets[lfoTargets[j]].param == modTargets[value].param) return j;
		return 0;
	}
	if(isMenu(param, MENU_AUDIO_OUT))
		return value == MIXER_ROUTING_FX_BUS;
	if(value > infos[i].max) return infos[i].max;
	if(value < infos[i].min) return infos[i].min;
	return value;
}
//-----------------------------------------------------------
static void targetText(uint8_t modTargetIdx, char* buf, size_t size)
{
	if(modTargetIdx == 0 || modTargets[modTargetIdx].param == PAR_NONE) {
		snprintf(buf, size, "off");
		return;
	}
	// the front panel's target names reuse labels (the FM waveform reads as the oscillator's); the parameter's own
	// name tells them apart
	const int i = lxr_params_indexOf(modTargets[modTargetIdx].param);
	if(i >= 0) {
		snprintf(buf, size, "%s", infos[i].name);
		return;
	}
	const uint8_t nameIdx = modTargets[modTargetIdx].nameIdx;
	char cat[32], name[32];
	categoryOf(nameIdx, cat, sizeof cat);
	longNameOf(nameIdx, name, sizeof name);

	const uint8_t voice = voiceFromModTargValue(modTargetIdx);
	if(!strcmp(cat, "FX") || voice == 0)
		snprintf(buf, size, "%s %s", cat, name);
	else if(!strcmp(cat, "Voice"))
		snprintf(buf, size, "%s %s", pageNames[voice - 1], name);
	else
		snprintf(buf, size, "%s %s %s", pageNames[voice - 1], cat, name);
}

bool lxr_params_isTarget(uint16_t param)
{
	return param < END_OF_SOUND_PARAMETERS && (isVeloDest(param) || isLfoDest(param));
}

int lxr_params_targetVoice(uint16_t param, uint16_t value)
{
	if(!lxr_params_isTarget(param)) return -1;
	const uint8_t t = lxr_params_toFirmware(param, value);
	if(t == 0 || modTargets[t].param == PAR_NONE) return -1;
	const int i = lxr_params_indexOf(modTargets[t].param);
	return i >= 0 && infos[i].voice < LXR_PARAMS_NUM_VOICES ? infos[i].voice : -1;
}

// the parameter with the same place on another voice: same group, same label
static int counterpart(uint16_t param, int voice)
{
	const int i = lxr_params_indexOf(param);
	if(i < 0) return -1;
	if(infos[i].voice == voice) return param;
	for(int j = 0; j < count; j++)
		if(infos[j].voice == voice && infos[j].group == infos[i].group && !strcmp(infos[j].label, infos[i].label))
			return infos[j].param;
	return -1;
}

// a modulation target (modTargets index) of one voice's destination, moved to another voice's destination
static uint8_t moveTarget(uint8_t target, int fromVoice, uint16_t toDest, int toVoice)
{
	if(target == 0 || target >= getNumModTargets() || modTargets[target].param == PAR_NONE) return 0;
	const int i = lxr_params_indexOf(modTargets[target].param);
	if(i < 0 || infos[i].voice != fromVoice) return 0;
	const int param = counterpart(modTargets[target].param, toVoice);
	if(param < 0) return 0;
	int first = 2, last = getNumModTargets() - 1;
	if(isVeloDest(toDest)) {
		first = modTargetVoiceOffsets[toDest - PAR_VEL_DEST_1].start;
		last = modTargetVoiceOffsets[toDest - PAR_VEL_DEST_1].end;
	}
	for(int t = first; t <= last; t++)
		if(modTargets[t].param == param) return t;
	return 0;
}

void lxr_params_copyVoice(const uint8_t* fromKit, int fromVoice, uint8_t* toKit, int toVoice)
{
	lxr_params_count();
	for(int i = 0; i < count; i++) {
		if(infos[i].voice != fromVoice) continue;
		const uint16_t from = infos[i].param;
		const int to = counterpart(from, toVoice);
		if(to < 0) continue;
		uint8_t value = fromKit[from];
		if(lxr_params_isTarget(from))
			value = moveTarget(value, fromVoice, to, toVoice);
		// an LFO retriggered by its own voice (v1-v6) stays retriggered by its voice
		else if(isMenu(from, MENU_RETRIGGER) && value == fromVoice + 1)
			value = toVoice + 1;
		else if(isMenu(from, MENU_RETRIGGER) && value == fromVoice + 7)
			value = toVoice + 7;
		toKit[to] = value;
	}
}

bool lxr_params_voiceIsInit(const uint8_t* kit, int voice)
{
	uint8_t init[END_OF_SOUND_PARAMETERS];
	lxr_host_initKit(init);
	lxr_params_count();
	for(int i = 0; i < count; i++) {
		const uint16_t p = infos[i].param;
		if(infos[i].voice == voice && !isMenu(p, MENU_AUDIO_OUT) && kit[p] != init[p]) return false;
	}
	return true;
}

void lxr_params_valueText(uint16_t param, uint16_t value, char* buf, size_t size)
{
	const int i = lxr_params_indexOf(param);
	if(i < 0) {
		snprintf(buf, size, "%u", value);
		return;
	}

	switch(extras[i].dtype & 0xff)
	{
	case DTYPE_MENU:
		snprintf(buf, size, "%s", menuEntryName(extras[i].dtype >> 8, value));
		break;
	case DTYPE_ON_OFF:
		snprintf(buf, size, "%s", value ? "on" : "off");
		break;
	case DTYPE_0b1:
		snprintf(buf, size, "%u", value + 1);
		break;
	case DTYPE_TARGET_SELECTION_VELO:
	case DTYPE_TARGET_SELECTION_LFO:
		targetText(lxr_params_toFirmware(param, value), buf, size);
		break;
	case DTYPE_PM63:
		// as menu.c: tuning parameters are centred on 60, everything else on 63
		switch(param)
		{
		case PAR_COARSE1: case PAR_COARSE2: case PAR_COARSE3: case PAR_COARSE4: case PAR_COARSE5: case PAR_COARSE6:
		case PAR_FM_FREQ1: case PAR_FM_FREQ2: case PAR_FM_FREQ3:
		case PAR_MOD_OSC_F1_CYM: case PAR_MOD_OSC_F2_CYM: case PAR_MOD_OSC_F1: case PAR_MOD_OSC_F2:
			snprintf(buf, size, "%+d", (int)value - 60);
			break;
		default:
			snprintf(buf, size, "%+d", (int)value - 63);
			break;
		}
		break;
	default:
		snprintf(buf, size, "%u", value);
		break;
	}
}

void lxr_params_mutateTable(uint8_t* top, uint8_t* voice)
{
	for(int i = 0; i < lxr_params_count(); i++) {
		const LxrParamInfo* p = lxr_params_info(i);
		if(p->voice < LXR_PARAMS_FX_VOICE && !lxr_params_isTarget(p->param) && p->max <= 255) {
			top[p->param] = (uint8_t)p->max;
			voice[p->param] = p->voice;
		}
	}
}

int lxr_params_lfoSyncOf(uint16_t param)
{
	return param >= PAR_FREQ_LFO1 && param <= PAR_FREQ_LFO6 ? PAR_SYNC_LFO1 + (param - PAR_FREQ_LFO1) : -1;
}

int lxr_params_lfoFreqOf(uint16_t param)
{
	return param >= PAR_SYNC_LFO1 && param <= PAR_SYNC_LFO6 ? PAR_FREQ_LFO1 + (param - PAR_SYNC_LFO1) : -1;
}
