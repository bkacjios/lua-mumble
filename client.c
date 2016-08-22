#include "mumble.h"

/*--------------------------------
	MUMBLE CLIENT META METHODS
--------------------------------*/

int client_auth(lua_State *l)
{
	MumbleClient *client = luaL_checkudata(l, 1, METATABLE_CLIENT);

	MumbleProto__Version version = MUMBLE_PROTO__VERSION__INIT;
	MumbleProto__Authenticate auth = MUMBLE_PROTO__AUTHENTICATE__INIT;

	auth.has_opus = true;
	auth.opus = true;
	auth.username = (char*) luaL_checkstring(l, 2);
	auth.n_tokens = 0;

	if (lua_isnil(l, 3) != 0) {
		auth.password = (char*) luaL_checkstring(l, 3);
	}

	if (lua_isnil(l, 4) != 0) {
		luaL_checktype(l, 4, LUA_TTABLE);

		lua_pushvalue(l, 4);
		lua_pushnil(l);

		int i = 0;

		while (lua_next(l, -2)) {
			lua_pushvalue(l, -2);

			char *value = (char*) lua_tostring(l, -2);

			auth.tokens[i] = value;

			lua_pop(l, 2);
		}
		lua_pop(l, 1);

		auth.n_tokens = i;
	}

	struct utsname unameData;
	uname(&unameData);

	version.has_version = true;
	version.version = 1 << 16 | 2 << 8 | 8;
	version.release = MODULE_NAME " " MODULE_VERSION;
	version.os = unameData.sysname;
	version.os_version = unameData.release;

	packet_send(client, PACKET_VERSION, &version);
	packet_send(client, PACKET_AUTHENTICATE, &auth);
	return 0;
}

int client_update(lua_State *l)
{
	MumbleClient *client = luaL_checkudata(l, 1, METATABLE_CLIENT);

	pthread_mutex_lock(&client->lock);
	if (!client->connected)
		return 0;
	pthread_mutex_unlock(&client->lock);

	double curTime = gettime();

	mumble_hook_call(l, "OnTick", 0);

	if (client->nextping < curTime) {
		client->nextping = curTime + PING_TIMEOUT;
		MumbleProto__Ping ping = MUMBLE_PROTO__PING__INIT;
		mumble_hook_call(l, "OnClientPing", 0);
		packet_send(client, PACKET_PING, &ping);
	}

	pthread_mutex_lock(&client->lock);
	if (client->audio_finished) {
		client->audio_finished = false;
		mumble_hook_call(l, "OnAudioFinished", 0);
	}
	pthread_mutex_unlock(&client->lock);

	static Packet packet_read;

	int total_read = 0;

	int ret = SSL_read(client->ssl, packet_read.buffer, 6);

	if (ret <= 0) {
		int err = SSL_get_error(client->ssl, ret);
		if (err == SSL_ERROR_ZERO_RETURN || err == SSL_ERROR_SYSCALL) {
			mumble_hook_call(l, "OnDisconnect", 0);
			mumble_disconnect(client);
		}
		return 0;
	}

	if (ret != 6) {
		return 0;
	}

	packet_read.type = ntohs(*(uint16_t *)packet_read.buffer);
	if (packet_read.type >= sizeof(packet_handler) / sizeof(Packet_Handler_Func)) {
		return 0;
	}
	packet_read.length = ntohl(*(uint32_t *)(packet_read.buffer + 2));
	if (packet_read.length > PAYLOAD_SIZE_MAX) {
		return 0;
	}

	while (total_read < packet_read.length) {
		ret = SSL_read(client->ssl, packet_read.buffer + total_read, packet_read.length - total_read);
		if (ret <= 0) {
			return 0;
		}
		total_read += ret;
	}

	if (total_read != packet_read.length) {
		return 0;
	}

	Packet_Handler_Func handler = packet_handler[packet_read.type];

	if (handler != NULL) {
		handler(client, l, &packet_read);
	}

	return 0;
}

int client_disconnect(lua_State *l)
{
	MumbleClient *client = luaL_checkudata(l, 1, METATABLE_CLIENT);
	SSL_shutdown(client->ssl);
	close(client->socket);
	mumble_disconnect(client);
	return 0;
}

int client_isConnected(lua_State *l)
{
	MumbleClient *client = luaL_checkudata(l, 1, METATABLE_CLIENT);
	
	pthread_mutex_lock(&client->lock);
	lua_pushboolean(l, client->connected);
	pthread_mutex_unlock(&client->lock);
	return 1;
}

int client_play(lua_State *l)
{
	MumbleClient *client 	= luaL_checkudata(l, 1, METATABLE_CLIENT);
	const char* filepath	= luaL_checkstring(l, 2);
	float volume			= (float) luaL_optnumber(l, 3, 1);

	pthread_mutex_lock(&client->lock);
	audio_transmission_stop(client);
	pthread_mutex_unlock(&client->lock);

	//AudioTransmission *sound = lua_newuserdata(l, sizeof(AudioTransmission));
	//luaL_getmetatable(l, METATABLE_AUDIO);
	//lua_setmetatable(l, -2);

	AudioTransmission *sound = malloc(sizeof *sound);

	sound->client = client;
	sound->lua = l;
	sound->sequence = 1;
	sound->buffer.size = 0;
	sound->volume = volume;
	sound->file = fopen(filepath, "rb");

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
	client->audio_job = sound;
	pthread_cond_signal(&client->cond);
	pthread_mutex_unlock(&client->lock);

	lua_pushboolean(l, true);
	return 1;
}

