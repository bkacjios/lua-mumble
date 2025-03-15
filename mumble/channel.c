#include "mumble.h"

#include "channel.h"
#include "packet.h"
#include "util.h"
#include "log.h"

/*--------------------------------
	MUMBLE CHANNEL META METHODS
--------------------------------*/

static int channel_message(lua_State *l) {
	MumbleChannel *channel = luaL_checkudata(l, 1, METATABLE_CHAN);

	MumbleProto__TextMessage msg = MUMBLE_PROTO__TEXT_MESSAGE__INIT;

	msg.message = (char*) luaL_checkstring(l, 2);
	msg.n_channel_id = 1;
	msg.channel_id = malloc(sizeof(uint32_t) * msg.n_channel_id);

	if (msg.channel_id == NULL)
		return luaL_error(l, "failed to malloc: %s", strerror(errno));

	msg.channel_id[0] = channel->channel_id;

	packet_send(channel->client, PACKET_TEXTMESSAGE, &msg);
	free(msg.channel_id);
	return 0;
}

static int channel_setDescription(lua_State *l) {
	MumbleChannel *channel = luaL_checkudata(l, 1, METATABLE_CHAN);

	MumbleProto__ChannelState msg = MUMBLE_PROTO__CHANNEL_STATE__INIT;

	msg.has_channel_id = true;
	msg.channel_id = channel->channel_id;
	msg.description = (char *)lua_tostring(l, 2);

	packet_send(channel->client, PACKET_CHANNELSTATE, &msg);
	return 0;
}

static int channel_remove(lua_State *l) {
	MumbleChannel *channel = luaL_checkudata(l, 1, METATABLE_CHAN);

	MumbleProto__ChannelRemove msg = MUMBLE_PROTO__CHANNEL_REMOVE__INIT;

	msg.channel_id = channel->channel_id;

	packet_send(channel->client, PACKET_CHANNELREMOVE, &msg);
	return 0;
}

static int channel_getClient(lua_State *l) {
	MumbleChannel *channel = luaL_checkudata(l, 1, METATABLE_CHAN);
	mumble_client_raw_get(channel->client);
	return 1;
}

static int channel_getName(lua_State *l) {
	MumbleChannel *channel = luaL_checkudata(l, 1, METATABLE_CHAN);
	lua_pushstring(l, channel->name);
	return 1;
}

static int channel_getId(lua_State *l) {
	MumbleChannel *channel = luaL_checkudata(l, 1, METATABLE_CHAN);
	lua_pushinteger(l, channel->channel_id);
	return 1;
}

static int channel_getParent(lua_State *l) {
	MumbleChannel *channel = luaL_checkudata(l, 1, METATABLE_CHAN);

	// This should only happen on the root channel
	if (channel->channel_id == channel->parent) {
		lua_pushnil(l);
		return 1;
	}

	mumble_channel_raw_get(channel->client, channel->parent);
	return 1;
}

static int channel_getChildren(lua_State *l) {
	MumbleChannel *channel = luaL_checkudata(l, 1, METATABLE_CHAN);
	LinkNode* current = channel->client->channel_list;

	lua_newtable(l);
	int i = 1;

	while (current != NULL) {
		MumbleChannel *chan = current->data;
		if (chan->channel_id != chan->parent && chan->parent == channel->channel_id) {
			lua_pushinteger(l, i++);
			mumble_channel_raw_get(channel->client, current->index);
			lua_settable(l, -3);
		}
		current = current->next;
	}
	return 1;
}

static int channel_getUsers(lua_State *l) {
	MumbleChannel *channel = luaL_checkudata(l, 1, METATABLE_CHAN);
	LinkNode* current = channel->client->user_list;

	lua_newtable(l);
	int i = 1;

	while (current != NULL) {
		MumbleUser *user = current->data;
		if (user->channel_id == channel->channel_id) {
			lua_pushinteger(l, i++);
			mumble_user_raw_get(channel->client, current->index);
			lua_settable(l, -3);
		}
		current = current->next;
	}
	return 1;
}

static int channel_getDescription(lua_State *l) {
	MumbleChannel *channel = luaL_checkudata(l, 1, METATABLE_CHAN);
	lua_pushstring(l, channel->description);
	return 1;
}

static int channel_getDescriptionHash(lua_State *l) {
	MumbleChannel *channel = luaL_checkudata(l, 1, METATABLE_CHAN);
	char* result;
	bin_to_strhex(channel->description_hash, channel->description_hash_len, &result);
	lua_pushstring(l, result);
	free(result);
	return 1;
}

static int channel_isTemporary(lua_State *l) {
	MumbleChannel *channel = luaL_checkudata(l, 1, METATABLE_CHAN);
	lua_pushboolean(l, channel->temporary);
	return 1;
}

