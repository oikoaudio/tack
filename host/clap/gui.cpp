/*
 * gui.cpp
 *
 * Tack's editor, drawn with Dear ImGui into a pugl OpenGL view.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "gui.h"
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <strings.h>
#include <cstdlib>
#include <ctime>
#include <algorithm>
#include <vector>
#include <pugl/pugl.h>
#include <pugl/gl.h>
#include "imgui.h"
#include "backends/imgui_impl_opengl3.h"

#include "lxr_params.h"
#include "kit_name.h"
#include "presets.h"

template<typename T> static T clampTo(T v, T lo, T hi) { return v < lo ? lo : (v > hi ? hi : v); }
static const float PI = 3.14159265f;

// A box holds a front panel sub-page as one row of controls, each with a short label (the full name is a tooltip).
// On voice tabs the boxes sit in three rows that follow the signal: what makes the sound, what shapes it, what
// modulates it. Every sub-page has a fixed slot, as wide as the group is on any voice, so nothing moves between tabs.
static const float CELL_W = 64, CELL_H = 80, KNOB = 36, BOX_PAD = 8, TITLE_H = 20, LABEL_H = 14;
// rows are stretched to the window's width, so the box being drawn may have wider cells than CELL_W
static float cellW = CELL_W;
static const float BOX_H = TITLE_H + CELL_H + 2 * BOX_PAD, GAP = 6, MARGIN = 10, HEADER_H = 66;
// a voice plugin's header has no row of tabs: the boxes start this much higher
static const float TAB_ROW_H = 25;
static float headerHeight(int voice) { return voice < 0 ? HEADER_H : HEADER_H - TAB_ROW_H; }

// front panel sub-pages per row: oscillator, FM (mod osc on cymbal and hi-hat), transient; filter, amp EG, mix;
// modulation (velocity on cymbal and hi-hat), LFO
static const int ROWS = 3, SLOTS_PER_ROW = 3;
static const int rowSlots[ROWS][SLOTS_PER_ROW] = { { 0, 3, 4 }, { 5, 1, 7 }, { 2, 6, -1 } };

// short labels that fit a control; anything not listed is short enough already
static const char* const shortLabels[][2] =
{
	{"Waveform", "Wave"}, {"Waveform 1", "Wave 1"}, {"Waveform 2", "Wave 2"}, {"Pulse Width", "PW"},
	{"Repeat Count", "Repeat"}, {"Decay Closed", "Closed"}, {"Decay Open", "Open"},
	{"Pitch Mod Amount", "Pitch Amt"}, {"Velocity Destination", "Velo Dest"}, {"Velocity Amount", "Velo Amt"},
	{"Velocity Volume Mod", "Velo Vol"}, {"Destination", "Dest"}, {"Volume Mod", "Vol Mod"},
	{"Frequency", "Freq"}, {"Frequency 1", "Freq 1"}, {"Frequency 2", "Freq 2"}, {"Noise Frequency", "Noise Frq"},
	{"Noise/Osc Mix", "Noise Mix"}, {"Resonance", "Reso"}, {"Overdrive", "Drive"}, {"Clock Sync", "Sync"},
	{"Retrigger", "Retrig"}, {"Panning", "Pan"}, {"Sample Rate", "Smp Rate"}, {"Audio Out", "Output"},
	{"Threshold", "Thresh"}, {"Reflection", "Reflect"}, {"Feedback", "Fdbk"},
};

static const char* shortLabel(const char* label)
{
	for(const auto& s : shortLabels)
		if(!strcmp(label, s[0])) return s[1];
	return label;
}

static const char* const tabNames[] = { "Drum 1", "Drum 2", "Drum 3", "Snare", "Cymbal", "Hi-hat", "Mix & FX" };

// each kind of voice has a colour: red drums, orange snare, yellow cymbal and cream hi-hat.
// A voice plugin wears its voice's; Tack the open tab's. Values stay the accent orange.
static const ImVec4 voiceColours[] =
{
	ImVec4(0.82f, 0.23f, 0.18f, 1), ImVec4(0.82f, 0.23f, 0.18f, 1), ImVec4(0.82f, 0.23f, 0.18f, 1),
	ImVec4(0.94f, 0.49f, 0.14f, 1), ImVec4(0.95f, 0.76f, 0.24f, 1), ImVec4(0.93f, 0.90f, 0.83f, 1),
};

// a voice's colour (0-5), or the accent for none (the FX tab)
static ImVec4 voiceColour(int voice)
{
	return voice >= 0 && voice < 6 ? voiceColours[voice] : ImGui::GetStyleColorVec4(ImGuiCol_SliderGrab);
}

static ImVec4 darker(ImVec4 c, float f) { return ImVec4(c.x * f, c.y * f, c.z * f, c.w); }

struct TackGui
{
	TackGuiHost host;
	PuglWorld* world = nullptr;
	PuglView* view = nullptr;
	ImGuiContext* imgui = nullptr;
	bool glReady = false;
	double lastTime = 0;
	int tab = 0;
	// window pixels per unit of the sizes in this file: the host's scale (set_scale), or on macOS the screen's
	double scale = 1;

	// the save kit dialog
	char saveName[9] = "";
	bool saveExists = false;	// a kit of that name is there: the next save overwrites it
	bool saveFailed = false;
	bool focusName = false;

	// the kit browser, read from the kit folder when it opens and when stepping through kits
	TackKitList kits = {};
	std::vector<struct Entry> entries;
	int browseFolder = -1;	// shown in the browser; -1 the built-in Init Kit
	bool browserOpen = false;
	bool keepClosed = false;	// the click that closed the browser was on the kit button, which would reopen it
};

//-----------------------------------------------------------
// parameter access in plugin values

static double value(TackGui* g, uint32_t id) { return g->host.getValue(g->host.ctx, id); }

static void valueText(TackGui* g, uint32_t id, uint16_t v, char* text, size_t size)
{
	g->host.valueText(g->host.ctx, id, v, text, size);
}


static void edit(TackGui* g, uint32_t id, double v)
{
	g->host.beginEdit(g->host.ctx, id);
	g->host.setValue(g->host.ctx, id, v);
	g->host.endEdit(g->host.ctx, id);
}

//-----------------------------------------------------------
// controls

// draws a control's short label centred in its cell, in a smaller font if it is still too wide
static void drawLabel(const char* label, ImVec2 pos, ImU32 color)
{
	const char* text = shortLabel(label);
	const float w = ImGui::CalcTextSize(text).x;
	const float scale = w > (cellW - 6) ? std::max(0.75f, (cellW - 6) / w) : 1.f;
	const ImVec4 clip(pos.x, pos.y, pos.x + cellW, pos.y + LABEL_H + 2);
	ImGui::GetWindowDrawList()->AddText(ImGui::GetFont(), ImGui::GetFontSize() * scale,
		ImVec2(pos.x + std::max(0.f, (cellW - w * scale) * 0.5f), pos.y + (1 - scale) * ImGui::GetFontSize() * 0.5f),
		color, text, nullptr, 0, &clip);
}

// the full name, and the value if it doesn't fit, when the pointer rests on a control
static void tooltip(const char* label, const char* value)
{
	if(!ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) return;
	if(value) ImGui::SetTooltip("%s: %s", label, value);
	else ImGui::SetTooltip("%s", label);
}

// pulse width only shapes oscillators set to the PWM waveform: the voice's oscillator, or its FM or mod oscillators
static bool isPulseWidth(const LxrParamInfo* p) { return !strcmp(p->label, "Pulse Width"); }

static bool voiceUsesPwm(TackGui* g, int voice)
{
	for(int i = 0; i < lxr_params_count(); i++) {
		const LxrParamInfo* p = lxr_params_info(i);
		if(p->voice != voice || !p->isEnum || !strstr(p->label, "Waveform")) continue;
		char text[64];
		valueText(g, p->param, (uint16_t)value(g, p->param), text, sizeof text);
		if(!strcmp(text, "PWM")) return true;
	}
	return false;
}

// a knob for a stepped parameter: drag vertically (Shift for fine), wheel steps by one, double-click resets
static void knob(TackGui* g, const LxrParamInfo* p, const char* label)
{
	ImGuiIO& io = ImGui::GetIO();
	ImDrawList* dl = ImGui::GetWindowDrawList();
	const ImVec2 pos = ImGui::GetCursorScreenPos();
	const double v = value(g, p->param);
	const double range = p->max - p->min;

	// dimmed, but still editable, while it has no effect
	const bool inactive = isPulseWidth(p) && !voiceUsesPwm(g, p->voice);
	if(inactive) ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.35f);
	ImGui::PushID(p->param);
	ImGui::InvisibleButton("knob", ImVec2(cellW, CELL_H));
	const bool hovered = ImGui::IsItemHovered();
	ImGuiStorage* store = ImGui::GetStateStorage();
	const ImGuiID accumId = ImGui::GetID("accum");

	if(ImGui::IsItemActivated()) {
		g->host.beginEdit(g->host.ctx, p->param);
		store->SetFloat(accumId, (float)v);
	}
	// the frame a knob is grabbed, MouseDelta can still hold the move that brought the pointer there
	if(ImGui::IsItemActive() && !ImGui::IsItemActivated() && io.MouseDelta.y != 0) {
		// the full range over about 200 pixels, five times finer with Shift
		const float perPixel = (float)range / (io.KeyShift ? 1000.f : 200.f);
		float acc = store->GetFloat(accumId, (float)v) - io.MouseDelta.y * perPixel;
		acc = clampTo(acc, (float)p->min, (float)p->max);
		store->SetFloat(accumId, acc);
		const double stepped = std::round(acc);
		if(stepped != v) g->host.setValue(g->host.ctx, p->param, stepped);
	}
	if(ImGui::IsItemDeactivated()) g->host.endEdit(g->host.ctx, p->param);
	if(hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
		edit(g, p->param, lxr_params_default(p->param));
	if(hovered && io.MouseWheel != 0 && !ImGui::IsItemActive()) {
		const double nv = clampTo(v + (io.MouseWheel > 0 ? 1 : -1), (double)p->min, (double)p->max);
		if(nv != v) edit(g, p->param, nv);
	}

	// drawing: an arc from 7 to 5 o'clock, filled up to the value
	const ImVec2 c(pos.x + cellW * 0.5f, pos.y + KNOB * 0.5f + 4);
	const float r = KNOB * 0.5f;
	const float a0 = 0.75f * PI, a1 = 2.25f * PI;
	const float t = range > 0 ? (float)((v - p->min) / range) : 0;
	const float a = a0 + (a1 - a0) * t;
	const ImU32 track = ImGui::GetColorU32(ImGuiCol_FrameBg);
	const ImU32 accent = ImGui::GetColorU32(ImGui::IsItemActive() ? ImGuiCol_SliderGrabActive : ImGuiCol_SliderGrab);
	dl->PathArcTo(c, r - 2, a0, a1, 32);
	dl->PathStroke(track, ImDrawFlags_None, 4.f);
	dl->PathArcTo(c, r - 2, a0, a, 32);
	dl->PathStroke(accent, ImDrawFlags_None, 4.f);
	dl->AddLine(c, ImVec2(c.x + std::cos(a) * (r - 6), c.y + std::sin(a) * (r - 6)), ImGui::GetColorU32(ImGuiCol_Text), 2);
	// the host's modulation: a thin ring outside the track, from the value to where the modulation takes it
	const double m = g->host.modulation(g->host.ctx, p->param);
	if(m != 0 && range > 0) {
		const float tm = (float)clampTo((v + m - p->min) / range, 0.0, 1.0);
		const float am = a0 + (a1 - a0) * tm;
		dl->PathArcTo(c, r + 1.5f, std::min(a, am), std::max(a, am), 32);
		dl->PathStroke(ImGui::GetColorU32(ImGuiCol_PlotHistogram), ImDrawFlags_None, 2.f);
	}

	// label and value, centred and clipped to the cell
	char text[64];
	valueText(g, p->param, (uint16_t)v, text, sizeof text);
	drawLabel(label, ImVec2(pos.x, pos.y + KNOB + 8), ImGui::GetColorU32(ImGuiCol_Text));
	const ImVec4 clip(pos.x, pos.y, pos.x + cellW, pos.y + CELL_H);
	const float vw = ImGui::CalcTextSize(text).x;
	dl->AddText(nullptr, 0, ImVec2(pos.x + std::max(0.f, (cellW - vw) * 0.5f), pos.y + KNOB + 10 + LABEL_H),
		ImGui::GetColorU32(ImGuiCol_TextDisabled), text, nullptr, 0, &clip);
	if(!ImGui::IsItemActive()) {
		if(inactive) tooltip("Pulse Width: only affects waveforms set to PWM", nullptr);
		else tooltip(label, vw > cellW ? text : nullptr);
	}
	ImGui::PopID();
	if(inactive) ImGui::PopStyleVar();
}

// a drop-down list for parameters with named values
static void selector(TackGui* g, const LxrParamInfo* p, const char* label)
{
	const ImVec2 pos = ImGui::GetCursorScreenPos();
	const uint16_t v = (uint16_t)value(g, p->param);
	char current[64];
	valueText(g, p->param, v, current, sizeof current);

	ImGui::PushID(p->param);
	ImGui::BeginGroup();
	drawLabel(label, pos, ImGui::GetColorU32(ImGuiCol_Text));
	ImGui::SetCursorScreenPos(ImVec2(pos.x + 2, pos.y + LABEL_H + 6));
	ImGui::SetNextItemWidth(cellW - 4);
	// the list may need more room than the box, e.g. for modulation destinations
	ImGui::SetNextWindowSizeConstraints(ImVec2(cellW, 0), ImVec2(320, 400));
	if(ImGui::BeginCombo("##v", current, ImGuiComboFlags_HeightLarge)) {
		for(uint16_t i = p->min; i <= p->max; i++) {
			if(!g->host.offersValue(g->host.ctx, p->param, i)) continue;
			char text[96];
			valueText(g, p->param, i, text, sizeof text);
			// by value, not text: two options may read the same
			ImGui::PushID(i);
			if(ImGui::Selectable(text, i == v)) edit(g, p->param, i);
			ImGui::PopID();
			if(i == v) ImGui::SetItemDefaultFocus();
		}
		ImGui::EndCombo();
	}
	if(!ImGui::IsPopupOpen("##v", ImGuiPopupFlags_AnyPopupId)) tooltip(label, current);
	ImGui::SetCursorScreenPos(pos);
	ImGui::Dummy(ImVec2(cellW, CELL_H));
	ImGui::EndGroup();
	ImGui::PopID();
}

static float boxWidth(int cells) { return cells * CELL_W + 2 * BOX_PAD; }

// one front panel sub-page: a titled box of the given width with its controls in a row of the given number of cells
static void box(TackGui* g, const char* title, const LxrParamInfo* const* params, const char* const* labels, int n,
	float width, int cells)
{
	cellW = (width - 2 * BOX_PAD) / cells;
	ImGui::PushID(title);
	ImGui::BeginChild("box", ImVec2(width, BOX_H), ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar);
	ImGui::TextUnformatted(title);
	for(int i = 0; i < n; i++)
	{
		if(i) ImGui::SameLine(0, 0);
		if(params[i]->isEnum) selector(g, params[i], labels[i]);
		else knob(g, params[i], labels[i]);
	}
	ImGui::EndChild();
	ImGui::PopID();
}

//-----------------------------------------------------------
// what a tab shows

struct Group { const char* title; const LxrParamInfo* params[8]; const char* labels[8]; int n; };

// g: the parameters the plugin has; nullptr for all of them, as for sizing the window
static int collectGroups(const TackGui* g, int voice, uint8_t fxType, Group* groups)
{
	int numGroups = 0;
	auto addGroup = [&](const char* title) -> Group& {
		Group& gr = groups[numGroups++];
		gr.title = title;
		gr.n = 0;
		return gr;
	};
	auto add = [](Group& gr, const LxrParamInfo* p, const char* label) {
		if(gr.n < 8) { gr.params[gr.n] = p; gr.labels[gr.n] = label; gr.n++; }
	};

	if(voice < LXR_PARAMS_FX_VOICE) {
		for(uint8_t grp = 0; grp < LXR_PARAMS_NUM_GROUPS; grp++) {
			const char* title = lxr_params_groupName(voice, grp);
			if(!title) continue;
			Group& gr = addGroup(title);
			for(int i = 0; i < lxr_params_count(); i++) {
				const LxrParamInfo* p = lxr_params_info(i);
				if(p->voice == voice && p->group == grp && (!g || g->host.hasParam(g->host.ctx, p->param))) add(gr, p, p->label);
			}
		}
	} else {
		// the FX bus' own settings, then the controls of the selected FX type, named for it, then master
		Group& common = addGroup(lxr_params_groupName(LXR_PARAMS_FX_VOICE, 0));
		Group* typeGroup = fxType >= 1 && fxType <= 4 ? &addGroup(lxr_params_groupName(LXR_PARAMS_FX_VOICE, fxType)) : nullptr;
		Group& master = addGroup(lxr_params_groupName(LXR_PARAMS_FX_VOICE, LXR_PARAMS_FX_MASTER_GROUP));
		for(int i = 0; i < lxr_params_count(); i++) {
			const LxrParamInfo* p = lxr_params_info(i);
			if(p->voice != LXR_PARAMS_FX_VOICE) continue;
			if(p->group == LXR_PARAMS_FX_MASTER_GROUP) add(master, p, p->label);
			else if(p->group == 0) add(common, p, p->label);
			else if(typeGroup) {
				const char* label = lxr_params_fxLabel(p->param, fxType);
				if(label) add(*typeGroup, p, label);
			}
		}
	}
	return numGroups;
}

// a slot is as many cells wide as its group has controls on any voice
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

// a box in a row; group is NULL for a slot that stays empty on this tab
struct Slot { const Group* group; int cells; };

// The FX tab: the FX bus' settings, the selected FX type's controls and master; then a kit mixer, each voice's
// volume and pan, and its output (which is how a voice reaches the FX bus) and drive.
enum { FX_COMMON, FX_TYPE, FX_MASTER, MIX_VOLUME, MIX_PAN, MIX_OUTPUT, MIX_DRIVE, FX_TAB_GROUPS };
static const char* const mixerLabels[] = { "Volume", "Panning", "Audio Out", "Overdrive" };
static const char* const mixerTitles[] = { "Volume", "Pan", "Output", "Drive" };

static void collectFxTab(uint8_t fxType, Group* groups)
{
	for(int i = 0; i < FX_TAB_GROUPS; i++) groups[i].n = 0;
	groups[FX_COMMON].title = lxr_params_groupName(LXR_PARAMS_FX_VOICE, 0);
	groups[FX_TYPE].title = fxType >= 1 && fxType <= 4 ? lxr_params_groupName(LXR_PARAMS_FX_VOICE, fxType) : nullptr;
	groups[FX_MASTER].title = lxr_params_groupName(LXR_PARAMS_FX_VOICE, LXR_PARAMS_FX_MASTER_GROUP);
	auto add = [](Group& gr, const LxrParamInfo* p, const char* label) {
		if(gr.n < 8) { gr.params[gr.n] = p; gr.labels[gr.n] = label; gr.n++; }
	};
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
}

// the FX type box is as wide as the widest type, so nothing moves when the type changes
static int fxTypeCells()
{
	int cells = 1;
	Group groups[FX_TAB_GROUPS];
	for(uint8_t fx = 1; fx <= 4; fx++) {
		collectFxTab(fx, groups);
		cells = std::max(cells, groups[FX_TYPE].n);
	}
	return cells;
}

// the rows of a tab: three for every tab
static int tabRows(const TackGui* g, int tab, uint8_t fxType, Group* groups, Slot rows[ROWS][SLOTS_PER_ROW], int counts[ROWS])
{
	if(tab < LXR_PARAMS_FX_VOICE) {
		const int n = collectGroups(g, tab, 0, groups);
		for(int r = 0; r < ROWS; r++) {
			counts[r] = 0;
			for(int i = 0; i < SLOTS_PER_ROW && rowSlots[r][i] >= 0; i++) {
				const int group = rowSlots[r][i];
				const Group* found = nullptr;
				for(int j = 0; j < n; j++)
					if(groups[j].n && groups[j].params[0]->group == group) found = &groups[j];
				rows[r][counts[r]++] = { found, slotCells(group) };
			}
		}
		return ROWS;
	}
	collectFxTab(fxType, groups);
	static const int fxRows[ROWS][SLOTS_PER_ROW] = {
		{ FX_COMMON, FX_TYPE, FX_MASTER }, { MIX_VOLUME, MIX_PAN, -1 }, { MIX_OUTPUT, MIX_DRIVE, -1 }
	};
	for(int r = 0; r < ROWS; r++) {
		counts[r] = 0;
		for(int i = 0; i < SLOTS_PER_ROW && fxRows[r][i] >= 0; i++) {
			const Group& gr = groups[fxRows[r][i]];
			const int cells = fxRows[r][i] == FX_TYPE ? fxTypeCells() : std::max(gr.n, 2);
			rows[r][counts[r]++] = { gr.title && gr.n ? &gr : nullptr, cells };
		}
	}
	return ROWS;
}

// the outputs switch, on the right of the Output box's title row
static void outputsSwitch(TackGui* g, ImVec2 boxPos, float boxWidth)
{
	const bool individual = value(g, TACK_GUI_SEPARATE_OUTPUTS_ID) >= 0.5;
	const ImGuiStyle& s = ImGui::GetStyle();
	const float w = ImGui::CalcTextSize("Stereo").x + ImGui::CalcTextSize("Individual").x
		+ 2 * (ImGui::GetFrameHeight() + s.ItemInnerSpacing.x) + s.ItemSpacing.x;
	ImGui::SetCursorPos(ImVec2(boxPos.x + boxWidth - BOX_PAD - w, boxPos.y + 3));
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(s.FramePadding.x, 1));
	if(ImGui::RadioButton("Stereo", !individual)) edit(g, TACK_GUI_SEPARATE_OUTPUTS_ID, 0);
	if(ImGui::IsItemHovered()) ImGui::SetTooltip("Everything on the Main output");
	ImGui::SameLine();
	if(ImGui::RadioButton("Individual", individual)) edit(g, TACK_GUI_SEPARATE_OUTPUTS_ID, 1);
	if(ImGui::IsItemHovered())
		ImGui::SetTooltip("Each Direct voice on its own output; Main has the voices that go through the FX");
	ImGui::PopStyleVar();
}

static float naturalWidth(const Slot* slots, int n)
{
	float w = (n - 1) * GAP;
	for(int i = 0; i < n; i++) w += boxWidth(slots[i].cells);
	return w;
}

// the widest row of any tab sets the window's width; the other rows are stretched to it
static uint32_t windowWidth()
{
	static uint32_t width;
	if(width) return width;
	float widest = 0;
	Group groups[FX_TAB_GROUPS > LXR_PARAMS_NUM_GROUPS + 1 ? FX_TAB_GROUPS : LXR_PARAMS_NUM_GROUPS + 1];
	Slot rows[ROWS][SLOTS_PER_ROW];
	int counts[ROWS];
	for(int tab = 0; tab <= LXR_PARAMS_FX_VOICE; tab++) {
		tabRows(nullptr, tab, 1, groups, rows, counts);
		for(int r = 0; r < ROWS; r++) widest = std::max(widest, naturalWidth(rows[r], counts[r]));
	}
	width = (uint32_t)(widest + 2 * MARGIN);
	return width;
}

static uint32_t windowHeight(int voice)
{
	return (uint32_t)(headerHeight(voice) + ROWS * BOX_H + (ROWS - 1) * GAP + MARGIN);
}

static void drawTab(TackGui* g)
{
	Group groups[FX_TAB_GROUPS > LXR_PARAMS_NUM_GROUPS + 1 ? FX_TAB_GROUPS : LXR_PARAMS_NUM_GROUPS + 1];
	Slot rows[ROWS][SLOTS_PER_ROW];
	int counts[ROWS];
	tabRows(g, g->tab, (uint8_t)value(g, lxr_params_fxTypeParam()), groups, rows, counts);

	const float inner = windowWidth() - 2 * MARGIN;
	for(int r = 0; r < ROWS; r++) {
		// spread the row's spare width over its boxes by their size
		int cells = 0;
		for(int i = 0; i < counts[r]; i++) cells += rows[r][i].cells;
		const float extra = inner - naturalWidth(rows[r], counts[r]);
		float x = MARGIN;
		for(int i = 0; i < counts[r]; i++) {
			const Slot& slot = rows[r][i];
			const float w = boxWidth(slot.cells) + extra * slot.cells / cells;
			if(slot.group) {
				ImGui::SetCursorPos(ImVec2(x, headerHeight(g->host.voice) + r * (BOX_H + GAP)));
				box(g, slot.group->title, slot.group->params, slot.group->labels, slot.group->n, w, slot.cells);
				if(slot.group->title == mixerTitles[MIX_OUTPUT - MIX_VOLUME])
					outputsSwitch(g, ImVec2(x, headerHeight(g->host.voice) + r * (BOX_H + GAP)), w);
			}
			x += w + GAP;
		}
	}
	cellW = CELL_W;
}

// what the browser lists: whole kits in Tack, in a voice plugin the voices of kits it can play
struct Entry { int kit; int voice; char name[24]; };

// the voice of a kit that is its only one that isn't the init kit's (TackKit.voices), or -1
static int onlyVoice(uint8_t voices)
{
	for(int v = 0; v < 6; v++)
		if(voices == 1 << v) return v;
	return -1;
}

static void buildEntries(TackGui* g)
{
	g->entries.clear();
	const int voice = g->host.voice;
	for(int i = 0; i < g->kits.numKits; i++) {
		const TackKit& kit = g->kits.kits[i];
		if(voice < 0) {
			// a kit with a single voice of its own, as the voice plugins save them, says which
			static const char* const kinds[] = { "Drum", "Drum", "Drum", "Snare", "Cymbal", "Hi-hat" };
			Entry e = { i, -1, "" };
			const int only = onlyVoice(kit.voices);
			if(only >= 0) snprintf(e.name, sizeof e.name, "%s (%s)", kit.name, kinds[only]);
			else snprintf(e.name, sizeof e.name, "%s", kit.name);
			g->entries.push_back(e);
			continue;
		}
		// Tack Drum plays any of drums 1-3; voices that are just the init kit's are left out
		const int first = voice < 3 ? 0 : voice, last = voice < 3 ? 2 : voice;
		for(int v = first; v <= last; v++) {
			if(!(kit.voices & (1 << v))) continue;
			Entry e = { i, v, "" };
			tack_voicePresetName(kit.name, v, kit.voices, e.name, sizeof e.name);
			g->entries.push_back(e);
		}
	}
	// folder by folder, alphabetically
	std::stable_sort(g->entries.begin(), g->entries.end(), [g](const Entry& a, const Entry& b) {
		const int fa = g->kits.kits[a.kit].folder, fb = g->kits.kits[b.kit].folder;
		return fa != fb ? fa < fb : strcasecmp(a.name, b.name) < 0;
	});
}

static void scan(TackGui* g)
{
	tack_scanKits(&g->kits);
	buildEntries(g);
}

static int entryFolder(const TackGui* g, int e) { return g->kits.kits[g->entries[e].kit].folder; }

static bool isCurrent(TackGui* g, int e)
{
	const char* path = g->host.kitPath(g->host.ctx);
	const Entry& entry = g->entries[e];
	return path && !strcmp(g->kits.kits[entry.kit].path, path) && entry.voice == g->host.kitVoice(g->host.ctx);
}

// the current entry's index, or -1 for the init kit or a kit that isn't in the kit folder
static int currentEntry(TackGui* g)
{
	for(int e = 0; e < (int)g->entries.size(); e++)
		if(isCurrent(g, e)) return e;
	return -1;
}

static void load(TackGui* g, int e)
{
	const TackKit& kit = g->kits.kits[g->entries[e].kit];
	const int only = onlyVoice(kit.voices);
	if(g->host.voice < 0 && only >= 0) {
		// a voice saved on its own replaces just that voice of the kit: in its place, or a drum on the drum tab that
		// is open
		g->host.loadVoice(g->host.ctx, kit.path, only, only < 3 && g->tab < 3 ? g->tab : only);
		g->browseFolder = entryFolder(g, e);
		return;
	}
	g->host.loadKit(g->host.ctx, kit.path, g->entries[e].voice);
	g->browseFolder = entryFolder(g, e);
}

// loads the previous (dir -1) or next entry in the current one's folder, wrapping around; from the init kit, the
// first or last of all
static void stepKit(TackGui* g, int dir)
{
	scan(g);
	const int n = (int)g->entries.size();
	if(!n) return;
	const int cur = currentEntry(g);
	int next;
	if(cur < 0) {
		next = dir > 0 ? 0 : n - 1;
	} else {
		const int folder = entryFolder(g, cur);
		int first = cur, last = cur;
		while(first > 0 && entryFolder(g, first - 1) == folder) first--;
		while(last < n - 1 && entryFolder(g, last + 1) == folder) last++;
		next = cur + dir;
		if(next < first) next = last;
		if(next > last) next = first;
	}
	load(g, next);
}

static void openBrowser(TackGui* g)
{
	scan(g);
	const int cur = currentEntry(g);
	g->browseFolder = cur >= 0 ? entryFolder(g, cur) : (!g->entries.empty() && g->host.kitPath(g->host.ctx)[0] ? entryFolder(g, 0) : -1);
}

// folders on the left, the selected folder's kits (or voices) on the right, alphabetically; clicking one loads it and
// leaves the browser open, so they can be tried one after the other
static void drawBrowser(TackGui* g)
{
	const ImVec2 size = ImGui::GetIO().DisplaySize;
	ImGui::SetNextWindowPos(ImVec2(MARGIN, 36));
	ImGui::SetNextWindowSize(ImVec2(size.x - 2 * MARGIN, size.y - 36 - MARGIN));
	if(!ImGui::BeginPopup("Kits")) {
		// ImGui closes a popup when the mouse goes down outside it; the kit button acts on release
		if(g->browserOpen && ImGui::IsMouseDown(ImGuiMouseButton_Left)) g->keepClosed = true;
		g->browserOpen = false;
		return;
	}
	g->browserOpen = true;
	if(ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();

	// folders with something to offer
	std::vector<bool> used(g->kits.numFolders, false);
	for(int e = 0; e < (int)g->entries.size(); e++) used[entryFolder(g, e)] = true;

	const float folderW = 190;
	ImGui::BeginChild("folders", ImVec2(folderW, 0), ImGuiChildFlags_Borders);
	if(ImGui::Selectable("Tack", g->browseFolder == -1)) g->browseFolder = -1;
	for(int f = 0; f < g->kits.numFolders; f++) {
		if(!used[f]) continue;
		ImGui::PushID(f);
		if(ImGui::Selectable(g->kits.folders[f][0] ? g->kits.folders[f] : "Kits", g->browseFolder == f)) g->browseFolder = f;
		ImGui::PopID();
	}
	if(!g->kits.numFolders) ImGui::TextDisabled("No kits in %s", tack_kitFolder());
	ImGui::EndChild();

	ImGui::SameLine();
	ImGui::BeginChild("kits", ImVec2(0, 0), ImGuiChildFlags_Borders);
	const float kitW = 150;
	const int columns = std::max(1, (int)(ImGui::GetContentRegionAvail().x / (kitW + ImGui::GetStyle().ItemSpacing.x)));
	if(g->browseFolder < 0) {
		if(ImGui::Selectable(g->host.voice < 0 ? "Init Kit" : "Init", !g->host.kitPath(g->host.ctx)[0], 0, ImVec2(kitW, 0)))
			g->host.loadKit(g->host.ctx, nullptr, -1);
	} else {
		int column = 0;
		for(int e = 0; e < (int)g->entries.size(); e++) {
			if(entryFolder(g, e) != g->browseFolder) continue;
			if(column++ % columns) ImGui::SameLine();
			ImGui::PushID(e);
			if(ImGui::Selectable(g->entries[e].name, isCurrent(g, e), 0, ImVec2(kitW, 0))) load(g, e);
			ImGui::PopID();
		}
	}
	ImGui::EndChild();
	ImGui::EndPopup();
}

// a voice plugin's chromatic switch, drawn as a small keyboard: lit when notes set the pitch
static void chromaticSwitch(TackGui* g)
{
	const bool on = value(g, TACK_GUI_CHROMATIC_ID) >= 0.5;
	const ImVec2 size(30, ImGui::GetFrameHeight());
	const ImVec2 p = ImGui::GetCursorScreenPos();
	if(ImGui::InvisibleButton("chromatic", size)) edit(g, TACK_GUI_CHROMATIC_ID, on ? 0 : 1);
	const bool hovered = ImGui::IsItemHovered();
	if(hovered)
		ImGui::SetTooltip(on ? "Chromatic: notes set the pitch, C3 is the tuned pitch" : "Not chromatic: every note plays the tuned pitch");

	ImDrawList* dl = ImGui::GetWindowDrawList();
	dl->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y), ImGui::GetColorU32(hovered ? ImGuiCol_ButtonHovered : ImGuiCol_Button), 3);
	const ImU32 white = on ? ImGui::GetColorU32(ImGuiCol_SliderGrab) : ImGui::GetColorU32(ImGuiCol_TextDisabled);
	const ImU32 black = ImGui::GetColorU32(ImGuiCol_Button);
	// seven white keys with five black ones over them
	const float x0 = p.x + 4, y0 = p.y + 4, w = (size.x - 8) / 7, h = size.y - 8;
	dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x0 + 7 * w, y0 + h), white, 1);
	for(int k = 1; k < 7; k++) dl->AddLine(ImVec2(x0 + k * w, y0), ImVec2(x0 + k * w, y0 + h), black);
	for(int k : { 1, 2, 4, 5, 6 })
		dl->AddRectFilled(ImVec2(x0 + k * w - w * 0.35f, y0), ImVec2(x0 + k * w + w * 0.35f, y0 + h * 0.6f), black);
}

// audition the selected voice
static void drawAudition(TackGui* g)
{
	if(g->tab >= LXR_PARAMS_FX_VOICE) return;
	ImGui::SameLine(0, 24);
	if(g->tab == 5) {
		if(ImGui::Button("Trig closed")) g->host.audition(g->host.ctx, 5);
		ImGui::SameLine();
		if(ImGui::Button("Trig open")) g->host.audition(g->host.ctx, 6);
	} else if(ImGui::Button("Trig")) {
		g->host.audition(g->host.ctx, g->tab);
	}
}

static void drawHeader(TackGui* g)
{
	// the plugin's name; a voice plugin's chromatic switch next to it, then the kits' voices it can play like
	// Tack's kits
	static const char* const titles[] = { "TACK DRUM", "TACK DRUM", "TACK DRUM", "TACK SNARE", "TACK CYMBAL", "TACK HI-HAT" };
	// a band in the voice's colour along the top
	const ImVec4 colour = voiceColour(g->tab);
	const ImVec2 origin = ImGui::GetWindowPos();
	ImGui::GetWindowDrawList()->AddRectFilled(origin, ImVec2(origin.x + ImGui::GetWindowWidth(), origin.y + 4),
		ImGui::GetColorU32(colour));
	// the name opens the about box
	const char* title = g->host.voice < 0 ? "TACK" : titles[g->host.voice];
	const ImVec2 at = ImGui::GetCursorPos();
	if(ImGui::InvisibleButton("about", ImGui::CalcTextSize(title))) ImGui::OpenPopup("###about");
	if(ImGui::IsItemHovered()) ImGui::SetTooltip("About Tack");
	ImGui::SetCursorPos(at);
	ImGui::TextColored(g->host.voice < 0 ? ImGui::GetStyleColorVec4(ImGuiCol_SliderGrab) : colour, "%s", title);
	const char* kit = g->host.kitName(g->host.ctx);
	if(g->host.voice >= 0) {
		ImGui::SameLine(0, 12);
		chromaticSwitch(g);
	}
	ImGui::SameLine(0, 16);
	if(ImGui::ArrowButton("prev", ImGuiDir_Left)) stepKit(g, -1);
	ImGui::SameLine(0, 2);
	if(ImGui::ArrowButton("next", ImGuiDir_Right)) stepKit(g, 1);
	ImGui::SameLine(0, 2);
	// the kit's name opens the browser; * once it's been edited
	char label[64];
	snprintf(label, sizeof label, "%s%s###kit", kit && kit[0] ? kit : "(no name)", g->host.kitEdited(g->host.ctx) ? " *" : "");
	if(!ImGui::IsMouseDown(ImGuiMouseButton_Left) && !ImGui::IsMouseReleased(ImGuiMouseButton_Left)) g->keepClosed = false;
	if(ImGui::Button(label, ImVec2(110, 0))) {
		if(!g->keepClosed) {
			openBrowser(g);
			ImGui::OpenPopup("Kits");
		}
		g->keepClosed = false;
	}
	if(ImGui::IsItemHovered())
		ImGui::SetTooltip(g->host.voice < 0 ? "Choose a kit from the kit folder" : "Choose a voice from the kits in the kit folder");
	ImGui::SameLine(0, 8);
	if(ImGui::Button(g->host.voice < 0 ? "Save kit" : "Save")) {
		// start from the kit's name, as far as the device allows it
		int n = 0;
		for(const char* c = kit ? kit : ""; *c && n < 8; c++)
			if(tack_isKitNameChar(*c)) g->saveName[n++] = *c;
		g->saveName[n] = 0;
		g->saveExists = g->saveFailed = false;
		g->focusName = true;
		ImGui::OpenPopup("###save");
		puglGrabFocus(g->view);
	}
	ImGui::SameLine(0, 8);
	if(ImGui::Button(g->host.voice < 0 ? "Mutate kit" : "Mutate")) g->host.mutate(g->host.ctx, -1, 1);
	if(ImGui::IsItemHovered()) ImGui::SetTooltip("A small random change to the sound. Levels, pans and the FX stay");
	if(g->host.voice < 0) {
		ImGui::SameLine(0, 2);
		ImGui::BeginDisabled(g->tab >= LXR_PARAMS_FX_VOICE);
		if(ImGui::Button("Mutate voice")) g->host.mutate(g->host.ctx, g->tab, 1);
		if(ImGui::IsItemHovered()) ImGui::SetTooltip("The same, for the voice on the open tab");
		ImGui::EndDisabled();
	}
	const int mutated = g->host.mutateStep(g->host.ctx);
	char back[32];
	snprintf(back, sizeof back, mutated ? "Back (%d)###back" : "Back###back", mutated);
	ImGui::SameLine(0, 2);
	ImGui::BeginDisabled(!mutated);
	if(ImGui::Button(back)) g->host.mutate(g->host.ctx, TACK_MUTATE_AGAIN, -1);
	if(ImGui::IsItemHovered()) ImGui::SetTooltip("One step back along the way Mutate came");
	ImGui::EndDisabled();
	if(g->host.voice >= 0) {
		// no tabs, and no play button: notes play the voice
		ImGui::Separator();
		return;
	}

	// the tabs: each voice's with a line in its colour, the open one filled with it
	for(int t = 0; t <= LXR_PARAMS_FX_VOICE; t++) {
		if(t) ImGui::SameLine();
		const bool selected = g->tab == t;
		const ImVec4 c = voiceColour(t < LXR_PARAMS_FX_VOICE ? t : -1);
		if(selected) ImGui::PushStyleColor(ImGuiCol_Button, darker(c, 0.5f));
		if(ImGui::Button(tabNames[t], ImVec2(90, 0))) g->tab = t;
		if(selected) ImGui::PopStyleColor();
		if(t < LXR_PARAMS_FX_VOICE) {
			const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
			ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(a.x, b.y - 2), b, ImGui::GetColorU32(c), 3,
				ImDrawFlags_RoundCornersBottom);
		}
	}

	drawAudition(g);
	ImGui::Separator();
	// the boxes start at headerHeight() whatever the header's exact height
}

static int kitNameFilter(ImGuiInputTextCallbackData* data)
{
	return data->EventChar < 128 && tack_isKitNameChar((char)data->EventChar) ? 0 : 1;
}

// what Tack is, where it comes from and its licences: the GPL's "Appropriate Legal Notices", and the attribution
// Steinberg's VST usage guidelines ask for
static void drawAbout(TackGui* g)
{
	const ImVec2 size = ImGui::GetIO().DisplaySize;
	ImGui::SetNextWindowPos(ImVec2(size.x * 0.5f, size.y * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	if(!ImGui::BeginPopupModal("About Tack###about", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove))
		return;
	ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 560);
	ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_SliderGrab), "TACK %s", TACK_VERSION);
	ImGui::Text("A drum synth by oikoaudio. github.com/oikoaudio/tack");
	ImGui::Spacing();
	ImGui::TextWrapped("Its sound engine is the LXR-02 firmware's, by Sonic Potions and Erica Synths, modified. Tack "
		"is not affiliated with or endorsed by Erica Synths or Sonic Potions.");
	ImGui::Spacing();
	ImGui::TextWrapped("Copyright (C) 2012-2026 Erica Synths, 2013 Julian Schmidt (Sonic Potions), 2026 oikoaudio.");
	ImGui::TextWrapped("Tack is free software under the GNU General Public License version 3, with the additional terms "
		"in NOTICE.md, and keeps to the Sonic Potions LXR licence: it may not be sold. It comes with ABSOLUTELY NO "
		"WARRANTY. LICENSE, NOTICE.md and the licences of the libraries in it come with Tack, and its source is at "
		"the address above.");
	ImGui::Spacing();
	ImGui::TextWrapped("Built with the CLAP SDK, clap-wrapper, the VST3 SDK, Apple's AudioUnitSDK, Dear ImGui, pugl and "
		"libsamplerate.");
	ImGui::TextDisabled("VST is a registered trademark of Steinberg Media Technologies GmbH.");
	ImGui::PopTextWrapPos();
	ImGui::Spacing();
	if(ImGui::Button("Close", ImVec2(90, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
	ImGui::EndPopup();
}

static void drawSaveDialog(TackGui* g)
{
	const ImVec2 size = ImGui::GetIO().DisplaySize;
	ImGui::SetNextWindowPos(ImVec2(size.x * 0.5f, size.y * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	const bool voice = g->host.voice >= 0;
	if(!ImGui::BeginPopupModal(voice ? "Save voice###save" : "Save kit###save", nullptr,
		ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove))
		return;
	ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 460);
	// the folder, with the home folder as ~
	const char* folder = tack_kitFolder();
	const char* home = getenv("HOME");
	const size_t homeLen = home ? strlen(home) : 0;
	const bool inHome = homeLen && !strncmp(folder, home, homeLen) && folder[homeLen] == '/';
	// a voice is saved as a kit that has only this voice, which the voice plugins list under its own name
	ImGui::TextDisabled("To %s%s/" TACK_USER_PACK ", %s", inHome ? "~" : "", inHome ? folder + homeLen : folder,
		voice ? "as a kit with only this voice" : "where the host's preset browser lists it");

	if(g->focusName) {
		ImGui::SetKeyboardFocusHere();
		g->focusName = false;
	}
	ImGui::SetNextItemWidth(120);
	const bool enter = ImGui::InputText("Name", g->saveName, sizeof g->saveName,
		ImGuiInputTextFlags_CallbackCharFilter | ImGuiInputTextFlags_EnterReturnsTrue, kitNameFilter);
	if(ImGui::IsItemEdited()) g->saveExists = g->saveFailed = false;
	ImGui::SameLine();
	if(ImGui::Button("Random")) {
		static uint32_t rolls;
		char made[9] = {0};
		kit_name_make_up((uint32_t)time(NULL) * 2654435761u + ++rolls * 40503u, made);
		for(int i = 7; i >= 0 && made[i] == ' '; i--) made[i] = 0;
		snprintf(g->saveName, sizeof g->saveName, "%s", made);
		g->saveExists = g->saveFailed = false;
		g->focusName = true;
	}

	// trailing spaces would only pad the name
	char name[9];
	snprintf(name, sizeof name, "%s", g->saveName);
	for(int i = (int)strlen(name) - 1; i >= 0 && name[i] == ' '; i--) name[i] = 0;

	if(g->saveExists) ImGui::Text("There is a kit named %s already. Overwrite it?", name);
	if(g->saveFailed) ImGui::TextColored(ImVec4(1, 0.4f, 0.3f, 1), "Could not write the kit file.");

	ImGui::BeginDisabled(!name[0]);
	if(ImGui::Button(g->saveExists ? "Overwrite" : "Save", ImVec2(90, 0)) || (enter && name[0])) {
		const int r = g->host.saveKit(g->host.ctx, name, g->saveExists);
		g->saveExists = r == TACK_SAVE_EXISTS;
		g->saveFailed = r == TACK_SAVE_FAILED;
		g->focusName = g->saveExists;	// so that a second Enter overwrites
		if(r == TACK_SAVE_OK) ImGui::CloseCurrentPopup();
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	if(ImGui::Button("Cancel", ImVec2(90, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
	ImGui::PopTextWrapPos();
	ImGui::EndPopup();
}

static void drawFrame(TackGui* g)
{
	ImGui::SetNextWindowPos(ImVec2(0, 0));
	ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
	ImGui::Begin("Tack", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
	if(g->host.voice >= 0) g->tab = g->host.voice;
	drawHeader(g);
	drawTab(g);
	drawSaveDialog(g);
	drawAbout(g);
	drawBrowser(g);
	ImGui::End();
}

//-----------------------------------------------------------
// pugl events to ImGui

static ImGuiKey imguiKey(uint32_t key)
{
	switch(key) {
	case PUGL_KEY_TAB: return ImGuiKey_Tab;
	case PUGL_KEY_LEFT: return ImGuiKey_LeftArrow;
	case PUGL_KEY_RIGHT: return ImGuiKey_RightArrow;
	case PUGL_KEY_UP: return ImGuiKey_UpArrow;
	case PUGL_KEY_DOWN: return ImGuiKey_DownArrow;
	case PUGL_KEY_HOME: return ImGuiKey_Home;
	case PUGL_KEY_END: return ImGuiKey_End;
	case PUGL_KEY_DELETE: return ImGuiKey_Delete;
	case PUGL_KEY_BACKSPACE: return ImGuiKey_Backspace;
	case PUGL_KEY_ENTER: return ImGuiKey_Enter;
	case PUGL_KEY_ESCAPE: return ImGuiKey_Escape;
	default: return ImGuiKey_None;
	}
}

static void setMods(ImGuiIO& io, PuglMods mods)
{
	io.AddKeyEvent(ImGuiMod_Shift, mods & PUGL_MOD_SHIFT);
	io.AddKeyEvent(ImGuiMod_Ctrl, mods & PUGL_MOD_CTRL);
	io.AddKeyEvent(ImGuiMod_Alt, mods & PUGL_MOD_ALT);
	io.AddKeyEvent(ImGuiMod_Super, mods & PUGL_MOD_SUPER);
}

static PuglStatus onEvent(PuglView* view, const PuglEvent* event)
{
	TackGui* g = (TackGui*)puglGetHandle(view);
	ImGui::SetCurrentContext(g->imgui);
	ImGuiIO& io = ImGui::GetIO();

	switch(event->type)
	{
	case PUGL_REALIZE:
		g->glReady = ImGui_ImplOpenGL3_Init("#version 330 core");
		if(TACK_GUI_OWN_TIMER) puglStartTimer(view, 1, 0.02);
		break;
	case PUGL_UNREALIZE:
		if(TACK_GUI_OWN_TIMER) puglStopTimer(view, 1);
		if(g->glReady) ImGui_ImplOpenGL3_Shutdown();
		g->glReady = false;
		break;
	case PUGL_TIMER:
		puglObscureView(view);
		break;
	case PUGL_CONFIGURE:
#ifdef __APPLE__
		// pugl counts pixels; the screen's backing scale changes when the window moves to another screen
		g->scale = puglGetScaleFactor(view);
#endif
		// ImGui lays out in the editor's units and draws, fonts too, at the full resolution
		io.DisplaySize = ImVec2((float)(event->configure.width / g->scale), (float)(event->configure.height / g->scale));
		io.DisplayFramebufferScale = ImVec2((float)g->scale, (float)g->scale);
		break;
	case PUGL_EXPOSE: {
		if(!g->glReady) break;
		const double now = puglGetTime(g->world);
		io.DeltaTime = g->lastTime > 0 ? (float)std::max(now - g->lastTime, 1e-4) : 1.f / 60;
		g->lastTime = now;
		ImGui_ImplOpenGL3_NewFrame();
		ImGui::NewFrame();
		drawFrame(g);
		ImGui::Render();
		glViewport(0, 0, (GLsizei)std::lround(io.DisplaySize.x * g->scale), (GLsizei)std::lround(io.DisplaySize.y * g->scale));
		glClearColor(0.09f, 0.09f, 0.10f, 1);
		glClear(GL_COLOR_BUFFER_BIT);
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
		break;
	}
	case PUGL_MOTION:
		io.AddMousePosEvent((float)(event->motion.x / g->scale), (float)(event->motion.y / g->scale));
		break;
	case PUGL_BUTTON_PRESS:
	case PUGL_BUTTON_RELEASE: {
		// pugl numbers buttons left, right, middle; ImGui left, right, middle too
		setMods(io, event->button.state);
		io.AddMousePosEvent((float)(event->button.x / g->scale), (float)(event->button.y / g->scale));
		if(event->button.button < 3)
			io.AddMouseButtonEvent((int)event->button.button, event->type == PUGL_BUTTON_PRESS);
		break;
	}
	case PUGL_SCROLL:
		io.AddMouseWheelEvent((float)event->scroll.dx, (float)event->scroll.dy);
		break;
	case PUGL_POINTER_OUT:
		io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
		break;
	case PUGL_KEY_PRESS:
	case PUGL_KEY_RELEASE: {
		setMods(io, event->key.state);
		const ImGuiKey key = imguiKey(event->key.key);
		if(key != ImGuiKey_None) io.AddKeyEvent(key, event->type == PUGL_KEY_PRESS);
		// keys the editor doesn't use go back to the host, so that space still starts and stops it (macOS)
		if(!io.WantCaptureKeyboard) return PUGL_UNSUPPORTED;
		break;
	}
	case PUGL_TEXT:
		io.AddInputCharactersUTF8(event->text.string);
		break;
	default:
		break;
	}
	return PUGL_SUCCESS;
}

//-----------------------------------------------------------
static void applyStyle()
{
	ImGui::StyleColorsDark();
	ImGuiStyle& s = ImGui::GetStyle();
	s.WindowPadding = ImVec2(10, 8);
	s.ChildRounding = 4;
	s.FrameRounding = 3;
	s.ItemSpacing = ImVec2(6, 6);
	ImVec4* c = s.Colors;
	c[ImGuiCol_WindowBg] = ImVec4(0.09f, 0.09f, 0.10f, 1);
	c[ImGuiCol_ChildBg] = ImVec4(0.12f, 0.12f, 0.13f, 1);
	c[ImGuiCol_Border] = ImVec4(0.24f, 0.24f, 0.26f, 1);
	c[ImGuiCol_FrameBg] = ImVec4(0.22f, 0.22f, 0.24f, 1);
	c[ImGuiCol_SliderGrab] = ImVec4(0.95f, 0.55f, 0.18f, 1);
	c[ImGuiCol_SliderGrabActive] = ImVec4(1.00f, 0.70f, 0.35f, 1);
	c[ImGuiCol_Button] = ImVec4(0.20f, 0.20f, 0.22f, 1);
	c[ImGuiCol_ButtonHovered] = ImVec4(0.30f, 0.30f, 0.32f, 1);
	c[ImGuiCol_ButtonActive] = ImVec4(0.55f, 0.32f, 0.12f, 1);
	c[ImGuiCol_CheckMark] = c[ImGuiCol_SliderGrab];
	c[ImGuiCol_Header] = ImVec4(0.55f, 0.32f, 0.12f, 0.6f);
	c[ImGuiCol_HeaderHovered] = ImVec4(0.55f, 0.32f, 0.12f, 0.8f);
	c[ImGuiCol_PopupBg] = c[ImGuiCol_ChildBg];
	c[ImGuiCol_TitleBg] = c[ImGuiCol_TitleBgActive] = c[ImGuiCol_FrameBg];
	c[ImGuiCol_TextSelectedBg] = c[ImGuiCol_Header];
}

// the window's size in pixels
static void pixelSize(const TackGui* g, uint32_t* width, uint32_t* height)
{
	*width = (uint32_t)std::lround(windowWidth() * g->scale);
	*height = (uint32_t)std::lround(windowHeight(g->host.voice) * g->scale);
}

static void applySize(TackGui* g)
{
	uint32_t w, h;
	pixelSize(g, &w, &h);
	puglSetSizeHint(g->view, PUGL_DEFAULT_SIZE, w, h);
	if(puglGetNativeView(g->view)) puglSetSizeHint(g->view, PUGL_CURRENT_SIZE, w, h);
}

TackGui* tack_guiCreate(const TackGuiHost* host)
{
	TackGui* g = new TackGui();
	g->host = *host;

	// each editor has its own ImGui context; ImGui's current context is switched before every use
	ImGuiContext* previous = ImGui::GetCurrentContext();
	g->imgui = ImGui::CreateContext();
	ImGui::SetCurrentContext(g->imgui);
	ImGui::GetIO().IniFilename = nullptr;
	applyStyle();
	ImGui::SetCurrentContext(previous);

	g->world = puglNewWorld(PUGL_MODULE, PUGL_WORLD_THREADS);
	puglSetWorldString(g->world, PUGL_CLASS_NAME, "Tack");
	g->view = puglNewView(g->world);
	puglSetHandle(g->view, g);
	puglSetBackend(g->view, puglGlBackend());
	puglSetViewHint(g->view, PUGL_CONTEXT_VERSION_MAJOR, 3);
	puglSetViewHint(g->view, PUGL_CONTEXT_VERSION_MINOR, 3);
	puglSetViewHint(g->view, PUGL_CONTEXT_PROFILE, PUGL_OPENGL_CORE_PROFILE);
	puglSetViewHint(g->view, PUGL_DOUBLE_BUFFER, 1);
	puglSetViewHint(g->view, PUGL_RESIZABLE, 0);
#ifdef __APPLE__
	g->scale = puglGetScaleFactor(g->view);
#endif
	applySize(g);
	puglSetEventFunc(g->view, onEvent);
	return g;
}

void tack_guiDestroy(TackGui* g)
{
	if(!g) return;
	if(g->view) {
		puglUnrealize(g->view);
		puglFreeView(g->view);
	}
	if(g->world) puglFreeWorld(g->world);
	ImGui::DestroyContext(g->imgui);
	tack_freeKits(&g->kits);
	delete g;
}

void tack_guiGetSize(const TackGui* g, uint32_t* width, uint32_t* height)
{
#ifdef __APPLE__
	// Cocoa sizes are in points
	*width = windowWidth();
	*height = windowHeight(g->host.voice);
#else
	pixelSize(g, width, height);
#endif
}

bool tack_guiSetScale(TackGui* g, double scale)
{
#ifdef __APPLE__
	return false;
#else
	if(!(scale >= 0.5 && scale <= 4)) return false;
	g->scale = scale;
	applySize(g);
	return true;
#endif
}

bool tack_guiSetParent(TackGui* g, uintptr_t parent)
{
	puglSetParent(g->view, (PuglNativeView)parent);
	return puglRealize(g->view) == PUGL_SUCCESS;
}

bool tack_guiShow(TackGui* g)
{
	return puglShow(g->view, PUGL_SHOW_PASSIVE) == PUGL_SUCCESS;
}

bool tack_guiHide(TackGui* g)
{
	return puglHide(g->view) == PUGL_SUCCESS;
}

void tack_guiIdle(TackGui* g)
{
	puglObscureView(g->view);
	// elsewhere the host's event loop delivers the window's events, and this would run it
	if(!TACK_GUI_OWN_TIMER) puglUpdate(g->world, 0);
}
