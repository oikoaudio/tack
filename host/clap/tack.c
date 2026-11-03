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
 * The same binary also offers one plugin per kind of voice (Tack Drum, Snare, Cymbal, Hi-hat): a whole engine
 * that only plays and shows one voice, chromatically on any channel, without the FX bus. Tack Drum is drum 1.
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
#include <time.h>
#include <clap/clap.h>
#include "lxr_host.h"
#include "lxr_params.h"
#include "kit_mutate.h"
#include "presets.h"
#include "engine.h"
#include "gui.h"
#include "wrapper_info.h"

#define TACK_CHUNK	256

// state: magic, then the number of parameters, then the kit in firmware representation, then the separate
// outputs switch, the kit's name, whether it was edited since loading, the kit file it came from, the voice of it
// (-1 the whole kit) and the chromatic switch (each missing in states saved before it existed)
#define STATE_MAGIC		"KTL1"
#define KIT_NAME_SIZE	16
#define KIT_PATH_SIZE	1024

// ids past the firmware's parameter numbers, for settings that aren't part of the kit. The outputs switch is only the
// editor's: the host doesn't list it, so it has no automation and no undo, and the state keeps it
#define PARAM_SEPARATE_OUTPUTS	1000
#define PARAM_CHROMATIC			1001	// a voice plugin's parameter: notes set the pitch, or all play the tuned pitch

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

// a single voice has one output, its own: the engine's solo voice, its only channels
#define numPorts(k) ((k)->type->voice < 0 ? NUM_OUTPUT_PORTS : 1)
#define portChannel(k, port) ((k)->type->voice < 0 ? outputPorts[port].channel : 0)

// changes made in the editor, passed from the main thread to whichever thread next processes or flushes
enum { GUI_BEGIN_EDIT, GUI_SET_VALUE, GUI_END_EDIT };
typedef struct
{
	uint8_t type;
	uint32_t id;	// parameter id
	double value;
} GuiEvent;
#define GUI_QUEUE_SIZE 256
#define GUI_TIMER_MS 20

// the plugins in this binary: the whole kit, or one voice of it
typedef struct
{
	uint16_t param;
	uint16_t values[256];	// the destinations on offer, as the whole kit's plugin values
	int n;
} TargetMap;

typedef struct
{
	const clap_plugin_descriptor_t* desc;
	int voice;	// -1 the whole kit; else the voice (0-5: drum 1-3, snare, cymbal, hi-hat) it plays
	// a single voice's parameters: indices into lxr_params, and its modulation destinations, which only offer
	// targets on the voice itself
	int params[LXR_HOST_NUM_PARAMS];
	int numParams;
	TargetMap targets[2];
} TackType;

typedef struct
{
	clap_plugin_t plugin;
	const TackType* type;
	const clap_host_t* host;
	TackEngineCopy engine;	// this instance's own copy of the sound engine
	const clap_host_params_t* hostParams;
	const clap_host_preset_load_t* hostPresetLoad;
	const clap_host_state_t* hostState;
	bool active;

	// the kit as the host sees it, in firmware representation. Written by whichever thread changes parameters
	uint8_t values[LXR_HOST_NUM_PARAMS];
	// the host's modulation of each parameter, in plugin values, added to the parameter's own value; and what the
	// engine plays, the two together. Written by whichever thread processes or flushes; the editor reads mod
	_Atomic float mod[LXR_HOST_NUM_PARAMS];
	uint8_t engineValues[LXR_HOST_NUM_PARAMS];

	// a loaded state waiting for the audio thread
	uint8_t pendingKit[LXR_HOST_NUM_PARAMS];
	atomic_bool hasPendingKit;
	atomic_flag pendingLock;

	// everything on Main, or each direct voice on its own output and the FX bus on Main; main thread
	bool separateOutputs;
	atomic_int pendingSeparateOutputs;	// for the audio thread; -1 if none

	// main thread
	char kitName[KIT_NAME_SIZE];
	char kitPath[KIT_PATH_SIZE];	// the kit file it was loaded from or saved to; empty for the init kit or unknown
	int kitVoice;	// voice plugins: the voice of that kit file it plays; -1 for the whole kit
	bool kitEdited;	// in the editor, since it was loaded or saved
	_Atomic float tempo;	// the host's, 0 until it has said; the editor's Sync goes by it
	KitWalk walk;	// the editor's Mutate
	bool walkFromEdited;	// back at the walk's start, the kit is as edited as it was then

	// voice plugins: notes set the pitch (C3 the tuned pitch), or every note plays the tuned pitch. Read by the
	// audio thread; a lost race only affects the note being played as it changes
	atomic_bool chromatic;

	// the editor; its changes reach the engine and the host through guiEvents
	TackGui* gui;
	bool guiParented;	// the editor is in the host's window
	const clap_host_gui_t* hostGui;
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
static const char* voiceFeatures[] = {
	CLAP_PLUGIN_FEATURE_INSTRUMENT, CLAP_PLUGIN_FEATURE_DRUM, CLAP_PLUGIN_FEATURE_SYNTHESIZER,
	CLAP_PLUGIN_FEATURE_STEREO, NULL
};

#define DESCRIPTOR(idSuffix, pluginName, what, feats) { \
	.clap_version = CLAP_VERSION_INIT, .id = TACK_PLUGIN_ID idSuffix, .name = pluginName, .vendor = "oikoaudio", \
	.url = "", .manual_url = "", .support_url = "", .version = TACK_VERSION, \
	.description = what ". DSP derived from the LXR-02 firmware by Sonic Potions and Erica Synths (modified); not affiliated with either.", \
	.features = feats }

