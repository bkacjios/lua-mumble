#include "mumble.h"

#include "thread.h"
#include "buffer.h"
#include "util.h"
#include "log.h"

#include <lualib.h>
#include <unistd.h>

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

void mumble_thread_worker_start(void *arg)
{
	MumbleThreadController *controller = arg;

	lua_State *l = luaL_newstate();

	lua_stackguard_entry(l);
	
	luaL_openlibs(l);

	// Open ourself in the new state, since we need the worker metatable
	mumble_init(l);

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
		lua_pop(l, 1); // Pop the error
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

	pthread_mutex_init(&worker->mutex, NULL);
	pthread_cond_init(&worker->cond, NULL);

	lua_pushvalue(l, -1);
	worker->self = mumble_registry_ref(l, MUMBLE_THREAD_REG);

	uv_loop_init(&worker->loop);

	worker->async_message.data = worker;
	uv_async_init(&worker->loop, &worker->async_message, mumble_thread_worker_message);

	pthread_mutex_lock(&controller->mutex);
	controller->worker = worker;
	controller->started = true; // Mark work as started
	pthread_cond_signal(&controller->cond); // Signal the waiting thread
	pthread_mutex_unlock(&controller->mutex);

	// Call the worker with our custom error handler function
	if (lua_pcall(l, 1, 0, -3) != 0) {
		mumble_log(LOG_ERROR, "%s", lua_tostring(l, -1));
		lua_pop(l, 1); // Pop the error
	}

	// Pop the error handler
	lua_pop(l, 1);

	uv_async_send(&controller->async_finish);

	pthread_mutex_lock(&worker->mutex);
	worker->finished = true; // Mark work as finished
	pthread_cond_signal(&worker->cond); // Signal the waiting thread
	pthread_mutex_unlock(&worker->mutex);

	mumble_registry_unref(l, MUMBLE_THREAD_REG, worker->self);

	lua_stackguard_exit(l);

	// Close state
	lua_close(l);
}

void mumble_thread_worker_finish(uv_async_t *handle)
{
	MumbleThreadController* controller = (MumbleThreadController*) handle->data;

	lua_State* l = controller->l;

	lua_stackguard_entry(l);

	// Check if we have a message callback reference
	if (controller->finish > MUMBLE_UNREFERENCED) {
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
		mumble_registry_unref(l, MUMBLE_THREAD_REG, controller->finish);
	}

	if (controller->self > MUMBLE_UNREFERENCED) {
		// Unreference ourself
		mumble_registry_unref(l, MUMBLE_THREAD_REG, controller->self);
	}

	lua_stackguard_exit(l);
}

void mumble_thread_controller_message(uv_async_t *handle)
{
	MumbleThreadController* controller = (MumbleThreadController*) handle->data;

	pthread_mutex_lock(&controller->mutex);

	lua_State* l = controller->l;

	if (controller->message > MUMBLE_UNREFERENCED) {

		// Push our error handler
		lua_pushcfunction(l, mumble_traceback);

		LinkQueue* queue = controller->message_queue;

		while (queue->front != NULL) {
			// Get the front message
			QueueNode *message = queue_pop(queue);

			// Push the callback function from the registry
			mumble_registry_pushref(l, MUMBLE_THREAD_REG, controller->message);

			// Push ourself
			mumble_registry_pushref(l, MUMBLE_THREAD_REG, controller->self);

			// Push our message
			lua_pushlstring(l, message->data, message->size);

			// Call the callback with our custom error handler function
			if (lua_pcall(l, 2, 0, -4) != 0) {
				mumble_log(LOG_ERROR, "%s", lua_tostring(l, -1));
				lua_pop(l, 1); // Pop the error
			}

			free(message->data);
			free(message);
		}

		// Pop the error handler
		lua_pop(l, 1);
	}

	pthread_mutex_unlock(&controller->mutex);
}

void mumble_thread_worker_message(uv_async_t *handle)
{
	MumbleThreadWorker* worker = (MumbleThreadWorker*) handle->data;	
	MumbleThreadController* controller = (MumbleThreadController*) worker->controller;

	pthread_mutex_lock(&worker->mutex);

	lua_State* l = worker->l;

	if (worker->message > MUMBLE_UNREFERENCED) {

		// Push our error handler
		lua_pushcfunction(l, mumble_traceback);

		LinkQueue* queue = worker->message_queue;

		while (queue->front != NULL) {
			// Get the front message
			QueueNode *message = queue_pop(queue);

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

			free(message); // Free the node
		}

		// Pop the error handler
		lua_pop(l, 1);
	}

	pthread_mutex_unlock(&worker->mutex);
}

