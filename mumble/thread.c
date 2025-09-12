#include "mumble.h"

#include "thread.h"
#include "buffer.h"
#include "util.h"
#include "log.h"

#include <lualib.h>
#include <unistd.h>
#include <pthread.h>

void mumble_thread_worker_message(uv_async_t *handle);

// Callback function to handle dumping bytecode into memory
static int mumble_dump_controller(lua_State *l, const void *buffer, size_t size, void *ud) {
	MumbleThreadController* controller = (MumbleThreadController*) ud;

	// Reallocate memory to accommodate the additional bytecode data
	void *bytecode = realloc(controller->bytecode, controller->bytecode_size + size);
	if (bytecode == NULL) {
		return 1;
	}

	// Copy the new bytecode chunk into the allocated memory
	memcpy((char*)bytecode + controller->bytecode_size, buffer, size);

	// Update the bytecode pointer and size
	controller->bytecode = bytecode;
	controller->bytecode_size += size;
	return 0;
}

void mumble_thread_worker_start(void *arg) {
	MumbleThreadController *controller = arg;

	lua_State *l = luaL_newstate();

	luaL_openlibs(l);

	// Open ourself in the new state, since we need the worker metatable
	// and make it available to require("mumble") and as _G.mumble
	#if defined(LUA_VERSION_NUM) && (LUA_VERSION_NUM >= 502) /* Lua 5.2+ or LuaJIT with compat */
		luaL_requiref(l, "mumble", luaopen_mumble, 1); // pushes module, sets package.loaded["mumble"], sets _G.mumble
		lua_pop(l, 1); // pop module
	#else
		// Lua 5.1 fallback: call opener and manually set package.loaded["mumble"] and _G.mumble
		lua_pushcfunction(l, luaopen_mumble);
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
	MumbleThreadWorker *worker = lua_newuserdata(l, sizeof(MumbleThreadWorker));
	worker->l = l;
	worker->controller = controller;
	worker->finished = false;
	worker->message_queue = queue_new();

	luaL_getmetatable(l, METATABLE_THREAD_WORKER);
	lua_setmetatable(l, -2);

	uv_mutex_init(&worker->mutex);
	uv_cond_init(&worker->cond);

	lua_pushvalue(l, -1);
	worker->self = mumble_registry_ref(l, MUMBLE_THREAD_REG);

	uv_loop_init(&worker->loop);

	worker->async_message.data = worker;
	uv_async_init(&worker->loop, &worker->async_message, mumble_thread_worker_message);

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

	uv_async_send(&controller->async_finish);

	uv_mutex_lock(&worker->mutex);
	worker->finished = true; // Mark work as finished
	uv_cond_signal(&worker->cond); // Signal the waiting thread
	uv_mutex_unlock(&worker->mutex);

	mumble_registry_unref(l, MUMBLE_THREAD_REG, &worker->self);

	lua_stackguard_exit(l);

	// Close state
	lua_close(l);
}

void mumble_thread_worker_finish(uv_async_t *handle) {
	MumbleThreadController* controller = (MumbleThreadController*) handle->data;

	lua_State* l = controller->l;

	lua_stackguard_entry(l);

	// Check if we have a message callback reference
	if (controller->finish > LUA_REFNIL) {
		// Push our error handler
		lua_pushcfunction(l, mumble_traceback);

		// Push the callback function from the registry
		mumble_registry_pushref(l, MUMBLE_THREAD_REG, controller->finish);
		mumble_registry_pushref(l, MUMBLE_THREAD_REG, controller->self);

		// Call the callback with our custom error handler function
		if (lua_pcall(l, 1, 0, -3) != 0) {
			mumble_log(LOG_ERROR, "%s", lua_tostring(l, -1));
			lua_pop(l, 1); // Pop the error
		}

		// Pop the error handler
		lua_pop(l, 1);

		// Unreference our finished callback
		mumble_registry_unref(l, MUMBLE_THREAD_REG, &controller->finish);
	}

	if (controller->self > LUA_REFNIL) {
		// Unreference ourself
		mumble_registry_unref(l, MUMBLE_THREAD_REG, &controller->self);
	}

	lua_stackguard_exit(l);
}

