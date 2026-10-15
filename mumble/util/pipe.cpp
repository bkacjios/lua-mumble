#include "util/pipe.h"

#include "lua/luainc.h"
#include "lua/metatables.h"
#include "lua/registry.h"
#include "module.h"
#include "util/log.h"

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>

#ifndef MUMBLE_NAMED_PIPE
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

MumblePipe::MumblePipe(lua_State *l) : l(l) {
}

MumblePipe::~MumblePipe() {
	closeHandles();
}

void MumblePipe::closeHandle(uv_pipe_t *handle) {
	uv_read_stop((uv_stream_t*) handle);
	if (!uv_is_closing((uv_handle_t*) handle)) {
		uv_close((uv_handle_t*) handle, [](uv_handle_t *h) {
			::free(h);
		});
	}
}

void MumblePipe::close() {
	closeHandles();

	if (callback > LUA_REFNIL) {
		mumble_log(LOG_TRACE, "%s: %p unreference callback: %d", METATABLE_PIPE, this, callback);
		LuaRegistry::unrefIn(l, LuaRegistry::pipes, &callback);
	}
	if (self > LUA_REFNIL) {
		mumble_log(LOG_TRACE, "%s: %p unreference self: %d", METATABLE_PIPE, this, self);
		LuaRegistry::unrefIn(l, LuaRegistry::pipes, &self);
	}
}

void MumblePipe::listen(int self_index, int callback_index) {
	lua_pushvalue(l, self_index);
	self = LuaRegistry::refIn(l, LuaRegistry::pipes);

	lua_pushvalue(l, callback_index);
	callback = LuaRegistry::refIn(l, LuaRegistry::pipes);

#ifdef MUMBLE_NAMED_PIPE
	// Writers are read from as they connect, in onConnection
#else
	uv_read_start((uv_stream_t*) pipe, MumblePipe::onAlloc, MumblePipe::onRead);
#endif
}

void MumblePipe::onAlloc(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf) {
	buf->base = (char*) malloc(suggested_size);
	buf->len = buf->base ? suggested_size : 0;
}

void MumblePipe::dispatch(const char *data, size_t length) {
	// Push our error handler
	lua_pushcfunction(l, mumble_traceback);

	LuaRegistry::pushFrom(l, LuaRegistry::pipes, callback);
	lua_pushlstring(l, data, length);

	// Call the callback with the received data
	if (lua_pcall(l, 1, 0, -3) != 0) {
		mumble_log(LOG_ERROR, "%s: %s", METATABLE_PIPE, lua_tostring(l, -1));
		lua_pop(l, 1); // Pop the error message
	}

	lua_pop(l, 1); // Pop the error handler
}

#ifdef MUMBLE_NAMED_PIPE

void MumblePipe::closeHandles() {
	if (closed) return;
	closed = true;

	for (uv_pipe_t *client : clients) {
		closeHandle(client);
	}
	clients.clear();

	if (server) {
		closeHandle(server);
		server = nullptr;
	}
}

std::string MumblePipe::open(const char *name) {
#ifdef _WIN32
	// Named pipes all live under \\.\pipe\, so put any plain name there
	static const char prefix[] = "\\\\.\\pipe\\";
	path = strncmp(name, prefix, sizeof(prefix) - 1) == 0 ? name : std::string(prefix) + name;
#else
	// Only for testing named pipes elsewhere, where libuv makes this a unix socket
	path = name;
#endif

	uv_pipe_t *handle = (uv_pipe_t*) malloc(sizeof(uv_pipe_t));
	if (!handle) {
		return std::string("failed to allocate memory for pipe: ") + strerror(errno);
	}

	int error = uv_pipe_init(uv_default_loop(), handle, 0);
	if (error != 0) {
		::free(handle);
		return std::string("failed to initialize pipe: ") + uv_strerror(error);
	}
	handle->data = this;

	error = uv_pipe_bind(handle, path.c_str());
	if (error == 0) {
		error = uv_listen((uv_stream_t*) handle, 4, MumblePipe::onConnection);
	}
	if (error != 0) {
		closeHandle(handle);
		return std::string("failed to open pipe: ") + uv_strerror(error);
	}

	server = handle;
	return "";
}

