#include "mumble.h"

/*--------------------------------
	MUMBLE USER META METHODS
--------------------------------*/

int user_message(lua_State *l)
{
	MumbleClient *client = mumble_check_meta(l, 1, METATABLE_USER);

	MumbleProto__TextMessage msg = MUMBLE_PROTO__TEXT_MESSAGE__INIT;

	lua_getfield(l, 1, "session");
	uint32_t session = lua_tointeger(l, -1);

	msg.n_session = 1;
	msg.session = &session;
	msg.message = (char*) luaL_checkstring(l, 2);

	packet_send(client, PACKET_TEXTMESSAGE, &msg);
	return 0;
}

int user_kick(lua_State *l)
{
	MumbleClient *client = mumble_check_meta(l, 1, METATABLE_USER);

	MumbleProto__UserRemove msg = MUMBLE_PROTO__USER_REMOVE__INIT;

	lua_getfield(l, 1, "session");
	msg.session = lua_tointeger(l, -1);

	if (lua_isnil(l, 2) == 0) {
		msg.reason = (char*) luaL_checkstring(l, 2);
	}

	packet_send(client, PACKET_USERREMOVE, &msg);
	return 0;
}

int user_ban(lua_State *l)
{
	MumbleClient *client = mumble_check_meta(l, 1, METATABLE_USER);

	MumbleProto__UserRemove msg = MUMBLE_PROTO__USER_REMOVE__INIT;

	lua_getfield(l, 1, "session");
	msg.session = lua_tointeger(l, -1);

	if (lua_isnil(l, 2) == 0) {
		msg.reason = (char *)luaL_checkstring(l, 2);
	}

	msg.has_ban = true;
	msg.ban = true;

	packet_send(client, PACKET_USERREMOVE, &msg);
	return 0;
}

int user_move(lua_State *l)
{
	MumbleClient *client = mumble_check_meta(l, 1, METATABLE_USER);
	luaL_checktablemeta(l, 2, METATABLE_CHAN);

	MumbleProto__UserState msg = MUMBLE_PROTO__USER_STATE__INIT;

	lua_getfield(l, 1, "session");
	msg.has_session = true;
	msg.session = lua_tointeger(l, -1);
	
	lua_getfield(l, 2, "channel_id");
	msg.has_channel_id = true;
	msg.channel_id = lua_tointeger(l, -1);

	packet_send(client, PACKET_USERSTATE, &msg);
	return 0;
}

int user_setMuted(lua_State *l)
{
	MumbleClient *client = mumble_check_meta(l, 1, METATABLE_USER);

	MumbleProto__UserState msg = MUMBLE_PROTO__USER_STATE__INIT;

	lua_getfield(l, 1, "session");
	msg.has_session = true;
	msg.session = lua_tointeger(l, -1);

	msg.has_mute = true;
	msg.mute = lua_toboolean(l, 2);

	packet_send(client, PACKET_USERSTATE, &msg);
	return 0;
}

int user_setDeaf(lua_State *l)
{
	MumbleClient *client = mumble_check_meta(l, 1, METATABLE_USER);
	
	MumbleProto__UserState msg = MUMBLE_PROTO__USER_STATE__INIT;

	lua_getfield(l, 1, "session");
	msg.has_session = true;
	msg.session = lua_tointeger(l, -1);

	msg.has_deaf = true;
	msg.deaf = lua_toboolean(l, 2);

	packet_send(client, PACKET_USERSTATE, &msg);
	return 0;
}

int user_comment(lua_State *l)
{
	MumbleClient *client = mumble_check_meta(l, 1, METATABLE_USER);
	
	MumbleProto__UserState msg = MUMBLE_PROTO__USER_STATE__INIT;

	lua_getfield(l, 1, "session");
	msg.has_session = true;
	msg.session = lua_tointeger(l, -1);

	msg.comment = (char*) luaL_checkstring(l, 2);

	packet_send(client, PACKET_USERSTATE, &msg);
	return 0;
}

int user_register(lua_State *l)
{
	MumbleClient *client = mumble_check_meta(l, 1, METATABLE_USER);
	
	MumbleProto__UserState msg = MUMBLE_PROTO__USER_STATE__INIT;
	msg.has_user_id = true;
	msg.user_id = 0;

	lua_getfield(l, 1, "session");
	msg.has_session = true;
	msg.session = lua_tointeger(l, -1);

	packet_send(client, PACKET_USERSTATE, &msg);
	return 0;
}

int user_request_stats(lua_State *l)
{
	MumbleClient *client = mumble_check_meta(l, 1, METATABLE_USER);

	MumbleProto__UserStats msg = MUMBLE_PROTO__USER_STATS__INIT;

	lua_getfield(l, 1, "session");
	msg.has_session = true;
	msg.session = lua_tointeger(l, -1);

	msg.stats_only = lua_toboolean(l, 2);

	packet_send(client, PACKET_USERSTATS, &msg);
	return 0;
}

int user_tostring(lua_State *l)
{	
	MumbleClient *client = mumble_check_meta(l, 1, METATABLE_USER);
	lua_pushfstring(l, "%s: %p", METATABLE_USER, lua_topointer(l, 1));
	return 1;
}