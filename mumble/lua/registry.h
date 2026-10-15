#pragma once

#include "lua/luainc.h"

// References to Lua values that the module keeps alive, stored in our own table inside the Lua registry
class LuaRegistry {
public:
	// The tables stored inside our table, at fixed indexes so every Lua state (and thread) agrees on them
	static constexpr int clients = 1;
	static constexpr int timers = 2;
	static constexpr int threads = 3;
	static constexpr int pipes = 4;
	static constexpr int data = 5;

	// Create our tables, called when the module is opened
	static void open(lua_State *l);
	// Push our table, which is stored in the Lua registry under a fixed key
	static void pushRoot(lua_State *l);

	// Pop the value on top of the stack and reference it in our table
	static int ref(lua_State *l);
	static void push(lua_State *l, int ref);
	static void unref(lua_State *l, int *ref);

	// Pop the value on top of the stack and reference it in one of our tables
	static int refIn(lua_State *l, int table);
	static void pushFrom(lua_State *l, int table, int ref);
	static void unrefIn(lua_State *l, int table, int *ref);
};
