#pragma once

#include "lua/luainc.h"
#include "proto/Mumble.pb-c.h"

#include <cstdint>
#include <string>
#include <vector>

// A Lua "mumble.voicetarget", which is sent as part of a MumbleProto.VoiceTarget message
// A Lua "mumble.voicetarget", which is sent as part of a MumbleProto.VoiceTarget message.
// The message points into storage we own, so it lives as long as the target does.
class VoiceTarget {
public:
	VoiceTarget() {
		mumble_proto__voice_target__target__init(&target);
	}

	void addSession(uint32_t session) {
		sessions.push_back(session);
		target.n_session = sessions.size();
		target.session = sessions.data();
	}

	void setChannel(uint32_t channel_id) {
		target.has_channel_id = true;
		target.channel_id = channel_id;
	}

	void setLinks(bool links) {
		target.has_links = true;
		target.links = links;
	}

	void setChildren(bool children) {
		target.has_children = true;
		target.children = children;
	}

	void setGroup(const char *value) {
		group = value;
		target.group = &group[0];
	}

	MumbleProto__VoiceTarget__Target target;

private:
	std::vector<uint32_t> sessions;
	std::string group;
};
