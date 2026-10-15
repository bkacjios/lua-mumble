#pragma once

#include "lua/luainc.h"

#include <string>
#include <uv.h>
#include <vector>

// Windows has no FIFO files, so there we listen on a named pipe that writers connect to instead
#if defined(_WIN32) && !defined(MUMBLE_NAMED_PIPE)
#define MUMBLE_NAMED_PIPE
#endif

class MumblePipe {
public:
	explicit MumblePipe(lua_State *l);
	~MumblePipe();

	// Stop reading and stop referencing ourself
	void close();

	// Get ready to read from path. Returns an error message on failure.
	// With named pipes this listens on \\.\pipe\<path> (unless path already is one), which any number of writers can connect to.
	// Otherwise this creates (if needed) and opens the FIFO file at path, reopening it whenever a writer closes it.
	std::string open(const char *path);
	// Start reading, calling the function at callback_index (a Lua stack index) with what we read.
	// The pipe at self_index is kept referenced until it's closed.
	void listen(int self_index, int callback_index);

	lua_State* l;
	std::string path;
	int self = LUA_NOREF;
	int callback = LUA_NOREF;

private:
	// Close every handle we have open. Their close callbacks only free the handles, so we can be gone by then.
	void closeHandles();
	static void closeHandle(uv_pipe_t *handle);
	// Call our Lua callback with what we read
	void dispatch(const char *data, size_t length);

	static void onAlloc(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf);
	static void onRead(uv_stream_t *handle, ssize_t nread, const uv_buf_t *buf);

	bool closed = false;

#ifdef MUMBLE_NAMED_PIPE
	static void onConnection(uv_stream_t *server, int status);

	uv_pipe_t* server = nullptr;
	// Every writer that's connected to us
	std::vector<uv_pipe_t*> clients;
#else
	// Open our FIFO file for reading. Returns an error message on failure.
	std::string openFifo();
	// A writer closed the FIFO, so open it again for the next one
	void reopen();

	uv_pipe_t* pipe = nullptr;
#endif
};
