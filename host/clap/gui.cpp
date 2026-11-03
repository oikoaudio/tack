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
#include <cctype>
#include <ctime>
#include <algorithm>
#include <vector>
#include <pugl/pugl.h>
#include <pugl/gl.h>
#include "imgui.h"
#include "backends/imgui_impl_opengl3.h"

#include "lxr_params.h"
#include "kit_name.h"
#include "lfo_speed.h"
#include "presets.h"
#include "layout.h"

template<typename T> static T clampTo(T v, T lo, T hi) { return v < lo ? lo : (v > hi ? hi : v); }
static const float PI = 3.14159265f;

// where the boxes and cells go and what kind of control each is: layout.h
using namespace layout;
static_assert(OUTPUTS_ID == TACK_GUI_SEPARATE_OUTPUTS_ID, "the outputs switch's id");

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

	// what the last Mutate step changed: the whole kit (-1) or a voice, so that its button shows whose walk it is
	int mutateScope = -1;
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

// a point of a layout rect on the screen; o is the screen position of what the rect is relative to
static ImVec2 at(ImVec2 o, float x, float y) { return ImVec2(o.x + x, o.y + y); }

// draws a cell's short label centred in its band
static void drawLabel(const char* label, ImVec2 o, const Rect& band, ImU32 color)
{
	const char* text = shortLabel(label);
	const float w = ImGui::CalcTextSize(text).x;
	const ImVec4 clip(o.x + band.x, o.y + band.y, o.x + band.right(), o.y + band.bottom() + 2);
	ImGui::GetWindowDrawList()->AddText(nullptr, 0, at(o, band.x + std::max(0.f, (band.w - w) * 0.5f), band.y), color, text,
		nullptr, 0, &clip);
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

// a knob for a stepped parameter: drag vertically (Shift for fine), wheel steps by one, double-click resets. p is the
// cell's parameter, or the one it stands for (an LFO's Freq in sync turns its Sync's fractions)
static void knob(TackGui* g, const Cell& cell, const LxrParamInfo* p, ImVec2 o)
{
	ImGuiIO& io = ImGui::GetIO();
	ImDrawList* dl = ImGui::GetWindowDrawList();
	const double v = value(g, p->param);
	// an LFO in sync: this is its Freq, turning through the fractions. 0 is the Sync switch's
	const bool fraction = lxr_params_lfoFreqOf(p->param) >= 0;
	const int lo = fraction ? 1 : p->min;
	const double range = p->max - lo;

	// dimmed, but still editable, while it has no effect
	const bool inactive = isPulseWidth(p) && !voiceUsesPwm(g, p->voice);
	if(inactive) ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.35f);
	ImGui::PushID(p->param);
	ImGui::SetCursorScreenPos(at(o, cell.cell.x, cell.cell.y));
	ImGui::InvisibleButton("knob", ImVec2(cell.cell.w, cell.cell.h));
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
		acc = clampTo(acc, (float)lo, (float)p->max);
		store->SetFloat(accumId, acc);
		const double stepped = std::round(acc);
		if(stepped != v) g->host.setValue(g->host.ctx, p->param, stepped);
	}
	if(ImGui::IsItemDeactivated()) g->host.endEdit(g->host.ctx, p->param);
	if(hovered && !fraction && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
		edit(g, p->param, lxr_params_default(p->param));
	if(hovered && io.MouseWheel != 0 && !ImGui::IsItemActive()) {
		const double nv = clampTo(v + (io.MouseWheel > 0 ? 1 : -1), (double)lo, (double)p->max);
		if(nv != v) edit(g, p->param, nv);
	}

	// drawing: an arc from 7 to 5 o'clock, filled up to the value
	const Rect& k = cell.control;
	const ImVec2 c = at(o, k.x + k.w * 0.5f, k.y + k.h * 0.5f);
	const float r = k.w * 0.5f;
	const float a0 = 0.75f * PI, a1 = 2.25f * PI;
	const float t = range > 0 ? (float)((v - lo) / range) : 0;
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
		const float tm = (float)clampTo((v + m - lo) / range, 0.0, 1.0);
		const float am = a0 + (a1 - a0) * tm;
		dl->PathArcTo(c, r + 1.5f, std::min(a, am), std::max(a, am), 32);
		dl->PathStroke(ImGui::GetColorU32(ImGuiCol_PlotHistogram), ImDrawFlags_None, 2.f);
	}

	// label and value, centred and clipped to their bands. The value is dim, and bright on the knob the pointer is on
	char text[64];
	if(fraction) snprintf(text, sizeof text, "%s", lfo_fraction_name((uint8_t)v));
	else valueText(g, p->param, (uint16_t)v, text, sizeof text);
	drawLabel(cell.label, o, cell.labelBand, ImGui::GetColorU32(ImGuiCol_Text));
	const Rect& vb = cell.valueBand;
	const ImVec4 clip(o.x + vb.x, o.y + vb.y, o.x + vb.right(), o.y + vb.bottom());
	const float vw = ImGui::CalcTextSize(text).x;
	dl->AddText(nullptr, 0, at(o, vb.x + std::max(0.f, (vb.w - vw) * 0.5f), vb.y),
		ImGui::GetColorU32(hovered || ImGui::IsItemActive() ? ImGuiCol_Text : ImGuiCol_TextDisabled), text, nullptr, 0, &clip);
	if(!ImGui::IsItemActive()) {
		if(inactive) tooltip("Pulse Width: only affects waveforms set to PWM", nullptr);
		else tooltip(cell.label, vw > vb.w ? text : nullptr);
	}
	ImGui::PopID();
	if(inactive) ImGui::PopStyleVar();
}

