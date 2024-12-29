#pragma once

#include <lauxlib.h>

#define METATABLE_CHAN			"mumble.channel"

extern int channel_call(lua_State *l);
extern const luaL_Reg mumble_channel[];