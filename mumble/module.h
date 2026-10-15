#pragma once

#include "lua/luainc.h"

extern "C" int luaopen_mumble(lua_State *l);
// The module for thread workers, without anything that needs the main thread's event loop
int luaopen_mumble_worker(lua_State *l);

// Error handler for lua_pcall that appends a stack traceback to the error
int mumble_traceback(lua_State *l);
