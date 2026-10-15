#include "client/client.h"

#include "client/channel.h"
#include "client/target.h"
#include "client/user.h"
#include "defines.h"
#include "lua/luainc.h"
#include "lua/luautil.h"
#include "lua/metatables.h"
#include "lua/registry.h"
#include "net/crypt.h"
#include "net/packet.h"
#include "util/log.h"
#include "util/platform.h"
#include "util/util.h"

#include <cassert>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <openssl/ssl.h>

// A UDP packet on its way out, freed once it's sent
struct UDPSend {
	uv_udp_send_t req;
	uint8_t data[];
};

static void on_send(uv_udp_send_t* req, int status) {
	// Cancelled sends are just our socket closing
	if (status < 0 && status != UV_ECANCELED) {
		mumble_log(LOG_ERROR, "[UDP] Error sending UDP packet: %s", uv_strerror(status));
	}

	free(req);
}

bool MumbleClient::sendUDP(const void *message, const int length) {
	UDPSend *send = (UDPSend*) malloc(sizeof(UDPSend) + length + 4);

	if (!send) {
		mumble_log(LOG_ERROR, "failed to malloc UDP packet: %s", strerror(errno));
		return false;
	}

	if (!crypt->isValid() || !crypt->encrypt((const uint8_t*) message, send->data, length)) {
		mumble_log(LOG_ERROR, "[UDP] Unable to encrypt UDP packet");
		free(send);
		return false;
	}

	uv_buf_t buf = uv_buf_init((char*) send->data, length + 4);

	int ret = uv_udp_send(&send->req, &socket_udp, &buf, 1, NULL, on_send);
	if (ret < 0) {
		mumble_log(LOG_ERROR, "[UDP] Unable to send UDP packet: %s", uv_strerror(ret));
		free(send);
		return false;
	}

	return true;
}

void MumbleClient::sendUDPOrTunnel(const uint8_t *data, int length) {
	// Whether we use UDP is decided by our TCP pings, but a packet that couldn't go out at all can still use TCP
	if (!sendUDP(data, length)) {
		sendTunnel(data, length);
	}
}

// The message type each outgoing TCP packet type must contain, or NULL if we never send it
static const ProtobufCMessageDescriptor* packet_descriptor(int type) {
	switch (type) {
	case PACKET_VERSION:			return &mumble_proto__version__descriptor;
	case PACKET_AUTHENTICATE:		return &mumble_proto__authenticate__descriptor;
	case PACKET_PING:				return &mumble_proto__ping__descriptor;
	case PACKET_CHANNELREMOVE:		return &mumble_proto__channel_remove__descriptor;
	case PACKET_CHANNELSTATE:		return &mumble_proto__channel_state__descriptor;
	case PACKET_USERREMOVE:			return &mumble_proto__user_remove__descriptor;
	case PACKET_USERSTATE:			return &mumble_proto__user_state__descriptor;
	case PACKET_BANLIST:			return &mumble_proto__ban_list__descriptor;
	case PACKET_TEXTMESSAGE:		return &mumble_proto__text_message__descriptor;
	case PACKET_ACL:				return &mumble_proto__acl__descriptor;
	case PACKET_QUERYUSERS:			return &mumble_proto__query_users__descriptor;
	case PACKET_CRYPTSETUP:			return &mumble_proto__crypt_setup__descriptor;
	case PACKET_CONTEXTACTION:		return &mumble_proto__context_action__descriptor;
	case PACKET_PERMISSIONQUERY:	return &mumble_proto__permission_query__descriptor;
	case PACKET_USERLIST:			return &mumble_proto__user_list__descriptor;
	case PACKET_VOICETARGET:		return &mumble_proto__voice_target__descriptor;
	case PACKET_CODECVERSION:		return &mumble_proto__codec_version__descriptor;
	case PACKET_USERSTATS:			return &mumble_proto__user_stats__descriptor;
	case PACKET_REQUESTBLOB:		return &mumble_proto__request_blob__descriptor;
	case PACKET_PLUGINDATA:			return &mumble_proto__plugin_data_transmission__descriptor;
	default:						return NULL;
	}
}

// Prefix a payload with its packet header and write it to the SSL stream
static int packet_write(MumbleClient* client, int type, uint8_t *packet_out, size_t payload_size, const char* name, const void* message) {
	size_t total_size = PACKET_HEADER_SIZE + payload_size;

	*(uint16_t *)packet_out = htons(type);
	*(uint32_t *)(packet_out + 2) = htonl(payload_size);

	mumble_log(LOG_TRACE, "[TCP] Sending %s: %p", name, message);

	int written = SSL_write(client->ssl, packet_out, total_size);

	free(packet_out);

	return written == (int) total_size ? 0 : -1;
}

int MumbleClient::sendMessage(int type, const ProtobufCMessage *message) {
	const ProtobufCMessageDescriptor* descriptor = packet_descriptor(type);

	if (descriptor == NULL) {
		mumble_log(LOG_WARN, "unable to get payload size for packet #%i", type);
		return 1;
	}

	// The generated per-message pack functions assert this too
	assert(message->descriptor == descriptor);

	size_t payload_size = protobuf_c_message_get_packed_size(message);
	uint8_t *packet_out = (uint8_t*) malloc(sizeof(uint8_t) * (PACKET_HEADER_SIZE + payload_size));

	if (packet_out == NULL) {
		mumble_log(LOG_ERROR, "failed to malloc packet buffer: %s", strerror(errno));
		return 2;
	}

	if (payload_size > 0) {
		protobuf_c_message_pack(message, packet_out + PACKET_HEADER_SIZE);
	}

	return packet_write(this, type, packet_out, payload_size, message->descriptor->name, message);
}

int MumbleClient::sendTunnel(const uint8_t *data, const int length) {
	size_t payload_size = length;
	uint8_t *packet_out = (uint8_t*) malloc(sizeof(uint8_t) * (PACKET_HEADER_SIZE + payload_size));

	if (packet_out == NULL) {
		mumble_log(LOG_ERROR, "failed to malloc packet buffer: %s", strerror(errno));
		return 2;
	}

	if (payload_size > 0) {
		memmove(packet_out + PACKET_HEADER_SIZE, data, length);
	}

	return packet_write(this, PACKET_UDPTUNNEL, packet_out, payload_size, "MumbleProto.UDPTunnel", data);
}

void MumbleClient::onServerVersion(MumblePacket *packet) {
	MumbleProto__Version *version =  mumble_proto__version__unpack(NULL, packet->length, packet->body);
	if (version == NULL) {
		mumble_log(LOG_WARN, "[TCP] Error unpacking server version packet");
		return;
	}

	mumble_log(LOG_TRACE, "[TCP] Received %s: %p", version->base.descriptor->name, version);

	mumble_log(LOG_INFO, "%s[%d] server version: %s", METATABLE_CLIENT, self, version->release);
	mumble_log(LOG_INFO, "%s[%d] server system : %s - %s", METATABLE_CLIENT, self, version->os, version->os_version);

	lua_newtable(l);
	if (version->has_version_v1) {
		lua_pushinteger(l, version->version_v1);
		lua_setfield(l, -2, "version_v1");
	}
	if (version->has_version_v2) {
		lua_pushinteger(l, version->version_v2);
		lua_setfield(l, -2, "version_v2");
	} else if (!legacy) {
		// Protobuf supported client connected to an older server
		mumble_log(LOG_WARN, "falling back to legacy packets for legacy server");
		legacy = true;
	}
	lua_pushstring(l, version->release);
	lua_setfield(l, -2, "release");
	lua_pushstring(l, version->os);
	lua_setfield(l, -2, "os");
	lua_pushstring(l, version->os_version);
	lua_setfield(l, -2, "os_version");
	hookCall("OnServerVersion", 1);

	mumble_proto__version__free_unpacked(version, NULL);
}

