#include "mumble.h"

#include "audio.h"
#include "acl.h"
#include "banentry.h"
#include "channel.h"
#include "encoder.h"
#include "decoder.h"
#include "client.h"
#include "user.h"
#include "target.h"
#include "timer.h"
#include "thread.h"
#include "packet.h"
#include "ocb.h"

int MUMBLE_REGISTRY;
int MUMBLE_CLIENTS;
int MUMBLE_TIMER_REG;
int MUMBLE_THREAD_REG;

static ev_signal mumble_signal;
static thread_io user_thread_watcher;

int user_thread_pipe[2];

static void mumble_signal_event(struct ev_loop *loop, ev_signal *w, int revents)
{
	ev_break(EV_DEFAULT, EVBREAK_ALL);
}

static void mumble_ping_timer(EV_P_ ev_timer *w_, int revents)
{
	my_timer *w = (my_timer *) w_;

	MumbleClient *client = w->client;
	lua_State *l = w->l;
	
	lua_stackguard_entry(l);

	mumble_ping_tcp(l, client);
	mumble_ping_udp(l, client);

	lua_stackguard_exit(l);
}

void mumble_ping_udp(lua_State *l, MumbleClient* client) {
	if (crypt_isValid(client->crypt)) {
		double timestamp = gettime(CLOCK_MONOTONIC);
		uint64_t ts64 = (uint64_t) (timestamp * 1000);
		unsigned char packet[UDP_BUFFER_MAX];
		packet[0] = UDP_PING << 5;
		int len = 1 + sizeof(ts64);
		memcpy(packet + 1, &ts64, sizeof(ts64));

		if (client->udp_ping_acc >= 3) {
			printf("Server no longer responding to UDP pings, falling back to TCP..\n");
			client->udp_tunnel = true;
		}

		lua_newtable(l);
			lua_pushnumber(l, timestamp);
			lua_setfield(l, -2, "timestamp");
		mumble_hook_call(l, client, "OnPingUDP", 1);

		client->udp_ping_acc++;

		packet_sendudp(client, packet, len);
	}
}

void mumble_ping_tcp(lua_State *l, MumbleClient *client)
{
	MumbleProto__Ping ping = MUMBLE_PROTO__PING__INIT;

	double timestamp = gettime(CLOCK_MONOTONIC);
	uint64_t ts64 = (uint64_t) (timestamp * 1000);

	ping.has_timestamp = true;
	ping.timestamp = ts64;

	ping.has_tcp_packets = true;
	ping.tcp_packets = client->tcp_packets;

	ping.has_tcp_ping_avg = true;
	ping.tcp_ping_avg = client->tcp_ping_avg;
	
	ping.has_tcp_ping_var = true;
	ping.tcp_ping_var = client->tcp_ping_var;

	ping.has_good = true;
	ping.good = crypt_getGood(client->crypt);

	ping.has_late = true;
	ping.late = crypt_getLate(client->crypt);

	ping.has_lost = true;
	ping.lost = crypt_getLost(client->crypt);

	ping.has_resync = true;
	ping.resync = client->resync;

	ping.has_udp_packets = true;
	ping.udp_packets = client->udp_packets;

	ping.has_udp_ping_avg = true;
	ping.udp_ping_avg = client->udp_ping_avg;
	
	ping.has_udp_ping_var = true;
	ping.udp_ping_var = client->udp_ping_var;

	lua_newtable(l);
		// Push the timestamp we are sending to the server
		lua_pushnumber(l, timestamp);
		lua_setfield(l, -2, "timestamp");
		lua_pushinteger(l, ping.tcp_packets);
		lua_setfield(l, -2, "tcp_packets");
		lua_pushnumber(l, ping.tcp_ping_avg);
		lua_setfield(l, -2, "tcp_ping_avg");
		lua_pushnumber(l, ping.tcp_ping_var);
		lua_setfield(l, -2, "tcp_ping_var");
		lua_pushinteger(l, ping.good);
		lua_setfield(l, -2, "good");
		lua_pushinteger(l, ping.late);
		lua_setfield(l, -2, "late");
		lua_pushinteger(l, ping.lost);
		lua_setfield(l, -2, "lost");
		lua_pushinteger(l, ping.resync);
		lua_setfield(l, -2, "resync");
		lua_pushinteger(l, ping.udp_packets);
		lua_setfield(l, -2, "udp_packets");
		lua_pushnumber(l, ping.udp_ping_avg);
		lua_setfield(l, -2, "udp_ping_avg");
		lua_pushnumber(l, ping.udp_ping_var);
		lua_setfield(l, -2, "udp_ping_var");
	mumble_hook_call(l, client, "OnPingTCP", 1);

	packet_send(client, PACKET_PING, &ping);
}

