#include "util/thread.h"

#include "client/client.h"
#include "lua/luainc.h"
#include "lua/metatables.h"
#include "lua/registry.h"
#include "module.h"
#include "util/log.h"
#include "util/util.h"

#include <cstdlib>
#include <cstring>

MumbleThreadController::MumbleThreadController(lua_State *l) : l(l) {
}

// Replace the callback referenced by ref with the function at a Lua stack index
static void thread_set_callback(lua_State *l, int *ref, int index) {
	if (*ref > LUA_REFNIL) {
		LuaRegistry::unrefIn(l, LuaRegistry::threads, ref);
	}
	lua_pushvalue(l, index);
	*ref = LuaRegistry::refIn(l, LuaRegistry::threads);
}

void MumbleThreadController::setFinishCallback(int index) {
	thread_set_callback(l, &finish, index);
}

void MumbleThreadController::setMessageCallback(int index) {
	thread_set_callback(l, &message, index);
}

void MumbleThreadWorker::setMessageCallback(int index) {
	thread_set_callback(l, &message, index);
}

void MumbleThreadWorker::run() {
	uv_run(&loop, UV_RUN_DEFAULT);
}

void MumbleThreadWorker::stop() {
	uv_stop(&loop);
}

#if UV_VERSION_MAJOR < 1 || (UV_VERSION_MAJOR == 1 && UV_VERSION_MINOR < 50)
// This was added in libuv 1.50
#ifdef _WIN32
#include <windows.h>
int uv_thread_detach(uv_thread_t *tid) {
	return CloseHandle(*tid) ? 0 : -1;
}
#else
#include <pthread.h>
int uv_thread_detach(uv_thread_t *tid) {
	return pthread_detach(*tid);
}
#endif

#endif

MumbleThreadController::~MumbleThreadController() {
	// Our handles are normally closed once the worker finishes, but we can still be collected
	// before then when the Lua state is closed
	if (!uv_is_closing((uv_handle_t*)&async_finish)) {
		uv_close((uv_handle_t*)&async_finish, NULL);
	}
	if (!uv_is_closing((uv_handle_t*)&async_message)) {
		uv_close((uv_handle_t*)&async_message, NULL);
	}

	if (thread) {
		uv_thread_detach(&thread);
	}

	uv_mutex_destroy(&mutex);
	uv_cond_destroy(&cond);

	if (bytecode) {
		free(bytecode);
		bytecode = nullptr;
	}
}

void MumbleThreadController::waitForWorker() {
	uv_mutex_lock(&mutex);
	while (!started) {
		uv_cond_wait(&cond, &mutex);
	}
	uv_mutex_unlock(&mutex);
}

MumbleThreadWorker::MumbleThreadWorker(lua_State *l, MumbleThreadController *controller) : l(l), controller(controller) {
}

MumbleThreadWorker::~MumbleThreadWorker() {
	uv_stop(&loop);
	if (!uv_is_closing((uv_handle_t*)&async_message)) {
		uv_close((uv_handle_t*) &async_message, NULL);
	}
	while (uv_loop_close(&loop) == UV_EBUSY) {
		uv_run(&loop, UV_RUN_NOWAIT);
	}
	uv_mutex_destroy(&mutex);
}

void MumbleThreadWorker::send(std::string message) {
	uv_mutex_lock(&controller->mutex);
	controller->message_queue.push(std::move(message));
	uv_mutex_unlock(&controller->mutex);
	uv_async_send(&controller->async_message);
}

void MumbleThreadController::start() {
	uv_mutex_init(&mutex);
	uv_cond_init(&cond);

	// Reference ourself to prevent garbage collection
	self = LuaRegistry::refIn(l, LuaRegistry::threads);

	open_handles = 2;

	async_finish.data = this;
	uv_async_init(uv_default_loop(), &async_finish, MumbleThreadController::onFinish);

	async_message.data = this;
	uv_async_init(uv_default_loop(), &async_message, MumbleThreadController::onMessage);

	uv_thread_create(&thread, MumbleThreadController::onStart, this);
}

void MumbleThreadController::join() {
	waitForWorker();

	// Don't touch worker here; it may already be destroyed by lua_close.
	if (thread) {
		uv_thread_join(&thread);
		thread = 0;
	}
}

bool MumbleThreadController::send(std::string message) {
	// We can't send until the worker has started
	waitForWorker();

	// Hold our lock the whole time, so the worker can't finish and be destroyed while we use it
	uv_mutex_lock(&mutex);
	MumbleThreadWorker *target = worker;
	if (target) {
		uv_mutex_lock(&target->mutex);
		target->message_queue.push(std::move(message));
		uv_mutex_unlock(&target->mutex);
		uv_async_send(&target->async_message);
	}
	uv_mutex_unlock(&mutex);
	return target != nullptr;
}

