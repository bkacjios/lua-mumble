#include "mumble.h"

#include "channel.h"

/*--------------------------------
	MUMBLE CHANNEL META METHODS
--------------------------------*/

static int channel_add(lua_State *l)
{
	//MumbleChannel *channel = luaL_checkudata(l, 1, METATABLE_CHAN);

	// TODO: Figure out how channel adding works
	return 0;
}

static int channel_message(lua_State *l)
{
	MumbleChannel *channel = luaL_checkudata(l, 1, METATABLE_CHAN);

	MumbleProto__TextMessage msg = MUMBLE_PROTO__TEXT_MESSAGE__INIT;

	msg.message = (char*) luaL_checkstring(l, 2);
	msg.n_channel_id = 1;
	msg.channel_id = malloc(sizeof(uint32_t));
	msg.channel_id[0] = channel->channel_id;

	packet_send(channel->client, PACKET_TEXTMESSAGE, &msg);

	free(msg.channel_id);
	return 0;
}

static int channel_setDescription(lua_State *l)
{
	MumbleChannel *channel = luaL_checkudata(l, 1, METATABLE_CHAN);

	MumbleProto__ChannelState msg = MUMBLE_PROTO__CHANNEL_STATE__INIT;

	msg.has_channel_id = true;
	msg.channel_id = channel->channel_id;
	msg.description = (char *)lua_tostring(l, 2);

	packet_send(channel->client, PACKET_CHANNELSTATE, &msg);
	return 0;
}

static int channel_remove(lua_State *l)
{
	MumbleChannel *channel = luaL_checkudata(l, 1, METATABLE_CHAN);

	MumbleProto__ChannelRemove msg = MUMBLE_PROTO__CHANNEL_REMOVE__INIT;

	msg.channel_id = channel->channel_id;
	
	packet_send(channel->client, PACKET_CHANNELREMOVE, &msg);
	return 0;
}

static int channel_getClient(lua_State *l)
{
	MumbleChannel *channel = luaL_checkudata(l, 1, METATABLE_CHAN);
	mumble_client_raw_get(l, channel->client);
	return 1;
}

static int channel_getName(lua_State *l)
{
	MumbleChannel *channel = luaL_checkudata(l, 1, METATABLE_CHAN);
	lua_pushstring(l, channel->name);
	return 1;
}

static int channel_getID(lua_State *l)
{
	MumbleChannel *channel = luaL_checkudata(l, 1, METATABLE_CHAN);
	lua_pushinteger(l, channel->channel_id);
	return 1;
}

static int channel_getParent(lua_State *l)
{
	MumbleChannel *channel = luaL_checkudata(l, 1, METATABLE_CHAN);

	// This should only happen on the root channel
	if (channel->channel_id == channel->parent) {
		lua_pushnil(l);
		return 1;
	}
	
	mumble_channel_raw_get(l, channel->client, channel->parent);
	return 1;
}

static int channel_getChildren(lua_State *l)
{
	MumbleChannel *channel = luaL_checkudata(l, 1, METATABLE_CHAN);

	lua_newtable(l);

	lua_rawgeti(l, LUA_REGISTRYINDEX, channel->client->channels);
	lua_pushnil(l);

	while (lua_next(l, -2)) {
		if (lua_isuserdata(l, -1)) {
			MumbleChannel *chan = lua_touserdata(l, -1);
			if (chan->channel_id != chan->parent && chan->parent == channel->channel_id) {
				lua_pushinteger(l, chan->channel_id);
				lua_pushvalue(l, -2);
				lua_settable(l, -6);
			}
		}
		lua_pop(l, 1);
	}

	lua_pop(l, 1);

	return 1;
}

static int channel_getUsers(lua_State *l)
{
	MumbleChannel *channel = luaL_checkudata(l, 1, METATABLE_CHAN);

	lua_newtable(l);

	lua_rawgeti(l, LUA_REGISTRYINDEX, channel->client->users);
	lua_pushnil(l);

	while (lua_next(l, -2)) {
		if (lua_isuserdata(l, -1)) {
			MumbleUser *user = lua_touserdata(l, -1);
			if (user->channel_id == channel->channel_id) {
				lua_pushinteger(l, user->session);
				lua_pushvalue(l, -2);
				lua_settable(l, -6);
			}
		}
		lua_pop(l, 1);
	}

	lua_pop(l, 1);

	return 1;
}

static int channel_getDescription(lua_State *l)
{
	MumbleChannel *channel = luaL_checkudata(l, 1, METATABLE_CHAN);
	lua_pushstring(l, channel->description);
	return 1;
}

static int channel_getDescriptionHash(lua_State *l)
{
	MumbleChannel *channel = luaL_checkudata(l, 1, METATABLE_CHAN);
	char* result;
	bin_to_strhex(channel->description_hash, channel->description_hash_len, &result);
	lua_pushstring(l, result);
	free(result);
	return 1;
}