static void socket_connect_event_tcp(struct ev_loop *loop, ev_io *w_, int revents)
{
	my_io *w = (my_io *) w_;

	MumbleClient *client = w->client;
	lua_State *l = w->l;

	int ret;
	if ((ret = connect(client->socket_tcp, client->server_host_tcp->ai_addr, client->server_host_tcp->ai_addrlen)) < 0) {
		if (errno != EINPROGRESS && errno != EISCONN) {
			mumble_disconnect(l, client, strerror(errno));
			ev_io_stop(loop, w_);
			return;
		}
	}

	ret = SSL_connect(client->ssl);
	if (ret <= 0) {
		int err = SSL_get_error(client->ssl, ret);
		if (err != SSL_ERROR_WANT_READ && err != SSL_ERROR_WANT_WRITE) {
			mumble_disconnect(l, client, "could not create secure connection");
			ev_io_stop(loop, w_);
		}
		return;
	}

	client->connected = true;
	mumble_hook_call(l, client, "OnConnect", 0);
	ev_io_stop(loop, w_);
}

static void socket_read_event_tcp(struct ev_loop *loop, ev_io *w_, int revents)
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
			mumble_disconnect(l, client, "connection closed by server");
		}
		return;
	}

	if (ret != 6) {
		return;
	}

	packet_read.type = ntohs(*(uint16_t *)packet_read.buffer);
	if (packet_read.type >= sizeof(packet_handler) / sizeof(Packet_Handler_Func)) {
		return;
	}
	packet_read.length = ntohl(*(uint32_t *)(packet_read.buffer + 2));
	if (packet_read.length > PAYLOAD_SIZE_MAX) {
		return;
	}

	while (total_read < packet_read.length) {
		ret = SSL_read(client->ssl, packet_read.buffer + total_read, packet_read.length - total_read);
		if (ret <= 0) {
			return;
		}
		total_read += ret;
	}

	if (total_read != packet_read.length) {
		return;
	}

	Packet_Handler_Func handler = packet_handler[packet_read.type];

	if (handler != NULL) {
		// Call our packet handler functions
		lua_stackguard_entry(l);
		handler(l, client, &packet_read);
		lua_stackguard_exit(l);
	}

	if (client->connected && SSL_pending(client->ssl) > 0) {
		// If we still have pending packets to read, set this event to be called again
		ev_feed_fd_event(loop, w_->fd, revents);
	}
}