// Callback function to handle dumping bytecode into memory
int MumbleThreadController::onDump(lua_State *l, const void *buffer, size_t size, void *ud) {
	MumbleThreadController* controller = (MumbleThreadController*) ud;

	// Reallocate memory to accommodate the additional bytecode data
	void *bytecode = realloc(controller->bytecode, controller->bytecode_size + size);
	if (bytecode == NULL) {
		return 1;
	}

	// Copy the new bytecode chunk into the allocated memory
	memcpy((char*)bytecode + controller->bytecode_size, buffer, size);

	// Update the bytecode pointer and size
	controller->bytecode = (char*) bytecode;
	controller->bytecode_size += size;
	return 0;
}

void MumbleThreadController::onStart(void *arg) {
	MumbleThreadController *controller = (MumbleThreadController*) arg;

	lua_State *l = luaL_newstate();

	luaL_openlibs(l);

	// Open ourself in the new state, since we need the worker metatable
	// and make it available to require("mumble") and as _G.mumble
	#if defined(LUA_VERSION_NUM) && (LUA_VERSION_NUM >= 502) /* Lua 5.2+ or LuaJIT with compat */
		luaL_requiref(l, "mumble", luaopen_mumble_worker, 1); // pushes module, sets package.loaded["mumble"], sets _G.mumble
		lua_pop(l, 1); // pop module
	#else
		// Lua 5.1 fallback: call opener and manually set package.loaded["mumble"] and _G.mumble
		lua_pushcfunction(l, luaopen_mumble_worker);
		lua_pushstring(l, "mumble");
		lua_call(l, 1, 1);                      // -> module table
		lua_setglobal(l, "mumble");              // _G.mumble = module

		lua_getglobal(l, "package");
		lua_getfield(l, -1, "loaded");           // package.loaded
		lua_getglobal(l, "mumble");              // module
		lua_setfield(l, -2, "mumble");           // package.loaded["mumble"] = module
		lua_pop(l, 2);                           // pop package.loaded, package
	#endif

	lua_stackguard_entry(l);

	// Push our error handler
	lua_pushcfunction(l, mumble_traceback);

	int err;

	if (controller->bytecode) {
		err = luaL_loadbuffer(l, controller->bytecode, controller->bytecode_size, "thread");
	} else {
		err = luaL_loadfile(l, controller->filename);
	}

	if (err != 0) {
		mumble_log(LOG_ERROR, "%s", lua_tostring(l, -1));
		lua_pop(l, 2); // Pop the error and traceback function

		// Unblock controller waiters so they don't deadlock
		uv_mutex_lock(&controller->mutex);
		controller->worker = NULL;       // make it explicit
		controller->started = true;      // signal "we're done starting" (even if failed)
		uv_cond_signal(&controller->cond);
		uv_mutex_unlock(&controller->mutex);

		// notify finish callback
		uv_async_send(&controller->async_finish);

		lua_stackguard_exit(l);

		// Close the Lua state for this worker thread and exit
		lua_close(l);
		return;
	}

	// Create a worker thread object and pass it to our compiled bytecode
	MumbleThreadWorker *worker = lua_newclass<MumbleThreadWorker>(l, l, controller);

	luaL_getmetatable(l, METATABLE_THREAD_WORKER);
	lua_setmetatable(l, -2);

	uv_mutex_init(&worker->mutex);

	lua_pushvalue(l, -1);
	worker->self = LuaRegistry::refIn(l, LuaRegistry::threads);

	uv_loop_init(&worker->loop);

	worker->async_message.data = worker;
	uv_async_init(&worker->loop, &worker->async_message, MumbleThreadWorker::onMessage);

	uv_mutex_lock(&controller->mutex);
	controller->worker = worker;
	controller->started = true; // Mark work as started
	uv_cond_signal(&controller->cond); // Signal the waiting thread
	uv_mutex_unlock(&controller->mutex);

	// Call the worker with our custom error handler function
	if (lua_pcall(l, 1, 0, -3) != 0) {
		mumble_log(LOG_ERROR, "%s: %s", METATABLE_THREAD_WORKER, lua_tostring(l, -1));
		lua_pop(l, 1); // Pop the error
	}

	// Pop the error handler
	lua_pop(l, 1);

	// Stop the controller sending to us, since lua_close is about to destroy us
	uv_mutex_lock(&controller->mutex);
	controller->worker = NULL;
	uv_mutex_unlock(&controller->mutex);

	// The controller may be collected once this is handled, so it's the last time we touch it
	uv_async_send(&controller->async_finish);

	LuaRegistry::unrefIn(l, LuaRegistry::threads, &worker->self);

	lua_stackguard_exit(l);

	// Close state
	lua_close(l);
}