static int channel_isTemporary(lua_State *l)
{
	MumbleChannel *channel = luaL_checkudata(l, 1, METATABLE_CHAN);
	lua_pushboolean(l, channel->temporary);
	return 1;
}

static int channel_getPosition(lua_State *l)
{
	MumbleChannel *channel = luaL_checkudata(l, 1, METATABLE_CHAN);
	lua_pushinteger(l, channel->position);
	return 1;
}

static int channel_getMaxUsers(lua_State *l)
{
	MumbleChannel *channel = luaL_checkudata(l, 1, METATABLE_CHAN);
	lua_pushinteger(l, channel->max_users);
	return 1;
}

static int channel_link(lua_State *l)
{
	MumbleChannel *channel = luaL_checkudata(l, 1, METATABLE_CHAN);

	MumbleProto__ChannelState msg = MUMBLE_PROTO__CHANNEL_STATE__INIT;

	msg.has_channel_id = true;
	msg.channel_id = channel->channel_id;

	// Get the number of channels we want to link
	int n_links_add = lua_gettop(l) - 1;

	msg.n_links_add = n_links_add;

	msg.links_add = malloc(sizeof(uint32_t) * n_links_add);

	// Loop through each argument and add the channel_id to the array
	for (int i = 0; i < n_links_add; i++) {
		MumbleChannel *link = luaL_checkudata(l, i+2, METATABLE_CHAN);
		msg.links_add[i] = link->channel_id;
	}

	packet_send(channel->client, PACKET_CHANNELSTATE, &msg);

	free(msg.links_add);

	return 0;
}

static int channel_unlink(lua_State *l)
{
	MumbleChannel *channel = luaL_checkudata(l, 1, METATABLE_CHAN);

	MumbleProto__ChannelState msg = MUMBLE_PROTO__CHANNEL_STATE__INIT;

	msg.has_channel_id = true;
	msg.channel_id = channel->channel_id;

	// Get the number of channels we want to unlink
	int n_links_remove = lua_gettop(l) - 1;

	msg.n_links_remove = n_links_remove;
	msg.links_remove = malloc(sizeof(uint32_t) * n_links_remove);

	for (int i = 0; i < n_links_remove; i++) {
		MumbleChannel *link = luaL_checkudata(l, i+2, METATABLE_CHAN);
		msg.links_remove[i] = link->channel_id;
	}

	packet_send(channel->client, PACKET_CHANNELSTATE, &msg);

	free(msg.links_remove);

	return 0;
}

static int channel_getLinks(lua_State *l)
{
	MumbleChannel *channel = luaL_checkudata(l, 1, METATABLE_CHAN);

	lua_newtable(l);

	LinkNode * current = channel->links;

	// Add all linked channels to the table
    while (current != NULL) {
		lua_pushinteger(l, current->data);
		mumble_channel_raw_get(l, channel->client, current->data);
		lua_settable(l, -3);

        current = current->next;
    }

	return 1;
}

int channel_call(lua_State *l)
{
	MumbleChannel *self = luaL_checkudata(l, 1, METATABLE_CHAN);
	char* path = (char*) luaL_checkstring(l, 2);

	MumbleChannel *channel = self;
	char *pch = strtok(path, "/\\");

	while (pch != NULL)
	{
		MumbleChannel *current = NULL;

		if (strcmp(pch, ".") == 0) {
			current = channel;
		} else if(strcmp(pch, "..") == 0) {
			current = mumble_channel_raw_get(l, channel->client, channel->parent);
		} else {
			lua_rawgeti(l, LUA_REGISTRYINDEX, self->client->channels);
			lua_pushnil(l);

			while (lua_next(l, -2)) {
				if (lua_isuserdata(l, -1)) {
					MumbleChannel *chan = lua_touserdata(l, -1);
					if (chan->channel_id != chan->parent && chan->parent == channel->channel_id && strcmp(pch, chan->name) == 0) {
						current = chan;
					}
				}
				lua_pop(l, 1);
			}

			lua_pop(l, 1);
		}

		if (current == NULL) {
			lua_pushnil(l);
			return 1;
		}

		channel = current;
		pch = strtok(NULL, "/\\");
	}

	mumble_channel_raw_get(l, channel->client, channel->channel_id);
	return 1;
}

static int channel_tostring(lua_State *l)
{
	MumbleChannel *channel = luaL_checkudata(l, 1, METATABLE_CHAN);
	lua_pushfstring(l, "%s [%d][%s]", METATABLE_CHAN, channel->channel_id, channel->name);
	return 1;
}

static int channel_newindex(lua_State *l)
{
	MumbleChannel *channel = luaL_checkudata(l, 1, METATABLE_CHAN);

	lua_rawgeti(l, LUA_REGISTRYINDEX, channel->data);
	lua_pushvalue(l, 2);
	lua_pushvalue(l, 3);
	lua_settable(l, -3);
	return 0;
}

static int channel_index(lua_State *l)
{
	MumbleChannel *channel = luaL_checkudata(l, 1, METATABLE_CHAN);

	lua_rawgeti(l, LUA_REGISTRYINDEX, channel->data);
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