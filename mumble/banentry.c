#include "mumble.h"

#include "channel.h"
#include "user.h"
#include "banentry.h"

int mumble_banentry_new(lua_State *l) {
	MumbleProto__BanList__BanEntry *entry = lua_newuserdata(l, sizeof(MumbleProto__BanList__BanEntry));
	mumble_proto__ban_list__ban_entry__init(entry);

	entry->address.data = (uint8_t *) luaL_checklstring(l, 1, &entry->address.len);
	entry->mask = luaL_checkinteger(l, 2);

	luaL_getmetatable(l, METATABLE_BANENTRY);
	lua_setmetatable(l, -2);
	return 1;
}

static int banentry_setAddress(lua_State *l) {
	MumbleProto__BanList__BanEntry *entry = luaL_checkudata(l, 1, METATABLE_BANENTRY);
	entry->address.data = (uint8_t *) luaL_checklstring(l, 2, &entry->address.len);
	return 0;
}

static int banentry_getAddress(lua_State *l) {
	MumbleProto__BanList__BanEntry *entry = luaL_checkudata(l, 1, METATABLE_BANENTRY);
	mumble_push_address(l, entry->address);
	return 1;
}

static int banentry_setMask(lua_State *l) {
	MumbleProto__BanList__BanEntry *entry = luaL_checkudata(l, 1, METATABLE_BANENTRY);
	entry->mask = luaL_checkinteger(l, 2);
	return 0;
}

static int banentry_getMask(lua_State *l) {
	MumbleProto__BanList__BanEntry *entry = luaL_checkudata(l, 1, METATABLE_BANENTRY);
	lua_pushinteger(l, entry->mask);
	return 1;
}

static int banentry_setName(lua_State *l) {
	MumbleProto__BanList__BanEntry *entry = luaL_checkudata(l, 1, METATABLE_BANENTRY);
	entry->name = (char*) luaL_checkstring(l, 2);
	return 0;
}

static int banentry_getName(lua_State *l) {
	MumbleProto__BanList__BanEntry *entry = luaL_checkudata(l, 1, METATABLE_BANENTRY);
	lua_pushstring(l, entry->name);
	return 1;
}

static int banentry_setHash(lua_State *l) {
	MumbleProto__BanList__BanEntry *entry = luaL_checkudata(l, 1, METATABLE_BANENTRY);
	entry->hash = (char*) luaL_checkstring(l, 2);
	return 0;
}

static int banentry_getHash(lua_State *l) {
	MumbleProto__BanList__BanEntry *entry = luaL_checkudata(l, 1, METATABLE_BANENTRY);
	lua_pushstring(l, entry->hash);
	return 1;
}

static int banentry_setReason(lua_State *l) {
	MumbleProto__BanList__BanEntry *entry = luaL_checkudata(l, 1, METATABLE_BANENTRY);
	entry->reason = (char*) luaL_checkstring(l, 2);
	return 0;
}

static int banentry_getReason(lua_State *l) {
	MumbleProto__BanList__BanEntry *entry = luaL_checkudata(l, 1, METATABLE_BANENTRY);
	lua_pushstring(l, entry->reason);
	return 1;
}

static int banentry_setStart(lua_State *l) {
	MumbleProto__BanList__BanEntry *entry = luaL_checkudata(l, 1, METATABLE_BANENTRY);
	entry->start = (char*) luaL_checkstring(l, 2);
	return 0;
}


static int banentry_getStart(lua_State *l) {
	MumbleProto__BanList__BanEntry *entry = luaL_checkudata(l, 1, METATABLE_BANENTRY);
	lua_pushstring(l, entry->start);
	return 1;
}

static int banentry_setDuration(lua_State *l) {
	MumbleProto__BanList__BanEntry *entry = luaL_checkudata(l, 1, METATABLE_BANENTRY);
	entry->duration = luaL_checkinteger(l, 2);
	return 0;
}


static int banentry_getDuration(lua_State *l) {
	MumbleProto__BanList__BanEntry *entry = luaL_checkudata(l, 1, METATABLE_BANENTRY);
	lua_pushinteger(l, entry->duration);
	return 1;
}

static int banentry_tostring(lua_State *l) {
	lua_pushfstring(l, "%s: %p", METATABLE_BANENTRY, lua_topointer(l, 1));
	return 1;
}

const luaL_Reg mumble_banentry[] = {
	{"setAddress", banentry_setAddress},
	{"getAddress", banentry_setAddress},
	{"setMask", banentry_setMask},
	{"getMask", banentry_getMask},
	{"setName", banentry_setName},
	{"getName", banentry_getName},
	{"setHash", banentry_setHash},
	{"getHash", banentry_getHash},
	{"setReason", banentry_setReason},
	{"getReason", banentry_getReason},
	{"setStart", banentry_setStart},
	{"getStart", banentry_getStart},
	{"setDuration", banentry_setDuration},
	{"getDuration", banentry_getDuration},
	{"__tostring", banentry_tostring},
	{NULL, NULL}
};