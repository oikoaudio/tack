/*
 * layout.h
 *
 * Where the editor puts its boxes and controls, worked out from the numbers below without drawing anything: the editor
 * draws from it, and tools/layout_test.cpp checks it against the rules.
 *
 * The spec, in editor units (pixels at scale 1):
 *
 * - A tab has three rows of boxes. Every row is as wide as the widest row of any tab, MARGIN from the window's edges,
 *   with GAP between boxes and between rows. A row's spare width goes to its boxes by their number of cells, so all
 *   the cells of a row are the same width.
 * - A box holds one front panel sub-page: a title, then one row of cells, BOX_PAD inside its border. Each sub-page has
 *   a slot as many cells wide as it has controls on any voice, so a voice with fewer leaves the end of its box empty,
 *   and nothing moves between tabs.
 * - Inside a box, BOX_PAD separates every line from what is next to it: the top border from the title, the title's
 *   rule (RULE_Y under the title's top) from the labels, and the knobs' values from the bottom border.
 * - A cell is CELL_H high, exactly its three bands: its label on top (LABEL_H), its control centred on CONTROL_CY, and
 *   a knob's value under the knob (VALUE_Y, LABEL_H), BAND_GAP between them. Lists and switches are FRAME_H high and as wide as the cell less
 *   2 * WIDGET_INSET.
 * - A control is a knob for an amount, a switch (a button that names the setting) for an option with two values, and a
 *   list for one with more. An LFO's Sync is a switch, and the outputs switch too.
 * - Text keeps TEXT_INSET from the sides of its band, and inside a switch FRAME_PAD_X from its sides. A list's text
 *   starts FRAME_PAD_X in and runs up to its arrow, which takes FRAME_H of its width. Everything is at the font's one
 *   size. Every label fits its cell, shortened (shortLabel) where the name is too long; the tooltip has the name. Every
 *   value of a switch and a knob fits too, and of a list but the modulation destinations', whose names are long. A
 *   closed list shows a value's short form (shortValue), and the open list and the tooltip its name.
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
// the cells' top in a box: the title, its rule and the space under it
constexpr float CELLS_Y = BOX_PAD + RULE_Y + BOX_PAD;
// ImGui's pixel font, at its size; a list's or a switch's frame is the font's height and FRAME_PAD_Y above and below it
constexpr float FONT_SIZE = 13, FRAME_PAD_X = 4, FRAME_PAD_Y = 3, FRAME_H = FONT_SIZE + 2 * FRAME_PAD_Y;
constexpr float WIDGET_INSET = 2, TEXT_INSET = 3;
constexpr float BOX_H = CELLS_Y + CELL_H + BOX_PAD, GAP = 6, MARGIN = 10;
// the header: the plugin's name, the kit and Mutate, then the tabs; a voice plugin has no tabs
constexpr float HEADER_H = 66, TAB_ROW_H = 25;
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

/** The label a cell shows: the full name, or a short form of it. */
const char* shortLabel(const char* label);
/** What a closed list shows of a value: its name, or a short form of it ("V1 1x" for "V1 once"). */
void shortValue(const char* value, char* out, size_t size);

extern const char* const tabNames[LXR_PARAMS_FX_VOICE + 1];
/** The outputs switch's entry, in the Master box on Mix & FX. */
extern const LxrParamInfo outputsInfo;

}
