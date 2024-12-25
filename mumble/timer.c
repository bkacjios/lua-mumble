#include "mumble.h"

#include "timer.h"

static MumbleTimer* mumble_checktimer(lua_State *l, int index)
{
	MumbleTimer *timer = luaL_checkudata(l, index, METATABLE_TIMER);

	return timer;
}

static void mumble_lua_timer_finish(lua_State *l, MumbleTimer *ltimer)
{
	if (ltimer->running) {
		if (uv_is_active((uv_handle_t*) &ltimer->timer)) {
			mumble_log(LOG_TRACE, "mumble.timer: %p stopping\n", ltimer);
			uv_timer_stop(&ltimer->timer);
		}
		// Cleanup all our references
		mumble_log(LOG_TRACE, "mumble.timer: %p cleanup\n", ltimer);
		mumble_registry_unref(l, MUMBLE_TIMER_REG, ltimer->self);
		mumble_registry_unref(l, MUMBLE_TIMER_REG, ltimer->callback);
		ltimer->running = false;
	}
}

static void mumble_lua_timer(uv_timer_t* handle)
{
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
		mumble_log(LOG_ERROR, "%s\n", lua_tostring(l, -1));
		lua_pop(l, 1); // Pop the error
	}

	// Pop the error handler
	lua_pop(l, 1);

	if (!uv_is_active((uv_handle_t*) handle)) {
		// Timer is no longer active
		// Finish and cleanup timer references to allow for garbage collection
		mumble_lua_timer_finish(l, ltimer);
	} else {
		// Timer is still active, stop and go again incase we adjusted the values
		uv_timer_stop(&ltimer->timer);
		uv_timer_start(&ltimer->timer, mumble_lua_timer, ltimer->after, ltimer->repeat);
	}

	lua_stackguard_exit(l);
}

int mumble_timer_new(lua_State *l)
{
	MumbleTimer *ltimer = lua_newuserdata(l, sizeof(MumbleTimer));
	ltimer->count = 0;
	ltimer->l = l;
	ltimer->running = false;
	ltimer->after = 0;
	ltimer->repeat = 0;
	ltimer->self = -1;
	ltimer->callback = -1;
	ltimer->timer.data = ltimer;

	luaL_getmetatable(l, METATABLE_TIMER);
	lua_setmetatable(l, -2);

	// Return the timer metatable
	return 1;
}

static int timer_start(lua_State *l)
{
	MumbleTimer *ltimer = luaL_checkudata(l, 1, METATABLE_TIMER);
	luaL_checktype(l, 2, LUA_TFUNCTION);

	uint64_t after = (uint64_t) luaL_optnumber(l, 3, 0) * 1000;
	uint64_t repeat = (uint64_t) luaL_optnumber(l, 4, 0) * 1000;

	ltimer->after = after;
	ltimer->repeat = repeat;

	if (!ltimer->running) {
		ltimer->running = true;

		lua_pushvalue(l, 1); // Push a copy of the userdata to prevent garabage collection
		ltimer->self = mumble_registry_ref(l, MUMBLE_TIMER_REG); // Pop it off as a reference

		lua_pushvalue(l, 2); // Push a copy of our callback function
		ltimer->callback = mumble_registry_ref(l, MUMBLE_TIMER_REG); // Pop it off as a reference

		// Initialize our timer object
		uv_timer_init(uv_default_loop(), &ltimer->timer);
		uv_timer_start(&ltimer->timer, mumble_lua_timer, after, repeat); // Fire every 1 second

		// Return ourself
		lua_settop(l, 1);
	} else {
		// Timer is already running, so stop and go again
		uv_timer_stop(&ltimer->timer);
		uv_timer_start(&ltimer->timer, mumble_lua_timer, ltimer->after, ltimer->repeat);
	}
	return 1;
}

static int timer_stop(lua_State *l)
{
	MumbleTimer *ltimer = luaL_checkudata(l, 1, METATABLE_TIMER);
	mumble_lua_timer_finish(l, ltimer);
	// Return ourself
	lua_settop(l, 1);
	return 1;
}

static int timer_set(lua_State *l)
{
	MumbleTimer *ltimer = luaL_checkudata(l, 1, METATABLE_TIMER);
	double after = luaL_checknumber(l, 2);
	double repeat = luaL_optnumber(l, 3, 0);

	ltimer->after = after;
	ltimer->repeat = repeat;

	// Return ourself
	lua_settop(l, 1);
	return 1;
}

static int timer_get(lua_State *l)
{
	MumbleTimer *ltimer = luaL_checkudata(l, 1, METATABLE_TIMER);
	lua_pushnumber(l, ltimer->after);
	lua_pushnumber(l, ltimer->repeat);
	return 2;
}

static int timer_setDuration(lua_State *l)
{
	MumbleTimer *ltimer = luaL_checkudata(l, 1, METATABLE_TIMER);

	ltimer->after = luaL_checknumber(l, 2);

	// Return ourself
	lua_settop(l, 1);
	return 1;
}

static int timer_getDuration(lua_State *l)
{
	MumbleTimer *ltimer = luaL_checkudata(l, 1, METATABLE_TIMER);
	lua_pushnumber(l, ltimer->after);
	return 1;
}

static int timer_setRepeat(lua_State *l)
{
	MumbleTimer *ltimer = luaL_checkudata(l, 1, METATABLE_TIMER);
	
	ltimer->repeat = luaL_checknumber(l, 2);

	// Return ourself
	lua_settop(l, 1);
	return 1;
}

static int timer_getRepeat(lua_State *l)
{
	MumbleTimer *ltimer = luaL_checkudata(l, 1, METATABLE_TIMER);
	lua_pushnumber(l, ltimer->repeat);
	return 1;
}

static int timer_getCount(lua_State *l)
{
	MumbleTimer *ltimer = luaL_checkudata(l, 1, METATABLE_TIMER);
	lua_pushinteger(l, ltimer->count);
	return 1;
}

static int timer_again(lua_State *l)
{
	MumbleTimer *ltimer = luaL_checkudata(l, 1, METATABLE_TIMER);

	uv_timer_stop(&ltimer->timer);
	uv_timer_start(&ltimer->timer, mumble_lua_timer, ltimer->after, ltimer->repeat);

	// Return ourself
	lua_settop(l, 1);
	return 1;
}

static int timer_isActive(lua_State *l)
{
	MumbleTimer *ltimer = luaL_checkudata(l, 1, METATABLE_TIMER);
	lua_pushboolean(l, uv_is_active((uv_handle_t*) &ltimer->timer));
	return 1;
}

static int timer_gc(lua_State *l)
{
	MumbleTimer *ltimer = luaL_checkudata(l, 1, METATABLE_TIMER);
	mumble_lua_timer_finish(l, ltimer);
	mumble_log(LOG_DEBUG, "%s: %p garbage collected\n", METATABLE_TIMER, ltimer);
	return 0;
}

static int timer_tostring(lua_State *l)
{
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
	{"again", timer_again},
	{"isActive", timer_isActive},
	{"__tostring", timer_tostring},
	{"__gc", timer_gc},
	{NULL, NULL}
};