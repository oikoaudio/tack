/*
 * layout.cpp
 *
 * Where the editor's boxes and controls go: see layout.h for the spec.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "layout.h"
#include <algorithm>
#include <stdio.h>
#include <string.h>

namespace layout {

const char* const tabNames[LXR_PARAMS_FX_VOICE + 1] = { "Drum 1", "Drum 2", "Drum 3", "Snare", "Cymbal", "Hi-hat", "Mix & FX" };
const char* const voiceTitles[LXR_PARAMS_FX_VOICE] = { "DRUM", "DRUM", "DRUM", "SNARE", "CYMBAL", "HI-HAT" };
const LxrParamInfo outputsInfo = { OUTPUTS_ID, "Outputs", "Outputs", 0, 1, true, LXR_PARAMS_FX_VOICE, 0, "Outputs" };

// short labels that fit a cell; anything not listed is short enough already
static const char* const shortLabels[][2] =
{
	{"Waveform", "Wave"}, {"Waveform 1", "Wave 1"}, {"Waveform 2", "Wave 2"}, {"Pulse Width", "PW"},
	{"Repeat Count", "Repeat"}, {"Decay Closed", "Closed"}, {"Decay Open", "Open"},
	{"Pitch Mod Amount", "Sweep"}, {"Velocity Destination", "Vel Dest"}, {"Velocity Amount", "Vel Amt"},
	{"Velocity Volume Mod", "Vel Vol"}, {"Destination", "Dest"}, {"Volume Mod", "Vol Mod"},
	{"Frequency", "Freq"}, {"Frequency 1", "Freq 1"}, {"Frequency 2", "Freq 2"}, {"Noise Frequency", "Noise Frq"},
	{"Noise/Osc Mix", "Mix"}, {"Resonance", "Reso"}, {"Overdrive", "Drive"}, {"Clock Sync", "Sync"},
	{"Retrigger", "Retrig"}, {"Panning", "Pan"}, {"Sample Rate", "Smp Rate"}, {"Audio Out", "Output"},
	{"Threshold", "Thresh"}, {"Reflection", "Reflect"}, {"Feedback", "Fdbk"},
};

const char* shortLabel(const char* label)
{
	for(const auto& s : shortLabels)
		if(!strcmp(label, s[0])) return s[1];
	return label;
}

void shortValue(const char* value, char* out, size_t size)
{
	// an LFO's Retrig: "V1 once" plays once when voice 1 fires
	const size_t n = strlen(value);
	if(n > 5 && !strcmp(value + n - 5, " once")) snprintf(out, size, "%.*s 1x", (int)(n - 5), value);
	else snprintf(out, size, "%s", value);
}

float labelWidth(const char* label, float (*textWidth)(const char* begin, const char* end))
{
	float w = 0;
	for(const char* word = label; *word; ) {
		const char* end = strchr(word, ' ');
		if(!end) end = word + strlen(word);
		w += (w > 0 ? WORD_GAP : 0) + textWidth(word, end);
		word = *end ? end + 1 : end;
	}
	return w;
}

float headerHeight(int pluginVoice) { return pluginVoice < 0 ? TABS_Y + HEADER_ITEM_H + MARGIN : HEADER_ROW_Y + HEADER_ITEM_H + MARGIN; }

Rect tabRect(int t)
{
	const int n = LXR_PARAMS_FX_VOICE + 1;
	const float w = (windowWidth() - 2 * MARGIN - (n - 1) * GAP) / n;
	return { MARGIN + t * (w + GAP), TABS_Y, w, HEADER_ITEM_H };
}

//-----------------------------------------------------------
// what a tab shows: groups of controls, one per box

struct Group { const char* title; const LxrParamInfo* params[8]; const char* labels[8]; int n; };

static void add(Group& gr, const LxrParamInfo* p, const char* label)
{
	if(gr.n < 8) { gr.params[gr.n] = p; gr.labels[gr.n] = label; gr.n++; }
}

// a voice's front panel sub-pages, with the parameters the plugin has
static int voiceGroups(int voice, bool (*has)(void*, uint32_t), void* ctx, Group* groups)
{
	int n = 0;
	for(uint8_t grp = 0; grp < LXR_PARAMS_NUM_GROUPS; grp++) {
		const char* title = lxr_params_groupName(voice, grp);
		if(!title) continue;
		Group& gr = groups[n++];
		gr.title = title;
		gr.n = 0;
		for(int i = 0; i < lxr_params_count(); i++) {
			const LxrParamInfo* p = lxr_params_info(i);
			if(p->voice == voice && p->group == grp && (!has || has(ctx, p->param))) add(gr, p, p->label);
		}
	}
	return n;
}

// a slot is as many cells wide as its sub-page has controls on any voice
static int slotCells(int group)
{
	int cells = 1;
	for(int voice = 0; voice < LXR_PARAMS_NUM_VOICES; voice++) {
		int n = 0;
		for(int i = 0; i < lxr_params_count(); i++) {
			const LxrParamInfo* p = lxr_params_info(i);
			n += p->voice == voice && p->group == group;
		}
		cells = std::max(cells, n);
	}
	return cells;
}

// The FX tab: the FX bus' settings, the selected FX type's controls and master; then a kit mixer, each voice's
// volume and pan, and its output (which is how a voice reaches the FX bus) and drive.
enum { FX_COMMON, FX_TYPE, FX_MASTER, MIX_VOLUME, MIX_PAN, MIX_OUTPUT, MIX_DRIVE, FX_TAB_GROUPS };
static const char* const mixerLabels[] = { "Volume", "Panning", "Audio Out", "Overdrive" };
static const char* const mixerTitles[] = { "Volume", "Pan", "Output", "Drive" };

static void fxGroups(uint8_t fxType, Group* groups)
{
	for(int i = 0; i < FX_TAB_GROUPS; i++) groups[i].n = 0;
	groups[FX_COMMON].title = lxr_params_groupName(LXR_PARAMS_FX_VOICE, 0);
	groups[FX_TYPE].title = fxType >= 1 && fxType <= 5 ? lxr_params_groupName(LXR_PARAMS_FX_VOICE, fxType) : nullptr;
	groups[FX_MASTER].title = lxr_params_groupName(LXR_PARAMS_FX_VOICE, LXR_PARAMS_FX_MASTER_GROUP);
	for(int i = 0; i < lxr_params_count(); i++) {
		const LxrParamInfo* p = lxr_params_info(i);
		if(p->voice != LXR_PARAMS_FX_VOICE) continue;
		if(p->group == LXR_PARAMS_FX_MASTER_GROUP) add(groups[FX_MASTER], p, p->label);
		else if(p->group == 0) add(groups[FX_COMMON], p, p->label);
		else if(groups[FX_TYPE].title) {
			const char* label = lxr_params_fxLabel(p->param, fxType);
			if(label) add(groups[FX_TYPE], p, label);
		}
	}
	for(int m = 0; m < 4; m++) {
		Group& gr = groups[MIX_VOLUME + m];
		gr.title = mixerTitles[m];
		for(int voice = 0; voice < LXR_PARAMS_NUM_VOICES; voice++)
			for(int i = 0; i < lxr_params_count(); i++) {
				const LxrParamInfo* p = lxr_params_info(i);
				if(p->voice == voice && p->group == LXR_PARAMS_NUM_GROUPS - 1 && !strcmp(p->label, mixerLabels[m]))
					add(gr, p, tabNames[voice]);
			}
	}
	add(groups[FX_MASTER], &outputsInfo, outputsInfo.label);
}

// the FX type box is as wide as the widest type, so nothing moves when the type changes
static int fxTypeCells()
{
	int cells = 1;
	Group groups[FX_TAB_GROUPS];
	for(uint8_t fx = 1; fx <= 5; fx++) {
		fxGroups(fx, groups);
		cells = std::max(cells, groups[FX_TYPE].n);
	}
	return cells;
}

// a box in a row; group is null for a slot that stays empty on this tab
struct Slot { const Group* group; int cells; };
constexpr int SLOTS_PER_ROW = 3;

// voice tabs: oscillator, FM (mod osc on cymbal and hi-hat), transient; filter, amp EG, mix; modulation (velocity on
// cymbal and hi-hat), LFO
static const int voiceRows[ROWS][SLOTS_PER_ROW] = { { 0, 3, 4 }, { 5, 1, 7 }, { 2, 6, -1 } };
static const int fxRows[ROWS][SLOTS_PER_ROW] = {
	{ FX_COMMON, FX_TYPE, FX_MASTER }, { MIX_VOLUME, MIX_PAN, -1 }, { MIX_OUTPUT, MIX_DRIVE, -1 }
};

static void tabRows(int tab, uint8_t fxType, bool (*has)(void*, uint32_t), void* ctx, Group* groups,
	Slot rows[ROWS][SLOTS_PER_ROW], int counts[ROWS])
{
	if(tab < LXR_PARAMS_FX_VOICE) {
		const int n = voiceGroups(tab, has, ctx, groups);
		for(int r = 0; r < ROWS; r++) {
			counts[r] = 0;
			for(int i = 0; i < SLOTS_PER_ROW && voiceRows[r][i] >= 0; i++) {
				const int group = voiceRows[r][i];
				const Group* found = nullptr;
				for(int j = 0; j < n; j++)
					if(groups[j].n && groups[j].params[0]->group == group) found = &groups[j];
				rows[r][counts[r]++] = { found, slotCells(group) };
			}
		}
		return;
	}
	fxGroups(fxType, groups);
	for(int r = 0; r < ROWS; r++) {
		counts[r] = 0;
		for(int i = 0; i < SLOTS_PER_ROW && fxRows[r][i] >= 0; i++) {
			const Group& gr = groups[fxRows[r][i]];
			const int cells = fxRows[r][i] == FX_TYPE ? fxTypeCells() : std::max(gr.n, 2);
			rows[r][counts[r]++] = { gr.title && gr.n ? &gr : nullptr, cells };
		}
	}
}

static float boxWidth(int cells) { return cells * CELL_W + 2 * BOX_PAD; }

static float naturalWidth(const Slot* slots, int n)
{
	float w = (n - 1) * GAP;
	for(int i = 0; i < n; i++) w += boxWidth(slots[i].cells);
	return w;
}

constexpr int MAX_GROUPS = FX_TAB_GROUPS > LXR_PARAMS_NUM_GROUPS ? FX_TAB_GROUPS : LXR_PARAMS_NUM_GROUPS;

float windowWidth()
{
	static float width;
	if(width > 0) return width;
	float widest = 0;
	Group groups[MAX_GROUPS];
	Slot rows[ROWS][SLOTS_PER_ROW];
	int counts[ROWS];
	for(int t = 0; t <= LXR_PARAMS_FX_VOICE; t++) {
		tabRows(t, 1, nullptr, nullptr, groups, rows, counts);
		for(int r = 0; r < ROWS; r++) widest = std::max(widest, naturalWidth(rows[r], counts[r]));
	}
	width = widest + 2 * MARGIN;
	return width;
}

float windowHeight(int pluginVoice) { return headerHeight(pluginVoice) + ROWS * BOX_H + (ROWS - 1) * GAP + MARGIN; }

//-----------------------------------------------------------
// cells

static Kind kindOf(const LxrParamInfo* p)
{
	if(p == &outputsInfo) return OUTPUTS_CELL;
	if(lxr_params_lfoFreqOf(p->param) >= 0) return SYNC_CELL;
	if(!p->isEnum) return KNOB_CELL;
	return p->max - p->min == 1 ? SWITCH_CELL : LIST_CELL;
}

static Cell cellAt(const LxrParamInfo* p, const char* label, float x, float y, float w)
{
	Cell c = { p, label, kindOf(p), { x, y, w, CELL_H }, { x, y, w, LABEL_H }, {}, {} };
	if(c.kind == KNOB_CELL) {
		c.control = { x + (w - KNOB) / 2, y + CONTROL_CY - KNOB / 2, KNOB, KNOB };
		c.valueBand = { x, y + VALUE_Y, w, LABEL_H };
	} else {
		c.control = { x + WIDGET_INSET, y + CONTROL_CY - FRAME_H / 2, w - 2 * WIDGET_INSET, FRAME_H };
	}
	return c;
}

Tab tab(int pluginVoice, int t, uint8_t fxType, bool (*has)(void*, uint32_t), void* ctx)
{
	Tab out = { windowWidth(), windowHeight(pluginVoice), {} };
	Group groups[MAX_GROUPS];
	Slot rows[ROWS][SLOTS_PER_ROW];
	int counts[ROWS];
	tabRows(t, fxType, has, ctx, groups, rows, counts);

	const float inner = out.width - 2 * MARGIN;
	for(int r = 0; r < ROWS; r++) {
		// the row's spare width goes to its boxes by their number of cells
		int cells = 0;
		for(int i = 0; i < counts[r]; i++) cells += rows[r][i].cells;
		const float extra = inner - naturalWidth(rows[r], counts[r]);
		const float cellW = CELL_W + extra / cells;
		const float y = headerHeight(pluginVoice) + r * (BOX_H + GAP);
		float x = MARGIN;
		for(int i = 0; i < counts[r]; i++) {
			const Slot& slot = rows[r][i];
			const float w = slot.cells * cellW + 2 * BOX_PAD;
			if(slot.group) {
				Box b = { slot.group->title, r, { x, y, w, BOX_H }, slot.cells, {} };
				for(int c = 0; c < slot.group->n; c++)
					b.cells.push_back(cellAt(slot.group->params[c], slot.group->labels[c], BOX_PAD + c * cellW,
						CELLS_Y, cellW));
				out.boxes.push_back(b);
			}
			x += w + GAP;
		}
	}
	return out;
}



float kitArrowsX(int pluginVoice)
{
	if(pluginVoice < 0) return tabRect(2).x;
	for(const Box& b : tab(pluginVoice, pluginVoice, 0, nullptr, nullptr).boxes)
		if(b.row == 0 && b.rect.x > MARGIN) return b.rect.x - GAP / 2 - HEADER_ITEM_GAP / 2 - HEADER_ITEM_H;
	return tabRect(2).x;
}

}
