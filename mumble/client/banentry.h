#pragma once

#include "lua/luainc.h"
#include "proto/Mumble.pb-c.h"

#include <cstddef>
#include <cstdint>
#include <string>

// A Lua "mumble.banentry", which mirrors a MumbleProto.BanList.BanEntry message
// A Lua "mumble.banentry", which mirrors a MumbleProto.BanList.BanEntry message.
// The message points into strings we own, so they live as long as the entry does.
class BanEntry {
public:
	BanEntry() {
		mumble_proto__ban_list__ban_entry__init(&entry);
	}

	void setAddress(const char *data, size_t length) {
		// Addresses are read as 16 bytes, so never hold less than that
		address.assign(data, length);
		address.resize(length < 16 ? 16 : length, '\0');
		entry.address.data = (uint8_t*) &address[0];
		entry.address.len = length;
	}

	void setMask(uint32_t value) { entry.mask = value; }
	void setDuration(uint32_t value) {
		entry.has_duration = true;
		entry.duration = value;
	}
	void setName(const char *value) { entry.name = assign(name, value); }
	void setHash(const char *value) { entry.hash = assign(hash, value); }
	void setReason(const char *value) { entry.reason = assign(reason, value); }
	void setStart(const char *value) { entry.start = assign(start, value); }

	MumbleProto__BanList__BanEntry entry;

private:
	static char* assign(std::string &storage, const char *value) {
		storage = value;
		return &storage[0];
	}

	std::string address;
	std::string name;
	std::string hash;
	std::string reason;
	std::string start;
};
