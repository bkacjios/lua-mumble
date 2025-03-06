#include "mumble.h"

#include "timer.h"
#include "util.h"
#include "log.h"

static MumbleTimer* mumble_active_timer(lua_State *l, int arg) {
	MumbleTimer *ltimer = luaL_checkudata(l, 1, METATABLE_TIMER);

	if (ltimer->closed) {
		lua_Debug ar;

		if (!lua_getstack(l, 0, &ar))
			luaL_error(l, "attempt to call method on stopped %s", METATABLE_TIMER);

		lua_getinfo(l, "nS", &ar);
		luaL_error(l, "attempt to call %s '%s' on stopped %s", ar.namewhat, ar.name, METATABLE_TIMER);
	}

	return ltimer;
}

static void mumble_timer_on_close(uv_handle_t* handle) {
	MumbleTimer* ltimer = (MumbleTimer*) handle->data;

	lua_State *l = ltimer->l;

	if (ltimer->callback > LUA_REFNIL) {
		mumble_log(LOG_TRACE, "%s: %p unreference callback: %d", METATABLE_TIMER, ltimer, ltimer->callback);
		mumble_registry_unref(l, MUMBLE_TIMER_REG, &ltimer->callback);
	}
	if (ltimer->self > LUA_REFNIL) {
		mumble_log(LOG_TRACE, "%s: %p unreference timer: %d", METATABLE_TIMER, ltimer, ltimer->self);
		mumble_registry_unref(l, MUMBLE_TIMER_REG, &ltimer->self);
	}
}

static void mumble_timer_close(MumbleTimer* ltimer) {
	if (!ltimer->closed) {
		ltimer->closed = true;
		uv_timer_stop(&ltimer->timer);
		uv_close((uv_handle_t*)&ltimer->timer, mumble_timer_on_close);
		mumble_log(LOG_TRACE, "%s: %p stopped and closed", METATABLE_TIMER, ltimer);
	}
}

static void mumble_lua_timer(uv_timer_t* handle) {
	MumbleTimer* ltimer = (MumbleTimer*) handle->data;

	lua_State *l = ltimer->l;
	ltimer->count++;

	lua_stackguard_entry(l);

	// Push our error handler
	lua_pushcfunction(l, mumble_traceback);

	// Push the callback function from the registry
	mumble_registry_pushref(l, MUMBLE_TIMER_REG, ltimer->callback);
	// Push ourself to the callback for use
	mumble_registry_pushref(l, MUMBLE_TIMER_REG, ltimer->self);

	// Call the callback with our custom error handler function
	if (lua_pcall(l, 1, 0, -3) != 0) {
		mumble_log(LOG_ERROR, "%s", lua_tostring(l, -1));
		lua_pop(l, 1); // Pop the error
	}

	// Pop the error handler
	lua_pop(l, 1);

	if (!uv_is_active((uv_handle_t*) &ltimer->timer) && !ltimer->paused) {
		// Timer stopped on its own and was not paused
		mumble_log(LOG_TRACE, "%s: %p timer inactive and not paused, closing", METATABLE_TIMER, ltimer);
		mumble_timer_close(ltimer);
	}

	lua_stackguard_exit(l);
}

int mumble_timer_new(lua_State *l) {
	MumbleTimer *ltimer = lua_newuserdata(l, sizeof(MumbleTimer));
	ltimer->count = 0;
	ltimer->l = l;
	ltimer->after = 0;
	ltimer->closed = false;
	ltimer->paused = false;

	ltimer->self = LUA_NOREF;
	ltimer->callback = LUA_NOREF;
	luaL_getmetatable(l, METATABLE_TIMER);
	lua_setmetatable(l, -2);

	uv_timer_init(uv_default_loop(), &ltimer->timer);
	ltimer->timer.data = ltimer;

	// Return the timer metatable
	return 1;
}

