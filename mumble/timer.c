#include "mumble.h"

#include "timer.h"

static void mumble_lua_timer_close(lua_State *l, lua_timer *ltimer)
{
	if (!ltimer->closed) {
		// Cleanup all our references
		mumble_registry_unref(l, MUMBLE_TIMER_REG, ltimer->self);
		mumble_registry_unref(l, MUMBLE_TIMER_REG, ltimer->callback);
		ltimer->closed = true;
	}
}

static void mumble_lua_timer(EV_P_ ev_timer *w_, int revents)
{
	struct lua_timer *w = (struct lua_timer *) w_;

	lua_State *l = w->l;

	lua_stackguard_entry(l);

	// Push our error handler
	lua_pushcfunction(l, mumble_traceback);

	// Push the callback function from the registry
	mumble_registry_pushref(l, MUMBLE_TIMER_REG, w->callback);
	mumble_registry_pushref(l, MUMBLE_TIMER_REG, w->self);

	// Call the callback with our custom error handler function
	if (lua_pcall(l, 1, 0, -3) != 0) {
		fprintf(stderr, "%s\n", lua_tostring(l, -1));
		lua_pop(l, 1); // Pop the error
	}

	// Pop the error handler
	lua_pop(l, 1);

	// If the timer isn't active after our callback function call..
	if (!ev_is_active(w_)) {
		// Mark our timer as closed.
		// This allows the timer object to be garbage collected, in most cases, when improperly used..
		mumble_lua_timer_close(l, w);
	}

	lua_stackguard_exit(l);
}

int mumble_timer_new(lua_State *l)
{
	luaL_checktype(l, 2, LUA_TFUNCTION);

	double after = luaL_optnumber(l, 3, 0);
	double repeat = luaL_optnumber(l, 4, 0);

	lua_timer *ltimer = lua_newuserdata(l, sizeof(lua_timer));
	ltimer->l = l;
	ltimer->closed = false;

	lua_pushvalue(l, -1); // Push a copy of the timers userdata
	ltimer->self = mumble_registry_ref(l, MUMBLE_TIMER_REG); // Pop it off as a reference

	lua_pushvalue(l, 2); // Push a copy of our callback function
	ltimer->callback = mumble_registry_ref(l, MUMBLE_TIMER_REG); // Pop it off as a reference

	luaL_getmetatable(l, METATABLE_TIMER);
	lua_setmetatable(l, -2);

	// Initialize our timer object
	ev_timer_init(&ltimer->timer, mumble_lua_timer, after, repeat);

	// Return the timer metatable
	return 1;
}

static int timer_start(lua_State *l)
{
	lua_timer *ltimer = luaL_checkudata(l, 1, METATABLE_TIMER);

	if (ltimer->closed) return luaL_error(l, "attempt to call 'start' on closed timer");

	ev_timer_start(EV_DEFAULT, &ltimer->timer);

	// Return ourself
	lua_settop(l, 1);
	return 1;
}

static int timer_stop(lua_State *l)
{
	lua_timer *ltimer = luaL_checkudata(l, 1, METATABLE_TIMER);

	if (ltimer->closed) return luaL_error(l, "attempt to call 'stop' on closed timer");

	ev_timer_stop(EV_DEFAULT, &ltimer->timer);

	// Return ourself
	lua_settop(l, 1);
	return 1;
}

static int timer_close(lua_State *l)
{
	lua_timer *ltimer = luaL_checkudata(l, 1, METATABLE_TIMER);

	if (ltimer->closed) return luaL_error(l, "attempt to call 'close' on closed timer");

	ev_timer_stop(EV_DEFAULT, &ltimer->timer);
	mumble_lua_timer_close(l, ltimer);
	return 0;
}

static int timer_set(lua_State *l)
{
	lua_timer *ltimer = luaL_checkudata(l, 1, METATABLE_TIMER);

	if (ltimer->closed) return luaL_error(l, "attempt to call 'set' on closed timer");

	double after = luaL_checknumber(l, 2);
	double repeat = luaL_optnumber(l, 3, 0);

	ev_timer_set(&ltimer->timer, after, repeat);

	// Return ourself
	lua_settop(l, 1);
	return 1;
}

static int timer_get(lua_State *l)
{
	lua_timer *ltimer = luaL_checkudata(l, 1, METATABLE_TIMER);
	lua_pushnumber(l, ltimer->timer.at);
	lua_pushnumber(l, ltimer->timer.repeat);
	return 2;
}

static int timer_setDuration(lua_State *l)
{
	lua_timer *ltimer = luaL_checkudata(l, 1, METATABLE_TIMER);
	ltimer->timer.at = luaL_checknumber(l, 2);

	// Return ourself
	lua_settop(l, 1);
	return 1;
}

static int timer_getDuration(lua_State *l)
{
	lua_timer *ltimer = luaL_checkudata(l, 1, METATABLE_TIMER);
	lua_pushnumber(l, ltimer->timer.at);
	return 1;
}

static int timer_setRepeat(lua_State *l)
{
	lua_timer *ltimer = luaL_checkudata(l, 1, METATABLE_TIMER);
	ltimer->timer.repeat = luaL_checknumber(l, 2);

	// Return ourself
	lua_settop(l, 1);
	return 1;
}

static int timer_getRepeat(lua_State *l)
{
	lua_timer *ltimer = luaL_checkudata(l, 1, METATABLE_TIMER);
	lua_pushnumber(l, ltimer->timer.repeat);
	return 1;
}

static int timer_again(lua_State *l)
{
	lua_timer *ltimer = luaL_checkudata(l, 1, METATABLE_TIMER);

	if (ltimer->closed) return luaL_error(l, "attempt to call 'again' on closed timer");

	ev_timer_again(EV_DEFAULT, &ltimer->timer);

	// Return ourself
	lua_settop(l, 1);
	return 1;
}

static int timer_isActive(lua_State *l)
{
	lua_timer *ltimer = luaL_checkudata(l, 1, METATABLE_TIMER);
	lua_pushboolean(l, ev_is_active(&ltimer->timer));
	return 1;
}

static int timer_tostring(lua_State *l)
{
	lua_pushfstring(l, "%s: %p", METATABLE_TIMER, lua_topointer(l, 1));
	return 1;
}

const luaL_Reg mumble_timer[] = {
	{"start", timer_start},
	{"stop", timer_stop},
	{"close", timer_close},
	{"set", timer_set},
	{"get", timer_get},
	{"setDuration", timer_setDuration},
	{"getDuration", timer_getDuration},
	{"setRepeat", timer_setRepeat},
	{"getRepeat", timer_getRepeat},
	{"again", timer_again},
	{"isActive", timer_isActive},
	{"__tostring", timer_tostring},
	{NULL, NULL}
};