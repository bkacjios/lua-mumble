#include "util/timer.h"

#include "lua/luainc.h"
#include "lua/metatables.h"
#include "lua/registry.h"
#include "module.h"
#include "util/log.h"

MumbleTimer::MumbleTimer(lua_State *l) : l(l) {
	uv_timer_init(uv_default_loop(), &timer);
	timer.data = this;
}

void MumbleTimer::onClose(uv_handle_t* handle) {
	MumbleTimer* ltimer = (MumbleTimer*) handle->data;

	lua_State *l = ltimer->l;

	if (ltimer->callback > LUA_REFNIL) {
		mumble_log(LOG_TRACE, "%s: %p unreference callback: %d", METATABLE_TIMER, ltimer, ltimer->callback);
		LuaRegistry::unrefIn(l, LuaRegistry::timers, &ltimer->callback);
	}
	if (ltimer->self > LUA_REFNIL) {
		mumble_log(LOG_TRACE, "%s: %p unreference timer: %d", METATABLE_TIMER, ltimer, ltimer->self);
		LuaRegistry::unrefIn(l, LuaRegistry::timers, &ltimer->self);
	}
}

void MumbleTimer::close() {
	if (!closed) {
		closed = true;
		uv_timer_stop(&timer);
		if (!uv_is_closing((uv_handle_t*)&timer)) {
			uv_close((uv_handle_t*)&timer, MumbleTimer::onClose);
		}
		mumble_log(LOG_TRACE, "%s: %p stopped and closed", METATABLE_TIMER, this);
	}
}

void MumbleTimer::onTick(uv_timer_t* handle) {
	MumbleTimer* ltimer = (MumbleTimer*) handle->data;

	if (uv_is_closing((uv_handle_t*)handle)) return;

	lua_State *l = ltimer->l;
	ltimer->count++;

	lua_stackguard_entry(l);

	// Push our error handler
	lua_pushcfunction(l, mumble_traceback);

	// Push the callback function from the registry
	LuaRegistry::pushFrom(l, LuaRegistry::timers, ltimer->callback);
	// Push ourself to the callback for use
	LuaRegistry::pushFrom(l, LuaRegistry::timers, ltimer->self);

	// Call the callback with our custom error handler function
	if (lua_pcall(l, 1, 0, -3) != 0) {
		mumble_log(LOG_ERROR, "%s: %s", METATABLE_TIMER, lua_tostring(l, -1));
		lua_pop(l, 1); // Pop the error
	}

	// Pop the error handler
	lua_pop(l, 1);

	if (!uv_is_active((uv_handle_t*)handle) && !ltimer->paused) {
		// Timer stopped on its own and was not paused
		mumble_log(LOG_TRACE, "%s: %p timer inactive and not paused, closing", METATABLE_TIMER, ltimer);
		ltimer->close();
	}

	lua_stackguard_exit(l);
}

void MumbleTimer::start(int self_index, int callback_index, uint64_t after, uint64_t repeat) {
	this->after = after;

	mumble_log(LOG_TRACE, "%s: %p start", METATABLE_TIMER, this);

	if (uv_is_active((uv_handle_t*) &timer)) {
		// Timer is already running, so stop before going again
		uv_timer_stop(&timer);
	}

	if (self <= LUA_NOREF) {
		// Self will be referenced until closed
		lua_pushvalue(l, self_index);
		self = LuaRegistry::refIn(l, LuaRegistry::timers);
		mumble_log(LOG_TRACE, "%s: %p register timer: %d", METATABLE_TIMER, this, self);
	}

	if (callback > LUA_REFNIL) {
		// We had a previously referenced callback, so unrefrence it
		mumble_log(LOG_TRACE, "%s: %p unreference callback: %d", METATABLE_TIMER, this, callback);
		LuaRegistry::unrefIn(l, LuaRegistry::timers, &callback);
	}

	lua_pushvalue(l, callback_index);
	callback = LuaRegistry::refIn(l, LuaRegistry::timers);
	mumble_log(LOG_TRACE, "%s: %p register callback: %d", METATABLE_TIMER, this, callback);

	uv_timer_start(&timer, MumbleTimer::onTick, after, repeat);
	count = 0;
	paused = false;
}

const char* MumbleTimer::again() {
	if (uv_timer_get_repeat(&timer) <= 0) {
		return "non-repeating";
	} else if (uv_timer_again(&timer) == UV_EINVAL) {
		return "unstarted";
	}
	return NULL;
}

const char* MumbleTimer::pause() {
	const char *problem = again();
	if (problem) return problem;
	uv_timer_stop(&timer);
	paused = true;
	return NULL;
}

const char* MumbleTimer::resume() {
	const char *problem = again();
	if (problem) return problem;
	paused = false;
	return NULL;
}

const char* MumbleTimer::restart() {
	const char *problem = resume();
	if (problem) return problem;
	count = 0;
	return NULL;
}

void MumbleTimer::setRepeat(uint64_t repeat) {
	uv_timer_set_repeat(&timer, repeat);
}

uint64_t MumbleTimer::getRepeat() const {
	return uv_timer_get_repeat(&timer);
}

uint64_t MumbleTimer::getRemain() const {
	return uv_timer_get_due_in(&timer);
}

bool MumbleTimer::isActive() const {
	return uv_is_active((const uv_handle_t*) &timer);
}
