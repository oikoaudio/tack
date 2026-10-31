/*
 * wrapper_info.cpp
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * What clap-wrapper can't take from the CLAP descriptors: the VST3 factory's vendor details and the Audio Unit
 * identities. An AU is known to hosts by type, subtype and manufacturer code, saved in their projects, so these codes
 * must never change. (The VST3 class ids are hashes of the CLAP ids.)
 */

#include <clap/clap.h>
#include <clapwrapper/auv2.h>
#include <clapwrapper/vst3.h>
#include <cstring>
#include "wrapper_info.h"
#include "presets.h"

namespace
{
// no contact address: the wrapper would show "info@" for none
const clap_plugin_factory_as_vst3 vst3Factory = { nullptr, "https://github.com/oikoaudio/tack", "", nullptr, nullptr };

struct AuIdentity
{
	const char* pluginId;
	const char* subtype;
};

const AuIdentity auIdentities[] =
{
	{ TACK_PLUGIN_ID, "Tack" },
	{ TACK_PLUGIN_ID ".drum", "Tdrm" },
	{ TACK_PLUGIN_ID ".snare", "Tsnr" },
	{ TACK_PLUGIN_ID ".cymbal", "Tcym" },
	{ TACK_PLUGIN_ID ".hihat", "That" },
};

bool auv2Info(const clap_plugin_factory_as_auv2* f, uint32_t index, clap_plugin_info_as_auv2_t* info)
{
	const auto* plugins = static_cast<const clap_plugin_factory_t*>(clap_entry.get_factory(CLAP_PLUGIN_FACTORY_ID));
	const clap_plugin_descriptor_t* desc = plugins->get_plugin_descriptor(plugins, index);
	if(!desc) return false;
	for(const AuIdentity& a : auIdentities)
		if(!strcmp(desc->id, a.pluginId)) {
			// instruments ("aumu"), from the CLAP features
			info->au_type[0] = 0;
			strcpy(info->au_subt, a.subtype);
			return true;
		}
	return false;
}

// at least one capital letter: all-lowercase manufacturer codes are Apple's
const clap_plugin_factory_as_auv2 auv2Factory = { "Oiko", "oikoaudio", auv2Info };
}

const void* tack_wrapperFactory(const char* id)
{
	if(!strcmp(id, CLAP_PLUGIN_FACTORY_INFO_VST3) || !strcmp(id, CLAP_PLUGIN_FACTORY_INFO_VST3_V1)) return &vst3Factory;
	if(!strcmp(id, CLAP_PLUGIN_FACTORY_INFO_AUV2)) return &auv2Factory;
	return nullptr;
}
