#include "mumble.h"

#include "audio.h"
#include "acl.h"
#include "channel.h"
#include "encoder.h"
#include "decoder.h"
#include "client.h"
#include "user.h"
#include "target.h"
#include "timer.h"
#include "packet.h"

static void signal_event(struct ev_loop *loop, ev_signal *w_, int revents)
{
	my_signal *w = (my_signal *) w_;
	MumbleClient *client = w->client;
	mumble_disconnect(w->l, client);
	ev_break(EV_DEFAULT, EVBREAK_ALL);
}

static void mumble_audio_timer(EV_P_ ev_timer *w_, int revents)
{
	my_timer *w = (my_timer *) w_;
	MumbleClient *client = w->client;

	if (client->connected) {
		lua_stackguard_entry(w->l);
		audio_transmission_event(w->l, client);
		lua_stackguard_exit(w->l);
	}
}

static void mumble_ping_timer(EV_P_ ev_timer *w_, int revents)
{
	my_timer *w = (my_timer *) w_;

	MumbleClient *client = w->client;
	lua_State *l = w->l;

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
	
	lua_stackguard_entry(l);

	lua_newtable(l);
		// Push the timestamp we are sending to the server
		lua_pushinteger(l, ts);
		lua_setfield(l, -2, "timestamp");
	mumble_hook_call(l, client, "OnClientPing", 1);

	// Safety clear stack
	lua_settop(l, 0);

	packet_send(client, PACKET_PING, &ping);

	lua_stackguard_exit(l);
}

static void socket_read_event(struct ev_loop *loop, ev_io *w_, int revents)
{
	my_io *w = (my_io *) w_;

	MumbleClient *client = w->client;
	lua_State *l = w->l;

	static Packet packet_read;

	uint32_t total_read = 0;

	int ret = SSL_read(client->ssl, packet_read.buffer, 6);

	if (ret <= 0) {
		int err = SSL_get_error(client->ssl, ret);
		if (err == SSL_ERROR_ZERO_RETURN || err == SSL_ERROR_SYSCALL) {
			mumble_disconnect(l, client);
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
		// Call our packet handler functions
		lua_stackguard_entry(l);
		handler(l, client, &packet_read);
		lua_settop(l, 0); // Incase anything gets left on the stack, remove it
		lua_stackguard_exit(l);
	}

	if (SSL_pending(client->ssl) > 0) {
		// If we still have pending packets to read, set this event to be called again
		ev_feed_fd_event(loop, w_->fd, revents);
	}
}

int MUMBLE_CONNECTIONS;

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

	lua_newtable(l);
	client->hooks = luaL_ref(l, LUA_REGISTRYINDEX);

	lua_newtable(l);
	client->users = luaL_ref(l, LUA_REGISTRYINDEX);

	lua_newtable(l);
	client->channels = luaL_ref(l, LUA_REGISTRYINDEX);

	client->host = server_host_str;
	client->port = port;
	client->time = gettime();
	client->volume = 0.5;
	client->connected = true;
	client->synced = false;
	client->audio_target = 0;
	client->audio_frames = AUDIO_DEFAULT_FRAMES;

	client->tcp_packets = 0;
	client->tcp_ping_avg = 0;
	client->tcp_ping_var = 0;

	int err;
	client->encoder = opus_encoder_create(AUDIO_SAMPLE_RATE, 1, OPUS_APPLICATION_AUDIO, &err);

	if (err != OPUS_OK) {
		lua_pushnil(l);
		lua_pushfstring(l, "could not initialize encoder: %s", opus_strerror(err));
		return 2;
	}

	opus_encoder_ctl(client->encoder, OPUS_SET_VBR(0));
	opus_encoder_ctl(client->encoder, OPUS_SET_BITRATE(AUDIO_DEFAULT_BITRATE));

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

	if ((err = SSL_connect(client->ssl)) != 1) {
		lua_pushnil(l);
		lua_pushfstring(l, "could not create secure connection: %s", SSL_get_error(client->ssl, err));
		return 2;
	}

	client->signal.l = l;
	client->signal.client = client;

	client->socket_io.l = l;
	client->socket_io.client = client;

	client->audio_timer.l = l;
	client->audio_timer.client = client;
	
	client->ping_timer.l = l;
	client->ping_timer.client = client;

	// Create a signal event.
	// Disconnects the client on exit request and ends the main loop
	ev_signal_init(&client->signal.signal, signal_event, SIGINT);
	ev_signal_start(EV_DEFAULT, &client->signal.signal);

	// Create a callback for when the socket is ready to be read from
	ev_io_init(&client->socket_io.io, socket_read_event, client->socket, EV_READ);
	ev_io_start(EV_DEFAULT, &client->socket_io.io);

	// Create a timer to ping the server every X seconds
	ev_timer_init(&client->ping_timer.timer, mumble_ping_timer, PING_TIMEOUT, PING_TIMEOUT);
	ev_timer_start(EV_DEFAULT, &client->ping_timer.timer);

	return 1;
}

