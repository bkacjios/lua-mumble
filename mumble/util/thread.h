#pragma once

#include "lua/luainc.h"
#include "util/util.h"

#include <uv.h>

class MumbleThreadController;

class MumbleThreadWorker {
public:
	MumbleThreadWorker(lua_State *l, MumbleThreadController *controller);
	~MumbleThreadWorker();

	// Queue a message for the controller
	void send(std::string message);
	// Call the function at a Lua stack index for every message from the controller
	void setMessageCallback(int index);
	// Run our event loop until stopped
	void run();
	void stop();

	static void onMessage(uv_async_t *handle);

	lua_State* l;
	uv_loop_t loop;
	MumbleThreadController* controller;
	uv_async_t async_message;
	// Guards message_queue
	uv_mutex_t mutex;
	MessageQueue message_queue;
	int message = LUA_NOREF;
	int self = LUA_NOREF;
};

class MumbleThreadController {
public:
	explicit MumbleThreadController(lua_State *l);
	~MumbleThreadController();

	// Start the worker thread, referencing the controller on top of the Lua stack (popping it) until it finishes
	void start();
	// Wait for the worker thread to exit
	void join();
	// Queue a message for the worker, returns false if the worker failed to start or has finished
	bool send(std::string message);
	// Call the function at a Lua stack index when the worker finishes, or for every message from the worker
	void setFinishCallback(int index);
	void setMessageCallback(int index);

	// Wait until the worker thread has started (or failed to start)
	void waitForWorker();

	static int onDump(lua_State *l, const void *buffer, size_t size, void *ud);
	static void onStart(void *arg);
	static void onFinish(uv_async_t *handle);
	static void onMessage(uv_async_t *handle);
	static void onClose(uv_handle_t *handle);

	lua_State* l;
	// The running worker, or nullptr before it starts and once it finishes. Guarded by mutex.
	MumbleThreadWorker* worker = nullptr;
	uv_thread_t thread = 0;
	uv_async_t async_finish;
	uv_async_t async_message;
	// Guards worker, started and message_queue
	uv_mutex_t mutex;
	uv_cond_t cond;
	bool started = false;
	// How many of our async handles are still open
	int open_handles = 0;
	char* bytecode = nullptr;
	const char* filename = nullptr;
	size_t bytecode_size = 0;
	int self = LUA_NOREF;
	int finish = LUA_NOREF;
	int message = LUA_NOREF;
	MessageQueue message_queue;
};