static int timer_start(lua_State *l) {
	MumbleTimer *ltimer = mumble_active_timer(l, 1);
	luaL_checktype(l, 2, LUA_TFUNCTION);

	uint64_t after = luaL_optnumber(l, 3, 0) * 1000;
	uint64_t repeat = luaL_optnumber(l, 4, 0) * 1000;

	ltimer->after = after;

	mumble_log(LOG_TRACE, "%s: %p start", METATABLE_TIMER, ltimer);

	if (uv_is_active((uv_handle_t*) &ltimer->timer)) {
		// Timer is already running, so stop before going again
		uv_timer_stop(&ltimer->timer);
	}

	if (ltimer->self <= LUA_NOREF) {
		// Self will be referenced until closed
		lua_pushvalue(l, 1); // Push a copy of the userdata to prevent garabage collection
		ltimer->self = mumble_registry_ref(l, MUMBLE_TIMER_REG); // Pop it off as a reference
		mumble_log(LOG_TRACE, "%s: %p register timer: %d", METATABLE_TIMER, ltimer, ltimer->self);
	}

	if (ltimer->callback > LUA_REFNIL) {
		// We had a previously referenced callback, so unrefrence it
		mumble_log(LOG_TRACE, "%s: %p unreference callback: %d", METATABLE_TIMER, ltimer, ltimer->callback);
		mumble_registry_unref(l, MUMBLE_TIMER_REG, &ltimer->callback);
	}

	// Create a new callback referenced
	lua_pushvalue(l, 2); // Push a copy of our callback function
	ltimer->callback = mumble_registry_ref(l, MUMBLE_TIMER_REG); // Pop it off as a reference
	mumble_log(LOG_TRACE, "%s: %p register callback: %d", METATABLE_TIMER, ltimer, ltimer->callback);

	// Start our timer
	uv_timer_start(&ltimer->timer, mumble_lua_timer, after, repeat);
	ltimer->count = 0;
	ltimer->paused = false;

	// Return ourself
	lua_pushvalue(l, 1);
	return 1;
}

static int timer_stop(lua_State *l) {
	MumbleTimer *ltimer = luaL_checkudata(l, 1, METATABLE_TIMER);
	mumble_timer_close(ltimer);
	return 0;
}

static int timer_pause(lua_State *l) {
	MumbleTimer *ltimer = luaL_checkudata(l, 1, METATABLE_TIMER);

	if (uv_timer_get_repeat(&ltimer->timer) <= 0) {
		return luaL_error(l, "attempt to call 'pause' on non-repeating %s", METATABLE_TIMER);
	} else if (uv_timer_again(&ltimer->timer) == UV_EINVAL) {
		return luaL_error(l, "attempt to call 'pause' on unstarted %s", METATABLE_TIMER);
	}

	// Pause the timer
	uv_timer_stop(&ltimer->timer);
	ltimer->paused = true;

	// Return ourself
	lua_pushvalue(l, 1);
	return 1;
}

static int timer_again(lua_State *l) {
	MumbleTimer *ltimer = mumble_active_timer(l, 1);

	if (uv_timer_get_repeat(&ltimer->timer) <= 0) {
		return luaL_error(l, "attempt to call 'again' on non-repeating %s", METATABLE_TIMER);
	} else if (uv_timer_again(&ltimer->timer) == UV_EINVAL) {
		return luaL_error(l, "attempt to call 'again' on unstarted %s", METATABLE_TIMER);
	}

	// Reset and unpause
	ltimer->count = 0;
	ltimer->paused = false;

	// Return ourself
	lua_pushvalue(l, 1);
	return 1;
}

static int timer_resume(lua_State *l) {
	MumbleTimer *ltimer = mumble_active_timer(l, 1);

	if (uv_timer_get_repeat(&ltimer->timer) <= 0) {
		return luaL_error(l, "attempt to call 'resume' on non-repeating %s", METATABLE_TIMER);
	} else if (uv_timer_again(&ltimer->timer) == UV_EINVAL) {
		return luaL_error(l, "attempt to call 'resume' on unstarted %s", METATABLE_TIMER);
	}

	ltimer->paused = false;

	// Return ourself
	lua_pushvalue(l, 1);
	return 1;
}

