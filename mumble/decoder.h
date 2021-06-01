#pragma once

#define METATABLE_DECODER		"mumble.decoder"

extern int mumble_decoder_new(lua_State *l);
extern const luaL_Reg mumble_decoder[];