void MumbleClient::onUDPTunnel(MumblePacket *packet) {
	handleUDPPacket(packet->body, packet->length, false);
}

void MumbleClient::onPing(MumblePacket *packet) {
	MumbleProto__Ping *ping = mumble_proto__ping__unpack(NULL, packet->length, packet->body);
	if (ping == NULL) {
		mumble_log(LOG_WARN, "[TCP] Error unpacking TCP ping packet");
		return;
	}

	mumble_log(LOG_TRACE, "[TCP] Received %s: %p", ping->base.descriptor->name, ping);

	// The server tells us how many of our UDP packets it decrypted
	if (crypt != NULL) {
		updateUDPMode(ping->has_good ? ping->good : 0);
	}

	lua_newtable(l);
	if (ping->has_timestamp) {
		double delay = updatePingTCP(ping->timestamp);
		lua_pushinteger(l, ping->timestamp);
		lua_setfield(l, -2, "timestamp");
		lua_pushnumber(l, delay);
		lua_setfield(l, -2, "ping");
		lua_pushnumber(l, udp_ping_avg);
		lua_setfield(l, -2, "average");
		lua_pushnumber(l, udp_ping_var);
		lua_setfield(l, -2, "deviation");
	}
	if (ping->has_good) {
		lua_pushnumber(l, ping->good);
		lua_setfield(l, -2, "good");
	}
	if (ping->has_late) {
		lua_pushnumber(l, ping->late);
		lua_setfield(l, -2, "late");
	}
	if (ping->has_lost) {
		lua_pushnumber(l, ping->lost);
		lua_setfield(l, -2, "lost");
	}
	if (ping->has_resync) {
		lua_pushnumber(l, ping->resync);
		lua_setfield(l, -2, "resync");
	}
	if (ping->has_udp_packets) {
		lua_pushnumber(l, ping->udp_packets);
		lua_setfield(l, -2, "udp_packets");
	}
	if (ping->has_tcp_packets) {
		lua_pushnumber(l, ping->tcp_packets);
		lua_setfield(l, -2, "tcp_packets");
	}
	if (ping->has_udp_ping_avg) {
		lua_pushnumber(l, ping->udp_ping_avg);
		lua_setfield(l, -2, "udp_ping_avg");
	}
	if (ping->has_udp_ping_var) {
		lua_pushnumber(l, ping->udp_ping_var);
		lua_setfield(l, -2, "udp_ping_var");
	}
	if (ping->has_tcp_ping_avg) {
		lua_pushnumber(l, ping->tcp_ping_avg);
		lua_setfield(l, -2, "tcp_ping_avg");
	}
	if (ping->has_tcp_ping_var) {
		lua_pushnumber(l, ping->tcp_ping_var);
		lua_setfield(l, -2, "tcp_ping_var");
	}
	hookCall("OnPongTCP", 1);

	mumble_proto__ping__free_unpacked(ping, NULL);
}

void MumbleClient::onReject(MumblePacket *packet) {
	MumbleProto__Reject *reject = mumble_proto__reject__unpack(NULL, packet->length, packet->body);
	if (reject == NULL) {
		mumble_log(LOG_WARN, "[TCP] Error unpacking server reject packet");
		return;
	}

	mumble_log(LOG_TRACE, "[TCP] Received %s: %p", reject->base.descriptor->name, reject);

	mumble_log(LOG_WARN, "%s[%d] server rejected connection: %s", METATABLE_CLIENT, self, reject->reason);

	lua_newtable(l);
	if (reject->has_type) {
		lua_pushinteger(l, reject->type);
		lua_setfield(l, -2, "type");
	}
	lua_pushstring(l, reject->reason);
	lua_setfield(l, -2, "reason");
	hookCall("OnServerReject", 1);

	mumble_proto__reject__free_unpacked(reject, NULL);
}

void MumbleClient::onServerSync(MumblePacket *packet) {
	MumbleProto__ServerSync *sync = mumble_proto__server_sync__unpack(NULL, packet->length, packet->body);
	if (sync == NULL) {
		mumble_log(LOG_WARN, "[TCP] Error unpacking server sync packet");
		return;
	}

	mumble_log(LOG_TRACE, "[TCP] Received %s: %p", sync->base.descriptor->name, sync);

	synced = true;
	uv_timer_start(&ping_timer, MumbleClient::onPingTimer, PING_TIME, PING_TIME);

	lua_newtable(l);
	if (sync->has_session) {
		session = sync->session;
		pushUser(sync->session);
		lua_setfield(l, -2, "user");
	}
	if (sync->has_max_bandwidth) {
		lua_pushinteger(l, sync->max_bandwidth);
		lua_setfield(l, -2, "max_bandwidth");

		max_bandwidth = sync->max_bandwidth;

		adjustAudioBandwidth();
	}
	if (sync->welcome_text != NULL) {
		lua_pushstring(l, sync->welcome_text);
		lua_setfield(l, -2, "welcome_text");
	}
	if (sync->has_permissions) {
		lua_pushinteger(l, sync->permissions);
		lua_setfield(l, -2, "permissions");

		MumbleChannel* root = getChannel(0);
		root->permissions = sync->permissions;
	}
	hookCall("OnServerSync", 1);

	mumble_proto__server_sync__free_unpacked(sync, NULL);
}

void MumbleClient::onChannelRemove(MumblePacket *packet) {
	MumbleProto__ChannelRemove *channel = mumble_proto__channel_remove__unpack(NULL, packet->length, packet->body);
	if (channel == NULL) {
		mumble_log(LOG_WARN, "[TCP] Error unpacking channel remove packet");
		return;
	}

	mumble_log(LOG_TRACE, "[TCP] Received %s: %p", channel->base.descriptor->name, channel);

	pushChannel(channel->channel_id);
	hookCall("OnChannelRemove", 1);
	removeChannel(channel->channel_id);

	mumble_proto__channel_remove__free_unpacked(channel, NULL);
}

