#include "mumble.h"

static void signal_event(struct ev_loop *loop, ev_signal *w_, int revents)
{
	struct my_signal *w = (struct my_signal *) w_;
	MumbleClient *client = w->client;
	mumble_disconnect(client);
	ev_break(EV_DEFAULT, EVBREAK_ALL);
}

static void mumble_audio_timer(EV_P_ ev_timer *w_, int revents)
{
	struct my_timer *w = (struct my_timer *) w_;

	MumbleClient *client = w->client;

	if (client->connected && client->audio_job != NULL) {
		audio_transmission_event(client);
	}
}

static void mumble_ping_timer(EV_P_ ev_timer *w_, int revents)
{
	struct my_timer *w = (struct my_timer *) w_;

	MumbleClient *client = w->client;

	MumbleProto__Ping ping = MUMBLE_PROTO__PING__INIT;

	double ts = gettime() * 1000;

	ping.has_timestamp = true;
	ping.timestamp = ts;

	ping.has_tcp_packets = true;
	ping.tcp_packets = client->tcp_packets;

	ping.has_tcp_ping_avg = true;
	ping.tcp_ping_avg = client->tcp_ping_avg;
	
	ping.has_tcp_ping_var = true;
	ping.tcp_ping_var = client->tcp_ping_var;

	lua_newtable(client->l);
		// Push the timestamp we are sending to the server
		lua_pushinteger(client->l, ts);
		lua_setfield(client->l, -2, "timestamp");
	mumble_hook_call(client, "OnClientPing", 1);
	packet_send(client, PACKET_PING, &ping);
}

static void socket_read_event(struct ev_loop *loop, ev_io *w_, int revents)
{
	struct my_io *w = (struct my_io *) w_;

	MumbleClient *client = w->client;
	lua_State *l = client->l;

	static Packet packet_read;

	int total_read = 0;

	int ret = SSL_read(client->ssl, packet_read.buffer, 6);

	if (ret <= 0) {
		int err = SSL_get_error(client->ssl, ret);
		if (err == SSL_ERROR_ZERO_RETURN || err == SSL_ERROR_SYSCALL) {
			mumble_disconnect(client);
		}
		ev_break(loop, EVBREAK_ALL);
		return;
	}

	if (ret != 6) {
		ev_break(loop, EVBREAK_ALL);
		return;
	}

	packet_read.type = ntohs(*(uint16_t *)packet_read.buffer);
	if (packet_read.type >= sizeof(packet_handler) / sizeof(Packet_Handler_Func)) {
		ev_break(loop, EVBREAK_ALL);
		return;
	}
	packet_read.length = ntohl(*(uint32_t *)(packet_read.buffer + 2));
	if (packet_read.length > PAYLOAD_SIZE_MAX) {
		ev_break(loop, EVBREAK_ALL);
		return;
	}

	while (total_read < packet_read.length) {
		ret = SSL_read(client->ssl, packet_read.buffer + total_read, packet_read.length - total_read);
		if (ret <= 0) {
			ev_break(loop, EVBREAK_ALL);
			return;
		}
		total_read += ret;
	}

	if (total_read != packet_read.length) {
		ev_break(loop, EVBREAK_ALL);
		return;
	}

	Packet_Handler_Func handler = packet_handler[packet_read.type];

	if (handler != NULL) {
		handler(client, l, &packet_read);
	}

	if (SSL_pending(client->ssl) > 0) {
		ev_feed_fd_event(loop, w_->fd, revents);
	}
}

