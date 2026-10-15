#pragma once

#include <cassert>
#include <new>

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

#define LUA_COMPAT_MODULE
#define LUA_COMPAT_5_1

#if defined(LUA_VERSION_NUM) && LUA_VERSION_NUM == 502 && !defined(LUAJIT)
#define lua_objlen lua_rawlen
#endif

#if LUA_VERSION_NUM >= 502
#define luaL_register(L, libname, l) \
    ((libname) == NULL ? luaL_setfuncs(L, (l), 0) : \
    (lua_newtable(L), luaL_setfuncs(L, (l), 0), lua_pushvalue(L, -1), lua_setglobal(L, (libname))))
#endif

#if LUA_VERSION_NUM >= 503
#define luaL_optlong(L, n, d) ((long)luaL_opt(L, luaL_checkinteger, (n), (lua_Integer)(d)))
#define luaL_checkint(L, n) ((int)luaL_checkinteger(L, (n)))
#define lua_objlen(L, idx) lua_rawlen(L, (idx))
#define lua_dump(L, writer, data) lua_dump(L, writer, data, 1)
#endif

#define lua_stackguard_entry(L) int __lua_stackguard_entry = lua_gettop(L);
#define lua_stackguard_exit(L) assert(__lua_stackguard_entry == lua_gettop(L));

// Typed wrappers around the Lua userdata accessors
template <typename T>
static inline T* luaL_checkclass(lua_State *l, int index, const char *tname) {
	return static_cast<T*>(luaL_checkudata(l, index, tname));
}

template <typename T>
static inline T* lua_toclass(lua_State *l, int index) {
	return static_cast<T*>(lua_touserdata(l, index));
}

// Allocate a Lua userdata and construct a T inside of it
template <typename T, typename... Args>
static inline T* lua_newclass(lua_State *l, Args&&... args) {
	return new (lua_newuserdata(l, sizeof(T))) T(static_cast<Args&&>(args)...);
}
