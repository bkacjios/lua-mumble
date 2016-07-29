#include "mumble.h"

double gettime()
{
	struct timeval time;
	gettimeofday(&time, (struct timezone *) NULL);
	return time.tv_sec + time.tv_usec/1.0e6;
}

MumbleClient* mumble_check_meta(lua_State *L, int i, const char* meta)
{
	luaL_checktablemeta(L, i, meta);

	lua_getfield(L, i, "client");

	MumbleClient *client = lua_touserdata(L, -1);

	if (client != NULL) {
		lua_getmetatable(L, -1);
		lua_getfield(L, LUA_REGISTRYINDEX, METATABLE_CLIENT);
		if (lua_rawequal(L, -1, -2)) {
			lua_pop(L, 3);
			return client;
		}
	}

	luaL_error(L, "%s does not have a %s", meta, METATABLE_CLIENT);
	return NULL;
}

int luaL_checkboolean(lua_State *L, int i){
	if(!lua_isboolean(L,i))
		luaL_typerror(L,i,"boolean");
	return lua_toboolean(L,i);
}

int luaL_optboolean(lua_State *L, int i, int d){
	if(lua_type(L, i) < 1)
		return d;
	else
		return luaL_checkboolean(L, i);
}

void luaL_checktablemeta(lua_State *L, int i, const char* m)
{
	luaL_checktype(L, i, LUA_TTABLE);

	lua_getmetatable(L, i);
	luaL_getmetatable(L, m);

	int ret = lua_equal(L, -1, -2);

	lua_pop(L, 2);

	if (ret == 0) {
		luaL_typerror(L, i, m);
	}
}

const char* eztype(lua_State *L, int i)
{
	return lua_typename(L, lua_type(L, i));
}