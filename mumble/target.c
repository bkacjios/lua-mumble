#include "mumble.h"

#include "channel.h"
#include "user.h"
#include "target.h"

int mumble_target_new(lua_State *l)
{
	MumbleProto__VoiceTarget__Target *target = lua_newuserdata(l, sizeof(MumbleProto__VoiceTarget__Target));
	mumble_proto__voice_target__target__init(target);
	luaL_getmetatable(l, METATABLE_VOICETARGET);
	lua_setmetatable(l, -2);
	return 1;
}

static int target_addUser(lua_State *l)
{
	MumbleProto__VoiceTarget__Target *target = luaL_checkudata(l, 1, METATABLE_VOICETARGET);
	MumbleUser *user = luaL_checkudata(l, 2, METATABLE_USER);

	uint32_t* session = realloc(target->session, sizeof(uint32_t) * (target->n_session + 1));

	if (session == NULL)
		return luaL_error(l, "failed to realloc: %s", strerror(errno));

	target->session = session;
	target->session[target->n_session] = user->session;
	target->n_session++;
	return 0;
}

static int target_getUsers(lua_State *l)
{
	MumbleProto__VoiceTarget__Target *target = luaL_checkudata(l, 1, METATABLE_VOICETARGET);
	lua_newtable(l);
	for (uint32_t i = 0; i < target->n_session; i++) {
		lua_pushinteger(l, i+1);
		lua_pushinteger(l, target->session[i]);
		lua_settable(l, -2);
	}
	return 1;
}

static int target_setChannel(lua_State *l)
{
	MumbleProto__VoiceTarget__Target *target = luaL_checkudata(l, 1, METATABLE_VOICETARGET);
	MumbleChannel *channel = luaL_checkudata(l, 2, METATABLE_CHAN);

	target->has_channel_id = true;
	target->channel_id = channel->channel_id;
	return 0;
}

static int target_getChannel(lua_State *l)
{
	MumbleProto__VoiceTarget__Target *target = luaL_checkudata(l, 1, METATABLE_VOICETARGET);
	lua_pushinteger(l, target->channel_id);
	return 1;
}

static int target_setGroup(lua_State *l)
{
	MumbleProto__VoiceTarget__Target *target = luaL_checkudata(l, 1, METATABLE_VOICETARGET);

	target->group = (char*) luaL_checkstring(l,2);
	return 0;
}

static int target_getGroup(lua_State *l)
{
	MumbleProto__VoiceTarget__Target *target = luaL_checkudata(l, 1, METATABLE_VOICETARGET);
	lua_pushstring(l, target->group);
	return 1;
}

static int target_setLinks(lua_State *l)
{
	MumbleProto__VoiceTarget__Target *target = luaL_checkudata(l, 1, METATABLE_VOICETARGET);
	
	target->has_links = true;
	target->links = luaL_checkboolean(l,2);
	return 0;
}

static int target_getLinks(lua_State *l)
{
	MumbleProto__VoiceTarget__Target *target = luaL_checkudata(l, 1, METATABLE_VOICETARGET);
	lua_pushboolean(l, target->links);
	return 1;
}

static int target_setChildren(lua_State *l)
{
	MumbleProto__VoiceTarget__Target *target = luaL_checkudata(l, 1, METATABLE_VOICETARGET);
	
	target->has_children = true;
	target->children = luaL_checkboolean(l,2);
	return 0;
}

static int target_getChildren(lua_State *l)
{
	MumbleProto__VoiceTarget__Target *target = luaL_checkudata(l, 1, METATABLE_VOICETARGET);
	lua_pushboolean(l, target->children);
	return 1;
}

static int target_tostring(lua_State *l)
{	
	lua_pushfstring(l, "%s: %p", METATABLE_VOICETARGET, lua_topointer(l, 1));
	return 1;
}

static int target_gc(lua_State *l)
{
	MumbleProto__VoiceTarget__Target *target = luaL_checkudata(l, 1, METATABLE_VOICETARGET);
	free(target->session);
	return 0;
}

const luaL_Reg mumble_target[] = {
	{"addUser", target_addUser},
	{"getUsers", target_getUsers},
	{"setChannel", target_setChannel},
	{"getChannel", target_getChannel},
	{"setGroup", target_setGroup},
	{"getGroup", target_getGroup},
	{"setLinks", target_setLinks},
	{"getLinks", target_getLinks},
	{"setChildren", target_setChildren},
	{"getChildren", target_getChildren},
	{"__tostring", target_tostring},
	{"__gc", target_gc},
	{NULL, NULL}
};