void mumble_thread_controller_message(uv_async_t *handle) {
	MumbleThreadController* controller = (MumbleThreadController*) handle->data;
	lua_State* l = controller->l;

	if (controller->message > LUA_REFNIL) {
		// Push our error handler
		lua_pushcfunction(l, mumble_traceback);

		uv_mutex_lock(&controller->mutex);
		LinkQueue* queue = controller->message_queue;

		while (controller->message > LUA_REFNIL && queue->front != NULL) {
			// Pop one message under the lock, then unlock before calling into Lua
			QueueNode *message = queue_pop(queue);
			uv_mutex_unlock(&controller->mutex);

			// Push the callback function from the registry
			mumble_registry_pushref(l, MUMBLE_THREAD_REG, controller->message);

			// Push ourself
			mumble_registry_pushref(l, MUMBLE_THREAD_REG, controller->self);

			// Push our message
			lua_pushlstring(l, message->data, message->size);

			// Call the callback with our custom error handler function
			if (lua_pcall(l, 2, 0, -4) != 0) {
				mumble_log(LOG_ERROR, "%s: %s", METATABLE_THREAD_CONTROLLER, lua_tostring(l, -1));
				lua_pop(l, 1); // Pop the error
			}

			if (message->data) free(message->data);
			free(message);

			// Re‑acquire lock and loop
			uv_mutex_lock(&controller->mutex);
		}

		uv_mutex_unlock(&controller->mutex);

		// Pop the error handler
		lua_pop(l, 1);
	}
}

void mumble_thread_worker_message(uv_async_t *handle) {
	MumbleThreadWorker* worker = (MumbleThreadWorker*) handle->data;
	lua_State* l = worker->l;

	if (worker->message > LUA_REFNIL) {
		// Push our error handler
		lua_pushcfunction(l, mumble_traceback);

		uv_mutex_lock(&worker->mutex);
		LinkQueue* queue = worker->message_queue;

		while (worker->message > LUA_REFNIL && queue->front != NULL) {
			// Pop one message under the lock, then unlock before calling into Lua
			QueueNode *message = queue_pop(queue);
			uv_mutex_unlock(&worker->mutex);

			// Push the callback function from the registry
			mumble_registry_pushref(l, MUMBLE_THREAD_REG, worker->message);

			// Push ourself
			mumble_registry_pushref(l, MUMBLE_THREAD_REG, worker->self);

			// Push our message
			lua_pushlstring(l, message->data, message->size);

			// Call the callback with our custom error handler function
			if (lua_pcall(l, 2, 0, -4) != 0) {
				mumble_log(LOG_ERROR, "%s: %s", METATABLE_THREAD_WORKER, lua_tostring(l, -1));
				lua_pop(l, 1); // Pop the error
			}

			if (message->data) free(message->data);
			free(message);

			// Re‑acquire lock and loop
			uv_mutex_lock(&worker->mutex);
		}

		uv_mutex_unlock(&worker->mutex);

		// Pop the error handler
		lua_pop(l, 1);
	}
}

int mumble_thread_new(lua_State *l) {
	MumbleThreadController *controller = lua_newuserdata(l, sizeof(MumbleThreadController));
	controller->l = l;
	controller->self = LUA_NOREF;
	controller->finish = LUA_NOREF;
	controller->message = LUA_NOREF;
	controller->filename = NULL;
	controller->bytecode = NULL;
	controller->bytecode_size = 0;
	controller->started = false;

	const char *msg = NULL;

	switch (lua_type(l, 2)) {
	case LUA_TSTRING:
		controller->filename = lua_tostring(l, 2);
		break;
	case LUA_TFUNCTION:
		// Convert our worker function to bytecode, so we can use it in a new state
		lua_pushvalue(l, 2); // Push a copy of our worker function
		if (lua_dump(l, mumble_dump_controller, controller) != 0) {
			return luaL_error(l, "unable to convert worker function into bytecode");
		}
		lua_pop(l, 1); // Pop our worker function copy
		break;
	default:
		msg = lua_pushfstring(l, "%s or %s expected, got %s",
		                      lua_typename(l, LUA_TSTRING), lua_typename(l, LUA_TFUNCTION), luaL_typename(l, 2));
		return luaL_argerror(l, 1, msg);
	}

	controller->message_queue = queue_new();

	uv_mutex_init(&controller->mutex);
	uv_cond_init(&controller->cond);

	lua_pushvalue(l, -1); // Reference a copy of our userdata to prevent garbage collection
	controller->self = mumble_registry_ref(l, MUMBLE_THREAD_REG); // Pop it off as a reference

	controller->async_finish.data = controller;
	uv_async_init(uv_default_loop(), &controller->async_finish, mumble_thread_worker_finish);

	controller->async_message.data = controller;
	uv_async_init(uv_default_loop(), &controller->async_message, mumble_thread_controller_message);

	uv_thread_create(&controller->thread, mumble_thread_worker_start, controller);

	luaL_getmetatable(l, METATABLE_THREAD_CONTROLLER);
	lua_setmetatable(l, -2);
	return 1;
}