int client_isPlaying(lua_State *l)
{
	MumbleClient *client 	= luaL_checkudata(l, 1, METATABLE_CLIENT);
	pthread_mutex_lock(&client->lock);
	lua_pushboolean(l, client->audio_job != NULL);
	pthread_mutex_unlock(&client->lock);
	return 1;
}

int client_stopPlaying(lua_State *l)
{
	MumbleClient *client 	= luaL_checkudata(l, 1, METATABLE_CLIENT);
	pthread_mutex_lock(&client->lock);
	audio_transmission_stop(client);
	pthread_mutex_unlock(&client->lock);
	return 1;
}

int client_setVolume(lua_State *l)
{
	MumbleClient *client 	= luaL_checkudata(l, 1, METATABLE_CLIENT);
	client->volume = luaL_checknumber(l, 2);
	return 0;
}

int client_getVolume(lua_State *l)
{
	MumbleClient *client 	= luaL_checkudata(l, 1, METATABLE_CLIENT);
	lua_pushnumber(l, client->volume);
	return 1;
}

int client_setComment(lua_State *l)
{
	MumbleClient *client = luaL_checkudata(l, 1, METATABLE_CLIENT);
	
	MumbleProto__UserState msg = MUMBLE_PROTO__USER_STATE__INIT;

	msg.has_session = true;
	msg.session = client->session;

	msg.comment = (char*) luaL_checkstring(l, 2);

	packet_send(client, PACKET_USERSTATE, &msg);
	return 0;
}

int client_hook(lua_State *l)
{
	MumbleClient *client = luaL_checkudata(l, 1, METATABLE_CLIENT);

	const char* hook = luaL_checkstring(l,2);
	const char* name = "hook";

	int funcIndex = 3;

	if (lua_isfunction(l, 3) == 0) {
		hook = luaL_checkstring(l,2);
		name = lua_tostring(l,3);
		funcIndex = 4;
	}

	luaL_checktype(l, funcIndex, LUA_TFUNCTION);

	lua_rawgeti(l, LUA_REGISTRYINDEX, client->hooksref);
	lua_getfield(l, -1, hook);

	if (lua_istable(l, -1) == 0) {
		lua_pop(l, 1);
		lua_newtable(l);
		lua_setfield(l, -2, hook);
		lua_getfield(l, -1, hook);
	}

	lua_pushvalue(l, funcIndex);
	lua_setfield(l, -2, name);

	return 0;
}

int client_call(lua_State *l)
{
	MumbleClient *client = luaL_checkudata(l, 1, METATABLE_CLIENT);
	const char* hook = luaL_checkstring(l, 2);
	int nargs = lua_gettop(l) - 2;
	mumble_hook_call(l, hook, nargs);
	return 0;
}

int client_getHooks(lua_State *l)
{
	MumbleClient *client = luaL_checkudata(l, 1, METATABLE_CLIENT);
	lua_rawgeti(l, LUA_REGISTRYINDEX, client->hooksref);
	return 1;
}

int client_getUsers(lua_State *l)
{
	MumbleClient *client = luaL_checkudata(l, 1, METATABLE_CLIENT);

	lua_newtable(l);

	Node* current = client->users;

	while (current->value != NULL) {
		lua_pushlightuserdata(client->l, current->value);
		luaL_getmetatable(client->l, METATABLE_USER);
		lua_setmetatable(client->l, -2);
		current = current->next;
	}

	//lua_rawgeti(l, LUA_REGISTRYINDEX, client->usersref);
	return 1;
}

int client_getChannels(lua_State *l)
{
	MumbleClient *client = luaL_checkudata(l, 1, METATABLE_CLIENT);
	//lua_rawgeti(l, LUA_REGISTRYINDEX, client->channelsref);
	return 1;
}

int client_gc(lua_State *l)
{
	MumbleClient *client = luaL_checkudata(l, 1, METATABLE_CLIENT);
	
	pthread_mutex_lock(&client->lock);
	audio_transmission_stop(client);
	pthread_mutex_unlock(&client->lock);

	luaL_unref(l, LUA_REGISTRYINDEX, client->hooksref);

	pthread_mutex_destroy(&client->lock);
	pthread_cond_destroy(&client->cond);

	pthread_join(client->audio_thread, NULL);

	opus_encoder_destroy(client->encoder);
	return 0;
}

int client_tostring(lua_State *l)
{
	MumbleClient *client = luaL_checkudata(l, 1, METATABLE_CLIENT);
	lua_pushfstring(l, "%s: %p", METATABLE_CLIENT, client);
	return 1;
}

int client_index(lua_State *l)
{
	MumbleClient *client = luaL_checkudata(l, 1, METATABLE_CLIENT);

	if (strcmp(lua_tostring(l, 2), "me") == 0 && client->session) {
		mumble_user_push(client, client->session);
		return 1;
	} else if (strcmp(lua_tostring(l, 2), "host") == 0) {
		lua_pushstring(l, client->host);
		return 1;
	} else if (strcmp(lua_tostring(l, 2), "port") == 0) {
		lua_pushinteger(l, client->port);
		return 1;
	}

	lua_getmetatable(l, 1);
	lua_pushvalue(l, 2);
	lua_gettable(l, -2);
	return 1;
}