static void socket_read_event_udp(struct ev_loop *loop, ev_io *w_, int revents)
{
	my_io *w = (my_io *) w_;

	MumbleClient *client = w->client;
	lua_State *l = w->l;

	int caddrlen;
	struct sockaddr_in cliaddr;
	char encrypted[UDP_BUFFER_MAX];

	ssize_t size;
	size = recvfrom(client->socket_udp, encrypted, sizeof(encrypted), 0, client->server_host_udp->ai_addr, &client->server_host_udp->ai_addrlen);
	if (size > 0)
	{
		unsigned char plaintext[size];

		if (crypt_isValid(client->crypt) && crypt_decrypt(client->crypt, encrypted, plaintext, size))
		{
			unsigned char header = plaintext[0];
			unsigned char id = header >> 5;

			switch (id) {
				case UDP_PING:
				{
					uint64_t timestamp;
					memcpy(&timestamp, plaintext + 1, sizeof(uint64_t));

					double response = gettime(CLOCK_MONOTONIC);
					double delay = (response * 1000) - timestamp;

					double n = client->udp_packets + 1;
					client->udp_packets = n;
					client->udp_ping_avg = client->udp_ping_avg * (n-1)/n + delay/n;
					client->udp_ping_var = pow(fabs(delay - client->udp_ping_avg), 2);

					if (client->udp_tunnel && client->udp_ping_acc > 1) {
						printf("Server is responding to UDP packets again, switching back to UDP..\n");
					}

					client->udp_ping_acc = 0;
					client->udp_tunnel = false;

					lua_newtable(l);
						lua_pushnumber(l, (double) timestamp / 1000);
						lua_setfield(l, -2, "timestamp");
						lua_pushnumber(l, delay);
						lua_setfield(l, -2, "ping");
						lua_pushnumber(l, client->udp_ping_avg);
						lua_setfield(l, -2, "average");
						lua_pushnumber(l, client->udp_ping_var);
						lua_setfield(l, -2, "deviation");
					mumble_hook_call(l, client, "OnPongUDP", 1);
					break;
				}
				case UDP_OPUS:
				case UDP_SPEEX:
				case UDP_CELT_ALPHA:
				case UDP_CELT_BETA:
				{
					unsigned char target = header >> 0x1F;

					int read = 1;
					int session = util_get_varint(plaintext + read, &read);

					mumble_handle_speaking_hooks(l, client, plaintext + read, id, target, session);
					break;
				}
			}
		}
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

	client->closed = false;

	lua_newtable(l);
	client->hooks = mumble_ref(l);

	lua_newtable(l);
	client->users = mumble_ref(l);

	lua_newtable(l);
	client->channels = mumble_ref(l);

	lua_newtable(l);
	client->audio_streams = mumble_ref(l);

	client->host = server_host_str;
	client->port = port;
	client->session = 0;
	client->time = gettime(CLOCK_MONOTONIC);
	client->volume = 0.5;
	client->connected = false;
	client->synced = false;
	client->audio_target = 0;
	client->audio_frames = AUDIO_DEFAULT_FRAMES;

	for(int i = 0; i < AUDIO_MAX_STREAMS; ++i)
		client->audio_jobs[i] = NULL;

	client->tcp_packets = 0;
	client->tcp_ping_avg = 0;
	client->tcp_ping_var = 0;

	client->udp_ping_acc = 0;
	client->udp_packets = 0;
	client->udp_ping_avg = 0;
	client->udp_ping_var = 0;

	client->udp_tunnel = true;

	client->crypt = crypt_new();

	client->user_list = NULL;
	client->channel_list = NULL;

	if (client->crypt == NULL) {
		lua_pushnil(l);
		lua_pushfstring(l, "could not initialize ocb cryptstate");
		return 2;
	}

	client->encoder = lua_newuserdata(l, opus_encoder_get_size(AUDIO_PLAYBACK_CHANNELS));
	luaL_getmetatable(l, METATABLE_ENCODER);
	lua_setmetatable(l, -2);

	int err = opus_encoder_init(client->encoder, AUDIO_SAMPLE_RATE, AUDIO_PLAYBACK_CHANNELS, OPUS_APPLICATION_AUDIO);
	if (err != OPUS_OK) {
		lua_pushnil(l);
		lua_pushfstring(l, "could not initialize opus encoder: %s", opus_strerror(err));
		return 2;
	}

	client->encoder_ref = mumble_ref(l);

	opus_encoder_ctl(client->encoder, OPUS_SET_VBR(0));
	opus_encoder_ctl(client->encoder, OPUS_SET_BITRATE(AUDIO_DEFAULT_BITRATE));

	client->ssl_context = SSL_CTX_new(SSLv23_client_method());

	if (client->ssl_context == NULL) {
		lua_pushnil(l);
		lua_pushstring(l, "could not create SSL context");
		return 2;
	}

	if (!SSL_CTX_use_certificate_chain_file(client->ssl_context, certificate_file) ||
		!SSL_CTX_use_PrivateKey_file(client->ssl_context, key_file, SSL_FILETYPE_PEM) ||
		!SSL_CTX_check_private_key(client->ssl_context)) {

		lua_pushnil(l);
		lua_pushstring(l, "could not load certificate and/or key file");
		return 2;
	}

	struct addrinfo hint_tcp;
	memset(&hint_tcp, 0, sizeof(hint_tcp));
	hint_tcp.ai_family = AF_UNSPEC;
	hint_tcp.ai_socktype = SOCK_STREAM;

	err = getaddrinfo(server_host_str, port_str, &hint_tcp, &client->server_host_tcp);
	
	if(err != 0) {
		lua_pushnil(l);
		lua_pushfstring(l, "could not parse server address: %s", gai_strerror(err));
		return 2;
	}

#ifdef ENABLE_UDP
	// UDP Connection

	struct addrinfo hint_udp;
	memset(&hint_udp, 0, sizeof(hint_udp));
	hint_udp.ai_family = AF_UNSPEC;
	hint_udp.ai_socktype = SOCK_DGRAM;

	err = getaddrinfo(server_host_str, port_str, &hint_udp, &client->server_host_udp);

	if(err != 0) {
		lua_pushnil(l);
		lua_pushfstring(l, "could not parse server address: %s", gai_strerror(err));
		return 2;
	}

	client->socket_udp = socket(client->server_host_udp->ai_family, client->server_host_udp->ai_socktype, client->server_host_udp->ai_protocol);
	if (client->socket_udp < 0) {
		freeaddrinfo(client->server_host_udp);
		lua_pushnil(l);
		lua_pushfstring(l, "could not create udp socket: %s", strerror(errno));
		return 2;
	}

	int n = UDP_BUFFER_MAX;
	if (setsockopt(client->socket_udp, SOL_SOCKET, SO_RCVBUF, &n, sizeof(n)) < 0) {
		lua_pushnil(l);
		lua_pushfstring(l, "could not set udp socket buffer size: %s", strerror(errno));
		return 2;
	}
#endif

	client->socket_tcp = socket(client->server_host_tcp->ai_family, client->server_host_tcp->ai_socktype, 0);
	if (client->socket_tcp < 0) {
		lua_pushnil(l);
		lua_pushfstring(l, "could not create tcp socket: %s", strerror(errno));
		return 2;
	}

	struct timeval timeout;
	timeout.tv_sec = 5;
	timeout.tv_usec = 0;

	if (setsockopt(client->socket_tcp, SOL_SOCKET, (SO_RCVTIMEO | SO_SNDTIMEO), (char *)&timeout, sizeof(timeout)) < 0) {
		lua_pushnil(l);
		lua_pushfstring(l, "setsockopt failed: %s", strerror(errno));
		return 2;
	}

	if (fcntl(client->socket_tcp, F_SETFL, SOCK_NONBLOCK) < 0) {
		lua_pushnil(l);
		lua_pushfstring(l, "could not switch to non-blocking tcp socket: %s", strerror(errno));
		return 2;
	}
	
	client->ssl = SSL_new(client->ssl_context);

	if (client->ssl == NULL) {
		lua_pushnil(l);
		lua_pushstring(l, "could not create SSL object");
		return 2;
	}

	if (SSL_set_fd(client->ssl, client->socket_tcp) == 0) {
		lua_pushnil(l);
		lua_pushstring(l, "could not set SSL file descriptor");
		return 2;
	}

	client->signal.l = l;
	client->signal.client = client;

	client->socket_tcp_io.l = l;
	client->socket_tcp_io.client = client;

	client->socket_tcp_connect.l = l;
	client->socket_tcp_connect.client = client;

	client->audio_timer.l = l;
	client->audio_timer.client = client;
	
	client->ping_timer.l = l;
	client->ping_timer.client = client;

#ifdef ENABLE_UDP
	client->socket_udp_io.l = l;
	client->socket_udp_io.client = client;

	// Create a callback for when the udp socket is ready to be read from
	ev_io_init(&client->socket_udp_io.io, socket_read_event_udp, client->socket_udp, EV_READ);
	ev_io_start(EV_DEFAULT, &client->socket_udp_io.io);
#endif

	// Create a callback for when the tcp socket is ready to be read from
	ev_io_init(&client->socket_tcp_io.io, socket_read_event_tcp, client->socket_tcp, EV_READ);
	ev_io_start(EV_DEFAULT, &client->socket_tcp_io.io);

	ev_io_init(&client->socket_tcp_connect.io, socket_connect_event_tcp, client->socket_tcp, EV_WRITE);
	ev_io_start(EV_DEFAULT, &client->socket_tcp_connect.io);

	// Create a timer to ping the server every X seconds
	ev_timer_init(&client->ping_timer.timer, mumble_ping_timer, PING_TIMEOUT, PING_TIMEOUT);

	// Register ourself in the list of active clients
	lua_pushvalue(l, -1);
	client->self = mumble_registry_ref(l, MUMBLE_CLIENTS);
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
	int frames = client->audio_frames / 10;
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
		client->audio_frames = frames * 10;
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

static int mumble_stop(lua_State *l)
{
	ev_break(EV_DEFAULT, EVBREAK_ALL);
	return 0;
}

void mumble_disconnect(lua_State *l, MumbleClient *client, const char* reason)
{
	if (client->closed) return;

	client->closed = true;
	client->connected = false;

#ifdef ENABLE_UDP
	ev_io_stop(EV_DEFAULT, &client->socket_udp_io.io);
#endif
	ev_io_stop(EV_DEFAULT, &client->socket_tcp_io.io);
	ev_io_stop(EV_DEFAULT, &client->socket_tcp_connect.io);
	ev_timer_stop(EV_DEFAULT, &client->ping_timer.timer);

	if (client->crypt) {
		free(client->crypt);
		client->crypt = NULL;
	}

	if (client->ssl) {
		SSL_shutdown(client->ssl);
		client->ssl = NULL;
	}

	if (client->ssl_context) {
		SSL_CTX_free(client->ssl_context);
		client->ssl_context = NULL;
	}

	if (client->socket_tcp) {
		close(client->socket_tcp);
		client->socket_tcp = 0;
	}

	if (client->socket_udp) {
		close(client->socket_udp);
		client->socket_udp = 0;
	}

	if (client->server_host_udp) {
		freeaddrinfo(client->server_host_udp);
		client->server_host_udp = NULL;
	}

	if (client->server_host_tcp) {
		freeaddrinfo(client->server_host_tcp);
		client->server_host_tcp = NULL;
	}

	lua_stackguard_entry(l);

	if (client->audio_jobs) {
		for(int i = 1; i <= AUDIO_MAX_STREAMS; ++i) {
			audio_transmission_stop(l, client, i);
		}
	}

	lua_pushstring(l, reason);
	mumble_hook_call(l, client, "OnDisconnect", 1);

	LinkNode* current = client->user_list;

	if (current) {
		while (current != NULL)
		{
			mumble_user_remove(l, client, current->data);
			current = current->next;
		}
	}

	current = client->channel_list;

	if (current) {
		while (current != NULL)
		{
			mumble_channel_remove(l, client, current->data);
			current = current->next;
		}
	}

	mumble_registry_unref(l, MUMBLE_CLIENTS, client->self);
	mumble_unref(l, client->hooks);
	mumble_unref(l, client->users);
	mumble_unref(l, client->channels);
	mumble_unref(l, client->audio_streams);
	mumble_unref(l, client->encoder_ref);

	lua_stackguard_exit(l);
}

static int mumble_getTime(lua_State *l)
{
	lua_pushnumber(l, gettime(CLOCK_REALTIME));
	return 1;
}

static int mumble_getConnections(lua_State *l)
{
	lua_rawgeti(l, LUA_REGISTRYINDEX, MUMBLE_CLIENTS);
	return 1;
}

static bool erroring = false;

int mumble_traceback(lua_State *l)
{
	lua_pushfstring(l, "mumble: %s", lua_tostring(l, 1));
#if (defined(LUA_VERSION_NUM) && LUA_VERSION_NUM >= 502) || defined(LUAJIT)
	luaL_traceback(l, l, lua_tostring(l, -1), 1);
#endif
	return 1;
}

int mumble_hook_call(lua_State* l, MumbleClient *client, const char* hook, int nargs)
{
	return mumble_hook_call_ret(l, client, hook, nargs, 0);
}

int mumble_hook_call_ret(lua_State* l, MumbleClient *client, const char* hook, int nargs, int nresults)
{
	//lua_stackguard_entry(l);

	const int top = lua_gettop(l);
	const int callargs = nargs + 1;
	const int argpos = top - nargs;

	int returned = false;
	int nreturns = 0;

	// Get hook table
	mumble_pushref(l, client->hooks);

	// Get the table of callbacks
	lua_getfield(l, -1, hook);

	// Remove hook table from stack
	lua_remove(l, -2);

	// if getfield returned nil OR the returned value is NOT a table..
	if (lua_isnil(l, -1) == 1 || lua_istable(l, -1) == 0) {
		// Pop the nil or nontable value
		lua_pop(l, 1);

		// Exit if we have no hook to call
		goto exit;
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
				// Push a copy of the argument
				lua_pushvalue(l, argpos + i);
			}

			// Call
			if (erroring == true) {
				// If the user errors within the OnError hook, PANIC
				lua_call(l, callargs, 0);
			} else {
				const int base = lua_gettop(l) - callargs;

				lua_pushcfunction(l, mumble_traceback);
				lua_insert(l, 1);

				// Call our callback with our arguments and our traceback function
				if (lua_pcall(l, callargs, nresults, 1) != 0) {
					// Call errored, call OnError hook
					erroring = true;
					fprintf(stderr, "%s\n", lua_tostring(l, -1));
					mumble_hook_call(l, client, "OnError", 1);
					erroring = false;
				} else {
					// Call success
					const int nret = lua_gettop(l) - base;

					if (!returned) {
						// Only return values of first called hook
						returned = true;
						nreturns = nret;

						for (int i = 0; i < nreturns; i++) {
							// Insert all return values at a safe location for later
							lua_insert(l, top + argpos);
						}
					} else {
						// We already had a hook return values, so ignore and pop these
						lua_pop(l, nret);
					}
				}

				// Pop the traceback function
				lua_remove(l, 1);
			}
		}

		// Pop the key and value..
		lua_pop(l, 2);
	}

	// Pop the hook table
	lua_pop(l, 1);

	exit:

	// Call exit early, since mumble_hook_call removes the function called and its arguments from the stack
	//lua_stackguard_exit(l);

	// Remove original arguments from the stack
	for (int i = top; i > argpos; i--) {
		lua_remove(l, i);
	}

	// Stack will now contain any returns that we inserted above

	// Return how many results we pushed to the stack
	return nreturns;
}

