#include "module.h"

#include "client/acl.h"
#include "client/client.h"
#include "defines.h"
#include "lua/bindings.h"
#include "lua/luainc.h"
#include "lua/luautil.h"
#include "lua/metatables.h"
#include "lua/registry.h"
#include "util/log.h"

#include <cstdio>

static uv_signal_t mumble_signal;
static void mumble_close();

static void mumble_signal_event(uv_signal_t* handle, int signum) {
	if (signum == SIGINT) {
		uv_signal_stop(handle);
		uv_close((uv_handle_t*) handle, NULL);
		mumble_close();
	}
}

static int mumble_loop(lua_State *l) {
	uv_signal_init(uv_default_loop(), &mumble_signal);
	uv_signal_start(&mumble_signal, mumble_signal_event, SIGINT);

	uv_run(uv_default_loop(), UV_RUN_DEFAULT);
	return 0;
}

static int mumble_stop(lua_State *l) {
	mumble_close();
	return 0;
}

static int mumble_getTime(lua_State *l) {
	lua_pushnumber(l, uv_now(uv_default_loop()) / 1000.0);
	return 1;
}

// The main loop's cached time isn't safe to read from other threads, but both use the same monotonic clock
static int mumble_getTimeWorker(lua_State *l) {
	lua_pushnumber(l, uv_hrtime() / 1e9);
	return 1;
}

static int mumble_getConnections(lua_State *l) {
	LuaRegistry::push(l, LuaRegistry::clients);
	return 1;
}

// Stands in for anything that needs the main thread's event loop, when opened inside a thread worker
static int mumble_unavailable(lua_State *l) {
	return luaL_error(l, "mumble.%s is not available inside a thread worker", lua_tostring(l, lua_upvalueindex(1)));
}

static void mumble_pushunavailable(lua_State *l, const char *name) {
	lua_pushstring(l, name);
	lua_pushcclosure(l, mumble_unavailable, 1);
}

static void mumble_setunavailable(lua_State *l, const char *name) {
	mumble_pushunavailable(l, name);
	lua_setfield(l, -2, name);
}

int mumble_traceback(lua_State *l) {
	luaL_traceback(l, l, lua_tostring(l, 1), 1);
	return 1;
}

const luaL_Reg mumble[] = {
	{"loop", mumble_loop},
	{"stop", mumble_stop},
	{"time", mumble_getTime},
	{"gettime", mumble_getTime},
	{"getTime", mumble_getTime},
	{"getConnections", mumble_getConnections},
	{"getClients", mumble_getConnections},
	{NULL, NULL}
};

// Thread workers only get what's safe to use off the main thread
static const luaL_Reg mumble_worker[] = {
	{"time", mumble_getTimeWorker},
	{"gettime", mumble_getTimeWorker},
	{"getTime", mumble_getTimeWorker},
	{NULL, NULL}
};

