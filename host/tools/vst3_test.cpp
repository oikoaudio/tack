/*
 * vst3_test.cpp
 *
 * Checks Tack.vst3 the way a VST3 host uses it: the five plugins are there and each one plays; MIDI channels
 * reach the plugin (channel 1 is the kit, channel 2 plays drum 1 chromatically, channel 10 nothing); several Tack
 * instances at once are independent (each its own engine copy); a saved state brings back what a render depends on.
 *
 *   vst3_test Tack.vst3
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/eventlist.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "public.sdk/source/vst/hosting/processdata.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"

using namespace Steinberg;
using namespace Steinberg::Vst;

static constexpr int RATE = 48000;
static constexpr int BLOCK = 256;
static constexpr int FRAMES = RATE * 2;

// Drum 1 Filter Frequency, a kit parameter (the firmware's number, which is also the VST3 parameter id)
static constexpr ParamID FILTER_FREQ = 37;

static HostApplication hostApp;
static VST3::Hosting::Module::Ptr module;

[[noreturn]] static void fail(const std::string& what)
{
	fprintf(stderr, "FAILED: %s\n", what.c_str());
	exit(1);
}

struct Note
{
	int16 channel;
	int16 key;
};

class Instance
{
public:
	explicit Instance(const VST3::UID& cid)
	{
		component = module->getFactory().createInstance<IComponent>(cid);
		if(!component || component->initialize(&hostApp) != kResultOk) fail("could not create an instance");
		processor = FUnknownPtr<IAudioProcessor>(component);
		controller = FUnknownPtr<IEditController>(component);
		if(!processor || !controller) fail("no processor or controller in the component");
		ProcessSetup setup{ kRealtime, kSample32, BLOCK, RATE };
		if(processor->setupProcessing(setup) != kResultOk) fail("setupProcessing");
		// Main and the event input; the other outputs stay off, as a host leaves them until they are routed
		component->activateBus(kAudio, kOutput, 0, true);
		component->activateBus(kEvent, kInput, 0, true);
		data.prepare(*component, BLOCK, kSample32);
		data.numSamples = BLOCK;
		data.inputEvents = &events;
		data.inputParameterChanges = &changes;
		if(component->setActive(true) != kResultOk) fail("setActive");
		processor->setProcessing(true);
		out.assign(FRAMES * 2, 0.f);
	}

	~Instance()
	{
		processor->setProcessing(false);
		component->setActive(false);
		data.unprepare();
		component->terminate();
	}

	// before the first block: a parameter change and notes at frame 0
	void schedule(std::vector<Note> notes, int filter = -1)
	{
		pendingNotes = std::move(notes);
		pendingFilter = filter;
	}

	// renders the next block; false when the render is complete
	bool block()
	{
		if(pos >= FRAMES) return false;
		events.clear();
		changes.clearQueue();
		if(pos == 0) {
			for(const Note& n : pendingNotes) {
				Event e{};
				e.type = Event::kNoteOnEvent;
				e.noteOn.channel = n.channel;
				e.noteOn.pitch = n.key;
				e.noteOn.velocity = 1.f;
				e.noteOn.noteId = -1;
				events.addEvent(e);
			}
			if(pendingFilter >= 0) {
				int32 index;
				IParamValueQueue* q = changes.addParameterData(FILTER_FREQ, index);
				q->addPoint(0, pendingFilter / 127.0, index);
			}
		}
		if(processor->process(data) != kResultOk) fail("process");
		const AudioBusBuffers& main = data.outputs[0];
		for(int i = 0; i < BLOCK; i++) {
			out[(pos + i) * 2] = main.channelBuffers32[0][i];
			out[(pos + i) * 2 + 1] = main.channelBuffers32[1][i];
		}
		pos += BLOCK;
		return true;
	}

	const std::vector<float>& render()
	{
		while(block()) {}
		return out;
	}

	IPtr<MemoryStream> saveState()
	{
		auto s = owned(new MemoryStream);
		if(component->getState(s) != kResultOk) fail("getState");
		return s;
	}

	void loadState(MemoryStream* s)
	{
		int64 at;
		s->seek(0, IBStream::kIBSeekSet, &at);
		if(component->setState(s) != kResultOk) fail("setState");
		s->seek(0, IBStream::kIBSeekSet, &at);
		controller->setComponentState(s);
	}

	std::vector<float> out;

private:
	IPtr<IComponent> component;
	FUnknownPtr<IAudioProcessor> processor;
	FUnknownPtr<IEditController> controller;
	HostProcessData data;
	EventList events{ 64 };
	ParameterChanges changes{ 4 };
	std::vector<Note> pendingNotes;
	int pendingFilter = -1;
	int pos = 0;
};

static double rms(const std::vector<float>& v)
{
	double sum = 0;
	for(float x : v) sum += (double)x * x;
	return std::sqrt(sum / v.size());
}

static void step(const char* what) { printf("== %s\n", what); }

int main(int argc, char** argv)
{
	if(argc != 2) {
		fprintf(stderr, "usage: vst3_test Tack.vst3\n");
		return 2;
	}
	std::string error;
	module = VST3::Hosting::Module::create(argv[1], error);
	if(!module) fail("could not load " + std::string(argv[1]) + ": " + error);

	step("the plugins");
	const char* const expected[] = { "Tack", "Tack Drum", "Tack Snare", "Tack Cymbal", "Tack Hi-hat" };
	std::vector<VST3::UID> cids;
	for(const auto& info : module->getFactory().classInfos())
		if(info.category() == kVstAudioEffectClass) {
			if(cids.size() >= 5 || info.name() != expected[cids.size()]) fail("unexpected plugin " + info.name());
			cids.push_back(info.ID());
		}
	if(cids.size() != 5) fail("expected 5 plugins");

	for(size_t i = 0; i < cids.size(); i++) {
		step(expected[i]);
		Instance in(cids[i]);
		// the voice plugins play notes on any channel
		in.schedule({ { 0, 60 } });
		if(rms(in.render()) < 1e-3) fail(std::string(expected[i]) + " is silent");
	}
	const VST3::UID& tack = cids[0];

	step("MIDI channels");
	auto renderNote = [&](Note n) {
		Instance in(tack);
		in.schedule({ n });
		return in.render();
	};
	const auto drum2 = renderNote({ 0, 61 });		// channel 1, C#3: drum 2
	const auto drum1Up = renderNote({ 1, 61 });		// channel 2, C#3: drum 1 a semitone up
	if(rms(drum2) < 1e-3 || rms(drum1Up) < 1e-3) fail("a note was silent");
	if(drum2 == drum1Up) fail("channel 2 played as channel 1");
	if(rms(renderNote({ 9, 60 })) != 0) fail("channel 10 played a note");

	step("independent instances");
	const int filters[] = { 20, 70, 120 };
	std::vector<std::vector<float>> alone;
	for(int f : filters) {
		Instance in(tack);
		in.schedule({ { 0, 60 } }, f);
		alone.push_back(in.render());
	}
	if(alone[0] == alone[1]) fail("the filter parameter made no difference");
	{
		std::vector<std::unique_ptr<Instance>> together;
		for(int f : filters) {
			together.push_back(std::make_unique<Instance>(tack));
			together.back()->schedule({ { 0, 60 } }, f);
		}
		for(bool more = true; more; ) {
			more = false;
			for(auto& in : together) more |= in->block();
		}
		for(size_t i = 0; i < together.size(); i++)
			if(together[i]->out != alone[i]) fail("instance " + std::to_string(i) + " differs when not alone");
	}

	step("state round trip");
	{
		Instance a(tack);
		a.schedule({}, 30);
		a.render();
		auto state = a.saveState();
		Instance b(tack);
		b.loadState(state);
		b.schedule({ { 0, 60 } });
		Instance c(tack);
		c.schedule({ { 0, 60 } }, 30);
		if(b.render() != c.render()) fail("the restored instance renders differently");
	}

	printf("all passed\n");
	module.reset();
	return 0;
}
