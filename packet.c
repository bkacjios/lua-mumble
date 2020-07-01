#include "mumble.h"

#include "packet.h"

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
		case PACKET_ACL:
			payload_size = mumble_proto__acl__get_packed_size(message);
			break;
		case PACKET_PERMISSIONQUERY:
			payload_size = mumble_proto__permission_query__get_packed_size(message);
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
		case PACKET_PLUGINDATA:
			payload_size = mumble_proto__plugin_data_transmission__get_packed_size(message);
			break;
		default:
			printf("WARNING: unable to get payload size for packet #%i\n", type);
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
			case PACKET_ACL:
				mumble_proto__acl__pack(message, packet_out.buffer + 6);
				break;
			case PACKET_PERMISSIONQUERY:
				mumble_proto__permission_query__pack(message, packet_out.buffer + 6);
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
			case PACKET_PLUGINDATA:
				mumble_proto__plugin_data_transmission__pack(message, packet_out.buffer + 6);
				break;
			default:
				printf("WARNING: attempted to pack unspported packet #%i\n", type);
				break;
		}
	}
	*(uint16_t *)packet_out.buffer = htons(type);
	*(uint32_t *)(packet_out.buffer + 2) = htonl(payload_size);

	return SSL_write(client->ssl, packet_out.buffer, total_size) == total_size ? 0 : 3;
}

void packet_server_version(lua_State *l, MumbleClient *client, Packet *packet)
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
	mumble_hook_call(l, client, "OnServerVersion", 1);

	mumble_proto__version__free_unpacked(version, NULL);
}

