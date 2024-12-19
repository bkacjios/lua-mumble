#pragma once

#define METATABLE_THREAD_SERVER		"mumble.thread.server"
#define METATABLE_THREAD_CLIENT		"mumble.thread.client"

extern int mumble_thread_new(lua_State *l);
int mumble_thread_exit(lua_State *l, pthread_t thread, int finished);
extern void mumble_thread_join_all();

extern const luaL_Reg mumble_thread_server[];
extern const luaL_Reg mumble_thread_client[];