#include "mumble.h"

/*--------------------------------
	MUMBLE CHANNEL META METHODS
--------------------------------*/

int channel_message(lua_State *l)
{
	MumbleClient *client = mumble_check_meta(l, 1, METATABLE_CHAN);

	MumbleProto__TextMessage msg = MUMBLE_PROTO__TEXT_MESSAGE__INIT;

	lua_getfield(l, 1, "channel_id");
	uint32_t channel = lua_tointeger(l, -1);

	msg.message = (char*) luaL_checkstring(l, 2);
	msg.n_channel_id = 1;
	msg.channel_id = &channel;

	packet_send(client, PACKET_TEXTMESSAGE, &msg);
	return 0;
}

int channel_setDescription(lua_State *l)
{
	MumbleClient *client = mumble_check_meta(l, 1, METATABLE_CHAN);

	MumbleProto__ChannelState msg = MUMBLE_PROTO__CHANNEL_STATE__INIT;

	lua_getfield(l, 1, "channel_id");
	msg.has_channel_id = true;
	msg.channel_id = lua_tointeger(l, -1);
	msg.description = (char *)lua_tostring(l, 2);

	packet_send(client, PACKET_CHANNELSTATE, &msg);
	return 0;
}

int channel_remove(lua_State *l)
{
	MumbleClient *client = mumble_check_meta(l, 1, METATABLE_CHAN);

	MumbleProto__ChannelRemove msg = MUMBLE_PROTO__CHANNEL_REMOVE__INIT;

	lua_getfield(l, 1, "channel_id");
	msg.channel_id = lua_tointeger(l, -1);
	
	packet_send(client, PACKET_CHANNELREMOVE, &msg);
	return 0;
}

int channel_tostring(lua_State *l)
{
	MumbleClient *client = mumble_check_meta(l, 1, METATABLE_CHAN);
	lua_pushfstring(l, "%s: %p", METATABLE_CHAN, lua_topointer(l, 1));
	return 1;
}