// Open the module, with only what's safe to use off the main thread for thread workers
static int mumble_open(lua_State *l, bool worker) {

#if defined(LUAJIT)
	lua_getglobal(l, "jit");
	int notjit = lua_isnil(l, -1);
	lua_pop(l, 1);
	if (notjit) {
		lua_getglobal(l, "_VERSION");
		const char* version = lua_tostring(l, -1);
		lua_pop(l, 1);
		return luaL_error(l, "mumble compiled for LuaJIT, but is running under vanilla %s", version);
	}
#endif

	LuaRegistry::open(l);

#if LUA_VERSION_NUM >= 502
	if (worker) {
		luaL_newlib(l, mumble_worker);
	} else {
		luaL_newlib(l, mumble);
	}
#else
	luaL_register(l, "mumble", worker ? mumble_worker : mumble);
#endif
	{
		if (worker) {
			mumble_setunavailable(l, "loop");
			mumble_setunavailable(l, "stop");
			mumble_setunavailable(l, "getConnections");
			mumble_setunavailable(l, "getClients");
		}

		// Create a table of all RejectType enums
		lua_newtable(l);
		for (uint32_t i = 0; i < mumble_proto__reject__reject_type__descriptor.n_values; i++) {
			ProtobufCEnumValueIndex reject = mumble_proto__reject__reject_type__descriptor.values_by_name[i];
			lua_pushnumber(l, reject.index);
			lua_pushstring(l, reject.name);
			lua_settable(l, -3);
		}
		lua_setfield(l, -2, "reject");

		// Create a table of all DenyType enums
		lua_newtable(l);
		for (uint32_t i = 0; i < mumble_proto__permission_denied__deny_type__descriptor.n_values; i++) {
			ProtobufCEnumValueIndex deny = mumble_proto__permission_denied__deny_type__descriptor.values_by_name[i];
			lua_pushnumber(l, deny.index);
			lua_pushstring(l, deny.name);
			lua_settable(l, -3);
		}
		lua_setfield(l, -2, "deny");

		lua_newtable(l);
		{
			lua_pushinteger(l, ACL_NONE);
			lua_setfield(l, -2, "NONE");
			lua_pushinteger(l, ACL_WRITE);
			lua_setfield(l, -2, "WRITE");
			lua_pushinteger(l, ACL_TRAVERSE);
			lua_setfield(l, -2, "TRAVERSE");
			lua_pushinteger(l, ACL_ENTER);
			lua_setfield(l, -2, "ENTER");
			lua_pushinteger(l, ACL_SPEAK);
			lua_setfield(l, -2, "SPEAK");
			lua_pushinteger(l, ACL_MUTE_DEAFEN);
			lua_setfield(l, -2, "MUTE_DEAFEN");
			lua_pushinteger(l, ACL_MOVE);
			lua_setfield(l, -2, "MOVE");
			lua_pushinteger(l, ACL_MAKE_CHANNEL);
			lua_setfield(l, -2, "MAKE_CHANNEL");
			lua_pushinteger(l, ACL_LINK_CHANNEL);
			lua_setfield(l, -2, "LINK_CHANNEL");
			lua_pushinteger(l, ACL_WHISPER);
			lua_setfield(l, -2, "WHISPER");
			lua_pushinteger(l, ACL_TEXT_MESSAGE);
			lua_setfield(l, -2, "TEXT_MESSAGE");
			lua_pushinteger(l, ACL_MAKE_TEMP_CHANNEL);
			lua_setfield(l, -2, "MAKE_TEMP_CHANNEL");
			lua_pushinteger(l, ACL_LISTEN);
			lua_setfield(l, -2, "LISTEN");

			// Root channel only ACL permissions
			lua_pushinteger(l, ACL_KICK);
			lua_setfield(l, -2, "KICK");
			lua_pushinteger(l, ACL_BAN);
			lua_setfield(l, -2, "BAN");
			lua_pushinteger(l, ACL_REGISTER);
			lua_setfield(l, -2, "REGISTER");
			lua_pushinteger(l, ACL_SELF_REGISTER);
			lua_setfield(l, -2, "SELF_REGISTER");
			lua_pushinteger(l, ACL_RESET_USER_CONTENT);
			lua_setfield(l, -2, "RESET_USER_CONTENT");

			lua_pushinteger(l, ACL_CACHED);
			lua_setfield(l, -2, "CACHED");

			lua_pushinteger(l, ACL_ALL);
			lua_setfield(l, -2, "ALL");
		}
		lua_setfield(l, -2, "acl");

		lua_newtable(l);
		{
			lua_pushinteger(l, 1);
			lua_setfield(l, -2, "TINY");
			lua_pushinteger(l, 2);
			lua_setfield(l, -2, "SMALL");
			lua_pushinteger(l, 3);
			lua_setfield(l, -2, "MEDIUM");
			lua_pushinteger(l, 4);
			lua_setfield(l, -2, "LARGE");
		}
		lua_setfield(l, -2, "audio");

		lua_newtable(l);
		{
			lua_pushinteger(l, LOG_INFO);
			lua_setfield(l, -2, "INFO");
			lua_pushinteger(l, LOG_WARN);
			lua_setfield(l, -2, "WARN");
			lua_pushinteger(l, LOG_ERROR);
			lua_setfield(l, -2, "ERROR");
			lua_pushinteger(l, LOG_DEBUG);
			lua_setfield(l, -2, "DEBUG");
			lua_pushinteger(l, LOG_TRACE);
			lua_setfield(l, -2, "TRACE");
		}
		luaL_register(l, NULL, mumble_log_reg);
		lua_setfield(l, -2, "log");

		lua_newtable(l);
		{
			lua_pushinteger(l, LEGACY_UDP_OPUS);
			lua_setfield(l, -2, "OPUS");
			lua_pushinteger(l, LEGACY_UDP_SPEEX);
			lua_setfield(l, -2, "SPEEX");
			lua_pushinteger(l, LEGACY_UDP_CELT_ALPHA);
			lua_setfield(l, -2, "CELT_ALPHA");
			lua_pushinteger(l, LEGACY_UDP_CELT_BETA);
			lua_setfield(l, -2, "UCELT_BETA");
		}
		lua_setfield(l, -2, "codec");

		if (worker) {
			mumble_setunavailable(l, "client");
		} else {
			// Register client metatable
			luaL_newmetatable(l, METATABLE_CLIENT);
			{
				lua_pushvalue(l, -1);
				lua_setfield(l, -2, "__index");
			}
			luaL_register(l, NULL, mumble_client);

			// If you call the client metatable as a function it will return a new client object
			lua_newtable(l);
			{
				lua_pushcfunction(l, mumble_client_new);
				lua_setfield(l, -2, "__call");
			}
			lua_setmetatable(l, -2);
			lua_setfield(l, -2, "client");
		}

		// Register user metatable
		luaL_newmetatable(l, METATABLE_USER);
		{
			lua_pushvalue(l, -1);
			lua_setfield(l, -2, "__index");
		}
		luaL_register(l, NULL, mumble_user);
		lua_setfield(l, -2, "user");

		// Register channel metatable
		luaL_newmetatable(l, METATABLE_CHAN);
		{
			lua_pushvalue(l, -1);
			lua_setfield(l, -2, "__index");
		}
		luaL_register(l, NULL, mumble_channel);
		lua_setfield(l, -2, "channel");

		// Register encoder metatable
		luaL_newmetatable(l, METATABLE_ENCODER);
		{
			lua_pushvalue(l, -1);
			lua_setfield(l, -2, "__index");
		}
		luaL_register(l, NULL, mumble_encoder);

		// If you call the encoder metatable as a function it will return a new encoder object
		lua_newtable(l);
		{
			lua_pushcfunction(l, mumble_encoder_new);
			lua_setfield(l, -2, "__call");
		}
		lua_setmetatable(l, -2);
		lua_setfield(l, -2, "encoder");

		// Register decoder metatable
		luaL_newmetatable(l, METATABLE_DECODER);
		{
			lua_pushvalue(l, -1);
			lua_setfield(l, -2, "__index");
		}
		luaL_register(l, NULL, mumble_decoder);

		// If you call the encoder metatable as a function it will return a new encoder object
		lua_newtable(l);
		{
			lua_pushcfunction(l, mumble_decoder_new);
			lua_setfield(l, -2, "__call");
		}
		lua_setmetatable(l, -2);
		lua_setfield(l, -2, "decoder");

		// Register ban entry metatable
		luaL_newmetatable(l, METATABLE_BANENTRY);
		{
			lua_pushvalue(l, -1);
			lua_setfield(l, -2, "__index");
		}
		luaL_register(l, NULL, mumble_banentry);

		// If you call the ban entry metatable as a function it will return a new ban entry object
		lua_newtable(l);
		{
			lua_pushcfunction(l, mumble_banentry_new);
			lua_setfield(l, -2, "__call");
		}
		lua_setmetatable(l, -2);
		lua_setfield(l, -2, "banentry");

		// Register voice target metatable
		luaL_newmetatable(l, METATABLE_VOICETARGET);
		{
			lua_pushvalue(l, -1);
			lua_setfield(l, -2, "__index");
		}
		luaL_register(l, NULL, mumble_target);

		// If you call the voice target metatable as a function it will return a new voice target object
		lua_newtable(l);
		{
			lua_pushcfunction(l, mumble_target_new);
			lua_setfield(l, -2, "__call");
		}
		lua_setmetatable(l, -2);
		lua_setfield(l, -2, "voicetarget");

		if (worker) {
			mumble_setunavailable(l, "timer");
		} else {
			// Register timer metatable
			luaL_newmetatable(l, METATABLE_TIMER);
			{
				lua_pushvalue(l, -1);
				lua_setfield(l, -2, "__index");
			}
			luaL_register(l, NULL, mumble_timer);

			// If you call the voice target metatable as a function it will return a new voice target object
			lua_newtable(l);
			{
				lua_pushcfunction(l, mumble_timer_new);
				lua_setfield(l, -2, "__call");
			}
			lua_setmetatable(l, -2);
			lua_setfield(l, -2, "timer");
		}

		lua_newtable(l);
		{
			// Register thread metatable
			luaL_newmetatable(l, METATABLE_THREAD_WORKER);
			{
				lua_pushvalue(l, -1);
				lua_setfield(l, -2, "__index");
			}
			luaL_register(l, NULL, mumble_thread_worker);
			lua_setfield(l, -2, "worker");

			if (!worker) {
				// Register thread metatable
				luaL_newmetatable(l, METATABLE_THREAD_CONTROLLER);
				{
					lua_pushvalue(l, -1);
					lua_setfield(l, -2, "__index");
				}
				luaL_register(l, NULL, mumble_thread_controller);
				lua_setfield(l, -2, "controller");
			}
		}
		// If you call the thread metatable as a function it will return a new thread object
		lua_newtable(l);
		{
			if (worker) {
				mumble_pushunavailable(l, "thread");
			} else {
				lua_pushcfunction(l, mumble_thread_new);
			}
			lua_setfield(l, -2, "__call");
		}
		lua_setmetatable(l, -2);
		lua_setfield(l, -2, "thread");

		// https://publist.mumble.info/v1/list
		// lua_pushcfunction(l, mumble_getPublicServerList)
		// lua_setfield(l, -2, "getPublicServerList")

		// Register encoder metatable
		luaL_newmetatable(l, METATABLE_AUDIOSTREAM);
		{
			lua_pushvalue(l, -1);
			lua_setfield(l, -2, "__index");
		}
		luaL_register(l, NULL, mumble_audiostream);
		lua_setfield(l, -2, "audiostream");

		// Register buffer metatable
		luaL_newmetatable(l, METATABLE_BUFFER);
		{
			lua_pushvalue(l, -1);
			lua_setfield(l, -2, "__index");
		}
		luaL_register(l, NULL, mumble_buffer);

		// If you call the buffer metatable as a function it will return a new buffer object
		lua_newtable(l);
		{
			lua_pushcfunction(l, mumble_buffer_new);
			lua_setfield(l, -2, "__call");
		}
		lua_setmetatable(l, -2);
		lua_setfield(l, -2, "buffer");

		if (worker) {
			mumble_setunavailable(l, "pipe");
		} else {
			// Register pipe metatable
			luaL_newmetatable(l, METATABLE_PIPE);
			{
				lua_pushvalue(l, -1);
				lua_setfield(l, -2, "__index");
			}
			luaL_register(l, NULL, mumble_pipe);

			// If you call the pipe metatable as a function it will return a new pipe object
			lua_newtable(l);
			{
				lua_pushcfunction(l, mumble_pipe_new);
				lua_setfield(l, -2, "__call");
			}
			lua_setmetatable(l, -2);
			lua_setfield(l, -2, "pipe");
		}

		// Register crypt metatable
		luaL_newmetatable(l, METATABLE_CRYPT);
		{
			lua_pushvalue(l, -1);
			lua_setfield(l, -2, "__index");
		}
		luaL_register(l, NULL, mumble_ocb_aes128);

		// If you call the crypt metatable as a function it will return a new crypt object
		lua_newtable(l);
		{
			lua_pushcfunction(l, mumble_crypt_new);
			lua_setfield(l, -2, "__call");
		}
		lua_setmetatable(l, -2);
		lua_setfield(l, -2, "crypt");

		LuaRegistry::pushRoot(l);
		lua_setfield(l, -2, "registry");
	}

	return 1;
}