static const clap_plugin_descriptor_t descriptors[] =
{
	DESCRIPTOR("", "Tack", "Drum synth", features),
	DESCRIPTOR(".drum", "Tack Drum", "Drum voice", voiceFeatures),
	DESCRIPTOR(".snare", "Tack Snare", "Snare voice", voiceFeatures),
	DESCRIPTOR(".cymbal", "Tack Cymbal", "Cymbal voice", voiceFeatures),
	DESCRIPTOR(".hihat", "Tack Hi-hat", "Hi-hat voice", voiceFeatures),
};
#define NUM_TYPES (sizeof descriptors / sizeof descriptors[0])
static TackType types[NUM_TYPES];

// names of the voices, as parameter names begin
static const char* const voiceNames[LXR_PARAMS_NUM_VOICES] = { "Drum 1", "Drum 2", "Drum 3", "Snare", "Cymbal", "Hi-hat" };

static void buildTypes(void)
{
	static const int voices[NUM_TYPES] = { -1, 0, 3, 4, 5 };
	for(size_t t = 0; t < NUM_TYPES; t++) {
		TackType* type = &types[t];
		type->desc = &descriptors[t];
		type->voice = voices[t];
		if(type->voice < 0) continue;
		type->numParams = 0;
		int targets = 0;
		for(int i = 0; i < lxr_params_count(); i++) {
			const LxrParamInfo* p = lxr_params_info(i);
			// its own parameters; it always plays directly, without the FX bus
			if(p->voice != type->voice || p->param == PAR_AUDIO_OUT1 + type->voice) continue;
			type->params[type->numParams++] = i;
			if(lxr_params_isTarget(p->param) && targets < 2) {
				TargetMap* m = &type->targets[targets++];
				m->param = p->param;
				m->n = 0;
				for(uint16_t v = p->min; v <= p->max && m->n < 256; v++)
					if(v == 0 || lxr_params_targetVoice(p->param, v) == type->voice) m->values[m->n++] = v;
			}
		}
	}
}

static const TargetMap* targetMap(const TackType* t, clap_id id)
{
	for(int i = 0; t->voice >= 0 && i < 2; i++)
		if(t->targets[i].param == id && t->targets[i].n) return &t->targets[i];
	return NULL;
}

// whether this plugin has a kit parameter
static bool hasParam(const TackType* t, clap_id id)
{
	const int i = lxr_params_indexOf(id);
	if(i < 0) return false;
	if(t->voice < 0) return true;
	for(int j = 0; j < t->numParams; j++)
		if(t->params[j] == i) return true;
	return false;
}

// a single voice's destinations count only its own targets: its plugin values to and from the whole kit's
static uint16_t toKitValue(const TackType* t, clap_id id, uint16_t value)
{
	const TargetMap* m = targetMap(t, id);
	return m ? (value < m->n ? m->values[value] : 0) : value;
}

static uint16_t fromKitValue(const TackType* t, clap_id id, uint16_t value)
{
	const TargetMap* m = targetMap(t, id);
	if(!m) return value;
	for(int i = 0; i < m->n; i++)
		if(m->values[i] == value) return i;
	return 0;
}

// a single voice's names leave out the voice: "Filter Frequency"
static const char* withoutVoice(const TackType* t, const char* name)
{
	if(t->voice < 0) return name;
	const size_t n = strlen(voiceNames[t->voice]);
	if(!strncmp(name, voiceNames[t->voice], n) && (name[n] == ' ' || name[n] == '/')) return name + n + 1;
	return name;
}

static void valueText(const TackType* t, clap_id id, uint16_t kitValue, char* text, size_t size)
{
	char full[96];
	lxr_params_valueText(id, kitValue, full, sizeof full);
	snprintf(text, size, "%s", withoutVoice(t, full));
}

// a single voice plays directly, with the FX bus off, and its destinations stay on the voice. The other voices
// aren't computed, so their modulation is off too.
static void adaptKit(const TackType* t, uint8_t* kit)
{
	if(t->voice < 0) return;
	kit[PAR_AUDIO_OUT1 + t->voice] = lxr_params_toFirmware(PAR_AUDIO_OUT1 + t->voice, 0);
	kit[PAR_FX_TYPE] = 0;
	for(int v = 0; v < LXR_PARAMS_NUM_VOICES; v++)
		if(v != t->voice) kit[PAR_TARGET_LFO1 + v] = kit[PAR_VEL_DEST_1 + v] = 0;
	for(int i = 0; i < 2; i++) {
		const TargetMap* m = &t->targets[i];
		if(m->n && lxr_params_targetVoice(m->param, lxr_params_fromFirmware(m->param, kit[m->param])) != t->voice)
			kit[m->param] = 0;
	}
}

