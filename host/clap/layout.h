/*
 * layout.h
 *
 * Where the editor's boxes and controls go, in editor units (pixels at scale 1), worked out without drawing. The
 * editor draws from it; tools/layout_test.cpp checks it against the rules.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once
#include <stddef.h>
#include <stdint.h>
#include <vector>
#include "../src/lxr_params.h"

namespace layout {

constexpr float CELL_W = 64, KNOB = 36, BOX_PAD = 8, RULE_Y = 16, LABEL_H = 14, BAND_GAP = 4;
constexpr float CONTROL_CY = LABEL_H + BAND_GAP + KNOB / 2, VALUE_Y = LABEL_H + BAND_GAP + KNOB + BAND_GAP;
constexpr float CELL_H = VALUE_Y + LABEL_H;
constexpr float CELLS_Y = BOX_PAD + RULE_Y + BOX_PAD;
// ImGui's pixel font, at its own size
constexpr float FONT_SIZE = 13, FRAME_PAD_X = 4, FRAME_PAD_Y = 3, FRAME_H = FONT_SIZE + 2 * FRAME_PAD_Y;
constexpr float WIDGET_INSET = 2, TEXT_INSET = 3;
constexpr float BOX_H = CELLS_Y + CELL_H + BOX_PAD, GAP = 6, MARGIN = 10;
// between a label's words: narrower than the font's space, so that "Vel Dest" reads as one label next to "Vel Amt"
constexpr float WORD_GAP = 3;
// the header's buttons and tabs are all HEADER_ITEM_H high
constexpr float TOP_BAND = 4, HEADER_ROW_Y = TOP_BAND + GAP, HEADER_ITEM_H = 23, HEADER_BUTTON_W = 52, KIT_BUTTON_W = 120;
constexpr float HEADER_ITEM_GAP = 4, HEADER_GROUP_GAP = 16, DIE = 13;
constexpr float TABS_Y = HEADER_ROW_Y + HEADER_ITEM_H + GAP, TAB_LINE = 2;
// the logo's letters are LOGO_COLS x LOGO_ROWS squares of LOGO_CELL, LOGO_CELL apart
constexpr float LOGO_CELL = 3;
constexpr int LOGO_COLS = 5, LOGO_ROWS = 7, LOGO_LETTERS = 4;
constexpr float LOGO_W = LOGO_LETTERS * (LOGO_COLS + 1) * LOGO_CELL - LOGO_CELL;
constexpr int ROWS = 3;

/** Plugin parameter id of the outputs switch, which isn't a kit parameter; the editor's own setting. */
constexpr uint16_t OUTPUTS_ID = 1000;

struct Rect
{
	float x, y, w, h;
	float right() const { return x + w; }
	float bottom() const { return y + h; }
};

enum Kind { KNOB_CELL, LIST_CELL, SWITCH_CELL, SYNC_CELL, OUTPUTS_CELL };

/** A control and its three bands, relative to its box's top left corner. */
struct Cell
{
	const LxrParamInfo* param;
	const char* label;	// the full name; the cell shows shortLabel(label)
	Kind kind;
	Rect cell, labelBand, control, valueBand;	// valueBand is empty but for knobs
};

/** A box, relative to the window's top left corner, and its cells. */
struct Box
{
	const char* title;
	int row;
	Rect rect;
	int slotCells;	// the cells the slot has room for; the box has as many or fewer
	std::vector<Cell> cells;
};

struct Tab
{
	float width, height;
	std::vector<Box> boxes;
};

/**
 * The boxes of a tab: 0-5 a voice, LXR_PARAMS_FX_VOICE the FX bus and the mixer (fxType 0-5 its FX type). A voice
 * plugin is pluginVoice 0-5 and has only the parameters has() says it has; the whole kit is -1, has NULL.
 */
Tab tab(int pluginVoice, int tab, uint8_t fxType, bool (*has)(void* ctx, uint32_t param), void* ctx);

/** The window's size, the same for every tab. */
float windowWidth();
float windowHeight(int pluginVoice);
float headerHeight(int pluginVoice);
/** 0-5 a voice's tab, LXR_PARAMS_FX_VOICE Mix & FX's. */
Rect tabRect(int tab);

/** textWidth measures the text from begin to end in the editor's font. */
float labelWidth(const char* label, float (*textWidth)(const char* begin, const char* end));

/** The label a cell shows: the full name, or a short form of it. */
const char* shortLabel(const char* label);
/** What a closed list shows of a value: its name, or a short form of it ("V1 1x" for "V1 once"). */
void shortValue(const char* value, char* out, size_t size);

extern const char* const tabNames[LXR_PARAMS_FX_VOICE + 1];
/** The outputs switch's entry, in the Master box on Mix & FX. */
extern const LxrParamInfo outputsInfo;

}