void MumblePipe::onConnection(uv_stream_t *server, int status) {
	MumblePipe *lpipe = (MumblePipe*) server->data;

	if (status < 0) {
		mumble_log(LOG_ERROR, "error accepting writer on pipe %s (%s)", lpipe->path.c_str(), uv_strerror(status));
		return;
	}

	uv_pipe_t *client = (uv_pipe_t*) malloc(sizeof(uv_pipe_t));
	if (!client) {
		mumble_log(LOG_ERROR, "failed to allocate memory for pipe %s (%s)", lpipe->path.c_str(), strerror(errno));
		return;
	}

	uv_pipe_init(uv_default_loop(), client, 0);
	client->data = lpipe;

	int error = uv_accept(server, (uv_stream_t*) client);
	if (error == 0) {
		error = uv_read_start((uv_stream_t*) client, MumblePipe::onAlloc, MumblePipe::onRead);
	}
	if (error != 0) {
		mumble_log(LOG_ERROR, "failed to read from writer on pipe %s (%s)", lpipe->path.c_str(), uv_strerror(error));
		closeHandle(client);
		return;
	}

	lpipe->clients.push_back(client);
	mumble_log(LOG_TRACE, "%s: %p writer connected", METATABLE_PIPE, lpipe);
}

void MumblePipe::onRead(uv_stream_t *handle, ssize_t nread, const uv_buf_t *buf) {
	MumblePipe *lpipe = (MumblePipe*) handle->data;

	if (nread > 0) {
		lpipe->dispatch(buf->base, nread);
	} else if (nread < 0) {
		if (nread == UV_EOF) {
			mumble_log(LOG_TRACE, "%s: %p writer disconnected", METATABLE_PIPE, lpipe);
		} else {
			mumble_log(LOG_ERROR, "error reading from pipe %s (%s)", lpipe->path.c_str(), uv_strerror(nread));
		}

		// Only this writer is done, we keep listening for others.
		// Our callback may have closed us already, which closes every writer.
		auto client = std::find(lpipe->clients.begin(), lpipe->clients.end(), (uv_pipe_t*) handle);
		if (client != lpipe->clients.end()) {
			lpipe->clients.erase(client);
			closeHandle((uv_pipe_t*) handle);
		}
	}

	::free(buf->base);
}

#else

void MumblePipe::closeHandles() {
	if (closed) return;
	closed = true;

	if (pipe) {
		closeHandle(pipe);
		pipe = nullptr;
	}
}

std::string MumblePipe::open(const char *filepath) {
	path = filepath;

	// Make the fifo file if it doesn't already exist
	if (mkfifo(filepath, 0666) != 0 && errno != EEXIST) {
		return std::string("failed to mkfifo pipe: ") + strerror(errno);
	}

	return openFifo();
}

std::string MumblePipe::openFifo() {
	int fd = ::open(path.c_str(), O_RDONLY | O_NONBLOCK);
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
	handle->data = this;

	error = uv_pipe_open(handle, fd);
	if (error != 0) {
		::close(fd);
		// The handle is registered with the loop now, so it has to be closed before it's freed
		closeHandle(handle);
		return std::string("failed to open pipe: ") + uv_strerror(error);
	}

	pipe = handle;
	return "";
}

void MumblePipe::reopen() {
	closeHandle(pipe);
	pipe = nullptr;

	std::string error = openFifo();
	if (error.empty()) {
		int status = uv_read_start((uv_stream_t*) pipe, MumblePipe::onAlloc, MumblePipe::onRead);
		if (status != 0) {
			error = std::string("failed to restart reading: ") + uv_strerror(status);
		}
	}

	if (!error.empty()) {
		mumble_log(LOG_ERROR, "failed to reopen pipe %s (%s)", path.c_str(), error.c_str());
		close();
		return;
	}

	mumble_log(LOG_TRACE, "%s: %p pipe successfully reopened for reading", METATABLE_PIPE, this);
}

void MumblePipe::onRead(uv_stream_t *handle, ssize_t nread, const uv_buf_t *buf) {
	MumblePipe *lpipe = (MumblePipe*) handle->data;

	if (nread > 0) {
		lpipe->dispatch(buf->base, nread);
	} else if (nread < 0 && !lpipe->closed) {
		if (nread == UV_EOF) {
			// The writer closed its end, so wait for the next one
			mumble_log(LOG_TRACE, "%s: %p end of pipe reached", METATABLE_PIPE, lpipe);
			lpipe->reopen();
		} else {
			mumble_log(LOG_ERROR, "error reading from pipe %s (%s)", lpipe->path.c_str(), uv_strerror(nread));
			lpipe->close();
		}
	}

	::free(buf->base);
}

#endif
