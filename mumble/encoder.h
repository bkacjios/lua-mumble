#pragma once

#include <lauxlib.h>

#define METATABLE_ENCODER		"mumble.encoder"

extern int mumble_encoder_new(lua_State *l);
extern const luaL_Reg mumble_encoder[];