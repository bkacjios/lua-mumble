#include "client/client.h"

#include "audio/audiocontext.h"
#include "audio/audiostream.h"
#include "audio/encoder.h"
#include "client/channel.h"
#include "client/user.h"
#include "defines.h"
#include "lua/luainc.h"
#include "lua/metatables.h"
#include "lua/registry.h"
#include "module.h"
#include "net/crypt.h"
#include "net/packet.h"
#include "net/varint.h"
#include "util/log.h"
#include "util/util.h"

#include <arpa/inet.h>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <vector>

void MumbleClient::onPingTimer(uv_timer_t* handle) {
	MumbleClient* client = (MumbleClient*) handle->data;
	lua_State *l = client->l;

	lua_stackguard_entry(l);

	client->ping();

	lua_stackguard_exit(l);
}

static const char* mumble_ssl_error(unsigned long err) {
	const char* error = ERR_reason_error_string(err);
	if (error == NULL) {
		return strerror(errno);
	} else {
		return error;
	}
}

LinkList<MumbleClient> MumbleClient::connections;

MumbleClient::MumbleClient(lua_State *l, uint64_t major, uint64_t minor, uint64_t patch)
	: l(l), version_major(major), version_minor(minor), version_patch(patch) {
	legacy = version_major <= 1 && version_minor < 5;
	memset(audio_output, 0, sizeof(audio_output));
}

void MumbleClient::init() {
	lua_newtable(l);
	hooks = LuaRegistry::ref(l);

	lua_newtable(l);
	users = LuaRegistry::ref(l);

	lua_newtable(l);
	channels = LuaRegistry::ref(l);

	lua_newtable(l);
	audio_streams = LuaRegistry::ref(l);

	uv_mutex_init(&main_mutex);
	uv_mutex_init(&inner_mutex);

	// Create a thread that buffers the reading of open audio files
	audio_buffer_thread_running = true;
	uv_thread_create(&audio_buffer_thread, MumbleClient::audioBufferThread, this);

	// Create a thread that handles the playback of audio
	audio_playback_thread_running = true;
	audio_playback_async_pending = false;
	audio_playback_last = uv_hrtime();
	audio_playback_next = audio_playback_last;
	uv_thread_create(&audio_playback_thread, MumbleClient::audioPlaybackThread, this);

	connect_timer = new uv_timer_t;
	uv_timer_init(uv_default_loop(), connect_timer);
	connect_timer->data = this;

	audio_encode_thread_running = true;
	uv_thread_create(&audio_encode_thread, MumbleClient::audioEncodeThread, this);
}

uint64_t MumbleClient::versionV1() const {
	return version_major << 16 | version_minor << 8 | version_patch;
}

uint64_t MumbleClient::versionV2() const {
	return version_major << 48 | version_minor << 32 | version_patch << 16;
}

uint64_t MumbleClient::pingUDPLegacy() {
	uint64_t timestamp = uv_hrtime();

	unsigned char packet[UDP_BUFFER_MAX];
	packet[0] = LEGACY_PROTO_UDP_PING << 5;

	int len = 1 + VarInt::encode(packet + 1, timestamp);

	mumble_log(LOG_TRACE, "[UDP] Sending legacy ping packet: %p", &packet);
	sendUDP(packet, len);
	return timestamp;
}

uint64_t MumbleClient::pingUDPProtobuf() {
	MumbleUDP__Ping ping = MUMBLE_UDP__PING__INIT;

	uint64_t timestamp = uv_hrtime();

	ping.timestamp = timestamp;

	unsigned char packet[UDP_BUFFER_MAX];
	packet[0] = PROTO_UDP_PING;

	int len = 1 + mumble_udp__ping__pack(&ping, packet + 1);

	mumble_log(LOG_TRACE, "[UDP] Sending %s: %p", "MumbleUDP.Ping", &ping);
	sendUDP(packet, len);
	return timestamp;
}

void MumbleClient::pingTCP() {
	MumbleProto__Ping ping = MUMBLE_PROTO__PING__INIT;

	uint64_t timestamp = uv_hrtime();

	ping.has_timestamp = true;
	ping.timestamp = timestamp;

	ping.has_tcp_packets = true;
	ping.tcp_packets = tcp_packets;

	ping.has_tcp_ping_avg = true;
	ping.tcp_ping_avg = tcp_ping_avg;

	ping.has_tcp_ping_var = true;
	ping.tcp_ping_var = tcp_ping_var;

	ping.has_good = true;
	ping.good = crypt->getGood();

	ping.has_late = true;
	ping.late = crypt->getLate();

	ping.has_lost = true;
	ping.lost = crypt->getLost();

	ping.has_resync = true;
	ping.resync = resync;

	ping.has_udp_packets = true;
	ping.udp_packets = udp_packets;

	ping.has_udp_ping_avg = true;
	ping.udp_ping_avg = udp_ping_avg;

	ping.has_udp_ping_var = true;
	ping.udp_ping_var = udp_ping_var;

	lua_newtable(l);
	// Push the timestamp we are sending to the server
	lua_pushinteger(l, ping.timestamp);
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
	hookCall("OnPingTCP", 1);

	send(PACKET_PING, &ping);
}

void MumbleClient::pingUDP() {
	// Like Mumble, stop pinging over UDP once we've given up on it, so the server doesn't route our voice back to UDP
	if (crypt->isValid() && !tcp_udp_tunnel) {
		uint64_t timestamp;
		if (legacy) {
			timestamp = pingUDPLegacy();
		} else {
			timestamp = pingUDPProtobuf();
		}

		lua_newtable(l);
		lua_pushinteger(l, timestamp);
		lua_setfield(l, -2, "timestamp");
		hookCall("OnPingUDP", 1);
	}
}

