#include "util/thread.h"

#include "client/client.h"
#include "lua/bindings.h"
#include "lua/luainc.h"
#include "lua/luautil.h"
#include "lua/metatables.h"
#include "lua/registry.h"
#include "util/buffer.h"
#include "util/log.h"

#include <cstdlib>
#include <cstring>

// The message to send to the other side of a thread, from a string or a buffer at index 2
static std::string check_message(lua_State *l) {
	switch (lua_type(l, 2)) {
	case LUA_TSTRING: {
		size_t size;
		const char *data = lua_tolstring(l, 2, &size);
		return std::string(data, size);
	}
	case LUA_TUSERDATA: {
		ByteBuffer *buffer = luaL_checkclass<ByteBuffer>(l, 2, METATABLE_BUFFER);
		return std::string((char*) buffer->data + buffer->read_head, buffer->length());
	}
	default:
		return std::string();
	}
}

// Raise an error if index 2 isn't something check_message accepts
static void check_message_type(lua_State *l) {
	int type = lua_type(l, 2);
	if (type == LUA_TUSERDATA) {
		luaL_checkclass<ByteBuffer>(l, 2, METATABLE_BUFFER);
	} else if (type != LUA_TSTRING) {
		const char *msg = lua_pushfstring(l, "%s or %s expected, got %s",
		                                  lua_typename(l, LUA_TSTRING), METATABLE_BUFFER, luaL_typename(l, 2));
		luaL_argerror(l, 1, msg);
	}
}

int mumble_thread_new(lua_State *l) {
	MumbleThreadController *controller = lua_newclass<MumbleThreadController>(l, l);

	switch (lua_type(l, 2)) {
	case LUA_TSTRING:
		controller->filename = lua_tostring(l, 2);
		break;
	case LUA_TFUNCTION:
		// Convert our worker function to bytecode, so we can use it in a new state
		lua_pushvalue(l, 2); // Push a copy of our worker function
		if (lua_dump(l, MumbleThreadController::onDump, controller) != 0) {
			return luaL_error(l, "unable to convert worker function into bytecode");
		}
		lua_pop(l, 1); // Pop our worker function copy
		break;
	default: {
		const char *msg = lua_pushfstring(l, "%s or %s expected, got %s",
		                                  lua_typename(l, LUA_TSTRING), lua_typename(l, LUA_TFUNCTION), luaL_typename(l, 2));
		return luaL_argerror(l, 1, msg);
	}
	}

	lua_pushvalue(l, -1);
	controller->start();

	luaL_getmetatable(l, METATABLE_THREAD_CONTROLLER);
	lua_setmetatable(l, -2);
	return 1;
}

static int thread_controller_join(lua_State *l) {
	MumbleThreadController *controller = luaL_checkclass<MumbleThreadController>(l, 1, METATABLE_THREAD_CONTROLLER);
	controller->join();
	lua_pushvalue(l, 1);
	return 1;
}

static int thread_controller_onFinish(lua_State *l) {
	MumbleThreadController *controller = luaL_checkclass<MumbleThreadController>(l, 1, METATABLE_THREAD_CONTROLLER);

	luaL_checkfunction(l, 2);
	controller->setFinishCallback(2);

	lua_pushvalue(l, 1);
	return 1;
}

static int thread_controller_send(lua_State *l) {
	MumbleThreadController *controller = luaL_checkclass<MumbleThreadController>(l, 1, METATABLE_THREAD_CONTROLLER);
	check_message_type(l);
	if (!controller->send(check_message(l))) {
		return luaL_error(l, "thread worker failed to start or has finished");
	}
	lua_pushvalue(l, 1);
	return 1;
}

static int thread_controller_onMessage(lua_State *l) {
	MumbleThreadController *controller = luaL_checkclass<MumbleThreadController>(l, 1, METATABLE_THREAD_CONTROLLER);

	luaL_checkfunction(l, 2);
	controller->setMessageCallback(2);

	lua_pushvalue(l, 1);
	return 1;
}

static int thread_controller_tostring(lua_State *l) {
	lua_pushfstring(l, "%s: %p", METATABLE_THREAD_CONTROLLER, lua_topointer(l, 1));
	return 1;
}


static int thread_controller_gc(lua_State *l) {
	MumbleThreadController *controller = luaL_checkclass<MumbleThreadController>(l, 1, METATABLE_THREAD_CONTROLLER);
	mumble_log(LOG_DEBUG, "%s: %p garbage collected", METATABLE_THREAD_CONTROLLER, controller);
	controller->~MumbleThreadController();
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
	MumbleThreadWorker *worker = luaL_checkclass<MumbleThreadWorker>(l, 1, METATABLE_THREAD_WORKER);
	check_message_type(l);
	worker->send(check_message(l));
	lua_pushvalue(l, 1);
	return 1;
}

static int thread_worker_onMessage(lua_State *l) {
	MumbleThreadWorker *worker = luaL_checkclass<MumbleThreadWorker>(l, 1, METATABLE_THREAD_WORKER);

	luaL_checkfunction(l, 2);
	worker->setMessageCallback(2);

	lua_pushvalue(l, 1);
	return 1;
}

static int thread_worker_sleep(lua_State *l) {
	luaL_checkudata(l, 1, METATABLE_THREAD_WORKER);
	uv_sleep(luaL_checkinteger(l, 2));
	lua_pushvalue(l, 1);
	return 1;
}

static int thread_worker_loop(lua_State *l) {
	MumbleThreadWorker *worker = luaL_checkclass<MumbleThreadWorker>(l, 1, METATABLE_THREAD_WORKER);
	worker->run();
	lua_pushvalue(l, 1);
	return 1;
}

static int thread_worker_stop(lua_State *l) {
	MumbleThreadWorker *worker = luaL_checkclass<MumbleThreadWorker>(l, 1, METATABLE_THREAD_WORKER);
	worker->stop();
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
	MumbleThreadWorker *worker = luaL_checkclass<MumbleThreadWorker>(l, 1, METATABLE_THREAD_WORKER);
	mumble_log(LOG_DEBUG, "%s: %p garbage collected", METATABLE_THREAD_WORKER, worker);
	worker->~MumbleThreadWorker();
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
