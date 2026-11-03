/*
 * layout_test.cpp
 *
 * Checks the editor's layout against its spec (clap/layout.h) on every tab: the whole kit's with each FX type, and the
 * voice plugins'. It measures text with the editor's font in an ImGui context of its own, and draws nothing.
 *
 *   layout_test
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <string>
#include "imgui.h"
#include "../clap/layout.h"
#include "lfo_speed.h"

using namespace layout;

static int failures;
static std::string where;

static void check(bool ok, const char* what, const char* detail = "")
{
	if(ok) return;
	if(++failures <= 40) printf("FAIL %s: %s %s\n", where.c_str(), what, detail);
}

static float textWidth(const char* s) { return ImGui::CalcTextSize(s).x; }

// a overlaps b: they share more than an edge
static bool overlaps(const Rect& a, const Rect& b)
{
	return a.x < b.right() && b.x < a.right() && a.y < b.bottom() && b.y < a.bottom();
}

static bool inside(const Rect& r, float x0, float y0, float x1, float y1)
{
	return r.x >= x0 && r.y >= y0 && r.right() <= x1 + 0.01f && r.bottom() <= y1 + 0.01f;
}

static bool same(float a, float b) { return a > b - 0.01f && a < b + 0.01f; }

// the widest of a parameter's value texts, or of their short forms, and which
static float widestValue(const LxrParamInfo* p, char* which, size_t size, bool shortForm = false)
{
	float widest = 0;
	for(int v = p->min; v <= p->max; v++) {
		char name[96], text[96];
		lxr_params_valueText(p->param, (uint16_t)v, name, sizeof name);
		if(shortForm) shortValue(name, text, sizeof text);
		else snprintf(text, sizeof text, "%s", name);
		if(textWidth(text) > widest) { widest = textWidth(text); snprintf(which, size, "%s", text); }
	}
	return widest;
}

static void checkCell(const Box& b, const Cell& c)
{
	const LxrParamInfo* p = c.param;
	char detail[160];
	snprintf(detail, sizeof detail, "(%s)", c.label);

	// the kind of control for the parameter
	Kind kind = !p->isEnum ? KNOB_CELL : p->max - p->min == 1 ? SWITCH_CELL : LIST_CELL;
	if(lxr_params_lfoFreqOf(p->param) >= 0) kind = SYNC_CELL;
	if(p->param == OUTPUTS_ID) kind = OUTPUTS_CELL;
	check(c.kind == kind, "the kind of control", detail);

	// the three bands, in order, inside the cell and the box's padding
	// BOX_PAD between the rule and the labels, and between the cells and the bottom border
	check(same(c.cell.y - (BOX_PAD + RULE_Y), BOX_PAD) && same(c.cell.h, CELL_H), "the cell BOX_PAD under the rule", detail);
	check(same(b.rect.h - c.cell.bottom(), BOX_PAD), "the cell BOX_PAD above the bottom border", detail);
	check(inside(c.cell, BOX_PAD, BOX_PAD, b.rect.w - BOX_PAD, b.rect.h - BOX_PAD), "the cell inside the padding", detail);
	check(same(c.labelBand.y, c.cell.y) && same(c.labelBand.h, LABEL_H) && same(c.labelBand.w, c.cell.w),
		"the label band on top", detail);
	check(same(c.control.y + c.control.h / 2, c.cell.y + CONTROL_CY), "the control on the control centre", detail);
	check(same(c.control.x + c.control.w / 2, c.cell.x + c.cell.w / 2), "the control centred", detail);
	check(inside(c.control, c.cell.x, c.cell.y, c.cell.right(), c.cell.bottom()), "the control inside the cell", detail);
	check(c.labelBand.bottom() <= c.control.y, "the label above the control", detail);
	if(c.kind == KNOB_CELL) {
		check(same(c.control.w, KNOB) && same(c.control.h, KNOB), "a knob's size", detail);
		check(same(c.valueBand.y, c.cell.y + VALUE_Y) && same(c.valueBand.h, LABEL_H), "the value band", detail);
		check(same(c.valueBand.bottom(), c.cell.bottom()), "the value band at the cell's bottom", detail);
		check(c.control.bottom() <= c.valueBand.y, "the value under the knob", detail);
		check(inside(c.valueBand, c.cell.x, c.cell.y, c.cell.right(), c.cell.bottom()), "the value inside the cell", detail);
	} else {
		check(same(c.control.h, FRAME_H) && same(c.control.w, c.cell.w - 2 * WIDGET_INSET), "a list's or switch's size",
			detail);
		check(c.valueBand.w == 0, "no value band", detail);
	}

	// the text fits
	const char* label = shortLabel(c.label);
	check(textWidth(label) <= c.labelBand.w - 2 * TEXT_INSET, "the label fits", detail);
	char which[96] = "";
	switch(c.kind) {
	case KNOB_CELL: {
		float widest = widestValue(p, which, sizeof which);
		// an LFO's Freq in sync shows its Sync's fractions
		if(lxr_params_lfoSyncOf(p->param) >= 0)
			for(uint8_t v = 1; v < 16; v++)
				if(textWidth(lfo_fraction_name(v)) > widest) {
					widest = textWidth(lfo_fraction_name(v));
					snprintf(which, sizeof which, "%s", lfo_fraction_name(v));
				}
		snprintf(detail, sizeof detail, "(%s: %s)", c.label, which);
		check(widest <= c.valueBand.w - 2 * TEXT_INSET, "a knob's value fits", detail);
		break;
	}
	case SWITCH_CELL:
		snprintf(detail, sizeof detail, "(%s: %s)", c.label, (widestValue(p, which, sizeof which), which));
		check(widestValue(p, which, sizeof which) <= c.control.w - 2 * FRAME_PAD_X, "a switch's value fits", detail);
		break;
	case SYNC_CELL:
		check(textWidth("Off") <= c.control.w - 2 * FRAME_PAD_X, "Sync's value fits", detail);
		break;
	case OUTPUTS_CELL:
		check(textWidth("Individual") <= c.control.w - 2 * FRAME_PAD_X, "the outputs switch's value fits", detail);
		break;
	case LIST_CELL:
		if(lxr_params_isTarget(p->param)) break;
		snprintf(detail, sizeof detail, "(%s: %s)", c.label, (widestValue(p, which, sizeof which, true), which));
		check(widestValue(p, which, sizeof which, true) <= c.control.w - FRAME_H - FRAME_PAD_X, "a list's value fits",
			detail);
		break;
	}
}

static void checkTab(int pluginVoice, int t, uint8_t fxType)
{
	char name[64];
	if(t == LXR_PARAMS_FX_VOICE) snprintf(name, sizeof name, "Tack, %s, FX type %d", tabNames[t], fxType);
	else snprintf(name, sizeof name, "%s, %s", pluginVoice < 0 ? "Tack" : tabNames[pluginVoice], tabNames[t]);
	const Tab tb = tab(pluginVoice, t, fxType, nullptr, nullptr);
	check(same(tb.width, windowWidth()) && same(tb.height, windowHeight(pluginVoice)), "the window's size");
	check(!tb.boxes.empty(), "boxes");

	float rowCellW[ROWS] = { 0, 0, 0 };
	for(size_t i = 0; i < tb.boxes.size(); i++) {
		const Box& b = tb.boxes[i];
		where = std::string(name) + ", " + b.title;
		// in its row, inside the margins, clear of the other boxes
		check(same(b.rect.y, headerHeight(pluginVoice) + b.row * (BOX_H + GAP)) && same(b.rect.h, BOX_H), "the box in its row");
		check(inside(b.rect, MARGIN, headerHeight(pluginVoice), tb.width - MARGIN, tb.height - MARGIN), "the box inside the margins");
		for(size_t j = 0; j < i; j++)
			if(tb.boxes[j].row == b.row) {
				const Rect& o = tb.boxes[j].rect;
				check(!overlaps(b.rect, o) && (b.rect.x >= o.right() + GAP - 0.01f || o.x >= b.rect.right() + GAP - 0.01f),
					"a gap to the next box");
			}
		// the title over its rule, inside the padding
		char heading[64];
		size_t n = 0;
		for(; b.title[n] && n < sizeof heading - 1; n++) heading[n] = (char)toupper((unsigned char)b.title[n]);
		heading[n] = 0;
		check(textWidth(heading) <= b.rect.w - 2 * BOX_PAD, "the title fits");
		check(FONT_SIZE <= RULE_Y, "the rule under the title");
		// one row of cells of one width, side by side from the padding, as wide as the slot allows
		check(!b.cells.empty() && (int)b.cells.size() <= b.slotCells, "the cells fit the slot");
		for(size_t c = 0; c < b.cells.size(); c++) {
			const Cell& cell = b.cells[c];
			check(same(cell.cell.w, b.cells[0].cell.w), "cells of one width");
			check(same(cell.cell.x, BOX_PAD + c * cell.cell.w), "cells side by side");
			checkCell(b, cell);
		}
		check(same(b.rect.w, b.slotCells * b.cells[0].cell.w + 2 * BOX_PAD), "the box as wide as its slot");
		float& w = rowCellW[b.row];
		if(w == 0) w = b.cells[0].cell.w;
		check(same(b.cells[0].cell.w, w), "the cells of a row of one width");
		check(b.cells[0].cell.w >= CELL_W - 0.01f, "cells at least CELL_W wide");
	}
}

int main()
{
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.IniFilename = nullptr;
	io.DisplaySize = ImVec2(windowWidth(), windowHeight(-1));
	io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
	io.Fonts->AddFontDefaultBitmap();
	ImGui::NewFrame();
	ImGui::PushFont(nullptr, FONT_SIZE);

	int tabs = 0;
	for(int t = 0; t < LXR_PARAMS_FX_VOICE; t++, tabs++) checkTab(-1, t, 0);
	for(uint8_t fx = 0; fx <= 5; fx++, tabs++) checkTab(-1, LXR_PARAMS_FX_VOICE, fx);
	for(int v = 0; v < LXR_PARAMS_NUM_VOICES; v++, tabs++) checkTab(v, v, 0);

	ImGui::PopFont();
	ImGui::EndFrame();
	ImGui::DestroyContext();
	if(failures) {
		printf("%d failures\n", failures);
		return 1;
	}
	printf("layout of %d tabs passed\n", tabs);
	return 0;
}