void MumbleClient::updateUDPMode(uint32_t remote_good) {
	uint32_t local_good = crypt->getGood();

	// Mumble's ServerHandler: give up on UDP once either side has decrypted nothing 20 seconds after connecting,
	// and go back to it once both sides have decrypted more than a few packets
	if ((remote_good == 0 || local_good == 0) && !tcp_udp_tunnel && uv_now(uv_default_loop()) - time > UDP_TCP_FALLBACK_MS) {
		if (remote_good == 0 && local_good == 0) {
			mumble_log(LOG_WARN, "[UDP] UDP packets cannot be sent to or received from the server. Switching to TCP mode.");
		} else if (remote_good == 0) {
			mumble_log(LOG_WARN, "[UDP] UDP packets cannot be sent to the server. Switching to TCP mode.");
		} else {
			mumble_log(LOG_WARN, "[UDP] UDP packets cannot be received from the server. Switching to TCP mode.");
		}
		tcp_udp_tunnel = true;
	} else if (tcp_udp_tunnel && remote_good > 3 && local_good > 3) {
		mumble_log(LOG_WARN, "[UDP] UDP packets can be sent to and received from the server. Switching back to UDP mode.");
		tcp_udp_tunnel = false;
	}
}

void MumbleClient::ping() {
	pingTCP();
	pingUDP();
}

void MumbleClient::updatePingUDP(uint64_t timestamp, bool udp) {
	uint64_t now = uv_hrtime();

	double delay = (double) (now - timestamp) / 1e6;

	double n = udp_packets + 1;
	udp_packets = n;
	udp_ping_avg = udp_ping_avg * (n - 1) / n + delay / n;
	udp_ping_var = pow(fabs(delay - udp_ping_avg), 2);

	lua_newtable(l);
	lua_pushinteger(l, timestamp);
	lua_setfield(l, -2, "timestamp");
	lua_pushnumber(l, delay);
	lua_setfield(l, -2, "ping");
	lua_pushnumber(l, udp_ping_avg);
	lua_setfield(l, -2, "average");
	lua_pushnumber(l, udp_ping_var);
	lua_setfield(l, -2, "deviation");
	hookCall("OnPongUDP", 1);
}

double MumbleClient::updatePingTCP(uint64_t timestamp) {
	uint64_t now = uv_hrtime();
	double delay = (double)(now - timestamp) / 1e6;

	double n = tcp_packets + 1;
	tcp_packets = n;
	tcp_ping_avg = tcp_ping_avg * (n - 1) / n + delay / n;
	tcp_ping_var = pow(fabs(delay - tcp_ping_avg), 2);

	return delay;
}

void MumbleClient::handleUDPPacket(unsigned char* unencrypted, ssize_t size, bool udp) {
	uint8_t header = unencrypted[0];

	if (legacy) {
		uint8_t id = (header >> 5) & 0x7;

		switch (id) {
		case LEGACY_PROTO_UDP_PING: {
			int read = 1;
			uint64_t timestamp = VarInt::decode(unencrypted + read, &read);

			mumble_log(LOG_TRACE, "[UDP] Received legacy ping packet (size=%u, id=%u, timestamp=%lu)", size, id, timestamp);

			updatePingUDP(timestamp, udp);
			return;
		}
		case LEGACY_UDP_OPUS:
		case LEGACY_UDP_SPEEX:
		case LEGACY_UDP_CELT_ALPHA:
		case LEGACY_UDP_CELT_BETA: {
			uint8_t target = header >> 0x1F;

			int read = 1;
			int sender = VarInt::decode(unencrypted + read, &read);

			mumble_log(LOG_TRACE, "[UDP] Received legacy audio packet (size=%u, id=%u, target=%u, session=%u)", size, id, target, sender);
			handleSpeakingHooksLegacy(unencrypted + read, id, target, sender);
			return;
		}
		default: {
			mumble_log(LOG_DEBUG, "[UDP] Received unhandled legacy packet: %x", unencrypted);
			break;
		}
		}
	} else {
		switch (header) {
		case PROTO_UDP_AUDIO: {
			MumbleUDP__Audio *audio = mumble_udp__audio__unpack(NULL, size - 1, unencrypted + 1);
			if (audio != NULL) {
				mumble_log(LOG_TRACE, "[UDP] Received %s: %p", audio->base.descriptor->name, audio);
				handleSpeakingHooksProtobuf(audio, audio->sender_session);
				mumble_udp__audio__free_unpacked(audio, NULL);
			} else {
				mumble_log(LOG_WARN, "[UDP] Error unpacking UDP audio packet");
			}
			return;
		}
		case PROTO_UDP_PING: {
			MumbleUDP__Ping *ping = mumble_udp__ping__unpack(NULL, size - 1, unencrypted + 1);
			if (ping != NULL) {
				mumble_log(LOG_TRACE, "[UDP] Received %s: %p", ping->base.descriptor->name, ping);
				updatePingUDP(ping->timestamp, udp);
				mumble_udp__ping__free_unpacked(ping, NULL);
			} else {
				mumble_log(LOG_WARN, "[UDP] Error unpacking ping packet");
			}
			return;
		}
		default: {
			mumble_log(LOG_DEBUG, "[UDP] Received unhandled protobuf packet: %x", unencrypted);
			break;
		}
		}
	}
}

void MumbleClient::onReadUDP(uv_udp_t* handle, ssize_t nread, const uv_buf_t* buf, const struct sockaddr* addr, unsigned flags) {
	MumbleClient* client = (MumbleClient*) handle->data;

	if (nread > 0) {
		std::vector<unsigned char> unencrypted(nread, 0);

		if (client->crypt->isValid() && client->crypt->decrypt((unsigned char*) buf->base, unencrypted.data(), nread)) {
			client->handleUDPPacket(unencrypted.data(), nread - 4, true);
		} else {
			mumble_log(LOG_ERROR, "[UDP] Unable to decrypt UDP packet: %x", &buf->base);
		}
	} else if (nread < 0) {
		mumble_log(LOG_ERROR, "[UDP] Error receiving UDP packet: %s", uv_strerror(nread));
	}

	if (buf->base) {
		::free(buf->base);
	}
}

