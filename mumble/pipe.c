#include "mumble.h"

#include "pipe.h"
#include "util.h"
#include "log.h"
#include <unistd.h>

static void mumble_pipe_on_read(uv_stream_t *handle, ssize_t nread, const uv_buf_t *buf);

static void mumble_pipe_close_cb(uv_handle_t* handle) {
	MumblePipe* lpipe = (MumblePipe*)handle->data;
	if (lpipe->pipe) {
		free(lpipe->pipe); // Safe to free now
	}
}

static void mumble_pipe_close(MumblePipe* lpipe) {
	uv_read_stop((uv_stream_t*) lpipe->pipe);
	
	if (!uv_is_closing((uv_handle_t*) lpipe->pipe)) {
		uv_close((uv_handle_t*) lpipe->pipe, mumble_pipe_close_cb);
	}

	if (lpipe->callback > LUA_REFNIL) {
		mumble_log(LOG_TRACE, "%s: %p unreference callback: %d", METATABLE_PIPE, lpipe, lpipe->callback);
		mumble_registry_unref(lpipe->l, MUMBLE_PIPE_REG, &lpipe->callback);
	}
	if (lpipe->self > LUA_REFNIL) {
		mumble_log(LOG_TRACE, "%s: %p unreference self: %d", METATABLE_PIPE, lpipe, lpipe->self);
		mumble_registry_unref(lpipe->l, MUMBLE_PIPE_REG, &lpipe->self);
	}
}

static void mumble_pipe_alloc_buffer(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf) {
	buf->base = (char*)malloc(suggested_size);
	buf->len = suggested_size;
}

static void mumble_pipe_reopen(uv_handle_t* handle) {
	MumblePipe* lpipe = (MumblePipe*) handle->data;

	// Allocate a new uv_pipe_t
	uv_pipe_t *new_pipe = malloc(sizeof(uv_pipe_t));
	if (!new_pipe) {
		mumble_log(LOG_ERROR, "failed to allocate memory for pipe %s (%s)", lpipe->path, strerror(errno));
		mumble_pipe_close(lpipe);
		return;
	}

	// Initialize the new pipe handle
	uv_pipe_init(uv_default_loop(), new_pipe, 0);

	// Open the new file descriptor
	int fd = open(lpipe->path, O_RDONLY | O_NONBLOCK);
	if (fd < 0) {
		mumble_log(LOG_ERROR, "failed to reopen pipe %s (%s)", lpipe->path, strerror(errno));
		free(new_pipe);  // Free allocated memory on failure
		mumble_pipe_close(lpipe);
		return;
	}

	// Associate new file descriptor with the new pipe
	int error = uv_pipe_open(new_pipe, fd);
	if (error != 0) {
		mumble_log(LOG_ERROR, "failed to re-associate pipe %s (%s)", lpipe->path, uv_strerror(error));
		close(fd);
		free(new_pipe);
		mumble_pipe_close(lpipe);
		return;
	}

	// Restart reading on the new pipe
	error = uv_read_start((uv_stream_t*) new_pipe, mumble_pipe_alloc_buffer, mumble_pipe_on_read);
	if (error != 0) {
		mumble_log(LOG_ERROR, "failed to restart reading from pipe %s (%s)", new_pipe, uv_strerror(error));
		mumble_pipe_close(lpipe);
		return;
	}

	free(lpipe->pipe);
	lpipe->pipe = new_pipe;
	lpipe->pipe->data = lpipe;

	mumble_log(LOG_TRACE, "%s: %p pipe successfully reopened for reading", METATABLE_PIPE, lpipe);
}

static void mumble_pipe_on_read(uv_stream_t *handle, ssize_t nread, const uv_buf_t *buf) {
	MumblePipe* lpipe = (MumblePipe*) handle->data;
	lua_State* l = lpipe->l;

	if (nread < 0) {
		if (nread == UV_EOF) {
			mumble_log(LOG_TRACE, "%s: %p end of pipe reached", METATABLE_PIPE, lpipe);
			uv_close((uv_handle_t*) lpipe->pipe, mumble_pipe_reopen);
		} else {
			mumble_log(LOG_ERROR, "error reading from pipe %s (%s)", lpipe->path, uv_strerror(nread));
			mumble_pipe_close(lpipe);
		}
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

int mumble_pipe_new(lua_State *l) {
	lua_remove(l, 1);

	const char* filepath = luaL_checkstring(l, 1);
	luaL_checktype(l, 2, LUA_TFUNCTION);

	int error = mkfifo(filepath, 0666);

	// Make the fifo file if it doesn't already exist
	if (error != 0 && errno != EEXIST) {
		lua_pushnil(l);
		lua_pushfstring(l, "failed to mkfifo pipe: %s", strerror(errno));
		return 2;
	}

	// Open the new file descriptor
	int fd = open(filepath, O_RDONLY | O_NONBLOCK);
	if (fd < 0) {
		lua_pushnil(l);
		lua_pushfstring(l, "failed to open pipe: %s", strerror(errno));
		return 2;
	}

	// Check the file is proper FIFI file
	struct stat st;
	if (fstat(fd, &st) != 0 || !S_ISFIFO(st.st_mode)) {
		lua_pushnil(l);
		lua_pushstring(l, "failed to open pipe: file is not an FIFO file");
		close(fd);
		return 2;
	}

	// Allocate a new uv_pipe_t
	uv_pipe_t *pipe = malloc(sizeof(uv_pipe_t));
	if (!pipe) {
		lua_pushnil(l);
		lua_pushfstring(l, "failed to allocate memory for pipe: %s", strerror(errno));
		close(fd);
		return 2;
	}

	// Initialize the new pipe handle
	error = uv_pipe_init(uv_default_loop(), pipe, 0);
	if (error != 0) {
		lua_pushnil(l);
		lua_pushfstring(l, "failed to initialize pipe: %s", uv_strerror(error));
		close(fd);
		free(pipe);
		return 2;
	}

	// Associate new file descriptor with the new pipe
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
	lpipe->path = strdup((char*)filepath);
	lpipe->pipe = pipe;
	lpipe->pipe->data = lpipe;
	luaL_getmetatable(l, METATABLE_PIPE);
	lua_setmetatable(l, -2);

	lua_pushvalue(l, -1);
	lpipe->self = mumble_registry_ref(l, MUMBLE_PIPE_REG);

	lua_pushvalue(l, 3);
	lpipe->callback = mumble_registry_ref(l, MUMBLE_PIPE_REG);

	uv_read_start((uv_stream_t*)lpipe->pipe, mumble_pipe_alloc_buffer, mumble_pipe_on_read);

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
	if (lpipe->path) {
		free(lpipe->path);
	}
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