// a switch: a button that names the setting, for an option with two values. A click changes it; lit for one that is on
static bool switchButton(const Cell& cell, ImVec2 o, const char* text, bool lit)
{
	drawLabel(cell.label, o, cell.labelBand, ImGui::GetColorU32(ImGuiCol_Text));
	ImGui::SetCursorScreenPos(at(o, cell.control.x, cell.control.y));
	if(lit) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
	const bool clicked = ImGui::Button(text, ImVec2(cell.control.w, cell.control.h));
	if(lit) ImGui::PopStyleColor();
	return clicked;
}

// An LFO's Sync: on or off, and each way the other control takes the nearest speed at the host's tempo
static void syncSwitch(TackGui* g, const Cell& cell, ImVec2 o)
{
	const LxrParamInfo* p = cell.param;
	const uint8_t v = (uint8_t)value(g, p->param);
	const uint16_t freq = (uint16_t)lxr_params_lfoFreqOf(p->param);
	const float tempo = g->host.tempo(g->host.ctx);
	ImGui::PushID(p->param);
	if(switchButton(cell, o, v ? "On" : "Off", v)) {
		if(v) {
			edit(g, freq, lfo_freq_near(v, tempo));
			edit(g, p->param, 0);
		} else {
			edit(g, p->param, lfo_sync_near((uint8_t)value(g, freq), tempo));
		}
	}
	tooltip("Sync: Freq follows the tempo as a fraction of the bar", nullptr);
	ImGui::PopID();
}

static void outputsSwitch(TackGui* g, const Cell& cell, ImVec2 o)
{
	const bool individual = value(g, OUTPUTS_ID) >= 0.5;
	ImGui::PushID(OUTPUTS_ID);
	if(switchButton(cell, o, individual ? "Individual" : "Stereo", false)) edit(g, OUTPUTS_ID, individual ? 0 : 1);
	tooltip(individual ? "Individual: each Direct voice on its own output, and Main has the voices that go through the FX"
		: "Stereo: everything on Main", nullptr);
	ImGui::PopID();
}

// an option with two values, such as Direct or FX. It is lit when it is on, for an option that is off or on
static void toggle(TackGui* g, const Cell& cell, ImVec2 o)
{
	const LxrParamInfo* p = cell.param;
	const uint16_t v = (uint16_t)value(g, p->param);
	char current[64], first[64];
	valueText(g, p->param, v, current, sizeof current);
	valueText(g, p->param, p->min, first, sizeof first);
	ImGui::PushID(p->param);
	if(switchButton(cell, o, current, v != p->min && !strcmp(first, "Off"))) edit(g, p->param, v == p->min ? p->max : p->min);
	tooltip(cell.label, current);
	ImGui::PopID();
}

