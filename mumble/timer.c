#include "mumble.h"

#include "timer.h"

static lua_timer* mumble_checktimer(lua_State *l, int index)
{
	lua_timer *timer = luaL_checkudata(l, index, METATABLE_TIMER);

	return timer;
}

static void mumble_lua_timer_finish(lua_State *l, lua_timer *ltimer)
{
	if (ltimer->started) {
		// if (ev_is_active(&ltimer->timer)) {
			mumble_log(LOG_TRACE, "mumble.timer: %p stopping\n", ltimer);
			uv_timer_init(uv_default_loop(), &ltimer->timer);
		// }
		// Cleanup all our references
		mumble_log(LOG_TRACE, "mumble.timer: %p cleanup\n", ltimer);
		mumble_registry_unref(l, MUMBLE_TIMER_REG, ltimer->self);
		mumble_registry_unref(l, MUMBLE_TIMER_REG, ltimer->callback);
		ltimer->started = false;
	}
}

static void mumble_lua_timer(uv_timer_t* handle)
{
	lua_timer* timer = (lua_timer*) handle->data;

	lua_State *l = timer->l;
	timer->count++;

	lua_stackguard_entry(l);

	// Push our error handler
	lua_pushcfunction(l, mumble_traceback);

	// Push the callback function from the registry
	mumble_registry_pushref(l, MUMBLE_TIMER_REG, timer->callback);
	// Push ourself to the callback for use
	mumble_registry_pushref(l, MUMBLE_TIMER_REG, timer->self);

	// Call the callback with our custom error handler function
	if (lua_pcall(l, 1, 0, -3) != 0) {
		mumble_log(LOG_ERROR, "%s\n", lua_tostring(l, -1));
		lua_pop(l, 1); // Pop the error
	}

	// Pop the error handler
	lua_pop(l, 1);

	// If the timer isn't active after our callback function call..
	// if (!ev_is_active(w_)) {
		// Mark our timer as finished.
		// This allows the timer object to be garbage collected the moment our callback is done being used.
		mumble_lua_timer_finish(l, timer);
	// }

	lua_stackguard_exit(l);
}

int mumble_timer_new(lua_State *l)
{
	lua_timer *ltimer = lua_newuserdata(l, sizeof(lua_timer));
	ltimer->count = 0;
	ltimer->l = l;
	ltimer->started = false;

	luaL_getmetatable(l, METATABLE_TIMER);
	lua_setmetatable(l, -2);

	// Return the timer metatable
	return 1;
}

static int timer_start(lua_State *l)
{
	lua_timer *ltimer = luaL_checkudata(l, 1, METATABLE_TIMER);
	luaL_checktype(l, 2, LUA_TFUNCTION);

	uint64_t after = (uint64_t) luaL_optnumber(l, 3, 0) * 1000;
	uint64_t repeat = (uint64_t) luaL_optnumber(l, 4, 0) * 1000;

	if (!ltimer->started) {
		ltimer->started = true;

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
		// Timer is already running, so go again
		// ev_timer_again(EV_DEFAULT, &ltimer->timer);
	}
	return 1;
}

static int timer_stop(lua_State *l)
{
	lua_timer *ltimer = luaL_checkudata(l, 1, METATABLE_TIMER);
	mumble_lua_timer_finish(l, ltimer);
	// Return ourself
	lua_settop(l, 1);
	return 1;
}

static int timer_set(lua_State *l)
{
	lua_timer *ltimer = luaL_checkudata(l, 1, METATABLE_TIMER);
	double after = luaL_checknumber(l, 2);
	double repeat = luaL_optnumber(l, 3, 0);

	// ev_timer_set(&ltimer->timer, after, repeat);

	// Return ourself
	lua_settop(l, 1);
	return 1;
}

static int timer_get(lua_State *l)
{
	lua_timer *ltimer = luaL_checkudata(l, 1, METATABLE_TIMER);
	lua_pushnumber(l, ltimer->delay);
	lua_pushnumber(l, ltimer->repeat);
	return 2;
}

static int timer_setDuration(lua_State *l)
{
	lua_timer *ltimer = luaL_checkudata(l, 1, METATABLE_TIMER);

	ltimer->delay = luaL_checknumber(l, 2);

	// Return ourself
	lua_settop(l, 1);
	return 1;
}

static int timer_getDuration(lua_State *l)
{
	lua_timer *ltimer = luaL_checkudata(l, 1, METATABLE_TIMER);
	lua_pushnumber(l, ltimer->delay);
	return 1;
}

static int timer_setRepeat(lua_State *l)
{
	lua_timer *ltimer = luaL_checkudata(l, 1, METATABLE_TIMER);
	
	ltimer->repeat = luaL_checknumber(l, 2);

	// Return ourself
	lua_settop(l, 1);
	return 1;
}

static int timer_getRepeat(lua_State *l)
{
	lua_timer *ltimer = luaL_checkudata(l, 1, METATABLE_TIMER);
	lua_pushnumber(l, ltimer->repeat);
	return 1;
}

static int timer_getCount(lua_State *l)
{
	lua_timer *ltimer = luaL_checkudata(l, 1, METATABLE_TIMER);
	lua_pushinteger(l, ltimer->count);
	return 1;
}

static int timer_again(lua_State *l)
{
	lua_timer *ltimer = luaL_checkudata(l, 1, METATABLE_TIMER);

	// ev_timer_again(EV_DEFAULT, &ltimer->timer);

	// Return ourself
	lua_settop(l, 1);
	return 1;
}

static int timer_isActive(lua_State *l)
{
	lua_timer *ltimer = luaL_checkudata(l, 1, METATABLE_TIMER);
	// lua_pushboolean(l, ev_is_active(&ltimer->timer));
	return 1;
}

static int timer_gc(lua_State *l)
{
	lua_timer *ltimer = luaL_checkudata(l, 1, METATABLE_TIMER);
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