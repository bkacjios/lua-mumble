#include "mumble.h"

static unsigned long long getTime()
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (ts.tv_sec + ts.tv_nsec);
}

static bool thread_isPlaying(MumbleClient *client)
{
	bool result = false;
	pthread_mutex_lock(&client->lock);

	if (client->audiojob != NULL)
		result = !client->audiojob->done;
	
	pthread_mutex_unlock(&client->lock);
	return result;
}

static void mumble_audiothread(MumbleClient *client)
{
	while (true) {
		while (thread_isPlaying(client)) {
			unsigned long long diff;
			unsigned long long start, stop;

			start = getTime();
			pthread_mutex_lock(&client->lock);
			audio_transmission_event(client->audiojob);
			pthread_mutex_unlock(&client->lock);
			stop = getTime();

			diff = fabs((double) stop - start);

			struct timespec sleep;
			sleep.tv_nsec = 10000000LL - diff;

			nanosleep(&sleep, NULL);
		}

		usleep(10000);
	}
}

int mumble_sleep(lua_State *l)
{
	double n = luaL_checknumber(l, 1);
	struct timespec t, r;
	if (n < 0.0) n = 0.0;
	if (n > INT_MAX) n = INT_MAX;
	t.tv_sec = (int) n;
	n -= t.tv_sec;
	t.tv_nsec = (int) (n * 1000000000);
	if (t.tv_nsec >= 1000000000) t.tv_nsec = 999999999;
	while (nanosleep(&t, &r) != 0) {
		t.tv_sec = r.tv_sec;
		t.tv_nsec = r.tv_nsec;
	}
	return 0;
}


int mumble_new(lua_State *l)
{
	const char* certificate_file = luaL_checkstring(l, 1);
	const char* key_file = luaL_checkstring(l, 2);

	MumbleClient *client = lua_newuserdata(l, sizeof(MumbleClient));
	luaL_getmetatable(l, METATABLE_CLIENT);
	lua_setmetatable(l, -2);

	client->nextping = 0;
	client->volume = 1;

	lua_newtable(l);
	client->hooksref = luaL_ref(l, LUA_REGISTRYINDEX);

	lua_newtable(l);
	client->usersref = luaL_ref(l, LUA_REGISTRYINDEX);

	lua_newtable(l);
	client->channelsref = luaL_ref(l, LUA_REGISTRYINDEX);

	if (pthread_mutex_init(&client->lock , NULL))
	{
		lua_pushnil(l);
		lua_pushstring(l, "could not init mutex");
		return 2;
	}

	pthread_mutex_lock(&client->lock);
	client->audiojob = NULL;
	pthread_mutex_unlock(&client->lock);

	if (pthread_create(&client->audiothread, NULL, (void*) mumble_audiothread, client)){
		lua_pushnil(l);
		lua_pushstring(l, "could not create audio thread");
		return 2;
	}

	client->socket = socket(AF_INET, SOCK_STREAM, 0);
	if (client->socket < 0) {
		lua_pushnil(l);
		lua_pushstring(l, "could not create socket");
		return 2;
	}

	client->ssl_context = SSL_CTX_new(SSLv23_client_method());

	if (client->ssl_context == NULL) {
		lua_pushnil(l);
		lua_pushstring(l, "could not create SSL context");
		return 2;
	}

	if (certificate_file != NULL) {
		if (!SSL_CTX_use_certificate_chain_file(client->ssl_context, certificate_file) ||
			!SSL_CTX_use_PrivateKey_file(client->ssl_context, key_file, SSL_FILETYPE_PEM) ||
			!SSL_CTX_check_private_key(client->ssl_context)) {

			lua_pushnil(l);
			lua_pushstring(l, "could not load certificate and/or key file");
			return 2;
		}
	}

	return 1;
}

void mumble_hook_call(lua_State *l, const char* hook, int argsStart, int nargs)
{
	MumbleClient *client = luaL_checkudata(l, 1, METATABLE_CLIENT);

	// Get hook table
	lua_rawgeti(l, LUA_REGISTRYINDEX, client->hooksref);

	// Get the table of callbacks
	lua_getfield(l, -1, hook);

	lua_remove(l, -2);

	if (lua_isnil(l, -1) == 1 || lua_istable(l, -1) == 0) {
		lua_pop(l, 1);
		return;
	}

	// Push nil, needed for lua_next
	lua_pushnil(l);

	int i = 1;

	while (lua_next(l, -2)) {
		// Copy key..
		lua_pushvalue(l, -2);

		// Check value
		if (lua_isfunction(l, -2)) {
			// Copy function
			lua_pushvalue(l, -2);

			for (i = 1; i <= nargs; i++) {
				// Copy number of arguments
				lua_pushvalue(l, argsStart+i);
			}

			// Call
			lua_call(l, nargs, 0);
		}

		// Pop off the key and value..
		lua_pop(l, 2);
	}

	// Pop off the nil needed for lua_next
	lua_pop(l, 1);
}

void mumble_user_get(lua_State *l, uint32_t session)
{
	MumbleClient *client = luaL_checkudata(l, 1, METATABLE_CLIENT);
	lua_rawgeti(l, LUA_REGISTRYINDEX, client->usersref);

	lua_pushinteger(l, session);
	lua_gettable(l, -2);

	if (lua_istable(l, -1) == 0) {
		lua_pop(l, 1);

		lua_pushinteger(l, session);
		lua_newtable(l);
			lua_pushinteger(l, session);
			lua_setfield(l, -2, "session");

			lua_pushvalue(l, 1);
			lua_setfield(l, -2, "client");

			luaL_getmetatable(l, METATABLE_USER);
			lua_setmetatable(l, -2);
		lua_settable(l, -3);

		lua_pushinteger(l, session);
		lua_gettable(l, -2);
	}

	lua_remove(l, -2);
}