void MumbleClient::onChannelState(MumblePacket *packet) {
	MumbleProto__ChannelState *state = mumble_proto__channel_state__unpack(NULL, packet->length, packet->body);
	if (state == NULL) {
		mumble_log(LOG_WARN, "[TCP] Error unpacking channel state packet");
		return;
	}

	mumble_log(LOG_TRACE, "[TCP] Received %s: %p", state->base.descriptor->name, state);

	if (!state->has_channel_id) {
		mumble_log(LOG_WARN, "[TCP] Received a channel state packet without a channel id");
		mumble_proto__channel_state__free_unpacked(state, NULL);
		return;
	}

	MumbleChannel* channel = getChannel(state->channel_id);

	lua_newtable(l);
	pushChannel(channel->channel_id);
	lua_setfield(l , -2, "channel");

	lua_pushinteger(l, channel->channel_id);
	lua_setfield(l , -2, "channel_id");

	if (state->has_parent) {
		channel->parent = state->parent;
		pushChannel(channel->parent);
		lua_setfield(l , -2, "parent");
	}
	if (state->name != NULL) {
		channel->name = std::string(state->name);
		lua_pushstring(l, channel->name->c_str());
		lua_setfield(l , -2, "name");
	}
	if (state->description != NULL) {
		channel->description = std::string(state->description);
		lua_pushstring(l, channel->description->c_str());
		lua_setfield(l , -2, "description");
	}
	if (state->has_temporary) {
		channel->temporary = state->temporary;
		lua_pushboolean(l, channel->temporary);
		lua_setfield(l , -2, "temporary");
	}
	if (state->has_position) {
		channel->position = state->position;
		lua_pushinteger(l, channel->position);
		lua_setfield(l , -2, "position");
	}
	if (state->has_description_hash) {
		channel->description_hash.assign((char*) state->description_hash.data, state->description_hash.len);

		lua_pushstring(l, bin_to_strhex(channel->description_hash).c_str());
		lua_setfield(l, -2, "description_hash");
	}
	if (state->has_max_users) {
		channel->max_users = state->max_users;
		lua_pushinteger(l, channel->max_users);
		lua_setfield(l , -2, "max_users");
	}
	if (state->n_links_add > 0) {
		// Add the new entries to the head of the list
		lua_newtable(l);
		for (uint32_t i = 0; i < state->n_links_add; i++) {
			channel->links.add(state->links_add[i], nullptr);
			lua_pushinteger(l, i + 1);
			pushChannel(state->links_add[i]);
			lua_settable(l, -3);
		}
		lua_setfield(l , -2, "links_add");
	}
	if (state->n_links_remove > 0) {
		lua_newtable(l);
		for (uint32_t i = 0; i < state->n_links_remove; i++) {
			channel->links.remove(state->links_remove[i]);
			lua_pushinteger(l, i + 1);
			pushChannel(state->links_remove[i]);
			lua_settable(l, -3);
		}
		lua_setfield(l , -2, "links_remove");
	}
	if (state->n_links > 0) {
		channel->links.clear();

		lua_newtable(l);
		// Store links in new list
		for (uint32_t i = 0; i < state->n_links; i++) {
			channel->links.add(state->links[i], nullptr);
			lua_pushinteger(l, i + 1);
			pushChannel(state->links[i]);
			lua_settable(l, -3);
		}
		lua_setfield(l , -2, "links");
	}
	if (state->has_is_enter_restricted) {
		channel->is_enter_restricted = state->is_enter_restricted;
		lua_pushboolean(l, channel->is_enter_restricted);
		lua_setfield(l , -2, "is_enter_restricted");
	}
	if (state->has_can_enter) {
		channel->can_enter = state->can_enter;
		lua_pushboolean(l, channel->can_enter);
		lua_setfield(l , -2, "can_enter");
	}

	hookCall("OnChannelState", 1);

	mumble_proto__channel_state__free_unpacked(state, NULL);
}

void MumbleClient::onUserRemove(MumblePacket *packet) {
	MumbleProto__UserRemove *user = mumble_proto__user_remove__unpack(NULL, packet->length, packet->body);
	if (user == NULL) {
		mumble_log(LOG_WARN, "[TCP] Error unpacking user remove packet");
		return;
	}

	mumble_log(LOG_TRACE, "[TCP] Received %s: %p", user->base.descriptor->name, user);

	lua_newtable(l);
	pushUser(user->session);
	lua_setfield(l, -2, "user");
	if (user->has_actor) {
		pushUser(user->actor);
		lua_setfield(l, -2, "actor");
	}
	if (user->reason != NULL) {
		lua_pushstring(l, user->reason);
		lua_setfield(l, -2, "reason");
	}
	if (user->has_ban) {
		lua_pushboolean(l, user->ban);
		lua_setfield(l, -2, "ban");
	}
	hookCall("OnUserRemove", 1);

	if (session == user->session) {
		const char* type = (user->has_ban && user->ban) ? "banned" : "kicked";
		const char* reason = (user->reason != NULL && strcmp(user->reason, "") != 0) ? user->reason : "No reason given";

		const char* message;
		if (user->has_actor && user->has_ban) {
			MumbleUser* actor = getUser(user->actor);
			message = lua_pushfstring(l, "%s from server by %s (Reason \"%s\")", type, optcstr(actor->name), reason);
			mumble_log(LOG_INFO, "%s[%d] %s from server by %s [%d][\"%s\"] (Reason \"%s\")", METATABLE_CLIENT, self, type, METATABLE_USER, actor->session, optcstr(actor->name), reason);
		} else {
			message = lua_pushfstring(l, "%s from server (Reason \"%s\")", type, reason);
			mumble_log(LOG_INFO, "%s[%d] %s from server (Reason \"%s\")", METATABLE_CLIENT, self, type, reason);
		}
		lua_pop(l, 1);
		disconnect(message, false);
	} else {
		removeUser(user->session);
	}

	mumble_proto__user_remove__free_unpacked(user, NULL);
}

