#include "client/target.h"

#include "client/channel.h"
#include "client/client.h"
#include "client/user.h"
#include "lua/bindings.h"
#include "lua/luainc.h"
#include "lua/luautil.h"
#include "lua/metatables.h"
#include "util/log.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>

static MumbleProto__VoiceTarget__Target* target_check(lua_State *l, int index) {
	return &luaL_checkclass<VoiceTarget>(l, index, METATABLE_VOICETARGET)->target;
}

int mumble_target_new(lua_State *l) {
	lua_newclass<VoiceTarget>(l);
	luaL_getmetatable(l, METATABLE_VOICETARGET);
	lua_setmetatable(l, -2);
	return 1;
}

static int target_addUser(lua_State *l) {
	VoiceTarget *target = luaL_checkclass<VoiceTarget>(l, 1, METATABLE_VOICETARGET);
	MumbleUser *user = luaL_checkclass<MumbleUser>(l, 2, METATABLE_USER);
	target->addSession(user->session);
	return 0;
}

static int target_getUsers(lua_State *l) {
	MumbleProto__VoiceTarget__Target *target = target_check(l, 1);
	lua_newtable(l);
	for (uint32_t i = 0; i < target->n_session; i++) {
		lua_pushinteger(l, i + 1);
		lua_pushinteger(l, target->session[i]);
		lua_settable(l, -3);
	}
	return 1;
}

static int target_setChannel(lua_State *l) {
	VoiceTarget *target = luaL_checkclass<VoiceTarget>(l, 1, METATABLE_VOICETARGET);
	MumbleChannel *channel = luaL_checkclass<MumbleChannel>(l, 2, METATABLE_CHAN);
	target->setChannel(channel->channel_id);
	return 0;
}

static int target_getChannel(lua_State *l) {
	MumbleProto__VoiceTarget__Target *target = target_check(l, 1);
	lua_pushinteger(l, target->channel_id);
	return 1;
}

static int target_setGroup(lua_State *l) {
	luaL_checkclass<VoiceTarget>(l, 1, METATABLE_VOICETARGET)->setGroup(luaL_checkstring(l, 2));
	return 0;
}

static int target_getGroup(lua_State *l) {
	MumbleProto__VoiceTarget__Target *target = target_check(l, 1);
	lua_pushstring(l, target->group);
	return 1;
}

static int target_setLinks(lua_State *l) {
	luaL_checkclass<VoiceTarget>(l, 1, METATABLE_VOICETARGET)->setLinks(luaL_checkboolean(l, 2));
	return 0;
}

static int target_getLinks(lua_State *l) {
	MumbleProto__VoiceTarget__Target *target = target_check(l, 1);
	lua_pushboolean(l, target->links);
	return 1;
}

static int target_setChildren(lua_State *l) {
	luaL_checkclass<VoiceTarget>(l, 1, METATABLE_VOICETARGET)->setChildren(luaL_checkboolean(l, 2));
	return 0;
}

static int target_getChildren(lua_State *l) {
	MumbleProto__VoiceTarget__Target *target = target_check(l, 1);
	lua_pushboolean(l, target->children);
	return 1;
}

static int target_tostring(lua_State *l) {
	lua_pushfstring(l, "%s: %p", METATABLE_VOICETARGET, lua_topointer(l, 1));
	return 1;
}

static int target_gc(lua_State *l) {
	MumbleProto__VoiceTarget__Target *target = target_check(l, 1);
	mumble_log(LOG_DEBUG, "%s: %p garbage collected", METATABLE_VOICETARGET, target);
	luaL_checkclass<VoiceTarget>(l, 1, METATABLE_VOICETARGET)->~VoiceTarget();
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
