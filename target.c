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

	uint32_t* new = realloc(target->session, sizeof(uint32_t) * (target->n_session + 1));

	if (new == NULL)
		return luaL_error(l, "out of memory");

	target->session = new;
	target->session[target->n_session] = user->session;
	target->n_session++;
	return 0;
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

static int target_setLinks(lua_State *l)
{
	MumbleProto__VoiceTarget__Target *target = luaL_checkudata(l, 1, METATABLE_VOICETARGET);
	
	target->has_links = true;
	target->links = luaL_checkboolean(l,2);
	return 0;
}

static int target_setChildren(lua_State *l)
{
	MumbleProto__VoiceTarget__Target *target = luaL_checkudata(l, 1, METATABLE_VOICETARGET);
	
	target->has_children = true;
	target->children = luaL_checkboolean(l,2);
	return 0;
}

static int target_tostring(lua_State *l)
{	
	MumbleProto__VoiceTarget__Target *target = luaL_checkudata(l, 1, METATABLE_VOICETARGET);

	lua_pushfstring(l, "%s: %p", METATABLE_VOICETARGET, target);
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
	{"setChannel", target_setChannel},
	{"getChannel", target_getChannel},
	{"setGroup", target_setGroup},
	{"setLinks", target_setLinks},
	{"setChildren", target_setChildren},
	{"__tostring", target_tostring},
	{"__gc", target_gc},
	{NULL, NULL}
};