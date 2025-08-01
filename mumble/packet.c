#include "mumble.h"

#include "packet.h"
#include "ocb.h"
#include "user.h"
#include "client.h"
#include "util.h"
#include "log.h"

#include <openssl/ssl.h>
#include <math.h>

#define SAFE_STRDUP(dest, src) \
	do { \
		if ((dest) != NULL) { \
			free(dest); \
		} \
		(dest) = (src) ? strdup((char*)src) : NULL; \
	} while (0)

#define SAFE_STRNDUP(dest, src, n) \
	do { \
		if ((dest) != NULL) { \
			free(dest); \
		} \
		(dest) = (src) ? strndup((char*)(src), (n)) : NULL; \
	} while (0)

void on_send(uv_udp_send_t* req, int status) {
	uint8_t* encrypted = (uint8_t*) req->data;

	if (status < 0) {
		mumble_log(LOG_ERROR, "[UDP] Error sending UDP packet: %s", uv_strerror(status));
	}

	free(encrypted);
	free(req);
}

int packet_sendudp(MumbleClient* client, const void *message, const int length) {
	uint8_t* encrypted = malloc(length + 4);

	if (!encrypted) {
		mumble_log(LOG_ERROR, "failed to malloc encrypted data buffer: %s", strerror(errno));
		return 0;
	}

	if (crypt_isValid(client->crypt) && crypt_encrypt(client->crypt, message, encrypted, length)) {
		uv_buf_t buf = uv_buf_init((char*)encrypted, length + 4);

		uv_udp_send_t* req = malloc(sizeof(uv_udp_send_t));

		if (!req) {
			mumble_log(LOG_ERROR, "failed to malloc send request: %s", strerror(errno));
			free(encrypted);
			return 0;
		}

		req->data = encrypted;

		int ret = uv_udp_send(req, &client->socket_udp, &buf, 1, NULL, on_send);

		if (ret < 0) {
			mumble_log(LOG_ERROR, "[UDP] Unable to send UDP packet: %s", uv_strerror(ret));
			free(encrypted);
			free(req);
			return 0;
		}

		return ret;
	} else {
		mumble_log(LOG_ERROR, "[UDP] Unable to encrypt UDP packet");
		free(encrypted);
		return 0;
	}
}

