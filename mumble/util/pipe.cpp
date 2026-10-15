#include "util/pipe.h"

#include "lua/luainc.h"
#include "lua/metatables.h"
#include "lua/registry.h"
#include "module.h"
#include "util/log.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

void MumblePipe::onClose(uv_handle_t* handle) {
	MumblePipe* lpipe = (MumblePipe*)handle->data;
	if (lpipe->pipe) {
		free(lpipe->pipe); // Safe to free now
	}
}

MumblePipe::MumblePipe(lua_State *l) : l(l) {
}

std::string MumblePipe::open(const char *filepath) {
	// Make the fifo file if it doesn't already exist
	if (mkfifo(filepath, 0666) != 0 && errno != EEXIST) {
		return std::string("failed to mkfifo pipe: ") + strerror(errno);
	}

	int fd = ::open(filepath, O_RDONLY | O_NONBLOCK);
	if (fd < 0) {
		return std::string("failed to open pipe: ") + strerror(errno);
	}

	// Check the file is proper FIFO file
	struct stat st;
	if (fstat(fd, &st) != 0 || !S_ISFIFO(st.st_mode)) {
		::close(fd);
		return "failed to open pipe: file is not an FIFO file";
	}

	uv_pipe_t *handle = (uv_pipe_t*) malloc(sizeof(uv_pipe_t));
	if (!handle) {
		::close(fd);
		return std::string("failed to allocate memory for pipe: ") + strerror(errno);
	}

	int error = uv_pipe_init(uv_default_loop(), handle, 0);
	if (error != 0) {
		::close(fd);
		::free(handle);
		return std::string("failed to initialize pipe: ") + uv_strerror(error);
	}

	error = uv_pipe_open(handle, fd);
	if (error != 0) {
		::close(fd);
		// The handle is registered with the loop now, so it has to be closed before it's freed
		uv_close((uv_handle_t*) handle, [](uv_handle_t *h) { ::free(h); });
		return std::string("failed to open pipe: ") + uv_strerror(error);
	}

	pipe = handle;
	pipe->data = this;
	path = strdup(filepath);
	return "";
}

void MumblePipe::listen(int self_index, int callback_index) {
	lua_pushvalue(l, self_index);
	self = LuaRegistry::refIn(l, LuaRegistry::pipes);

	lua_pushvalue(l, callback_index);
	callback = LuaRegistry::refIn(l, LuaRegistry::pipes);

	uv_read_start((uv_stream_t*) pipe, MumblePipe::onAlloc, MumblePipe::onRead);
}

MumblePipe::~MumblePipe() {
	if (path) {
		free(path);
	}
}

void MumblePipe::close() {
	uv_read_stop((uv_stream_t*) pipe);

	if (!uv_is_closing((uv_handle_t*) pipe)) {
		uv_close((uv_handle_t*) pipe, MumblePipe::onClose);
	}

	if (callback > LUA_REFNIL) {
		mumble_log(LOG_TRACE, "%s: %p unreference callback: %d", METATABLE_PIPE, this, callback);
		LuaRegistry::unrefIn(l, LuaRegistry::pipes, &callback);
	}
	if (self > LUA_REFNIL) {
		mumble_log(LOG_TRACE, "%s: %p unreference self: %d", METATABLE_PIPE, this, self);
		LuaRegistry::unrefIn(l, LuaRegistry::pipes, &self);
	}
}

void MumblePipe::onAlloc(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf) {
	buf->base = (char*)malloc(suggested_size);
	buf->len = suggested_size;
}

void MumblePipe::onReopen(uv_handle_t* handle) {
	MumblePipe* lpipe = (MumblePipe*) handle->data;

	// Allocate a new uv_pipe_t
	uv_pipe_t *new_pipe = (uv_pipe_t*) malloc(sizeof(uv_pipe_t));
	if (!new_pipe) {
		mumble_log(LOG_ERROR, "failed to allocate memory for pipe %s (%s)", lpipe->path, strerror(errno));
		lpipe->close();
		return;
	}

	// Initialize the new pipe handle
	uv_pipe_init(uv_default_loop(), new_pipe, 0);

	// Open the new file descriptor
	int fd = ::open(lpipe->path, O_RDONLY | O_NONBLOCK);
	if (fd < 0) {
		mumble_log(LOG_ERROR, "failed to reopen pipe %s (%s)", lpipe->path, strerror(errno));
		free(new_pipe);  // Free allocated memory on failure
		lpipe->close();
		return;
	}

	// Associate new file descriptor with the new pipe
	int error = uv_pipe_open(new_pipe, fd);
	if (error != 0) {
		mumble_log(LOG_ERROR, "failed to re-associate pipe %s (%s)", lpipe->path, uv_strerror(error));
		::close(fd);
		free(new_pipe);
		lpipe->close();
		return;
	}

	// Restart reading on the new pipe
	error = uv_read_start((uv_stream_t*) new_pipe, MumblePipe::onAlloc, MumblePipe::onRead);
	if (error != 0) {
		mumble_log(LOG_ERROR, "failed to restart reading from pipe %s (%s)", lpipe->path, uv_strerror(error));
		lpipe->close();
		return;
	}

	free(lpipe->pipe);
	lpipe->pipe = new_pipe;
	lpipe->pipe->data = lpipe;

	mumble_log(LOG_TRACE, "%s: %p pipe successfully reopened for reading", METATABLE_PIPE, lpipe);
}

void MumblePipe::onRead(uv_stream_t *handle, ssize_t nread, const uv_buf_t *buf) {
	MumblePipe* lpipe = (MumblePipe*) handle->data;
	lua_State* l = lpipe->l;

	if (nread < 0) {
		if (nread == UV_EOF) {
			mumble_log(LOG_TRACE, "%s: %p end of pipe reached", METATABLE_PIPE, lpipe);
			uv_close((uv_handle_t*) lpipe->pipe, MumblePipe::onReopen);
		} else {
			mumble_log(LOG_ERROR, "error reading from pipe %s (%s)", lpipe->path, uv_strerror(nread));
			lpipe->close();
		}
	} else if (nread > 0) {
		// Push error handler (if any)
		lua_pushcfunction(l, mumble_traceback);

		LuaRegistry::pushFrom(l, LuaRegistry::pipes, lpipe->callback);
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