static void alloc_buffer(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf) {
	buf->base = (char*) malloc(suggested_size);
	buf->len = suggested_size;
}

void MumblePacket::reset() {
	type = 0;
	length = 0;
	header_len = 0;
	if (header) {
		free(header);
		header = NULL;
	}
	body_len = 0;
	if (body) {
		free(body);
		body = NULL;
	}
}

bool MumbleClient::handleSSLReadError(int ret) {
	int err = SSL_get_error(ssl, ret);

	if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
		return true; // Retry when SSL is ready
	}

	disconnect(mumble_ssl_error(ERR_get_error()), false);
	return false;
}

void MumbleClient::onReadableTCP(uv_poll_t* handle, int status, int events) {
	MumbleClient* client = (MumbleClient*) handle->data;

	if (status < 0) {
		client->disconnect(uv_strerror(status), false);
		return;
	}

	if (!(events & UV_READABLE)) return;

	// A packet may arrive over several read events, so its state lives on the client.
	// SSL can also buffer more than one packet per socket read, so keep going until it needs more data.
	while (client->connected) {
		MumblePacket &packet = client->tcp_read_packet;

		// Setup for a new packet to be read
		if (!packet.header) {
			packet.header_len = 0;
			packet.header = (uint8_t*) malloc(sizeof(uint8_t) * PACKET_HEADER_SIZE);
			packet.body_len = 0;
			packet.body = NULL;
		}

		// Read the header if not fully read
		if (packet.header_len < PACKET_HEADER_SIZE) {
			int ret = SSL_read(client->ssl,
			                   packet.header + packet.header_len,
			                   PACKET_HEADER_SIZE - packet.header_len);

			if (ret <= 0) {
				if (!client->handleSSLReadError(ret)) {
					packet.reset();
				}
				return;
			}

			packet.header_len += ret;

			if (packet.header_len < PACKET_HEADER_SIZE) continue;

			// Header complete, parse packet type and length
			packet.type = ntohs(*(uint16_t *)packet.header);
			packet.length = ntohl(*(uint32_t *)(packet.header + 2));

			// Allocate a buffer for the message body (at least one byte, so an empty body is not NULL)
			packet.body = (uint8_t*) malloc(sizeof(uint8_t) * (packet.length > 0 ? packet.length : 1));
			if (!packet.body) {
				mumble_log(LOG_ERROR, "failed to create packet buffer: %s", strerror(errno));
				packet.reset();
				client->disconnect("failed to create packet buffer", false);
				return;
			}
		}

		// Read the message body
		if (packet.body_len < packet.length) {
			int ret = SSL_read(client->ssl,
			                   packet.body + packet.body_len,
			                   packet.length - packet.body_len);

			if (ret <= 0) {
				if (!client->handleSSLReadError(ret)) {
					packet.reset();
				}
				return;
			}

			packet.body_len += ret;

			if (packet.body_len < packet.length) continue;
		}

		// Body complete, process the packet
		if (!client->handlePacket(&packet)) {
			// Unknown packets are still read in full, to stay in sync with the stream
			mumble_log(LOG_DEBUG, "received unknown packet type %u", packet.type);
		}
		packet.reset();
	}
}

void MumbleClient::onWritableTCP(uv_poll_t* handle, int status, int events) {
	MumbleClient* client = (MumbleClient*) handle->data;

	if (status < 0) {
		client->disconnect(uv_strerror(status), false);
		return;
	}

	if (events & UV_WRITABLE && !client->connected) {
		int ret = SSL_connect(client->ssl);
		if (ret == 1 && SSL_is_init_finished(client->ssl)) {
			client->connected = true;
			// Like Mumble, try sending voice over UDP once we're connected, until our TCP pings say it isn't working
			client->tcp_udp_tunnel = false;

			mumble_log(LOG_INFO, "%s[%d] connected to server %s:%d", METATABLE_CLIENT, client->self, client->serverAddress().c_str(), client->port);

			// Set the connected flag and trigger any connection callback
			client->hookCall("OnConnect", 0);

			uv_poll_stop(&client->ssl_poll);
			uv_poll_start(&client->ssl_poll, UV_READABLE, MumbleClient::onReadableTCP);
		} else {
			int error = SSL_get_error(client->ssl, ret);
			if (error != SSL_ERROR_WANT_READ && error != SSL_ERROR_WANT_WRITE) {
				const char* message = (char*) mumble_ssl_error(error);
				mumble_log(LOG_ERROR, "SSL handshake failed: %s", message);
				client->disconnect(message, false);
			}
		}
	}
}

void MumbleClient::onConnectedTCP(uv_connect_t *req, int status) {
	uv_tcp_t* tcp_handle = (uv_tcp_t*) req->handle;
	MumbleClient* client = (MumbleClient*) tcp_handle->data;

	if (status < 0) {
		client->disconnect(uv_strerror(status), false);
		return;
	}

	// Create a new uv_poll_t to monitor the socket for SSL handshake
	uv_poll_init(uv_default_loop(), &client->ssl_poll, client->socket_tcp_fd);
	client->openHandle((uv_handle_t*) &client->ssl_poll);

	// Start polling the socket for writability for the handshake
	uv_poll_start(&client->ssl_poll, UV_WRITABLE, MumbleClient::onWritableTCP);
}

int MumbleClient::connect(lua_State *l) {
	this->l = l;

	const char* host = luaL_checkstring(l, 2);
	int port = luaL_checkinteger(l, 3);
	const char* certificate = luaL_checkstring(l, 4);
	const char* key = luaL_checkstring(l, 5);

	if (connecting) {
		lua_pushboolean(l, false);
		lua_pushstring(l, "already connected");
		return 2;
	}

	if (disconnecting || closing_handles > 0) {
		// Our last connection is still closing, so connect once it's done.
		// Anything that goes wrong then is reported to the OnDisconnect hook.
		pending_connect = PendingConnect{host, (uint16_t) port, certificate, key};
		// Stay alive until it starts
		if (alive <= LUA_NOREF) {
			lua_pushvalue(l, 1);
			alive = LuaRegistry::ref(l);
		}
		lua_pushboolean(l, true);
		return 1;
	}

	bool failed;
	{
		// Scoped, so the error string is freed before we return to Lua
		std::string error = start(1, host, port, certificate, key);
		failed = !error.empty();
		if (failed) lua_pushstring(l, error.c_str());
	}
	if (failed) {
		lua_pushboolean(l, false);
		lua_insert(l, -2);
		return 2;
	}

	lua_pushboolean(l, true);
	return 1;
}