extern "C" int luaopen_mumble(lua_State *l) {
	return mumble_open(l, false);
}

int luaopen_mumble_worker(lua_State *l) {
	return mumble_open(l, true);
}

static void stop_then_close(uv_handle_t* h, void* arg) {
	if (uv_is_closing(h)) return;

	switch (h->type) {
	case UV_TIMER:        uv_timer_stop((uv_timer_t*)h); break;
	case UV_IDLE:         uv_idle_stop((uv_idle_t*)h); break;
	case UV_PREPARE:      uv_prepare_stop((uv_prepare_t*)h); break;
	case UV_CHECK:        uv_check_stop((uv_check_t*)h); break;
	case UV_SIGNAL:       uv_signal_stop((uv_signal_t*)h); break;
	case UV_POLL:         uv_poll_stop((uv_poll_t*)h); break;
	case UV_FS_EVENT:     uv_fs_event_stop((uv_fs_event_t*)h); break;
	case UV_FS_POLL:      uv_fs_poll_stop((uv_fs_poll_t*)h); break;
	case UV_UDP:          uv_udp_recv_stop((uv_udp_t*)h); break;

	case UV_TCP:
	case UV_NAMED_PIPE:
	case UV_TTY: {
		// All are streams; stop reads before closing
		uv_read_stop((uv_stream_t*)h);
		break;
	}

	default:
		// Other handle types: just close below.
		break;
	}

	// Close
	uv_close(h, NULL);
}

static void mumble_close() {
	// Stop any clients
	for (auto current = MumbleClient::connections.begin(); current != MumbleClient::connections.end();) {
		auto next = std::next(current);
		current->data->cleanup();
		current = next;
	}

	uv_loop_t* loop = uv_default_loop();
	uv_walk(loop, stop_then_close, NULL);
	uv_stop(loop);

	printf(NEWLINE);
	mumble_log(LOG_WARN, "exiting loop");
}