//-----------------------------------------------------------
// the host can modulate amounts; options (waveforms, destinations, outputs) only change with their value
static bool isModulatable(const LxrParamInfo* p) { return !p->isEnum; }

// a parameter's value with the host's modulation added, as the engine should play it
static uint8_t modulatedValue(Tack* k, uint16_t id, uint8_t value)
{
	const float m = atomic_load(&k->mod[id]);
	if(m == 0) return value;
	const LxrParamInfo* p = lxr_params_info(lxr_params_indexOf(id));
	const double v = fmin(fmax(lrint(lxr_params_fromFirmware(id, value) + m), p->min), p->max);
	return lxr_params_toFirmware(id, (uint16_t)v);
}

// passes a parameter to the engine if what it should play changed.
// queued: called from process(), so the change lands lxr_host_latency() later like the notes around it
static void sendParam(Tack* k, uint16_t id, uint32_t frame, bool queued)
{
	const uint8_t value = modulatedValue(k, id, k->values[id]);
	if(value == k->engineValues[id]) return;
	k->engineValues[id] = value;
	if(queued)
		k->engine.api->queueParam(frame, id, value);
	else
		k->engine.api->setParam(id, value);
}

// a kit straight to the engine, with the host's modulation on it; from the thread that owns the engine
static void loadEngineKit(Tack* k, const uint8_t* kit)
{
	k->engine.api->loadKit(kit);
	memcpy(k->engineValues, kit, LXR_HOST_NUM_PARAMS);
	for(uint16_t id = 0; id < LXR_HOST_NUM_PARAMS; id++) {
		if(atomic_load(&k->mod[id]) == 0) continue;
		k->engineValues[id] = modulatedValue(k, id, kit[id]);
		k->engine.api->setParam(id, k->engineValues[id]);
	}
}

static void handleModEvent(Tack* k, const clap_event_param_mod_t* ev, uint32_t frame, bool queued)
{
	// modulation is per parameter, not per note: every voice is monophonic
	if(ev->param_id >= LXR_HOST_NUM_PARAMS || !hasParam(k->type, ev->param_id)) return;
	if(!isModulatable(lxr_params_info(lxr_params_indexOf(ev->param_id)))) return;
	atomic_store(&k->mod[ev->param_id], (float)ev->amount);
	sendParam(k, ev->param_id, frame, queued);
}

static void handleParamEvent(Tack* k, const clap_event_param_value_t* ev, uint32_t frame, bool queued)
{
	if(ev->param_id == PARAM_CHROMATIC) {
		if(k->type->voice >= 0) atomic_store(&k->chromatic, ev->value >= 0.5);
		return;
	}
	if(!hasParam(k->type, ev->param_id)) return;
	const uint16_t kitValue = toKitValue(k->type, ev->param_id, (uint16_t)lrint(fmax(ev->value, 0)));
	k->values[ev->param_id] = lxr_params_toFirmware(ev->param_id, kitValue);
	sendParam(k, ev->param_id, frame, queued);
}