std::string MumbleClient::start(int index, const char *host, uint16_t port, const char *certificate, const char *key) {
	char port_str[6];
	snprintf(port_str, sizeof(port_str), "%u", port);

	// Keep ourself alive until this connection has completely closed
	if (alive <= LUA_NOREF) {
		lua_pushvalue(l, index);
		alive = LuaRegistry::ref(l);
	}

	std::string error = [&]() -> std::string {
		this->host = strdup(host);
		this->port = port;
		connection_id++;
		time = uv_now(uv_default_loop());

		// TCP Connection

		ssl_context = SSL_CTX_new(SSLv23_client_method());

		if (ssl_context == NULL) {
			return std::string("could not create SSL context: ") + mumble_ssl_error(ERR_get_error());
		}

		if (!SSL_CTX_use_certificate_chain_file(ssl_context, certificate) ||
		        !SSL_CTX_use_PrivateKey_file(ssl_context, key, SSL_FILETYPE_PEM) ||
		        !SSL_CTX_check_private_key(ssl_context)) {
			return std::string("could not load certificate and/or key file: ") + mumble_ssl_error(ERR_get_error());
		}

		struct addrinfo hint_tcp;
		memset(&hint_tcp, 0, sizeof(hint_tcp));
		hint_tcp.ai_family = AF_UNSPEC;
		hint_tcp.ai_socktype = SOCK_STREAM;

		int err = getaddrinfo(host, port_str, &hint_tcp, &server_host_tcp);

		if (err != 0) {
			return std::string("could not parse server address: ") + gai_strerror(err);
		}

		// UDP Connection

		crypt = new (std::nothrow) CryptState();

		if (crypt == NULL) {
			return "could not initialize ocb cryptstate";
		}

		struct addrinfo hint_udp;
		memset(&hint_udp, 0, sizeof(hint_udp));
		hint_udp.ai_family = AF_UNSPEC;
		hint_udp.ai_socktype = SOCK_DGRAM;

		err = getaddrinfo(host, port_str, &hint_udp, &server_host_udp);

		if (err != 0) {
			return std::string("could not parse server address: ") + gai_strerror(err);
		}

		ssl = SSL_new(ssl_context);

		if (ssl == NULL) {
			return std::string("could not create SSL object: ") + mumble_ssl_error(ERR_get_error());
		}

		// Our encoder and decoder are kept across connections, just starting over each time
		if (encoder == nullptr) {
			MumbleOpusEncoder *wrapper = lua_newclass<MumbleOpusEncoder>(l);
			luaL_getmetatable(l, METATABLE_ENCODER);
			lua_setmetatable(l, -2);
			encoder_ref = LuaRegistry::ref(l);

			err = wrapper->init(AUDIO_SAMPLE_RATE, AUDIO_PLAYBACK_CHANNELS);
			if (err != OPUS_OK) {
				return std::string("could not initialize opus encoder: ") + opus_strerror(err);
			}
			encoder = wrapper;
		} else {
			encoder->reset();
		}

		if (decoder == NULL) {
			decoder = opus_decoder_create(AUDIO_SAMPLE_RATE, AUDIO_PLAYBACK_CHANNELS, &err);
			if (err != OPUS_OK) {
				decoder = NULL;
				return std::string("could not initialize opus decoder: ") + opus_strerror(err);
			}
		} else {
			opus_decoder_ctl(decoder, OPUS_RESET_STATE);
		}

		uv_loop_t* loop = uv_default_loop();

		uv_tcp_init(loop, &socket_tcp);
		openHandle((uv_handle_t*) &socket_tcp);
		uv_udp_init(loop, &socket_udp);
		openHandle((uv_handle_t*) &socket_udp);
		uv_async_init(loop, &audio_playback_async, MumbleClient::onAudioPlaybackAsync);
		openHandle((uv_handle_t*) &audio_playback_async);

		err = uv_tcp_connect(&tcp_connect_req, &socket_tcp, server_host_tcp->ai_addr, MumbleClient::onConnectedTCP);
		if (err) {
			return std::string("could not connect to tcp address: ") + uv_strerror(err);
		}

		err = uv_fileno((uv_handle_t*) &socket_tcp, &socket_tcp_fd);
		if (err) {
			return std::string("failed getting file descriptor for tcp socket: ") + uv_strerror(err);
		}

		fcntl(socket_tcp_fd, F_SETFL, O_NONBLOCK);
		SSL_set_mode(ssl, SSL_MODE_ASYNC | SSL_MODE_ENABLE_PARTIAL_WRITE);

		if (SSL_set_fd(ssl, socket_tcp_fd) == 0) {
			return std::string("could not set SSL file descriptor: ") + mumble_ssl_error(ERR_get_error());
		}

		err = uv_udp_connect(&socket_udp, server_host_udp->ai_addr);
		if (err) {
			return std::string("could not connect to udp address: ") + uv_strerror(err);
		}

		uv_udp_recv_start(&socket_udp, alloc_buffer, MumbleClient::onReadUDP);

		// Create a timer to constantly send out pings to the server
		uv_timer_init(loop, &ping_timer);
		openHandle((uv_handle_t*) &ping_timer);
		return "";
	}();

	if (!error.empty()) {
		closeConnection();
		return error;
	}

	// Register ourself in the list of connected clients
	lua_pushvalue(l, index);
	self = LuaRegistry::refIn(l, LuaRegistry::clients);
	connecting = true;
	connections.add(self, this);

	mumble_log(LOG_INFO, "%s[%d] connecting to host %s:%d", METATABLE_CLIENT, self, host, port);
	return "";
}

