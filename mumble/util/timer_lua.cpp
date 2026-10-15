#include "util/timer.h"

#include "lua/bindings.h"
#include "lua/luainc.h"
#include "lua/metatables.h"
#include "lua/registry.h"
#include "util/log.h"

static MumbleTimer* mumble_active_timer(lua_State *l, int arg) {
	MumbleTimer *ltimer = luaL_checkclass<MumbleTimer>(l, 1, METATABLE_TIMER);

	if (ltimer->closed) {
		lua_Debug ar;

		if (!lua_getstack(l, 0, &ar))
			luaL_error(l, "attempt to call method on stopped %s", METATABLE_TIMER);

		lua_getinfo(l, "nS", &ar);
		luaL_error(l, "attempt to call %s '%s' on stopped %s", ar.namewhat, ar.name, METATABLE_TIMER);
	}

	return ltimer;
}

int mumble_timer_new(lua_State *l) {
	lua_newclass<MumbleTimer>(l, l);
	luaL_getmetatable(l, METATABLE_TIMER);
	lua_setmetatable(l, -2);

	// Return the timer metatable
	return 1;
}

static int timer_start(lua_State *l) {
	MumbleTimer *ltimer = mumble_active_timer(l, 1);
	luaL_checktype(l, 2, LUA_TFUNCTION);
	uint64_t after = luaL_optnumber(l, 3, 0) * 1000;
	uint64_t repeat = luaL_optnumber(l, 4, 0) * 1000;
	ltimer->start(1, 2, after, repeat);

	// Return ourself
	lua_pushvalue(l, 1);
	return 1;
}

static int timer_stop(lua_State *l) {
	MumbleTimer *ltimer = luaL_checkclass<MumbleTimer>(l, 1, METATABLE_TIMER);
	ltimer->close();
	return 0;
}

static int timer_pause(lua_State *l) {
	MumbleTimer *ltimer = luaL_checkclass<MumbleTimer>(l, 1, METATABLE_TIMER);
	const char *problem = ltimer->pause();
	if (problem) {
		return luaL_error(l, "attempt to call 'pause' on %s %s", problem, METATABLE_TIMER);
	}

	// Return ourself
	lua_pushvalue(l, 1);
	return 1;
}

static int timer_again(lua_State *l) {
	MumbleTimer *ltimer = mumble_active_timer(l, 1);
	const char *problem = ltimer->restart();
	if (problem) {
		return luaL_error(l, "attempt to call 'again' on %s %s", problem, METATABLE_TIMER);
	}

	// Return ourself
	lua_pushvalue(l, 1);
	return 1;
}

static int timer_resume(lua_State *l) {
	MumbleTimer *ltimer = mumble_active_timer(l, 1);
	const char *problem = ltimer->resume();
	if (problem) {
		return luaL_error(l, "attempt to call 'resume' on %s %s", problem, METATABLE_TIMER);
	}

	// Return ourself
	lua_pushvalue(l, 1);
	return 1;
}

static int timer_set(lua_State *l) {
	MumbleTimer *ltimer = luaL_checkclass<MumbleTimer>(l, 1, METATABLE_TIMER);
	ltimer->after = luaL_checknumber(l, 2) * 1000;
	ltimer->setRepeat(luaL_optnumber(l, 3, 0) * 1000);

	// Return ourself
	lua_pushvalue(l, 1);
	return 1;
}

static int timer_get(lua_State *l) {
	MumbleTimer *ltimer = luaL_checkclass<MumbleTimer>(l, 1, METATABLE_TIMER);
	lua_pushnumber(l, (double) ltimer->after / 1000);
	lua_pushnumber(l, (double) ltimer->getRepeat() / 1000);
	return 2;
}

static int timer_setDuration(lua_State *l) {
	MumbleTimer *ltimer = luaL_checkclass<MumbleTimer>(l, 1, METATABLE_TIMER);

	ltimer->after = luaL_checknumber(l, 2) * 1000;

	// Return ourself
	lua_pushvalue(l, 1);
	return 1;
}

static int timer_getDuration(lua_State *l) {
	MumbleTimer *ltimer = luaL_checkclass<MumbleTimer>(l, 1, METATABLE_TIMER);
	lua_pushnumber(l, (double) ltimer->after / 1000);
	return 1;
}

static int timer_setRepeat(lua_State *l) {
	MumbleTimer *ltimer = luaL_checkclass<MumbleTimer>(l, 1, METATABLE_TIMER);

	ltimer->setRepeat(luaL_checknumber(l, 2) * 1000);

	// Return ourself
	lua_pushvalue(l, 1);
	return 1;
}

static int timer_getRepeat(lua_State *l) {
	MumbleTimer *ltimer = luaL_checkclass<MumbleTimer>(l, 1, METATABLE_TIMER);
	lua_pushnumber(l, (double) ltimer->getRepeat() / 1000);
	return 1;
}

static int timer_getCount(lua_State *l) {
	MumbleTimer *ltimer = luaL_checkclass<MumbleTimer>(l, 1, METATABLE_TIMER);
	lua_pushinteger(l, ltimer->count);
	return 1;
}

static int timer_getRemain(lua_State *l) {
	MumbleTimer *ltimer = luaL_checkclass<MumbleTimer>(l, 1, METATABLE_TIMER);
	lua_pushnumber(l, (double) ltimer->getRemain() / 1000);
	return 1;
}

static int timer_isActive(lua_State *l) {
	MumbleTimer *ltimer = luaL_checkclass<MumbleTimer>(l, 1, METATABLE_TIMER);
	lua_pushboolean(l, ltimer->isActive());
	return 1;
}

static int timer_isPaused(lua_State *l) {
	MumbleTimer *ltimer = luaL_checkclass<MumbleTimer>(l, 1, METATABLE_TIMER);
	lua_pushboolean(l, ltimer->paused);
	return 1;
}

static int timer_gc(lua_State *l) {
	MumbleTimer *ltimer = luaL_checkclass<MumbleTimer>(l, 1, METATABLE_TIMER);
	mumble_log(LOG_DEBUG, "%s: %p garbage collected", METATABLE_TIMER, ltimer);
	ltimer->close();
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
