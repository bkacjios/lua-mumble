#pragma once

#include "lua/luainc.h"

#include <cstdint>
#include <uv.h>

class MumbleTimer {
public:
	explicit MumbleTimer(lua_State *l);

	void close();

	// Call the function at callback_index (a Lua stack index) after `after` ms, then every `repeat` ms.
	// The timer at self_index is kept referenced until it's closed.
	void start(int self_index, int callback_index, uint64_t after, uint64_t repeat);
	// Restart the countdown of a repeating timer. Returns why it can't ("non-repeating" or "unstarted"), or NULL.
	const char* again();
	// Like again(), but also stop it until resumed
	const char* pause();
	// Like again(), but also unpause it
	const char* resume();
	// Like resume(), but also reset our tick count
	const char* restart();

	// Durations are in ms
	void setRepeat(uint64_t repeat);
	uint64_t getRepeat() const;
	uint64_t getRemain() const;
	bool isActive() const;

	static void onTick(uv_timer_t* handle);
	static void onClose(uv_handle_t* handle);

	uv_timer_t timer;
	lua_State* l;
	bool closed = false;
	bool paused = false;
	uint32_t count = 0;
	int self = LUA_NOREF;
	int callback = LUA_NOREF;
	uint64_t after = 0;
};