void MumbleClient::onUserState(MumblePacket *packet) {
	MumbleProto__UserState *state = mumble_proto__user_state__unpack(NULL, packet->length, packet->body);
	if (state == NULL) {
		mumble_log(LOG_WARN, "[TCP] Error unpacking user state packet");
		return;
	}

	mumble_log(LOG_TRACE, "[TCP] Received %s: %p", state->base.descriptor->name, state);

	if (!state->has_session) {
		mumble_proto__user_state__free_unpacked(state, NULL);
		return;
	}

	MumbleUser* user = getUser(state->session);

	lua_newtable(l);
	if (state->has_actor) {
		pushUser(state->actor);
		lua_setfield(l, -2, "actor");
	}

	user->session = state->session;
	lua_pushinteger(l, user->session);
	lua_setfield(l, -2, "session");

	if (state->name != NULL) {
		user->name = std::string(state->name);
		lua_pushstring(l, user->name->c_str());
		lua_setfield(l, -2, "name");
	}
	if (state->has_channel_id) {
		if (user->connected == true && synced == true && user->channel_id != state->channel_id) {
			lua_newtable(l);
			if (state->has_actor) {
				pushUser(state->actor);
				lua_setfield(l, -2, "actor");
			}
			pushChannel(user->channel_id);
			lua_setfield(l, -2, "from");
			pushChannel(state->channel_id);
			lua_setfield(l, -2, "to");
			pushUser(state->session);
			lua_setfield(l, -2, "user");
			hookCall("OnUserChannel", 1);
		}
		user->channel_id = state->channel_id;
		pushChannel(user->channel_id);
		lua_setfield(l, -2, "channel");
	}
	if (state->has_user_id) {
		user->user_id = state->user_id;
		lua_pushinteger(l, user->user_id);
		lua_setfield(l, -2, "user_id");
	}
	if (state->has_mute) {
		user->mute = state->mute;
		lua_pushboolean(l, user->mute);
		lua_setfield(l, -2, "mute");
	}
	if (state->has_deaf) {
		user->deaf = state->deaf;
		lua_pushboolean(l, user->deaf);
		lua_setfield(l, -2, "deaf");
	}
	if (state->has_self_mute) {
		user->self_mute = state->self_mute;
		lua_pushboolean(l, user->self_mute);
		lua_setfield(l, -2, "self_mute");
	}
	if (state->has_self_deaf) {
		user->self_deaf = state->self_deaf;
		lua_pushboolean(l, user->self_deaf);
		lua_setfield(l, -2, "self_deaf");
	}
	if (state->has_suppress) {
		user->suppress = state->suppress;
		lua_pushboolean(l, user->suppress);
		lua_setfield(l, -2, "suppress");
	}
	if (state->comment != NULL) {
		user->comment = std::string(state->comment);
		lua_pushstring(l, user->comment->c_str());
		lua_setfield(l, -2, "comment");
	}
	if (state->has_recording) {
		user->recording = state->recording;
		lua_pushboolean(l, user->recording);
		lua_setfield(l, -2, "recording");
	}
	if (state->has_priority_speaker) {
		user->priority_speaker = state->priority_speaker;
		lua_pushboolean(l, user->priority_speaker);
		lua_setfield(l, -2, "priority_speaker");
	}
	if (state->has_texture) {
		if (state->texture.data) {
			user->texture = std::string((char*) state->texture.data, state->texture.len);
		} else {
			user->texture.reset();
		}
		lua_pushlstring(l, user->texture ? user->texture->data() : NULL, state->texture.len);
		lua_setfield(l, -2, "texture");
	}
	if (state->hash != NULL) {
		user->hash = std::string(state->hash);
		lua_pushstring(l, user->hash->c_str());
		lua_setfield(l, -2, "hash");
	}
	if (state->has_comment_hash) {
		user->comment_hash.assign((char*) state->comment_hash.data, state->comment_hash.len);

		lua_pushstring(l, bin_to_strhex(user->comment_hash).c_str());
		lua_setfield(l, -2, "comment_hash");
	}
	if (state->has_texture_hash) {
		user->texture_hash.assign((char*) state->texture_hash.data, state->texture_hash.len);

		lua_pushstring(l, bin_to_strhex(user->texture_hash).c_str());
		lua_setfield(l, -2, "texture_hash");
	}
	if (state->n_listening_channel_add > 0) {
		// Add the new entries to the head of the list
		lua_newtable(l);
		for (uint32_t i = 0; i < state->n_listening_channel_add; i++) {
			user->listens.add(state->listening_channel_add[i], nullptr);
			lua_pushinteger(l, i + 1);
			pushChannel(state->listening_channel_add[i]);
			lua_settable(l, -3);
		}
		lua_setfield(l , -2, "listening_channel_add");
	}
	if (state->n_listening_channel_remove > 0) {
		lua_newtable(l);
		for (uint32_t i = 0; i < state->n_listening_channel_remove; i++) {
			user->listens.remove(state->listening_channel_remove[i]);
			lua_pushinteger(l, i + 1);
			pushChannel(state->listening_channel_remove[i]);
			lua_settable(l, -3);
		}
		lua_setfield(l , -2, "listening_channel_remove");
	}
	if (state->n_listening_volume_adjustment > 0) {
		lua_newtable(l);
		for (uint32_t i = 0; i < state->n_listening_volume_adjustment; i++) {
			uint32_t channel_id = state->listening_volume_adjustment[i]->listening_channel;
			float volume_adjustment = state->listening_volume_adjustment[i]->volume_adjustment;

			lua_pushinteger(l, channel_id);
			lua_pushnumber(l, volume_adjustment);
			lua_settable(l, -3);

			MumbleChannel* chan = getChannel(channel_id);
			chan->listening_volume_adjustment = volume_adjustment;
		}
		lua_setfield(l , -2, "listening_volume_adjustment");
	}

	pushUser(state->session);
	lua_setfield(l, -2, "user");

	if (user->connected == false) {
		user->connected = true;

		if (synced == true) {
			lua_pushvalue(l, -1); // Push a copy of the event table we will send to the 'OnUserState' hook
			hookCall("OnUserConnect", 1);
		}
	}
	hookCall("OnUserState", 1);

	mumble_proto__user_state__free_unpacked(state, NULL);
}

void MumbleClient::onBanList(MumblePacket *packet) {
	MumbleProto__BanList *list = mumble_proto__ban_list__unpack(NULL, packet->length, packet->body);
	if (list == NULL) {
		mumble_log(LOG_WARN, "[TCP] Error unpacking ban list packet");
		return;
	}

	mumble_log(LOG_TRACE, "[TCP] Received %s: %p", list->base.descriptor->name, list);

	lua_newtable(l);
	if (list->n_bans > 0) {
		for (uint32_t i = 0; i < list->n_bans; i++) {
			MumbleProto__BanList__BanEntry *ban = list->bans[i];
			lua_pushinteger(l, i + 1);
			lua_newtable(l);
			lua_pushaddress(l, ban->address);
			lua_setfield(l, -2, "address");

			lua_pushinteger(l, ban->mask);
			lua_setfield(l, -2, "mask");
			if (ban->name != NULL) {
				lua_pushstring(l, ban->name);
				lua_setfield(l, -2, "name");
			}
			if (ban->hash != NULL) {
				lua_pushstring(l, ban->hash);
				lua_setfield(l, -2, "hash");
			}
			if (ban->reason != NULL) {
				lua_pushstring(l, ban->reason);
				lua_setfield(l, -2, "reason");
			}
			if (ban->start != NULL) {
				lua_pushstring(l, ban->start);
				lua_setfield(l, -2, "start");
			}
			if (ban->has_duration) {
				lua_pushinteger(l, ban->duration);
				lua_setfield(l, -2, "duration");
			}

			lua_settable(l, -3);
		}
	}
	hookCall("OnBanList", 1);

	mumble_proto__ban_list__free_unpacked(list, NULL);
}

void MumbleClient::onTextMessage(MumblePacket *packet) {
	MumbleProto__TextMessage *msg = mumble_proto__text_message__unpack(NULL, packet->length, packet->body);
	if (msg == NULL) {
		mumble_log(LOG_WARN, "[TCP] Error unpacking text message packet");
		return;
	}

	mumble_log(LOG_TRACE, "[TCP] Received %s: %p", msg->base.descriptor->name, msg);

	lua_newtable(l);
	if (msg->has_actor) {
		pushUser(msg->actor);
		lua_setfield(l, -2, "actor");
	}
	if (msg->message != NULL) {
		lua_pushstring(l, msg->message);
		lua_setfield(l, -2, "message");
	}
	if (msg->n_session > 0) {
		lua_newtable(l);
		for (uint32_t i = 0; i < msg->n_session; i++) {
			lua_pushinteger(l, i + 1);
			pushUser(msg->session[i]);
			lua_settable(l, -3);
		}
		lua_setfield(l, -2, "users");
	}
	if (msg->n_channel_id > 0) {
		lua_newtable(l);
		for (uint32_t i = 0; i < msg->n_channel_id; i++) {
			lua_pushinteger(l, i + 1);
			pushChannel(msg->channel_id[i]);
			lua_settable(l, -3);
		}
		lua_setfield(l, -2, "channels");
	} else {
		lua_pushboolean(l, true);
		lua_setfield(l, -2, "direct");
	}
	hookCall("OnMessage", 1);

	mumble_proto__text_message__free_unpacked(msg, NULL);
}