static int timer_set(lua_State *l) {
	MumbleTimer *ltimer = luaL_checkudata(l, 1, METATABLE_TIMER);
	double after = luaL_checknumber(l, 2);
	double repeat = luaL_optnumber(l, 3, 0);

	ltimer->after = after;
	uv_timer_set_repeat(&ltimer->timer, repeat * 1000);

	// Return ourself
	lua_pushvalue(l, 1);
	return 1;
}

static int timer_get(lua_State *l) {
	MumbleTimer *ltimer = luaL_checkudata(l, 1, METATABLE_TIMER);
	lua_pushnumber(l, ltimer->after);
	lua_pushnumber(l, (double) uv_timer_get_repeat(&ltimer->timer) / 1000);
	return 2;
}

static int timer_setDuration(lua_State *l) {
	MumbleTimer *ltimer = luaL_checkudata(l, 1, METATABLE_TIMER);

	ltimer->after = luaL_checknumber(l, 2) * 1000;

	// Return ourself
	lua_pushvalue(l, 1);
	return 1;
}

static int timer_getDuration(lua_State *l) {
	MumbleTimer *ltimer = luaL_checkudata(l, 1, METATABLE_TIMER);
	lua_pushnumber(l, (double) ltimer->after / 1000);
	return 1;
}

static int timer_setRepeat(lua_State *l) {
	MumbleTimer *ltimer = luaL_checkudata(l, 1, METATABLE_TIMER);

	uv_timer_set_repeat(&ltimer->timer, luaL_checknumber(l, 2) * 1000);

	// Return ourself
	lua_pushvalue(l, 1);
	return 1;
}

static int timer_getRepeat(lua_State *l) {
	MumbleTimer *ltimer = luaL_checkudata(l, 1, METATABLE_TIMER);
	lua_pushnumber(l, (double) uv_timer_get_repeat(&ltimer->timer) / 1000);
	return 1;
}

static int timer_getCount(lua_State *l) {
	MumbleTimer *ltimer = luaL_checkudata(l, 1, METATABLE_TIMER);
	lua_pushinteger(l, ltimer->count);
	return 1;
}

static int timer_getRemain(lua_State *l) {
	MumbleTimer *ltimer = luaL_checkudata(l, 1, METATABLE_TIMER);
	lua_pushinteger(l, (double) uv_timer_get_due_in(&ltimer->timer) / 1000);
	return 1;
}

static int timer_isActive(lua_State *l) {
	MumbleTimer *ltimer = luaL_checkudata(l, 1, METATABLE_TIMER);
	lua_pushboolean(l, uv_is_active((uv_handle_t*) &ltimer->timer));
	return 1;
}

static int timer_isPaused(lua_State *l) {
	MumbleTimer *ltimer = luaL_checkudata(l, 1, METATABLE_TIMER);
	lua_pushboolean(l, ltimer->paused);
	return 1;
}

static int timer_gc(lua_State *l) {
	MumbleTimer *ltimer = luaL_checkudata(l, 1, METATABLE_TIMER);
	mumble_timer_close(ltimer);
	mumble_log(LOG_DEBUG, "%s: %p garbage collected", METATABLE_TIMER, ltimer);
	return 0;
}

static int timer_tostring(lua_State *l) {
	lua_pushfstring(l, "%s: %p", METATABLE_TIMER, lua_topointer(l, 1));
	return 1;
}

const luaL_Reg mumble_timer[] = {
	{"start", timer_start},
	{"stop", timer_stop},
	{"pause", timer_pause},
	{"again", timer_again},
	{"resume", timer_resume},
	{"set", timer_set},
	{"get", timer_get},
	{"setDuration", timer_setDuration},
	{"getDuration", timer_getDuration},
	{"setRepeat", timer_setRepeat},
	{"getRepeat", timer_getRepeat},
	{"getCount", timer_getCount},
	{"getRemain", timer_getRemain},
	{"isActive", timer_isActive},
	{"isPaused", timer_isPaused},
	{"__tostring", timer_tostring},
	{"__gc", timer_gc},
	{NULL, NULL}
};