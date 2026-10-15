#include "util/pipe.h"

#include "lua/bindings.h"
#include "lua/luainc.h"
#include "lua/metatables.h"
#include "lua/registry.h"
#include "util/log.h"

int mumble_pipe_new(lua_State *l) {
	lua_remove(l, 1);

	const char* path = luaL_checkstring(l, 1);
	luaL_checktype(l, 2, LUA_TFUNCTION);

	MumblePipe *lpipe = lua_newclass<MumblePipe>(l, l);
	std::string error = lpipe->open(path);
	if (!error.empty()) {
		lpipe->~MumblePipe();
		lua_pop(l, 1);
		lua_pushnil(l);
		lua_pushstring(l, error.c_str());
		return 2;
	}

	luaL_getmetatable(l, METATABLE_PIPE);
	lua_setmetatable(l, -2);
	lpipe->listen(lua_gettop(l), 2);

	// Return the pipe
	return 1;
}

static int pipe_close(lua_State *l) {
	MumblePipe *lpipe = luaL_checkclass<MumblePipe>(l, 1, METATABLE_PIPE);
	lpipe->close();
	return 1;
}

static int pipe_gc(lua_State *l) {
	MumblePipe *lpipe = luaL_checkclass<MumblePipe>(l, 1, METATABLE_PIPE);
	mumble_log(LOG_DEBUG, "%s: %p garbage collected", METATABLE_PIPE, lpipe);
	lpipe->~MumblePipe();
	return 0;
}

static int pipe_tostring(lua_State *l) {
	lua_pushfstring(l, "%s: %p", METATABLE_PIPE, lua_topointer(l, 1));
	return 1;
}

const luaL_Reg mumble_pipe[] = {
	{"close", pipe_close},
	{"__tostring", pipe_tostring},
	{"__gc", pipe_gc},
	{NULL, NULL}
};
