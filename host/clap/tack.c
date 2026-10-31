/*
 * tack.c
 *
 * Tack: a CLAP drum synth plugin. The DSP is derived from the Erica Synths LXR firmware, modified to run on a
 * desktop computer. No affiliation with or endorsement by Erica Synths.
 *
 * Starts with the factory init kit. On MIDI channel 1, notes from C3 (MIDI 60) up trigger the voices, each at its
 * tuned pitch; channels 2-8 each play one voice chromatically, C3 at its tuned pitch. Every sound parameter the
 * front panel offers is a plugin parameter, with the firmware parameter number as its id. The state is the kit
 * itself. Each instance drives its own copy of the engine (engine.c).
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <math.h>
#include <clap/clap.h>
#include "lxr_host.h"
#include "lxr_params.h"
#include "presets.h"
#include "engine.h"
#include "gui.h"

#define TACK_CHUNK	256

// state: magic, then the number of parameters, then the kit in firmware representation, then the separate
// outputs switch, the kit's name, whether it was edited since loading and the kit file it came from (each missing
// in states saved before it existed)
#define STATE_MAGIC		"KTL1"
#define KIT_NAME_SIZE	16
#define KIT_PATH_SIZE	1024

// plugin parameters that aren't part of the kit get ids past the firmware's parameter numbers
#define PARAM_SEPARATE_OUTPUTS	1000

// output ports and the k->engine.api->process() channels they carry. Main has everything, or with individual
// outputs the FX bus: the voices routed through the FX, processed together
static const struct { const char* name; int channel; } outputPorts[] =
{
	{ "Main", LXR_HOST_OUT_MAIN },
	{ "Drum 1", LXR_HOST_OUT_VOICE(0) },
	{ "Drum 2", LXR_HOST_OUT_VOICE(1) },
	{ "Drum 3", LXR_HOST_OUT_VOICE(2) },
	{ "Snare", LXR_HOST_OUT_VOICE(3) },
	{ "Cymbal", LXR_HOST_OUT_VOICE(4) },
	{ "Hi-hat", LXR_HOST_OUT_VOICE(5) },
};
#define NUM_OUTPUT_PORTS (sizeof outputPorts / sizeof outputPorts[0])

// changes made in the editor, passed from the main thread to whichever thread next processes or flushes
enum { GUI_BEGIN_EDIT, GUI_SET_VALUE, GUI_END_EDIT, GUI_NOTE_ON };
typedef struct
{
	uint8_t type;
	uint32_t id;	// parameter id, or the voice for GUI_NOTE_ON
	double value;
} GuiEvent;
#define GUI_QUEUE_SIZE 256
#define GUI_TIMER_MS 20

typedef struct
{
	clap_plugin_t plugin;
	const clap_host_t* host;
	TackEngineCopy engine;	// this instance's own copy of the sound engine
	const clap_host_params_t* hostParams;
	const clap_host_preset_load_t* hostPresetLoad;
	const clap_host_state_t* hostState;
	bool active;

	// the kit as the host sees it, in firmware representation. Written by whichever thread changes parameters
	uint8_t values[LXR_HOST_NUM_PARAMS];

	// a loaded state waiting for the audio thread
	uint8_t pendingKit[LXR_HOST_NUM_PARAMS];
	atomic_bool hasPendingKit;
	atomic_flag pendingLock;

	// everything on Main, or each direct voice on its own output and the FX bus on Main
	bool separateOutputs;
	atomic_int pendingSeparateOutputs;	// from a loaded state, for the audio thread; -1 if none

	// main thread
	char kitName[KIT_NAME_SIZE];
	char kitPath[KIT_PATH_SIZE];	// the kit file it was loaded from or saved to; empty for the init kit or unknown
	bool kitEdited;	// in the editor, since it was loaded or saved

	// the editor; its changes reach the engine and the host through guiEvents
	TackGui* gui;
	const clap_host_timer_support_t* hostTimer;
	clap_id guiTimer;
	GuiEvent guiEvents[GUI_QUEUE_SIZE];
	atomic_uint guiRead, guiWrite;

	float buf[TACK_CHUNK * LXR_HOST_OUT_CHANNELS];
} Tack;


// channel 1 (0 here) triggers the voices from this note up: C3 in the convention where middle C is C3 (Bitwig,
// Ableton, Cubase). Channels 2-8 play voices 0-6, the note setting the pitch.
#define FIRST_NOTE 60

static const char* features[] = {
	CLAP_PLUGIN_FEATURE_INSTRUMENT, CLAP_PLUGIN_FEATURE_DRUM_MACHINE, CLAP_PLUGIN_FEATURE_SYNTHESIZER,
	CLAP_PLUGIN_FEATURE_STEREO, NULL
};

static const clap_plugin_descriptor_t descriptor =
{
	.clap_version = CLAP_VERSION_INIT,
	.id = TACK_PLUGIN_ID,
	.name = "Tack",
	.vendor = "oikoaudio",
	.url = "",
	.manual_url = "",
	.support_url = "",
	.version = "0.1.0",
	.description = "Drum synth. DSP derived from the Erica Synths LXR firmware (modified); not affiliated with Erica Synths.",
	.features = features,
};

//-----------------------------------------------------------
// queued: called from process(), so the change lands lxr_host_latency() later like the notes around it
static void handleParamEvent(Tack* k, const clap_event_param_value_t* ev, uint32_t frame, bool queued)
{
	if(ev->param_id == PARAM_SEPARATE_OUTPUTS) {
		k->separateOutputs = ev->value >= 0.5;
		if(queued)
			k->engine.api->queueSeparateOutputs(frame, k->separateOutputs);
		else
			k->engine.api->setSeparateOutputs(k->separateOutputs);
		return;
	}
	if(lxr_params_indexOf(ev->param_id) < 0) return;
	const uint8_t value = lxr_params_toFirmware(ev->param_id, (uint16_t)lrint(fmax(ev->value, 0)));
	k->values[ev->param_id] = value;
	if(queued)
		k->engine.api->queueParam(frame, ev->param_id, value);
	else
		k->engine.api->setParam(ev->param_id, value);
}

static void handleEvent(Tack* k, const clap_event_header_t* ev, uint32_t frame)
{
	if(ev->space_id != CLAP_CORE_EVENT_SPACE_ID) return;

	if(ev->type == CLAP_EVENT_PARAM_VALUE) {
		handleParamEvent(k, (const clap_event_param_value_t*)ev, frame, true);
		return;
	}

	// note-off is ignored, as on the device: every voice is one-shot
	int channel = 0, note = -1, vel = 0;
	if(ev->type == CLAP_EVENT_NOTE_ON) {
		const clap_event_note_t* n = (const clap_event_note_t*)ev;
		channel = n->channel;
		note = n->key;
		vel = (int)lrint(n->velocity * 127.0);
	} else if(ev->type == CLAP_EVENT_MIDI) {
		const clap_event_midi_t* m = (const clap_event_midi_t*)ev;
		if((m->data[0] & 0xf0) == 0x90 && m->data[2] > 0) {
			channel = m->data[0] & 0x0f;
			note = m->data[1];
			vel = m->data[2];
		}
	}
	if(note < 0 || note > 127) return;

	vel = vel < 1 ? 1 : (vel > 127 ? 127 : vel);
	if(channel <= 0) {	// -1: the host didn't say
		if(note >= FIRST_NOTE && note < FIRST_NOTE + LXR_HOST_NUM_VOICES)
			k->engine.api->queueNoteOn(frame, note - FIRST_NOTE, LXR_HOST_BASE_NOTE, vel);
	} else if(channel <= LXR_HOST_NUM_VOICES) {
		k->engine.api->queueNoteOn(frame, channel - 1, note, vel);
	}
}
//-----------------------------------------------------------
static void render(Tack* k, const clap_process_t* p)
{
	const clap_audio_buffer_t* out = p->audio_outputs;
	uint32_t start = 0;
	const uint32_t end = p->frames_count;
	while(start < end)
	{
		const uint32_t n = end - start < TACK_CHUNK ? end - start : TACK_CHUNK;
		k->engine.api->process(k->buf, n);
		for(uint32_t port = 0; port < p->audio_outputs_count && port < NUM_OUTPUT_PORTS; port++)
		{
			const int c = outputPorts[port].channel;
			for(uint32_t i = 0; i < n; i++) {
				out[port].data32[0][start + i] = k->buf[i*LXR_HOST_OUT_CHANNELS + c];
				out[port].data32[1][start + i] = k->buf[i*LXR_HOST_OUT_CHANNELS + c + 1];
			}
		}
		start += n;
	}
}
//-----------------------------------------------------------
static void applyPendingKit(Tack* k)
{
	if(!atomic_load(&k->hasPendingKit) || atomic_flag_test_and_set(&k->pendingLock)) return;
	k->engine.api->loadKit(k->pendingKit);
	atomic_store(&k->hasPendingKit, false);
	atomic_flag_clear(&k->pendingLock);
}

// applies the editor's changes and tells the host about them, as if they were automation recorded at frame 0.
// queued: from process(), so they go through the engine's event queue like everything else there
static void applyGuiEvents(Tack* k, const clap_output_events_t* out, bool queued)
{
	unsigned r = atomic_load(&k->guiRead);
	const unsigned w = atomic_load(&k->guiWrite);
	for(; r != w; r++)
	{
		const GuiEvent* e = &k->guiEvents[r % GUI_QUEUE_SIZE];
		if(e->type == GUI_NOTE_ON) {
			if(k->active) k->engine.api->queueNoteOn(0, e->id, LXR_HOST_BASE_NOTE, 100);
			continue;
		}
		if(e->type == GUI_SET_VALUE) {
			const clap_event_param_value_t ev = {
				.header = { sizeof ev, 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_VALUE, 0 },
				.param_id = e->id, .note_id = -1, .port_index = -1, .channel = -1, .key = -1, .value = e->value,
			};
			handleParamEvent(k, &ev, 0, queued);
			out->try_push(out, &ev.header);
		} else {
			const clap_event_param_gesture_t ev = {
				.header = { sizeof ev, 0, CLAP_CORE_EVENT_SPACE_ID,
					e->type == GUI_BEGIN_EDIT ? CLAP_EVENT_PARAM_GESTURE_BEGIN : CLAP_EVENT_PARAM_GESTURE_END, 0 },
				.param_id = e->id,
			};
			out->try_push(out, &ev.header);
		}
	}
	atomic_store(&k->guiRead, r);
}

static clap_process_status tackProcess(const clap_plugin_t* plugin, const clap_process_t* p)
{
	Tack* k = plugin->plugin_data;
	const uint32_t numEvents = p->in_events->size(p->in_events);

	applyPendingKit(k);
	const int separate = atomic_exchange(&k->pendingSeparateOutputs, -1);
	if(separate >= 0) k->engine.api->setSeparateOutputs(separate);
	applyGuiEvents(k, p->out_events, true);

	// events are scheduled at their exact time and come out lxr_host_latency() frames later
	for(uint32_t e = 0; e < numEvents; e++)
	{
		const clap_event_header_t* ev = p->in_events->get(p->in_events, e);
		handleEvent(k, ev, ev->time < p->frames_count ? ev->time : p->frames_count);
	}
	render(k, p);
	return CLAP_PROCESS_CONTINUE;
}
//-----------------------------------------------------------
static uint32_t notePortsCount(const clap_plugin_t* plugin, bool isInput)
{
	return isInput ? 1 : 0;
}

static bool notePortsGet(const clap_plugin_t* plugin, uint32_t index, bool isInput, clap_note_port_info_t* info)
{
	if(!isInput || index != 0) return false;
	info->id = 0;
	info->supported_dialects = CLAP_NOTE_DIALECT_CLAP | CLAP_NOTE_DIALECT_MIDI;
	info->preferred_dialect = CLAP_NOTE_DIALECT_CLAP;
	strncpy(info->name, "Notes", sizeof info->name);
	return true;
}

static const clap_plugin_note_ports_t notePorts = { notePortsCount, notePortsGet };
//-----------------------------------------------------------
static uint32_t audioPortsCount(const clap_plugin_t* plugin, bool isInput)
{
	return isInput ? 0 : NUM_OUTPUT_PORTS;
}

static bool audioPortsGet(const clap_plugin_t* plugin, uint32_t index, bool isInput, clap_audio_port_info_t* info)
{
	if(isInput || index >= NUM_OUTPUT_PORTS) return false;
	info->id = index;
	snprintf(info->name, sizeof info->name, "%s", outputPorts[index].name);
	info->flags = index == 0 ? CLAP_AUDIO_PORT_IS_MAIN : 0;
	info->channel_count = 2;
	info->port_type = CLAP_PORT_STEREO;
	info->in_place_pair = CLAP_INVALID_ID;
	return true;
}

static const clap_plugin_audio_ports_t audioPorts = { audioPortsCount, audioPortsGet };
//-----------------------------------------------------------
static uint32_t latencyGet(const clap_plugin_t* plugin)
{
	Tack* k = plugin->plugin_data;
	return k->engine.api->latency();
}

static const clap_plugin_latency_t latency = { latencyGet };
//-----------------------------------------------------------
static uint32_t paramsCount(const clap_plugin_t* plugin)
{
	return lxr_params_count() + 1;
}

static bool paramsGetInfo(const clap_plugin_t* plugin, uint32_t index, clap_param_info_t* info)
{
	if(index == (uint32_t)lxr_params_count()) {
		memset(info, 0, sizeof *info);
		info->id = PARAM_SEPARATE_OUTPUTS;
		// switching outputs mid-song isn't something to automate
		info->flags = CLAP_PARAM_IS_STEPPED | CLAP_PARAM_IS_ENUM;
		snprintf(info->name, sizeof info->name, "Outputs");
		snprintf(info->module, sizeof info->module, "Outputs");
		info->max_value = 1;
		return true;
	}
	const LxrParamInfo* p = lxr_params_info(index);
	if(!p) return false;

	uint8_t kit[LXR_HOST_NUM_PARAMS];
	lxr_host_initKit(kit);

	memset(info, 0, sizeof *info);
	info->id = p->param;
	info->flags = CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_STEPPED | (p->isEnum ? CLAP_PARAM_IS_ENUM : 0);
	snprintf(info->name, sizeof info->name, "%s", p->name);
	snprintf(info->module, sizeof info->module, "%s", p->module);
	info->min_value = p->min;
	info->max_value = p->max;
	info->default_value = lxr_params_fromFirmware(p->param, kit[p->param]);
	return true;
}

static bool paramsGetValue(const clap_plugin_t* plugin, clap_id id, double* value)
{
	Tack* k = plugin->plugin_data;
	if(id == PARAM_SEPARATE_OUTPUTS) {
		*value = k->separateOutputs;
		return true;
	}
	if(lxr_params_indexOf(id) < 0) return false;
	*value = lxr_params_fromFirmware(id, k->values[id]);
	return true;
}

static bool paramsValueToText(const clap_plugin_t* plugin, clap_id id, double value, char* text, uint32_t size)
{
	if(id == PARAM_SEPARATE_OUTPUTS) {
		snprintf(text, size, "%s", value >= 0.5 ? "Individual" : "Stereo");
		return true;
	}
	if(lxr_params_indexOf(id) < 0) return false;
	lxr_params_valueText(id, (uint16_t)lrint(fmax(value, 0)), text, size);
	return true;
}

static bool paramsTextToValue(const clap_plugin_t* plugin, clap_id id, const char* text, double* value)
{
	if(id == PARAM_SEPARATE_OUTPUTS) {
		if(!strcasecmp(text, "Individual") || !strcmp(text, "1")) *value = 1;
		else if(!strcasecmp(text, "Stereo") || !strcmp(text, "0")) *value = 0;
		else return false;
		return true;
	}
	const int index = lxr_params_indexOf(id);
	if(index < 0) return false;
	const LxrParamInfo* p = lxr_params_info(index);

	// every parameter has at most a few hundred values: find the one that reads like the text
	char* end;
	const long number = strtol(text, &end, 10);
	const bool isNumber = end != text && *end == 0;
	for(uint16_t v = p->min; v <= p->max; v++)
	{
		char t[64];
		lxr_params_valueText(id, v, t, sizeof t);
		if(!strcasecmp(t, text) || (isNumber && strtol(t, NULL, 10) == number)) {
			*value = v;
			return true;
		}
	}
	return false;
}

static void paramsFlush(const clap_plugin_t* plugin, const clap_input_events_t* in, const clap_output_events_t* out)
{
	// not processing, so nothing else is using the engine
	Tack* k = plugin->plugin_data;
	const uint32_t n = in->size(in);
	for(uint32_t i = 0; i < n; i++) {
		const clap_event_header_t* ev = in->get(in, i);
		if(ev->space_id == CLAP_CORE_EVENT_SPACE_ID && ev->type == CLAP_EVENT_PARAM_VALUE)
			handleParamEvent(k, (const clap_event_param_value_t*)ev, 0, false);
	}
	applyGuiEvents(k, out, false);
}

static const clap_plugin_params_t params = {
	paramsCount, paramsGetInfo, paramsGetValue, paramsValueToText, paramsTextToValue, paramsFlush
};
//-----------------------------------------------------------
// replaces the whole kit; main thread
static void setKit(Tack* k, const uint8_t* kit)
{
	memcpy(k->values, kit, LXR_HOST_NUM_PARAMS);
	if(!k->active) {
		k->engine.api->loadKit(kit);
	} else {
		// the audio thread picks it up at its next process() call
		while(atomic_flag_test_and_set(&k->pendingLock)) {}
		memcpy(k->pendingKit, kit, LXR_HOST_NUM_PARAMS);
		atomic_store(&k->hasPendingKit, true);
		atomic_flag_clear(&k->pendingLock);
	}
	if(k->hostParams) k->hostParams->rescan(k->host, CLAP_PARAM_RESCAN_VALUES);
}
//-----------------------------------------------------------
static bool stateSave(const clap_plugin_t* plugin, const clap_ostream_t* stream)
{
	Tack* k = plugin->plugin_data;
	uint8_t data[8 + LXR_HOST_NUM_PARAMS + 1 + KIT_NAME_SIZE + 1 + KIT_PATH_SIZE];
	const uint32_t n = LXR_HOST_NUM_PARAMS;
	memcpy(data, STATE_MAGIC, 4);
	memcpy(data + 4, &n, 4);
	memcpy(data + 8, k->values, n);
	uint8_t* p = data + 8 + n;
	*p++ = k->separateOutputs;
	memcpy(p, k->kitName, KIT_NAME_SIZE);
	p += KIT_NAME_SIZE;
	*p++ = k->kitEdited;
	memcpy(p, k->kitPath, KIT_PATH_SIZE);

	for(size_t done = 0; done < sizeof data; ) {
		const int64_t w = stream->write(stream, data + done, sizeof data - done);
		if(w <= 0) return false;
		done += w;
	}
	return true;
}

static bool stateLoad(const clap_plugin_t* plugin, const clap_istream_t* stream)
{
	Tack* k = plugin->plugin_data;
	uint8_t data[8 + 4096];
	size_t size = 0;
	while(size < sizeof data) {
		const int64_t r = stream->read(stream, data + size, sizeof data - size);
		if(r < 0) return false;
		if(r == 0) break;
		size += r;
	}
	uint32_t n;
	if(size < 8 || memcmp(data, STATE_MAGIC, 4)) return false;
	memcpy(&n, data + 4, 4);
	if(size < 8 + (size_t)n) return false;

	// parameters missing from an older state keep their init kit values
	uint8_t kit[LXR_HOST_NUM_PARAMS];
	lxr_host_initKit(kit);
	memcpy(kit, data + 8, n < LXR_HOST_NUM_PARAMS ? n : LXR_HOST_NUM_PARAMS);
	setKit(k, kit);

	k->separateOutputs = size > 8 + (size_t)n && data[8 + n];
	const uint8_t* p = data + 8 + n + 1;
	const uint8_t* end = data + size;
	k->kitName[0] = k->kitPath[0] = 0;
	k->kitEdited = false;
	if(end >= p + KIT_NAME_SIZE) {
		memcpy(k->kitName, p, KIT_NAME_SIZE);
		k->kitName[KIT_NAME_SIZE - 1] = 0;
		p += KIT_NAME_SIZE;
	}
	if(end >= p + 1 + KIT_PATH_SIZE) {
		k->kitEdited = *p++;
		memcpy(k->kitPath, p, KIT_PATH_SIZE);
		k->kitPath[KIT_PATH_SIZE - 1] = 0;
	}
	if(!k->active)
		k->engine.api->setSeparateOutputs(k->separateOutputs);
	else
		atomic_store(&k->pendingSeparateOutputs, k->separateOutputs);
	return true;
}

static const clap_plugin_state_t state = { stateSave, stateLoad };
//-----------------------------------------------------------
// loads a kit file, or the init kit if path is NULL; main thread
static bool loadKit(Tack* k, const char* path)
{
	uint8_t kit[LXR_HOST_NUM_PARAMS];
	char name[9] = "Init Kit";
	if(!path)
		lxr_host_initKit(kit);
	else if(lxr_host_readKitFile(path, kit, name))
		return false;
	setKit(k, kit);
	snprintf(k->kitName, sizeof k->kitName, "%s", name);
	snprintf(k->kitPath, sizeof k->kitPath, "%s", path ? path : "");
	k->kitEdited = false;
	return true;
}

static bool presetLoadFromLocation(const clap_plugin_t* plugin, uint32_t kind, const char* location, const char* key)
{
	Tack* k = plugin->plugin_data;
	const bool init = kind == CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN && key && !strcmp(key, TACK_INIT_KIT_KEY);
	const bool file = kind == CLAP_PRESET_DISCOVERY_LOCATION_FILE && location;
	if(!(init || file) || !loadKit(k, init ? NULL : location)) {
		if(k->hostPresetLoad)
			k->hostPresetLoad->on_error(k->host, kind, location, key, 0, "Could not read the kit file");
		return false;
	}
	if(k->hostPresetLoad) k->hostPresetLoad->loaded(k->host, kind, location, key);
	return true;
}

static const clap_plugin_preset_load_t presetLoad = { presetLoadFromLocation };
//-----------------------------------------------------------
// the editor's view of the plugin; main thread

static double guiGetValue(void* ctx, uint32_t id)
{
	Tack* k = ctx;
	double v = 0;
	paramsGetValue(&k->plugin, id, &v);
	return v;
}

static void guiPush(Tack* k, uint8_t type, uint32_t id, double value)
{
	const unsigned w = atomic_load(&k->guiWrite);
	if(w - atomic_load(&k->guiRead) >= GUI_QUEUE_SIZE) return;
	k->guiEvents[w % GUI_QUEUE_SIZE] = (GuiEvent){ type, id, value };
	atomic_store(&k->guiWrite, w + 1);
	// if the host isn't processing, it calls flush instead
	if(k->hostParams) k->hostParams->request_flush(k->host);
}

static void guiBeginEdit(void* ctx, uint32_t id) { guiPush(ctx, GUI_BEGIN_EDIT, id, 0); }
static void guiEndEdit(void* ctx, uint32_t id) { guiPush(ctx, GUI_END_EDIT, id, 0); }

static void guiSetValue(void* ctx, uint32_t id, double value)
{
	Tack* k = ctx;
	// show the new value straight away; the engine gets it with the event
	if(id == PARAM_SEPARATE_OUTPUTS)
		k->separateOutputs = value >= 0.5;
	else if(lxr_params_indexOf(id) >= 0) {
		k->values[id] = lxr_params_toFirmware(id, (uint16_t)lrint(fmax(value, 0)));
		k->kitEdited = true;
	}
	guiPush(k, GUI_SET_VALUE, id, value);
}

static void guiAudition(void* ctx, uint8_t voice) { guiPush(ctx, GUI_NOTE_ON, voice, 0); }
static const char* guiKitName(void* ctx) { return ((Tack*)ctx)->kitName; }
static const char* guiKitPath(void* ctx) { return ((Tack*)ctx)->kitPath; }
static bool guiKitEdited(void* ctx) { return ((Tack*)ctx)->kitEdited; }

static bool guiLoadKit(void* ctx, const char* path)
{
	Tack* k = ctx;
	if(!loadKit(k, path)) return false;
	// tell the host, as if it had loaded the preset itself, and that the project changed
	if(k->hostPresetLoad) {
		if(path)
			k->hostPresetLoad->loaded(k->host, CLAP_PRESET_DISCOVERY_LOCATION_FILE, path, NULL);
		else
			k->hostPresetLoad->loaded(k->host, CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN, NULL, TACK_INIT_KIT_KEY);
	}
	if(k->hostState) k->hostState->mark_dirty(k->host);
	return true;
}

static int guiSaveKit(void* ctx, const char* name, bool overwrite)
{
	Tack* k = ctx;
	char path[2048];
	const int r = tack_saveKit(k->values, name, overwrite, path, sizeof path);
	if(r != TACK_SAVE_OK) return r;
	snprintf(k->kitName, sizeof k->kitName, "%s", name);
	snprintf(k->kitPath, sizeof k->kitPath, "%s", path);
	k->kitEdited = false;
	// the project now holds this kit file, as if it had been loaded from it
	if(k->hostPresetLoad) k->hostPresetLoad->loaded(k->host, CLAP_PRESET_DISCOVERY_LOCATION_FILE, path, NULL);
	if(k->hostState) k->hostState->mark_dirty(k->host);
	return r;
}

static bool guiIsApiSupported(const clap_plugin_t* plugin, const char* api, bool isFloating)
{
	// Linux only for now: embedded in the host's X11 window
	return !strcmp(api, CLAP_WINDOW_API_X11) && !isFloating;
}

static bool guiGetPreferredApi(const clap_plugin_t* plugin, const char** api, bool* isFloating)
{
	*api = CLAP_WINDOW_API_X11;
	*isFloating = false;
	return true;
}

static void guiDestroy(const clap_plugin_t* plugin)
{
	Tack* k = plugin->plugin_data;
	if(!k->gui) return;
	if(k->hostTimer) k->hostTimer->unregister_timer(k->host, k->guiTimer);
	tack_guiDestroy(k->gui);
	k->gui = NULL;
}

static bool guiCreate(const clap_plugin_t* plugin, const char* api, bool isFloating)
{
	Tack* k = plugin->plugin_data;
	// the editor needs a timer from the host to handle its window events
	if(!guiIsApiSupported(plugin, api, isFloating) || k->gui || !k->hostTimer) return false;
	const TackGuiHost host = { k, guiGetValue, guiBeginEdit, guiSetValue, guiEndEdit, guiAudition, guiKitName, guiSaveKit,
		guiKitPath, guiKitEdited, guiLoadKit };
	k->gui = tack_guiCreate(&host);
	if(!k->gui) return false;
	if(!k->hostTimer->register_timer(k->host, GUI_TIMER_MS, &k->guiTimer)) {
		tack_guiDestroy(k->gui);
		k->gui = NULL;
		return false;
	}
	return true;
}

static bool guiSetScale(const clap_plugin_t* plugin, double scale) { return false; }

static bool guiGetSize(const clap_plugin_t* plugin, uint32_t* width, uint32_t* height)
{
	tack_guiGetSize(width, height);
	return true;
}

static bool guiCanResize(const clap_plugin_t* plugin) { return false; }
static bool guiGetResizeHints(const clap_plugin_t* plugin, clap_gui_resize_hints_t* hints) { return false; }
static bool guiAdjustSize(const clap_plugin_t* plugin, uint32_t* width, uint32_t* height) { return guiGetSize(plugin, width, height); }

static bool guiSetSize(const clap_plugin_t* plugin, uint32_t width, uint32_t height)
{
	uint32_t w, h;
	tack_guiGetSize(&w, &h);
	return width == w && height == h;
}

static bool guiSetParent(const clap_plugin_t* plugin, const clap_window_t* window)
{
	Tack* k = plugin->plugin_data;
	return k->gui && tack_guiSetParent(k->gui, (uintptr_t)window->x11);
}

static bool guiSetTransient(const clap_plugin_t* plugin, const clap_window_t* window) { return false; }
static void guiSuggestTitle(const clap_plugin_t* plugin, const char* title) {}
static bool guiShow(const clap_plugin_t* plugin) { Tack* k = plugin->plugin_data; return k->gui && tack_guiShow(k->gui); }
static bool guiHide(const clap_plugin_t* plugin) { Tack* k = plugin->plugin_data; return k->gui && tack_guiHide(k->gui); }

static const clap_plugin_gui_t gui = {
	guiIsApiSupported, guiGetPreferredApi, guiCreate, guiDestroy, guiSetScale, guiGetSize, guiCanResize,
	guiGetResizeHints, guiAdjustSize, guiSetSize, guiSetParent, guiSetTransient, guiSuggestTitle, guiShow, guiHide
};

static void onTimer(const clap_plugin_t* plugin, clap_id timer)
{
	Tack* k = plugin->plugin_data;
	if(k->gui && timer == k->guiTimer) tack_guiIdle(k->gui);
}

static const clap_plugin_timer_support_t timerSupport = { onTimer };
//-----------------------------------------------------------
static bool tackInit(const clap_plugin_t* plugin)
{
	Tack* k = plugin->plugin_data;
	k->hostParams = k->host->get_extension(k->host, CLAP_EXT_PARAMS);
	k->hostTimer = k->host->get_extension(k->host, CLAP_EXT_TIMER_SUPPORT);
	k->hostState = k->host->get_extension(k->host, CLAP_EXT_STATE);
	snprintf(k->kitName, sizeof k->kitName, "Init Kit");
	k->hostPresetLoad = k->host->get_extension(k->host, CLAP_EXT_PRESET_LOAD);
	if(!k->hostPresetLoad) k->hostPresetLoad = k->host->get_extension(k->host, CLAP_EXT_PRESET_LOAD_COMPAT);
	k->engine.api->init();
	k->engine.api->setSeparateOutputs(false);
	for(uint16_t i = 0; i < LXR_HOST_NUM_PARAMS; i++)
		k->values[i] = k->engine.api->getParam(i);
	return true;
}

static void tackDestroy(const clap_plugin_t* plugin)
{
	Tack* k = plugin->plugin_data;
	guiDestroy(plugin);
	k->engine.api->shutdown();
	tack_engineClose(&k->engine);
	free(k);
}

static bool tackActivate(const clap_plugin_t* plugin, double fs, uint32_t minFrames, uint32_t maxFrames)
{
	Tack* k = plugin->plugin_data;
	if(k->engine.api->setOutputRate(fs)) return false;
	k->active = true;
	return true;
}

static void tackDeactivate(const clap_plugin_t* plugin)
{
	Tack* k = plugin->plugin_data;
	k->active = false;
	// the audio thread has stopped: a kit or output switch it didn't get to can be applied here
	applyPendingKit(k);
	const int separate = atomic_exchange(&k->pendingSeparateOutputs, -1);
	if(separate >= 0) k->engine.api->setSeparateOutputs(separate);
}
static bool tackStartProcessing(const clap_plugin_t* plugin) { return true; }
static void tackStopProcessing(const clap_plugin_t* plugin) {}
static void tackReset(const clap_plugin_t* plugin) {}
static void tackOnMainThread(const clap_plugin_t* plugin) {}

static const void* tackGetExtension(const clap_plugin_t* plugin, const char* id)
{
	if(!strcmp(id, CLAP_EXT_NOTE_PORTS)) return &notePorts;
	if(!strcmp(id, CLAP_EXT_AUDIO_PORTS)) return &audioPorts;
	if(!strcmp(id, CLAP_EXT_LATENCY)) return &latency;
	if(!strcmp(id, CLAP_EXT_PARAMS)) return &params;
	if(!strcmp(id, CLAP_EXT_STATE)) return &state;
	if(!strcmp(id, CLAP_EXT_PRESET_LOAD) || !strcmp(id, CLAP_EXT_PRESET_LOAD_COMPAT)) return &presetLoad;
	if(!strcmp(id, CLAP_EXT_GUI)) return &gui;
	if(!strcmp(id, CLAP_EXT_TIMER_SUPPORT)) return &timerSupport;
	return NULL;
}
//-----------------------------------------------------------
static uint32_t factoryCount(const clap_plugin_factory_t* f) { return 1; }

static const clap_plugin_descriptor_t* factoryDescriptor(const clap_plugin_factory_t* f, uint32_t index)
{
	return index == 0 ? &descriptor : NULL;
}

static const clap_plugin_t* factoryCreate(const clap_plugin_factory_t* f, const clap_host_t* host, const char* id)
{
	if(strcmp(id, descriptor.id) || !clap_version_is_compatible(host->clap_version)) return NULL;

	Tack* k = calloc(1, sizeof *k);
	if(!k) return NULL;
	if(!tack_engineOpen(&k->engine)) {
		free(k);
		return NULL;
	}
	atomic_flag_clear(&k->pendingLock);
	atomic_store(&k->pendingSeparateOutputs, -1);
	k->host = host;
	k->plugin = (clap_plugin_t){
		.desc = &descriptor,
		.plugin_data = k,
		.init = tackInit,
		.destroy = tackDestroy,
		.activate = tackActivate,
		.deactivate = tackDeactivate,
		.start_processing = tackStartProcessing,
		.stop_processing = tackStopProcessing,
		.reset = tackReset,
		.process = tackProcess,
		.get_extension = tackGetExtension,
		.on_main_thread = tackOnMainThread,
	};
	return &k->plugin;
}

static const clap_plugin_factory_t factory = { factoryCount, factoryDescriptor, factoryCreate };
//-----------------------------------------------------------
static bool entryInit(const char* path) { return true; }
static void entryDeinit(void) {}

static const void* entryGetFactory(const char* id)
{
	if(!strcmp(id, CLAP_PLUGIN_FACTORY_ID)) return &factory;
	if(!strcmp(id, CLAP_PRESET_DISCOVERY_FACTORY_ID) || !strcmp(id, CLAP_PRESET_DISCOVERY_FACTORY_ID_COMPAT))
		return tack_presetDiscoveryFactory();
	return NULL;
}

CLAP_EXPORT const clap_plugin_entry_t clap_entry = { CLAP_VERSION_INIT, entryInit, entryDeinit, entryGetFactory };