void MumbleClient::openHandle(uv_handle_t *handle) {
	handle->data = this;
	connection_handles.push_back(handle);
}

void MumbleClient::closeConnection() {
	// Close in reverse, so the SSL poll is closed before the TCP socket it watches
	for (auto handle = connection_handles.rbegin(); handle != connection_handles.rend(); ++handle) {
		if (!uv_is_closing(*handle)) {
			closing_handles++;
			uv_close(*handle, MumbleClient::onHandleClosed);
		}
	}
	connection_handles.clear();

	freeConnection();

	if (closing_handles == 0) {
		finishClosing();
	}
}

void MumbleClient::onHandleClosed(uv_handle_t *handle) {
	MumbleClient *client = (MumbleClient*) handle->data;
	if (--client->closing_handles == 0) {
		client->finishClosing();
	}
}

void MumbleClient::finishClosing() {
	if (pending_connect) {
		// Start it on the next loop iteration, so a connect that keeps failing can't recurse through OnDisconnect
		uv_timer_start(connect_timer, MumbleClient::onConnectTimer, 0, 0);
		return;
	}
	releaseIfIdle();
}

void MumbleClient::onConnectTimer(uv_timer_t *handle) {
	MumbleClient *client = (MumbleClient*) handle->data;
	lua_State *l = client->l;

	if (!client->pending_connect || client->connecting || client->closing_handles > 0) {
		return;
	}

	PendingConnect next = *client->pending_connect;
	client->pending_connect.reset();

	lua_stackguard_entry(l);

	LuaRegistry::push(l, client->alive);
	int index = lua_gettop(l);

	std::string error = client->start(index, next.host.c_str(), next.port, next.certificate.c_str(), next.key.c_str());
	if (!error.empty()) {
		mumble_log(LOG_INFO, "%s: %p failed to connect: %s", METATABLE_CLIENT, client, error.c_str());

		// Report it like any connection that failed. Hooks need us registered,
		// and a connect() from the hook waits, just like it does in disconnect().
		lua_pushvalue(l, index);
		client->self = LuaRegistry::refIn(l, LuaRegistry::clients);
		client->disconnecting = true;
		lua_pushstring(l, error.c_str());
		client->hookCall("OnDisconnect", 1);
		client->disconnecting = false;
		LuaRegistry::unrefIn(l, LuaRegistry::clients, &client->self);

		client->finishClosing();
	}

	lua_pop(l, 1); // Our userdata

	lua_stackguard_exit(l);
}

void MumbleClient::releaseIfIdle() {
	if (!connecting && closing_handles == 0 && !pending_connect && alive > LUA_NOREF) {
		LuaRegistry::unref(l, &alive);
	}
}

// Bits per second used sending audio at this bitrate, in packets of `frames` 10ms frames (from Mumble's AudioInput)
static int getNetworkBandwidth(int bitrate, int frames) {
	// Bytes per packet: IP (20) + UDP (8) + crypt header (4) + type/target (1) + session (2) + per-frame headers.
	// Always leave room for tunneling through TCP (a bigger header, plus our own TCP packet header),
	// since we can fall back to it at any time when UDP drops packets.
	int overhead = 20 + 8 + 4 + 1 + 2 + 12 + frames;

	// 100 / frames packets per second, 8 bits per byte
	return bitrate + overhead * 800 / frames;
}

void MumbleClient::adjustAudioBandwidth() {
	int frames = audio_frames / 10;
	uint32_t maxbitrate = max_bandwidth;
	opus_int32 bitrate;
	encoder->get(OPUS_GET_BITRATE_REQUEST, &bitrate);
	opus_int32 original_bitrate = bitrate;

	if (maxbitrate == (uint32_t) -1) {
		// No limit
	} else if ((uint32_t) getNetworkBandwidth(bitrate, frames) > maxbitrate) {
		if ((frames <= 4) && (maxbitrate <= 32000))
			frames = 4;
		else if ((frames == 1) && (maxbitrate <= 64000))
			frames = 2;
		else if ((frames == 2) && (maxbitrate <= 48000))
			frames = 4;

		if ((uint32_t) getNetworkBandwidth(bitrate, frames) > maxbitrate) {
			do {
				bitrate -= 1000;
			} while ((bitrate > 8000) && ((uint32_t) getNetworkBandwidth(bitrate, frames) > maxbitrate));
		}
	}
	if (bitrate < 8000) {
		bitrate = 8000;
	}

	// Bigger packets alone can be enough to fit, so check both
	if (bitrate != original_bitrate || (uint32_t) frames * 10 != audio_frames) {
		mumble_log(LOG_WARN, "Server maximum network bandwidth is only %d kbit/s. Audio quality auto-adjusted to %d kbit/s (%d ms)", maxbitrate / 1000, bitrate / 1000, frames * 10);
		audio_frames = frames * 10;
		encoder->set(OPUS_SET_BITRATE_REQUEST, bitrate);
	}
}

void MumbleClient::freeConnection() {
	tcp_read_packet.reset();

	if (host) {
		::free(host);
		host = NULL;
	}
	if (crypt) {
		delete crypt;
		crypt = NULL;
	}

	if (ssl) {
		SSL_shutdown(ssl);
		SSL_free(ssl);
		ssl = NULL;
	}

	if (ssl_context) {
		SSL_CTX_free(ssl_context);
		ssl_context = NULL;
	}

	if (server_host_udp) {
		freeaddrinfo(server_host_udp);
		server_host_udp = NULL;
	}

	if (server_host_tcp) {
		freeaddrinfo(server_host_tcp);
		server_host_tcp = NULL;
	}

	resetConnectionState();
}