MumbleUser* mumble_user_get(lua_State* l, MumbleClient* client, uint32_t session) {
	MumbleUser* user = NULL;

	mumble_pushref(l, client->users);
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
			user->data = mumble_ref(l);
			user->connected = false;
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
			user->listens = NULL;
		}
		luaL_getmetatable(l, METATABLE_USER);
		lua_setmetatable(l, -2);

		list_add(&client->user_list, session);

		lua_pushinteger(l, session);
		lua_pushvalue(l, -2); // Push a copy of the new user metatable
		lua_settable(l, -4); // Set the user metatable to where we store the table of users, using session as its index
	}
	
	lua_remove(l, -2); // Remove the clients users table from the stack
	lua_pop(l, 1); // Remove the user lua object from the stack, since all we wanted was the pointer

	return user;
}

void mumble_client_raw_get(lua_State* l, MumbleClient* client) {
	mumble_registry_pushref(l, MUMBLE_CLIENTS, client->self);
}

bool mumble_user_isnil(lua_State* l, MumbleClient* client, uint32_t session) {

	mumble_pushref(l, client->users);
	lua_pushinteger(l, session);
	lua_gettable(l, -2);

	bool isNil = lua_isnil(l, -1);

	lua_pop(l, 2);

	return isNil;
}

void mumble_user_raw_get(lua_State* l, MumbleClient* client, uint32_t session)
{
	mumble_registry_pushref(l, client->users, session);
}