void MumbleClient::onPermissionDenied(MumblePacket *packet) {
	MumbleProto__PermissionDenied *proto = mumble_proto__permission_denied__unpack(NULL, packet->length, packet->body);
	if (proto == NULL) {
		mumble_log(LOG_WARN, "[TCP] Error unpacking permission denied packet");
		return;
	}

	mumble_log(LOG_TRACE, "[TCP] Received %s: %p", proto->base.descriptor->name, proto);

	lua_newtable(l);
	if (proto->has_type) {
		lua_pushinteger(l, proto->type);
		lua_setfield(l, -2, "type");
	}
	if (proto->has_permission) {
		lua_pushinteger(l, proto->permission);
		lua_setfield(l, -2, "permission");
	}
	if (proto->has_channel_id) {
		pushChannel(proto->channel_id);
		lua_setfield(l, -2, "channel");
	}
	if (proto->has_session) {
		pushUser(proto->session);
		lua_setfield(l, -2, "user");
	}
	if (proto->reason != NULL) {
		lua_pushstring(l, proto->reason);
		lua_setfield(l, -2, "reason");
	}
	if (proto->name != NULL) {
		lua_pushstring(l, proto->name);
		lua_setfield(l, -2, "name");
	}
	hookCall("OnPermissionDenied", 1);

	mumble_proto__permission_denied__free_unpacked(proto, NULL);
}

void MumbleClient::onACL(MumblePacket *packet) {
	MumbleProto__ACL *acl = mumble_proto__acl__unpack(NULL, packet->length, packet->body);
	if (acl == NULL) {
		mumble_log(LOG_WARN, "[TCP] Error unpacking ACL packet");
		return;
	}

	mumble_log(LOG_TRACE, "[TCP] Received %s: %p", acl->base.descriptor->name, acl);

	lua_newtable(l);
	pushChannel(acl->channel_id);
	lua_setfield(l, -2, "channel");
	if (acl->n_groups > 0) {
		lua_newtable(l);
		for (uint32_t i = 0; i < acl->n_groups; i++) {
			MumbleProto__ACL__ChanGroup *changroup = acl->groups[i];
			lua_pushinteger(l, i + 1);
			lua_newtable(l);
			if (changroup->name != NULL) {
				lua_pushstring(l, changroup->name);
				lua_setfield(l, -2, "name");
			}
			if (changroup->has_inherited) {
				lua_pushboolean(l, changroup->inherited);
				lua_setfield(l, -2, "inherited");
			}
			if (changroup->has_inheritable) {
				lua_pushboolean(l, changroup->inheritable);
				lua_setfield(l, -2, "inheritable");
			}
			if (changroup->n_add > 0) {
				lua_newtable(l);
				for (uint32_t j = 0; j < changroup->n_add; j++) {
					lua_pushinteger(l, j + 1);
					lua_pushinteger(l, changroup->add[j]);
					lua_settable(l, -3);
				}
				lua_setfield(l, -2, "add");
			}
			if (changroup->n_remove > 0) {
				lua_newtable(l);
				for (uint32_t j = 0; j < changroup->n_remove; j++) {
					lua_pushinteger(l, j + 1);
					lua_pushinteger(l, changroup->remove[j]);
					lua_settable(l, -3);
				}
				lua_setfield(l, -2, "remove");
			}
			if (changroup->n_inherited_members > 0) {
				lua_newtable(l);
				for (uint32_t j = 0; j < changroup->n_inherited_members; j++) {
					lua_pushinteger(l, j + 1);
					lua_pushinteger(l, changroup->inherited_members[j]);
					lua_settable(l, -3);
				}
				lua_setfield(l, -2, "inherited_members");
			}
			lua_settable(l, -3);
		}
		lua_setfield(l, -2, "groups");
	}
	if (acl->n_acls > 0) {
		lua_newtable(l);
		for (uint32_t i = 0; i < acl->n_acls; i++) {
			MumbleProto__ACL__ChanACL *chanacl = acl->acls[i];
			lua_pushinteger(l, i + 1);
			lua_newtable(l);
			if (chanacl->has_apply_here) {
				lua_pushboolean(l, chanacl->apply_here);
				lua_setfield(l, -2, "apply_here");
			}
			if (chanacl->has_apply_subs) {
				lua_pushboolean(l, chanacl->apply_subs);
				lua_setfield(l, -2, "apply_subs");
			}
			if (chanacl->has_inherited) {
				lua_pushboolean(l, chanacl->inherited);
				lua_setfield(l, -2, "inherited");
			}
			if (chanacl->has_user_id) {
				lua_pushboolean(l, chanacl->user_id);
				lua_setfield(l, -2, "user_id");
			}
			if (chanacl->group != NULL) {
				lua_pushstring(l, chanacl->group);
				lua_setfield(l, -2, "group");
			}
			if (chanacl->has_grant) {
				lua_pushinteger(l, chanacl->grant);
				lua_setfield(l, -2, "grant");
			}
			if (chanacl->has_deny) {
				lua_pushinteger(l, chanacl->deny);
				lua_setfield(l, -2, "deny");
			}
			lua_settable(l, -3);
		}
		lua_setfield(l, -2, "acls");
	}
	if (acl->has_inherit_acls) {
		lua_pushboolean(l, acl->inherit_acls);
		lua_setfield(l, -2, "inherit_acls");
	}
	if (acl->has_query) {
		lua_pushboolean(l, acl->query);
		lua_setfield(l, -2, "query");
	}
	hookCall("OnACL", 1);

	mumble_proto__acl__free_unpacked(acl, NULL);
}

void MumbleClient::onQueryUsers(MumblePacket *packet) {
	MumbleProto__QueryUsers *query = mumble_proto__query_users__unpack(NULL, packet->length, packet->body);
	if (query == NULL) {
		mumble_log(LOG_WARN, "[TCP] Error unpacking query query packet");
		return;
	}

	mumble_log(LOG_TRACE, "[TCP] Received %s: %p", query->base.descriptor->name, query);

	lua_newtable(l);
	if (query->n_ids > 0 && query->n_ids == query->n_names) {
		for (uint32_t i = 0; i < query->n_ids; i++) {
			uint32_t id = query->ids[i];
			char* name = query->names[i];

			lua_pushinteger(l, id);
			lua_pushstring(l, name);
			lua_settable(l, -3);
		}
	}
	hookCall("OnQueryUsers", 1);

	mumble_proto__query_users__free_unpacked(query, NULL);
}

