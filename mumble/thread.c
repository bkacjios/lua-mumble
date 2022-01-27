#include "mumble.h"

#include "thread.h"

static void *mumble_thread_worker(void *arg)
{
    UserThread *uthread = arg;
	lua_State *l = uthread->l;

	lua_stackguard_entry(l);

	// Push our error handler
	lua_pushcfunction(l, mumble_traceback);

	// Push the worker function from the registry
	mumble_pushref(l, uthread->worker);
	mumble_pushref(l, uthread->self);

	// Call the worker with our custom error handler function
	if (lua_pcall(l, 1, 0, -3) != 0) {
		fprintf(stderr, "%s\n", lua_tostring(l, -1));
		lua_pop(l, 1); // Pop the error
	}

	// Pop the error handler
	lua_pop(l, 1);

	lua_stackguard_exit(l);

	// Signal the main Lua stack that our thread has completed
	write(user_thread_pipe[1], &uthread->self, sizeof(int));

	return NULL;
}

void mumble_thread_event(struct ev_loop *loop, ev_io *w_, int revents)
{
	thread_io *w = (thread_io *) w_;

	int thread_ref;

	if (read(w_->fd, &thread_ref, sizeof(int)) != sizeof(int)) {
		return;
	}

	lua_State *l = w->l;

	lua_stackguard_entry(l);

	// Push our thread metatable by reference
	mumble_pushref(l, thread_ref);

	if (luaL_isudata(l, -1, METATABLE_THREAD)) {
		UserThread *uthread = lua_touserdata(l, -1);

		// Check if we have a finished callback
		if (uthread->finished > 0) {
			// Push our error handler
			lua_pushcfunction(l, mumble_traceback);

			// Push the worker function from the registry
			mumble_pushref(l, uthread->finished);
			mumble_pushref(l, uthread->self);

			// Call the worker with our custom error handler function
			if (lua_pcall(l, 1, 0, -3) != 0) {
				fprintf(stderr, "%s\n", lua_tostring(l, -1));
				lua_pop(l, 1); // Pop the error
			}

			// Pop the error handler
			lua_pop(l, 1);
		}
	}
	// Pop the thread
	lua_pop(l, 1);

	mumble_unref(l, thread_ref);

	lua_stackguard_exit(l);
}

int mumble_thread_new(lua_State *l)
{
	luaL_checktype(l, 2, LUA_TFUNCTION);

	UserThread *uthread = lua_newuserdata(l, sizeof(UserThread));

	lua_pushvalue(l, -1); // Push a copy of the timers userdata
	uthread->self = mumble_ref(l); // Pop it off as a reference

	lua_pushvalue(l, 2); // Push a copy of our callback function
	uthread->worker = mumble_ref(l); // Pop it off as a reference

	uthread->finished = 0;

	if (lua_isfunction(l, 3)) {
		lua_pushvalue(l, 3); // Push a copy of our callback function
		uthread->finished = mumble_ref(l); // Pop it off as a reference
	}
	
	uthread->l = lua_newthread(l);
	lua_pop(l, 1);

	luaL_getmetatable(l, METATABLE_THREAD);
	lua_setmetatable(l, -2);

	pthread_create(&uthread->thread, NULL, mumble_thread_worker, uthread);

	// Return the thread metatable
	return 1;
}

static int thread_tostring(lua_State *l)
{
	UserThread *uthread = luaL_checkudata(l, 1, METATABLE_THREAD);
	lua_pushfstring(l, "%s: %p", METATABLE_THREAD, uthread);
	return 1;
}

static int thread_gc(lua_State *l)
{
	UserThread *uthread = luaL_checkudata(l, 1, METATABLE_THREAD);
	mumble_unref(l, uthread->worker);
	if (uthread->finished > 0) {
		mumble_unref(l, uthread->finished);
	}
	return 0;
}

const luaL_Reg mumble_thread[] = {
	{"__tostring", thread_tostring},
	{"__gc", thread_gc},
	{NULL, NULL}
};