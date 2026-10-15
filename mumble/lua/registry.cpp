#include "lua/registry.h"

#include "lua/luainc.h"

// Where our table is stored in the Lua registry
static const char *const REGISTRY_KEY = "mumble.registry";

void LuaRegistry::open(lua_State *l) {
	lua_newtable(l);

	// Fill our fixed indexes before anything else is referenced in our table.
	// luaL_ref keeps its free list at index 0, so it only ever hands out indexes after these.
	for (int table = clients; table <= data; table++) {
		lua_newtable(l);
		lua_rawseti(l, -2, table);
	}

	lua_setfield(l, LUA_REGISTRYINDEX, REGISTRY_KEY);
}

void LuaRegistry::pushRoot(lua_State *l) {
	lua_getfield(l, LUA_REGISTRYINDEX, REGISTRY_KEY);
}

int LuaRegistry::ref(lua_State *l) {
	int ref;
	pushRoot(l);
	lua_pushvalue(l, -2);
	ref = luaL_ref(l, -2);
	lua_pop(l, 2);
	return ref;
}

void LuaRegistry::push(lua_State *l, int ref) {
	pushRoot(l);
	lua_rawgeti(l, -1, ref);
	lua_remove(l, -2);
}

void LuaRegistry::unref(lua_State *l, int *ref) {
	if (*ref == LUA_NOREF) return;
	pushRoot(l);
	luaL_unref(l, -1, *ref);
	lua_pop(l, 1);
	*ref = LUA_NOREF;
}

int LuaRegistry::refIn(lua_State *l, int table) {
	int ref;
	push(l, table);
	lua_pushvalue(l, -2);
	ref = luaL_ref(l, -2);
	lua_pop(l, 2);
	return ref;
}

void LuaRegistry::pushFrom(lua_State *l, int table, int ref) {
	push(l, table);
	lua_rawgeti(l, -1, ref);
	lua_remove(l, -2);
}

void LuaRegistry::unrefIn(lua_State *l, int table, int *ref) {
	if (*ref == LUA_NOREF) return;
	push(l, table);
	luaL_unref(l, -1, *ref);
	lua_pop(l, 1);
	*ref = LUA_NOREF;
}