static int mumble_connect(lua_State *l)
{
	const char* server_host_str = luaL_checkstring(l, 1);
	int port = luaL_checkinteger(l, 2);
	char port_str[6];
	snprintf(port_str, sizeof(port_str), "%u\n", port);

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

	client->host = server_host_str;
	client->port = port;
	client->time = gettime();
	client->volume = 1;
	client->audio_job = NULL;
	client->connected = true;
	client->synced = false;
	client->audio_finished = false;
	client->audio_target = 0;
	client->audio_frames = 1;

	client->tcp_packets = 0;
	client->tcp_ping_avg = 0;
	client->tcp_ping_var = 0;

	int err;
	client->encoder = opus_encoder_create(48000, 1, OPUS_APPLICATION_AUDIO, &err);

	if (err != OPUS_OK) {
		lua_pushnil(l);
		lua_pushfstring(l, "could not initialize encoder: %s", opus_strerror(err));
		return 2;
	}

	opus_encoder_ctl(client->encoder, OPUS_SET_VBR(0));
	opus_encoder_ctl(client->encoder, OPUS_SET_BITRATE(40000));

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

	struct addrinfo hint;
	memset(&hint, 0, sizeof(hint));
	hint.ai_family = AF_UNSPEC;
	hint.ai_socktype = SOCK_STREAM;

	struct addrinfo* server_host;
	err = getaddrinfo(server_host_str, port_str, &hint, &server_host);
	
	if(err != 0) {
		lua_pushnil(l);
		lua_pushfstring(l, "could not parse server address: %s", gai_strerror(err));
		return 2;
	}

	client->socket = socket(server_host->ai_family, server_host->ai_socktype, 0);
	if (client->socket < 0) {
		freeaddrinfo(server_host);
		lua_pushnil(l);
		lua_pushfstring(l, "could not create socket: %s", strerror(errno));
		return 2;
	}

	struct timeval timeout;
	timeout.tv_sec = 5;
	timeout.tv_usec = 0;

	if (setsockopt(client->socket, SOL_SOCKET, (SO_RCVTIMEO | SO_SNDTIMEO), (char *)&timeout, sizeof(timeout)) < 0) {
		freeaddrinfo(server_host);
		lua_pushnil(l);
		lua_pushfstring(l, "setsockopt failed: %s", strerror(errno));
		return 2;
	}

	if (connect(client->socket, server_host->ai_addr, server_host->ai_addrlen) != 0) {
		freeaddrinfo(server_host);
		lua_pushnil(l);
		lua_pushfstring(l, "could not connect to server: %s", strerror(errno));
		return 2;
	}

	freeaddrinfo(server_host);
	
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

	if (err = SSL_connect(client->ssl) != 1) {
		lua_pushnil(l);
		lua_pushfstring(l, "could not create secure connection: %s", SSL_get_error(client->ssl, err));
		return 2;
	}

	client->signal.client = client;
	client->socket_io.client = client;
	client->audio_timer.client = client;
	client->ping_timer.client = client;

	// Create a signal event.
	// Disconnects the client on exit request and ends the main loop
	ev_signal_init(&client->signal.signal, signal_event, SIGINT);
	ev_signal_start(EV_DEFAULT, &client->signal.signal);

	// Create a callback for when the socket is ready to be read from
	ev_io_init(&client->socket_io.io, socket_read_event, client->socket, EV_READ);
	ev_io_start(EV_DEFAULT, &client->socket_io.io);

	// Create a timer for audio data
	ev_timer_init(&client->audio_timer.timer, mumble_audio_timer, 0, 0.01);
	ev_timer_start(EV_DEFAULT, &client->audio_timer.timer);

	// Create a timer to ping the server every X seconds
	ev_timer_init(&client->ping_timer.timer, mumble_ping_timer, PING_TIMEOUT, PING_TIMEOUT);
	ev_timer_start(EV_DEFAULT, &client->ping_timer.timer);

	return 1;
}

static int mumble_loop(lua_State *l)
{
	ev_run(EV_DEFAULT, 0);
	return 0;
}

void mumble_disconnect(MumbleClient *client)
{
	audio_transmission_stop(client);

	if (client->connected) {
		mumble_hook_call(client, "OnDisconnect", 0);
		client->connected = false;
	}

	if (client->ssl)
		SSL_shutdown(client->ssl);

	if (client->socket)
		close(client->socket);
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

static int traceback(lua_State *l)
{
	luaL_traceback(l, l, lua_tostring(l, 1), 1);
	return 1;
}

void mumble_hook_call(MumbleClient *client, const char* hook, int nargs)
{
	lua_State* l = client->l;

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

			//mumble_client_raw_get(client);

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
			} else {
				int base = lua_gettop(l) - nargs;
				lua_pushcfunction(l, traceback);
				lua_insert(l, base);
				if (lua_pcall(l, nargs, 0, base) != 0) {
					// Call the OnError hook
					erroring = true;
					mumble_hook_call(client, "OnError", 1);
					erroring = false;
				}
				lua_remove(l, base);
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

			lua_newtable(l);
			user->data = luaL_ref(l, LUA_REGISTRYINDEX);
			user->connected = client->synced ? false : true;
			user->session = session;
			user->user_id = 0;
			user->channel_id = 0;
			user->name = "";
			user->mute = false;
			user->deaf = false;
			user->self_mute = false;
			user->self_deaf = false;
			user->suppress = false;
			user->comment = "";
			user->comment_hash = "";
			user->comment_hash_len = 0;
			user->recording = false;
			user->priority_speaker = false;
			user->texture = "";
			user->texture_hash = "";
			user->texture_hash_len = 0;
			user->hash = "";
		}
		lua_settable(l, -3);

		lua_pushinteger(l, session);
		lua_gettable(l, -2);
	}
	
	lua_remove(l, -2);
	return user;
}