void MumbleClient::resetConnectionState() {
	synced = false;
	session = 0;
	legacy = version_major <= 1 && version_minor < 5;
	max_bandwidth = 0;
	socket_tcp_fd = -1;

	tcp_packets = 0;
	tcp_ping_avg = 0;
	tcp_ping_var = 0;
	tcp_udp_tunnel = true;

	udp_packets = 0;
	udp_ping_avg = 0;
	udp_ping_var = 0;
	resync = 0;

	recording = false;
	audio_sequence = 0;
	audio_stream_active = false;

	// Anything still queued was for this connection
	audio_encode_queue.clear();
	audio_send_queue.clear();
}

void MumbleClient::shutdown() {
	if (shut_down) return;
	shut_down = true;

	if (audio_buffer_thread) {
		uv_mutex_lock(&main_mutex);
		audio_buffer_thread_running = false;
		uv_mutex_unlock(&main_mutex);
		uv_thread_join(&audio_buffer_thread);
	}

	if (audio_playback_thread) {
		uv_mutex_lock(&main_mutex);
		audio_playback_thread_running = false;
		uv_mutex_unlock(&main_mutex);
		uv_thread_join(&audio_playback_thread);
	}

	shutdownAudioQueues();

	// Audio buffers may be collected after us, so they must forget about us
	for (auto &node : audio_pipes) {
		node.data->context->client = nullptr;
	}
	audio_pipes.clear();

	if (decoder != NULL) {
		opus_decoder_destroy(decoder);
		decoder = NULL;
	}

	// The timer is heap allocated, so it can finish closing after we're gone.
	// mumble.stop() may have already closed it, without a callback to free it, so then we can only let it go.
	if (connect_timer && !uv_is_closing((uv_handle_t*) connect_timer)) {
		uv_close((uv_handle_t*) connect_timer, [](uv_handle_t *handle) {
			delete (uv_timer_t*) handle;
		});
	}
	connect_timer = nullptr;

	uv_mutex_destroy(&main_mutex);
	uv_mutex_destroy(&inner_mutex);
}

void MumbleClient::cleanup() {
	uv_mutex_lock(&main_mutex);
	// Cleanup any active audio transmissions
	for (auto current = stream_list.begin(); current != stream_list.end();) {
		auto next = std::next(current);
		current->data->unreference(l);
		current = next;
	}
	uv_mutex_unlock(&main_mutex);

	// Cleanup our user objects
	for (auto current = user_list.begin(); current != user_list.end();) {
		auto next = std::next(current);
		removeUser(current->index);
		current = next;
	}

	// Cleanup our channel objects
	for (auto current = channel_list.begin(); current != channel_list.end();) {
		auto next = std::next(current);
		removeChannel(current->index);
		current = next;
	}

	if (self > LUA_REFNIL) {
		// Remove from the connected clients list
		connections.remove(self);
		LuaRegistry::unrefIn(l, LuaRegistry::clients, &self);
	}
}

void MumbleClient::destroy() {
	disconnect("garbage collected", true);
	shutdown();

	LuaRegistry::unref(l, &hooks);
	LuaRegistry::unref(l, &users);
	LuaRegistry::unref(l, &channels);
	LuaRegistry::unref(l, &audio_streams);
	LuaRegistry::unref(l, &encoder_ref);
}

void MumbleClient::disconnect(const char* reason, bool garbagecollected) {
	if (!connecting) {
		mumble_log(LOG_TRACE, "mumble.client: %p attempted to disconnect while not connected (%s)", this, reason);
		return;
	}

	connecting = false;
	disconnecting = true;

	// Under main_mutex, so the playback thread can't signal our playback async once we start closing it
	uv_mutex_lock(&main_mutex);
	connected = false;
	uv_mutex_unlock(&main_mutex);

	lua_stackguard_entry(l);

	if (!garbagecollected) {
		// Only call "OnDisconnect" hook if we weren't garbage collected.
		// A connect() from the hook is queued until we've finished closing.
		mumble_log(LOG_INFO, "%s[%d] disconnected from server: %s", METATABLE_CLIENT, self, reason);
		lua_pushstring(l, reason);
		hookCall("OnDisconnect", 1);
	}

	cleanup();
	disconnecting = false;
	closeConnection();

	lua_stackguard_exit(l);
}

static bool erroring = false;

int MumbleClient::hookCall(const char* hook, int nargs) {
	return hookCallRet(hook, nargs, 0);
}

void MumbleClient::addHook(const char* hook, const char* name, int index) {
	// Push our reference for the hooks table
	LuaRegistry::push(l, hooks);

	// Get the callback table for this hook
	lua_getfield(l, -1, hook);

	if (!lua_istable(l, -1)) {
		// No callback table exists, create one
		lua_pop(l, 1);
		lua_newtable(l);
		lua_setfield(l, -2, hook);
		lua_getfield(l, -1, hook);
	}

	// Push our callback value and add it to our callback table
	lua_pushvalue(l, index);
	lua_setfield(l, -2, name);

	lua_pop(l, 2); // Pop callback table and hook table
}

void MumbleClient::removeHook(const char* hook, const char* name) {
	// Push our reference for the hooks table
	LuaRegistry::push(l, hooks);

	// Get the callback table for this hook
	lua_getfield(l, -1, hook);

	if (lua_istable(l, -1)) {
		// Remove the field from the hook table by pushing a nil to it
		lua_pushnil(l);
		lua_setfield(l, -2, name);
	}

	lua_pop(l, 2); // Pop callback table (or nil) and hook table
}

