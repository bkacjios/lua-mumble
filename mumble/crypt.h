#pragma once

#include <lauxlib.h>

#define METATABLE_CRYPT		"mumble.crypt"

int mumble_crypt_new(lua_State* l);

extern const luaL_Reg mumble_ocb_aes128[];