/*
 * gui.h
 *
 * Tack's editor: a tab per voice plus FX, each showing the front panel's sub-pages as boxes of up to 4x2 controls.
 * Runs on the host's main thread; the host drives it with tack_guiIdle() from a timer.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** How the editor reads and changes the plugin. Values are plugin parameter values (see lxr_params.h). */
typedef struct
{
	void* ctx;
	double (*getValue)(void* ctx, uint32_t id);
	void (*beginEdit)(void* ctx, uint32_t id);
	void (*setValue)(void* ctx, uint32_t id, double value);
	void (*endEdit)(void* ctx, uint32_t id);
	/** plays a voice: 0-5 as in the engine, 6 the open hi-hat */
	void (*audition)(void* ctx, uint8_t voice);
	const char* (*kitName)(void* ctx);
	/** saves the kit under name to the user pack; returns a TACK_SAVE_ result (presets.h) */
	int (*saveKit)(void* ctx, const char* name, bool overwrite);
	/** the kit file the kit came from; empty for the init kit or if unknown */
	const char* (*kitPath)(void* ctx);
	/** whether the kit was edited in the editor since it was loaded or saved */
	bool (*kitEdited)(void* ctx);
	/** loads a kit file, or the init kit if path is NULL; false if it couldn't be read */
	bool (*loadKit)(void* ctx, const char* path);
} TackGuiHost;

/** Plugin parameter id of the outputs switch (0 stereo, 1 individual), which the editor also shows. */
#define TACK_GUI_SEPARATE_OUTPUTS_ID 1000

typedef struct TackGui TackGui;

TackGui* tack_guiCreate(const TackGuiHost* host);
void tack_guiDestroy(TackGui* gui);
void tack_guiGetSize(uint32_t* width, uint32_t* height);
/** Embeds the editor in a host window (an X11 window id) and creates it. */
bool tack_guiSetParent(TackGui* gui, uintptr_t parent);
bool tack_guiShow(TackGui* gui);
bool tack_guiHide(TackGui* gui);
/** Handles window events and redraws; call regularly. */
void tack_guiIdle(TackGui* gui);

#ifdef __cplusplus
}
#endif
