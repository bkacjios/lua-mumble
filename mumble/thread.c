#include "mumble.h"

#include "thread.h"

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

void mumble_thread_worker_message(uv_async_t *handle)
{
	MumbleThreadWorker* worker = (MumbleThreadWorker*) handle->data;

	lua_State* l = worker->l;

	lua_stackguard_entry(l);

	if (worker->message > MUMBLE_UNREFERENCED) {
		// Push our error handler
		lua_pushcfunction(l, mumble_traceback);

		// Push the callback function from the registry
		mumble_registry_pushref(l, MUMBLE_THREAD_REG, worker->message);

		// Call the callback with our custom error handler function
		if (lua_pcall(l, 0, 0, -3) != LUA_OK) {
			mumble_log(LOG_ERROR, "%s: %s\n", METATABLE_THREAD_CONTROLLER, lua_tostring(l, -1));
			lua_pop(l, 1); // Pop the error
		}

		// Pop the error handler
		lua_pop(l, 1);
	}

	lua_stackguard_exit(l);
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

	// Load the bytecode in our thread
	int err = luaL_loadbuffer(l, controller->bytecode, controller->bytecode_size, "thread");

	if (err != LUA_OK) {
		mumble_log(LOG_ERROR, "%s\n", lua_tostring(l, -1));
		lua_pop(l, 1); // Pop the error
		return;
	}

	// Create a worker thread object and pass it to our compiled bytecode
	MumbleThreadWorker *worker = lua_newuserdata(l, sizeof(MumbleThreadWorker));
	worker->l = l;
	worker->controller = controller;
	luaL_getmetatable(l, METATABLE_THREAD_WORKER);
	lua_setmetatable(l, -2);

	// Call the worker with our custom error handler function
	if (lua_pcall(l, 1, 0, -3) != LUA_OK) {
		mumble_log(LOG_ERROR, "%s\n", lua_tostring(l, -1));
		lua_pop(l, 1); // Pop the error
	}

	// Pop the error handler
	lua_pop(l, 1);

	lua_stackguard_exit(l);

	// Close state
	lua_close(l);

	// Tell our main thread this worker finished
	uv_async_send(&controller->async_finished);
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
		if (lua_pcall(l, 1, 0, -3) != LUA_OK) {
			mumble_log(LOG_ERROR, "%s: %s\n", METATABLE_THREAD_CONTROLLER, lua_tostring(l, -1));
			lua_pop(l, 1); // Pop the error
		}

		// Pop the error handler
		lua_pop(l, 1);

		// Unreference our finished callback
		mumble_registry_unref(l, MUMBLE_THREAD_REG, controller->finish);
	}

	if (controller->message > MUMBLE_UNREFERENCED) {
		// Unreference our message callback
		mumble_registry_unref(l, MUMBLE_THREAD_REG, controller->message);
	}

	if (controller->self > MUMBLE_UNREFERENCED) {
		// Unreference ourself
		mumble_registry_unref(l, MUMBLE_THREAD_REG, controller->self);
	}

	lua_stackguard_exit(l);
}

int mumble_thread_new(lua_State *l)
{
	MumbleThreadController *controller = lua_newuserdata(l, sizeof(MumbleThreadController));
	controller->l = l;
	controller->self = MUMBLE_UNREFERENCED;
	controller->finish = MUMBLE_UNREFERENCED;
	controller->message = MUMBLE_UNREFERENCED;
	controller->bytecode = NULL;
	controller->bytecode_size = 0;

	luaL_getmetatable(l, METATABLE_THREAD_CONTROLLER);
	lua_setmetatable(l, -2);
	return 1;
}

static int thread_controller_run(lua_State *l)
{
	MumbleThreadController *controller = luaL_checkudata(l, 1, METATABLE_THREAD_CONTROLLER);

	// Convert our worker function to bytecode, so we can use it in a new state
	if (luaL_checkfunction(l, 2)) {
		lua_pushvalue(l, 2); // Push a copy of our worker function
		if (lua_dump(l, mumble_dump_controller, controller) != LUA_OK) {
			return luaL_error(l, "unable to convert worker function into bytecode");
		}
		lua_pop(l, 1); // Pop our worker function
	}

	lua_pushvalue(l, 1); // Push a copy of the userdata to prevent garabage collection
	controller->self = mumble_registry_ref(l, MUMBLE_THREAD_REG); // Pop it off as a reference

	controller->async_finished.data = controller;

	uv_async_init(uv_default_loop(), &controller->async_finished, mumble_thread_worker_finish);
	uv_thread_create(&controller->thread, mumble_thread_worker_start, controller);

	lua_pushvalue(l, 1);
	return 1;
}

static int thread_controller_join(lua_State *l)
{
	MumbleThreadController *controller = luaL_checkudata(l, 1, METATABLE_THREAD_CONTROLLER);

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
	uv_close((uv_handle_t *) &controller->async_finished, NULL);
	mumble_log(LOG_DEBUG, "%s: %p garbage collected\n", METATABLE_THREAD_CONTROLLER, controller);
	return 0;
}

const luaL_Reg mumble_thread_controller[] = {
	{"run", thread_controller_run},
	{"join", thread_controller_join},
	{"onFinish", thread_controller_onFinish},
	{"onMessage", thread_controller_onMessage},
	{"__tostring", thread_controller_tostring},
	{"__gc", thread_controller_gc},
	{NULL, NULL}
};

static int thread_worker_onMessage(lua_State *l)
{
	MumbleThreadWorker *worker = luaL_checkudata(l, 1, METATABLE_THREAD_WORKER);

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
	uv_run(uv_default_loop(), UV_RUN_DEFAULT);
	lua_pushvalue(l, 1);
	return 1;
}

static int thread_worker_stop(lua_State *l)
{
	MumbleThreadWorker *worker = luaL_checkudata(l, 1, METATABLE_THREAD_WORKER);
	uv_stop(uv_default_loop());
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
	mumble_log(LOG_DEBUG, "%s: %p garbage collected\n", METATABLE_THREAD_WORKER, worker);
	return 0;
}

const luaL_Reg mumble_thread_worker[] = {
	{"onMessage", thread_worker_onMessage},
	{"sleep", thread_worker_sleep},
	{"loop", thread_worker_loop},
	{"stop", thread_worker_stop},
	{"buffer", thread_worker_buffer},
	{"__tostring", thread_worker_tostring},
	{"__gc", thread_worker_gc},
	{NULL, NULL}
};