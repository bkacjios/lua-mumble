/*
 * piepie - bot framework for Mumble
 *
 * Author: Tim Cooper <tim.cooper@layeh.com>
 * License: MIT (see LICENSE)
 *
 * This file contains handlers for the messages that are received from the
 * server.
 */

#include "mumble.h"

int packet_sendex(MumbleClient* client, const int type, const void *message, const int length)
{
	static Packet packet_out;
	int payload_size;
	int total_size;
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
		case PACKET_TEXTMESSAGE:
			payload_size = mumble_proto__text_message__get_packed_size(message);
			break;
		case PACKET_USERREMOVE:
			payload_size = mumble_proto__user_remove__get_packed_size(message);
			break;
		case PACKET_USERSTATE:
			payload_size = mumble_proto__user_state__get_packed_size(message);
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
		default:
			return 1;
	}
	if (payload_size >= PAYLOAD_SIZE_MAX) {
		return 2;
	}
	total_size = sizeof(uint16_t) + sizeof(uint32_t) + payload_size;
	if (payload_size > 0) {
		switch (type) {
			case PACKET_VERSION:
				mumble_proto__version__pack(message, packet_out.buffer + 6);
				break;
			case PACKET_UDPTUNNEL:
				memmove(packet_out.buffer + 6, message, length);
				break;
			case PACKET_AUTHENTICATE:
				mumble_proto__authenticate__pack(message, packet_out.buffer + 6);
				break;
			case PACKET_PING:
				mumble_proto__ping__pack(message, packet_out.buffer + 6);
				break;
			case PACKET_CHANNELREMOVE:
				mumble_proto__channel_remove__pack(message, packet_out.buffer + 6);
				break;
			case PACKET_CHANNELSTATE:
				mumble_proto__channel_state__pack(message, packet_out.buffer + 6);
				break;
			case PACKET_TEXTMESSAGE:
				mumble_proto__text_message__pack(message, packet_out.buffer + 6);
				break;
			case PACKET_USERREMOVE:
				mumble_proto__user_remove__pack(message, packet_out.buffer + 6);
				break;
			case PACKET_USERSTATE:
				mumble_proto__user_state__pack(message, packet_out.buffer + 6);
				break;
			case PACKET_VOICETARGET:
				mumble_proto__voice_target__pack(message, packet_out.buffer + 6);
				break;
			case PACKET_CODECVERSION:
				mumble_proto__codec_version__pack(message, packet_out.buffer + 6);
				break;
			case PACKET_USERSTATS:
				mumble_proto__user_stats__pack(message, packet_out.buffer + 6);
				break;
			case PACKET_REQUESTBLOB:
				mumble_proto__request_blob__pack(message, packet_out.buffer + 6);
				break;
		}
	}
	*(uint16_t *)packet_out.buffer = htons(type);
	*(uint32_t *)(packet_out.buffer + 2) = htonl(payload_size);

	return SSL_write(client->ssl, packet_out.buffer, total_size) == total_size ? 0 : 3;
}

void packet_server_version(lua_State *l, Packet *packet)
{
	MumbleProto__Version *version =  mumble_proto__version__unpack(NULL, packet->length, packet->buffer);
	if (version == NULL) {
		return;
	}

	lua_newtable(l);
		if (version->has_version) {
			lua_pushinteger(l, version->version);
			lua_setfield(l, -2, "version");
		}
		lua_pushstring(l, version->release);
		lua_setfield(l, -2, "release");
		lua_pushstring(l, version->os);
		lua_setfield(l, -2, "os");
		lua_pushstring(l, version->os_version);
		lua_setfield(l, -2, "os_version");
	mumble_hook_call(l, "OnServerVersion", 1);

	lua_settop(l, 0);

	mumble_proto__version__free_unpacked(version, NULL);
}

