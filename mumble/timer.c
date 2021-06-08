#include "mumble.h"

#include "timer.h"

static void mumble_lua_timer(EV_P_ ev_timer *w_, int revents)
{
	struct lua_timer *w = (struct lua_timer *) w_;

	lua_State *l = w->l;

	lua_stackguard_entry(l);

	// Push our error handler
	lua_pushcfunction(l, mumble_traceback);

	// Push the callback function from the registry
	lua_rawgeti(l, LUA_REGISTRYINDEX, w->callback);
	lua_rawgeti(l, LUA_REGISTRYINDEX, w->self);

	// Call the callback with our custom error handler function
	if (lua_pcall(l, 1, 0, -3) != 0) {
		fprintf(stderr, "%s\n", lua_tostring(l, -1));
		lua_pop(l, 1); // Pop the error
	}

	// Pop the error handler
	lua_pop(l, 1);

	lua_stackguard_exit(l);
}

int mumble_timer_new(lua_State *l)
{
	lua_timer *ltimer = lua_newuserdata(l, sizeof(lua_timer));
	ltimer->l = l;
	ltimer->callback = 0;
	ltimer->self = 0;

	luaL_getmetatable(l, METATABLE_TIMER);
	lua_setmetatable(l, -2);
	// Return the timer userdata
	return 1;
}

static int timer_start(lua_State *l)
{
	lua_timer *ltimer = luaL_checkudata(l, 1, METATABLE_TIMER);

	luaL_checktype(l, 2, LUA_TFUNCTION);
	double after = luaL_checknumber(l, 3);
	double repeat = luaL_optnumber(l, 4, 0);

	lua_pushvalue(l, 1); // Push a copy of our self
	ltimer->self = luaL_ref(l, LUA_REGISTRYINDEX); // Pop it off as a reference

	lua_pushvalue(l, 2); // Push a copy of our callback function
	ltimer->callback = luaL_ref(l, LUA_REGISTRYINDEX); // Pop it off as a reference

	// Initialize and start the timer
	ev_timer_init(&ltimer->timer, mumble_lua_timer, after, repeat);
	ev_timer_start(EV_DEFAULT, &ltimer->timer);
	return 0;
}

static int timer_set(lua_State *l)
{
	lua_timer *ltimer = luaL_checkudata(l, 1, METATABLE_TIMER);
	double after = luaL_checknumber(l, 3);
	double repeat = luaL_optnumber(l, 4, 0);
	ev_timer_set(&ltimer->timer, after, repeat);
	return 0;
}

static int timer_again(lua_State *l)
{
	lua_timer *ltimer = luaL_checkudata(l, 1, METATABLE_TIMER);
	ev_timer_again(EV_DEFAULT, &ltimer->timer);
	return 0;
}

static int timer_stop(lua_State *l)
{
	lua_timer *ltimer = luaL_checkudata(l, 1, METATABLE_TIMER);
	ev_timer_stop(EV_DEFAULT, &ltimer->timer);
	return 0;
}

static int timer_tostring(lua_State *l)
{
	lua_timer *ltimer = luaL_checkudata(l, 1, METATABLE_TIMER);
	lua_pushfstring(l, "%s: %p", METATABLE_TIMER, ltimer);
	return 1;
}

static int timer_gc(lua_State *l)
{
	lua_timer *ltimer = luaL_checkudata(l, 1, METATABLE_TIMER);
	ev_timer_stop(EV_DEFAULT, &ltimer->timer);

	// Clear our references from the registry

	if (ltimer->self > 0) {
		luaL_unref(l, LUA_REGISTRYINDEX, ltimer->self);
	}

	if (ltimer->callback > 0) {
		luaL_unref(l, LUA_REGISTRYINDEX, ltimer->callback);
	}
	return 0;
}

const luaL_Reg mumble_timer[] = {
	{"start", timer_start},
	{"set", timer_set},
	{"again", timer_again},
	{"stop", timer_stop},
	{"__tostring", timer_tostring},
	{"__gc", timer_gc},
	{NULL, NULL}
};