int packet_sendex(MumbleClient* client, int type, const void *message, const ProtobufCMessage* base, const int length) {
	uint8_t *packet_out;
	size_t payload_size = 0;
	size_t total_size = 0;
	switch (type) {
	case PACKET_VERSION:
		payload_size = mumble_proto__version__get_packed_size(message);
		break;
	case PACKET_UDPTUNNEL:
		payload_size = length;
		break;
	case PACKET_AUTHENTICATE:
		payload_size = mumble_proto__authenticate__get_packed_size(message);
		break;
	case PACKET_PING:
		payload_size = mumble_proto__ping__get_packed_size(message);
		break;
	case PACKET_CHANNELREMOVE:
		payload_size = mumble_proto__channel_remove__get_packed_size(message);
		break;
	case PACKET_CHANNELSTATE:
		payload_size = mumble_proto__channel_state__get_packed_size(message);
		break;
	case PACKET_USERREMOVE:
		payload_size = mumble_proto__user_remove__get_packed_size(message);
		break;
	case PACKET_USERSTATE:
		payload_size = mumble_proto__user_state__get_packed_size(message);
		break;
	case PACKET_BANLIST:
		payload_size = mumble_proto__ban_list__get_packed_size(message);
		break;
	case PACKET_TEXTMESSAGE:
		payload_size = mumble_proto__text_message__get_packed_size(message);
		break;
	case PACKET_ACL:
		payload_size = mumble_proto__acl__get_packed_size(message);
		break;
	case PACKET_QUERYUSERS:
		payload_size = mumble_proto__query_users__get_packed_size(message);
		break;
	case PACKET_CRYPTSETUP:
		payload_size = mumble_proto__crypt_setup__get_packed_size(message);
		break;
	case PACKET_CONTEXTACTION:
		payload_size = mumble_proto__context_action__get_packed_size(message);
		break;
	case PACKET_PERMISSIONQUERY:
		payload_size = mumble_proto__permission_query__get_packed_size(message);
		break;
	case PACKET_USERLIST:
		payload_size = mumble_proto__user_list__get_packed_size(message);
		break;
	case PACKET_VOICETARGET:
		payload_size = mumble_proto__voice_target__get_packed_size(message);
		break;
	case PACKET_CODECVERSION:
		payload_size = mumble_proto__codec_version__get_packed_size(message);
		break;
	case PACKET_USERSTATS:
		payload_size = mumble_proto__user_stats__get_packed_size(message);
		break;
	case PACKET_REQUESTBLOB:
		payload_size = mumble_proto__request_blob__get_packed_size(message);
		break;
	case PACKET_PLUGINDATA:
		payload_size = mumble_proto__plugin_data_transmission__get_packed_size(message);
		break;
	default:
		mumble_log(LOG_WARN, "unable to get payload size for packet #%i", type);
		return 1;
	}

	total_size = PACKET_HEADER_SIZE + payload_size;
	packet_out = malloc(sizeof(uint8_t) * total_size);

	if (packet_out == NULL) {
		mumble_log(LOG_ERROR, "failed to malloc packet buffer: %s", strerror(errno));
		return 2;
	}

	if (payload_size > 0) {
		switch (type) {
		case PACKET_VERSION:
			mumble_proto__version__pack(message, packet_out + PACKET_HEADER_SIZE);
			break;
		case PACKET_UDPTUNNEL:
			memmove(packet_out + PACKET_HEADER_SIZE, message, length);
			break;
		case PACKET_AUTHENTICATE:
			mumble_proto__authenticate__pack(message, packet_out + PACKET_HEADER_SIZE);
			break;
		case PACKET_PING:
			mumble_proto__ping__pack(message, packet_out + PACKET_HEADER_SIZE);
			break;
		case PACKET_CHANNELREMOVE:
			mumble_proto__channel_remove__pack(message, packet_out + PACKET_HEADER_SIZE);
			break;
		case PACKET_CHANNELSTATE:
			mumble_proto__channel_state__pack(message, packet_out + PACKET_HEADER_SIZE);
			break;
		case PACKET_USERREMOVE:
			mumble_proto__user_remove__pack(message, packet_out + PACKET_HEADER_SIZE);
			break;
		case PACKET_USERSTATE:
			mumble_proto__user_state__pack(message, packet_out + PACKET_HEADER_SIZE);
			break;
		case PACKET_BANLIST:
			mumble_proto__ban_list__pack(message, packet_out + PACKET_HEADER_SIZE);
			break;
		case PACKET_TEXTMESSAGE:
			mumble_proto__text_message__pack(message, packet_out + PACKET_HEADER_SIZE);
			break;
		case PACKET_ACL:
			mumble_proto__acl__pack(message, packet_out + PACKET_HEADER_SIZE);
			break;
		case PACKET_QUERYUSERS:
			mumble_proto__query_users__pack(message, packet_out + PACKET_HEADER_SIZE);
			break;
		case PACKET_CRYPTSETUP:
			mumble_proto__crypt_setup__pack(message, packet_out + PACKET_HEADER_SIZE);
			break;
		case PACKET_CONTEXTACTION:
			mumble_proto__context_action__pack(message, packet_out + PACKET_HEADER_SIZE);
			break;
		case PACKET_PERMISSIONQUERY:
			mumble_proto__permission_query__pack(message, packet_out + PACKET_HEADER_SIZE);
			break;
		case PACKET_USERLIST:
			mumble_proto__user_list__pack(message, packet_out + PACKET_HEADER_SIZE);
			break;
		case PACKET_VOICETARGET:
			mumble_proto__voice_target__pack(message, packet_out + PACKET_HEADER_SIZE);
			break;
		case PACKET_CODECVERSION:
			mumble_proto__codec_version__pack(message, packet_out + PACKET_HEADER_SIZE);
			break;
		case PACKET_USERSTATS:
			mumble_proto__user_stats__pack(message, packet_out + PACKET_HEADER_SIZE);
			break;
		case PACKET_REQUESTBLOB:
			mumble_proto__request_blob__pack(message, packet_out + PACKET_HEADER_SIZE);
			break;
		case PACKET_PLUGINDATA:
			mumble_proto__plugin_data_transmission__pack(message, packet_out + PACKET_HEADER_SIZE);
			break;
		default:
			mumble_log(LOG_WARN, "attempted to pack unspported packet #%i", type);
			break;
		}
	}
	*(uint16_t *)packet_out = htons(type);
	*(uint32_t *)(packet_out + 2) = htonl(payload_size);

	mumble_log(LOG_TRACE, "[TCP] Sending %s: %p", base != NULL ? base->descriptor->name : "MumbleProto.UDPTunnel", message);

	int written = SSL_write(client->ssl, packet_out, total_size);

	free(packet_out);

	return written == total_size ? 0 : -1;
}

void packet_server_version(MumbleClient *client, MumblePacket *packet) {
	MumbleProto__Version *version =  mumble_proto__version__unpack(NULL, packet->length, packet->body);
	if (version == NULL) {
		mumble_log(LOG_WARN, "[TCP] Error unpacking server version packet");
		return;
	}

	mumble_log(LOG_TRACE, "[TCP] Received %s: %p", version->base.descriptor->name, version);

	mumble_log(LOG_INFO, "%s[%d] server version: %s", METATABLE_CLIENT, client->self, version->release);
	mumble_log(LOG_INFO, "%s[%d] server system : %s - %s", METATABLE_CLIENT, client->self, version->os, version->os_version);

	lua_State* l = client->l;

	lua_newtable(l);
	if (version->has_version_v1) {
		lua_pushinteger(l, version->version_v1);
		lua_setfield(l, -2, "version_v1");
	}
	if (version->has_version_v2) {
		lua_pushinteger(l, version->version_v2);
		lua_setfield(l, -2, "version_v2");
	} else if (!client->legacy) {
		// Protobuf supported client connected to an older server
		mumble_log(LOG_WARN, "falling back to legacy packets for legacy server");
		client->legacy = true;
	}
	lua_pushstring(l, version->release);
	lua_setfield(l, -2, "release");
	lua_pushstring(l, version->os);
	lua_setfield(l, -2, "os");
	lua_pushstring(l, version->os_version);
	lua_setfield(l, -2, "os_version");
	mumble_hook_call(client, "OnServerVersion", 1);

	mumble_proto__version__free_unpacked(version, NULL);
}

