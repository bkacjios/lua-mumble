#pragma once

#define METATABLE_THREAD_CONTROLLER		"mumble.thread.controller"
#define METATABLE_THREAD_WORKER			"mumble.thread.worker"

extern int mumble_thread_new(lua_State *l);

extern const luaL_Reg mumble_thread_controller[];
extern const luaL_Reg mumble_thread_worker[];