void mumble_client_raw_get(MumbleClient* client) {
	lua_State* l = client->l;
	lua_rawgeti(l, LUA_REGISTRYINDEX, MUMBLE_CONNECTIONS);
	lua_pushinteger(l, client->self);
	lua_gettable(l, -2);
	lua_remove(l, -2);
}

void mumble_user_raw_get(MumbleClient* client, uint32_t session) {
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

MumbleChannel* mumble_channel_raw_get(MumbleClient* client, uint32_t channel_id)
{
	lua_State* l = client->l;
	lua_rawgeti(l, LUA_REGISTRYINDEX, client->channels);
	lua_pushinteger(l, channel_id);
	lua_gettable(l, -2);
	lua_remove(l, -2);
	return lua_touserdata(l, -1);
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

			lua_newtable(l);
			channel->data = luaL_ref(l, LUA_REGISTRYINDEX);
			channel->name = "";
			channel->channel_id = channel_id;
			channel->parent = 0;
			channel->description = "";
			channel->description_hash = "";
			channel->temporary = false;
			channel->position = 0;
			channel->max_users = 0;
			channel->links = NULL;
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

const luaL_Reg mumble[] = {
	{"connect", mumble_connect},
	{"loop", mumble_loop},
	{"gettime", mumble_gettime},
	{"getConnections", mumble_getConnections},
	{NULL, NULL}
};

const luaL_Reg mumble_client[] = {
	{"auth", client_auth},
	{"disconnect", client_disconnect},
	{"isConnected", client_isConnected},
	{"isSynced", client_isSynced},
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
	{"getChannel", client_getChannel},
	{"registerVoiceTarget", client_registerVoiceTarget},
	{"setVoiceTarget", client_setVoiceTarget},
	{"getVoiceTarget", client_getVoiceTarget},
	{"getPing", client_getPing},
	{"getUpTime", client_getUpTime},
	{"__gc", client_gc},
	{"__tostring", client_tostring},
	{"__index", client_index},
	{NULL, NULL}
};

const luaL_Reg mumble_user[] = {
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
	{"setTexture", user_setTexture},

	{"__tostring", user_tostring},
	{"__newindex", user_newindex},
	{"__index", user_index},
	{NULL, NULL}
};

const luaL_Reg mumble_channel[] = {
	{"message", channel_message},
	{"setDescription", channel_setDescription},
	{"remove", channel_remove},

	{"getClient", channel_getClient},
	{"getName", channel_getName},
	{"getID", channel_getID},
	{"getParent", channel_getParent},
	{"getChildren", channel_getChildren},
	{"getUsers", channel_getUsers},
	{"getDescription", channel_getDescription},
	{"getDescriptionHash", channel_getDescriptionHash},
	{"isTemporary", channel_isTemporary},
	{"getPosition", channel_getPosition},
	{"getMaxUsers", channel_getMaxUsers},
	{"link", channel_link},
	{"unlink", channel_unlink},
	{"getLinks", channel_getLinks},

	{"__call", channel_call},
	{"__tostring", channel_tostring},
	{"__newindex", channel_newindex},
	{"__index", channel_index},
	{NULL, NULL}
};

const luaL_Reg mumble_encoder[] = {
	{"setBitRate", encoder_setBitRate},
	{"__tostring", encoder_tostring},
	{NULL, NULL}
};

const luaL_Reg mumble_target[] = {
	{"addUser", target_addUser},
	{"setChannel", target_setChannel},
	{"getChannel", target_getChannel},
	{"setGroup", target_setGroup},
	{"setLinks", target_setLinks},
	{"setChildren", target_setChildren},
	{"__tostring", target_tostring},
	{"__gc", target_gc},
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
		lua_newtable(l);
		{
			lua_pushcfunction(l, encoder_new);
			lua_setfield(l, -2, "__call");
		}
		lua_setmetatable(l, -2);
		lua_setfield(l, -2, "encoder");

		luaL_newmetatable(l, METATABLE_VOICETARGET);
		{
			lua_pushvalue(l, -1);
			lua_setfield(l, -2, "__index");
		}
		luaL_register(l, NULL, mumble_target);
		lua_newtable(l);
		{
			lua_pushcfunction(l, target_new);
			lua_setfield(l, -2, "__call");
		}
		lua_setmetatable(l, -2);
		lua_setfield(l, -2, "voicetarget");
	}

	lua_newtable(l);
	MUMBLE_CONNECTIONS = luaL_ref(l, LUA_REGISTRYINDEX);

	return 0;
}
