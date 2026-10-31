/*
 * gui_test.c
 *
 * Opens Tack's editor in a window of its own, as a host would: loads the plugin, optionally a kit, embeds the
 * editor in a plain pugl window and drives it. For trying the editor without a DAW, or under Xvfb (or Wine) for
 * screenshots.
 *
 *   gui_test Tack.clap [seconds] [kit.snd]
 *
 * $TACK_MOD="id~amount ..." modulates parameters as a host's modulators would (plugin values), to show it.
 * $TACK_SCALE="factor" scales the editor as a host does for a high-DPI screen (set_scale; X11 and Windows), and
 * $TACK_RESCALE="factor@seconds" changes the scale while it shows, as when the window moves to another screen.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pugl/pugl.h>
#include <pugl/stub.h>
#include <clap/clap.h>
#include "clap_load.h"

static const clap_plugin_t* plugin;
static const clap_plugin_timer_support_t* pluginTimer;
static clap_id timerId = 1;
static bool timerOn;

static bool registerTimer(const clap_host_t* h, uint32_t ms, clap_id* id) { *id = timerId; timerOn = true; return true; }
static bool unregisterTimer(const clap_host_t* h, clap_id id) { timerOn = false; return true; }
static const clap_host_timer_support_t timerSupport = { registerTimer, unregisterTimer };

static void rescan(const clap_host_t* h, clap_param_rescan_flags f) {}
static void clear(const clap_host_t* h, clap_id id, clap_param_clear_flags f) {}
static bool flushRequested;
static void requestFlush(const clap_host_t* h) { flushRequested = true; }
static const clap_host_params_t hostParams = { rescan, clear, requestFlush };

// the host's window, which the editor asks to resize after a new scale
static PuglView* window;
// pugl sizes windows in pixels, but on Cocoa the editor's sizes are in points
static PuglSpan pixels(uint32_t size)
{
#ifdef __APPLE__
	return (PuglSpan)lround(size * puglGetScaleFactor(window));
#else
	return (PuglSpan)size;
#endif
}
static void resizeHintsChanged(const clap_host_t* h) {}
static bool requestResize(const clap_host_t* h, uint32_t width, uint32_t height)
{
	printf("editor asks for %ux%u\n", width, height);
	fflush(stdout);
	return puglSetSizeHint(window, PUGL_CURRENT_SIZE, pixels(width), pixels(height)) == PUGL_SUCCESS;
}
static bool requestShow(const clap_host_t* h) { return false; }
static void closed(const clap_host_t* h, bool wasDestroyed) {}
static const clap_host_gui_t hostGui = { resizeHintsChanged, requestResize, requestShow, requestShow, closed };

static const void* hostGetExtension(const clap_host_t* h, const char* id)
{
	if(!strcmp(id, CLAP_EXT_TIMER_SUPPORT)) return &timerSupport;
	if(!strcmp(id, CLAP_EXT_GUI)) return &hostGui;
	if(!strcmp(id, CLAP_EXT_PARAMS)) return &hostParams;
	return NULL;
}
static void hostRequest(const clap_host_t* h) {}
static const clap_host_t host = {
	CLAP_VERSION_INIT, NULL, "gui_test", "", "", "0.1", hostGetExtension, hostRequest, hostRequest, hostRequest
};

// the modulation from $TACK_MOD, sent with the first flush
#define MAX_MODS 32
static clap_event_param_mod_t mods[MAX_MODS];
static uint32_t numMods;

static uint32_t evSize(const clap_input_events_t* l) { return numMods; }
static const clap_event_header_t* evGet(const clap_input_events_t* l, uint32_t i) { return &mods[i].header; }
static bool evPush(const clap_output_events_t* l, const clap_event_header_t* e)
{
	if(e->type == CLAP_EVENT_PARAM_VALUE)
		printf("host got param %u = %g\n", ((const clap_event_param_value_t*)e)->param_id, ((const clap_event_param_value_t*)e)->value);
	return true;
}

static bool quit;
static PuglStatus onWindowEvent(PuglView* view, const PuglEvent* event)
{
	if(event->type == PUGL_CLOSE) quit = true;
	return PUGL_SUCCESS;
}

int main(int argc, char** argv)
{
	if(argc < 2) { fprintf(stderr, "usage: %s plugin.clap [seconds] [kit.snd]\n", argv[0]); return 1; }
	const double seconds = argc > 2 ? atof(argv[2]) : 3600;

	ClapFile lib;
	if(!clapLoad(&lib, argv[1])) return 1;
	const clap_plugin_factory_t* factory = lib.entry->get_factory(CLAP_PLUGIN_FACTORY_ID);
	// the first plugin in the file, or the one $TACK_PLUGIN_ID names
	const char* id = getenv("TACK_PLUGIN_ID");
	plugin = factory->create_plugin(factory, &host, id ? id : factory->get_plugin_descriptor(factory, 0)->id);
	if(!plugin || !plugin->init(plugin)) { fprintf(stderr, "could not create the plugin\n"); return 1; }
	if(argc > 3) {
		const clap_plugin_preset_load_t* pl = plugin->get_extension(plugin, CLAP_EXT_PRESET_LOAD);
		pl->from_location(plugin, CLAP_PRESET_DISCOVERY_LOCATION_FILE, argv[3], NULL);
	}

	const clap_plugin_gui_t* gui = plugin->get_extension(plugin, CLAP_EXT_GUI);
	pluginTimer = plugin->get_extension(plugin, CLAP_EXT_TIMER_SUPPORT);
	const clap_plugin_params_t* params = plugin->get_extension(plugin, CLAP_EXT_PARAMS);
	const char* api = NULL;
	bool floating;
	if(!gui || !gui->get_preferred_api(plugin, &api, &floating) || !gui->create(plugin, api, false)) {
		fprintf(stderr, "no editor\n");
		return 1;
	}

	const char* scale = getenv("TACK_SCALE");
	if(scale && !gui->set_scale(plugin, atof(scale))) printf("the editor doesn't take a scale here\n");

	// the host's window: a plain view the editor goes into
	uint32_t w, h;
	gui->get_size(plugin, &w, &h);
	PuglWorld* world = puglNewWorld(PUGL_PROGRAM, 0);
	puglSetWorldString(world, PUGL_CLASS_NAME, "TackGuiTest");
	window = puglNewView(world);
	puglSetBackend(window, puglStubBackend());
	puglSetSizeHint(window, PUGL_DEFAULT_SIZE, pixels(w), pixels(h));
	puglSetViewString(window, PUGL_WINDOW_TITLE, "Tack (gui_test)");
	puglSetEventFunc(window, onWindowEvent);
	if(puglRealize(window) || puglShow(window, PUGL_SHOW_RAISE)) { fprintf(stderr, "no window\n"); return 1; }

	clap_window_t cw = { .api = api };
	if(!strcmp(api, CLAP_WINDOW_API_X11)) cw.x11 = (clap_xwnd)puglGetNativeView(window);
	else cw.ptr = (void*)puglGetNativeView(window);
	if(!gui->set_parent(plugin, &cw) || !gui->show(plugin)) { fprintf(stderr, "could not embed the editor\n"); return 1; }
	printf("editor %ux%u open (%s)\n", w, h, api);
	fflush(stdout);

	clap_input_events_t in = { NULL, evSize, evGet };
	clap_output_events_t out = { NULL, evPush };
	for(const char* m = getenv("TACK_MOD"); m && *m && numMods < MAX_MODS; ) {
		char* end;
		const long pid = strtol(m, &end, 10);
		if(*end != '~') break;
		const double amount = strtod(end + 1, &end);
		mods[numMods++] = (clap_event_param_mod_t){
			.header = { sizeof(clap_event_param_mod_t), 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_MOD, 0 },
			.param_id = (clap_id)pid, .note_id = -1, .port_index = -1, .channel = -1, .key = -1, .amount = amount,
		};
		m = end + strspn(end, " ,");
	}
	if(numMods) { params->flush(plugin, &in, &out); numMods = 0; }

	double rescale = 0, rescaleAt = -1;
	if(getenv("TACK_RESCALE")) sscanf(getenv("TACK_RESCALE"), "%lf@%lf", &rescale, &rescaleAt);

	// the host's event loop: on Windows and macOS it also delivers the editor's events
	const double start = puglGetTime(world);
	while(!quit && puglGetTime(world) - start < seconds) {
		if(rescaleAt >= 0 && puglGetTime(world) - start >= rescaleAt) {
			rescaleAt = -1;
			if(!gui->set_scale(plugin, rescale)) printf("the editor doesn't take a scale here\n");
		}
		puglUpdate(world, 0.02);
		if(timerOn) pluginTimer->on_timer(plugin, timerId);
		// not processing audio, so do what a host does when asked to flush
		if(flushRequested) { flushRequested = false; params->flush(plugin, &in, &out); fflush(stdout); }
	}

	gui->destroy(plugin);
	plugin->destroy(plugin);
	puglFreeView(window);
	puglFreeWorld(world);
	clapUnload(&lib);
	return 0;
}