void MumbleClient::onContextActionModify(MumblePacket *packet) {
	MumbleProto__ContextActionModify *modify = mumble_proto__context_action_modify__unpack(NULL, packet->length, packet->body);
	if (modify == NULL) {
		mumble_log(LOG_WARN, "[TCP] Error unpacking context action modify packet");
		return;
	}

	mumble_log(LOG_TRACE, "[TCP] Received %s: %p", modify->base.descriptor->name, modify);

	lua_newtable(l);
	{
		if (modify->action != NULL) {
			lua_pushstring(l, modify->action);
			lua_setfield(l, -2, "action");
		}
		if (modify->text != NULL) {
			lua_pushstring(l, modify->text);
			lua_setfield(l, -2, "text");
		}
		if (modify->has_context) {
			lua_pushinteger(l, modify->context);
			lua_setfield(l, -2, "context");
		}
		if (modify->has_operation) {
			lua_pushinteger(l, modify->operation);
			lua_setfield(l, -2, "operation");
		}
	}
	hookCall("OnContextActionModify", 1);

	mumble_proto__context_action_modify__free_unpacked(modify, NULL);
}

void MumbleClient::onCryptSetup(MumblePacket *packet) {
	MumbleProto__CryptSetup *setup = mumble_proto__crypt_setup__unpack(NULL, packet->length, packet->body);
	if (setup == NULL) {
		mumble_log(LOG_WARN, "[TCP] Error unpacking crypt setup packet");
		return;
	}

	mumble_log(LOG_TRACE, "[TCP] Received %s: %p", setup->base.descriptor->name, setup);

	lua_newtable(l);
	if (setup->has_key && setup->has_client_nonce && setup->has_server_nonce) {
		if (!crypt->setKey(setup->key.data, setup->key.len,
						  setup->client_nonce.data, setup->client_nonce.len,
						  setup->server_nonce.data, setup->server_nonce.len)) {
			mumble_log(LOG_ERROR, "[OCB] CryptState: cipher resync failed (Invalid key/nonce from the server)");
		}
	} else if (setup->has_server_nonce) {
		resync++;
		if (!crypt->setDecryptIV(setup->server_nonce.data, setup->server_nonce.len)) {
			mumble_log(LOG_ERROR, "[OCB] CryptState: cipher resync failed (Invalid nonce from the server)");
		}
	} else {
		MumbleProto__CryptSetup msg = MUMBLE_PROTO__CRYPT_SETUP__INIT;
		msg.has_client_nonce = true;
		msg.client_nonce.len = AES_BLOCK_SIZE;
		msg.client_nonce.data = (uint8_t*) crypt->getEncryptIV();
		send(PACKET_CRYPTSETUP, &msg);
		mumble_log(LOG_WARN, "[OCB] CryptState: cipher disagreement, renegotiating with server");
	}

	bool validCrypt = crypt->isValid();

	if (validCrypt) {
		mumble_log(LOG_INFO, "[OCB] CryptState: handshake complete");
		ping();
	}

	lua_pushboolean(l, validCrypt);
	lua_setfield(l, -2, "valid");

	if (setup->has_key) {
		lua_pushstring(l, bin_to_strhex((char*) setup->key.data, setup->key.len).c_str());
		lua_setfield(l, -2, "key");
	}
	if (setup->has_client_nonce) {
		lua_pushstring(l, bin_to_strhex((char*) setup->client_nonce.data, setup->client_nonce.len).c_str());
		lua_setfield(l, -2, "client_nonce");
	}
	if (setup->has_server_nonce) {
		lua_pushstring(l, bin_to_strhex((char*) setup->server_nonce.data, setup->server_nonce.len).c_str());
		lua_setfield(l, -2, "server_nonce");
	}
	hookCall("OnCryptSetup", 1);

	mumble_proto__crypt_setup__free_unpacked(setup, NULL);
}

void MumbleClient::onUserList(MumblePacket *packet) {
	MumbleProto__UserList *list = mumble_proto__user_list__unpack(NULL, packet->length, packet->body);
	if (list == NULL) {
		mumble_log(LOG_WARN, "[TCP] Error unpacking user list packet");
		return;
	}

	mumble_log(LOG_TRACE, "[TCP] Received %s: %p", list->base.descriptor->name, list);

	lua_newtable(l);
	if (list->n_users > 0) {
		for (uint32_t i = 0; i < list->n_users; i++) {
			MumbleProto__UserList__User *user = list->users[i];
			lua_pushinteger(l, i + 1);
			lua_newtable(l);
			lua_pushinteger(l, user->user_id);
			lua_setfield(l, -2, "user_id");
			if (user->name != NULL) {
				lua_pushstring(l, user->name);
				lua_setfield(l, -2, "name");
			}
			if (user->last_seen != NULL) {
				lua_pushstring(l, user->last_seen);
				lua_setfield(l, -2, "last_seen");
			}
			if (user->has_last_channel) {
				pushChannel(user->last_channel);
				lua_setfield(l, -2, "last_channel");
			}
			lua_settable(l, -3);
		}
	}
	hookCall("OnUserList", 1);

	mumble_proto__user_list__free_unpacked(list, NULL);
}

void MumbleClient::onPermissionQuery(MumblePacket *packet) {
	MumbleProto__PermissionQuery *query = mumble_proto__permission_query__unpack(NULL, packet->length, packet->body);
	if (query == NULL) {
		mumble_log(LOG_WARN, "[TCP] Error unpacking permission query packet");
		return;
	}

	mumble_log(LOG_TRACE, "[TCP] Received %s: %p", query->base.descriptor->name, query);

	if (query->has_channel_id && query->has_permissions) {
		MumbleChannel* chan = getChannel(query->channel_id);
		chan->permissions = query->permissions;
	} else if (query->has_flush && query->flush) {
		// Loop through all channels and set permissions to 0
		LuaRegistry::push(l, channels);
		lua_pushnil(l);
		while (lua_next(l, -2)) {
			if (lua_isuserdata(l, -1) == 1) {
				MumbleChannel* channel = lua_toclass<MumbleChannel>(l, -1);
				channel->permissions = 0;
			}
			lua_pop(l, 1);
		}
		lua_pop(l, 1);
	}

	lua_newtable(l);
	if (query->has_channel_id) {
		pushChannel(query->channel_id);
		lua_setfield(l, -2, "channel");
	}
	if (query->has_permissions) {
		lua_pushinteger(l, query->permissions);
		lua_setfield(l, -2, "permissions");
	}
	if (query->has_flush) {
		lua_pushinteger(l, query->flush);
		lua_setfield(l, -2, "flush");
	}
	hookCall("OnPermissionQuery", 1);

	mumble_proto__permission_query__free_unpacked(query, NULL);
}