static void handleEvent(Tack* k, const clap_event_header_t* ev, uint32_t frame)
{
	if(ev->space_id != CLAP_CORE_EVENT_SPACE_ID) return;

	if(ev->type == CLAP_EVENT_PARAM_VALUE) {
		handleParamEvent(k, (const clap_event_param_value_t*)ev, frame, true);
		return;
	}
	if(ev->type == CLAP_EVENT_PARAM_MOD) {
		handleModEvent(k, (const clap_event_param_mod_t*)ev, frame, true);
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
	if(k->type->voice >= 0) {
		// a single voice on any channel; the hi-hat's open sound on channel 2
		const int voice = k->type->voice == 5 && channel == 1 ? 6 : k->type->voice;
		k->engine.api->queueNoteOn(frame, voice, atomic_load(&k->chromatic) ? note : LXR_HOST_BASE_NOTE, vel);
	} else if(channel <= 0) {	// -1: the host didn't say
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
		const uint32_t stride = k->engine.api->outChannels();
		for(uint32_t port = 0; port < p->audio_outputs_count && port < numPorts(k); port++)
		{
			const int c = portChannel(k, port);
			for(uint32_t i = 0; i < n; i++) {
				out[port].data32[0][start + i] = k->buf[i*stride + c];
				out[port].data32[1][start + i] = k->buf[i*stride + c + 1];
			}
		}
		start += n;
	}
}
//-----------------------------------------------------------
static void applyPendingKit(Tack* k)
{
	if(!atomic_load(&k->hasPendingKit) || atomic_flag_test_and_set(&k->pendingLock)) return;
	loadEngineKit(k, k->pendingKit);
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
	if(p->transport && (p->transport->flags & CLAP_TRANSPORT_HAS_TEMPO)) {
		k->engine.api->setTempo(p->transport->tempo);
		atomic_store(&k->tempo, (float)p->transport->tempo);
	}

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
	Tack* k = plugin->plugin_data;
	return isInput ? 0 : numPorts(k);
}

static bool audioPortsGet(const clap_plugin_t* plugin, uint32_t index, bool isInput, clap_audio_port_info_t* info)
{
	Tack* k = plugin->plugin_data;
	if(isInput || index >= numPorts(k)) return false;
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
	Tack* k = plugin->plugin_data;
	// a voice adds the chromatic switch
	return k->type->voice < 0 ? lxr_params_count() : k->type->numParams + 1;
}

static bool paramsGetInfo(const clap_plugin_t* plugin, uint32_t index, clap_param_info_t* info)
{
	Tack* k = plugin->plugin_data;
	const TackType* t = k->type;
	if(t->voice >= 0 && index == (uint32_t)t->numParams) {
		memset(info, 0, sizeof *info);
		info->id = PARAM_CHROMATIC;
		info->flags = CLAP_PARAM_IS_STEPPED | CLAP_PARAM_IS_ENUM;
		snprintf(info->name, sizeof info->name, "Chromatic");
		info->max_value = 1;
		info->default_value = 1;
		return true;
	}
	if(t->voice >= 0) {
		if(index >= (uint32_t)t->numParams) return false;
		index = t->params[index];
	}
	const LxrParamInfo* p = lxr_params_info(index);
	if(!p) return false;

	uint8_t kit[LXR_HOST_NUM_PARAMS];
	lxr_host_initKit(kit);

	memset(info, 0, sizeof *info);
	info->id = p->param;
	info->flags = CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_STEPPED | (p->isEnum ? CLAP_PARAM_IS_ENUM : 0)
		| (isModulatable(p) ? CLAP_PARAM_IS_MODULATABLE : 0);
	snprintf(info->name, sizeof info->name, "%s", withoutVoice(t, p->name));
	snprintf(info->module, sizeof info->module, "%s", withoutVoice(t, p->module));
	const TargetMap* m = targetMap(t, p->param);
	info->min_value = p->min;
	info->max_value = m ? m->n - 1 : p->max;
	info->default_value = fromKitValue(t, p->param, lxr_params_fromFirmware(p->param, kit[p->param]));
	return true;
}

static bool paramsGetValue(const clap_plugin_t* plugin, clap_id id, double* value)
{
	Tack* k = plugin->plugin_data;
	if(id == PARAM_CHROMATIC && k->type->voice >= 0) {
		*value = atomic_load(&k->chromatic);
		return true;
	}
	if(!hasParam(k->type, id)) return false;
	*value = fromKitValue(k->type, id, lxr_params_fromFirmware(id, k->values[id]));
	return true;
}

static bool paramsValueToText(const clap_plugin_t* plugin, clap_id id, double value, char* text, uint32_t size)
{
	Tack* k = plugin->plugin_data;
	if(id == PARAM_CHROMATIC && k->type->voice >= 0) {
		snprintf(text, size, "%s", value >= 0.5 ? "on" : "off");
		return true;
	}
	if(!hasParam(k->type, id)) return false;
	valueText(k->type, id, toKitValue(k->type, id, (uint16_t)lrint(fmax(value, 0))), text, size);
	return true;
}

static bool paramsTextToValue(const clap_plugin_t* plugin, clap_id id, const char* text, double* value)
{
	Tack* k = plugin->plugin_data;
	if(id == PARAM_CHROMATIC && k->type->voice >= 0) {
		if(!strcasecmp(text, "on") || !strcmp(text, "1")) *value = 1;
		else if(!strcasecmp(text, "off") || !strcmp(text, "0")) *value = 0;
		else return false;
		return true;
	}
	if(!hasParam(k->type, id)) return false;
	clap_param_info_t info;
	for(uint32_t i = 0; paramsGetInfo(plugin, i, &info) && info.id != id; i++) {}

	// every parameter has at most a few hundred values: find the one that reads like the text
	char* end;
	const long number = strtol(text, &end, 10);
	const bool isNumber = end != text && *end == 0;
	for(uint16_t v = (uint16_t)info.min_value; v <= (uint16_t)info.max_value; v++)
	{
		char t[96];
		valueText(k->type, id, toKitValue(k->type, id, v), t, sizeof t);
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
		if(ev->space_id != CLAP_CORE_EVENT_SPACE_ID) continue;
		if(ev->type == CLAP_EVENT_PARAM_VALUE)
			handleParamEvent(k, (const clap_event_param_value_t*)ev, 0, false);
		else if(ev->type == CLAP_EVENT_PARAM_MOD)
			handleModEvent(k, (const clap_event_param_mod_t*)ev, 0, false);
	}
	applyGuiEvents(k, out, false);
}

static const clap_plugin_params_t params = {
	paramsCount, paramsGetInfo, paramsGetValue, paramsValueToText, paramsTextToValue, paramsFlush
};
//-----------------------------------------------------------
// replaces the whole kit; main thread
static void setKit(Tack* k, const uint8_t* kitIn)
{
	uint8_t kit[LXR_HOST_NUM_PARAMS];
	memcpy(kit, kitIn, LXR_HOST_NUM_PARAMS);
	adaptKit(k->type, kit);
	memcpy(k->values, kit, LXR_HOST_NUM_PARAMS);
	if(!k->active) {
		loadEngineKit(k, kit);
	} else {
		// the audio thread picks it up at its next process() call
		while(atomic_flag_test_and_set(&k->pendingLock)) {}
		memcpy(k->pendingKit, kit, LXR_HOST_NUM_PARAMS);
		atomic_store(&k->hasPendingKit, true);
		atomic_flag_clear(&k->pendingLock);
	}
	if(k->hostParams) k->hostParams->rescan(k->host, CLAP_PARAM_RESCAN_VALUES);
}
// the outputs switch, from a loaded state or the editor; main thread
static void setSeparateOutputs(Tack* k, bool on)
{
	k->separateOutputs = on;
	if(!k->active)
		k->engine.api->setSeparateOutputs(on);
	else
		atomic_store(&k->pendingSeparateOutputs, on);
}
//-----------------------------------------------------------
static bool stateSave(const clap_plugin_t* plugin, const clap_ostream_t* stream)
{
	Tack* k = plugin->plugin_data;
	uint8_t data[8 + LXR_HOST_NUM_PARAMS + 1 + KIT_NAME_SIZE + 1 + KIT_PATH_SIZE + 2];
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
	p += KIT_PATH_SIZE;
	*p++ = (uint8_t)(int8_t)k->kitVoice;
	*p++ = atomic_load(&k->chromatic);

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

	// a single voice always has its own output
	setSeparateOutputs(k, k->type->voice >= 0 || (size > 8 + (size_t)n && data[8 + n]));
	const uint8_t* p = data + 8 + n + 1;
	const uint8_t* end = data + size;
	k->kitName[0] = k->kitPath[0] = 0;
	k->kitEdited = false;
	if(end >= p + KIT_NAME_SIZE) {
		memcpy(k->kitName, p, KIT_NAME_SIZE);
		k->kitName[KIT_NAME_SIZE - 1] = 0;
		p += KIT_NAME_SIZE;
	}
	k->kitVoice = -1;
	if(end >= p + 1 + KIT_PATH_SIZE) {
		k->kitEdited = *p++;
		memcpy(k->kitPath, p, KIT_PATH_SIZE);
		k->kitPath[KIT_PATH_SIZE - 1] = 0;
		p += KIT_PATH_SIZE;
	}
	if(end >= p + 2) {
		k->kitVoice = (int8_t)*p++;
		atomic_store(&k->chromatic, *p++ != 0);
	}
	return true;
}

static const clap_plugin_state_t state = { stateSave, stateLoad };
//-----------------------------------------------------------
// the voice of a kit file a plugin can play: any of drums 1-3 for Tack Drum, its own for the other voice plugins
// (voice -1 for the plugin's own); -1 for the whole kit in Tack, or -2 if the plugin can't play it
static int playableVoice(const TackType* t, int voice)
{
	if(t->voice < 0) return voice < 0 ? -1 : -2;
	if(voice < 0) return t->voice;
	if(t->voice < 3) return voice < 3 ? voice : -2;
	return voice == t->voice ? voice : -2;
}

// loads a kit file, or the init kit if path is NULL; a voice plugin takes one voice of it (-1 its own). Main thread
static bool loadKit(Tack* k, const char* path, int voice)
{
	voice = playableVoice(k->type, voice);
	if(voice < -1) return false;
	uint8_t kit[LXR_HOST_NUM_PARAMS];
	char name[9];
	snprintf(name, sizeof name, "%s", k->type->voice < 0 ? "Init Kit" : "Init");
	if(!path)
		lxr_host_initKit(kit);
	else if(lxr_host_readKitFile(path, kit, name))
		return false;
	if(path && voice >= 0) {
		// the voice on its own: on the init kit, in the place of the plugin's voice
		uint8_t file[LXR_HOST_NUM_PARAMS];
		memcpy(file, kit, sizeof file);
		lxr_host_initKit(kit);
		lxr_params_copyVoice(file, voice, kit, k->type->voice);
		tack_voicePresetName(name, voice, tack_soundingVoices(file), k->kitName, sizeof k->kitName);
	} else {
		snprintf(k->kitName, sizeof k->kitName, "%s", name);
	}
	setKit(k, kit);
	snprintf(k->kitPath, sizeof k->kitPath, "%s", path ? path : "");
	tack_slashes(k->kitPath);	// as the kit folder, so the editor finds it there
	k->kitVoice = path ? voice : -1;
	k->kitEdited = false;
	return true;
}

// tells the host which preset the plugin holds now, if it is one the host lists: a whole kit file or the init kit
static void presetLoaded(Tack* k)
{
	if(!k->hostPresetLoad) return;
	if(k->kitPath[0]) {
		if(k->type->voice < 0) k->hostPresetLoad->loaded(k->host, CLAP_PRESET_DISCOVERY_LOCATION_FILE, k->kitPath, NULL);
	} else
		k->hostPresetLoad->loaded(k->host, CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN, NULL, TACK_INIT_KIT_KEY);
}

static bool presetLoadFromLocation(const clap_plugin_t* plugin, uint32_t kind, const char* location, const char* key)
{
	Tack* k = plugin->plugin_data;
	const bool init = kind == CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN && key && !strcmp(key, TACK_INIT_KIT_KEY);
	const bool file = kind == CLAP_PRESET_DISCOVERY_LOCATION_FILE && location;
	if(!(init || file) || !loadKit(k, init ? NULL : location, tack_voiceOfKey(key))) {
		if(k->hostPresetLoad)
			k->hostPresetLoad->on_error(k->host, kind, location, key, 0, "Could not read the kit file");
		return false;
	}
	presetLoaded(k);
	return true;
}

static const clap_plugin_preset_load_t presetLoad = { presetLoadFromLocation };
//-----------------------------------------------------------
// the editor's view of the plugin; main thread. It works in the whole kit's plugin values, so that the same
// editor serves every plugin

static double guiGetValue(void* ctx, uint32_t id)
{
	Tack* k = ctx;
	if(id == PARAM_SEPARATE_OUTPUTS) return k->separateOutputs;
	if(id == PARAM_CHROMATIC) return atomic_load(&k->chromatic);
	return hasParam(k->type, id) ? lxr_params_fromFirmware(id, k->values[id]) : 0;
}

static bool guiHasParam(void* ctx, uint32_t id) { return hasParam(((Tack*)ctx)->type, id); }

static double guiModulation(void* ctx, uint32_t id)
{
	return id < LXR_HOST_NUM_PARAMS ? atomic_load(&((Tack*)ctx)->mod[id]) : 0;
}

static bool guiOffersValue(void* ctx, uint32_t id, uint16_t value)
{
	const TargetMap* m = targetMap(((Tack*)ctx)->type, id);
	if(!m) return true;
	for(int i = 0; i < m->n; i++)
		if(m->values[i] == value) return true;
	return false;
}

static void guiValueText(void* ctx, uint32_t id, uint16_t value, char* text, size_t size)
{
	if(id == PARAM_SEPARATE_OUTPUTS) snprintf(text, size, "%s", value ? "Individual" : "Stereo");
	else valueText(((Tack*)ctx)->type, id, value, text, size);
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

// the host hears of every change but the outputs switch's
static void guiBeginEdit(void* ctx, uint32_t id) { if(id != PARAM_SEPARATE_OUTPUTS) guiPush(ctx, GUI_BEGIN_EDIT, id, 0); }
static void guiEndEdit(void* ctx, uint32_t id) { if(id != PARAM_SEPARATE_OUTPUTS) guiPush(ctx, GUI_END_EDIT, id, 0); }

static void guiSetValue(void* ctx, uint32_t id, double value)
{
	Tack* k = ctx;
	if(id == PARAM_SEPARATE_OUTPUTS) {
		if(k->type->voice >= 0 || (value >= 0.5) == k->separateOutputs) return;
		setSeparateOutputs(k, value >= 0.5);
		if(k->hostState) k->hostState->mark_dirty(k->host);
		return;
	}
	// show the new value straight away; the engine gets it with the event
	if(id == PARAM_CHROMATIC)
		atomic_store(&k->chromatic, value >= 0.5);
	else if(hasParam(k->type, id)) {
		k->values[id] = lxr_params_toFirmware(id, (uint16_t)lrint(fmax(value, 0)));
		k->kitEdited = true;
		// the host gets the plugin's own value
		value = fromKitValue(k->type, id, (uint16_t)lrint(fmax(value, 0)));
	}
	guiPush(k, GUI_SET_VALUE, id, value);
}

static const char* guiKitName(void* ctx) { return ((Tack*)ctx)->kitName; }
static const char* guiKitPath(void* ctx) { return ((Tack*)ctx)->kitPath; }
static bool guiKitEdited(void* ctx) { return ((Tack*)ctx)->kitEdited; }

static int guiKitVoice(void* ctx) { return ((Tack*)ctx)->kitVoice; }

static bool guiLoadKit(void* ctx, const char* path, int voice)
{
	Tack* k = ctx;
	if(!loadKit(k, path, voice)) return false;
	// tell the host, as if it had loaded the preset itself, and that the project changed
	presetLoaded(k);
	if(k->hostState) k->hostState->mark_dirty(k->host);
	return true;
}

static bool guiLoadVoice(void* ctx, const char* path, int fromVoice, int toVoice)
{
	Tack* k = ctx;
	uint8_t file[LXR_HOST_NUM_PARAMS], kit[LXR_HOST_NUM_PARAMS];
	if(k->type->voice >= 0 || toVoice < 0 || toVoice >= LXR_PARAMS_NUM_VOICES || lxr_host_readKitFile(path, file, NULL))
		return false;
	memcpy(kit, k->values, sizeof kit);
	lxr_params_copyVoice(file, fromVoice, kit, toVoice);
	// the voice keeps its place in the mix
	kit[PAR_AUDIO_OUT1 + toVoice] = k->values[PAR_AUDIO_OUT1 + toVoice];
	setKit(k, kit);
	// still the same kit, edited: its name and file stay
	k->kitEdited = true;
	if(k->hostState) k->hostState->mark_dirty(k->host);
	return true;
}

static int guiMutate(void* ctx, int voice, int steps)
{
	Tack* k = ctx;
	static uint8_t top[LXR_HOST_NUM_PARAMS], voices[LXR_HOST_NUM_PARAMS];
	static uint32_t rolls;
	uint8_t kit[LXR_HOST_NUM_PARAMS];
	if(!top[PAR_FILTER_FREQ_1]) lxr_params_mutateTable(top, voices);
	if(k->type->voice >= 0) voice = k->type->voice;
	memcpy(kit, k->values, sizeof kit);
	if(!kit_walk_step(&k->walk, kit)) k->walkFromEdited = k->kitEdited;
	const int step = kit_walk(&k->walk, kit, top, voices, voice == TACK_MUTATE_AGAIN ? k->walk.scope : voice < 0 ? KIT_MUTATE_ALL : (uint8_t)voice,
		steps, (uint32_t)time(NULL) * 2654435761u + ++rolls);
	setKit(k, kit);
	// still the same kit, edited: its name and file stay
	k->kitEdited = step || k->walkFromEdited;
	if(k->hostState) k->hostState->mark_dirty(k->host);
	return step;
}

static float guiTempo(void* ctx)
{
	const float t = atomic_load(&((Tack*)ctx)->tempo);
	return t > 0 ? t : 120.f;
}

static int guiMutateStep(void* ctx)
{
	Tack* k = ctx;
	return kit_walk_step(&k->walk, k->values);
}

static int guiSaveKit(void* ctx, const char* name, bool overwrite)
{
	Tack* k = ctx;
	char path[2048];
	// a voice plugin's kit is the init kit with its voice in place: saved as it is, the kit file has the voice on
	// its own, which the device can load too
	const int r = tack_saveKit(k->values, name, overwrite, path, sizeof path);
	if(r != TACK_SAVE_OK) return r;
	snprintf(k->kitName, sizeof k->kitName, "%s", name);
	snprintf(k->kitPath, sizeof k->kitPath, "%s", path);
	k->kitVoice = k->type->voice;
	k->kitEdited = false;
	// the project now holds this kit file, as if it had been loaded from it
	presetLoaded(k);
	if(k->hostState) k->hostState->mark_dirty(k->host);
	return r;
}

// the editor is embedded in a window of the host's
#if defined(_WIN32)
#define GUI_API CLAP_WINDOW_API_WIN32
#elif defined(__APPLE__)
#define GUI_API CLAP_WINDOW_API_COCOA
#else
#define GUI_API CLAP_WINDOW_API_X11
#endif

static bool guiIsApiSupported(const clap_plugin_t* plugin, const char* api, bool isFloating)
{
	return !strcmp(api, GUI_API) && !isFloating;
}

static bool guiGetPreferredApi(const clap_plugin_t* plugin, const char** api, bool* isFloating)
{
	*api = GUI_API;
	*isFloating = false;
	return true;
}

static void guiDestroy(const clap_plugin_t* plugin)
{
	Tack* k = plugin->plugin_data;
	if(!k->gui) return;
	if(!TACK_GUI_OWN_TIMER) k->hostTimer->unregister_timer(k->host, k->guiTimer);
	tack_guiDestroy(k->gui);
	k->gui = NULL;
	k->guiParented = false;
}

static bool guiCreate(const clap_plugin_t* plugin, const char* api, bool isFloating)
{
	Tack* k = plugin->plugin_data;
	// on X11 the editor needs a timer from the host to handle its window events
	if(!guiIsApiSupported(plugin, api, isFloating) || k->gui || (!TACK_GUI_OWN_TIMER && !k->hostTimer)) return false;
	const TackGuiHost host = { k, guiGetValue, guiBeginEdit, guiSetValue, guiEndEdit, guiKitName, guiSaveKit,
		guiKitPath, guiKitEdited, guiLoadKit, k->type->voice, guiHasParam, guiOffersValue, guiValueText, guiKitVoice,
		guiModulation, guiLoadVoice, guiMutate, guiMutateStep, guiTempo };
	k->gui = tack_guiCreate(&host);
	if(!k->gui) return false;
	if(!TACK_GUI_OWN_TIMER && !k->hostTimer->register_timer(k->host, GUI_TIMER_MS, &k->guiTimer)) {
		tack_guiDestroy(k->gui);
		k->gui = NULL;
		return false;
	}
	return true;
}

static bool guiGetSize(const clap_plugin_t* plugin, uint32_t* width, uint32_t* height)
{
	Tack* k = plugin->plugin_data;
	if(!k->gui) return false;
	tack_guiGetSize(k->gui, width, height);
	return true;
}

static bool guiSetScale(const clap_plugin_t* plugin, double scale)
{
	Tack* k = plugin->plugin_data;
	if(!k->gui || !tack_guiSetScale(k->gui, scale)) return false;
	// a new scale for an editor that is already showing, e.g. on a screen with another DPI: ask for its new size
	uint32_t w, h;
	if(k->guiParented && k->hostGui && guiGetSize(plugin, &w, &h)) k->hostGui->request_resize(k->host, w, h);
	return true;
}

static bool guiCanResize(const clap_plugin_t* plugin) { return false; }
static bool guiGetResizeHints(const clap_plugin_t* plugin, clap_gui_resize_hints_t* hints) { return false; }
static bool guiAdjustSize(const clap_plugin_t* plugin, uint32_t* width, uint32_t* height) { return guiGetSize(plugin, width, height); }

static bool guiSetSize(const clap_plugin_t* plugin, uint32_t width, uint32_t height)
{
	uint32_t w, h;
	return guiGetSize(plugin, &w, &h) && width == w && height == h;
}

static bool guiSetParent(const clap_plugin_t* plugin, const clap_window_t* window)
{
	Tack* k = plugin->plugin_data;
#if defined(_WIN32) || defined(__APPLE__)
	k->guiParented = k->gui && tack_guiSetParent(k->gui, (uintptr_t)window->ptr);
#else
	k->guiParented = k->gui && tack_guiSetParent(k->gui, (uintptr_t)window->x11);
#endif
	return k->guiParented;
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
	k->hostGui = k->host->get_extension(k->host, CLAP_EXT_GUI);
	k->hostState = k->host->get_extension(k->host, CLAP_EXT_STATE);
	snprintf(k->kitName, sizeof k->kitName, "%s", k->type->voice < 0 ? "Init Kit" : "Init");
	k->hostPresetLoad = k->host->get_extension(k->host, CLAP_EXT_PRESET_LOAD);
	if(!k->hostPresetLoad) k->hostPresetLoad = k->host->get_extension(k->host, CLAP_EXT_PRESET_LOAD_COMPAT);
	k->engine.api->init();
	k->separateOutputs = k->type->voice >= 0;
	k->engine.api->setSeparateOutputs(k->separateOutputs);
	// a single voice: the engine computes only that voice and resamples only its output
	k->engine.api->setSoloVoice(k->type->voice);
	uint8_t kit[LXR_HOST_NUM_PARAMS];
	lxr_host_initKit(kit);
	adaptKit(k->type, kit);
	memcpy(k->values, kit, LXR_HOST_NUM_PARAMS);
	loadEngineKit(k, kit);
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
static uint32_t factoryCount(const clap_plugin_factory_t* f) { return NUM_TYPES; }

static const clap_plugin_descriptor_t* factoryDescriptor(const clap_plugin_factory_t* f, uint32_t index)
{
	return index < NUM_TYPES ? &descriptors[index] : NULL;
}

static const clap_plugin_t* factoryCreate(const clap_plugin_factory_t* f, const clap_host_t* host, const char* id)
{
	const TackType* type = NULL;
	for(size_t t = 0; t < NUM_TYPES; t++)
		if(!strcmp(id, types[t].desc->id)) type = &types[t];
	if(!type || !clap_version_is_compatible(host->clap_version)) return NULL;

	Tack* k = calloc(1, sizeof *k);
	if(!k) return NULL;
	if(!tack_engineOpen(&k->engine)) {
		free(k);
		return NULL;
	}
	atomic_flag_clear(&k->pendingLock);
	atomic_store(&k->pendingSeparateOutputs, -1);
	atomic_store(&k->chromatic, true);
	k->kitVoice = -1;
	k->host = host;
	k->type = type;
	k->plugin = (clap_plugin_t){
		.desc = type->desc,
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
static bool entryInit(const char* path)
{
	buildTypes();
	return true;
}
static void entryDeinit(void) {}

static const void* entryGetFactory(const char* id)
{
	if(!strcmp(id, CLAP_PLUGIN_FACTORY_ID)) return &factory;
	if(!strcmp(id, CLAP_PRESET_DISCOVERY_FACTORY_ID) || !strcmp(id, CLAP_PRESET_DISCOVERY_FACTORY_ID_COMPAT))
		return tack_presetDiscoveryFactory();
	return tack_wrapperFactory(id);
}

CLAP_EXPORT const clap_plugin_entry_t clap_entry = { CLAP_VERSION_INIT, entryInit, entryDeinit, entryGetFactory };
