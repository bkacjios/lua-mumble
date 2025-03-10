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
			mumble_log(LOG_DEBUG, "end of pipe reached");
		} else {
			mumble_log(LOG_ERROR, "error reading from pipe: %s", uv_strerror(nread));
		}
		mumble_pipe_close(lpipe);
	} else if (nread > 0) {
		// Push error handler (if any)
		lua_pushcfunction(l, mumble_traceback);

		mumble_registry_pushref(l, MUMBLE_PIPE_REG, lpipe->callback);
		lua_pushlstring(l, buf->base, nread);  // Push the data to Lua

		// Call the callback with the received data
		if (lua_pcall(l, 1, 0, -4) != 0) {
			mumble_log(LOG_ERROR, "%s: %s", METATABLE_PIPE, lua_tostring(l, -1));
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

	int error = mkfifo(filepath, 0666);

	if (error != 0 && errno != EEXIST) {
		lua_pushnil(l);
		lua_pushfstring(l, "failed to mkfifo file: %s", strerror(errno));
		return 2;
	}

	int fd = open(filepath, O_RDONLY | O_NONBLOCK);

	if (fd == -1) {
		lua_pushnil(l);
		lua_pushfstring(l, "failed to open pipe: %s", strerror(errno));
		return 2;
	}

	struct stat st;
	if (fstat(fd, &st) != 0 || !S_ISFIFO(st.st_mode)) {
		lua_pushnil(l);
		lua_pushstring(l, "failed to open pipe: file is not an FIFO file");
		close(fd);
		return 2;
	}

	uv_pipe_t *pipe = malloc(sizeof(uv_pipe_t));

	if (!pipe) {
		lua_pushnil(l);
		lua_pushfstring(l, "failed to allocate memory for pipe: %s", strerror(errno));
		close(fd);
		return 2;
	}

	error = uv_pipe_init(uv_default_loop(), pipe, 0);

	if (error != 0) {
		lua_pushnil(l);
		lua_pushfstring(l, "failed to initialize pipe: %s", uv_strerror(error));
		close(fd);
		free(pipe);
		return 2;
	}

	error = uv_pipe_open(pipe, fd);

	if (error != 0) {
		lua_pushnil(l);
		lua_pushfstring(l, "failed to open pipe: %s", uv_strerror(error));
		close(fd);
		free(pipe);
		return 2;
	}

	MumblePipe *lpipe = lua_newuserdata(l, sizeof(MumblePipe));
	lpipe->l = l;
	lpipe->callback = LUA_NOREF;
	lpipe->pipe = pipe;
	luaL_getmetatable(l, METATABLE_PIPE);
	lua_setmetatable(l, -2);

	lpipe->pipe->data = lpipe;

	lua_pushvalue(l, -1);
	lpipe->self = mumble_registry_ref(l, MUMBLE_PIPE_REG);

	lua_pushvalue(l, 3);
	lpipe->callback = mumble_registry_ref(l, MUMBLE_PIPE_REG);

	uv_read_start((uv_stream_t*)&lpipe->pipe, alloc_buffer, on_read);

	// Return the pipe
	return 1;
}

static int pipe_close(lua_State *l) {
	MumblePipe *lpipe = luaL_checkudata(l, 1, METATABLE_PIPE);
	mumble_pipe_close(lpipe);
	if (lpipe->pipe) {
		free(lpipe->pipe);
	}
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