static int channel_getPosition(lua_State *l) {
	MumbleChannel *channel = luaL_checkudata(l, 1, METATABLE_CHAN);
	lua_pushinteger(l, channel->position);
	return 1;
}

static int channel_getMaxUsers(lua_State *l) {
	MumbleChannel *channel = luaL_checkudata(l, 1, METATABLE_CHAN);
	lua_pushinteger(l, channel->max_users);
	return 1;
}

static int channel_link(lua_State *l) {
	MumbleChannel *channel = luaL_checkudata(l, 1, METATABLE_CHAN);

	MumbleProto__ChannelState msg = MUMBLE_PROTO__CHANNEL_STATE__INIT;

	msg.has_channel_id = true;
	msg.channel_id = channel->channel_id;

	size_t n_links_add = 0;
	uint32_t *links_add = NULL;

	// Check if the 2nd argument is a table (a list of userdata)
	if (lua_istable(l, 2)) {
		// If it's a table, get the length of the table
		n_links_add = lua_objlen(l, 2);
		links_add = malloc(sizeof(uint32_t) * n_links_add);
		if (!links_add)
			return luaL_error(l, "failed to malloc: %s", strerror(errno));

		// Iterate over the table and extract the channel->channel_id
		lua_pushvalue(l, 2); // Push the table onto the stack
		lua_pushnil(l);
		int i = 0;
		while (lua_next(l, -2)) { // Iterate over the table
			if (luaL_isudata(l, -1, METATABLE_CHAN)) {
				MumbleChannel *channel = lua_touserdata(l, -1);
				links_add[i++] = channel->channel_id;
			} else {
				// If not userdata of the expected type, return an error
				free(links_add);
				return luaL_typerror_table(l, 2, -2, -1, METATABLE_CHAN);
			}
			lua_pop(l, 1);  // Pop the value, keep the key
		}
		lua_pop(l, 1);  // Pop the table
	} else {
		// Otherwise, process varargs (userdata arguments)
		int top = lua_gettop(l);
		n_links_add = top - 1;  // Subtract first parameter (user)
		links_add = malloc(sizeof(uint32_t) * n_links_add);
		if (!links_add)
			return luaL_error(l, "failed to malloc: %s", strerror(errno));

		// Extract channel->channel_id from each userdata vararg
		for (int i = 0; i < n_links_add; ++i) {
			if (luaL_isudata(l, 2 + i, METATABLE_CHAN)) {
				MumbleChannel *channel = lua_touserdata(l, -1);
				links_add[i] = channel->channel_id;
			} else {
				free(links_add);
				return luaL_typerror(l, 2 + i, METATABLE_CHAN);
			}
		}
	}

	msg.links_add = links_add;
	msg.n_links_add = n_links_add;

	packet_send(channel->client, PACKET_CHANNELSTATE, &msg);
	free(msg.links_add);
	return 0;
}

static int channel_unlink(lua_State *l) {
	MumbleChannel *channel = luaL_checkudata(l, 1, METATABLE_CHAN);

	MumbleProto__ChannelState msg = MUMBLE_PROTO__CHANNEL_STATE__INIT;

	msg.has_channel_id = true;
	msg.channel_id = channel->channel_id;

	size_t n_links_remove = 0;
	uint32_t *links_remove = NULL;

	// Check if the 2nd argument is a table (a list of userdata)
	if (lua_istable(l, 2)) {
		// If it's a table, get the length of the table
		n_links_remove = lua_objlen(l, 2);
		links_remove = malloc(sizeof(uint32_t) * n_links_remove);
		if (!links_remove)
			return luaL_error(l, "failed to malloc: %s", strerror(errno));

		// Iterate over the table and extract the channel->channel_id
		lua_pushvalue(l, 2); // Push the table onto the stack
		lua_pushnil(l);
		int i = 0;
		while (lua_next(l, -2)) { // Iterate over the table
			if (luaL_isudata(l, -1, METATABLE_CHAN)) {
				MumbleChannel *channel = lua_touserdata(l, -1);
				links_remove[i++] = channel->channel_id;
			} else {
				// If not userdata of the expected type, return an error
				free(links_remove);
				return luaL_typerror_table(l, 2, -2, -1, METATABLE_CHAN);
			}
			lua_pop(l, 1);  // Pop the value, keep the key
		}
		lua_pop(l, 1);  // Pop the table
	} else {
		// Otherwise, process varargs (userdata arguments)
		int top = lua_gettop(l);
		n_links_remove = top - 1;  // Subtract first parameter (user)
		links_remove = malloc(sizeof(uint32_t) * n_links_remove);
		if (!links_remove)
			return luaL_error(l, "failed to malloc: %s", strerror(errno));

		// Extract channel->channel_id from each userdata vararg
		for (int i = 0; i < n_links_remove; ++i) {
			if (luaL_isudata(l, 2 + i, METATABLE_CHAN)) {
				MumbleChannel *channel = lua_touserdata(l, -1);
				links_remove[i] = channel->channel_id;
			} else {
				free(links_remove);
				return luaL_typerror(l, 2 + i, METATABLE_CHAN);
			}
		}
	}

	msg.links_remove = links_remove;
	msg.n_links_remove = n_links_remove;

	packet_send(channel->client, PACKET_CHANNELSTATE, &msg);
	free(msg.links_remove);
	return 0;
}

