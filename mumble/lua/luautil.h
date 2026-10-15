#pragma once

#include "lua/luainc.h"

#include <optional>
#include <protobuf-c/protobuf-c.h>
#include <string>
#include <vector>

#if (defined(LUA_VERSION_NUM) && LUA_VERSION_NUM < 502) && !defined(LUAJIT)
void* luaL_testudata(lua_State* L, int index, const char* tname);
void luaL_traceback(lua_State* L, lua_State* L1, const char* msg, int level);
#endif

void luaL_debugstack(lua_State *l, const char* text);
int luaL_typerror(lua_State *L, int narg, const char *tname);
int luaL_typerror_table(lua_State *L, int narg, int nkey, int nvalue, const char *tname);
int luaL_checkfunction(lua_State *L, int i);
int luaL_checkboolean(lua_State *L, int i);
int luaL_optboolean(lua_State *L, int i, int d);
int luaL_isudata(lua_State *L, int ud, const char *tname);

// Pushes a table describing a 16 byte (IPv4 mapped or IPv6) address
int lua_pushaddress(lua_State* l, ProtobufCBinaryData address);

// Pushes the string, or nil when it was never set
void lua_pushoptstring(lua_State *l, const std::optional<std::string> &str);

// Read the objects of type T passed either as a table at index, or as index and every argument after it.
// Validates everything before allocating, so a Lua error can't leak the result.
template <typename T>
std::vector<T*> luaL_checkobjects(lua_State *l, int index, const char *tname) {
	std::vector<T*> objects;
	if (lua_istable(l, index)) {
		lua_pushvalue(l, index);
		for (int pass = 0; pass < 2; pass++) {
			lua_pushnil(l);
			while (lua_next(l, -2)) {
				if (pass == 0 && !luaL_isudata(l, -1, tname)) {
					luaL_typerror_table(l, index, -2, -1, tname);
				}
				if (pass == 1) objects.push_back(static_cast<T*>(lua_touserdata(l, -1)));
				lua_pop(l, 1);
			}
		}
		lua_pop(l, 1);
	} else {
		int top = lua_gettop(l);
		for (int i = index; i <= top; i++) {
			if (!luaL_isudata(l, i, tname)) luaL_typerror(l, i, tname);
		}
		for (int i = index; i <= top; i++) {
			objects.push_back(static_cast<T*>(lua_touserdata(l, i)));
		}
	}
	return objects;
}

// Read the strings in a table, in iteration order
std::vector<const char*> luaL_checkstrings(lua_State *l, int index);
