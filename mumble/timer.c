#include "mumble.h"

#include "timer.h"
#include "util.h"
#include "log.h"

static void mumble_lua_timer_cleanup(lua_State *l, MumbleTimer *ltimer) {
	// Cleanup all our references
	if (ltimer->self > LUA_REFNIL) {
		mumble_registry_unref(l, MUMBLE_TIMER_REG, &ltimer->self);
	}
	if (ltimer->callback > LUA_REFNIL) {
		mumble_registry_unref(l, MUMBLE_TIMER_REG, &ltimer->callback);
	}
	ltimer->count = 0;
}

static void mumble_lua_timer_stop(lua_State *l, MumbleTimer *ltimer) {
	if (uv_is_active((uv_handle_t*) &ltimer->timer)) {
		uv_timer_stop(&ltimer->timer);
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

	if (!uv_is_active((uv_handle_t*) handle)) {
		// Timer is no longer active
		// Cleanup timer references to allow for garbage collection
		mumble_lua_timer_cleanup(l, ltimer);
	}

	lua_stackguard_exit(l);
}

int mumble_timer_new(lua_State *l) {
	MumbleTimer *ltimer = lua_newuserdata(l, sizeof(MumbleTimer));
	ltimer->count = 0;
	ltimer->l = l;
	ltimer->after = 0;
	ltimer->self = LUA_NOREF;
	ltimer->callback = LUA_NOREF;
	ltimer->timer.data = ltimer;
	luaL_getmetatable(l, METATABLE_TIMER);
	lua_setmetatable(l, -2);

	uv_timer_init(uv_default_loop(), &ltimer->timer);

	// Return the timer metatable
	return 1;
}

static int timer_start(lua_State *l) {
	MumbleTimer *ltimer = luaL_checkudata(l, 1, METATABLE_TIMER);
	luaL_checktype(l, 2, LUA_TFUNCTION);

	uint64_t after = (uint64_t) luaL_optnumber(l, 3, 0) * 1000;
	uint64_t repeat = (uint64_t) luaL_optnumber(l, 4, 0) * 1000;

	ltimer->after = after;

	if (uv_is_active((uv_handle_t*) &ltimer->timer)) {
		// Timer is already running, so stop before going again
		uv_timer_stop(&ltimer->timer);
	}

	if (ltimer->self <= LUA_NOREF) {
		// Self will be referenced until stopped
		lua_pushvalue(l, 1); // Push a copy of the userdata to prevent garabage collection
		ltimer->self = mumble_registry_ref(l, MUMBLE_TIMER_REG); // Pop it off as a reference
	}

	if (ltimer->callback > LUA_REFNIL) {
		// We had a previously referenced callback, so unrefrence it
		mumble_registry_unref(l, MUMBLE_TIMER_REG, &ltimer->callback);
	}

	// Create a new callback referenced
	lua_pushvalue(l, 2); // Push a copy of our callback function
	ltimer->callback = mumble_registry_ref(l, MUMBLE_TIMER_REG); // Pop it off as a reference

	// Start our timer
	uv_timer_start(&ltimer->timer, mumble_lua_timer, after, repeat);

	// Return ourself
	lua_settop(l, 1);
	return 1;
}

static int timer_stop(lua_State *l) {
	MumbleTimer *ltimer = luaL_checkudata(l, 1, METATABLE_TIMER);
	mumble_lua_timer_stop(l, ltimer);
	mumble_lua_timer_cleanup(l, ltimer);
	// Return ourself
	lua_settop(l, 1);
	return 1;
}

static int timer_set(lua_State *l) {
	MumbleTimer *ltimer = luaL_checkudata(l, 1, METATABLE_TIMER);
	double after = luaL_checknumber(l, 2);
	double repeat = luaL_optnumber(l, 3, 0);

	ltimer->after = after;
	uv_timer_set_repeat(&ltimer->timer, repeat * 1000);

	// Return ourself
	lua_settop(l, 1);
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

	ltimer->after = (uint64_t) luaL_checknumber(l, 2) * 1000;

	// Return ourself
	lua_settop(l, 1);
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
	lua_settop(l, 1);
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

static int timer_again(lua_State *l) {
	MumbleTimer *ltimer = luaL_checkudata(l, 1, METATABLE_TIMER);

	if (uv_timer_get_repeat(&ltimer->timer) <= 0) {
		return luaL_error(l, "attempt to call 'again' on non-repeating timer");
	} else if (uv_timer_again(&ltimer->timer) == UV_EINVAL) {
		return luaL_error(l, "attempt to call 'again' on unstarted timer");
	}

	// Return ourself
	lua_settop(l, 1);
	return 1;
}

static int timer_isActive(lua_State *l) {
	MumbleTimer *ltimer = luaL_checkudata(l, 1, METATABLE_TIMER);
	lua_pushboolean(l, uv_is_active((uv_handle_t*) &ltimer->timer));
	return 1;
}

static int timer_gc(lua_State *l) {
	MumbleTimer *ltimer = luaL_checkudata(l, 1, METATABLE_TIMER);
	mumble_lua_timer_stop(l, ltimer);
	mumble_lua_timer_cleanup(l, ltimer);
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
	{"set", timer_set},
	{"get", timer_get},
	{"setDuration", timer_setDuration},
	{"getDuration", timer_getDuration},
	{"setRepeat", timer_setRepeat},
	{"getRepeat", timer_getRepeat},
	{"getCount", timer_getCount},
	{"getRemain", timer_getRemain},
	{"again", timer_again},
	{"isActive", timer_isActive},
	{"__tostring", timer_tostring},
	{"__gc", timer_gc},
	{NULL, NULL}
};