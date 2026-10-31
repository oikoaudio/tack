/*
 * clap_render.c
 *
 * Minimal CLAP host for testing Tack: loads the plugin, plays notes, writes all outputs summed to a WAV.
 *
 *   clap_render Tack.clap out.wav [rate] [-s] [-p kit.snd] [[channel:]note | id=value | id~amount [@seconds] ...]
 *
 * Notes are MIDI note numbers, played at velocity 1.0 every 0.75 s (default: the 7 voices, notes 60-66), on MIDI
 * channel 1 unless a channel (1-16) is given: 2:48 plays drum 1 an octave below its tuned pitch.
 * id=value sets a parameter (plugin value) through params.flush before playing; id=value@seconds sends it as an
 * event to process() at that time, as automation would. id~amount and id~amount@seconds do the same with the
 * host's modulation of the parameter (CLAP_EVENT_PARAM_MOD, in plugin values) instead of its value.
 * -s saves the state after setting parameters, destroys the instance, creates a new one and loads the state into it
 * before playing, so the output should match a render without -s.
 * -p loads a kit file through the preset-load extension, as a host's preset browser would, before anything else.
 * $TACK_PLUGIN_ID picks one of the plugins in the file (e.g. com.oikoaudio.tack.snare); default the first.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <dlfcn.h>
#include <clap/clap.h>
#include "lxr_host.h"

#define BLOCK 256
#define MAX_ITEMS 64

static const void* hostGetExtension(const clap_host_t* h, const char* id) { return NULL; }
static void hostRequest(const clap_host_t* h) {}

static const clap_host_t host = {
	CLAP_VERSION_INIT, NULL, "clap_render", "", "", "0.1",
	hostGetExtension, hostRequest, hostRequest, hostRequest
};

// input event lists backed by an array of note or param events
typedef union {
	clap_event_header_t header; clap_event_note_t note; clap_event_param_value_t param; clap_event_param_mod_t mod;
} AnyEvent;
typedef struct { AnyEvent ev[MAX_ITEMS]; uint32_t count; } EventList;

static uint32_t evSize(const clap_input_events_t* l) { return ((const EventList*)l->ctx)->count; }
static const clap_event_header_t* evGet(const clap_input_events_t* l, uint32_t i) { return &((const EventList*)l->ctx)->ev[i].header; }
static bool evPush(const clap_output_events_t* l, const clap_event_header_t* e) { return true; }

// state goes through a memory buffer
typedef struct { uint8_t data[4096]; size_t size, pos; } Buffer;

static int64_t bufWrite(const clap_ostream_t* s, const void* src, uint64_t n)
{
	Buffer* b = s->ctx;
	if(n > sizeof b->data - b->size) n = sizeof b->data - b->size;
	memcpy(b->data + b->size, src, n);
	b->size += n;
	return n;
}

static int64_t bufRead(const clap_istream_t* s, void* dst, uint64_t n)
{
	Buffer* b = s->ctx;
	// hand the data out in small pieces, as some hosts do
	if(n > 7) n = 7;
	if(n > b->size - b->pos) n = b->size - b->pos;
	memcpy(dst, b->data + b->pos, n);
	b->pos += n;
	return n;
}

static const clap_plugin_t* createPlugin(const clap_plugin_factory_t* factory, const char* id)
{
	const clap_plugin_t* plugin = factory->create_plugin(factory, &host, id);
	if(!plugin || !plugin->init(plugin)) {
		fprintf(stderr, "could not create %s\n", id);
		exit(1);
	}
	return plugin;
}

int main(int argc, char** argv)
{
	if(argc < 3) {
		fprintf(stderr, "usage: %s plugin.clap out.wav [rate] [-s] [-p kit.snd] [[channel:]note | id=value | id~amount ...]\n", argv[0]);
		return 1;
	}
	const double fs = argc > 3 ? atof(argv[3]) : 48000;
	int notes[MAX_ITEMS] = {60, 61, 62, 63, 64, 65, 66}, channels[MAX_ITEMS] = {0}, numNotes = 0;
	EventList paramEvents = {0};
	struct { AnyEvent ev; uint32_t at; } timed[MAX_ITEMS];
	int numTimed = 0;
	bool roundTrip = false;
	const char* kitPath = NULL;
	for(int i = 4; i < argc; i++) {
		const char* eq = strchr(argv[i], '=');
		const char* tilde = strchr(argv[i], '~');
		// modulation: an event like a value change, but CLAP_EVENT_PARAM_MOD
		const AnyEvent mod = { .mod = {
			.header = { sizeof(clap_event_param_mod_t), 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_MOD, 0 },
			.param_id = atoi(argv[i]), .note_id = -1, .port_index = -1, .channel = -1, .key = -1,
			.amount = tilde ? atof(tilde + 1) : 0,
		} };
		if(!strcmp(argv[i], "-s")) {
			roundTrip = true;
		} else if(!strcmp(argv[i], "-p") && i + 1 < argc) {
			kitPath = argv[++i];
		} else if(tilde && strchr(tilde, '@') && numTimed < MAX_ITEMS) {
			timed[numTimed].ev = mod;
			timed[numTimed++].at = (uint32_t)(atof(strchr(tilde, '@') + 1) * fs);
		} else if(tilde && paramEvents.count < MAX_ITEMS) {
			paramEvents.ev[paramEvents.count++] = mod;
		} else if(eq && strchr(eq, '@') && numTimed < MAX_ITEMS) {
			timed[numTimed].ev.param = (clap_event_param_value_t){
				.header = { sizeof(clap_event_param_value_t), 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_VALUE, 0 },
				.param_id = atoi(argv[i]), .note_id = -1, .port_index = -1, .channel = -1, .key = -1, .value = atof(eq + 1),
			};
			timed[numTimed++].at = (uint32_t)(atof(strchr(eq, '@') + 1) * fs);
		} else if(eq && paramEvents.count < MAX_ITEMS) {
			paramEvents.ev[paramEvents.count++].param = (clap_event_param_value_t){
				.header = { sizeof(clap_event_param_value_t), 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_VALUE, 0 },
				.param_id = atoi(argv[i]), .note_id = -1, .port_index = -1, .channel = -1, .key = -1, .value = atof(eq + 1),
			};
		} else if(numNotes < MAX_ITEMS) {
			const char* colon = strchr(argv[i], ':');
			channels[numNotes] = colon ? atoi(argv[i]) - 1 : 0;
			notes[numNotes++] = atoi(colon ? colon + 1 : argv[i]);
		}
	}
	if(numNotes == 0) numNotes = 7;

	void* lib = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
	if(!lib) { fprintf(stderr, "%s\n", dlerror()); return 1; }
	const clap_plugin_entry_t* entry = dlsym(lib, "clap_entry");
	entry->init(argv[1]);
	const clap_plugin_factory_t* factory = entry->get_factory(CLAP_PLUGIN_FACTORY_ID);
	// the first plugin in the file, or the one $TACK_PLUGIN_ID names
	const char* wanted = getenv("TACK_PLUGIN_ID");
	const clap_plugin_descriptor_t* desc = factory->get_plugin_descriptor(factory, 0);
	for(uint32_t i = 0; wanted && i < factory->get_plugin_count(factory); i++)
		if(!strcmp(factory->get_plugin_descriptor(factory, i)->id, wanted)) desc = factory->get_plugin_descriptor(factory, i);
	const clap_plugin_t* plugin = createPlugin(factory, desc->id);
	printf("%s: %s\n", desc->name, desc->description);

	if(kitPath) {
		const clap_plugin_preset_load_t* pl = plugin->get_extension(plugin, CLAP_EXT_PRESET_LOAD);
		if(!pl || !pl->from_location(plugin, CLAP_PRESET_DISCOVERY_LOCATION_FILE, kitPath, NULL)) {
			fprintf(stderr, "could not load %s\n", kitPath);
			return 1;
		}
		printf("loaded kit %s\n", kitPath);
	}

	clap_output_events_t outEvents = { NULL, evPush };
	clap_input_events_t paramIn = { &paramEvents, evSize, evGet };
	if(paramEvents.count) {
		const clap_plugin_params_t* params = plugin->get_extension(plugin, CLAP_EXT_PARAMS);
		params->flush(plugin, &paramIn, &outEvents);
		for(uint32_t i = 0; i < paramEvents.count; i++) {
			double v;
			char text[64];
			const clap_id id = paramEvents.ev[i].param.param_id;
			params->get_value(plugin, id, &v);
			params->value_to_text(plugin, id, v, text, sizeof text);
			printf("param %u = %g (%s)\n", id, v, text);
		}
	}

	if(roundTrip) {
		Buffer buf = {0};
		const clap_plugin_state_t* st = plugin->get_extension(plugin, CLAP_EXT_STATE);
		clap_ostream_t os = { &buf, bufWrite };
		if(!st->save(plugin, &os)) { fprintf(stderr, "state save failed\n"); return 1; }
		plugin->destroy(plugin);

		plugin = createPlugin(factory, desc->id);
		st = plugin->get_extension(plugin, CLAP_EXT_STATE);
		clap_istream_t is = { &buf, bufRead };
		if(!st->load(plugin, &is)) { fprintf(stderr, "state load failed\n"); return 1; }
		printf("state round trip: %zu bytes\n", buf.size);
	}

	if(!plugin->activate(plugin, fs, 1, BLOCK) || !plugin->start_processing(plugin)) {
		fprintf(stderr, "could not start %s\n", desc->id);
		return 1;
	}
	const clap_plugin_latency_t* lat = plugin->get_extension(plugin, CLAP_EXT_LATENCY);
	if(lat) printf("latency %u frames\n", lat->get(plugin));

	const uint32_t step = (uint32_t)(0.75 * fs), preroll = (uint32_t)(0.01 * fs);
	const uint32_t numFrames = preroll + numNotes * step + (uint32_t)(0.25 * fs);
	int16_t* audio = calloc(numFrames * 2, sizeof(int16_t));

	// a stereo buffer for every output port the plugin has; the file gets their sum
	const clap_plugin_audio_ports_t* ports = plugin->get_extension(plugin, CLAP_EXT_AUDIO_PORTS);
	const uint32_t numPorts = ports ? ports->count(plugin, false) : 0;
	static float portData[MAX_ITEMS][2][BLOCK];
	static float* portChannels[MAX_ITEMS][2];
	clap_audio_buffer_t outs[MAX_ITEMS] = {0};
	for(uint32_t i = 0; i < numPorts && i < MAX_ITEMS; i++) {
		portChannels[i][0] = portData[i][0];
		portChannels[i][1] = portData[i][1];
		outs[i] = (clap_audio_buffer_t){ .data32 = portChannels[i], .channel_count = 2 };
	}
	printf("%u output ports\n", numPorts);
	EventList noteEvents = {0};
	clap_input_events_t in = { &noteEvents, evSize, evGet };
	clap_process_t p = {
		.steady_time = 0, .audio_outputs = outs, .audio_outputs_count = numPorts, .in_events = &in, .out_events = &outEvents,
	};

	int next = 0;
	for(uint32_t pos = 0; pos < numFrames; pos += BLOCK)
	{
		const uint32_t n = numFrames - pos < BLOCK ? numFrames - pos : BLOCK;
		noteEvents.count = 0;
		// events in time order: parameter changes before a note at the same frame
		for(int i = 0; i < numTimed; i++)
			if(timed[i].at >= pos && timed[i].at < pos + n) {
				noteEvents.ev[noteEvents.count] = timed[i].ev;
				noteEvents.ev[noteEvents.count++].header.time = timed[i].at - pos;
			}
		const uint32_t t = preroll + next * step;
		if(next < numNotes && t >= pos && t < pos + n) {
			noteEvents.ev[noteEvents.count].note = (clap_event_note_t){
				.header = { sizeof(clap_event_note_t), t - pos, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_NOTE_ON, 0 },
				.note_id = -1, .port_index = 0, .channel = (int16_t)channels[next], .key = notes[next], .velocity = 1.0,
			};
			noteEvents.count++;
			next++;
		}
		p.frames_count = n;
		p.steady_time = pos;
		plugin->process(plugin, &p);
		for(uint32_t i = 0; i < n; i++) {
			float l = 0, r = 0;
			for(uint32_t port = 0; port < numPorts; port++) {
				l += portData[port][0][i];
				r += portData[port][1][i];
			}
			audio[(pos + i)*2 + 0] = sat16(lrintf(l * 32768.f));
			audio[(pos + i)*2 + 1] = sat16(lrintf(r * 32768.f));
		}
	}

	plugin->stop_processing(plugin);
	plugin->deactivate(plugin);
	plugin->destroy(plugin);
	entry->deinit();
	dlclose(lib);

	if(lxr_host_writeWav(argv[2], audio, numFrames, 2, (uint32_t)fs)) { perror(argv[2]); return 1; }
	printf("wrote %u frames at %u Hz to %s\n", numFrames, (uint32_t)fs, argv[2]);
	free(audio);
	return 0;
}
