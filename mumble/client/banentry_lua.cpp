#include "client/banentry.h"

#include "client/client.h"
#include "lua/bindings.h"
#include "lua/luainc.h"
#include "lua/luautil.h"
#include "lua/metatables.h"

static BanEntry* banentry_object(lua_State *l, int index) {
	return luaL_checkclass<BanEntry>(l, index, METATABLE_BANENTRY);
}

static MumbleProto__BanList__BanEntry* banentry_check(lua_State *l, int index) {
	return &banentry_object(l, index)->entry;
}

int mumble_banentry_new(lua_State *l) {
	lua_remove(l, 1);

	BanEntry *ban = lua_newclass<BanEntry>(l);
	luaL_getmetatable(l, METATABLE_BANENTRY);
	lua_setmetatable(l, -2);

	size_t length;
	const char *address = luaL_checklstring(l, 1, &length);
	ban->setAddress(address, length);
	ban->setMask(luaL_checkinteger(l, 2));

	return 1;
}

static int banentry_setAddress(lua_State *l) {
	size_t length;
	const char *address = luaL_checklstring(l, 2, &length);
	banentry_object(l, 1)->setAddress(address, length);
	return 0;
}

static int banentry_getAddress(lua_State *l) {
	MumbleProto__BanList__BanEntry *entry = banentry_check(l, 1);
	lua_pushaddress(l, entry->address);
	return 1;
}

static int banentry_setMask(lua_State *l) {
	banentry_object(l, 1)->setMask(luaL_checkinteger(l, 2));
	return 0;
}

static int banentry_getMask(lua_State *l) {
	MumbleProto__BanList__BanEntry *entry = banentry_check(l, 1);
	lua_pushinteger(l, entry->mask);
	return 1;
}

static int banentry_setName(lua_State *l) {
	banentry_object(l, 1)->setName(luaL_checkstring(l, 2));
	return 0;
}

static int banentry_getName(lua_State *l) {
	MumbleProto__BanList__BanEntry *entry = banentry_check(l, 1);
	lua_pushstring(l, entry->name);
	return 1;
}

static int banentry_setHash(lua_State *l) {
	banentry_object(l, 1)->setHash(luaL_checkstring(l, 2));
	return 0;
}

static int banentry_getHash(lua_State *l) {
	MumbleProto__BanList__BanEntry *entry = banentry_check(l, 1);
	lua_pushstring(l, entry->hash);
	return 1;
}

static int banentry_setReason(lua_State *l) {
	banentry_object(l, 1)->setReason(luaL_checkstring(l, 2));
	return 0;
}

static int banentry_getReason(lua_State *l) {
	MumbleProto__BanList__BanEntry *entry = banentry_check(l, 1);
	lua_pushstring(l, entry->reason);
	return 1;
}

static int banentry_setStart(lua_State *l) {
	banentry_object(l, 1)->setStart(luaL_checkstring(l, 2));
	return 0;
}

static int banentry_getStart(lua_State *l) {
	MumbleProto__BanList__BanEntry *entry = banentry_check(l, 1);
	lua_pushstring(l, entry->start);
	return 1;
}

static int banentry_setDuration(lua_State *l) {
	banentry_object(l, 1)->setDuration(luaL_checkinteger(l, 2));
	return 0;
}

static int banentry_getDuration(lua_State *l) {
	MumbleProto__BanList__BanEntry *entry = banentry_check(l, 1);
	lua_pushinteger(l, entry->duration);
	return 1;
}

static int banentry_gc(lua_State *l) {
	banentry_object(l, 1)->~BanEntry();
	return 0;
}

static int banentry_tostring(lua_State *l) {
	lua_pushfstring(l, "%s: %p", METATABLE_BANENTRY, lua_topointer(l, 1));
	return 1;
}

const luaL_Reg mumble_banentry[] = {
	{"setAddress", banentry_setAddress},
	{"getAddress", banentry_getAddress},
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
	{"__gc", banentry_gc},
	{NULL, NULL}
};