// a drop-down list for an option with more than two values
static void selector(TackGui* g, const Cell& cell, ImVec2 o)
{
	const LxrParamInfo* p = cell.param;
	const uint16_t v = (uint16_t)value(g, p->param);
	char current[64];
	valueText(g, p->param, v, current, sizeof current);

	ImGui::PushID(p->param);
	drawLabel(cell.label, o, cell.labelBand, ImGui::GetColorU32(ImGuiCol_Text));
	ImGui::SetCursorScreenPos(at(o, cell.control.x, cell.control.y));
	ImGui::SetNextItemWidth(cell.control.w);
	// the list may need more room than the box, e.g. for modulation destinations
	ImGui::SetNextWindowSizeConstraints(ImVec2(cell.cell.w, 0), ImVec2(320, 400));
	char shown[64];
	shortValue(current, shown, sizeof shown);
	if(ImGui::BeginCombo("##v", shown, ImGuiComboFlags_HeightLarge)) {
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
	if(!ImGui::IsPopupOpen("##v", ImGuiPopupFlags_AnyPopupId)) tooltip(cell.label, current);
	ImGui::PopID();
}

// one front panel sub-page: its title in capitals and the voice's colour over a thin rule, so that it reads as the
// box's heading, then its cells
static void drawBox(TackGui* g, const Box& b)
{
	ImGui::SetCursorPos(ImVec2(b.rect.x, b.rect.y));
	ImGui::PushID(b.title);
	ImGui::BeginChild("box", ImVec2(b.rect.w, b.rect.h), ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar);
	const ImVec2 o = ImGui::GetWindowPos();
	ImDrawList* dl = ImGui::GetWindowDrawList();
	char heading[32];
	size_t len = 0;
	for(; b.title[len] && len < sizeof heading - 1; len++) heading[len] = (char)toupper((unsigned char)b.title[len]);
	heading[len] = 0;
	const ImVec4 colour = voiceColour(g->tab);
	dl->AddText(at(o, BOX_PAD, BOX_PAD), ImGui::GetColorU32(colour), heading);
	dl->AddLine(at(o, BOX_PAD, BOX_PAD + RULE_Y), at(o, b.rect.w - BOX_PAD, BOX_PAD + RULE_Y),
		ImGui::GetColorU32(darker(colour, 0.4f)));
	for(const Cell& c : b.cells) {
		switch(c.kind) {
		case SYNC_CELL: syncSwitch(g, c, o); break;
		case OUTPUTS_CELL: outputsSwitch(g, c, o); break;
		case SWITCH_CELL: toggle(g, c, o); break;
		case LIST_CELL: selector(g, c, o); break;
		case KNOB_CELL: {
			// an LFO's Freq in sync turns through its Sync's fractions
			const int sync = lxr_params_lfoSyncOf(c.param->param);
			knob(g, c, sync >= 0 && value(g, (uint32_t)sync) != 0 ? lxr_params_info(lxr_params_indexOf((uint16_t)sync)) : c.param, o);
			break;
		}
		}
	}
	ImGui::EndChild();
	ImGui::PopID();
}

//-----------------------------------------------------------
// a tab: its boxes, as the layout has them

static bool guiHas(void* ctx, uint32_t id)
{
	const TackGui* g = (const TackGui*)ctx;
	return g->host.hasParam(g->host.ctx, id);
}

static void drawTab(TackGui* g)
{
	for(const Box& b : tab(g->host.voice, g->tab, (uint8_t)value(g, lxr_params_fxTypeParam()), guiHas, g).boxes)
		drawBox(g, b);
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

// Mutate: a step for the whole kit or for the voice on the open tab, a step back, and how far the walk has come. A walk
// follows the same way each time, so after a step back the next step comes back along it. The button of the walk the
// count belongs to is lit
static void drawMutate(TackGui* g)
{
	const int step = g->host.mutateStep(g->host.ctx);
	// a voice plugin has only its voice: one button, which says what it does
	if(g->host.voice < 0) {
		ImGui::AlignTextToFramePadding();
		ImGui::TextDisabled("Mutate");
	}
	bool first = g->host.voice >= 0;
	auto stepButton = [&](const char* text, int scope, const char* tip) {
		const bool lit = step && g->mutateScope == scope;
		if(!first) ImGui::SameLine(0, 4);
		first = false;
		if(lit) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
		if(ImGui::Button(text)) {
			g->host.mutate(g->host.ctx, scope, 1);
			g->mutateScope = scope;
		}
		if(lit) ImGui::PopStyleColor();
		if(ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
	};
	if(g->host.voice < 0) {
		stepButton("Kit", -1, "A small random change to the whole kit. Levels, pans and the FX stay");
		ImGui::BeginDisabled(g->tab >= LXR_PARAMS_FX_VOICE);
		stepButton("Voice", g->tab, "A small random change to the voice on the open tab");
		ImGui::EndDisabled();
	} else {
		stepButton("Mutate", -1, "A small random change to the voice");
	}
	ImGui::SameLine(0, 4);
	ImGui::BeginDisabled(!step);
	if(ImGui::ArrowButton("mutateBack", ImGuiDir_Left)) g->host.mutate(g->host.ctx, TACK_MUTATE_AGAIN, -1);
	if(ImGui::IsItemHovered()) ImGui::SetTooltip("One step back");
	ImGui::EndDisabled();
	ImGui::SameLine(0, 6);
	ImGui::AlignTextToFramePadding();
	if(step) ImGui::Text("%d", step);
	else ImGui::TextDisabled("0");
	if(ImGui::IsItemHovered()) ImGui::SetTooltip("Steps from the kit as it was");
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
	if(ImGui::Button("Save")) {
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
	ImGui::SameLine(0, 16);
	drawMutate(g);
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
	// lists and switches are the layout's height
	s.FramePadding = ImVec2(FRAME_PAD_X, FRAME_PAD_Y);
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
	// the layout's font: ImGui's pixel font at its own size
	ImGui::GetIO().Fonts->AddFontDefaultBitmap();
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
