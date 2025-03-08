#include "mumble.h"

#include "pipe.h"
#include "util.h"
#include "log.h"
#include <unistd.h>

static void mumble_pipe_close(MumblePipe* lpipe) {
	if (uv_is_active((uv_handle_t*) &lpipe->pipe)) {
		uv_close((uv_handle_t*) &lpipe->pipe, NULL);
	}

	if (lpipe->callback > LUA_REFNIL) {
		mumble_registry_unref(lpipe->l, MUMBLE_THREAD_REG, &lpipe->callback);
	}
	if (lpipe->self > LUA_REFNIL) {
		mumble_registry_unref(lpipe->l, MUMBLE_THREAD_REG, &lpipe->self);
	}
}

static void on_read(uv_stream_t *handle, ssize_t nread, const uv_buf_t *buf) {
	MumblePipe* lpipe = (MumblePipe*) handle->data;
	lua_State* l = lpipe->l;

	if (nread < 0) {
		if (nread == UV_EOF) {
			mumble_log(LOG_INFO, "End of pipe reached");
		} else {
			mumble_log(LOG_ERROR, "Error reading from pipe: %s", uv_strerror(nread));
		}
		mumble_pipe_close(lpipe);
	} else if (nread > 0) {
		// Push error handler (if any)
		lua_pushcfunction(l, mumble_traceback);

		mumble_registry_pushref(l, MUMBLE_PIPE_REG, lpipe->callback);
		lua_pushlstring(l, buf->base, nread);  // Push the data to Lua

		// Call the callback with the received data
		if (lua_pcall(l, 1, 0, -4) != 0) {
			mumble_log(LOG_ERROR, "Error in callback: %s", lua_tostring(l, -1));
			lua_pop(l, 1);  // Pop the error message
		}

		lua_pop(l, 1);  // Pop the error handler
	}

	// Clean up the buffer
	if (buf->base) {
		free(buf->base);
	}
}


static void alloc_buffer(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf) {
	buf->base = (char*)malloc(suggested_size);
	buf->len = suggested_size;
}

int mumble_pipe_new(lua_State *l) {
	const char* filepath = luaL_checkstring(l, 2);
	luaL_checktype(l, 3, LUA_TFUNCTION);

	MumblePipe *lpipe = lua_newuserdata(l, sizeof(MumblePipe));
	lpipe->l = l;
	lpipe->callback = LUA_NOREF;
	luaL_getmetatable(l, METATABLE_PIPE);
	lua_setmetatable(l, -2);

	uv_pipe_init(uv_default_loop(), &lpipe->pipe, 0);

	int fd = open(filepath, O_RDONLY | O_NONBLOCK);
	if (fd == -1) {
		lua_pushnil(l);
		lua_pushfstring(l, "failed to open pipe: %s", strerror(errno));
		return 2;
	}

	if (uv_pipe_open(&lpipe->pipe, fd)) {
		lua_pushnil(l);
		lua_pushfstring(l, "failed to open pipe: %s", strerror(errno));
		close(fd);
		return 2;
	}

	lpipe->pipe.data = lpipe;

	lua_pushvalue(l, -1);
	lpipe->self = mumble_registry_ref(l, MUMBLE_PIPE_REG);

	lua_pushvalue(l, 3);
	lpipe->callback = mumble_registry_ref(l, MUMBLE_PIPE_REG);

	uv_read_start((uv_stream_t*)&lpipe->pipe, alloc_buffer, on_read);

	luaL_debugstack(l, "create pipe");

	// Return the pipe
	return 1;
}

static int pipe_close(lua_State *l) {
	MumblePipe *lpipe = luaL_checkudata(l, 1, METATABLE_PIPE);
	mumble_pipe_close(lpipe);
	return 1;
}

static int pipe_gc(lua_State *l) {
	MumblePipe *lpipe = luaL_checkudata(l, 1, METATABLE_PIPE);
	mumble_log(LOG_DEBUG, "%s: %p garbage collected", METATABLE_PIPE, lpipe);
	mumble_pipe_close(lpipe);
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