static int channel_getLinks(lua_State *l) {
	MumbleChannel *channel = luaL_checkudata(l, 1, METATABLE_CHAN);

	lua_newtable(l);

	LinkNode * current = channel->links;

	// Add all linked channels to the table
	while (current != NULL) {
		lua_pushinteger(l, current->index);
		mumble_channel_raw_get(channel->client, current->index);
		lua_settable(l, -3);

		current = current->next;
	}

	return 1;
}

static int channel_isEnterRestricted(lua_State *l) {
	MumbleChannel *channel = luaL_checkudata(l, 1, METATABLE_CHAN);
	lua_pushboolean(l, channel->is_enter_restricted);
	return 1;
}

static int channel_canEnter(lua_State *l) {
	MumbleChannel *channel = luaL_checkudata(l, 1, METATABLE_CHAN);
	lua_pushboolean(l, channel->can_enter);
	return 1;
}

static int channel_requestACL(lua_State *l) {
	MumbleChannel *channel = luaL_checkudata(l, 1, METATABLE_CHAN);

	MumbleProto__ACL msg = MUMBLE_PROTO__ACL__INIT;
	msg.channel_id = channel->channel_id;
	msg.has_query = true;
	msg.query = true;

	packet_send(channel->client, PACKET_ACL, &msg);
	return 0;
}

static int channel_requestPermissions(lua_State *l) {
	MumbleChannel *channel = luaL_checkudata(l, 1, METATABLE_CHAN);

	MumbleProto__PermissionQuery msg = MUMBLE_PROTO__PERMISSION_QUERY__INIT;
	msg.has_channel_id = true;
	msg.channel_id = channel->channel_id;

	packet_send(channel->client, PACKET_PERMISSIONQUERY, &msg);
	return 0;
}

static int channel_getPermissions(lua_State *l) {
	MumbleChannel *channel = luaL_checkudata(l, 1, METATABLE_CHAN);
	lua_pushinteger(l, channel->permissions);
	return 1;
}

static int channel_hasPermission(lua_State *l) {
	MumbleChannel *channel = luaL_checkudata(l, 1, METATABLE_CHAN);
	lua_pushboolean(l, (channel->permissions & luaL_checkint(l, 2)));
	return 1;
}

static int channel_setListeningVolumeAdjustment(lua_State *l) {
	MumbleChannel *channel = luaL_checkudata(l, 1, METATABLE_CHAN);

	MumbleProto__UserState__VolumeAdjustment adjustment = MUMBLE_PROTO__USER_STATE__VOLUME_ADJUSTMENT__INIT;

	adjustment.has_listening_channel = true;
	adjustment.listening_channel = channel->channel_id;
	adjustment.has_volume_adjustment = true;
	adjustment.volume_adjustment = luaL_checknumber(l, 2);

	packet_send(channel->client, PACKET_USERSTATE, &adjustment);
	return 0;
}

static int channel_getListeningVolumeAdjustment(lua_State *l) {
	MumbleChannel *channel = luaL_checkudata(l, 1, METATABLE_CHAN);
	lua_pushnumber(l, channel->listening_volume_adjustment);
	return 1;
}

int channel_call(lua_State *l) {
	MumbleChannel *channel = luaL_checkudata(l, 1, METATABLE_CHAN);
	char* path = (char*) luaL_optstring(l, 2, ".");

	char *pch = strtok(path, "/\\");

	while (pch != NULL) {
		MumbleChannel *current = NULL;

		if (strcmp(pch, ".") == 0) {
			current = channel;
		} else if (strcmp(pch, "..") == 0) {
			mumble_channel_raw_get(channel->client, channel->parent);
			current = lua_touserdata(l, -1);
			lua_remove(l, -2);
		} else {
			LinkNode* next_channel = channel->client->channel_list;
			while (next_channel != NULL) {
				MumbleChannel *chan = next_channel->data;
				if (chan->channel_id != chan->parent && chan->parent == channel->channel_id && strcmp(pch, chan->name) == 0) {
					current = chan;
				}
				next_channel = next_channel->next;
			}
		}

		if (current == NULL) {
			lua_pushnil(l);
			return 1;
		}

		channel = current;
		pch = strtok(NULL, "/\\");
	}

	mumble_channel_raw_get(channel->client, channel->channel_id);
	return 1;
}

