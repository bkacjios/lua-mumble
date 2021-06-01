#pragma once

#define METATABLE_TIMER			"mumble.timer"

extern int mumble_timer_new(lua_State *l);
extern const luaL_Reg mumble_timer[];