static int thread_controller_join(lua_State *l) {
	MumbleThreadController *controller = luaL_checkudata(l, 1, METATABLE_THREAD_CONTROLLER);

	uv_mutex_lock(&controller->mutex);
	while (!controller->started) {
		uv_cond_wait(&controller->cond, &controller->mutex);
	}
	uv_mutex_unlock(&controller->mutex);

	// Don't touch controller->worker here; it may already be destroyed by lua_close.
	uv_thread_join(&controller->thread);

	lua_pushvalue(l, 1);
	return 1;
}

static int thread_controller_onFinish(lua_State *l) {
	MumbleThreadController *controller = luaL_checkudata(l, 1, METATABLE_THREAD_CONTROLLER);

	if (controller->finish > LUA_REFNIL) {
		mumble_registry_unref(l, MUMBLE_THREAD_REG, &controller->finish);
	}

	if (luaL_checkfunction(l, 2)) {
		lua_pushvalue(l, 2); // Push a copy of our callback function
		controller->finish = mumble_registry_ref(l, MUMBLE_THREAD_REG); // Pop it off as a reference
	}

	lua_pushvalue(l, 1);
	return 1;
}

static int thread_controller_send(lua_State *l) {
	MumbleThreadController *controller = luaL_checkudata(l, 1, METATABLE_THREAD_CONTROLLER);

	uv_mutex_lock(&controller->mutex);
	while (!controller->started) {
		// We can't send until the worker has started
		uv_cond_wait(&controller->cond, &controller->mutex);
	}
	MumbleThreadWorker *worker = controller->worker;
	uv_mutex_unlock(&controller->mutex);

	if (!worker) {
		return luaL_error(l, "thread worker failed to start");
	}

	switch (lua_type(l, 2)) {
	case LUA_TSTRING:
		// Initialize with raw data
		size_t size;
		char* data = (char*) luaL_checklstring(l, 2, &size);
		uv_mutex_lock(&worker->mutex);
		queue_push(worker->message_queue, strndup(data, size), size);
		uv_mutex_unlock(&worker->mutex);
		break;
	case LUA_TUSERDATA:
		ByteBuffer *buffer = luaL_checkudata(l, 2, METATABLE_BUFFER);
		uv_mutex_lock(&worker->mutex);
		size_t length = buffer_length(buffer);
		queue_push(worker->message_queue, strndup((char*) buffer->data + buffer->read_head, length), length);
		uv_mutex_unlock(&worker->mutex);
		break;
	default:
		const char *msg = lua_pushfstring(l, "%s or %s expected, got %s",
		                                  lua_typename(l, LUA_TSTRING), METATABLE_BUFFER, luaL_typename(l, 2));
		return luaL_argerror(l, 1, msg);
	}

	uv_async_send(&worker->async_message);

	lua_pushvalue(l, 1);
	return 1;
}

static int thread_controller_onMessage(lua_State *l) {
	MumbleThreadController *controller = luaL_checkudata(l, 1, METATABLE_THREAD_CONTROLLER);

	if (controller->message > LUA_REFNIL) {
		mumble_registry_unref(l, MUMBLE_THREAD_REG, &controller->message);
	}

	if (luaL_checkfunction(l, 2)) {
		lua_pushvalue(l, 2); // Push a copy of our callback function
		controller->message = mumble_registry_ref(l, MUMBLE_THREAD_REG); // Pop it off as a reference
	}

	lua_pushvalue(l, 1);
	return 1;
}

static int thread_controller_tostring(lua_State *l) {
	lua_pushfstring(l, "%s: %p", METATABLE_THREAD_CONTROLLER, lua_topointer(l, 1));
	return 1;
}

#if UV_VERSION_MAJOR < 1 || (UV_VERSION_MAJOR == 1 && UV_VERSION_MINOR < 50)
// This was added in libuv 1.50
int uv_thread_detach(uv_thread_t *tid) {
	return pthread_detach(*tid);
}
#endif

static int thread_controller_gc(lua_State *l) {
	MumbleThreadController *controller = luaL_checkudata(l, 1, METATABLE_THREAD_CONTROLLER);
	mumble_log(LOG_DEBUG, "%s: %p garbage collected", METATABLE_THREAD_CONTROLLER, controller);

	if (!uv_is_closing((uv_handle_t*)&controller->async_finish)) {
		uv_close((uv_handle_t*)&controller->async_finish, NULL);
	}
	if (!uv_is_closing((uv_handle_t*)&controller->async_message)) {
		uv_close((uv_handle_t*)&controller->async_message, NULL);
	}

	if (controller->thread) {
		uv_thread_detach(&controller->thread);
	}

	if (controller->bytecode) {
		free(controller->bytecode);
		controller->bytecode = NULL;
	}

	queue_free(&controller->message_queue);
	uv_mutex_destroy(&controller->mutex);
	uv_cond_destroy(&controller->cond);
	return 0;
}

