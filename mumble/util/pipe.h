#pragma once

#include "lua/luainc.h"

#include <string>
#include <uv.h>

class MumblePipe {
public:
	explicit MumblePipe(lua_State *l);
	~MumblePipe();

	void close();

	// Create (if needed) and open a FIFO file for reading. Returns an error message on failure.
	std::string open(const char *path);
	// Start reading, calling the function at callback_index (a Lua stack index) with what we read.
	// The pipe at self_index is kept referenced until it's closed.
	void listen(int self_index, int callback_index);

	static void onAlloc(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf);
	static void onRead(uv_stream_t *handle, ssize_t nread, const uv_buf_t *buf);
	static void onReopen(uv_handle_t* handle);
	static void onClose(uv_handle_t* handle);

	uv_pipe_t* pipe = nullptr;
	lua_State* l;
	char* path = nullptr;
	int self = LUA_NOREF;
	int callback = LUA_NOREF;
};
