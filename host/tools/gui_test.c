/*
 * gui_test.c
 *
 * Opens Tack's editor in a window of its own, as a host would: loads the plugin, optionally a kit, embeds the
 * editor and drives its timer. For trying the editor without a DAW, or under Xvfb for screenshots.
 *
 *   gui_test Tack.clap [seconds] [kit.snd]
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <dlfcn.h>
#include <X11/Xlib.h>
#include <clap/clap.h>

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

static const void* hostGetExtension(const clap_host_t* h, const char* id)
{
	if(!strcmp(id, CLAP_EXT_TIMER_SUPPORT)) return &timerSupport;
	if(!strcmp(id, CLAP_EXT_PARAMS)) return &hostParams;
	return NULL;
}
static void hostRequest(const clap_host_t* h) {}
static const clap_host_t host = {
	CLAP_VERSION_INIT, NULL, "gui_test", "", "", "0.1", hostGetExtension, hostRequest, hostRequest, hostRequest
};

static uint32_t evSize(const clap_input_events_t* l) { return 0; }
static const clap_event_header_t* evGet(const clap_input_events_t* l, uint32_t i) { return NULL; }
static bool evPush(const clap_output_events_t* l, const clap_event_header_t* e)
{
	if(e->type == CLAP_EVENT_PARAM_VALUE)
		printf("host got param %u = %g\n", ((const clap_event_param_value_t*)e)->param_id, ((const clap_event_param_value_t*)e)->value);
	return true;
}

int main(int argc, char** argv)
{
	if(argc < 2) { fprintf(stderr, "usage: %s plugin.clap [seconds] [kit.snd]\n", argv[0]); return 1; }
	const double seconds = argc > 2 ? atof(argv[2]) : 3600;

	void* lib = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
	if(!lib) { fprintf(stderr, "%s\n", dlerror()); return 1; }
	const clap_plugin_entry_t* entry = dlsym(lib, "clap_entry");
	entry->init(argv[1]);
	const clap_plugin_factory_t* factory = entry->get_factory(CLAP_PLUGIN_FACTORY_ID);
	plugin = factory->create_plugin(factory, &host, factory->get_plugin_descriptor(factory, 0)->id);
	if(!plugin || !plugin->init(plugin)) { fprintf(stderr, "could not create the plugin\n"); return 1; }
	if(argc > 3) {
		const clap_plugin_preset_load_t* pl = plugin->get_extension(plugin, CLAP_EXT_PRESET_LOAD);
		pl->from_location(plugin, CLAP_PRESET_DISCOVERY_LOCATION_FILE, argv[3], NULL);
	}

	const clap_plugin_gui_t* gui = plugin->get_extension(plugin, CLAP_EXT_GUI);
	pluginTimer = plugin->get_extension(plugin, CLAP_EXT_TIMER_SUPPORT);
	const clap_plugin_params_t* params = plugin->get_extension(plugin, CLAP_EXT_PARAMS);
	if(!gui || !gui->create(plugin, CLAP_WINDOW_API_X11, false)) { fprintf(stderr, "no X11 editor\n"); return 1; }

	uint32_t w, h;
	gui->get_size(plugin, &w, &h);
	Display* dpy = XOpenDisplay(NULL);
	if(!dpy) { fprintf(stderr, "no X display\n"); return 1; }
	Window win = XCreateSimpleWindow(dpy, DefaultRootWindow(dpy), 0, 0, w, h, 0, 0, 0);
	XStoreName(dpy, win, "Tack (gui_test)");
	XMapWindow(dpy, win);
	XFlush(dpy);

	clap_window_t cw = { .api = CLAP_WINDOW_API_X11, .x11 = win };
	if(!gui->set_parent(plugin, &cw) || !gui->show(plugin)) { fprintf(stderr, "could not embed the editor\n"); return 1; }
	printf("editor %ux%u open\n", w, h);
	fflush(stdout);

	clap_input_events_t in = { NULL, evSize, evGet };
	clap_output_events_t out = { NULL, evPush };
	const struct timespec tick = { 0, 20 * 1000000 };
	for(double t = 0; t < seconds; t += 0.02) {
		while(XPending(dpy)) { XEvent e; XNextEvent(dpy, &e); }
		if(timerOn) pluginTimer->on_timer(plugin, timerId);
		// not processing audio, so do what a host does when asked to flush
		if(flushRequested) { flushRequested = false; params->flush(plugin, &in, &out); fflush(stdout); }
		nanosleep(&tick, NULL);
	}

	gui->destroy(plugin);
	plugin->destroy(plugin);
	XDestroyWindow(dpy, win);
	XCloseDisplay(dpy);
	entry->deinit();
	return 0;
}