int MumbleClient::hookCallRet(const char* hook, int nargs, int nresults) {
	if (self <= LUA_NOREF) {
		lua_pop(l, nargs); // Just pop whatever we expected to get popped
		mumble_log(LOG_DEBUG, "unreferenced %s: %p hook \"%s\" ignored", METATABLE_CLIENT, this, hook);
		return 0;
	}

	mumble_log(LOG_TRACE, "%s: %p calling hook \"%s\" with %d args", METATABLE_CLIENT, this, hook, nargs);

	const int top = lua_gettop(l);
	const int callargs = nargs + 1;
	const int argpos = top - nargs;

	// Where returned values get moved to, below the hook iteration state
	const int retpos = top + 1;
	int nreturns = 0;

	// Get hook table
	LuaRegistry::push(l, hooks);

	// Get the table of callbacks
	lua_getfield(l, -1, hook);

	// Remove hook table from stack
	lua_remove(l, -2);

	// if getfield returned nil OR the returned value is NOT a table..
	if (lua_isnil(l, -1) == 1 || lua_istable(l, -1) == 0) {
		// Pop the nil or nontable value
		lua_pop(l, 1);
	} else {
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
				pushSelf();

				for (int i = 1; i <= nargs; i++) {
					// Push a copy of the argument
					lua_pushvalue(l, argpos + i);
				}

				// Call
				if (erroring == true) {
					// If the user errors within the OnError hook, PANIC
					lua_call(l, callargs, 0);
				} else {
					// Place traceback immediately before the function we're calling
					const int base = lua_gettop(l) - callargs; // index of function
					lua_pushcfunction(l, mumble_traceback);
					lua_insert(l, base); // ... err func args

					// NOTE: nresults may be LUA_MULTRET
					if (lua_pcall(l, callargs, nresults, base) != 0) {
						// Call errored, call OnError hook
						erroring = true;
						mumble_log(LOG_ERROR, "%s", lua_tostring(l, -1));
						hookCall("OnError", 1);
						erroring = false;

						// OnError consumed the error message, so only the traceback function remains
						lua_remove(l, base);
					} else {
						// Success: results are on top, traceback still at 'base'
						const int after = lua_gettop(l);
						const int nret = after - base; // actual number of returns (works for LUA_MULTRET)

						// Remove the traceback function first
						lua_remove(l, base);

						if (nret > 0 && !lua_isnil(l, -nret)) {
							// First non-nil return wins, move results below the callback table
							for (int i = 0; i < nret; i++) {
								lua_insert(l, retpos);
							}
							nreturns = nret;
						} else {
							// Returned nothing or nil, discard and continue
							lua_pop(l, nret);
						}
					}
				}
			}

			if (nreturns > 0) {
				// Pop the key, value and key copy, then stop calling the remaining hooks
				lua_pop(l, 3);
				break;
			}

			// Pop the key and value..
			lua_pop(l, 2);
		}

		// Pop the hook table
		lua_pop(l, 1);
	}

	// Remove original arguments from the stack and keep results on top
	for (int i = 0; i < nargs; ++i) {
		lua_remove(l, argpos + 1);
	}

	// Return how many results we pushed to the stack
	return nreturns;
}

MumbleUser* MumbleClient::getUser(uint32_t session) {
	MumbleUser* user = NULL;

	LuaRegistry::push(l, users);
	lua_pushinteger(l, session);
	lua_gettable(l, -2);

	if (lua_isuserdata(l, -1) == 1) {
		user = lua_toclass<MumbleUser>(l, -1);
	} else {
		lua_pop(l, 1); // Pop whatever was in the table before, most likely nil

		lua_newtable(l);
		int data = LuaRegistry::refIn(l, LuaRegistry::data);

		user = lua_newclass<MumbleUser>(l, this, session, data);
		luaL_getmetatable(l, METATABLE_USER);
		lua_setmetatable(l, -2);

		user_list.add(session, user);
		mumble_log(LOG_TRACE, "added user session: %u (user=%p)", session, user);

		lua_pushinteger(l, session);
		lua_pushvalue(l, -2); // Push a copy of the new user metatable
		lua_settable(l, -4); // Set the user metatable to where we store the table of users, using session as its index
	}

	lua_remove(l, -2); // Remove the clients users table from the stack
	lua_pop(l, 1); // Remove the user lua object from the stack, since all we wanted was the pointer

	return user;
}

MumbleChannel* MumbleClient::findChannel(const char *path) {
	// Paths are relative to the root channel, which always has an id of 0
	MumbleChannel *root = channel_list.get(0);
	if (root == NULL) {
		return NULL;
	}
	return root->find(path);
}

void MumbleClient::pushSelf() {
	LuaRegistry::pushFrom(l, LuaRegistry::clients, self);
}

void MumbleClient::pushUser(uint32_t session) {
	LuaRegistry::pushFrom(l, users, session);
}

void MumbleClient::removeUser(uint32_t session) {
	mumble_log(LOG_TRACE, "removing user session: %u", session);
	LuaRegistry::push(l, users);
	lua_pushinteger(l, session);
	lua_pushnil(l);
	lua_settable(l, -3);
	lua_pop(l, 1);
	user_list.remove(session);
}

void MumbleClient::pushChannel(uint32_t channel_id) {
	LuaRegistry::pushFrom(l, channels, channel_id);
}

MumbleChannel* MumbleClient::getChannel(uint32_t channel_id) {
	MumbleChannel* channel = NULL;

	LuaRegistry::push(l, channels);

	lua_pushinteger(l, channel_id);
	lua_gettable(l, -2);

	if (lua_isuserdata(l, -1) == 1) {
		channel = lua_toclass<MumbleChannel>(l, -1);
	} else {
		lua_pop(l, 1); // Pop whatever was in the table before, most likely nil

		lua_newtable(l);
		int data = LuaRegistry::refIn(l, LuaRegistry::data);

		channel = lua_newclass<MumbleChannel>(l, this, channel_id, data);
		luaL_getmetatable(l, METATABLE_CHAN);
		lua_setmetatable(l, -2);

		mumble_log(LOG_TRACE, "added channel: %u (channel=%p)", channel_id, channel);
		channel_list.add(channel_id, channel);

		lua_pushinteger(l, channel_id);
		lua_pushvalue(l, -2); // Push a copy of the new channel object
		lua_settable(l, -4); // Set the channel metatable to where we store the table of cahnnels, using channel_id as its index
	}

	lua_remove(l, -2); // Remove the clients channels table from the stack
	lua_pop(l, 1); // Remove the channel lua object from the stack, since all we wanted was the pointer

	return channel;
}

