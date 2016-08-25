#include "mumble.h"

/*--------------------------------
	MUMBLE CHANNEL META METHODS
--------------------------------*/

int channel_message(lua_State *l)
{
	MumbleChannel *channel = luaL_checkudata(l, 1, METATABLE_CHAN);

	MumbleProto__TextMessage msg = MUMBLE_PROTO__TEXT_MESSAGE__INIT;

	msg.message = (char*) luaL_checkstring(l, 2);
	msg.n_channel_id = 1;
	msg.channel_id = &channel->channel_id;

	packet_send(channel->client, PACKET_TEXTMESSAGE, &msg);
	return 0;
}

int channel_setDescription(lua_State *l)
{
	MumbleChannel *channel = luaL_checkudata(l, 1, METATABLE_CHAN);

	MumbleProto__ChannelState msg = MUMBLE_PROTO__CHANNEL_STATE__INIT;

	msg.has_channel_id = true;
	msg.channel_id = channel->channel_id;
	msg.description = (char *)lua_tostring(l, 2);

	packet_send(channel->client, PACKET_CHANNELSTATE, &msg);
	return 0;
}

int channel_remove(lua_State *l)
{
	MumbleChannel *channel = luaL_checkudata(l, 1, METATABLE_CHAN);

	MumbleProto__ChannelRemove msg = MUMBLE_PROTO__CHANNEL_REMOVE__INIT;

	msg.channel_id = channel->channel_id;
	
	packet_send(channel->client, PACKET_CHANNELREMOVE, &msg);
	return 0;
}

int channel_getClient(lua_State *l)
{
	MumbleChannel *channel = luaL_checkudata(l, 1, METATABLE_CHAN);
	lua_rawgeti(l, LUA_REGISTRYINDEX, MUMBLE_CONNECTIONS);
	lua_rawgeti(l, -1, channel->client->self);
	return 1;
}

int channel_getName(lua_State *l)
{
	MumbleChannel *channel = luaL_checkudata(l, 1, METATABLE_CHAN);
	lua_pushstring(l, channel->name);
	return 1;
}

int channel_getID(lua_State *l)
{
	MumbleChannel *channel = luaL_checkudata(l, 1, METATABLE_CHAN);
	lua_pushinteger(l, channel->channel_id);
	return 1;
}

int channel_getParent(lua_State *l)
{
	MumbleChannel *channel = luaL_checkudata(l, 1, METATABLE_CHAN);
	mumble_channel_raw_get(channel->client, channel->parent);
	return 1;
}

int channel_getDescription(lua_State *l)
{
	MumbleChannel *channel = luaL_checkudata(l, 1, METATABLE_CHAN);
	lua_pushstring(l, channel->description);
	return 1;
}

int channel_getDescriptionHash(lua_State *l)
{
	MumbleChannel *channel = luaL_checkudata(l, 1, METATABLE_CHAN);
	lua_pushstring(l, channel->description_hash);
	return 1;
}

int channel_isTemporary(lua_State *l)
{
	MumbleChannel *channel = luaL_checkudata(l, 1, METATABLE_CHAN);
	lua_pushboolean(l, channel->temporary);
	return 1;
}

int channel_getPosition(lua_State *l)
{
	MumbleChannel *channel = luaL_checkudata(l, 1, METATABLE_CHAN);
	lua_pushinteger(l, channel->position);
	return 1;
}

int channel_getMaxUsers(lua_State *l)
{
	MumbleChannel *channel = luaL_checkudata(l, 1, METATABLE_CHAN);
	lua_pushinteger(l, channel->max_users);
	return 1;
}

int channel_tostring(lua_State *l)
{
	MumbleChannel *channel = luaL_checkudata(l, 1, METATABLE_CHAN);
	lua_pushfstring(l, "%s: %p", METATABLE_CHAN, channel);
	return 1;
}

int channel_newindex(lua_State *l)
{
	MumbleChannel *channel = luaL_checkudata(l, 1, METATABLE_CHAN);

	lua_rawgeti(l, LUA_REGISTRYINDEX, channel->data);
	lua_pushvalue(l, 2);
	lua_pushvalue(l, 3);
	lua_settable(l, -3);
	return 0;
}

int channel_index(lua_State *l)
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