void packet_server_ping(lua_State *l, Packet *packet)
{
	MumbleProto__Ping *ping = mumble_proto__ping__unpack(NULL, packet->length, packet->buffer);
	if (ping == NULL) {
		return;
	}

	lua_newtable(l);
		if (ping->has_timestamp) {
			lua_pushinteger(l, ping->timestamp);
			lua_setfield(l, -2, "timestamp");
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
	mumble_hook_call(l, "OnServerPing", 1);

	lua_settop(l, 0);

	mumble_proto__ping__free_unpacked(ping, NULL);
}

void packet_server_reject(lua_State *l, Packet *packet)
{
	MumbleProto__Reject *reject = mumble_proto__reject__unpack(NULL, packet->length, packet->buffer);
	if (reject == NULL) {
		return;
	}

	lua_newtable(l);
		if (reject->has_type) {
			lua_pushinteger(l, reject->type);
			lua_setfield(l, -2, "type");
		}
		lua_pushstring(l, reject->reason);
		lua_setfield(l, -2, "reason");
	mumble_hook_call(l, "OnServerReject", 1);

	lua_settop(l, 0);

	mumble_proto__reject__free_unpacked(reject, NULL);
}

void packet_server_sync(lua_State *l, Packet *packet)
{
	MumbleClient *client = luaL_checkudata(l, 1, "mumble.client");

	MumbleProto__ServerSync *sync = mumble_proto__server_sync__unpack(NULL, packet->length, packet->buffer);
	if (sync == NULL) {
		return;
	}

	lua_newtable(l);
		if (sync->has_session) {
			client->session = sync->session;
			mumble_user_get(l, sync->session);
			lua_setfield(l, -2, "user");
		}
		if (sync->has_max_bandwidth) {
			lua_pushinteger(l, sync->max_bandwidth);
			lua_setfield(l, -2, "max_bandwidth");
		}
		if (sync->welcome_text != NULL) {
			lua_pushstring(l, sync->welcome_text);
			lua_setfield(l, -2, "welcome_text");
		}
		if (sync->has_permissions) {
			lua_pushinteger(l, sync->permissions);
			lua_setfield(l, -2, "permissions");
		}
	mumble_hook_call(l, "OnServerSync", 1);

	mumble_proto__server_sync__free_unpacked(sync, NULL);
}
void packet_channel_remove(lua_State *l, Packet *packet)
{
	MumbleProto__ChannelRemove *channel = mumble_proto__channel_remove__unpack(NULL, packet->length, packet->buffer);
	if (channel == NULL) {
		return;
	}

	lua_newtable(l);
		mumble_channel_get(l, channel->channel_id);
		lua_setfield(l, -2, "channel");
	mumble_hook_call(l, "OnChannelRemove", 1);
	mumble_channel_remove(l, channel->channel_id);

	lua_settop(l, 0);
	mumble_proto__channel_remove__free_unpacked(channel, NULL);
}

void packet_channel_state(lua_State *l, Packet *packet)
{
	MumbleProto__ChannelState *channel = mumble_proto__channel_state__unpack(NULL, packet->length, packet->buffer);
	if (channel == NULL) {
		return;
	}
	if (!channel->has_channel_id) {
		mumble_proto__channel_state__free_unpacked(channel, NULL);
		return;
	}

	lua_newtable(l);
		mumble_channel_get(l, channel->channel_id);
			if (channel->has_parent) {
				mumble_channel_get(l, channel->parent);
					if (channel->name != NULL) {
						lua_getfield(l, -1, "children");
							mumble_channel_get(l, channel->channel_id);
							lua_setfield(l, -2, channel->name);
						lua_pop(l, 1);
					}
				lua_setfield(l, -2, "parent");
			}
			if (channel->name != NULL) {
				lua_pushstring(l, channel->name);
				lua_setfield(l, -2, "name");
			}
			if (channel->description != NULL) {
				lua_pushstring(l, channel->description);
				lua_setfield(l, -2, "description");
			}
			if (channel->has_temporary) {
				lua_pushboolean(l, channel->temporary);
				lua_setfield(l, -2, "temporary");
			}
			if (channel->has_position) {
				lua_pushinteger(l, channel->position);
				lua_setfield(l, -2, "position");
			}
			if (channel->has_description_hash) {
				lua_pushlstring(l, (char *)channel->description_hash.data, channel->description_hash.len);
				lua_setfield(l, -2, "description_hash");
			}
			if (channel->has_max_users) {
				lua_pushinteger(l, channel->max_users);
				lua_setfield(l, -2, "max_users");
			}
		lua_setfield(l, -2, "channel");
	mumble_hook_call(l, "OnChannelState", 1);

	lua_settop(l, 0);
	mumble_proto__channel_state__free_unpacked(channel, NULL);
}

void packet_user_remove(lua_State *l, Packet *packet)
{
	MumbleProto__UserRemove *user = mumble_proto__user_remove__unpack(NULL, packet->length, packet->buffer);
	if (user == NULL) {
		return;
	}

	lua_newtable(l);
		mumble_user_get(l, user->session);
		lua_setfield(l, -2, "user");
		if (user->has_actor) {
			mumble_user_get(l, user->actor);
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
	mumble_hook_call(l, "OnUserRemove", 1);
	mumble_user_remove(l, user->session);

	lua_settop(l, 0);
	mumble_proto__user_remove__free_unpacked(user, NULL);
}

void packet_user_state(lua_State *l, Packet *packet)
{
	MumbleProto__UserState *user = mumble_proto__user_state__unpack(NULL, packet->length, packet->buffer);
	if (user == NULL) {
		return;
	}
	if (!user->has_session) {
		mumble_proto__user_state__free_unpacked(user, NULL);
		return;
	}

	lua_newtable(l);
		if (user->has_actor) {
			mumble_user_get(l, user->actor);
			lua_setfield(l, -2, "actor");
		}
		mumble_user_get(l, user->session);
			lua_pushinteger(l, user->session);
			lua_setfield(l, -2, "session");
			if (user->name != NULL) {
				lua_pushstring(l, user->name);
				lua_setfield(l, -2, "name");
			}
			if (user->has_channel_id) {
				lua_getfield(l, -1, "channel");
				lua_setfield(l, -2, "channel_from");

				mumble_channel_get(l, user->channel_id);
				lua_setfield(l, -2, "channel");
			} else {
				lua_pushnil(l);
				lua_setfield(l, -2, "channel_from");
			}
			if (user->has_user_id) {
				lua_pushinteger(l, user->user_id);
				lua_setfield(l, -2, "id");
			}
			if (user->has_mute) {
				lua_pushboolean(l, user->mute);
				lua_setfield(l, -2, "mute");
			}
			if (user->has_deaf) {
				lua_pushboolean(l, user->deaf);
				lua_setfield(l, -2, "deaf");
			}
			if (user->has_self_mute) {
				lua_pushboolean(l, user->self_mute);
				lua_setfield(l, -2, "self_mute");
			}
			if (user->has_self_deaf) {
				lua_pushboolean(l, user->self_deaf);
				lua_setfield(l, -2, "self_deaf");
			}
			if (user->has_suppress) {
				lua_pushboolean(l, user->suppress);
				lua_setfield(l, -2, "suppress");
			}
			if (user->comment != NULL) {
				lua_pushstring(l, user->comment);
				lua_setfield(l, -2, "comment");
			}
			if (user->has_recording) {
				lua_pushboolean(l, user->recording);
				lua_setfield(l, -2, "recording");
			}
			if (user->has_priority_speaker) {
				lua_pushboolean(l, user->priority_speaker);
				lua_setfield(l, -2, "priority_speaker");
			}
			if (user->has_texture) {
				lua_pushlstring(l, (char *)user->texture.data, user->texture.len);
				lua_setfield(l, -2, "texture");
			}
			if (user->hash != NULL) {
				lua_pushstring(l, user->hash);
				lua_setfield(l, -2, "hash");
			}
			if (user->has_comment_hash) {
				lua_pushlstring(l, (char *)user->comment_hash.data, user->comment_hash.len);
				lua_setfield(l, -2, "comment_hash");
			}
			if (user->has_texture_hash) {
				lua_pushlstring(l, (char *)user->texture_hash.data, user->texture_hash.len);
				lua_setfield(l, -2, "texture_hash");
			}
		lua_setfield(l, -2, "user");
	mumble_hook_call(l, "OnUserState", 1);

	lua_settop(l, 0);
	mumble_proto__user_state__free_unpacked(user, NULL);
}

void packet_text_message(lua_State *l, Packet *packet)
{
	MumbleProto__TextMessage *msg = mumble_proto__text_message__unpack(NULL, packet->length, packet->buffer);
	if (msg == NULL) {
		return;
	}

	lua_newtable(l);
		if (msg->has_actor) {
			mumble_user_get(l, msg->actor);
			lua_setfield(l, -2, "actor");
		}
		if (msg->message != NULL) {
			lua_pushstring(l, msg->message);
			lua_setfield(l, -2, "message");
		}
		if (msg->n_session > 0) {
			int i;
			lua_newtable(l);
			for (i = 0; i < msg->n_session; i++) {
				lua_pushinteger(l, i);
				mumble_user_get(l, msg->session[i]);
				lua_settable(l, -3);
			}
			lua_setfield(l, -2, "users");
		}
		if (msg->n_channel_id > 0) {
			int i;
			lua_newtable(l);
			for (i = 0; i < msg->n_channel_id; i++) {
				lua_pushinteger(l, i);
				mumble_channel_get(l, msg->channel_id[i]);
				lua_settable(l, -3);
			}
			lua_setfield(l, -2, "channels");
		}
	mumble_hook_call(l, "OnMessage", 1);

	lua_settop(l, 0);
	mumble_proto__text_message__free_unpacked(msg, NULL);
}

void packet_permissiondenied(lua_State *l, Packet *packet)
{
	MumbleProto__PermissionDenied *proto = mumble_proto__permission_denied__unpack(NULL, packet->length, packet->buffer);
	if (proto == NULL) {
		return;
	}

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
			mumble_channel_get(l, proto->channel_id);
			lua_setfield(l, -2, "channel");
		}
		if (proto->has_session) {
			mumble_user_get(l, proto->session);
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
	mumble_hook_call(l, "OnPermissionDenied", 1);

	lua_settop(l, 0);
	mumble_proto__permission_denied__free_unpacked(proto, NULL);
}

void packet_codec_version(lua_State *l, Packet *packet)
{
	MumbleProto__CodecVersion *codec = mumble_proto__codec_version__unpack(NULL, packet->length, packet->buffer);
	if (codec == NULL) {
		return;
	}

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
	mumble_hook_call(l, "OnCodecVersion", 1);

	lua_settop(l, 0);
	mumble_proto__codec_version__free_unpacked(codec, NULL);
}

void packet_user_stats(lua_State *l, Packet *packet)
{
	MumbleProto__UserStats *stats = mumble_proto__user_stats__unpack(NULL, packet->length, packet->buffer);
	if (stats == NULL) {
		return;
	}

	lua_newtable(l);
		if (stats->has_session) {
			mumble_user_get(l, stats->session);
			lua_setfield(l, -2, "user");
		}
		if (stats->has_stats_only) {
			lua_pushboolean(l, stats->stats_only);
			lua_setfield(l, -2, "stats_only");
		}
		lua_newtable(l);
		int i;
		for (i=0; i < stats->n_certificates; i++) {
			lua_pushinteger(l, i);
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
				if (stats->version->has_version) {
					lua_pushinteger(l, stats->version->version);
					lua_setfield(l, -2, "version");
				}
				lua_pushstring(l, stats->version->release);
				lua_setfield(l, -2, "release");
				lua_pushstring(l, stats->version->os);
				lua_setfield(l, -2, "os");
				lua_pushstring(l, stats->version->os_version);
				lua_setfield(l, -2, "os_version");
			lua_setfield(l, -2, "version");
		}

		lua_newtable(l);
		int j;
		for (j=0; j < stats->n_celt_versions; j++) {
			lua_pushinteger(l, j);
			lua_pushinteger(l, stats->celt_versions[j]);
			lua_settable(l, -3);
		}
		lua_setfield(l, -2, "celt_versions");

		if (stats->has_address) {
			//lua_pushlstring(l, (char *)stats->address.data, stats->address.len);

			lua_newtable(l);
				int k;
				for (k=0; k < stats->address.len; k++) {
					lua_pushinteger(l, k);
					lua_pushinteger(l, stats->address.data[k]);
					lua_settable(l, -3);
				}
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
	mumble_hook_call(l, "OnUserStats", 1);

	lua_settop(l, 0);

	mumble_proto__user_stats__free_unpacked(stats, NULL);
}

void packet_server_config(lua_State *l, Packet *packet)
{
	MumbleProto__ServerConfig *config = mumble_proto__server_config__unpack(NULL, packet->length, packet->buffer);
	if (config == NULL) {
		return;
	}

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
	mumble_hook_call(l, "OnServerConfig", 1);

	lua_settop(l, 0);
	mumble_proto__server_config__free_unpacked(config, NULL);
}

const Packet_Handler_Func packet_handler[26] = {
	/*  0 */ packet_server_version, // Version
	/*  1 */ NULL, // UDPTunnel
	/*  2 */ NULL, // Authenticate
	/*  3 */ packet_server_ping, // Ping
	/*  4 */ packet_server_reject, // Reject
	/*  5 */ packet_server_sync,
	/*  6 */ packet_channel_remove,
	/*  7 */ packet_channel_state,
	/*  8 */ packet_user_remove,
	/*  9 */ packet_user_state,
	/* 10 */ NULL, // Banlist
	/* 11 */ packet_text_message,
	/* 12 */ packet_permissiondenied,
	/* 13 */ NULL, // ACL
	/* 14 */ NULL, // QueryUsers
	/* 15 */ NULL, // CryptSetup
	/* 16 */ NULL, // ContextActionAdd
	/* 17 */ NULL, // Context Action
	/* 18 */ NULL, // UserList
	/* 19 */ NULL, // VoiceTarget
	/* 20 */ NULL, // PermissionQuery
	/* 21 */ packet_codec_version, // CodecVersion
	/* 22 */ packet_user_stats,
	/* 23 */ NULL, // RequestBlob
	/* 24 */ packet_server_config,
	/* 25 */ NULL,
};