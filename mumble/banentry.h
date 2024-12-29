#pragma once

#include <lauxlib.h>

#define METATABLE_BANENTRY	"mumble.banentry"

extern int mumble_banentry_new(lua_State *l);
extern const luaL_Reg mumble_banentry[];