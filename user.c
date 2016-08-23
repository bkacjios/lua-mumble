#include "mumble.h"

/*--------------------------------
	MUMBLE USER META METHODS
--------------------------------*/

int user_message(lua_State *l)
{
	MumbleUser *user = luaL_checkudata(l, 1, METATABLE_USER);

	MumbleProto__TextMessage msg = MUMBLE_PROTO__TEXT_MESSAGE__INIT;

	msg.n_session = 1;
	msg.session = &user->session;
	msg.message = (char*) luaL_checkstring(l, 2);

	packet_send(user->client, PACKET_TEXTMESSAGE, &msg);
	return 0;
}

int user_kick(lua_State *l)
{
	MumbleUser *user = luaL_checkudata(l, 1, METATABLE_USER);

	MumbleProto__UserRemove msg = MUMBLE_PROTO__USER_REMOVE__INIT;

	msg.session = user->session;

	if (lua_isnil(l, 2) == 0) {
		msg.reason = (char*) luaL_checkstring(l, 2);
	}

	packet_send(user->client, PACKET_USERREMOVE, &msg);
	return 0;
}

int user_ban(lua_State *l)
{
	MumbleUser *user = luaL_checkudata(l, 1, METATABLE_USER);

	MumbleProto__UserRemove msg = MUMBLE_PROTO__USER_REMOVE__INIT;

	msg.session = user->session;

	if (lua_isnil(l, 2) == 0) {
		msg.reason = (char *)luaL_checkstring(l, 2);
	}

	msg.has_ban = true;
	msg.ban = true;

	packet_send(user->client, PACKET_USERREMOVE, &msg);
	return 0;
}

int user_move(lua_State *l)
{
	MumbleUser *user = luaL_checkudata(l, 1, METATABLE_USER);
	luaL_checktablemeta(l, 2, METATABLE_CHAN);

	MumbleProto__UserState msg = MUMBLE_PROTO__USER_STATE__INIT;

	msg.has_session = true;
	msg.session = user->session;
	
	lua_getfield(l, 2, "id");
	msg.has_channel_id = true;
	msg.channel_id = lua_tointeger(l, -1);

	packet_send(user->client, PACKET_USERSTATE, &msg);
	return 0;
}

int user_setMuted(lua_State *l)
{
	MumbleUser *user = luaL_checkudata(l, 1, METATABLE_USER);

	MumbleProto__UserState msg = MUMBLE_PROTO__USER_STATE__INIT;

	msg.has_session = true;
	msg.session = user->session;

	msg.has_mute = true;
	msg.mute = lua_toboolean(l, 2);

	packet_send(user->client, PACKET_USERSTATE, &msg);
	return 0;
}

int user_setDeaf(lua_State *l)
{
	MumbleUser *user = luaL_checkudata(l, 1, METATABLE_USER);
	
	MumbleProto__UserState msg = MUMBLE_PROTO__USER_STATE__INIT;

	msg.has_session = true;
	msg.session = user->session;

	msg.has_mute = true;
	msg.mute = lua_toboolean(l, 2);
	
	msg.has_deaf = true;
	msg.deaf = lua_toboolean(l, 2);

	packet_send(user->client, PACKET_USERSTATE, &msg);
	return 0;
}

int user_register(lua_State *l)
{
	MumbleUser *user = luaL_checkudata(l, 1, METATABLE_USER);
	
	MumbleProto__UserState msg = MUMBLE_PROTO__USER_STATE__INIT;
	msg.has_user_id = true;
	msg.user_id = 0;

	msg.has_session = true;
	msg.session = user->session;

	packet_send(user->client, PACKET_USERSTATE, &msg);
	return 0;
}

int user_request_stats(lua_State *l)
{
	MumbleUser *user = luaL_checkudata(l, 1, METATABLE_USER);

	MumbleProto__UserStats msg = MUMBLE_PROTO__USER_STATS__INIT;

	msg.has_session = true;
	msg.session = user->session;

	msg.stats_only = lua_toboolean(l, 2);

	packet_send(user->client, PACKET_USERSTATS, &msg);
	return 0;
}

int user_getSession(lua_State *l)
{
	MumbleUser *user = luaL_checkudata(l, 1, METATABLE_USER);
	lua_pushinteger(l, user->session);
	return 1;
}

int user_getName(lua_State *l)
{
	MumbleUser *user = luaL_checkudata(l, 1, METATABLE_USER);
	lua_pushstring(l, user->name);
	return 1;
}

int user_getID(lua_State *l)
{
	MumbleUser *user = luaL_checkudata(l, 1, METATABLE_USER);
	lua_pushinteger(l, user->user_id);
	return 1;
}

int user_getMute(lua_State *l)
{
	MumbleUser *user = luaL_checkudata(l, 1, METATABLE_USER);
	lua_pushboolean(l, user->mute);
	return 1;
}

int user_tostring(lua_State *l)
{	
	MumbleUser *user = luaL_checkudata(l, 1, METATABLE_USER);
	lua_pushfstring(l, "%s: %p", METATABLE_USER, user);
	return 1;
}

int user_newindex(lua_State *l)
{
	MumbleUser *user = luaL_checkudata(l, 1, METATABLE_USER);

	lua_rawgeti(l, LUA_REGISTRYINDEX, user->data);
	lua_pushvalue(l, 2);
	lua_pushvalue(l, 3);
	lua_settable(l, -3);
	return 0;
}

int user_index(lua_State *l)
{
	MumbleUser *user = luaL_checkudata(l, 1, METATABLE_USER);

	lua_rawgeti(l, LUA_REGISTRYINDEX, user->data);
	lua_pushvalue(l, 2);
	lua_gettable(l, -2);

	if (lua_isnil(l, -1)) {
		lua_getmetatable(l, 1);
		lua_pushvalue(l, 2);
		lua_gettable(l, -2);
	}
	return 1;
}