void mumble_user_remove(lua_State* l, MumbleClient* client, uint32_t session)
{
	mumble_pushref(l, client->users);
		lua_pushinteger(l, session);
		lua_pushnil(l);
		lua_settable(l, -3);
	lua_pop(l, 1);
}

void mumble_channel_raw_get(lua_State* l, MumbleClient* client, uint32_t channel_id)
{
	mumble_registry_pushref(l, client->channels, channel_id);
}

MumbleChannel* mumble_channel_get(lua_State* l, MumbleClient* client, uint32_t channel_id)
{
	MumbleChannel* channel = NULL;

	mumble_pushref(l, client->channels);

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
			channel->data = mumble_ref(l);
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

		list_add(&client->channel_list, channel_id);

		lua_pushinteger(l, channel_id);
		lua_pushvalue(l, -2); // Push a copy of the new channel object
		lua_settable(l, -4); // Set the channel metatable to where we store the table of cahnnels, using channel_id as its index
	}
	
	lua_remove(l, -2); // Remove the clients channels table from the stack
	lua_pop(l, 1); // Remove the channel lua object from the stack, since all we wanted was the pointer

	return channel;
}

void mumble_channel_remove(lua_State* l, MumbleClient* client, uint32_t channel_id)
{
	mumble_pushref(l, client->channels);
		lua_pushinteger(l, channel_id);
		lua_pushnil(l);
		lua_settable(l, -3);
	lua_pop(l, 1);
}

