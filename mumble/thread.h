#pragma once

#define METATABLE_THREAD			"mumble.thread"

extern int mumble_thread_new(lua_State *l);
extern const luaL_Reg mumble_thread[];