static int getNetworkBandwidth(int bitrate, int frames)
{
	int overhead = 20 + 8 + 4 + 1 + 2 + 12 + frames;
	overhead *= (800 / frames);
	return overhead + bitrate;
}

void mumble_create_audio_timer(MumbleClient *client, int bitspersec)
{
	int frames = client->audio_frames;
	int bitrate;

	opus_encoder_ctl(client->encoder, OPUS_GET_BITRATE(&bitrate));

	if (bitspersec == -1) {
		// No limit
	}
	else if (getNetworkBandwidth(bitrate, frames) > bitspersec) {
		if ((frames <= 4) && (bitspersec <= 32000))
			frames = 4;
		else if ((frames == 1) &&  (bitspersec <= 64000))
			frames = 2;
		else if ((frames == 2) &&  (bitspersec <= 48000))
			frames = 4;

		if (getNetworkBandwidth(bitrate, frames) > bitspersec) {
			do {
				bitrate -= 1000;
			} while ((bitrate > 8000) && (getNetworkBandwidth(bitrate, frames) > bitspersec));
		}
	}
	if (bitrate < 8000)
		bitrate = 8000;

	if (bitrate != AUDIO_DEFAULT_BITRATE) {
		printf("Server maximum network bandwidth is only %d kbit/s. Audio quality auto-adjusted to %d kbit/s (%d ms)\n", bitspersec / 1000, bitrate / 1000, frames * 10);
		client->audio_frames = frames;
		opus_encoder_ctl(client->encoder, OPUS_SET_BITRATE(bitrate));
	}

	// Get the length of our timer for the audio stream..
	float time = (float) frames / 100;

	// Create a timer for audio data
	ev_timer_init(&client->audio_timer.timer, mumble_audio_timer, 0, time);
	ev_timer_start(EV_DEFAULT, &client->audio_timer.timer);
}

static int mumble_loop(lua_State *l)
{
	ev_run(EV_DEFAULT, 0);
	return 0;
}