int mumble_push_address(lua_State* l, ProtobufCBinaryData address)
{
	lua_newtable(l);
		uint8_t* bytes = (uint8_t*) address.data;
		uint64_t* addr = (uint64_t*) address.data;
		uint16_t* shorts = (uint16_t*) address.data;

		if (addr[0] != 0ULL || shorts[4] != 0 || shorts[5] != 0xFFFF) {
			char ipv6[INET6_ADDRSTRLEN];

			if (!inet_ntop(AF_INET6, address.data, ipv6, sizeof(ipv6))) {
				// Fallback
				sprintf(ipv6,"%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x",
					bytes[0], bytes[1], bytes[2], bytes[3],
					bytes[4], bytes[5], bytes[5], bytes[7],
					bytes[8], bytes[9], bytes[10], bytes[11],
					bytes[12], bytes[13], bytes[14], bytes[15]);
			}

			lua_pushboolean(l, true);
			lua_setfield(l, -2, "ipv6");
			lua_pushstring(l, ipv6);
			lua_setfield(l, -2, "string");
		} else {
			char ipv4[INET_ADDRSTRLEN];

			sprintf(ipv4, "%d.%d.%d.%d", bytes[12], bytes[13], bytes[14], bytes[15]);

			lua_pushboolean(l, true);
			lua_setfield(l, -2, "ipv4");
			lua_pushstring(l, ipv4);
			lua_setfield(l, -2, "string");
		}

		lua_newtable(l);
			for (uint32_t k = 0; k < address.len; k++) {
				lua_pushinteger(l, k+1);
				lua_pushinteger(l, address.data[k]);
				lua_settable(l, -3);
			}
		lua_setfield(l, -2, "data");

	return 1;
}