void MumbleClient::removeChannel(uint32_t channel_id) {
	mumble_log(LOG_TRACE, "removing channel: %u", channel_id);
	LuaRegistry::push(l, channels);
	lua_pushinteger(l, channel_id);
	lua_pushnil(l);
	lua_settable(l, -3);
	lua_pop(l, 1);
	channel_list.remove(channel_id);
}

void MumbleClient::updateRecordingStatus() {
	bool isRecordingUser = false;

	for (auto &node : user_list) {
		MumbleUser *user = node.data;

		if (user->recording_file != NULL) {
			isRecordingUser = true;
			break;
		}
	}

	if (recording != isRecordingUser) {
		recording = isRecordingUser;

		MumbleProto__UserState msg = MUMBLE_PROTO__USER_STATE__INIT;

		msg.has_session = true;
		msg.session = session;

		msg.has_recording = true;
		msg.recording = recording;

		send(PACKET_USERSTATE, &msg);
	}
}

void MumbleClient::handleSpeakingHooksProtobuf(MumbleUDP__Audio *audio, uint32_t session) {
	lua_stackguard_entry(l);

	uint64_t sequence = audio->frame_number;

	bool speaking = !audio->is_terminator;
	MumbleUser* user = getUser(session);

	bool state_change = false;

	// This will only be true if the user only sent exactly one audio packet
	bool one_frame = (user->speaking == false && speaking == false);

	if (user->speaking != speaking) {
		user->speaking = speaking;
		state_change = true;
	}

	if (one_frame || (state_change && speaking)) {
		pushUser(session);
		hookCall("OnUserStartSpeaking", 1);
		user->recordSilence();
	}

	user->record(audio->opus_data.data, audio->opus_data.len);

	lua_newtable(l);
	lua_pushnumber(l, LEGACY_UDP_OPUS);
	lua_setfield(l, -2, "codec");
	lua_pushnumber(l, audio->target);
	lua_setfield(l, -2, "target");
	lua_pushnumber(l, sequence);
	lua_setfield(l, -2, "sequence");
	pushUser(session);
	lua_setfield(l, -2, "user");
	lua_pushboolean(l, speaking);
	lua_setfield(l, -2, "speaking");
	lua_pushlstring(l, (char*) audio->opus_data.data, audio->opus_data.len);
	lua_setfield(l, -2, "data");
	lua_pushinteger(l, audio->context);
	lua_setfield(l, -2, "context");
	lua_pushinteger(l, opus_packet_get_nb_channels(audio->opus_data.data));
	lua_setfield(l, -2, "channels");
	lua_pushinteger(l, opus_packet_get_bandwidth(audio->opus_data.data));
	lua_setfield(l, -2, "bandwidth");
	lua_pushinteger(l, opus_packet_get_samples_per_frame(audio->opus_data.data, AUDIO_SAMPLE_RATE));
	lua_setfield(l, -2, "samples_per_frame");
	hookCall("OnUserSpeak", 1);

	if (one_frame || (state_change && !speaking)) {
		pushUser(session);
		hookCall("OnUserStopSpeaking", 1);
		user->last_spoke = uv_now(uv_default_loop());
	}

	lua_stackguard_exit(l);
}

void MumbleClient::handleSpeakingHooksLegacy(uint8_t buffer[], uint8_t codec, uint8_t target, uint32_t session) {
	lua_stackguard_entry(l);

	int read = 0;
	uint64_t sequence = VarInt::decode(buffer, &read);

	bool speaking = false;
	MumbleUser* user = getUser(session);

	int payload_len = 0;
	uint16_t frame_header = 0;

	if (codec == LEGACY_UDP_SPEEX || codec == LEGACY_UDP_CELT_ALPHA || codec == LEGACY_UDP_CELT_BETA) {
		frame_header = buffer[read++];
		payload_len = frame_header & 0x7F;
		speaking = ((frame_header & 0x80) == 0);
	} else if (codec == LEGACY_UDP_OPUS) {
		frame_header = VarInt::decode(buffer + read, &read);
		payload_len = frame_header & 0x1FFF;
		speaking = ((frame_header & 0x2000) == 0);
	}

	bool state_change = false;

	// This will only be true if the user only sent exactly one audio packet
	bool one_frame = (user->speaking == false && speaking == false);

	if (user->speaking != speaking) {
		user->speaking = speaking;
		state_change = true;
	}

	if (one_frame || (state_change && speaking)) {
		pushUser(session);
		hookCall("OnUserStartSpeaking", 1);
		user->recordSilence();
	}

	user->record(buffer + read, payload_len);

	lua_newtable(l);
	lua_pushnumber(l, codec);
	lua_setfield(l, -2, "codec");
	lua_pushnumber(l, target);
	lua_setfield(l, -2, "target");
	lua_pushnumber(l, sequence);
	lua_setfield(l, -2, "sequence");
	pushUser(session);
	lua_setfield(l, -2, "user");
	lua_pushboolean(l, speaking);
	lua_setfield(l, -2, "speaking");
	lua_pushlstring(l, (char*) buffer + read, payload_len);
	lua_setfield(l, -2, "data");
	lua_pushinteger(l, opus_packet_get_nb_channels(buffer + read));
	lua_setfield(l, -2, "channels");
	lua_pushinteger(l, opus_packet_get_bandwidth(buffer + read));
	lua_setfield(l, -2, "bandwidth");
	lua_pushinteger(l, opus_packet_get_samples_per_frame(buffer + read, AUDIO_SAMPLE_RATE));
	lua_setfield(l, -2, "samples_per_frame");
	hookCall("OnUserSpeak", 1);

	if (one_frame || (state_change && !speaking)) {
		pushUser(session);
		hookCall("OnUserStopSpeaking", 1);
		user->last_spoke = uv_now(uv_default_loop());
	}

	lua_stackguard_exit(l);
}