void MumbleClient::onCodecVersion(MumblePacket *packet) {
	MumbleProto__CodecVersion *codec = mumble_proto__codec_version__unpack(NULL, packet->length, packet->body);
	if (codec == NULL) {
		mumble_log(LOG_WARN, "[TCP] Error unpacking codec version packet");
		return;
	}

	mumble_log(LOG_TRACE, "[TCP] Received %s: %p", codec->base.descriptor->name, codec);

	lua_newtable(l);
	lua_pushinteger(l, codec->alpha);
	lua_setfield(l, -2, "alpha");
	lua_pushinteger(l, codec->beta);
	lua_setfield(l, -2, "beta");
	lua_pushboolean(l, codec->prefer_alpha);
	lua_setfield(l, -2, "prefer_alpha");
	if (codec->has_opus) {
		lua_pushboolean(l, codec->opus);
		lua_setfield(l, -2, "opus");
	}
	hookCall("OnCodecVersion", 1);

	mumble_proto__codec_version__free_unpacked(codec, NULL);
}

void MumbleClient::onUserStats(MumblePacket *packet) {
	MumbleProto__UserStats *stats = mumble_proto__user_stats__unpack(NULL, packet->length, packet->body);
	if (stats == NULL) {
		mumble_log(LOG_WARN, "[TCP] Error unpacking user stats packet");
		return;
	}

	mumble_log(LOG_TRACE, "[TCP] Received %s: %p", stats->base.descriptor->name, stats);

	lua_newtable(l);
	if (stats->has_session) {
		pushUser(stats->session);
		lua_setfield(l, -2, "user");
	}
	if (stats->has_stats_only) {
		lua_pushboolean(l, stats->stats_only);
		lua_setfield(l, -2, "stats_only");
	}
	lua_newtable(l);
	for (uint32_t i = 0; i < stats->n_certificates; i++) {
		lua_pushinteger(l, i + 1);
		lua_pushlstring(l, (char *)stats->certificates[i].data, stats->certificates[i].len);
		lua_settable(l, -3);
	}
	lua_setfield(l, -2, "certificates");

	if (stats->from_client != NULL) {
		lua_newtable(l);
		if (stats->from_client->has_good) {
			lua_pushinteger(l, stats->from_client->good);
			lua_setfield(l, -2, "good");
		}
		if (stats->from_client->has_late) {
			lua_pushinteger(l, stats->from_client->late);
			lua_setfield(l, -2, "late");
		}
		if (stats->from_client->has_lost) {
			lua_pushinteger(l, stats->from_client->lost);
			lua_setfield(l, -2, "lost");
		}
		if (stats->from_client->has_resync) {
			lua_pushinteger(l, stats->from_client->resync);
			lua_setfield(l, -2, "resync");
		}
		lua_setfield(l, -2, "from_client");
	}

	if (stats->from_server != NULL) {
		lua_newtable(l);
		if (stats->from_server->has_good) {
			lua_pushinteger(l, stats->from_server->good);
			lua_setfield(l, -2, "good");
		}
		if (stats->from_server->has_late) {
			lua_pushinteger(l, stats->from_server->late);
			lua_setfield(l, -2, "late");
		}
		if (stats->from_server->has_lost) {
			lua_pushinteger(l, stats->from_server->lost);
			lua_setfield(l, -2, "lost");
		}
		if (stats->from_server->has_resync) {
			lua_pushinteger(l, stats->from_server->resync);
			lua_setfield(l, -2, "resync");
		}
		lua_setfield(l, -2, "from_server");
	}

	if (stats->has_udp_packets) {
		lua_pushinteger(l, stats->udp_packets);
		lua_setfield(l, -2, "udp_packets");
	}

	if (stats->has_tcp_packets) {
		lua_pushinteger(l, stats->tcp_packets);
		lua_setfield(l, -2, "tcp_packets");
	}

	if (stats->has_udp_ping_avg) {
		lua_pushinteger(l, stats->udp_ping_avg);
		lua_setfield(l, -2, "udp_ping_avg");
	}

	if (stats->has_udp_ping_var) {
		lua_pushinteger(l, stats->udp_ping_var);
		lua_setfield(l, -2, "udp_ping_var");
	}

	if (stats->has_tcp_ping_avg) {
		lua_pushinteger(l, stats->tcp_ping_avg);
		lua_setfield(l, -2, "tcp_ping_avg");
	}

	if (stats->has_tcp_ping_var) {
		lua_pushinteger(l, stats->tcp_ping_var);
		lua_setfield(l, -2, "tcp_ping_var");
	}

	if (stats->version != NULL) {
		lua_newtable(l);
		{
			if (stats->version->has_version_v1) {
				lua_pushinteger(l, stats->version->version_v1);
				lua_setfield(l, -2, "version_v1");
			}
			if (stats->version->has_version_v2) {
				lua_pushinteger(l, stats->version->version_v2);
				lua_setfield(l, -2, "version_v2");
			}
			lua_pushstring(l, stats->version->release);
			lua_setfield(l, -2, "release");
			lua_pushstring(l, stats->version->os);
			lua_setfield(l, -2, "os");
			lua_pushstring(l, stats->version->os_version);
			lua_setfield(l, -2, "os_version");
		}
		lua_setfield(l, -2, "version");
	}

	lua_newtable(l);
	for (uint32_t j = 0; j < stats->n_celt_versions; j++) {
		lua_pushinteger(l, j + 1);
		lua_pushinteger(l, stats->celt_versions[j]);
		lua_settable(l, -3);
	}
	lua_setfield(l, -2, "celt_versions");

	if (stats->has_address) {
		lua_pushaddress(l, stats->address);
		lua_setfield(l, -2, "address");
	}

	if (stats->has_bandwidth) {
		lua_pushinteger(l, stats->bandwidth);
		lua_setfield(l, -2, "bandwidth");
	}

	if (stats->has_onlinesecs) {
		lua_pushinteger(l, stats->onlinesecs);
		lua_setfield(l, -2, "onlinesecs");
	}

	if (stats->has_idlesecs) {
		lua_pushinteger(l, stats->idlesecs);
		lua_setfield(l, -2, "idlesecs");
	}

	if (stats->has_strong_certificate) {
		lua_pushboolean(l, stats->strong_certificate);
		lua_setfield(l, -2, "strong_certificate");
	}

	if (stats->has_opus) {
		lua_pushboolean(l, stats->opus);
		lua_setfield(l, -2, "opus");
	}

	if (stats->rolling_stats != NULL) {
		lua_newtable(l);
		{
			if (stats->rolling_stats->has_time_window) {
				lua_pushinteger(l, stats->rolling_stats->time_window);
				lua_setfield(l, -2, "time_window");
			}
			if (stats->rolling_stats->from_client != NULL) {
				lua_newtable(l);
				{
					lua_pushinteger(l, stats->rolling_stats->from_client->good);
					lua_setfield(l, -2, "good");
					lua_pushinteger(l, stats->rolling_stats->from_client->late);
					lua_setfield(l, -2, "late");
					lua_pushinteger(l, stats->rolling_stats->from_client->lost);
					lua_setfield(l, -2, "lost");
					lua_pushinteger(l, stats->rolling_stats->from_client->resync);
					lua_setfield(l, -2, "resync");
				}
				lua_setfield(l, -2, "from_client");
			}
			if (stats->rolling_stats->from_server != NULL) {
				lua_newtable(l);
				{
					lua_pushinteger(l, stats->rolling_stats->from_server->good);
					lua_setfield(l, -2, "good");
					lua_pushinteger(l, stats->rolling_stats->from_server->late);
					lua_setfield(l, -2, "late");
					lua_pushinteger(l, stats->rolling_stats->from_server->lost);
					lua_setfield(l, -2, "lost");
					lua_pushinteger(l, stats->rolling_stats->from_server->resync);
					lua_setfield(l, -2, "resync");
				}
				lua_setfield(l, -2, "from_server");
			}
		}
		lua_setfield(l, -2, "rolling_stats");
	}

	hookCall("OnUserStats", 1);

	mumble_proto__user_stats__free_unpacked(stats, NULL);
}