void packet_tcp_udp_tunnel(MumbleClient *client, MumblePacket *packet) {
	mumble_handle_udp_packet(client, packet->body, packet->length, false);
}

void packet_server_ping(MumbleClient *client, MumblePacket *packet) {
	MumbleProto__Ping *ping = mumble_proto__ping__unpack(NULL, packet->length, packet->body);
	if (ping == NULL) {
		mumble_log(LOG_WARN, "[TCP] Error unpacking TCP ping packet");
		return;
	}

	mumble_log(LOG_TRACE, "[TCP] Received %s: %p", ping->base.descriptor->name, ping);

	lua_State* l = client->l;

	lua_newtable(l);
	if (ping->has_timestamp) {
		double time = mumble_update_ping_tcp(client, ping->timestamp);
		lua_pushinteger(l, ping->timestamp);
		lua_setfield(l, -2, "timestamp");
		lua_pushnumber(l, time);
		lua_setfield(l, -2, "ping");
		lua_pushnumber(l, client->udp_ping_avg);
		lua_setfield(l, -2, "average");
		lua_pushnumber(l, client->udp_ping_var);
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
	mumble_hook_call(client, "OnPongTCP", 1);

	mumble_proto__ping__free_unpacked(ping, NULL);
}

void packet_server_reject(MumbleClient *client, MumblePacket *packet) {
	MumbleProto__Reject *reject = mumble_proto__reject__unpack(NULL, packet->length, packet->body);
	if (reject == NULL) {
		mumble_log(LOG_WARN, "[TCP] Error unpacking server reject packet");
		return;
	}

	mumble_log(LOG_TRACE, "[TCP] Received %s: %p", reject->base.descriptor->name, reject);

	mumble_log(LOG_WARN, "%s[%d] server rejected connection: %s", METATABLE_CLIENT, client->self, reject->reason);

	lua_State* l = client->l;

	lua_newtable(l);
	if (reject->has_type) {
		lua_pushinteger(l, reject->type);
		lua_setfield(l, -2, "type");
	}
	lua_pushstring(l, reject->reason);
	lua_setfield(l, -2, "reason");
	mumble_hook_call(client, "OnServerReject", 1);

	mumble_proto__reject__free_unpacked(reject, NULL);
}

void packet_server_sync(MumbleClient *client, MumblePacket *packet) {
	MumbleProto__ServerSync *sync = mumble_proto__server_sync__unpack(NULL, packet->length, packet->body);
	if (sync == NULL) {
		mumble_log(LOG_WARN, "[TCP] Error unpacking server sync packet");
		return;
	}

	mumble_log(LOG_TRACE, "[TCP] Received %s: %p", sync->base.descriptor->name, sync);

	client->synced = true;
	uv_timer_start(&client->ping_timer, mumble_ping_timer, PING_TIME, PING_TIME);

	lua_State* l = client->l;

	lua_newtable(l);
	if (sync->has_session) {
		client->session = sync->session;
		mumble_user_raw_get(client, sync->session);
		lua_setfield(l, -2, "user");
	}
	if (sync->has_max_bandwidth) {
		lua_pushinteger(l, sync->max_bandwidth);
		lua_setfield(l, -2, "max_bandwidth");

		client->max_bandwidth = sync->max_bandwidth;

		mumble_adjust_audio_bandwidth(client);
	}
	if (sync->welcome_text != NULL) {
		lua_pushstring(l, sync->welcome_text);
		lua_setfield(l, -2, "welcome_text");
	}
	if (sync->has_permissions) {
		lua_pushinteger(l, sync->permissions);
		lua_setfield(l, -2, "permissions");

		MumbleChannel* root = mumble_channel_get(client, 0);
		root->permissions = sync->permissions;
	}
	mumble_hook_call(client, "OnServerSync", 1);

	mumble_proto__server_sync__free_unpacked(sync, NULL);
}

void packet_channel_remove(MumbleClient *client, MumblePacket *packet) {
	MumbleProto__ChannelRemove *channel = mumble_proto__channel_remove__unpack(NULL, packet->length, packet->body);
	if (channel == NULL) {
		mumble_log(LOG_WARN, "[TCP] Error unpacking channel remove packet");
		return;
	}

	mumble_log(LOG_TRACE, "[TCP] Received %s: %p", channel->base.descriptor->name, channel);

	mumble_channel_raw_get(client, channel->channel_id);
	mumble_hook_call(client, "OnChannelRemove", 1);
	mumble_channel_remove(client, channel->channel_id);

	mumble_proto__channel_remove__free_unpacked(channel, NULL);
}

void packet_channel_state(MumbleClient *client, MumblePacket *packet) {
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

	lua_State* l = client->l;
	MumbleChannel* channel = mumble_channel_get(client, state->channel_id);

	lua_newtable(l);
	mumble_channel_raw_get(client, channel->channel_id);
	lua_setfield(l , -2, "channel");

	lua_pushinteger(l, channel->channel_id);
	lua_setfield(l , -2, "channel_id");

	if (state->has_parent) {
		channel->parent = state->parent;
		mumble_channel_raw_get(client, channel->parent);
		lua_setfield(l , -2, "parent");
	}
	if (state->name != NULL) {
		SAFE_STRDUP(channel->name, state->name);
		lua_pushstring(l, channel->name);
		lua_setfield(l , -2, "name");
	}
	if (state->description != NULL) {
		SAFE_STRDUP(channel->description, state->description);
		lua_pushstring(l, channel->description);
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
		SAFE_STRNDUP(channel->description_hash, state->description_hash.data, state->description_hash.len);
		channel->description_hash_len = state->description_hash.len;

		char* result;
		bin_to_strhex((char*) channel->description_hash, channel->description_hash_len, &result);
		lua_pushstring(l, result);
		lua_setfield(l, -2, "description_hash");
		free(result);
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
			list_add(&channel->links, state->links_add[i], NULL);
			lua_pushinteger(l, i + 1);
			mumble_channel_raw_get(client, state->links_add[i]);
			lua_settable(l, -3);
		}
		lua_setfield(l , -2, "links_add");
	}
	if (state->n_links_remove > 0) {
		lua_newtable(l);
		for (uint32_t i = 0; i < state->n_links_remove; i++) {
			list_remove(&channel->links, state->links_remove[i]);
			lua_pushinteger(l, i + 1);
			mumble_channel_raw_get(client, state->links_remove[i]);
			lua_settable(l, -3);
		}
		lua_setfield(l , -2, "links_remove");
	}
	if (state->n_links > 0) {
		list_clear(&channel->links);

		lua_newtable(l);
		// Store links in new list
		for (uint32_t i = 0; i < state->n_links; i++) {
			list_add(&channel->links, state->links[i], NULL);
			lua_pushinteger(l, i + 1);
			mumble_channel_raw_get(client, state->links[i]);
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

	mumble_hook_call(client, "OnChannelState", 1);

	mumble_proto__channel_state__free_unpacked(state, NULL);
}

void packet_user_remove(MumbleClient *client, MumblePacket *packet) {
	MumbleProto__UserRemove *user = mumble_proto__user_remove__unpack(NULL, packet->length, packet->body);
	if (user == NULL) {
		mumble_log(LOG_WARN, "[TCP] Error unpacking user remove packet");
		return;
	}

	mumble_log(LOG_TRACE, "[TCP] Received %s: %p", user->base.descriptor->name, user);

	lua_State* l = client->l;

	lua_newtable(l);
	mumble_user_raw_get(client, user->session);
	lua_setfield(l, -2, "user");
	if (user->has_actor) {
		mumble_user_raw_get(client, user->actor);
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
	mumble_hook_call(client, "OnUserRemove", 1);

	if (client->session == user->session) {
		char* type = (user->has_ban && user->ban) ? "banned" : "kicked";
		char* reason = (user->reason != NULL && strcmp(user->reason, "") != 0) ? user->reason : "No reason given";

		const char* message;
		if (user->has_actor && user->has_ban) {
			MumbleUser* actor = mumble_user_get(client, user->actor);
			message = lua_pushfstring(l, "%s from server by %s (Reason \"%s\")", type, actor->name, reason);
			mumble_log(LOG_INFO, "%s[%d] %s from server by %s [%d][\"%s\"] (Reason \"%s\")", METATABLE_CLIENT, client->self, type, METATABLE_USER, actor->session, actor->name, reason);
		} else {
			message = lua_pushfstring(l, "%s from server (Reason \"%s\")", type, reason);
			mumble_log(LOG_INFO, "%s[%d] %s from server (Reason \"%s\")", METATABLE_CLIENT, client->self, type, reason);
		}
		lua_pop(l, 1);
		mumble_disconnect(client, message, false);
	} else {
		mumble_user_remove(client, user->session);
	}

	mumble_proto__user_remove__free_unpacked(user, NULL);
}

void packet_user_state(MumbleClient *client, MumblePacket *packet) {
	MumbleProto__UserState *state = mumble_proto__user_state__unpack(NULL, packet->length, packet->body);
	if (state == NULL) {
		mumble_log(LOG_WARN, "[TCP] Error unpacking user state packet");
		return;
	}

	mumble_log(LOG_TRACE, "[TCP] Received %s: %p", state->base.descriptor->name, state);

	lua_State* l = client->l;

	if (!state->has_session) {
		mumble_proto__user_state__free_unpacked(state, NULL);
		return;
	}

	MumbleUser* user = mumble_user_get(client, state->session);

	lua_newtable(l);
	if (state->has_actor) {
		mumble_user_raw_get(client, state->actor);
		lua_setfield(l, -2, "actor");
	}

	user->session = state->session;
	lua_pushinteger(l, user->session);
	lua_setfield(l, -2, "session");

	if (state->name != NULL) {
		SAFE_STRDUP(user->name, state->name);
		lua_pushstring(l, user->name);
		lua_setfield(l, -2, "name");
	}
	if (state->has_channel_id) {
		if (user->connected == true && client->synced == true && user->channel_id != state->channel_id) {
			lua_newtable(l);
			if (state->has_actor) {
				mumble_user_raw_get(client, state->actor);
				lua_setfield(l, -2, "actor");
			}
			mumble_channel_raw_get(client, user->channel_id);
			lua_setfield(l, -2, "from");
			mumble_channel_raw_get(client, state->channel_id);
			lua_setfield(l, -2, "to");
			mumble_user_raw_get(client, state->session);
			lua_setfield(l, -2, "user");
			mumble_hook_call(client, "OnUserChannel", 1);
		}
		user->channel_id = state->channel_id;
		mumble_channel_raw_get(client, user->channel_id);
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
		SAFE_STRDUP(user->comment, state->comment);
		lua_pushstring(l, user->comment);
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
		SAFE_STRNDUP(user->texture, state->texture.data, state->texture.len);
		lua_pushlstring(l, user->texture, state->texture.len);
		lua_setfield(l, -2, "texture");
	}
	if (state->hash != NULL) {
		SAFE_STRDUP(user->hash, state->hash);
		lua_pushstring(l, user->hash);
		lua_setfield(l, -2, "hash");
	}
	if (state->has_comment_hash) {
		SAFE_STRNDUP(user->comment_hash, state->comment_hash.data, state->comment_hash.len);
		user->comment_hash_len = state->comment_hash.len;

		char* result;
		bin_to_strhex((char*) user->comment_hash, user->comment_hash_len, &result);
		lua_pushstring(l, result);
		lua_setfield(l, -2, "comment_hash");
		free(result);
	}
	if (state->has_texture_hash) {
		SAFE_STRNDUP(user->texture_hash, state->texture_hash.data, state->texture_hash.len);
		user->texture_hash_len = state->texture_hash.len;

		char* result;
		bin_to_strhex((char*) user->texture_hash, user->texture_hash_len, &result);
		lua_pushstring(l, result);
		lua_setfield(l, -2, "texture_hash");
		free(result);
	}
	if (state->n_listening_channel_add > 0) {
		// Add the new entries to the head of the list
		lua_newtable(l);
		for (uint32_t i = 0; i < state->n_listening_channel_add; i++) {
			list_add(&user->listens, state->listening_channel_add[i], NULL);
			lua_pushinteger(l, i + 1);
			mumble_channel_raw_get(client, state->listening_channel_add[i]);
			lua_settable(l, -3);
		}
		lua_setfield(l , -2, "listening_channel_add");
	}
	if (state->n_listening_channel_remove > 0) {
		lua_newtable(l);
		for (uint32_t i = 0; i < state->n_listening_channel_remove; i++) {
			list_remove(&user->listens, state->listening_channel_remove[i]);
			lua_pushinteger(l, i + 1);
			mumble_channel_raw_get(client, state->listening_channel_remove[i]);
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

			MumbleChannel* chan = mumble_channel_get(client, channel_id);
			chan->listening_volume_adjustment = volume_adjustment;
		}
		lua_setfield(l , -2, "listening_volume_adjustment");
	}

	mumble_user_raw_get(client, state->session);
	lua_setfield(l, -2, "user");

	if (user->connected == false) {
		user->connected = true;

		if (client->synced == true) {
			lua_pushvalue(l, -1); // Push a copy of the event table we will send to the 'OnUserState' hook
			mumble_hook_call(client, "OnUserConnect", 1);
		}
	}
	mumble_hook_call(client, "OnUserState", 1);

	mumble_proto__user_state__free_unpacked(state, NULL);
}

void packet_ban_list(MumbleClient *client, MumblePacket *packet) {
	MumbleProto__BanList *list = mumble_proto__ban_list__unpack(NULL, packet->length, packet->body);
	if (list == NULL) {
		mumble_log(LOG_WARN, "[TCP] Error unpacking ban list packet");
		return;
	}

	mumble_log(LOG_TRACE, "[TCP] Received %s: %p", list->base.descriptor->name, list);

	lua_State* l = client->l;

	lua_newtable(l);
	if (list->n_bans > 0) {
		for (uint32_t i = 0; i < list->n_bans; i++) {
			MumbleProto__BanList__BanEntry *ban = list->bans[i];
			lua_pushinteger(l, i + 1);
			lua_newtable(l);
			mumble_push_address(l, ban->address);
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
	mumble_hook_call(client, "OnBanList", 1);

	mumble_proto__ban_list__free_unpacked(list, NULL);
}

void packet_text_message(MumbleClient *client, MumblePacket *packet) {
	MumbleProto__TextMessage *msg = mumble_proto__text_message__unpack(NULL, packet->length, packet->body);
	if (msg == NULL) {
		mumble_log(LOG_WARN, "[TCP] Error unpacking text message packet");
		return;
	}

	mumble_log(LOG_TRACE, "[TCP] Received %s: %p", msg->base.descriptor->name, msg);

	lua_State* l = client->l;

	lua_newtable(l);
	if (msg->has_actor) {
		mumble_user_raw_get(client, msg->actor);
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
			mumble_user_raw_get(client, msg->session[i]);
			lua_settable(l, -3);
		}
		lua_setfield(l, -2, "users");
	}
	if (msg->n_channel_id > 0) {
		lua_newtable(l);
		for (uint32_t i = 0; i < msg->n_channel_id; i++) {
			lua_pushinteger(l, i + 1);
			mumble_channel_raw_get(client, msg->channel_id[i]);
			lua_settable(l, -3);
		}
		lua_setfield(l, -2, "channels");
	} else {
		lua_pushboolean(l, true);
		lua_setfield(l, -2, "direct");
	}
	mumble_hook_call(client, "OnMessage", 1);

	mumble_proto__text_message__free_unpacked(msg, NULL);
}

void packet_permission_denied(MumbleClient *client, MumblePacket *packet) {
	MumbleProto__PermissionDenied *proto = mumble_proto__permission_denied__unpack(NULL, packet->length, packet->body);
	if (proto == NULL) {
		mumble_log(LOG_WARN, "[TCP] Error unpacking permission denied packet");
		return;
	}

	mumble_log(LOG_TRACE, "[TCP] Received %s: %p", proto->base.descriptor->name, proto);

	lua_State* l = client->l;

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
		mumble_channel_raw_get(client, proto->channel_id);
		lua_setfield(l, -2, "channel");
	}
	if (proto->has_session) {
		mumble_user_raw_get(client, proto->session);
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
	mumble_hook_call(client, "OnPermissionDenied", 1);

	mumble_proto__permission_denied__free_unpacked(proto, NULL);
}

void packet_acl(MumbleClient *client, MumblePacket *packet) {
	MumbleProto__ACL *acl = mumble_proto__acl__unpack(NULL, packet->length, packet->body);
	if (acl == NULL) {
		mumble_log(LOG_WARN, "[TCP] Error unpacking ACL packet");
		return;
	}

	mumble_log(LOG_TRACE, "[TCP] Received %s: %p", acl->base.descriptor->name, acl);

	lua_State* l = client->l;

	lua_newtable(l);
	mumble_channel_raw_get(client, acl->channel_id);
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
	mumble_hook_call(client, "OnACL", 1);

	mumble_proto__acl__free_unpacked(acl, NULL);
}

void packet_query_users(MumbleClient *client, MumblePacket *packet) {
	MumbleProto__QueryUsers *users = mumble_proto__query_users__unpack(NULL, packet->length, packet->body);
	if (users == NULL) {
		mumble_log(LOG_WARN, "[TCP] Error unpacking query users packet");
		return;
	}

	mumble_log(LOG_TRACE, "[TCP] Received %s: %p", users->base.descriptor->name, users);

	lua_State* l = client->l;

	lua_newtable(l);
	if (users->n_ids > 0 && users->n_ids == users->n_names) {
		for (uint32_t i = 0; i < users->n_ids; i++) {
			uint32_t id = users->ids[i];
			char* name = users->names[i];

			lua_pushinteger(l, id);
			lua_pushstring(l, name);
			lua_settable(l, -3);
		}
	}
	mumble_hook_call(client, "OnQueryUsers", 1);

	mumble_proto__query_users__free_unpacked(users, NULL);
}

void packet_context_action_modify(MumbleClient *client, MumblePacket *packet) {
	MumbleProto__ContextActionModify *modify = mumble_proto__context_action_modify__unpack(NULL, packet->length, packet->body);
	if (modify == NULL) {
		mumble_log(LOG_WARN, "[TCP] Error unpacking context action modify packet");
		return;
	}

	mumble_log(LOG_TRACE, "[TCP] Received %s: %p", modify->base.descriptor->name, modify);

	lua_State* l = client->l;

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
	mumble_hook_call(client, "OnContextActionModify", 1);

	mumble_proto__context_action_modify__free_unpacked(modify, NULL);
}

void packet_crypt_setup(MumbleClient *client, MumblePacket *packet) {
	MumbleProto__CryptSetup *crypt = mumble_proto__crypt_setup__unpack(NULL, packet->length, packet->body);
	if (crypt == NULL) {
		mumble_log(LOG_WARN, "[TCP] Error unpacking crypt setup packet");
		return;
	}

	mumble_log(LOG_TRACE, "[TCP] Received %s: %p", crypt->base.descriptor->name, crypt);

	lua_State* l = client->l;

	lua_newtable(l);
	if (crypt->has_key && crypt->has_client_nonce && crypt->has_server_nonce) {
		if (!crypt_setKey(client->crypt, crypt->key.data, crypt->key.len,
						  crypt->client_nonce.data, crypt->client_nonce.len,
						  crypt->server_nonce.data, crypt->server_nonce.len)) {
			mumble_log(LOG_ERROR, "[OCB] CryptState: cipher resync failed (Invalid key/nonce from the server)");
		}
	} else if (crypt->has_server_nonce) {
		client->resync++;
		if (!crypt_setDecryptIV(client->crypt, crypt->server_nonce.data, crypt->server_nonce.len)) {
			mumble_log(LOG_ERROR, "[OCB] CryptState: cipher resync failed (Invalid nonce from the server)");
		}
	} else {
		MumbleProto__CryptSetup msg = MUMBLE_PROTO__CRYPT_SETUP__INIT;
		msg.has_client_nonce = true;
		msg.client_nonce.len = AES_BLOCK_SIZE;
		msg.client_nonce.data = (uint8_t*) crypt_getEncryptIV(client->crypt);
		packet_send(client, PACKET_CRYPTSETUP, &msg);
		mumble_log(LOG_WARN, "[OCB] CryptState: cipher disagreement, renegotiating with server");
	}

	bool validCrypt = crypt_isValid(client->crypt);

	if (validCrypt) {
		mumble_log(LOG_INFO, "[OCB] CryptState: handshake complete");
		mumble_ping(client);
	}

	lua_pushboolean(l, validCrypt);
	lua_setfield(l, -2, "valid");

	if (crypt->has_key) {
		char* result;
		bin_to_strhex((char*) crypt->key.data, crypt->key.len, &result);
		lua_pushstring(l, result);
		lua_setfield(l, -2, "key");
		free(result);
	}
	if (crypt->has_client_nonce) {
		char* result;
		bin_to_strhex((char*) crypt->client_nonce.data, crypt->client_nonce.len, &result);
		lua_pushstring(l, result);
		lua_setfield(l, -2, "client_nonce");
		free(result);
	}
	if (crypt->has_server_nonce) {
		char* result;
		bin_to_strhex((char*) crypt->server_nonce.data, crypt->server_nonce.len, &result);
		lua_pushstring(l, result);
		lua_setfield(l, -2, "server_nonce");
		free(result);
	}
	mumble_hook_call(client, "OnCryptSetup", 1);

	mumble_proto__crypt_setup__free_unpacked(crypt, NULL);
}

void packet_user_list(MumbleClient *client, MumblePacket *packet) {
	MumbleProto__UserList *list = mumble_proto__user_list__unpack(NULL, packet->length, packet->body);
	if (list == NULL) {
		mumble_log(LOG_WARN, "[TCP] Error unpacking user list packet");
		return;
	}

	mumble_log(LOG_TRACE, "[TCP] Received %s: %p", list->base.descriptor->name, list);

	lua_State* l = client->l;

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
				mumble_channel_raw_get(client, user->last_channel);
				lua_setfield(l, -2, "last_channel");
			}
			lua_settable(l, -3);
		}
	}
	mumble_hook_call(client, "OnUserList", 1);

	mumble_proto__user_list__free_unpacked(list, NULL);
}

void packet_permission_query(MumbleClient *client, MumblePacket *packet) {
	MumbleProto__PermissionQuery *query = mumble_proto__permission_query__unpack(NULL, packet->length, packet->body);
	if (query == NULL) {
		mumble_log(LOG_WARN, "[TCP] Error unpacking permission query packet");
		return;
	}

	mumble_log(LOG_TRACE, "[TCP] Received %s: %p", query->base.descriptor->name, query);

	lua_State* l = client->l;

	if (query->has_channel_id && query->has_permissions) {
		MumbleChannel* chan = mumble_channel_get(client, query->channel_id);
		chan->permissions = query->permissions;
	} else if (query->has_flush && query->flush) {
		// Loop through all channels and set permissions to 0
		mumble_pushref(l, client->channels);
		lua_pushnil(l);
		while (lua_next(l, -2)) {
			if (lua_isuserdata(l, -1) == 1) {
				MumbleChannel* channel = lua_touserdata(l, -1);
				channel->permissions = 0;
			}
			lua_pop(l, 1);
		}
		lua_pop(l, 1);
	}

	lua_newtable(l);
	if (query->has_channel_id) {
		mumble_channel_raw_get(client, query->channel_id);
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
	mumble_hook_call(client, "OnPermissionQuery", 1);

	mumble_proto__permission_query__free_unpacked(query, NULL);
}

void packet_codec_version(MumbleClient *client, MumblePacket *packet) {
	MumbleProto__CodecVersion *codec = mumble_proto__codec_version__unpack(NULL, packet->length, packet->body);
	if (codec == NULL) {
		mumble_log(LOG_WARN, "[TCP] Error unpacking codec version packet");
		return;
	}

	mumble_log(LOG_TRACE, "[TCP] Received %s: %p", codec->base.descriptor->name, codec);

	lua_State* l = client->l;

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
	mumble_hook_call(client, "OnCodecVersion", 1);

	mumble_proto__codec_version__free_unpacked(codec, NULL);
}

void packet_user_stats(MumbleClient *client, MumblePacket *packet) {
	MumbleProto__UserStats *stats = mumble_proto__user_stats__unpack(NULL, packet->length, packet->body);
	if (stats == NULL) {
		mumble_log(LOG_WARN, "[TCP] Error unpacking user stats packet");
		return;
	}

	mumble_log(LOG_TRACE, "[TCP] Received %s: %p", stats->base.descriptor->name, stats);

	lua_State* l = client->l;

	lua_newtable(l);
	if (stats->has_session) {
		mumble_user_raw_get(client, stats->session);
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
				lua_setfield(l, -2, "version");
			} else if (stats->version->has_version_v2) {
				lua_pushinteger(l, stats->version->version_v2);
				lua_setfield(l, -2, "version");
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
		mumble_push_address(l, stats->address);
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

	mumble_hook_call(client, "OnUserStats", 1);

	mumble_proto__user_stats__free_unpacked(stats, NULL);
}

void packet_server_config(MumbleClient *client, MumblePacket *packet) {
	MumbleProto__ServerConfig *config = mumble_proto__server_config__unpack(NULL, packet->length, packet->body);
	if (config == NULL) {
		mumble_log(LOG_WARN, "[TCP] Error unpacking server config packet");
		return;
	}

	mumble_log(LOG_TRACE, "[TCP] Received %s: %p", config->base.descriptor->name, config);

	lua_State* l = client->l;

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
	mumble_hook_call(client, "OnServerConfig", 1);

	mumble_proto__server_config__free_unpacked(config, NULL);
}

void packet_suggest_config(MumbleClient *client, MumblePacket *packet) {
	MumbleProto__SuggestConfig *config = mumble_proto__suggest_config__unpack(NULL, packet->length, packet->body);
	if (config == NULL) {
		mumble_log(LOG_WARN, "[TCP] Error unpacking suggested config packet");
		return;
	}

	mumble_log(LOG_TRACE, "[TCP] Received %s: %p", config->base.descriptor->name, config);

	lua_State* l = client->l;

	lua_newtable(l);
	if (config->has_version_v1) {
		lua_pushinteger(l, config->version_v1);
		lua_setfield(l, -2, "version");
	} else if (config->has_version_v2) {
		lua_pushinteger(l, config->version_v2);
		lua_setfield(l, -2, "version");
	}
	if (config->has_positional) {
		mumble_log(LOG_WARN, "%s[%d] server recommends posisional audio", METATABLE_CLIENT, client->self);
		lua_pushboolean(l, config->positional);
		lua_setfield(l, -2, "positional");
	}
	if (config->has_push_to_talk) {
		mumble_log(LOG_WARN, "%s[%d] server recommends push-to-talk", METATABLE_CLIENT, client->self);
		lua_pushboolean(l, config->push_to_talk);
		lua_setfield(l, -2, "push_to_talk");
	}
	mumble_hook_call(client, "OnSuggestConfig", 1);

	mumble_proto__suggest_config__free_unpacked(config, NULL);
}

void packet_plugin_data(MumbleClient *client, MumblePacket *packet) {
	MumbleProto__PluginDataTransmission *transmission = mumble_proto__plugin_data_transmission__unpack(NULL, packet->length, packet->body);
	if (transmission == NULL) {
		mumble_log(LOG_WARN, "[TCP] Error unpacking data transmission packet");
		return;
	}

	mumble_log(LOG_TRACE, "[TCP] Received %s: %p", transmission->base.descriptor->name, transmission);

	lua_State* l = client->l;

	lua_newtable(l);
	if (transmission->has_sendersession) {
		mumble_user_raw_get(client, transmission->sendersession);
		lua_setfield(l, -2, "sender");
	}

	if (transmission->n_receiversessions > 0) {
		lua_newtable(l);
		for (uint32_t i = 0; i < transmission->n_receiversessions; i++) {
			lua_pushinteger(l, i + 1);
			mumble_user_raw_get(client, transmission->receiversessions[i]);
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
	mumble_hook_call(client, "OnPluginData", 1);

	mumble_proto__plugin_data_transmission__free_unpacked(transmission, NULL);
}

const Packet_Handler_Func packet_handler[NUM_PACKETS] = {
	/*  0 */ packet_server_version,
	/*  1 */ packet_tcp_udp_tunnel,
	/*  2 */ NULL, // Authenticate
	/*  3 */ packet_server_ping,
	/*  4 */ packet_server_reject,
	/*  5 */ packet_server_sync,
	/*  6 */ packet_channel_remove,
	/*  7 */ packet_channel_state,
	/*  8 */ packet_user_remove,
	/*  9 */ packet_user_state,
	/* 10 */ packet_ban_list,
	/* 11 */ packet_text_message,
	/* 12 */ packet_permission_denied,
	/* 13 */ packet_acl,
	/* 14 */ packet_query_users,
	/* 15 */ packet_crypt_setup,
	/* 16 */ packet_context_action_modify,
	/* 17 */ NULL, // Context Action
	/* 18 */ packet_user_list,
	/* 19 */ NULL, // VoiceTarget
	/* 20 */ packet_permission_query,
	/* 21 */ packet_codec_version,
	/* 22 */ packet_user_stats,
	/* 23 */ NULL, // RequestBlob
	/* 24 */ packet_server_config,
	/* 25 */ packet_suggest_config,
	/* 26 */ packet_plugin_data,
};