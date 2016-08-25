#include "mumble.h"

static unsigned long long getNanoTime()
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (ts.tv_sec + ts.tv_nsec);
}

static bool thread_isPlaying(MumbleClient *client)
{
	pthread_mutex_lock(&client->lock);
	bool result = client->audio_job != NULL;
	pthread_mutex_unlock(&client->lock);
	return result;
}

static bool thread_isConnected(MumbleClient *client)
{
	pthread_mutex_lock(&client->lock);		
	bool result = client->connected;
	pthread_mutex_unlock(&client->lock);
	return result;
}

static void mumble_audiothread(MumbleClient *client)
{
	while (thread_isConnected(client)) {

		pthread_mutex_lock(&client->lock);
		while (client->connected && client->audio_job == NULL)
			pthread_cond_wait(&client->cond, &client->lock);
		pthread_mutex_unlock(&client->lock);

		// Signal ended up being a disconnect
		if (!thread_isConnected(client)) {
			break;
		}

		while (thread_isPlaying(client)) {
			unsigned long long start, stop, diff;

			start = getNanoTime();
			pthread_mutex_lock(&client->lock);
			audio_transmission_event(client);
			pthread_mutex_unlock(&client->lock);
			stop = getNanoTime();

			diff = fabs((double) stop - start);

			struct timespec sleep;
			sleep.tv_nsec = 10000000LL - diff;

			nanosleep(&sleep, NULL);
		}
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

int mumble_connect(lua_State *l)
{
	const char* server_host_str = luaL_checkstring(l, 1);
	int port = luaL_checkinteger(l, 2);

	const char* certificate_file = luaL_checkstring(l, 3);
	const char* key_file = luaL_checkstring(l, 4);

	MumbleClient *client = lua_newuserdata(l, sizeof(MumbleClient));
	luaL_getmetatable(l, METATABLE_CLIENT);
	lua_setmetatable(l, -2);

	lua_rawgeti(l, LUA_REGISTRYINDEX, MUMBLE_CONNECTIONS);
	lua_pushvalue(l, -2);

	client->self = luaL_ref(l, -2);
	lua_pop(l, 1);

	client->l = l;

	lua_newtable(l);
	client->hooks = luaL_ref(l, LUA_REGISTRYINDEX);

	lua_newtable(l);
	client->users = luaL_ref(l, LUA_REGISTRYINDEX);

	lua_newtable(l);
	client->channels = luaL_ref(l, LUA_REGISTRYINDEX);

	client->nextping = gettime() + PING_TIMEOUT;
	client->volume = 1;
	client->audio_job = NULL;
	client->connected = true;
	client->audio_finished = false;

	int err;
	client->encoder = opus_encoder_create(48000, 1, OPUS_APPLICATION_AUDIO, &err);

	if (err != OPUS_OK) {
		lua_pushnil(l);
		lua_pushfstring(l, "could not initialize the Opus encoder: %s", opus_strerror(err));
		return 2;
	}

	opus_encoder_ctl(client->encoder, OPUS_SET_VBR(0));
	opus_encoder_ctl(client->encoder, OPUS_SET_BITRATE(40000));

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

	struct hostent *server_host;
	struct sockaddr_in server_addr;

	memset(&server_addr, 0, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(port);

	server_host = gethostbyname(server_host_str);
	if (server_host == NULL || server_host->h_addr_list[0] == NULL || server_host->h_addrtype != AF_INET) {
		lua_pushnil(l);
		lua_pushstring(l, "could not parse server address");
		return 2;
	}
	memmove(&server_addr.sin_addr, server_host->h_addr_list[0], server_host->h_length);

	int ret = connect(client->socket, (struct sockaddr *) &server_addr, sizeof(server_addr));
	if (ret != 0) {
		lua_pushnil(l);
		lua_pushfstring(l, "could not connect to server: %s", strerror(errno));
		return 2;
	}
	
	client->ssl = SSL_new(client->ssl_context);

	if (client->ssl == NULL) {
		lua_pushnil(l);
		lua_pushstring(l, "could not create SSL object");
		return 2;
	}

	if (SSL_set_fd(client->ssl, client->socket) == 0) {
		lua_pushnil(l);
		lua_pushstring(l, "could not set SSL file descriptor");
		return 2;
	}

	if (SSL_connect(client->ssl) != 1) {
		lua_pushnil(l);
		lua_pushstring(l, "could not create secure connection");
		return 2;
	}

	// Non blocking after connect
	int flags = fcntl(client->socket, F_GETFL, 0);
	fcntl(client->socket, F_SETFL, flags | O_NONBLOCK);

	if (pthread_mutex_init(&client->lock , NULL))
	{
		lua_pushnil(l);
		lua_pushstring(l, "could not init mutex");
		return 2;
	}

	if (pthread_cond_init(&client->cond, NULL))
	{
		lua_pushnil(l);
		lua_pushstring(l, "could not init condition");
		return 2;
	}

	if (pthread_create(&client->audio_thread, NULL, (void*) mumble_audiothread, client)){
		lua_pushnil(l);
		lua_pushstring(l, "could not create audio thread");
		return 2;
	}

	return 1;
}

void mumble_disconnect(MumbleClient *client)
{
	pthread_mutex_lock(&client->lock);
	client->connected = false;
	audio_transmission_stop(client);
	pthread_cond_signal(&client->cond);
	pthread_mutex_unlock(&client->lock);
}

static int mumble_gettime(lua_State *l)
{
	lua_pushnumber(l, gettime());
	return 1;
}

static int mumble_getConnections(lua_State *l)
{
	lua_rawgeti(l, LUA_REGISTRYINDEX, MUMBLE_CONNECTIONS);
	return 1;
}

static bool erroring = false;

void mumble_hook_call(lua_State *l, const char* hook, int nargs)
{
	//printf("mumble_hook_call(%s)\n", hook);

	MumbleClient *client = luaL_checkudata(l, 1, METATABLE_CLIENT);

	int top = lua_gettop(l);
	int i;

	// Get hook table
	lua_rawgeti(l, LUA_REGISTRYINDEX, client->hooks);

	// Get the table of callbacks
	lua_getfield(l, -1, hook);

	lua_remove(l, -2);

	if (lua_isnil(l, -1) == 1 || lua_istable(l, -1) == 0) {
		// Pop the nil
		lua_pop(l, 1);

		// Remove the arguments anyway despite no hook call
		for (i = top; i < top + nargs; i++) {
			lua_remove(l, i);
		}
		return;
	}

	// Push nil, needed for lua_next
	lua_pushnil(l);

	while (lua_next(l, -2)) {
		// Copy key..
		lua_pushvalue(l, -2);

		// Check value
		if (lua_isfunction(l, -2)) {
			// Copy function
			lua_pushvalue(l, -2);

			for (i = 1; i <= nargs; i++) {
				// Copy number of arguments
				int pos = top - nargs + i;

				// Push a copy of the argument
				lua_pushvalue(l, pos);
			}

			// Call
			if (erroring == true) {
				// If the user errors within the OnError hook, PANIC
				lua_call(l, nargs, 0);
			} else if (lua_pcall(l, nargs, 0, 0) != 0) {
				erroring = true;
				// Call the OnError hook
				mumble_hook_call(l, "OnError", 1);
				erroring = false;
			}
		}

		// Pop the key and value..
		lua_pop(l, 2);
	}

	// Pop the hook table
	lua_pop(l, 1);

	// Remove the original arguments
	for (i = top; i < top + nargs; i++) {
		lua_remove(l, i);
	}
}

MumbleUser* mumble_user_get(MumbleClient* client, uint32_t session) {
	MumbleUser* user = NULL;

	lua_State* l = client->l;

	lua_rawgeti(l, LUA_REGISTRYINDEX, client->users);

	lua_pushinteger(l, session);
	lua_gettable(l, -2);

	if (lua_isuserdata(l, -1) == 1) {
		user = lua_touserdata(l, -1);
	} else {
		lua_pop(l, 1);

		lua_pushinteger(l, session);

		user = lua_newuserdata(l, sizeof(MumbleUser));
		{
			luaL_getmetatable(l, METATABLE_USER);
			lua_setmetatable(l, -2);

			user->client = client;
			user->session = session;
			lua_newtable(l);
			user->data = luaL_ref(l, LUA_REGISTRYINDEX);
		}
		lua_settable(l, -3);

		lua_pushinteger(l, session);
		lua_gettable(l, -2);
	}
	
	lua_remove(l, -2);
	return user;
}

void mumble_user_raw_get(MumbleClient* client, uint32_t session)
{
	lua_State* l = client->l;
	lua_rawgeti(l, LUA_REGISTRYINDEX, client->users);
	lua_pushinteger(l, session);
	lua_gettable(l, -2);
	lua_remove(l, -2);
}

void mumble_user_remove(MumbleClient* client, uint32_t session) {
	lua_State* l = client->l;
	lua_rawgeti(l, LUA_REGISTRYINDEX, client->users);
		lua_pushinteger(l, session);
		lua_pushnil(l);
		lua_settable(l, -3);
	lua_pop(l, 1);
}

void mumble_channel_raw_get(MumbleClient* client, uint32_t channel_id)
{
	lua_State* l = client->l;
	lua_rawgeti(l, LUA_REGISTRYINDEX, client->channels);
	lua_pushinteger(l, channel_id);
	lua_gettable(l, -2);
	lua_remove(l, -2);
}

MumbleChannel* mumble_channel_get(MumbleClient* client, uint32_t channel_id)
{
	MumbleChannel* channel = NULL;

	lua_State* l = client->l;

	lua_rawgeti(l, LUA_REGISTRYINDEX, client->channels);

	lua_pushinteger(l, channel_id);
	lua_gettable(l, -2);

	if (lua_isuserdata(l, -1) == 1) {
		channel = lua_touserdata(l, -1);
	} else {
		lua_pop(l, 1);

		lua_pushinteger(l, channel_id);

		channel = lua_newuserdata(l, sizeof(MumbleChannel));
		{
			luaL_getmetatable(l, METATABLE_CHAN);
			lua_setmetatable(l, -2);

			channel->client = client;
			channel->channel_id = channel_id;
			lua_newtable(l);
			channel->data = luaL_ref(l, LUA_REGISTRYINDEX);
		}
		lua_settable(l, -3);

		lua_pushinteger(l, channel_id);
		lua_gettable(l, -2);
	}
	
	lua_remove(l, -2);
	return channel;
}

void mumble_channel_remove(MumbleClient* client, uint32_t channel_id)
{
	lua_State* l = client->l;
	lua_rawgeti(l, LUA_REGISTRYINDEX, client->channels);
		lua_pushinteger(l, channel_id);
		lua_pushnil(l);
		lua_settable(l, -3);
	lua_pop(l, 1);
}

const luaL_reg mumble[] = {
	{"connect", mumble_connect},
	{"encoder", encoder_new},
	{"sleep", mumble_sleep},
	{"gettime", mumble_gettime},
	{"getConnections", mumble_getConnections},
	{NULL, NULL}
};

const luaL_reg mumble_client[] = {
	{"auth", client_auth},
	{"update", client_update},
	{"disconnect", client_disconnect},
	{"isConnected", client_isConnected},
	{"play", client_play},
	{"isPlaying", client_isPlaying},
	{"stopPlaying", client_stopPlaying},
	{"setComment", client_setComment},
	{"setVolume", client_setVolume},
	{"getVolume", client_getVolume},
	{"hook", client_hook},
	{"call", client_call},
	{"getHooks", client_getHooks},
	{"getUsers", client_getUsers},
	{"getChannels", client_getChannels},
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
	{"setMuted", user_setMuted},
	{"setDeaf", user_setDeaf},
	{"requestStats", user_request_stats},

	{"getClient", user_getClient},
	{"getSession", user_getSession},
	{"getName", user_getName},
	{"getChannel", user_getChannel},
	{"getID", user_getID},
	{"isMute", user_isMute},
	{"isDeaf", user_isDeaf},
	{"isSelfMute", user_isSelfMute},
	{"isSelfDeaf", user_isSelfDeaf},
	{"isSuppressed", user_isSuppressed},
	{"getComment", user_getComment},
	{"getCommentHash", user_getCommentHash},
	{"isRecording", user_isRecording},
	{"isPrioritySpeaker", user_isPrioritySpeaker},
	{"getTexture", user_getTexture},
	{"getTextureHash", user_getTextureHash},
	{"getHash", user_getHash},

	{"__tostring", user_tostring},
	{"__newindex", user_newindex},
	{"__index", user_index},
	{NULL, NULL}
};

const luaL_reg mumble_channel[] = {
	{"message", channel_message},
	{"setDescription", channel_setDescription},
	{"remove", channel_remove},

	{"getClient", channel_getClient},
	{"getName", channel_getName},
	{"getID", channel_getID},
	{"getParent", channel_getParent},
	{"getDescription", channel_getDescription},
	{"getDescriptionHash", channel_getDescriptionHash},
	{"isTemporary", channel_isTemporary},
	{"getPosition", channel_getPosition},
	{"getMaxUsers", channel_getMaxUsers},

	{"__tostring", channel_tostring},
	{NULL, NULL}
};

const luaL_reg mumble_encoder[] = {
	{"setBitRate", encoder_setBitRate},
	{"__tostring", encoder_tostring},
	{NULL, NULL}
};

int luaopen_mumble(lua_State *l)
{
	signal(SIGPIPE, SIG_IGN);
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
	}

	lua_newtable(l);
	MUMBLE_CONNECTIONS = luaL_ref(l, LUA_REGISTRYINDEX);

	return 0;
}
