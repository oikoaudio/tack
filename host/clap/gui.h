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
#include <stddef.h>

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
	/**
	 * loads a kit file, or the init kit if path is NULL; false if it couldn't be read. A voice plugin takes one voice
	 * of the kit (0-5; -1 its own), the whole kit plugin all of it (voice -1).
	 */
	bool (*loadKit)(void* ctx, const char* path, int voice);
	/** -1 for the whole kit; else the one voice (0-5) the plugin has, whose tab is all the editor shows */
	int voice;
	/** whether the plugin has a kit parameter */
	bool (*hasParam)(void* ctx, uint32_t id);
	/** whether a parameter's value is on offer (a single voice's destinations stay on the voice) */
	bool (*offersValue)(void* ctx, uint32_t id, uint16_t value);
	/** the text for a value, as the plugin names it */
	void (*valueText)(void* ctx, uint32_t id, uint16_t value, char* text, size_t size);
	/** the voice of the kit file a voice plugin plays (see loadKit), -1 for the whole kit */
	int (*kitVoice)(void* ctx);
	/** the host's modulation of a parameter, in plugin values, on top of its value */
	double (*modulation)(void* ctx, uint32_t id);
	/**
	 * the whole kit plugin: copies one voice of a kit file onto voice toVoice of the kit, keeping the other voices and
	 * where the voice plays (Direct or FX); false if the file couldn't be read
	 */
	bool (*loadVoice)(void* ctx, const char* path, int fromVoice, int toVoice);
} TackGuiHost;

/** Plugin parameter id of a voice plugin's chromatic switch, shown as a keyboard in the editor. */
#define TACK_GUI_CHROMATIC_ID 1001

/** Plugin parameter id of the outputs switch (0 stereo, 1 individual), which the editor also shows. */
#define TACK_GUI_SEPARATE_OUTPUTS_ID 1000

/**
 * X11 needs the host's timer to drive the editor: tack_guiIdle() handles its window events. On Windows and macOS
 * the host's event loop delivers them, and the editor redraws from a timer of its own.
 */
#if defined(_WIN32) || defined(__APPLE__)
#define TACK_GUI_OWN_TIMER 1
#else
#define TACK_GUI_OWN_TIMER 0
#endif

typedef struct TackGui TackGui;

TackGui* tack_guiCreate(const TackGuiHost* host);
void tack_guiDestroy(TackGui* gui);
/**
 * The editor's size, as the plugin API counts it: pixels on X11 and Windows, points on macOS. A voice plugin's is
 * shorter than the whole kit's.
 */
void tack_guiGetSize(const TackGui* gui, uint32_t* width, uint32_t* height);
/**
 * Scales the editor by the host's factor (X11 and Windows), also once it is showing; the host then needs the new
 * size. False on macOS, where the screen decides the scale, and for a factor out of range.
 */
bool tack_guiSetScale(TackGui* gui, double scale);
/** Embeds the editor in a host window (an X11 window id, an HWND or an NSView) and creates it. */
bool tack_guiSetParent(TackGui* gui, uintptr_t parent);
bool tack_guiShow(TackGui* gui);
bool tack_guiHide(TackGui* gui);
/** Handles window events and redraws; call regularly from the host's timer unless TACK_GUI_OWN_TIMER. */
void tack_guiIdle(TackGui* gui);

#ifdef __cplusplus
}
#endif
