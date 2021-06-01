#pragma once

#define METATABLE_VOICETARGET	"mumble.voicetarget"

extern int mumble_target_new(lua_State *l);
extern const luaL_Reg mumble_target[];