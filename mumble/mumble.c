#include "mumble.h"

#include "audio.h"
#include "acl.h"
#include "buffer.h"
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
#include "util.h"
#include "log.h"

#include <openssl/ssl.h>
#include <openssl/err.h>
#include <math.h>

int MUMBLE_CLIENTS;
int MUMBLE_REGISTRY;
int MUMBLE_TIMER_REG;
int MUMBLE_THREAD_REG;

static uv_signal_t mumble_signal;
static void mumble_client_cleanup(MumbleClient *client);
static void mumble_client_free(MumbleClient *client);
static void mumble_close();

static LinkNode* mumble_clients = NULL;

static void mumble_signal_event(uv_signal_t* handle, int signum) {
	if (signum == SIGINT) {
		uv_signal_stop(handle);
		uv_close((uv_handle_t*) handle, NULL);
		mumble_close();
	}
}

void mumble_ping_timer(uv_timer_t* handle) {
	MumbleClient* client = (MumbleClient*) handle->data;
	lua_State *l = client->l;

	lua_stackguard_entry(l);

	mumble_ping(client);

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

static uint64_t mumble_ping_udp_legacy(lua_State *l, MumbleClient* client) {
	double milliseconds = gettime(CLOCK_MONOTONIC);
	uint64_t timestamp = (uint64_t) (milliseconds * 1000);

	unsigned char packet[UDP_BUFFER_MAX];
	packet[0] = LEGACY_PROTO_UDP_PING << 5;

	int len = 1 + util_set_varint(packet + 1, timestamp);

	mumble_log(LOG_TRACE, "[UDP] Sending legacy ping packet: %p", &packet);
	packet_sendudp(client, packet, len);
	return timestamp;
}

static uint64_t mumble_ping_udp_protobuf(lua_State *l, MumbleClient* client) {
	MumbleUDP__Ping ping = MUMBLE_UDP__PING__INIT;

	double milliseconds = gettime(CLOCK_MONOTONIC);
	uint64_t timestamp = (uint64_t) (milliseconds * 1000);

	ping.timestamp = timestamp;

	unsigned char packet[UDP_BUFFER_MAX];
	packet[0] = PROTO_UDP_PING;

	int len = 1 + mumble_udp__ping__pack(&ping, packet + 1);

	mumble_log(LOG_TRACE, "[UDP] Sending %s: %p", "MumbleUDP.Ping", &ping);
	packet_sendudp(client, packet, len);
	return timestamp;
}

static void mumble_ping_tcp(MumbleClient *client) {
	lua_State* l = client->l;

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
	mumble_hook_call(client, "OnPingTCP", 1);

	packet_send(client, PACKET_PING, &ping);
}

static void mumble_ping_udp(MumbleClient* client) {
	if (crypt_isValid(client->crypt)) {
		lua_State* l = client->l;
		uint64_t timestamp;
		if (client->legacy) {
			timestamp = mumble_ping_udp_legacy(l, client);
		} else {
			timestamp = mumble_ping_udp_protobuf(l, client);
		}

		if (client->udp_ping_acc >= UDP_TCP_FALLBACK && !client->tcp_udp_tunnel) {
			// We didn't get a response from a ping 3 times in a row
			mumble_log(LOG_WARN, "Server no longer responding to UDP pings, falling back to TCP..");
			client->tcp_udp_tunnel = true;
		}

		lua_newtable(l);
		lua_pushnumber(l, timestamp);
		lua_setfield(l, -2, "timestamp");
		mumble_hook_call(client, "OnPingUDP", 1);

		client->udp_ping_acc++;
	}
}

void mumble_ping(MumbleClient* client) {
	mumble_ping_tcp(client);
	mumble_ping_udp(client);
}

static void mumble_update_ping(MumbleClient* client, uint64_t timestamp, bool udp) {
	double response = gettime(CLOCK_MONOTONIC);
	double delay = (response * 1000) - timestamp;

	double n = client->udp_packets + 1;
	client->udp_packets = n;
	client->udp_ping_avg = client->udp_ping_avg * (n - 1) / n + delay / n;
	client->udp_ping_var = pow(fabs(delay - client->udp_ping_avg), 2);

	if (client->tcp_udp_tunnel && udp) {
		if (client->udp_ping_acc >= UDP_TCP_FALLBACK) {
			// We suddenly got a response, after sending out pings with multiple missing responses
			mumble_log(LOG_WARN, "[UDP] Server is responding to UDP packets again, disabling TCP tunnel");
		}
		// Fallback to tunneling UDP data through TCP
		mumble_log(LOG_DEBUG, "[UDP] Connection active");
		client->tcp_udp_tunnel = false;
	}

	// Reset our ping accumulation counter
	client->udp_ping_acc = 0;

	lua_State* l = client->l;
	lua_newtable(l);
	lua_pushnumber(l, (double) timestamp / 1000);
	lua_setfield(l, -2, "timestamp");
	lua_pushnumber(l, delay);
	lua_setfield(l, -2, "ping");
	lua_pushnumber(l, client->udp_ping_avg);
	lua_setfield(l, -2, "average");
	lua_pushnumber(l, client->udp_ping_var);
	lua_setfield(l, -2, "deviation");
	mumble_hook_call(client, "OnPongUDP", 1);
}

void mumble_handle_udp_packet(MumbleClient* client, unsigned char* unencrypted, ssize_t size, bool udp) {
	uint8_t header = unencrypted[0];

	if (client->legacy) {
		uint8_t id = (header >> 5) & 0x7;

		switch (id) {
		case LEGACY_PROTO_UDP_PING: {
			int read = 1;
			uint64_t timestamp = util_get_varint(unencrypted + read, &read);

			mumble_log(LOG_TRACE, "[UDP] Received legacy ping packet (size=%u, id=%u, timestamp=%lu)", size, id, timestamp);

			mumble_update_ping(client, timestamp, udp);
			return;
		}
		case LEGACY_UDP_OPUS:
		case LEGACY_UDP_SPEEX:
		case LEGACY_UDP_CELT_ALPHA:
		case LEGACY_UDP_CELT_BETA: {
			uint8_t target = header >> 0x1F;

			int read = 1;
			int session = util_get_varint(unencrypted + read, &read);

			mumble_log(LOG_TRACE, "[UDP] Received legacy audio packet (size=%u, id=%u, target=%u, session=%u)", size, id, target, session);
			mumble_handle_speaking_hooks_legacy(client, unencrypted + read, id, target, session);
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
				mumble_handle_speaking_hooks_protobuf(client, audio, audio->sender_session);
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
				mumble_update_ping(client, ping->timestamp, udp);
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

void socket_read_event_udp(uv_udp_t* handle, ssize_t nread, const uv_buf_t* buf, const struct sockaddr* addr, unsigned flags) {
	MumbleClient* client = (MumbleClient*) handle->data;

	// Check for errors in the read operation
	if (nread < 0) {
		mumble_log(LOG_ERROR, "[UDP] Error receiving UDP packet: %s", uv_strerror(nread));
		return;
	}

	// Check if we received any data
	if (nread == 0) {
		mumble_log(LOG_TRACE, "[UDP] No data received");
		return;
	}

	unsigned char unencrypted[nread];
	memset(unencrypted, 0, sizeof(unencrypted));

	if (!crypt_isValid(client->crypt)) {
		mumble_log(LOG_ERROR, "[UDP] Unable to decrypt UDP packet, cryptstate invalid: %x", &buf->base);
		return;
	}

	if (!crypt_decrypt(client->crypt, (unsigned char*) buf->base, unencrypted, nread)) {
		mumble_log(LOG_ERROR, "[UDP] Unable to decrypt UDP packet: %x", &buf->base);
		return;
	}

	mumble_handle_udp_packet(client, unencrypted, nread - 4, true);

	if (buf->base) {
		free(buf->base);
	}
}

void alloc_buffer(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf) {
	buf->base = (char *)malloc(suggested_size);
	buf->len = suggested_size;
}

void packet_reset(MumblePacket* packet) {
	packet->type = 0;
	packet->length = 0;
	packet->header_len = 0;
	if (packet->header) {
		free(packet->header);
		packet->header = NULL;
	}
	packet->body_len = 0;
	if (packet->body) {
		free(packet->body);
		packet->body = NULL;
	}
}

void handle_ssl_read_error(MumbleClient* client, int ret) {
	int err = SSL_get_error(client->ssl, ret);

	if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
		return; // Retry when SSL is ready
	}

	mumble_disconnect(client, mumble_ssl_error(ERR_get_error()), false);
}

void socket_read_event_tcp(uv_poll_t* handle, int status, int events) {
	if (status < 0) {
		mumble_log(LOG_ERROR, "tcp read event error: %s", uv_strerror(status));
		return;
	}

	MumbleClient* client = (MumbleClient*) handle->data;
	lua_State* l = client->l;

	if (events & UV_READABLE && client->connected) {
		// Static since this process might take a few iterations to complete
		static MumblePacket packet;

		// Setup for a new packet to be read
		if (!packet.header) {
			packet.header_len = 0;
			packet.header = malloc(sizeof(uint8_t) * PACKET_HEADER_SIZE);
			packet.body_len = 0;
			packet.body = NULL;
		}

		// Read the header if not fully read
		if (packet.header_len < PACKET_HEADER_SIZE) {
			int ret = SSL_read(client->ssl,
			                   packet.header + packet.header_len,
			                   PACKET_HEADER_SIZE - packet.header_len);

			if (ret > 0) {
				packet.header_len += ret;

				// Header complete, parse packet type and length
				if (packet.header_len == PACKET_HEADER_SIZE) {
					packet.type = ntohs(*(uint16_t *)packet.header);
					packet.length = ntohl(*(uint32_t *)(packet.header + 2));

					if (packet.type >= sizeof(packet_handler) / sizeof(Packet_Handler_Func)) {
						mumble_log(LOG_DEBUG, "received unknown packet type %u", packet.type);
						packet_reset(&packet);
						return;
					}

					// if (packet.length > PACKET_MAX_SIZE) { // Prevent excessive allocation
					// 	mumble_log(LOG_WARN, "packet length too large: %u", packet.length);
					// 	packet_reset(&packet);
					// 	return;
					// }

					// Reallocate buffer for the message body
					packet.body = malloc(sizeof(uint8_t) * packet.length);
					if (!packet.body) {
						mumble_log(LOG_ERROR, "failed to create packet buffer: %s", strerror(errno));
						packet_reset(&packet);
						return;
					}
				}
			} else {
				handle_ssl_read_error(client, ret);
				packet_reset(&packet);
				return;
			}
		}

		// Read the message body if the header is complete
		if (packet.header_len == PACKET_HEADER_SIZE && packet.body_len < packet.length) {
			int ret = SSL_read(client->ssl,
			                   packet.body + packet.body_len,
			                   packet.length - packet.body_len);
			if (ret > 0) {
				packet.body_len += ret;

				// Body complete, process the packet
				if (packet.body_len == packet.length) {
					Packet_Handler_Func handler = packet_handler[packet.type];
					if (handler) {
						lua_stackguard_entry(l);
						handler(client, &packet);
						lua_stackguard_exit(l);
					}
					packet_reset(&packet);
				}
			} else {
				handle_ssl_read_error(client, ret);
				packet_reset(&packet);
				return;
			}
		}
	}
}

void socket_write_event_tcp(uv_poll_t* handle, int status, int events) {
	MumbleClient* client = (MumbleClient*) handle->data;

	if (status < 0) {
		mumble_disconnect(client, uv_strerror(status), false);
		return;
	}

	if (events & UV_WRITABLE && !client->connected) {
		int ret = SSL_connect(client->ssl);
		if (ret == 1 && SSL_is_init_finished(client->ssl)) {
			client->connected = true;

			// Log the connection info
			char address[INET6_ADDRSTRLEN];
			if (client->server_host_tcp->ai_family == AF_INET) {
				inet_ntop(AF_INET, &(((struct sockaddr_in*)(client->server_host_tcp->ai_addr))->sin_addr), address, INET_ADDRSTRLEN);
			} else {
				inet_ntop(AF_INET6, &(((struct sockaddr_in6*)(client->server_host_tcp->ai_addr))->sin6_addr), address, INET6_ADDRSTRLEN);
			}

			mumble_log(LOG_INFO, "%s[%d] connected to server %s:%d", METATABLE_CLIENT, client->self, address, client->port);

			// Set the connected flag and trigger any connection callback
			mumble_hook_call(client, "OnConnect", 0);

			uv_poll_stop(&client->ssl_poll);
			uv_poll_start(&client->ssl_poll, UV_READABLE, socket_read_event_tcp);
		} else {
			int error = SSL_get_error(client->ssl, ret);
			if (error != SSL_ERROR_WANT_READ && error != SSL_ERROR_WANT_WRITE) {
				const char* message = (char*) mumble_ssl_error(error);
				mumble_log(LOG_ERROR, "SSL handshake failed: %s", message);
				mumble_client_cleanup(client);
			}
		}
	}
}

void mumble_connected_tcp(uv_connect_t *req, int status) {
	uv_tcp_t* tcp_handle = (uv_tcp_t*) req->handle;
	MumbleClient* client = (MumbleClient*) tcp_handle->data;

	if (status < 0) {
		mumble_disconnect(client, uv_strerror(status), false);
		return;
	}

	// Create a new uv_poll_t to monitor the socket for SSL handshake
	uv_poll_init(uv_default_loop(), &client->ssl_poll, client->socket_tcp_fd);
	client->ssl_poll.data = client;

	// Start polling the socket for writability for the handshake
	uv_poll_start(&client->ssl_poll, UV_WRITABLE, socket_write_event_tcp);
}

static int mumble_client_new(lua_State *l) {
	// 1 = metatable

	MumbleClient *client = lua_newuserdata(l, sizeof(MumbleClient));
	luaL_getmetatable(l, METATABLE_CLIENT);
	lua_setmetatable(l, -2);
	lua_remove(l, -2);

	lua_newtable(l);
	client->hooks = mumble_ref(l);

	lua_newtable(l);
	client->users = mumble_ref(l);

	lua_newtable(l);
	client->channels = mumble_ref(l);

	lua_newtable(l);
	client->audio_streams = mumble_ref(l);

	client->host = NULL;
	client->port = 0;
	client->self = MUMBLE_UNREFERENCED;
	client->session = 0;
	client->volume = 0.5;
	client->connecting = false;
	client->connected = false;
	client->synced = false;
	client->legacy = MUMBLE_VERSION_MAJOR <= 1 && MUMBLE_VERSION_MINOR < 5;
	client->audio_sequence = 0;
	client->audio_target = 0;
	client->audio_frames = AUDIO_DEFAULT_FRAMES;

	client->tcp_packets = 0;
	client->tcp_ping_avg = 0;
	client->tcp_ping_var = 0;

	client->udp_ping_acc = 0;
	client->udp_packets = 0;
	client->udp_ping_avg = 0;
	client->udp_ping_var = 0;

	client->tcp_udp_tunnel = true;

	client->stream_list = NULL;
	client->user_list = NULL;
	client->channel_list = NULL;
	client->audio_pipes = NULL;
	return 1;
}

int mumble_client_connect(lua_State *l) {
	MumbleClient *client = luaL_checkudata(l, 1, METATABLE_CLIENT);
	client->l = l;

	const char* server_host_str = luaL_checkstring(l, 2);
	int port = luaL_checkinteger(l, 3);
	char port_str[6];
	snprintf(port_str, sizeof(port_str), "%u", port);

	const char* certificate_file = luaL_checkstring(l, 4);
	const char* key_file = luaL_checkstring(l, 5);

	client->host = strdup(server_host_str);
	client->port = port;
	client->time = gettime(CLOCK_MONOTONIC);

	// TCP Connection

	client->ssl_context = SSL_CTX_new(SSLv23_client_method());

	if (client->ssl_context == NULL) {
		mumble_client_free(client);
		lua_pushboolean(l, false);
		lua_pushfstring(l, "could not create SSL context: %s", mumble_ssl_error(ERR_get_error()));
		return 2;
	}

	if (!SSL_CTX_use_certificate_chain_file(client->ssl_context, certificate_file) ||
	        !SSL_CTX_use_PrivateKey_file(client->ssl_context, key_file, SSL_FILETYPE_PEM) ||
	        !SSL_CTX_check_private_key(client->ssl_context)) {
		mumble_client_free(client);
		lua_pushboolean(l, false);
		lua_pushfstring(l, "could not load certificate and/or key file: %s", mumble_ssl_error(ERR_get_error()));
		return 2;
	}

	struct addrinfo hint_tcp;
	memset(&hint_tcp, 0, sizeof(hint_tcp));
	hint_tcp.ai_family = AF_UNSPEC;
	hint_tcp.ai_socktype = SOCK_STREAM;

	int err = getaddrinfo(server_host_str, port_str, &hint_tcp, &client->server_host_tcp);

	if (err != 0) {
		mumble_client_free(client);
		lua_pushboolean(l, false);
		lua_pushfstring(l, "could not parse server address: %s", gai_strerror(err));
		return 2;
	}

	// UDP Connection

	client->crypt = crypt_new();

	if (client->crypt == NULL) {
		mumble_client_free(client);
		lua_pushnil(l);
		lua_pushfstring(l, "could not initialize ocb cryptstate");
		return 2;
	}

	struct addrinfo hint_udp;
	memset(&hint_udp, 0, sizeof(hint_udp));
	hint_udp.ai_family = AF_UNSPEC;
	hint_udp.ai_socktype = SOCK_DGRAM;

	err = getaddrinfo(server_host_str, port_str, &hint_udp, &client->server_host_udp);

	if (err != 0) {
		mumble_client_free(client);
		lua_pushboolean(l, false);
		lua_pushfstring(l, "could not parse server address: %s", gai_strerror(err));
		return 2;
	}

	client->ssl = SSL_new(client->ssl_context);

	if (client->ssl == NULL) {
		mumble_client_free(client);
		lua_pushboolean(l, false);
		lua_pushfstring(l, "could not create SSL object: %s", mumble_ssl_error(ERR_get_error()));
		return 2;
	}

	// Audio encoder

	client->encoder = lua_newuserdata(l, opus_encoder_get_size(AUDIO_PLAYBACK_CHANNELS));
	luaL_getmetatable(l, METATABLE_ENCODER);
	lua_setmetatable(l, -2);

	err = opus_encoder_init(client->encoder, AUDIO_SAMPLE_RATE, AUDIO_PLAYBACK_CHANNELS, OPUS_APPLICATION_AUDIO);
	if (err != OPUS_OK) {
		mumble_client_free(client);
		lua_pushboolean(l, false);
		lua_pushfstring(l, "could not initialize opus encoder: %s", opus_strerror(err));
		return 2;
	}

	client->encoder_ref = mumble_ref(l);

	opus_encoder_ctl(client->encoder, OPUS_SET_VBR(0));
	opus_encoder_ctl(client->encoder, OPUS_SET_BITRATE(AUDIO_DEFAULT_BITRATE));

	client->socket_tcp.data = (void*) client;
	client->socket_udp.data = (void*) client;
	client->audio_timer.data = (void*) client;
	client->ping_timer.data = (void*) client;

	uv_loop_t* loop = uv_default_loop();

	uv_tcp_init(loop, &client->socket_tcp);
	uv_udp_init(loop, &client->socket_udp);

	err = uv_tcp_connect(&client->tcp_connect_req, &client->socket_tcp, client->server_host_tcp->ai_addr, mumble_connected_tcp);
	if (err) {
		mumble_client_free(client);
		lua_pushboolean(l, false);
		lua_pushfstring(l, "could not connect to tcp address: %s", uv_strerror(err));
		return 2;
	}

	err = uv_fileno((uv_handle_t*) &client->socket_tcp, &client->socket_tcp_fd);
	if (err) {
		mumble_client_free(client);
		lua_pushboolean(l, false);
		lua_pushfstring(l, "failed getting file descriptor for tcp socket: %s", uv_strerror(err));
		return 1;
	}

	if (SSL_set_fd(client->ssl, client->socket_tcp_fd) == 0) {
		mumble_client_free(client);
		lua_pushboolean(l, false);
		lua_pushfstring(l, "could not set SSL file descriptor: %s", mumble_ssl_error(ERR_get_error()));
		return 2;
	}

	err = uv_udp_connect(&client->socket_udp, client->server_host_udp->ai_addr);
	if (err) {
		mumble_client_free(client);
		lua_pushboolean(l, false);
		lua_pushfstring(l, "could not connect to udp address: %s", uv_strerror(err));
		return 2;
	}

	uv_udp_recv_start(&client->socket_udp, alloc_buffer, socket_read_event_udp);

	// Create a timer to constantly send out pings to the server
	uv_timer_init(loop, &client->ping_timer);

	// Register ourself in the list of connected clients
	lua_pushvalue(l, 1);
	client->self = mumble_registry_ref(l, MUMBLE_CLIENTS);
	client->connecting = true;
	list_add(&mumble_clients, client->self, client);

	mumble_log(LOG_INFO, "%s[%d] connecting to host %s:%d", METATABLE_CLIENT, client->self, server_host_str, port);

	lua_pushboolean(l, true);
	return 1;
}

static int getNetworkBandwidth(int bitrate, int frames) {
	// 8  - UDP overhead
	// 8  - ProtobufCMessage header
	// 4  - (uint32_t) sender_session
	// 8  - (uint64_t) frame_number
	// 24 - (uint64_t) positional_data (assuming * 3)
	// 4  - volume_adjustment
	// 1  - is_terminator
	// 4  - target
	// OPUS DATA
	int overhead = 8 + 8 + 4 + 8 + 24 + 4 + 1 + 4;

	// Calculate the size of the Opus data
	int opus_data_size = frames * (bitrate / 8) / frames * AUDIO_PLAYBACK_CHANNELS;

	return bitrate + overhead + opus_data_size;
}

uint64_t mumble_adjust_audio_bandwidth(MumbleClient *client) {
	int frames = client->audio_frames / 10;
	uint32_t maxbitrate = client->max_bandwidth;
	opus_int32 bitrate;
	opus_encoder_ctl(client->encoder, OPUS_GET_BITRATE(&bitrate));
	opus_int32 original_bitrate = bitrate;

	if (maxbitrate == -1) {
		// No limit
	} else if (getNetworkBandwidth(bitrate, frames) > maxbitrate) {
		if ((frames <= 4) && (maxbitrate <= 32000))
			frames = 4;
		else if ((frames == 1) && (maxbitrate <= 64000))
			frames = 2;
		else if ((frames == 2) && (maxbitrate <= 48000))
			frames = 4;

		if (getNetworkBandwidth(bitrate, frames) > maxbitrate) {
			do {
				bitrate -= 1000;
			} while ((bitrate > 8000) && (getNetworkBandwidth(bitrate, frames) > maxbitrate));
		}
	}
	if (bitrate < 8000) {
		bitrate = 8000;
	}

	if (bitrate != original_bitrate) {
		mumble_log(LOG_WARN, "Server maximum network bandwidth is only %d kbit/s. Audio quality auto-adjusted to %d kbit/s (%d ms)", maxbitrate / 1000, bitrate / 1000, frames * 10);
		client->audio_frames = frames * 10;
		opus_encoder_ctl(client->encoder, OPUS_SET_BITRATE(bitrate));

		if (bitrate >= 64000) {
			opus_encoder_ctl(client->encoder, OPUS_SET_APPLICATION(OPUS_APPLICATION_RESTRICTED_LOWDELAY));
		} else if (bitrate >= 32000) {
			opus_encoder_ctl(client->encoder, OPUS_SET_APPLICATION(OPUS_APPLICATION_AUDIO));
		} else {
			opus_encoder_ctl(client->encoder, OPUS_SET_APPLICATION(OPUS_APPLICATION_VOIP));
		}
	}

	// Get the length of our timer for the audio stream..
	return (uint64_t) frames * 10;
}

void mumble_create_audio_timer(MumbleClient *client) {
	uint64_t time = mumble_adjust_audio_bandwidth(client);

	// Create a timer for audio data
	uv_timer_init(uv_default_loop(), &client->audio_timer);
	uv_timer_start(&client->audio_timer, mumble_audio_timer, time, time);
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

static void mumble_client_free(MumbleClient *client) {
	if (client->host) {
		free(client->host);
		client->host = NULL;
	}
	if (client->crypt) {
		crypt_free(client->crypt);
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

	if (client->server_host_udp) {
		freeaddrinfo(client->server_host_udp);
		client->server_host_udp = NULL;
	}

	if (client->server_host_tcp) {
		freeaddrinfo(client->server_host_tcp);
		client->server_host_tcp = NULL;
	}
}

static void mumble_client_cleanup(MumbleClient *client) {
	if (uv_is_active((uv_handle_t*) &client->socket_udp)) {
		uv_udp_recv_stop(&client->socket_udp);
		uv_close((uv_handle_t*) &client->socket_udp, NULL);
	}

	uv_cancel((uv_req_t*)&client->tcp_connect_req);

	if (uv_is_active((uv_handle_t*) &client->ssl_poll)) {
		uv_poll_stop(&client->ssl_poll);
	}

	if (uv_is_active((uv_handle_t*) &client->socket_tcp)) {
		uv_close((uv_handle_t*) &client->socket_tcp, NULL);
	}

	if (uv_is_active((uv_handle_t*) &client->ping_timer)) {
		uv_timer_stop(&client->ping_timer);
	}

	if (uv_is_active((uv_handle_t*) &client->audio_timer)) {
		uv_timer_stop(&client->audio_timer);
	}

	LinkNode* current = client->stream_list;

	if (current) {
		// Cleanup any active audio transmissions
		while (current != NULL) {
			LinkNode* next = current->next;
			audio_transmission_unreference(client->l, current->data);
			current = next;
		}
	}

	current = client->user_list;

	if (current) {
		// Cleanup our user objects
		while (current != NULL) {
			LinkNode* next = current->next;
			mumble_user_remove(client, current->index);
			current = next;
		}
	}

	current = client->channel_list;

	if (current) {
		// Cleanup our channel objects
		while (current != NULL) {
			LinkNode* next = current->next;
			mumble_channel_remove(client, current->index);
			current = next;
		}
	}

	if (client->self > MUMBLE_UNREFERENCED) {
		// Remove from the connected clients list
		list_remove(&mumble_clients, client->self);
		mumble_registry_unref(client->l, MUMBLE_CLIENTS, &client->self);
	}
}

void mumble_disconnect(MumbleClient *client, const char* reason, bool garbagecollected) {
	if (!client->connecting) {
		mumble_log(LOG_TRACE, "mumble.client: %p attempted to disconnect while not connected (%s)", client, reason);
		return;
	}

	client->connecting = false;
	client->connected = false;

	lua_State* l = client->l;

	lua_stackguard_entry(l);

	if (!garbagecollected) {
		// Only call "OnDisconnect" hook if we weren't garbage collected
		mumble_log(LOG_INFO, "%s[%d] disconnected from server: %s", METATABLE_CLIENT, client->self, reason);
		lua_pushstring(l, reason);
		mumble_hook_call(client, "OnDisconnect", 1);
	}

	mumble_client_free(client);
	mumble_client_cleanup(client);

	// lua_pushcfunction(l, mumble_traceback);
	// lua_insert(l, 1);
	// lua_getglobal(l, "debug_registry");
	// lua_rawgeti(l, LUA_REGISTRYINDEX, MUMBLE_REGISTRY);
	// if (lua_pcall(l, 1, 0, 1) != 0) {
	// 	mumble_log(LOG_ERROR, "%s", lua_tostring(l, -1));
	// }
	// lua_remove(l, 1);

	lua_stackguard_exit(l);
}

static int mumble_getTime(lua_State *l) {
	lua_pushnumber(l, gettime(CLOCK_REALTIME));
	return 1;
}

static int mumble_getConnections(lua_State *l) {
	mumble_pushref(l, MUMBLE_CLIENTS);
	return 1;
}

static bool erroring = false;

int mumble_traceback(lua_State *l) {
	luaL_traceback(l, l, lua_tostring(l, 1), 1);
	return 1;
}

int mumble_hook_call(MumbleClient *client, const char* hook, int nargs) {
	return mumble_hook_call_ret(client, hook, nargs, 0);
}

int mumble_hook_call_ret(MumbleClient *client, const char* hook, int nargs, int nresults) {
	//lua_stackguard_entry(l);

	lua_State* l = client->l;

	if (client->self == MUMBLE_UNREFERENCED) {
		lua_pop(l, nargs); // Just pop whatever we expected to get popped
		mumble_log(LOG_DEBUG, "unreferenced %s: %p hook \"%s\" ignored", METATABLE_CLIENT, client, hook);
		return 0;
	}

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
			mumble_client_raw_get(client);

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
					mumble_log(LOG_ERROR, "%s", lua_tostring(l, -1));
					mumble_hook_call(client, "OnError", 1);
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

MumbleUser* mumble_user_get(MumbleClient* client, uint32_t session) {
	lua_State* l = client->l;
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
			user->name = NULL;
			user->mute = false;
			user->deaf = false;
			user->self_mute = false;
			user->self_deaf = false;
			user->suppress = false;
			user->comment = NULL;
			user->comment_hash = NULL;
			user->comment_hash_len = 0;
			user->speaking = false;
			user->recording = false;
			user->priority_speaker = false;
			user->texture = NULL;
			user->texture_hash = NULL;
			user->texture_hash_len = 0;
			user->hash = NULL;
			user->listens = NULL;
		}
		luaL_getmetatable(l, METATABLE_USER);
		lua_setmetatable(l, -2);

		list_add(&client->user_list, session, user);

		lua_pushinteger(l, session);
		lua_pushvalue(l, -2); // Push a copy of the new user metatable
		lua_settable(l, -4); // Set the user metatable to where we store the table of users, using session as its index
	}

	lua_remove(l, -2); // Remove the clients users table from the stack
	lua_pop(l, 1); // Remove the user lua object from the stack, since all we wanted was the pointer

	return user;
}

void mumble_client_raw_get(MumbleClient* client) {
	mumble_registry_pushref(client->l, MUMBLE_CLIENTS, client->self);
}

bool mumble_user_isnil(lua_State* l, MumbleClient* client, uint32_t session) {

	mumble_pushref(l, client->users);
	lua_pushinteger(l, session);
	lua_gettable(l, -2);

	bool isNil = lua_isnil(l, -1);

	lua_pop(l, 2);

	return isNil;
}

void mumble_user_raw_get(MumbleClient* client, uint32_t session) {
	mumble_registry_pushref(client->l, client->users, session);
}

void mumble_user_remove(MumbleClient* client, uint32_t session) {
	lua_State* l = client->l;
	mumble_pushref(l, client->users);
	lua_pushinteger(l, session);
	lua_pushnil(l);
	lua_settable(l, -3);
	lua_pop(l, 1);
	list_remove(&client->user_list, session);
}

void mumble_channel_raw_get(MumbleClient* client, uint32_t channel_id) {
	mumble_registry_pushref(client->l, client->channels, channel_id);
}

MumbleChannel* mumble_channel_get(MumbleClient* client, uint32_t channel_id) {
	lua_State* l = client->l;
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
			channel->name = NULL;
			channel->channel_id = channel_id;
			channel->parent = 0;
			channel->description = NULL;
			channel->description_hash = NULL;
			channel->temporary = false;
			channel->position = 0;
			channel->max_users = 0;
			channel->links = NULL;
			channel->is_enter_restricted = false;
			channel->permissions = 0;
		}
		luaL_getmetatable(l, METATABLE_CHAN);
		lua_setmetatable(l, -2);

		list_add(&client->channel_list, channel_id, channel);

		lua_pushinteger(l, channel_id);
		lua_pushvalue(l, -2); // Push a copy of the new channel object
		lua_settable(l, -4); // Set the channel metatable to where we store the table of cahnnels, using channel_id as its index
	}

	lua_remove(l, -2); // Remove the clients channels table from the stack
	lua_pop(l, 1); // Remove the channel lua object from the stack, since all we wanted was the pointer

	return channel;
}

void mumble_channel_remove(MumbleClient* client, uint32_t channel_id) {
	lua_State* l = client->l;
	mumble_pushref(l, client->channels);
	lua_pushinteger(l, channel_id);
	lua_pushnil(l);
	lua_settable(l, -3);
	lua_pop(l, 1);
	list_remove(&client->channel_list, channel_id);
}

int mumble_push_address(lua_State* l, ProtobufCBinaryData address) {
	lua_newtable(l);
	uint8_t* bytes = (uint8_t*) address.data;
	uint64_t* addr = (uint64_t*) address.data;
	uint16_t* shorts = (uint16_t*) address.data;

	if (addr[0] != 0ULL || shorts[4] != 0 || shorts[5] != 0xFFFF) {
		char ipv6[INET6_ADDRSTRLEN];

		if (!inet_ntop(AF_INET6, address.data, ipv6, sizeof(ipv6))) {
			// Fallback
			sprintf(ipv6, "%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x",
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

		if (!inet_ntop(AF_INET, address.data, ipv4, sizeof(ipv4))) {
			// Fallback
			sprintf(ipv4, "%d.%d.%d.%d", bytes[12], bytes[13], bytes[14], bytes[15]);
		}

		lua_pushboolean(l, true);
		lua_setfield(l, -2, "ipv4");
		lua_pushstring(l, ipv4);
		lua_setfield(l, -2, "string");
	}

	lua_newtable(l);
	for (uint32_t k = 0; k < address.len; k++) {
		lua_pushinteger(l, k + 1);
		lua_pushinteger(l, address.data[k]);
		lua_settable(l, -3);
	}
	lua_setfield(l, -2, "data");

	return 1;
}

void mumble_handle_speaking_hooks_protobuf(MumbleClient* client, MumbleUDP__Audio *audio, uint32_t session) {
	lua_State* l = client->l;
	lua_stackguard_entry(l);

	uint64_t sequence = audio->frame_number;

	bool speaking = !audio->is_terminator;
	MumbleUser* user = mumble_user_get(client, session);

	bool state_change = false;
	bool one_frame = (user->speaking == false && speaking == false); // This will only be true if the user only sent exactly one audio packet

	if (user->speaking != speaking) {
		user->speaking = speaking;
		state_change = true;
	}

	if ((one_frame || state_change) && speaking) {
		mumble_user_raw_get(client, session);
		mumble_hook_call(client, "OnUserStartSpeaking", 1);
	}

	lua_newtable(l);
	lua_pushnumber(l, LEGACY_UDP_OPUS);
	lua_setfield(l, -2, "codec");
	lua_pushnumber(l, audio->target);
	lua_setfield(l, -2, "target");
	lua_pushnumber(l, sequence);
	lua_setfield(l, -2, "sequence");
	mumble_user_raw_get(client, session);
	lua_setfield(l, -2, "user");
	lua_pushboolean(l, speaking);
	lua_setfield(l, -2, "speaking");
	lua_pushlstring(l, (char*) audio->opus_data.data, audio->opus_data.len);
	lua_setfield(l, -2, "data");
	lua_pushinteger(l, audio->context);
	lua_setfield(l, -2, "context");
	lua_pushinteger(l, opus_packet_get_nb_channels(audio->opus_data.data));
	lua_setfield(l, -2, "channels");
	lua_pushinteger(l, opus_packet_get_nb_channels(audio->opus_data.data));
	lua_setfield(l, -2, "bandwidth");
	lua_pushinteger(l, opus_packet_get_samples_per_frame(audio->opus_data.data, AUDIO_SAMPLE_RATE));
	lua_setfield(l, -2, "samples_per_frame");
	mumble_hook_call(client, "OnUserSpeak", 1);

	if ((one_frame || state_change) && !speaking) {
		mumble_user_raw_get(client, session);
		mumble_hook_call(client, "OnUserStopSpeaking", 1);
	}

	lua_stackguard_exit(l);
}

void mumble_handle_speaking_hooks_legacy(MumbleClient* client, uint8_t buffer[], uint8_t codec, uint8_t target, uint32_t session) {
	lua_State* l = client->l;
	lua_stackguard_entry(l);

	int read = 0;
	uint64_t sequence = util_get_varint(buffer, &read);

	bool speaking = false;
	MumbleUser* user = mumble_user_get(client, session);

	int payload_len = 0;
	uint16_t frame_header = 0;

	if (codec == LEGACY_UDP_SPEEX || codec == LEGACY_UDP_CELT_ALPHA || codec == LEGACY_UDP_CELT_BETA) {
		frame_header = buffer[read++];
		payload_len = frame_header & 0x7F;
		speaking = ((frame_header & 0x80) == 0);
	} else if (codec == LEGACY_UDP_OPUS) {
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

	if ((one_frame || state_change) && speaking) {
		mumble_user_raw_get(client, session);
		mumble_hook_call(client, "OnUserStartSpeaking", 1);
	}

	lua_newtable(l);
	lua_pushnumber(l, codec);
	lua_setfield(l, -2, "codec");
	lua_pushnumber(l, target);
	lua_setfield(l, -2, "target");
	lua_pushnumber(l, sequence);
	lua_setfield(l, -2, "sequence");
	mumble_user_raw_get(client, session);
	lua_setfield(l, -2, "user");
	lua_pushboolean(l, speaking);
	lua_setfield(l, -2, "speaking");
	lua_pushlstring(l, (char*) buffer + read, payload_len);
	lua_setfield(l, -2, "data");
	lua_pushinteger(l, opus_packet_get_nb_channels(buffer + read));
	lua_setfield(l, -2, "channels");
	lua_pushinteger(l, opus_packet_get_nb_channels(buffer + read));
	lua_setfield(l, -2, "bandwidth");
	lua_pushinteger(l, opus_packet_get_samples_per_frame(buffer + read, AUDIO_SAMPLE_RATE));
	lua_setfield(l, -2, "samples_per_frame");
	mumble_hook_call(client, "OnUserSpeak", 1);

	if ((one_frame || state_change) && !speaking) {
		mumble_user_raw_get(client, session);
		mumble_hook_call(client, "OnUserStopSpeaking", 1);
	}

	lua_stackguard_exit(l);
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

int luaopen_mumble(lua_State *l) {
	signal(SIGPIPE, SIG_IGN);
	mumble_init(l);
	return 0;
}

void mumble_init(lua_State *l) {
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

			// Register thread metatable
			luaL_newmetatable(l, METATABLE_THREAD_CONTROLLER);
			{
				lua_pushvalue(l, -1);
				lua_setfield(l, -2, "__index");
			}
			luaL_register(l, NULL, mumble_thread_controller);
			lua_setfield(l, -2, "controller");
		}
		// If you call the thread metatable as a function it will return a new thread object
		lua_newtable(l);
		{
			lua_pushcfunction(l, mumble_thread_new);
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

		// If you call the voice target metatable as a function it will return a new voice target object
		lua_newtable(l);
		{
			lua_pushcfunction(l, mumble_buffer_new);
			lua_setfield(l, -2, "__call");
		}
		lua_setmetatable(l, -2);
		lua_setfield(l, -2, "buffer");

		lua_rawgeti(l, LUA_REGISTRYINDEX, MUMBLE_REGISTRY);
		lua_setfield(l, -2, "registry");
	}
	lua_pop(l, 1);
}

static void mumble_close() {
	LinkNode* current = mumble_clients;

	if (current) {
		// Stop any clients
		while (current != NULL) {
			LinkNode* next = current->next;
			mumble_client_cleanup(current->data);
			current = next;
		}
	}

	printf(NEWLINE);
	mumble_log(LOG_WARN, "exiting loop");
	uv_stop(uv_default_loop());
}

void mumble_weak_table(lua_State *l) {
	lua_newtable(l);
	lua_pushliteral(l, "__mode");
	lua_pushliteral(l, "v");
	lua_rawset(l, -3);
	lua_setmetatable(l, -2);
}

int mumble_ref(lua_State *l) {
	int ref;
	lua_rawgeti(l, LUA_REGISTRYINDEX, MUMBLE_REGISTRY);
	lua_pushvalue(l, -2);
	ref = luaL_ref(l, -2);
	lua_pop(l, 2);
	return ref;
}

void mumble_pushref(lua_State *l, int ref) {
	lua_rawgeti(l, LUA_REGISTRYINDEX, MUMBLE_REGISTRY);
	lua_rawgeti(l, -1, ref);
	lua_remove(l, -2);
}

void mumble_unref(lua_State *l, int ref) {
	lua_rawgeti(l, LUA_REGISTRYINDEX, MUMBLE_REGISTRY);
	luaL_unref(l, -1, ref);
	lua_pop(l, 1);
}

int mumble_registry_ref(lua_State *l, int t) {
	int ref;
	mumble_pushref(l, t);
	lua_pushvalue(l, -2);
	ref = luaL_ref(l, -2);
	lua_pop(l, 2);
	return ref;
}

void mumble_registry_pushref(lua_State *l, int t, int ref) {
	mumble_pushref(l, t);
	lua_rawgeti(l, -1, ref);
	lua_remove(l, -2);
}

void mumble_registry_unref(lua_State *l, int t, int *ref) {
	mumble_pushref(l, t);
	luaL_unref(l, -1, *ref);
	lua_pop(l, 1);
	*ref = MUMBLE_UNREFERENCED;
}