int mumble_handle_speaking_hooks(lua_State* l, MumbleClient* client, uint8_t buffer[], uint8_t codec, uint8_t target, uint32_t session)
{
	lua_stackguard_entry(l);

	int read = 0;
	int sequence = util_get_varint(buffer, &read);
	
	bool speaking = false;
	MumbleUser* user = mumble_user_get(l, client, session);

	int payload_len = 0;
	uint16_t frame_header = 0;

	if (codec == UDP_SPEEX || codec == UDP_CELT_ALPHA || codec == UDP_CELT_BETA) {
		frame_header = buffer[read++];
		payload_len = frame_header & 0x7F;
		speaking = ((frame_header & 0x80) == 0);
	} else if (codec == UDP_OPUS) {
		frame_header = util_get_varint(buffer + read, &read);
		payload_len = frame_header & 0x1FFF;
		speaking = ((frame_header & 0x2000) == 0);
	}

	bool state_change = false;
	bool one_frame = (user->speaking == false && speaking == false); // This will only be true if the user only sent exactly one audio packet

	if (user->speaking != speaking) {
		user->speaking = speaking;
		state_change = true;
	}

	if (one_frame || state_change && speaking) {
		mumble_user_raw_get(l, client, session);
		mumble_hook_call(l, client, "OnUserStartSpeaking", 1);
	}

	lua_newtable(l);
		lua_pushnumber(l, codec);
		lua_setfield(l, -2, "codec");
		lua_pushnumber(l, target);
		lua_setfield(l, -2, "target");
		lua_pushnumber(l, sequence);
		lua_setfield(l, -2, "sequence");
		mumble_user_raw_get(l, client, session);
		lua_setfield(l, -2, "user");
		lua_pushboolean(l, speaking);
		lua_setfield(l, -2, "speaking");
		lua_pushlstring(l, buffer + read, payload_len);
		lua_setfield(l, -2, "data");
		lua_pushinteger(l, frame_header);
		lua_setfield(l, -2, "frame_header");
	mumble_hook_call(l, client, "OnUserSpeak", 1);

	if (one_frame || state_change && !speaking) {
		mumble_user_raw_get(l, client, session);
		mumble_hook_call(l, client, "OnUserStopSpeaking", 1);
	}
	
	lua_stackguard_exit(l);
}

const luaL_Reg mumble[] = {
	{"connect", mumble_connect},
	{"loop", mumble_loop},
	{"stop", mumble_stop},
	{"gettime", mumble_getTime},
	{"getTime", mumble_getTime},
	{"getConnections", mumble_getConnections},
	{"getClients", mumble_getConnections},
	{NULL, NULL}
};