int mumble_thread_new(lua_State *l)
{
	MumbleThreadController *controller = lua_newuserdata(l, sizeof(MumbleThreadController));
	controller->l = l;
	controller->self = MUMBLE_UNREFERENCED;
	controller->finish = MUMBLE_UNREFERENCED;
	controller->message = MUMBLE_UNREFERENCED;
	controller->filename = NULL;
	controller->bytecode = NULL;
	controller->bytecode_size = 0;
	controller->started = false;

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
			const char *msg = lua_pushfstring(l, "%s or %s expected, got %s",
				lua_typename(l, LUA_TSTRING), lua_typename(l, LUA_TFUNCTION), luaL_typename(l, 2));
			return luaL_argerror(l, 1, msg);
	}
	
	controller->message_queue = queue_new();

	pthread_mutex_init(&controller->mutex, NULL);
	pthread_cond_init(&controller->cond, NULL);

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

static int thread_controller_join(lua_State *l)
{
	MumbleThreadController *controller = luaL_checkudata(l, 1, METATABLE_THREAD_CONTROLLER);

	pthread_mutex_lock(&controller->mutex);
	while (!controller->started) {
		// Wait for worker to start
		pthread_cond_wait(&controller->cond, &controller->mutex);
	}
	MumbleThreadWorker *worker = controller->worker;
	pthread_mutex_unlock(&controller->mutex);

	pthread_mutex_lock(&worker->mutex);
	while (!worker->finished) {
		// Now wait for it to finish
		pthread_cond_wait(&worker->cond, &worker->mutex);
	}
	pthread_mutex_unlock(&worker->mutex);

	uv_thread_join(&controller->thread);

	lua_pushvalue(l, 1);
	return 1;
}

static int thread_controller_onFinish(lua_State *l)
{
	MumbleThreadController *controller = luaL_checkudata(l, 1, METATABLE_THREAD_CONTROLLER);

	if (controller->finish > MUMBLE_UNREFERENCED) {
		mumble_registry_unref(l, MUMBLE_THREAD_REG, controller->finish);
	}

	if (luaL_checkfunction(l, 2)) {
		lua_pushvalue(l, 2); // Push a copy of our callback function
		controller->finish = mumble_registry_ref(l, MUMBLE_THREAD_REG); // Pop it off as a reference
	}

	lua_pushvalue(l, 1);
	return 1;
}