void MumbleThreadController::onFinish(uv_async_t *handle) {
	MumbleThreadController* controller = (MumbleThreadController*) handle->data;

	lua_State* l = controller->l;

	// Deliver anything the worker sent before it finished, since libuv may run this callback first
	MumbleThreadController::onMessage(&controller->async_message);

	lua_stackguard_entry(l);

	// Check if we have a message callback reference
	if (controller->finish > LUA_REFNIL) {
		// Push our error handler
		lua_pushcfunction(l, mumble_traceback);

		// Push the callback function from the registry
		LuaRegistry::pushFrom(l, LuaRegistry::threads, controller->finish);
		LuaRegistry::pushFrom(l, LuaRegistry::threads, controller->self);

		// Call the callback with our custom error handler function
		if (lua_pcall(l, 1, 0, -3) != 0) {
			mumble_log(LOG_ERROR, "%s", lua_tostring(l, -1));
			lua_pop(l, 1); // Pop the error
		}

		// Pop the error handler
		lua_pop(l, 1);

		// Unreference our finished callback
		LuaRegistry::unrefIn(l, LuaRegistry::threads, &controller->finish);
	}

	// Our handles live in our userdata, so keep referencing ourself until they're closed
	if (!uv_is_closing((uv_handle_t*)&controller->async_finish)) {
		uv_close((uv_handle_t*)&controller->async_message, MumbleThreadController::onClose);
		uv_close((uv_handle_t*)&controller->async_finish, MumbleThreadController::onClose);
	}

	lua_stackguard_exit(l);
}

void MumbleThreadController::onClose(uv_handle_t *handle) {
	MumbleThreadController* controller = (MumbleThreadController*) handle->data;

	if (--controller->open_handles == 0 && controller->self > LUA_REFNIL) {
		// Unreference ourself
		LuaRegistry::unrefIn(controller->l, LuaRegistry::threads, &controller->self);
	}
}

void MumbleThreadController::onMessage(uv_async_t *handle) {
	MumbleThreadController* controller = (MumbleThreadController*) handle->data;
	lua_State* l = controller->l;

	if (controller->message > LUA_REFNIL) {
		// Push our error handler
		lua_pushcfunction(l, mumble_traceback);

		uv_mutex_lock(&controller->mutex);
		MessageQueue* queue = &controller->message_queue;

		while (controller->message > LUA_REFNIL && !queue->empty()) {
			// Pop one message under the lock, then unlock before calling into Lua
			std::string message = queue->pop();
			uv_mutex_unlock(&controller->mutex);

			// Push the callback function from the registry
			LuaRegistry::pushFrom(l, LuaRegistry::threads, controller->message);

			// Push ourself
			LuaRegistry::pushFrom(l, LuaRegistry::threads, controller->self);

			// Push our message
			lua_pushlstring(l, message.data(), message.size());

			// Call the callback with our custom error handler function
			if (lua_pcall(l, 2, 0, -4) != 0) {
				mumble_log(LOG_ERROR, "%s: %s", METATABLE_THREAD_CONTROLLER, lua_tostring(l, -1));
				lua_pop(l, 1); // Pop the error
			}

			// Re‑acquire lock and loop
			uv_mutex_lock(&controller->mutex);
		}

		uv_mutex_unlock(&controller->mutex);

		// Pop the error handler
		lua_pop(l, 1);
	}
}

void MumbleThreadWorker::onMessage(uv_async_t *handle) {
	MumbleThreadWorker* worker = (MumbleThreadWorker*) handle->data;
	lua_State* l = worker->l;

	if (worker->message > LUA_REFNIL) {
		// Push our error handler
		lua_pushcfunction(l, mumble_traceback);

		uv_mutex_lock(&worker->mutex);
		MessageQueue* queue = &worker->message_queue;

		while (worker->message > LUA_REFNIL && !queue->empty()) {
			// Pop one message under the lock, then unlock before calling into Lua
			std::string message = queue->pop();
			uv_mutex_unlock(&worker->mutex);

			// Push the callback function from the registry
			LuaRegistry::pushFrom(l, LuaRegistry::threads, worker->message);

			// Push ourself
			LuaRegistry::pushFrom(l, LuaRegistry::threads, worker->self);

			// Push our message
			lua_pushlstring(l, message.data(), message.size());

			// Call the callback with our custom error handler function
			if (lua_pcall(l, 2, 0, -4) != 0) {
				mumble_log(LOG_ERROR, "%s: %s", METATABLE_THREAD_WORKER, lua_tostring(l, -1));
				lua_pop(l, 1); // Pop the error
			}

			// Re‑acquire lock and loop
			uv_mutex_lock(&worker->mutex);
		}

		uv_mutex_unlock(&worker->mutex);

		// Pop the error handler
		lua_pop(l, 1);
	}
}
