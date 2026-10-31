/*
 * multi_test.c
 *
 * Checks that Tack instances are independent: renders a different kit in each of several instances at once,
 * first interleaved on one thread, then on one thread per instance, and compares every result with the same kit
 * rendered by a single instance alone. Any shared engine state shows up as a difference.
 *
 *   multi_test Tack.clap kit.snd...
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <clap/clap.h>
#include "clap_load.h"

#define BLOCK 256
#define FRAMES (48000 * 4)
#define MAX_INSTANCES 8
#define PORTS 9

static const void* hostGetExtension(const clap_host_t* h, const char* id) { return NULL; }
static void hostRequest(const clap_host_t* h) {}
static const clap_host_t host = {
	CLAP_VERSION_INIT, NULL, "multi_test", "", "", "0.1", hostGetExtension, hostRequest, hostRequest, hostRequest
};

typedef struct { clap_event_note_t ev[8]; uint32_t count; } Events;
static uint32_t evSize(const clap_input_events_t* l) { return ((const Events*)l->ctx)->count; }
static const clap_event_header_t* evGet(const clap_input_events_t* l, uint32_t i) { return &((const Events*)l->ctx)->ev[i].header; }
static bool evPush(const clap_output_events_t* l, const clap_event_header_t* e) { return true; }

typedef struct
{
	const clap_plugin_t* plugin;
	float* out;		// FRAMES * 2, Main L/R
	uint32_t pos;
} Instance;

static const clap_plugin_factory_t* factory;
static const char* pluginId;

static void create(Instance* in, const char* kit)
{
	in->plugin = factory->create_plugin(factory, &host, pluginId);
	if(!in->plugin || !in->plugin->init(in->plugin)) { fprintf(stderr, "could not create an instance\n"); exit(1); }
	const clap_plugin_preset_load_t* pl = in->plugin->get_extension(in->plugin, CLAP_EXT_PRESET_LOAD);
	if(!pl->from_location(in->plugin, CLAP_PRESET_DISCOVERY_LOCATION_FILE, kit, NULL)) { fprintf(stderr, "could not load %s\n", kit); exit(1); }
	in->plugin->activate(in->plugin, 48000, 1, BLOCK);
	in->plugin->start_processing(in->plugin);
	in->out = calloc(FRAMES * 2, sizeof(float));
	in->pos = 0;
}

static void destroy(Instance* in)
{
	in->plugin->stop_processing(in->plugin);
	in->plugin->deactivate(in->plugin);
	in->plugin->destroy(in->plugin);
}

// one block: every voice is hit in turn, a new note every 0.25 s
static void processBlock(Instance* in)
{
	static __thread float ports[PORTS][2][BLOCK];
	static __thread float* channels[PORTS][2];
	clap_audio_buffer_t outs[PORTS];
	for(int p = 0; p < PORTS; p++) {
		channels[p][0] = ports[p][0];
		channels[p][1] = ports[p][1];
		outs[p] = (clap_audio_buffer_t){ .data32 = channels[p], .channel_count = 2 };
	}
	Events events = {0};
	clap_input_events_t inEvents = { &events, evSize, evGet };
	clap_output_events_t outEvents = { NULL, evPush };
	const uint32_t step = 12000;
	const uint32_t t = (in->pos + step - 1) / step * step;
	if(t < in->pos + BLOCK) {
		events.ev[0] = (clap_event_note_t){
			.header = { sizeof(clap_event_note_t), t - in->pos, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_NOTE_ON, 0 },
			.note_id = -1, .port_index = 0, .channel = 0, .key = 60 + (t / step) % 7, .velocity = 0.9,
		};
		events.count = 1;
	}
	clap_process_t pr = { .steady_time = in->pos, .frames_count = BLOCK, .audio_outputs = outs,
		.audio_outputs_count = PORTS, .in_events = &inEvents, .out_events = &outEvents };
	in->plugin->process(in->plugin, &pr);
	for(int i = 0; i < BLOCK; i++) {
		in->out[(in->pos + i) * 2 + 0] = ports[0][0][i];
		in->out[(in->pos + i) * 2 + 1] = ports[0][1][i];
	}
	in->pos += BLOCK;
}

static void* runThread(void* arg)
{
	Instance* in = arg;
	while(in->pos + BLOCK <= FRAMES) processBlock(in);
	return NULL;
}

static int compare(const char* what, const float* a, const float* b)
{
	for(int i = 0; i < FRAMES * 2; i++)
		if(a[i] != b[i]) {
			printf("  %s: differs from frame %d\n", what, i / 2);
			return 1;
		}
	double e = 0;
	for(int i = 0; i < FRAMES * 2; i++) e += a[i] * a[i];
	printf("  %s: identical (energy %.1f)\n", what, e);
	return 0;
}

int main(int argc, char** argv)
{
	if(argc < 3) { fprintf(stderr, "usage: %s plugin.clap kit.snd...\n", argv[0]); return 1; }
	const int n = argc - 2 < MAX_INSTANCES ? argc - 2 : MAX_INSTANCES;

	ClapFile lib;
	if(!clapLoad(&lib, argv[1])) return 1;
	factory = lib.entry->get_factory(CLAP_PLUGIN_FACTORY_ID);
	pluginId = factory->get_plugin_descriptor(factory, 0)->id;

	// each kit alone
	float* solo[MAX_INSTANCES];
	for(int i = 0; i < n; i++) {
		Instance in;
		create(&in, argv[i + 2]);
		while(in.pos + BLOCK <= FRAMES) processBlock(&in);
		destroy(&in);
		solo[i] = in.out;
	}

	int failures = 0;
	Instance inst[MAX_INSTANCES];

	printf("%d instances, interleaved on one thread:\n", n);
	for(int i = 0; i < n; i++) create(&inst[i], argv[i + 2]);
	while(inst[0].pos + BLOCK <= FRAMES)
		for(int i = 0; i < n; i++) processBlock(&inst[i]);
	for(int i = 0; i < n; i++) { failures += compare(argv[i + 2], inst[i].out, solo[i]); destroy(&inst[i]); free(inst[i].out); }

	printf("%d instances, one thread each:\n", n);
	pthread_t threads[MAX_INSTANCES];
	for(int i = 0; i < n; i++) create(&inst[i], argv[i + 2]);
	for(int i = 0; i < n; i++) pthread_create(&threads[i], NULL, runThread, &inst[i]);
	for(int i = 0; i < n; i++) pthread_join(threads[i], NULL);
	for(int i = 0; i < n; i++) { failures += compare(argv[i + 2], inst[i].out, solo[i]); destroy(&inst[i]); free(inst[i].out); }

	for(int i = 0; i < n; i++) free(solo[i]);
	clapUnload(&lib);
	printf(failures ? "FAILED\n" : "all instances independent\n");
	return failures ? 1 : 0;
}
