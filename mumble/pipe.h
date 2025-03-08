#pragma once

#include <lauxlib.h>

typedef struct MumblePipe MumblePipe;

struct MumblePipe {
	uv_pipe_t pipe;
	lua_State* l;
	int self;
	int callback;
};

#define METATABLE_PIPE			"mumble.pipe"

extern int mumble_pipe_new(lua_State *l);
extern const luaL_Reg mumble_pipe[];