void packet_udp_tunnel(lua_State *l, MumbleClient *client, Packet *packet)
{
	unsigned char header	= packet->buffer[0];
	unsigned char codec		= header >> 5;
	unsigned char target	= header >> 31;

	int read = 1;
	int session = util_get_varint(packet->buffer + read, &read);
	int sequence = util_get_varint(packet->buffer + read, &read);
	
	bool speaking = false;
	MumbleUser* user = mumble_user_get(l, client, session);

	int payload_len = 0;
	uint16_t frame_header = 0;

	if (codec == UDP_SPEEX || codec == UDP_CELT_ALPHA || codec == UDP_CELT_BETA) {
		frame_header = packet->buffer[read++];
		payload_len = frame_header & 0x7F;
		speaking = ((frame_header & 0x80) == 0);
	} else if (codec == UDP_OPUS) {
		frame_header = util_get_varint(packet->buffer + read, &read);
		payload_len = frame_header & 0x1FFF;
		speaking = ((frame_header & 0x2000) == 0);
	}

	bool state_change = false;

	if (user->speaking != speaking) {
		user->speaking = speaking;
		state_change = true;
	}

	if (state_change && speaking) {
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
		lua_pushlstring(l, packet->buffer + read, payload_len);
		lua_setfield(l, -2, "data");
		lua_pushinteger(l, header);
		lua_setfield(l, -2, "header");
		lua_pushinteger(l, frame_header);
		lua_setfield(l, -2, "frame_header");
	mumble_hook_call(l, client, "OnUserSpeak", 1);

	if (state_change && !speaking) {
		mumble_user_raw_get(l, client, session);
		mumble_hook_call(l, client, "OnUserStopSpeaking", 1);
	}
}

void packet_server_ping(lua_State *l, MumbleClient *client, Packet *packet)
{
	MumbleProto__Ping *ping = mumble_proto__ping__unpack(NULL, packet->length, packet->buffer);
	if (ping == NULL) {
		return;
	}

	double ms = 0;

	lua_newtable(l);
		if (ping->has_timestamp) {
			ms = (gettime() * 1000) - ping->timestamp;
			client->tcp_ping_total += ms;
			client->tcp_packets += 1;
			client->tcp_ping_avg = client->tcp_ping_total / client->tcp_packets;
			client->tcp_ping_var = powf(fabs(ms - client->tcp_ping_avg), 2);

			lua_pushnumber(l, ms);
			lua_setfield(l, -2, "ping");

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
	mumble_hook_call(l, client, "OnServerPing", 1);

	mumble_proto__ping__free_unpacked(ping, NULL);
}

void packet_server_reject(lua_State *l, MumbleClient *client, Packet *packet)
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
	mumble_hook_call(l, client, "OnServerReject", 1);

	mumble_proto__reject__free_unpacked(reject, NULL);
}

void packet_server_sync(lua_State *l, MumbleClient *client, Packet *packet)
{
	MumbleProto__ServerSync *sync = mumble_proto__server_sync__unpack(NULL, packet->length, packet->buffer);
	if (sync == NULL) {
		return;
	}

	client->synced = true;

	lua_newtable(l);
		if (sync->has_session) {
			client->session = sync->session;
			mumble_user_raw_get(l, client, sync->session);
			lua_setfield(l, -2, "user");
		}
		if (sync->has_max_bandwidth) {
			lua_pushinteger(l, sync->max_bandwidth);
			lua_setfield(l, -2, "max_bandwidth");

			mumble_create_audio_timer(client, sync->max_bandwidth);
		}
		if (sync->welcome_text != NULL) {
			lua_pushstring(l, sync->welcome_text);
			lua_setfield(l, -2, "welcome_text");
		}
		if (sync->has_permissions) {
			lua_pushinteger(l, sync->permissions);
			lua_setfield(l, -2, "permissions");

			MumbleChannel* root = mumble_channel_get(l, client, 0); lua_pop(l, 1);
			root->permissions = sync->permissions;
		}
	mumble_hook_call(l, client, "OnServerSync", 1);

	mumble_proto__server_sync__free_unpacked(sync, NULL);
}

void packet_channel_remove(lua_State *l, MumbleClient *client, Packet *packet)
{
	MumbleProto__ChannelRemove *channel = mumble_proto__channel_remove__unpack(NULL, packet->length, packet->buffer);
	if (channel == NULL) {
		return;
	}

	mumble_channel_raw_get(l, client, channel->channel_id);
	mumble_hook_call(l, client, "OnChannelRemove", 1);
	mumble_channel_remove(l, client, channel->channel_id);

	mumble_proto__channel_remove__free_unpacked(channel, NULL);
}

void packet_channel_state(lua_State *l, MumbleClient *client, Packet *packet)
{
	MumbleProto__ChannelState *state = mumble_proto__channel_state__unpack(NULL, packet->length, packet->buffer);
	if (state == NULL) {
		return;
	}
	if (!state->has_channel_id) {
		mumble_proto__channel_state__free_unpacked(state, NULL);
		return;
	}

	MumbleChannel* channel = mumble_channel_get(l, client, state->channel_id);
	if (state->has_parent) {
		channel->parent = state->parent;
	}
	if (state->name != NULL) {
		channel->name = strdup(state->name);
	}
	if (state->description != NULL) {
		channel->description = strdup(state->description);
	}
	if (state->has_temporary) {
		channel->temporary = state->temporary;
	}
	if (state->has_position) {
		channel->position = state->position;
	}
	if (state->has_description_hash) {
		channel->description_hash = (char*) strdup((const char*)state->description_hash.data);
		channel->description_hash_len = state->description_hash.len;
	}
	if (state->has_max_users) {
		channel->max_users = state->max_users;
	}
	if (state->n_links_add > 0) {
		// Add the new entries to the head of the list
		for (uint32_t i = 0; i < state->n_links_add; i++) {
			list_add(&channel->links, state->links_add[i]);
		}
	}
	if (state->n_links_remove > 0) {
		for (uint32_t i = 0; i < state->n_links_remove; i++) {
			list_remove(&channel->links, state->links_remove[i]);
		}
	}
	if (state->n_links > 0) {
		list_clear(&channel->links);

		// Store links in new list
		for (uint32_t i = 0; i < state->n_links; i++) {
			list_add(&channel->links, state->links[i]);
		}
	}
	if (state->has_is_enter_restricted) {
		channel->is_enter_restricted = state->is_enter_restricted;
	}
	if (state->has_can_enter) {
		channel->can_enter = state->can_enter;
	}

	mumble_hook_call(l, client, "OnChannelState", 1);

	mumble_proto__channel_state__free_unpacked(state, NULL);
}

void packet_user_remove(lua_State *l, MumbleClient *client, Packet *packet)
{
	MumbleProto__UserRemove *user = mumble_proto__user_remove__unpack(NULL, packet->length, packet->buffer);
	if (user == NULL) {
		return;
	}

	lua_newtable(l);
		mumble_user_raw_get(l, client, user->session);
		lua_setfield(l, -2, "user");
		if (user->has_actor) {
			mumble_user_raw_get(l, client, user->actor);
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
	mumble_hook_call(l, client, "OnUserRemove", 1);
	mumble_user_remove(l, client, user->session);

	mumble_proto__user_remove__free_unpacked(user, NULL);
}

void packet_user_state(lua_State *l, MumbleClient *client, Packet *packet)
{
	MumbleProto__UserState *state = mumble_proto__user_state__unpack(NULL, packet->length, packet->buffer);
	if (state == NULL) {
		return;
	}
	if (!state->has_session) {
		mumble_proto__user_state__free_unpacked(state, NULL);
		return;
	}

	MumbleUser* user = mumble_user_get(l, client, state->session);

	lua_newtable(l);
		if (state->has_actor) {
			mumble_user_raw_get(l, client, state->actor);
			lua_setfield(l, -2, "actor");
		}
		mumble_user_raw_get(l, client, state->session);
			user->session = state->session;
			if (state->name != NULL) {
				user->name = strdup(state->name);
			}
			if (state->has_channel_id) {
				if (user->channel_id != state->channel_id) {
					lua_newtable(l);
						if (state->has_actor) {
							mumble_user_raw_get(l, client, state->actor);
							lua_setfield(l, -2, "actor");
						}
						mumble_channel_raw_get(l, client, user->channel_id);
						lua_setfield(l, -2, "from");
						mumble_channel_raw_get(l, client, state->channel_id);
						lua_setfield(l, -2, "to");
						mumble_user_raw_get(l, client, state->session);
						lua_setfield(l, -2, "user");
					mumble_hook_call(l, client, "OnUserChannel", 1);
				}
				user->channel_id = state->channel_id;
			}
			if (state->has_user_id) {
				user->user_id = state->user_id;
			}
			if (state->has_mute) {
				user->mute = state->mute;
			}
			if (state->has_deaf) {
				user->deaf = state->deaf;
			}
			if (state->has_self_mute) {
				user->self_mute = state->self_mute;
			}
			if (state->has_self_deaf) {
				user->self_deaf = state->self_deaf;
			}
			if (state->has_suppress) {
				user->suppress = state->suppress;
			}
			if (state->comment != NULL) {
				user->comment = strdup(state->comment);
			}
			if (state->has_recording) {
				user->recording = state->recording;
			}
			if (state->has_priority_speaker) {
				user->priority_speaker = state->priority_speaker;
			}
			if (state->has_texture) {
				user->texture = (char*) strdup((const char*)state->texture.data);
			}
			if (state->hash != NULL) {
				user->hash = (char*) strdup((const char*)state->hash);
			}
			if (state->has_comment_hash) {
				user->comment_hash = (char*) strdup((const char*)state->comment_hash.data);
				user->comment_hash_len = state->comment_hash.len;
			}
			if (state->has_texture_hash) {
				user->texture_hash = (char*) strdup((const char*)state->texture_hash.data);
				user->texture_hash_len = state->texture_hash.len;
			}
		lua_setfield(l, -2, "user");

		if (user->connected == false && client->synced == true) {
			user->connected = true;
			lua_pushvalue(l, -1);
			mumble_hook_call(l, client, "OnUserConnected", 1);
		}
	mumble_hook_call(l, client, "OnUserState", 1);

	mumble_proto__user_state__free_unpacked(state, NULL);
}

void packet_text_message(lua_State *l, MumbleClient *client, Packet *packet)
{
	MumbleProto__TextMessage *msg = mumble_proto__text_message__unpack(NULL, packet->length, packet->buffer);
	if (msg == NULL) {
		return;
	}

	lua_newtable(l);
		if (msg->has_actor) {
			mumble_user_raw_get(l, client, msg->actor);
			lua_setfield(l, -2, "actor");
		}
		if (msg->message != NULL) {
			lua_pushstring(l, msg->message);
			lua_setfield(l, -2, "message");
		}
		if (msg->n_session > 0) {
			lua_newtable(l);
			for (uint32_t i = 0; i < msg->n_session; i++) {
				lua_pushinteger(l, i+1);
				mumble_user_raw_get(l, client, msg->session[i]);
				lua_settable(l, -3);
			}
			lua_setfield(l, -2, "users");
		}
		if (msg->n_channel_id > 0) {
			lua_newtable(l);
			for (uint32_t i = 0; i < msg->n_channel_id; i++) {
				lua_pushinteger(l, i+1);
				mumble_channel_raw_get(l, client, msg->channel_id[i]);
				lua_settable(l, -3);
			}
			lua_setfield(l, -2, "channels");
		}
	mumble_hook_call(l, client, "OnMessage", 1);

	mumble_proto__text_message__free_unpacked(msg, NULL);
}

void packet_permission_denied(lua_State *l, MumbleClient *client, Packet *packet)
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
			mumble_channel_raw_get(l, client, proto->channel_id);
			lua_setfield(l, -2, "channel");
		}
		if (proto->has_session) {
			mumble_user_raw_get(l, client, proto->session);
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
	mumble_hook_call(l, client, "OnPermissionDenied", 1);

	mumble_proto__permission_denied__free_unpacked(proto, NULL);
}

void packet_acl(lua_State *l, MumbleClient *client, Packet *packet)
{
	MumbleProto__ACL *acl = mumble_proto__acl__unpack(NULL, packet->length, packet->buffer);
	if (acl == NULL) {
		return;
	}

	lua_newtable(l);
		mumble_channel_raw_get(l, client, acl->channel_id);
		lua_setfield(l, -2, "channel");
		if (acl->n_groups > 0) {
			lua_newtable(l);
			for (uint32_t i = 0; i < acl->n_groups; i++) {
				MumbleProto__ACL__ChanGroup *changroup = acl->groups[i];
				lua_pushinteger(l, i+1);
				lua_newtable(l);
				if(changroup->name != NULL) {
					lua_pushstring(l, changroup->name);
					lua_setfield(l, -2, "name");
				}
				if(changroup->has_inherited) {
					lua_pushboolean(l, changroup->inherited);
					lua_setfield(l, -2, "inherited");
				}
				if(changroup->has_inheritable) {
					lua_pushboolean(l, changroup->inheritable);
					lua_setfield(l, -2, "inheritable");
				}
				if (changroup->n_add > 0) {
					lua_newtable(l);
					for (uint32_t j = 0; j < changroup->n_add; j++) {
						lua_pushinteger(l, j+1);
						lua_pushinteger(l, changroup->add[j]);
						lua_settable(l, -3);
					}
					lua_setfield(l, -2, "add");
				}
				if (changroup->n_remove > 0) {
					lua_newtable(l);
					for (uint32_t j = 0; j < changroup->n_remove; j++) {
						lua_pushinteger(l, j+1);
						lua_pushinteger(l, changroup->remove[j]);
						lua_settable(l, -3);
					}
					lua_setfield(l, -2, "remove");
				}
				if (changroup->n_inherited_members > 0) {
					lua_newtable(l);
					for (uint32_t j = 0; j < changroup->n_inherited_members; j++) {
						lua_pushinteger(l, j+1);
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
				lua_pushinteger(l, i+1);
				lua_newtable(l);
				if(chanacl->has_apply_here) {
					lua_pushboolean(l, chanacl->apply_here);
					lua_setfield(l, -2, "apply_here");
				}
				if(chanacl->has_apply_subs) {
					lua_pushboolean(l, chanacl->apply_subs);
					lua_setfield(l, -2, "apply_subs");
				}
				if(chanacl->has_inherited) {
					lua_pushboolean(l, chanacl->inherited);
					lua_setfield(l, -2, "inherited");
				}
				if(chanacl->has_user_id) {
					lua_pushboolean(l, chanacl->user_id);
					lua_setfield(l, -2, "user_id");
				}
				if(chanacl->group != NULL) {
					lua_pushstring(l, chanacl->group);
					lua_setfield(l, -2, "group");
				}
				if(chanacl->has_grant) {
					lua_pushinteger(l, chanacl->grant);
					lua_setfield(l, -2, "grant");
				}
				if(chanacl->has_deny) {
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
	mumble_hook_call(l, client, "OnACL", 1);

	mumble_proto__acl__free_unpacked(acl, NULL);
}

void packet_permission_query(lua_State *l, MumbleClient *client, Packet *packet)
{
	MumbleProto__PermissionQuery *query = mumble_proto__permission_query__unpack(NULL, packet->length, packet->buffer);
	if (query == NULL) {
		return;
	}

	if (query->has_channel_id && query->has_permissions) {
		MumbleChannel* chan = mumble_channel_get(l, client, query->channel_id); lua_pop(l, 1);
		chan->permissions = query->permissions;
	} else if (query->has_flush && query->flush) {
		// Loop through all channels and set permissions to 0
		lua_rawgeti(l, LUA_REGISTRYINDEX, client->channels);
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
			mumble_channel_raw_get(l, client, query->channel_id);
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
	mumble_hook_call(l, client, "OnPermissionQuery", 1);

	mumble_proto__permission_query__free_unpacked(query, NULL);
}

void packet_codec_version(lua_State *l, MumbleClient *client, Packet *packet)
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
	mumble_hook_call(l, client, "OnCodecVersion", 1);

	mumble_proto__codec_version__free_unpacked(codec, NULL);
}

void packet_user_stats(lua_State *l, MumbleClient *client, Packet *packet)
{
	MumbleProto__UserStats *stats = mumble_proto__user_stats__unpack(NULL, packet->length, packet->buffer);
	if (stats == NULL) {
		return;
	}

	lua_newtable(l);
		if (stats->has_session) {
			mumble_user_raw_get(l, client, stats->session);
			lua_setfield(l, -2, "user");
		}
		if (stats->has_stats_only) {
			lua_pushboolean(l, stats->stats_only);
			lua_setfield(l, -2, "stats_only");
		}
		lua_newtable(l);
		for (uint32_t i = 0; i < stats->n_certificates; i++) {
			lua_pushinteger(l, i+1);
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
		for (uint32_t j = 0; j < stats->n_celt_versions; j++) {
			lua_pushinteger(l, j+1);
			lua_pushinteger(l, stats->celt_versions[j]);
			lua_settable(l, -3);
		}
		lua_setfield(l, -2, "celt_versions");

		if (stats->has_address) {
			lua_newtable(l);
				uint8_t* bytes = (uint8_t*) stats->address.data;
				uint64_t* addr = (uint64_t*) stats->address.data;
				uint16_t* shorts = (uint16_t*) stats->address.data;

				if (addr[0] != 0ULL || shorts[4] != 0 || shorts[5] != 0xFFFF) {
					char ipv6[40];
					sprintf(ipv6,"%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x",
						bytes[0], bytes[1], bytes[2], bytes[3],
						bytes[4], bytes[5], bytes[5], bytes[7],
						bytes[8], bytes[9], bytes[10], bytes[11],
						bytes[12], bytes[13], bytes[14], bytes[15]);

					lua_pushboolean(l, true);
					lua_setfield(l, -2, "ipv6");
					lua_pushstring(l, ipv6);
					lua_setfield(l, -2, "string");
				} else {
					char ipv4[16];
					sprintf(ipv4, "%d.%d.%d.%d", bytes[12], bytes[13], bytes[14], bytes[15]);
					
					lua_pushboolean(l, true);
					lua_setfield(l, -2, "ipv4");
					lua_pushstring(l, ipv4);
					lua_setfield(l, -2, "string");
				}

				lua_newtable(l);
					for (uint32_t k = 0; k < stats->address.len; k++) {
						lua_pushinteger(l, k+1);
						lua_pushinteger(l, stats->address.data[k]);
						lua_settable(l, -3);
					}
				lua_setfield(l, -2, "data");
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
	mumble_hook_call(l, client, "OnUserStats", 1);

	mumble_proto__user_stats__free_unpacked(stats, NULL);
}

void packet_server_config(lua_State *l, MumbleClient *client, Packet *packet)
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
	mumble_hook_call(l, client, "OnServerConfig", 1);

	mumble_proto__server_config__free_unpacked(config, NULL);
}

void packet_suggest_config(lua_State *l, MumbleClient *client, Packet *packet)
{
	MumbleProto__SuggestConfig *config = mumble_proto__suggest_config__unpack(NULL, packet->length, packet->buffer);
	if (config == NULL) {
		return;
	}

	lua_newtable(l);
		if (config->has_version) {
			lua_pushinteger(l, config->version);
			lua_setfield(l, -2, "version");
		}
		if (config->has_positional) {
			lua_pushboolean(l, config->positional);
			lua_setfield(l, -2, "positional");
		}
		if (config->has_push_to_talk) {
			lua_pushboolean(l, config->push_to_talk);
			lua_setfield(l, -2, "push_to_talk");
		}
	mumble_hook_call(l, client, "OnSuggestConfig", 1);

	mumble_proto__suggest_config__free_unpacked(config, NULL);
}

void packet_plugin_data(lua_State *l, MumbleClient *client, Packet *packet)
{
	MumbleProto__PluginDataTransmission *transmission = mumble_proto__plugin_data_transmission__unpack(NULL, packet->length, packet->buffer);
	if (transmission == NULL) {
		return;
	}

	lua_newtable(l);
		if (transmission->has_sendersession) {
			mumble_user_raw_get(l, client, transmission->sendersession);
			lua_setfield(l, -2, "sender");
		}

		if (transmission->n_receiversessions > 0) {
			lua_newtable(l);
			for (uint32_t i = 0; i < transmission->n_receiversessions; i++) {
				lua_pushinteger(l, i+1);
				mumble_user_raw_get(l, client, transmission->receiversessions[i]);
				lua_settable(l, -3);
			}
			lua_setfield(l, -2, "receivers");
		}

		if (transmission->has_data) {
			lua_pushlstring(l, transmission->data.data, transmission->data.len);
			lua_setfield(l, -2, "data");
		}
		if (transmission->dataid != NULL) {
			lua_pushstring(l, transmission->dataid);
			lua_setfield(l, -2, "id");
		}
	mumble_hook_call(l, client, "OnPluginData", 1);

	mumble_proto__plugin_data_transmission__free_unpacked(transmission, NULL);
}

const Packet_Handler_Func packet_handler[NUM_PACKETS] = {
	/*  0 */ packet_server_version, // Version
	/*  1 */ packet_udp_tunnel, // UDPTunnel
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
	/* 12 */ packet_permission_denied,
	/* 13 */ packet_acl, // ACL
	/* 14 */ NULL, // QueryUsers
	/* 15 */ NULL, // CryptSetup
	/* 16 */ NULL, // ContextActionAdd
	/* 17 */ NULL, // Context Action
	/* 18 */ NULL, // UserList
	/* 19 */ NULL, // VoiceTarget
	/* 20 */ packet_permission_query, // PermissionQuery
	/* 21 */ packet_codec_version, // CodecVersion
	/* 22 */ packet_user_stats,
	/* 23 */ NULL, // RequestBlob
	/* 24 */ packet_server_config,
	/* 25 */ packet_suggest_config,
	/* 26 */ packet_plugin_data,
};