#include "mumble.h"

#include "thread.h"

static void *mumble_thread_worker(void *arg)
{
	UserThread *uthread = arg;

	int finished = uthread->finished;

	// Create state and load libs
	lua_State *l = luaL_newstate();
	luaL_openlibs(l);

	lua_stackguard_entry(l);

	// Push our error handler
	lua_pushcfunction(l, mumble_traceback);

	// Load the file in our thread
	int err = luaL_loadfile(l, uthread->filename);

	// Call the worker with our custom error handler function
	if (err > 0 || lua_pcall(l, 0, 0, -2) != 0) {
		fprintf(stderr, "%s\n", lua_tostring(l, -1));
		lua_pop(l, 1); // Pop the error
	}

	// Pop the error handler
	lua_pop(l, 1);

	lua_stackguard_exit(l);

	// Close state
	lua_close(l);

	// Signal the main Lua stack that our thread has completed
	write(user_thread_pipe[1], &finished, sizeof(int));

	return NULL;
}

void mumble_thread_event(struct ev_loop *loop, ev_io *w_, int revents)
{
	thread_io *w = (thread_io *) w_;

	int finished;

	if (read(w_->fd, &finished, sizeof(int)) != sizeof(int)) {
		return;
	}

	lua_State *l = w->l; // Use the lua_State of the main

	lua_stackguard_entry(l);

	// Check if we have a finished callback
	if (finished > 0) {
		// Push our error handler
		lua_pushcfunction(l, mumble_traceback);

		// Push the worker function from the registry
		mumble_registry_pushref(l, MUMBLE_THREAD_REG, finished);

		// Call the worker with our custom error handler function
		if (lua_pcall(l, 0, 0, -2) != 0) {
			fprintf(stderr, "%s\n", lua_tostring(l, -1));
			lua_pop(l, 1); // Pop the error
		}

		// Pop the error handler
		lua_pop(l, 1);

		mumble_registry_unref(l, MUMBLE_THREAD_REG, finished);
	}

	lua_stackguard_exit(l);
}

int mumble_thread_new(lua_State *l)
{
	UserThread *uthread = lua_newuserdata(l, sizeof(UserThread));

	uthread->filename = luaL_checkstring(l, 2);
	uthread->finished = 0;

	if (lua_isfunction(l, 3)) {
		lua_pushvalue(l, 3); // Push a copy of our callback function
		uthread->finished = mumble_registry_ref(l, MUMBLE_THREAD_REG); // Pop it off as a reference
	}

	luaL_getmetatable(l, METATABLE_THREAD);
	lua_setmetatable(l, -2);

	pthread_create(&uthread->pthread, NULL, mumble_thread_worker, uthread);
	return 0;
}

static int thread_tostring(lua_State *l)
{
	lua_pushfstring(l, "%s: %p", METATABLE_THREAD, lua_topointer(l, 1));
	return 1;
}

const luaL_Reg mumble_thread[] = {
	{"__tostring", thread_tostring},
	{NULL, NULL}
};