static int channel_requestDescriptionBlob(lua_State *l) {
	MumbleChannel *channel = luaL_checkudata(l, 1, METATABLE_CHAN);

	MumbleProto__RequestBlob msg = MUMBLE_PROTO__REQUEST_BLOB__INIT;

	msg.n_channel_description = 1;
	msg.channel_description = malloc(sizeof(uint32_t) * msg.n_channel_description);

	if (msg.channel_description == NULL)
		return luaL_error(l, "failed to malloc: %s", strerror(errno));

	msg.channel_description[0] = channel->channel_id;

	packet_send(channel->client, PACKET_REQUESTBLOB, &msg);
	free(msg.channel_description);
	return 0;
}

static int channel_create(lua_State *l) {
	MumbleChannel *channel = luaL_checkudata(l, 1, METATABLE_CHAN);

	MumbleProto__ChannelState msg = MUMBLE_PROTO__CHANNEL_STATE__INIT;

	msg.parent = channel->channel_id;
	msg.name = (char*) luaL_checkstring(l, 2);
	msg.description = (char*) luaL_optstring(l, 3, "");
	msg.position = luaL_optinteger(l, 4, 0);
	msg.temporary = luaL_optboolean(l, 5, false);
	msg.max_users = luaL_optinteger(l, 6, 0);

	packet_send(channel->client, PACKET_CHANNELSTATE, &msg);
	return 0;
}

static int channel_contextAction(lua_State *l) {
	MumbleChannel *channel = luaL_checkudata(l, 1, METATABLE_CHAN);

	MumbleProto__ContextAction msg = MUMBLE_PROTO__CONTEXT_ACTION__INIT;

	msg.has_channel_id = true;
	msg.channel_id = channel->channel_id;

	msg.action = (char*) luaL_checkstring(l, 2);

	packet_send(channel->client, PACKET_CONTEXTACTION, &msg);
	return 0;
}

static int channel_gc(lua_State *l) {
	MumbleChannel *channel = luaL_checkudata(l, 1, METATABLE_CHAN);
	mumble_log(LOG_DEBUG, "%s: %p garbage collected", METATABLE_CHAN, channel);
	if (channel->name) {
		free(channel->name);
	}
	if (channel->description) {
		free(channel->description);
	}
	if (channel->description_hash) {
		free(channel->description_hash);
	}
	list_clear(&channel->links);
	mumble_registry_unref(l, MUMBLE_DATA_REG, &channel->data);
	return 0;
}

static int channel_tostring(lua_State *l) {
	MumbleChannel *channel = luaL_checkudata(l, 1, METATABLE_CHAN);
	lua_pushfstring(l, "%s [%d][\"%s\"] %p", METATABLE_CHAN, channel->channel_id, channel->name, channel);
	return 1;
}

static int channel_newindex(lua_State *l) {
	MumbleChannel *channel = luaL_checkudata(l, 1, METATABLE_CHAN);

	mumble_registry_pushref(l, MUMBLE_DATA_REG, channel->data);
	lua_pushvalue(l, 2);
	lua_pushvalue(l, 3);
	lua_settable(l, -3);
	return 0;
}

static int channel_index(lua_State *l) {
	MumbleChannel *channel = luaL_checkudata(l, 1, METATABLE_CHAN);

	mumble_registry_pushref(l, MUMBLE_DATA_REG, channel->data);
	lua_pushvalue(l, 2);
	lua_gettable(l, -2);

	if (lua_isnil(l, -1)) {
		lua_getmetatable(l, 1);
		lua_pushvalue(l, 2);
		lua_gettable(l, -2);
	}
	return 1;
}

const luaL_Reg mumble_channel[] = {
	{"message", channel_message},
	{"setDescription", channel_setDescription},
	{"remove", channel_remove},
	{"getClient", channel_getClient},
	{"getName", channel_getName},
	{"getId", channel_getId},
	{"getID", channel_getId},
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
	{"isEnterRestricted", channel_isEnterRestricted},
	{"canEnter", channel_canEnter},
	{"requestACL", channel_requestACL},
	{"requestPermissions", channel_requestPermissions},
	{"getPermissions", channel_getPermissions},
	{"hasPermission", channel_hasPermission},
	{"hasPermissions", channel_hasPermission},
	{"setListeningVolumeAdjustment", channel_setListeningVolumeAdjustment},
	{"getListeningVolumeAdjustment", channel_getListeningVolumeAdjustment},
	{"requestDescriptionBlob", channel_requestDescriptionBlob},
	{"create", channel_create},
	{"contextAction", channel_contextAction},

	{"__call", channel_call},
	{"__gc", channel_gc},
	{"__tostring", channel_tostring},
	{"__newindex", channel_newindex},
	{"__index", channel_index},
	{NULL, NULL}
};