/*
 * lxr_params.c
 *
 * The parameter list is read from the front panel's menu tables: menuPages (which parameter is shown where and
 * under which name), valueNames and parameter_dtypes (copied out of menu.c at build time) and modTargets.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "lxr_params.h"
#include <stdio.h>
#include <string.h>
#include "Parameters.h"
#include "Soundengine/FxBus.h"
#include "menu/menu.h"
#include "menu/MenuText.h"
#include "menu/menuPages.h"
#include "menu/CcNr2Text.h"
#include "menu_tables.inc"

static const char* const pageNames[] = { "Drum 1", "Drum 2", "Drum 3", "Snare", "Cymbal", "Hi-hat", "Hi-hat" };
// FX sub-page n shows the parameters of FX type n
static const char* const fxTypeNames[] = { "", "Distortion", "Ring Mod", "Compressor", "Delay" };
static const char* const audioOutNames[] = { "Out 1", "Out 2", "Out 1 L", "Out 1 R", "Out 2 L", "Out 2 R", "FX" };

// the LCD has room for 8 characters; spell the abbreviations out
static const char* const spellings[][2] =
{
	{"Oscilltr", "Oscillator"}, {"Transnt", "Transient"}, {"Nois/Osc", "Noise/Osc"}, {"PitchMod", "Pitch Mod"},
	{"Veloc EG", "Velocity EG"}, {"Fx", "FX"}, {"ClockSnc", "Clock Sync"}, {"Dcy Clsd", "Decay Closed"},
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
	case MENU_AUDIO_OUT:	return outputNames[0][0];
	case MENU_AUDIO_OUT_FX:	return outputNamesFx[0][0];
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
	case MENU_AUDIO_OUT:
	case MENU_AUDIO_OUT_FX:	return audioOutNames[value];
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
	// the LFO menu offers every target except index 1, the global sample rate
	case DTYPE_TARGET_SELECTION_LFO:	info->max = getNumModTargets() - 2;	info->isEnum = true;	break;
	default:			info->max = 127;	break;
	}
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

	for(int pg = 0; pg < NUM_PAGES; pg++)
		for(int sp = 0; sp < NUM_SUB_PAGES; sp++)
		{
			// the FX page has a sub-page per FX type; types past the menu's list (reverb) are compiled out
			if(pg == FX_MODE_PAGE && sp >= fxNames[0][0]) continue;

			const Page* page = &menuPages[pg][sp];
			const uint8_t* tops = &page->top1;
			const uint16_t* bots = &page->bot1;
			for(int i = 0; i < 8; i++)
			{
				const uint16_t p = bots[i];
				if(p == PAR_NONE || p >= END_OF_SOUND_PARAMETERS) continue;
				// the LFO's destination voice only narrows the front panel's target list; the destination
				// parameter covers every target
				if((parameter_dtypes[p] & 0xff) == DTYPE_VOICE_LFO) continue;

				char name[32];
				longNameOf(tops[i], name, sizeof name);
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
uint8_t lxr_params_toFirmware(uint16_t param, uint16_t value)
{
	const int i = lxr_params_indexOf(param);
	if(i < 0) return 0;
	if(value > infos[i].max) value = infos[i].max;
	if(value < infos[i].min) value = infos[i].min;

	if(isVeloDest(param))
		return value ? modTargetVoiceOffsets[param - PAR_VEL_DEST_1].start + value - 1 : 0;
	if(isLfoDest(param))
		return value ? value + 1 : 0;
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
	if(isLfoDest(param))
		return value >= 2 && value < getNumModTargets() ? value - 1 : 0;
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