const luaL_Reg mumble_thread_controller[] = {
	{"join", thread_controller_join},
	{"onFinish", thread_controller_onFinish},
	{"send", thread_controller_send},
	{"onMessage", thread_controller_onMessage},
	{"__tostring", thread_controller_tostring},
	{"__gc", thread_controller_gc},
	{NULL, NULL}
};

static int thread_worker_send(lua_State *l) {
	MumbleThreadWorker *worker = luaL_checkudata(l, 1, METATABLE_THREAD_WORKER);
	MumbleThreadController *controller = worker->controller;

	switch (lua_type(l, 2)) {
	case LUA_TSTRING:
		// Initialize with raw data
		size_t size;
		char* data = (char*) luaL_checklstring(l, 2, &size);
		uv_mutex_lock(&controller->mutex);
		queue_push(controller->message_queue, strndup(data, size), size);
		uv_mutex_unlock(&controller->mutex);
		break;
	case LUA_TUSERDATA:
		ByteBuffer *buffer = luaL_checkudata(l, 2, METATABLE_BUFFER);
		uv_mutex_lock(&controller->mutex);
		size_t length = buffer_length(buffer);
		queue_push(controller->message_queue, strndup((char*) buffer->data + buffer->read_head, length), length);
		uv_mutex_unlock(&controller->mutex);
		break;
	default:
		const char *msg = lua_pushfstring(l, "%s or %s expected, got %s",
		                                  lua_typename(l, LUA_TSTRING), METATABLE_BUFFER, luaL_typename(l, 2));
		return luaL_argerror(l, 1, msg);
	}

	uv_async_send(&controller->async_message);

	lua_pushvalue(l, 1);
	return 1;
}

static int thread_worker_onMessage(lua_State *l) {
	MumbleThreadWorker *worker = luaL_checkudata(l, 1, METATABLE_THREAD_WORKER);

	if (worker->message > LUA_REFNIL) {
		mumble_registry_unref(l, MUMBLE_THREAD_REG, &worker->message);
	}

	if (luaL_checkfunction(l, 2)) {
		lua_pushvalue(l, 2); // Push a copy of our callback function
		worker->message = mumble_registry_ref(l, MUMBLE_THREAD_REG); // Pop it off as a reference
	}

	lua_pushvalue(l, 1);
	return 1;
}

static int thread_worker_sleep(lua_State *l) {
	luaL_checkudata(l, 1, METATABLE_THREAD_WORKER);
	usleep(luaL_checkinteger(l, 2) * 1000);
	lua_pushvalue(l, 1);
	return 1;
}

static int thread_worker_loop(lua_State *l) {
	MumbleThreadWorker *worker = luaL_checkudata(l, 1, METATABLE_THREAD_WORKER);
	uv_run(&worker->loop, UV_RUN_DEFAULT);
	lua_pushvalue(l, 1);
	return 1;
}

static int thread_worker_stop(lua_State *l) {
	MumbleThreadWorker *worker = luaL_checkudata(l, 1, METATABLE_THREAD_WORKER);
	uv_stop(&worker->loop);
	lua_pushvalue(l, 1);
	return 1;
}

static int thread_worker_buffer(lua_State *l) {
	luaL_checkudata(l, 1, METATABLE_THREAD_WORKER);
	return mumble_buffer_new(l);
}

static int thread_worker_tostring(lua_State *l) {
	lua_pushfstring(l, "%s: %p", METATABLE_THREAD_WORKER, lua_topointer(l, 1));
	return 1;
}

static int thread_worker_gc(lua_State *l) {
	MumbleThreadWorker *worker = luaL_checkudata(l, 1, METATABLE_THREAD_WORKER);
	mumble_log(LOG_DEBUG, "%s: %p garbage collected", METATABLE_THREAD_WORKER, worker);
	uv_stop(&worker->loop);
	if (!uv_is_closing((uv_handle_t*)&worker->async_message)) {
		uv_close((uv_handle_t*) &worker->async_message, NULL);
	}
	while (uv_loop_close(&worker->loop) == UV_EBUSY) {
		uv_run(&worker->loop, UV_RUN_NOWAIT);
	}
	queue_free(&worker->message_queue);
	uv_mutex_destroy(&worker->mutex);
	uv_cond_destroy(&worker->cond);
	return 0;
}

const luaL_Reg mumble_thread_worker[] = {
	{"send", thread_worker_send},
	{"onMessage", thread_worker_onMessage},
	{"sleep", thread_worker_sleep},
	{"loop", thread_worker_loop},
	{"stop", thread_worker_stop},
	{"buffer", thread_worker_buffer},
	{"__tostring", thread_worker_tostring},
	{"__gc", thread_worker_gc},
	{NULL, NULL}
};