void MumbleClient::onServerConfig(MumblePacket *packet) {
	MumbleProto__ServerConfig *config = mumble_proto__server_config__unpack(NULL, packet->length, packet->body);
	if (config == NULL) {
		mumble_log(LOG_WARN, "[TCP] Error unpacking server config packet");
		return;
	}

	mumble_log(LOG_TRACE, "[TCP] Received %s: %p", config->base.descriptor->name, config);

	lua_newtable(l);
	if (config->has_max_bandwidth) {
		lua_pushinteger(l, config->max_bandwidth);
		lua_setfield(l, -2, "max_bandwidth");
	}
	lua_pushstring(l, config->welcome_text);
	lua_setfield(l, -2, "welcome_text");
	if (config->has_allow_html) {
		lua_pushboolean(l, config->allow_html);
		lua_setfield(l, -2, "allow_html");
	}
	if (config->has_message_length) {
		lua_pushinteger(l, config->message_length);
		lua_setfield(l, -2, "message_length");
	}
	if (config->has_image_message_length) {
		lua_pushinteger(l, config->image_message_length);
		lua_setfield(l, -2, "image_message_length");
	}
	if (config->has_max_users) {
		lua_pushinteger(l, config->max_users);
		lua_setfield(l, -2, "max_users");
	}
	if (config->has_recording_allowed) {
		lua_pushinteger(l, config->recording_allowed);
		lua_setfield(l , -2, "recording_allowed");
	}
	hookCall("OnServerConfig", 1);

	mumble_proto__server_config__free_unpacked(config, NULL);
}

void MumbleClient::onSuggestConfig(MumblePacket *packet) {
	MumbleProto__SuggestConfig *config = mumble_proto__suggest_config__unpack(NULL, packet->length, packet->body);
	if (config == NULL) {
		mumble_log(LOG_WARN, "[TCP] Error unpacking suggested config packet");
		return;
	}

	mumble_log(LOG_TRACE, "[TCP] Received %s: %p", config->base.descriptor->name, config);

	lua_newtable(l);
	if (config->has_version_v1) {
		lua_pushinteger(l, config->version_v1);
		lua_setfield(l, -2, "version_v1");
	}
	if (config->has_version_v2) {
		lua_pushinteger(l, config->version_v2);
		lua_setfield(l, -2, "version_v2");
	}
	if (config->has_positional) {
		mumble_log(LOG_WARN, "%s[%d] server recommends posisional audio", METATABLE_CLIENT, self);
		lua_pushboolean(l, config->positional);
		lua_setfield(l, -2, "positional");
	}
	if (config->has_push_to_talk) {
		mumble_log(LOG_WARN, "%s[%d] server recommends push-to-talk", METATABLE_CLIENT, self);
		lua_pushboolean(l, config->push_to_talk);
		lua_setfield(l, -2, "push_to_talk");
	}
	hookCall("OnSuggestConfig", 1);

	mumble_proto__suggest_config__free_unpacked(config, NULL);
}

void MumbleClient::onPluginData(MumblePacket *packet) {
	MumbleProto__PluginDataTransmission *transmission = mumble_proto__plugin_data_transmission__unpack(NULL, packet->length, packet->body);
	if (transmission == NULL) {
		mumble_log(LOG_WARN, "[TCP] Error unpacking data transmission packet");
		return;
	}

	mumble_log(LOG_TRACE, "[TCP] Received %s: %p", transmission->base.descriptor->name, transmission);

	lua_newtable(l);
	if (transmission->has_sendersession) {
		pushUser(transmission->sendersession);
		lua_setfield(l, -2, "sender");
	}

	if (transmission->n_receiversessions > 0) {
		lua_newtable(l);
		for (uint32_t i = 0; i < transmission->n_receiversessions; i++) {
			lua_pushinteger(l, i + 1);
			pushUser(transmission->receiversessions[i]);
			lua_settable(l, -3);
		}
		lua_setfield(l, -2, "receivers");
	}

	if (transmission->has_data) {
		lua_pushlstring(l, (char*) transmission->data.data, transmission->data.len);
		lua_setfield(l, -2, "data");
	}
	if (transmission->dataid != NULL) {
		lua_pushstring(l, transmission->dataid);
		lua_setfield(l, -2, "id");
	}
	hookCall("OnPluginData", 1);

	mumble_proto__plugin_data_transmission__free_unpacked(transmission, NULL);
}

const MumbleClient::PacketHandler MumbleClient::packetHandlers[NUM_PACKETS] = {
	/*  0 */ &MumbleClient::onServerVersion,
	/*  1 */ &MumbleClient::onUDPTunnel,
	/*  2 */ NULL, // Authenticate
	/*  3 */ &MumbleClient::onPing,
	/*  4 */ &MumbleClient::onReject,
	/*  5 */ &MumbleClient::onServerSync,
	/*  6 */ &MumbleClient::onChannelRemove,
	/*  7 */ &MumbleClient::onChannelState,
	/*  8 */ &MumbleClient::onUserRemove,
	/*  9 */ &MumbleClient::onUserState,
	/* 10 */ &MumbleClient::onBanList,
	/* 11 */ &MumbleClient::onTextMessage,
	/* 12 */ &MumbleClient::onPermissionDenied,
	/* 13 */ &MumbleClient::onACL,
	/* 14 */ &MumbleClient::onQueryUsers,
	/* 15 */ &MumbleClient::onCryptSetup,
	/* 16 */ &MumbleClient::onContextActionModify,
	/* 17 */ NULL, // Context Action
	/* 18 */ &MumbleClient::onUserList,
	/* 19 */ NULL, // VoiceTarget
	/* 20 */ &MumbleClient::onPermissionQuery,
	/* 21 */ &MumbleClient::onCodecVersion,
	/* 22 */ &MumbleClient::onUserStats,
	/* 23 */ NULL, // RequestBlob
	/* 24 */ &MumbleClient::onServerConfig,
	/* 25 */ &MumbleClient::onSuggestConfig,
	/* 26 */ &MumbleClient::onPluginData,
};

bool MumbleClient::handlePacket(MumblePacket *packet) {
	if (packet->type >= NUM_PACKETS) {
		return false;
	}

	PacketHandler handler = packetHandlers[packet->type];
	if (handler) {
		lua_stackguard_entry(l);
		(this->*handler)(packet);
		lua_stackguard_exit(l);
	}
	return true;
}