int luaopen_mumble(lua_State *l)
{
	signal(SIGPIPE, SIG_IGN);
	SSL_library_init();

	if (pipe(user_thread_pipe) != 0) {
		fprintf(stderr, "could not create user thread pipe\n");
		return 0;
	}

	lua_newtable(l);
	MUMBLE_REGISTRY = luaL_ref(l, LUA_REGISTRYINDEX);

	lua_newtable(l);
	MUMBLE_CLIENTS = mumble_ref(l);

	lua_newtable(l);
	MUMBLE_TIMER_REG = mumble_ref(l);

	lua_newtable(l);
	MUMBLE_THREAD_REG = mumble_ref(l);

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

		lua_newtable(l);
		{
			lua_pushinteger(l, 1);
			lua_setfield(l, -2, "LOW");
			lua_pushinteger(l, 2);
			lua_setfield(l, -2, "MEDIUM");
			lua_pushinteger(l, 3);
			lua_setfield(l, -2, "HIGH");
		}
		lua_setfield(l, -2, "quality");

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
		luaL_newmetatable(l, METATABLE_BANENTRY);
		{
			lua_pushvalue(l, -1);
			lua_setfield(l, -2, "__index");
		}
		luaL_register(l, NULL, mumble_banentry);

		// If you call the voice target metatable as a function it will return a new voice target object
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

		// Register thread metatable
		luaL_newmetatable(l, METATABLE_THREAD);
		{
			lua_pushvalue(l, -1);
			lua_setfield(l, -2, "__index");
		}
		luaL_register(l, NULL, mumble_thread);

		// If you call the voice target metatable as a function it will return a new voice target object
		lua_newtable(l);
		{
			lua_pushcfunction(l, mumble_thread_new);
			lua_setfield(l, -2, "__call");
		}
		lua_setmetatable(l, -2);
		lua_setfield(l, -2, "thread");

		// Register encoder metatable
		luaL_newmetatable(l, METATABLE_AUDIOSTREAM);
		{
			lua_pushvalue(l, -1);
			lua_setfield(l, -2, "__index");
		}
		luaL_register(l, NULL, mumble_audiostream);
		lua_setfield(l, -2, "audiostream");

		lua_rawgeti(l, LUA_REGISTRYINDEX, MUMBLE_REGISTRY);
		lua_setfield(l, -2, "registry");
	}

	// Break out of mumble.loop() when a SIGINT signal is received
	ev_signal_init(&mumble_signal, mumble_signal_event, SIGINT);
	ev_signal_start(EV_DEFAULT, &mumble_signal);

	user_thread_watcher.l = l;

	ev_io_init(&user_thread_watcher.io, mumble_thread_event, user_thread_pipe[0], EV_READ);
	ev_io_start(EV_DEFAULT, &user_thread_watcher.io);

	return 0;
}

int mumble_ref(lua_State *l)
{
	int ref;
	lua_rawgeti(l, LUA_REGISTRYINDEX, MUMBLE_REGISTRY);
	lua_pushvalue(l, -2);
	ref = luaL_ref(l, -2);
	lua_pop(l, 2);
	return ref;
}

void mumble_pushref(lua_State *l, int ref)
{
	lua_rawgeti(l, LUA_REGISTRYINDEX, MUMBLE_REGISTRY);
	lua_rawgeti(l, -1, ref);
	lua_remove(l, -2);
}

void mumble_unref(lua_State *l, int ref)
{
	lua_rawgeti(l, LUA_REGISTRYINDEX, MUMBLE_REGISTRY);
	luaL_unref(l, -1, ref);
	lua_pop(l, 1);
}

int mumble_registry_ref(lua_State *l, int t)
{
	int ref;
	mumble_pushref(l, t);
	lua_pushvalue(l, -2);
	ref = luaL_ref(l, -2);
	lua_pop(l, 2);
	return ref;
}

void mumble_registry_pushref(lua_State *l, int t, int ref)
{
	mumble_pushref(l, t);
	lua_rawgeti(l, -1, ref);
	lua_remove(l, -2);
}

void mumble_registry_unref(lua_State *l, int t, int ref)
{
	mumble_pushref(l, t);
	luaL_unref(l, -1, ref);
	lua_pop(l, 1);
}