static int thread_controller_send(lua_State *l)
{
	MumbleThreadController *controller = luaL_checkudata(l, 1, METATABLE_THREAD_CONTROLLER);

	pthread_mutex_lock(&controller->mutex);
	while (!controller->started) {
		// We can't send until the worker has started
		pthread_cond_wait(&controller->cond, &controller->mutex);
	}
	MumbleThreadWorker *worker = controller->worker;
	pthread_mutex_unlock(&controller->mutex);

	switch (lua_type(l, 2)) {
		case LUA_TSTRING:
			// Initialize with raw data
			size_t size;
			char* data = (char*) luaL_checklstring(l, 2, &size);
			pthread_mutex_lock(&worker->mutex);
			queue_push(worker->message_queue, strndup(data, size), size);
			pthread_mutex_unlock(&worker->mutex);
			break;
		case LUA_TUSERDATA:
			ByteBuffer *buffer = luaL_checkudata(l, 2, METATABLE_BUFFER);
			pthread_mutex_lock(&worker->mutex);
			size_t length = buffer_length(buffer);;
			queue_push(worker->message_queue, strndup(buffer->data + buffer->read_head, length), length);
			pthread_mutex_unlock(&worker->mutex);
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

static int thread_controller_onMessage(lua_State *l)
{
	MumbleThreadController *controller = luaL_checkudata(l, 1, METATABLE_THREAD_CONTROLLER);

	if (controller->message > MUMBLE_UNREFERENCED) {
		mumble_registry_unref(l, MUMBLE_THREAD_REG, controller->message);
	}

	if (luaL_checkfunction(l, 2)) {
		lua_pushvalue(l, 2); // Push a copy of our callback function
		controller->message = mumble_registry_ref(l, MUMBLE_THREAD_REG); // Pop it off as a reference
	}

	lua_pushvalue(l, 1);
	return 1;
}

static int thread_controller_tostring(lua_State *l)
{
	lua_pushfstring(l, "%s: %p", METATABLE_THREAD_CONTROLLER, lua_topointer(l, 1));
	return 1;
}

static int thread_controller_gc(lua_State *l)
{
	MumbleThreadController *controller = luaL_checkudata(l, 1, METATABLE_THREAD_CONTROLLER);
	if (controller->bytecode) {
		free(controller->bytecode);
	}
	if (uv_is_active((uv_handle_t*) &controller->async_finish)) {
		uv_close((uv_handle_t*) &controller->async_finish, NULL);
	}
	if (uv_is_active((uv_handle_t*) &controller->async_message)) {
		uv_close((uv_handle_t*) &controller->async_message, NULL);
	}
	pthread_mutex_destroy(&controller->mutex);
	pthread_cond_destroy(&controller->cond);
	if (controller->message_queue) {
		free(controller->message_queue);
	}
	mumble_log(LOG_DEBUG, "%s: %p garbage collected", METATABLE_THREAD_CONTROLLER, controller);
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

static int thread_worker_send(lua_State *l)
{
	MumbleThreadWorker *worker = luaL_checkudata(l, 1, METATABLE_THREAD_WORKER);
	MumbleThreadController *controller = worker->controller;

	switch (lua_type(l, 2)) {
		case LUA_TSTRING:
			// Initialize with raw data
			size_t size;
			char* data = (char*) luaL_checklstring(l, 2, &size);
			pthread_mutex_lock(&controller->mutex);
			queue_push(controller->message_queue, strndup(data, size), size);
			pthread_mutex_unlock(&controller->mutex);
			break;
		case LUA_TUSERDATA:
			ByteBuffer *buffer = luaL_checkudata(l, 2, METATABLE_BUFFER);
			pthread_mutex_lock(&controller->mutex);
			size_t length = buffer_length(buffer);
			queue_push(controller->message_queue, strndup(buffer->data + buffer->read_head, length), length);
			pthread_mutex_unlock(&controller->mutex);
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

static int thread_worker_onMessage(lua_State *l)
{
	MumbleThreadWorker *worker = luaL_checkudata(l, 1, METATABLE_THREAD_WORKER);

	if (worker->message > MUMBLE_UNREFERENCED) {
		mumble_registry_unref(l, MUMBLE_THREAD_REG, worker->message);
	}

	if (luaL_checkfunction(l, 2)) {
		lua_pushvalue(l, 2); // Push a copy of our callback function
		worker->message = mumble_registry_ref(l, MUMBLE_THREAD_REG); // Pop it off as a reference
	}

	lua_pushvalue(l, 1);
	return 1;
}

static int thread_worker_sleep(lua_State *l)
{
	MumbleThreadWorker *worker = luaL_checkudata(l, 1, METATABLE_THREAD_WORKER);
	usleep(luaL_checkinteger(l, 2) * 1000);
	lua_pushvalue(l, 1);
	return 1;
}

static int thread_worker_loop(lua_State *l)
{
	MumbleThreadWorker *worker = luaL_checkudata(l, 1, METATABLE_THREAD_WORKER);
	uv_run(&worker->loop, UV_RUN_DEFAULT);
	lua_pushvalue(l, 1);
	return 1;
}

static int thread_worker_stop(lua_State *l)
{
	MumbleThreadWorker *worker = luaL_checkudata(l, 1, METATABLE_THREAD_WORKER);
	uv_stop(&worker->loop);
	lua_pushvalue(l, 1);
	return 1;
}

static int thread_worker_buffer(lua_State *l)
{
	MumbleThreadWorker *worker = luaL_checkudata(l, 1, METATABLE_THREAD_WORKER);
	return mumble_buffer_new(l);
}

static int thread_worker_tostring(lua_State *l)
{
	lua_pushfstring(l, "%s: %p", METATABLE_THREAD_WORKER, lua_topointer(l, 1));
	return 1;
}

static int thread_worker_gc(lua_State *l)
{
	MumbleThreadWorker *worker = luaL_checkudata(l, 1, METATABLE_THREAD_WORKER);
	uv_loop_close(&worker->loop);
	if (uv_is_active((uv_handle_t*) &worker->async_message)) {
		uv_close((uv_handle_t*) &worker->async_message, NULL);
	}
	if (worker->message_queue) {
		free(worker->message_queue);
	}
	pthread_mutex_destroy(&worker->mutex);
	pthread_cond_destroy(&worker->cond);
	mumble_log(LOG_DEBUG, "%s: %p garbage collected", METATABLE_THREAD_WORKER, worker);
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