void mumble_user_remove(lua_State *l, uint32_t session)
{
	MumbleClient *client = luaL_checkudata(l, 1, METATABLE_CLIENT);
	lua_rawgeti(l, LUA_REGISTRYINDEX, client->usersref);
		lua_pushinteger(l, session);
		lua_pushnil(l);
		lua_settable(l, -3);
	lua_pop(l, 1);
}

void mumble_channel_get(lua_State *l, uint32_t channel_id)
{
	MumbleClient *client = luaL_checkudata(l, 1, METATABLE_CLIENT);
	lua_rawgeti(l, LUA_REGISTRYINDEX, client->channelsref);

	lua_pushinteger(l, channel_id);
	lua_gettable(l, -2);

	if (lua_istable(l, -1) == 0) {
		lua_pop(l, 1);

		lua_pushinteger(l, channel_id);
		lua_newtable(l);
			lua_pushinteger(l, channel_id);
			lua_setfield(l, -2, "channel_id");

			lua_pushvalue(l, 1);
			lua_setfield(l, -2, "client");

			lua_newtable(l);
			lua_setfield(l, -2, "children");

			luaL_getmetatable(l, METATABLE_CHAN);
			lua_setmetatable(l, -2);
		lua_settable(l, -3);

		lua_pushinteger(l, channel_id);
		lua_gettable(l, -2);
	}
	
	lua_remove(l, -2);
}

void mumble_channel_remove(lua_State *l, uint32_t channel_id)
{
	MumbleClient *client = luaL_checkudata(l, 1, METATABLE_CLIENT);
	lua_rawgeti(l, LUA_REGISTRYINDEX, client->channelsref);
		lua_pushinteger(l, channel_id);
		lua_pushnil(l);
		lua_settable(l, -3);
	lua_pop(l, 1);
}

const luaL_reg mumble[] = {
	{"new", mumble_new},
	{"encoder", encoder_new},
	{"sleep", mumble_sleep},
	{NULL, NULL}
};

const luaL_reg mumble_client[] = {
	{"connect", client_connect},
	{"auth", client_auth},
	{"update", client_update},
	{"disconnect", client_disconnect},
	{"play", client_play},
	{"isPlaying", client_isPlaying},
	{"setVolume", client_setVolume},
	{"getVolume", client_getVolume},
	{"hook", client_hook},
	{"call", client_call},
	{"getHooks", client_getHooks},
	{"getUsers", client_getUsers},
	{"getChannels", client_getChannels},
	{"gettime", client_gettime},
	{"__gc", client_gc},
	{"__tostring", client_tostring},
	{"__index", client_index},
	{NULL, NULL}
};

const luaL_reg mumble_user[] = {
	{"message", user_message},
	{"kick", user_kick},
	{"ban", user_ban},
	{"move", user_move},
	{"mute", user_mute},
	{"deafen", user_deafen},
	{"comment", user_comment},
	{"requestStats", user_request_stats},
	{"__tostring", user_tostring},
	{NULL, NULL}
};

const luaL_reg mumble_channel[] = {
	{"message", channel_message},
	{"setDescription", channel_setDescription},
	{"remove", channel_remove},
	{"__tostring", channel_tostring},
	{NULL, NULL}
};

const luaL_reg mumble_encoder[] = {
	{"setBitRate", encoder_setBitRate},
	{"__tostring", encoder_tostring},
	{NULL, NULL}
};

const luaL_reg mumble_audio[] = {
	{"stop", audio_stop},
	{"setVolume", audio_setVolume},
	{"getVolume", audio_getVolume},
	{"__tostring", audio_tostring},
	{NULL, NULL}
};

int luaopen_mumble(lua_State *l)
{
	SSL_library_init();

	luaL_register(l, "mumble", mumble);
	{
		lua_newtable(l);
		for (int i = 0; i < mumble_proto__reject__reject_type__descriptor.n_values; i++) {
			ProtobufCEnumValueIndex reject = mumble_proto__reject__reject_type__descriptor.values_by_name[i];
			lua_pushnumber(l, reject.index);
			lua_setfield(l, -2, reject.name);
		}
		lua_setfield(l, -2, "reject");

		luaL_newmetatable(l, METATABLE_CLIENT);
		{
			lua_pushvalue(l, -1);
			lua_setfield(l, -2, "__index");
		}
		luaL_register(l, NULL, mumble_client);
		lua_setfield(l, -2, "client");

		luaL_newmetatable(l, METATABLE_USER);
		{
			lua_pushvalue(l, -1);
			lua_setfield(l, -2, "__index");
		}
		luaL_register(l, NULL, mumble_user);
		lua_setfield(l, -2, "user");

		luaL_newmetatable(l, METATABLE_CHAN);
		{
			lua_pushvalue(l, -1);
			lua_setfield(l, -2, "__index");
		}
		luaL_register(l, NULL, mumble_channel);
		lua_setfield(l, -2, "channel");

		luaL_newmetatable(l, METATABLE_ENCODER);
		{
			lua_pushvalue(l, -1);
			lua_setfield(l, -2, "__index");
		}
		luaL_register(l, NULL, mumble_encoder);
		lua_setfield(l, -2, "opus");

		luaL_newmetatable(l, METATABLE_AUDIO);
		{
			lua_pushvalue(l, -1);
			lua_setfield(l, -2, "__index");
		}
		luaL_register(l, NULL, mumble_audio);
		lua_setfield(l, -2, "audio");
	}

	return 0;
}
