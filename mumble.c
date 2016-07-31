#include "mumble.h"

static unsigned long long getTime()
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (ts.tv_sec + ts.tv_nsec);
}

static void mumble_audiothread(MumbleClient *client)
{
	while (true) {
		pthread_mutex_lock(&client->lock);

		AudioTransmission *sound = client->audiojob;

		if (sound != NULL && sound->done == false) {
			while (sound->done == false) {
				unsigned long long diff;
				unsigned long long start, stop;

				start = getTime();
				audio_transmission_event(sound);
				stop = getTime();

				diff = fabs((double) stop - start);

				struct timespec sleep;
				sleep.tv_nsec = 10000000LL - diff;

				nanosleep(&sleep, NULL);
			}
		}
		pthread_mutex_unlock(&client->lock);

		usleep(10000);
	}
}

int mumble_new(lua_State *l)
{
	const char* certificate_file = luaL_checkstring(l, 1);
	const char* key_file = luaL_checkstring(l, 2);

	MumbleClient *client = lua_newuserdata(l, sizeof(MumbleClient));
	luaL_getmetatable(l, METATABLE_CLIENT);
	lua_setmetatable(l, -2);

	client->volume = 1;

	lua_newtable(l);
	client->hooksref = luaL_ref(l, LUA_REGISTRYINDEX);

	lua_newtable(l);
	client->usersref = luaL_ref(l, LUA_REGISTRYINDEX);

	lua_newtable(l);
	client->channelsref = luaL_ref(l, LUA_REGISTRYINDEX);

	pthread_mutex_lock(&client->lock);
	client->audiojob = NULL;
	pthread_mutex_unlock(&client->lock);

	pthread_create(&client->audiothread, NULL, (void*) mumble_audiothread, client);

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

int mumble_play(lua_State *l)
{
	MumbleClient *client 	= luaL_checkudata(l, 1, METATABLE_CLIENT);
	OpusEncoder *encoder	= luaL_checkudata(l, 2, METATABLE_ENCODER);
	const char* filepath	= luaL_checkstring(l, 3);
	float volume			= (float) luaL_optnumber(l, 4, 1);

	pthread_mutex_lock(&client->lock);
	if (client->audiojob != NULL)
		client->audiojob->done = true;
	pthread_mutex_unlock(&client->lock);

	AudioTransmission *sound = lua_newuserdata(l, sizeof(AudioTransmission));
	luaL_getmetatable(l, METATABLE_AUDIO);
	lua_setmetatable(l, -2);

	sound->client = client;
	sound->lua = l;
	sound->encoder = encoder;
	sound->sequence = 1;
	sound->buffer.size = 0;
	sound->volume = volume;
	sound->file = fopen(filepath, "rb");
	sound->done = false;

	if (sound->file == NULL) {
		lua_pushnil(l);
		lua_pushfstring(l, "error opening file %s: %s", luaL_checkstring(l, 3), strerror(errno));
		return 2;
	}

	int error = ov_open_callbacks(sound->file, &sound->ogg, NULL, 0, OV_CALLBACKS_STREAMONLY_NOCLOSE);

	if (error != 0) {
		lua_pushnil(l);
		lua_pushfstring(l, "error opening file %s: %s", luaL_checkstring(l, 3), opus_strerror(error));
		return 2;
	}

	pthread_mutex_lock(&client->lock);
	client->audiojob = sound;
	pthread_mutex_unlock(&client->lock);
	return 1;
}

int mumble_isPlaying(lua_State *l)
{
	MumbleClient *client 	= luaL_checkudata(l, 1, METATABLE_CLIENT);
	lua_pushboolean(l, client->audiojob != NULL);
	return 1;
}

void mumble_hook_call(lua_State *l, const char* hook, int nargs)
{
	MumbleClient *client = luaL_checkudata(l, 1, METATABLE_CLIENT);

	lua_rawgeti(l, LUA_REGISTRYINDEX, client->hooksref);
	lua_getfield(l, -1, hook);

	if (lua_isnil(l, -1) == 0 && lua_istable(l, -1)) {
		lua_pushnil(l);

		int i = 0;

		while (lua_next(l, -2)) {
			lua_pushvalue(l, -2);

			if (lua_isfunction(l, -2)) {
				lua_pushvalue(l, -2);
				for (int i = 0; i < nargs; i++) {
					lua_pushvalue(l, 2+i);
				}
				lua_call(l, 1, 0);
			}

			lua_pop(l, 2);
		}
		lua_pop(l, 1);
	}
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
	{NULL, NULL}
};

const luaL_reg mumble_client[] = {
	{"connect", mumble_connect},
	{"auth", mumble_auth},
	{"update", mumble_update},
	{"disconnect", mumble_disconnect},
	{"play", mumble_play},
	{"isPlaying", mumble_isPlaying},
	{"hook", mumble_hook},
	{"getHooks", mumble_getHooks},
	{"getUsers", mumble_getUsers},
	{"getChannels", mumble_getChannels},
	{"gettime", mumble_gettime},
	{"__gc", mumble_gc},
	{"__tostring", mumble_tostring},
	{"__index", mumble_index},
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