void mumble_disconnect(lua_State *l, MumbleClient *client)
{
	if (client->connected) {
		mumble_hook_call(l, client, "OnDisconnect", 0);
		client->connected = false;

		// Remove ourself from the list of connections
		lua_rawgeti(l, LUA_REGISTRYINDEX, MUMBLE_CONNECTIONS);
		luaL_unref(l, -1, client->self);
	}

	if (client->ssl)
		SSL_shutdown(client->ssl);

	if (client->socket)
		close(client->socket);

	// Get table of connections
	lua_rawgeti(l, LUA_REGISTRYINDEX, MUMBLE_CONNECTIONS);

	// If we have no more connections...
	if (lua_objlen(l, -1) <= 0) {
		// Break out of the mumble.loop() call to end the script
		ev_break(EV_DEFAULT, EVBREAK_ALL);
	}

	// Clear the stack
	lua_settop(l, 0);
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

int mumble_traceback(lua_State *l)
{
	luaL_traceback(l, l, lua_tostring(l, 1), 1);
	return 1;
}

void mumble_hook_call(lua_State* l, MumbleClient *client, const char* hook, int nargs)
{
	lua_stackguard_entry(l);

	const int callargs = nargs + 1;
	const int top = lua_gettop(l);

	// Get hook table
	lua_rawgeti(l, LUA_REGISTRYINDEX, client->hooks);

	// Get the table of callbacks
	lua_getfield(l, -1, hook);

	// Remove hook table from stack
	lua_remove(l, -2);

	// if getfield returned nil OR the returned value is NOT a table..
	if (lua_isnil(l, -1) == 1 || lua_istable(l, -1) == 0) {
		// Pop the nil or nontable value
		lua_pop(l, 1);

		// Call exit early, since mumble_hook_call removes the function called and its arguments from the stack
		lua_stackguard_exit(l);

		// Remove the arguments anyway despite no hook call
		for (int i = top; i < top + nargs; i++) {
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

			// Push the client the hook is for
			mumble_client_raw_get(l, client);

			for (int i = 1; i <= nargs; i++) {
				// Copy number of arguments
				int pos = top - nargs + i;

				// Push a copy of the argument
				lua_pushvalue(l, pos);
			}

			// Call
			if (erroring == true) {
				// If the user errors within the OnError hook, PANIC
				lua_call(l, callargs, 0);
			} else {
				const int base = lua_gettop(l) - callargs;
				lua_pushcfunction(l, mumble_traceback);
				lua_insert(l, base);

				if (lua_pcall(l, callargs, 0, base) != 0) {
					// Call the OnError hook
					erroring = true;
					fprintf(stderr, "%s\n", lua_tostring(l, -1));
					mumble_hook_call(l, client, "OnError", 1);
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

	// Call exit early, since mumble_hook_call removes the function called and its arguments from the stack
	lua_stackguard_exit(l);

	// Remove the original arguments
	for (int i = top; i < top + nargs; i++) {
		lua_remove(l, i);
	}
}

MumbleUser* mumble_user_get(lua_State* l, MumbleClient* client, uint32_t session) {
	MumbleUser* user = NULL;

	lua_rawgeti(l, LUA_REGISTRYINDEX, client->users);

	lua_pushinteger(l, session);
	lua_gettable(l, -2);

	if (lua_isuserdata(l, -1) == 1) {
		user = lua_touserdata(l, -1);
	} else {
		lua_pop(l, 1); // Pop whatever was in the table before, most likely nil

		user = lua_newuserdata(l, sizeof(MumbleUser));
		{
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
			user->speaking = false;
			user->recording = false;
			user->priority_speaker = false;
			user->texture = "";
			user->texture_hash = "";
			user->texture_hash_len = 0;
			user->hash = "";
		}
		luaL_getmetatable(l, METATABLE_USER);
		lua_setmetatable(l, -2);

		lua_pushinteger(l, session);
		lua_pushvalue(l, -2); // Push a copy of the new user metatable
		lua_settable(l, -4); // Set the user metatable to where we store the table of users, using session as its index
	}
	
	lua_remove(l, -2);
	return user;
}

void mumble_client_raw_get(lua_State* l, MumbleClient* client) {
	lua_rawgeti(l, LUA_REGISTRYINDEX, MUMBLE_CONNECTIONS);
	lua_pushinteger(l, client->self);
	lua_gettable(l, -2);
	lua_remove(l, -2);
}

void mumble_user_raw_get(lua_State* l, MumbleClient* client, uint32_t session) {
	lua_rawgeti(l, LUA_REGISTRYINDEX, client->users);
	lua_pushinteger(l, session);
	lua_gettable(l, -2);
	lua_remove(l, -2);
}

void mumble_user_remove(lua_State* l, MumbleClient* client, uint32_t session) {
	lua_rawgeti(l, LUA_REGISTRYINDEX, client->users);
		lua_pushinteger(l, session);
		lua_pushnil(l);
		lua_settable(l, -3);
	lua_pop(l, 1);
}

MumbleChannel* mumble_channel_raw_get(lua_State* l, MumbleClient* client, uint32_t channel_id)
{
	lua_rawgeti(l, LUA_REGISTRYINDEX, client->channels);
	lua_pushinteger(l, channel_id);
	lua_gettable(l, -2);
	lua_remove(l, -2);
	return lua_touserdata(l, -1);
}

MumbleChannel* mumble_channel_get(lua_State* l, MumbleClient* client, uint32_t channel_id)
{
	MumbleChannel* channel = NULL;

	lua_rawgeti(l, LUA_REGISTRYINDEX, client->channels);

	lua_pushinteger(l, channel_id);
	lua_gettable(l, -2);

	if (lua_isuserdata(l, -1) == 1) {
		channel = lua_touserdata(l, -1);
	} else {
		lua_pop(l, 1); // Pop whatever was in the table before, most likely nil

		channel = lua_newuserdata(l, sizeof(MumbleChannel));
		{
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
			channel->is_enter_restricted = false;
			channel->permissions = 0;
		}
		luaL_getmetatable(l, METATABLE_CHAN);
		lua_setmetatable(l, -2);

		lua_pushinteger(l, channel_id);
		lua_pushvalue(l, -2); // Push a copy of the new channel object
		lua_settable(l, -4); // Set the channel metatable to where we store the table of cahnnels, using channel_id as its index
	}
	
	lua_remove(l, -2);
	return channel;
}

void mumble_channel_remove(lua_State* l, MumbleClient* client, uint32_t channel_id)
{
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

int luaopen_mumble(lua_State *l)
{
	signal(SIGPIPE, SIG_IGN);
	SSL_library_init();

	luaL_register(l, "mumble", mumble);
	{
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

		// Register client metatable
		luaL_newmetatable(l, METATABLE_CLIENT);
		{
			lua_pushvalue(l, -1);
			lua_setfield(l, -2, "__index");
		}
		luaL_register(l, NULL, mumble_client);
		lua_setfield(l, -2, "client");

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
	MUMBLE_CONNECTIONS = luaL_ref(l, LUA_REGISTRYINDEX);

	return 0;
}
