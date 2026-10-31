/*
 * engine.h
 *
 * The sound engine keeps its state in globals, so every plugin instance runs its own copy of it: Tack loads a
 * private copy of its own binary per instance and drives the engine in that copy through this table. The copy's
 * internal references bind to its own globals because everything but the entry points has hidden visibility.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once
#include <stdint.h>
#include <stdbool.h>

/** The lxr_host functions that use engine state. */
typedef struct
{
	void (*init)(void);
	void (*loadKit)(const uint8_t* values);
	void (*setParam)(uint16_t param, uint8_t value);
	uint8_t (*getParam)(uint16_t param);
	int (*setOutputRate)(double fs);
	uint32_t (*latency)(void);
	void (*queueNoteOn)(uint32_t frame, uint8_t voice, uint8_t note, uint8_t vel);
	void (*queueParam)(uint32_t frame, uint16_t param, uint8_t value);
	void (*queueSeparateOutputs)(uint32_t frame, bool on);
	void (*setSeparateOutputs)(bool on);
	void (*process)(float* out, uint32_t frames);
	void (*shutdown)(void);
} TackEngine;

/** The engine of the binary this is called in. Exported, so that it can be looked up in a loaded copy. */
const TackEngine* tack_engine(void);

typedef struct
{
	const TackEngine* api;
	void* handle;	/**< the loaded copy, NULL when using this binary's own engine */
	int fd;			/**< the copy's in-memory file, kept open while it is loaded */
} TackEngineCopy;

/**
 * Loads a private copy of the engine. Falls back to this binary's own engine if no copy can be loaded and the own
 * engine isn't in use yet. Returns false if neither is available.
 */
bool tack_engineOpen(TackEngineCopy* engine);
void tack_engineClose(TackEngineCopy* engine);
