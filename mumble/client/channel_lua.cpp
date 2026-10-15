#include "client/channel.h"

#include "client/client.h"
#include "client/user.h"
#include "lua/bindings.h"
#include "lua/luainc.h"
#include "lua/luautil.h"
#include "lua/metatables.h"
#include "lua/registry.h"
#include "net/packet.h"
#include "util/log.h"
#include "util/util.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>

/*--------------------------------
	MUMBLE CHANNEL META METHODS
--------------------------------*/

static int channel_message(lua_State *l) {
	MumbleChannel *channel = luaL_checkclass<MumbleChannel>(l, 1, METATABLE_CHAN);
	channel->message(luaL_checkstring(l, 2));
	return 0;
}

static int channel_setDescription(lua_State *l) {
	MumbleChannel *channel = luaL_checkclass<MumbleChannel>(l, 1, METATABLE_CHAN);
	channel->setDescription(lua_tostring(l, 2));
	return 0;
}

static int channel_remove(lua_State *l) {
	MumbleChannel *channel = luaL_checkclass<MumbleChannel>(l, 1, METATABLE_CHAN);
	channel->remove();
	return 0;
}

static int channel_getClient(lua_State *l) {
	MumbleChannel *channel = luaL_checkclass<MumbleChannel>(l, 1, METATABLE_CHAN);
	channel->client->pushSelf();
	return 1;
}

static int channel_getName(lua_State *l) {
	MumbleChannel *channel = luaL_checkclass<MumbleChannel>(l, 1, METATABLE_CHAN);
	lua_pushoptstring(l, channel->name);
	return 1;
}

static int channel_getId(lua_State *l) {
	MumbleChannel *channel = luaL_checkclass<MumbleChannel>(l, 1, METATABLE_CHAN);
	lua_pushinteger(l, channel->channel_id);
	return 1;
}

static int channel_getParent(lua_State *l) {
	MumbleChannel *channel = luaL_checkclass<MumbleChannel>(l, 1, METATABLE_CHAN);

	// This should only happen on the root channel
	if (channel->isRoot()) {
		lua_pushnil(l);
		return 1;
	}

	channel->client->pushChannel(channel->parent);
	return 1;
}

static int channel_getChildren(lua_State *l) {
	MumbleChannel *channel = luaL_checkclass<MumbleChannel>(l, 1, METATABLE_CHAN);
	lua_newtable(l);
	int i = 1;
	for (MumbleChannel *child : channel->children()) {
		lua_pushinteger(l, i++);
		channel->client->pushChannel(child->channel_id);
		lua_settable(l, -3);
	}
	return 1;
}

static int channel_getUsers(lua_State *l) {
	MumbleChannel *channel = luaL_checkclass<MumbleChannel>(l, 1, METATABLE_CHAN);
	lua_newtable(l);
	int i = 1;
	for (MumbleUser *user : channel->users()) {
		lua_pushinteger(l, i++);
		channel->client->pushUser(user->session);
		lua_settable(l, -3);
	}
	return 1;
}

static int channel_getDescription(lua_State *l) {
	MumbleChannel *channel = luaL_checkclass<MumbleChannel>(l, 1, METATABLE_CHAN);
	lua_pushoptstring(l, channel->description);
	return 1;
}

static int channel_getDescriptionHash(lua_State *l) {
	MumbleChannel *channel = luaL_checkclass<MumbleChannel>(l, 1, METATABLE_CHAN);
	lua_pushstring(l, bin_to_strhex(channel->description_hash).c_str());
	return 1;
}

static int channel_isTemporary(lua_State *l) {
	MumbleChannel *channel = luaL_checkclass<MumbleChannel>(l, 1, METATABLE_CHAN);
	lua_pushboolean(l, channel->temporary);
	return 1;
}

static int channel_getPosition(lua_State *l) {
	MumbleChannel *channel = luaL_checkclass<MumbleChannel>(l, 1, METATABLE_CHAN);
	lua_pushinteger(l, channel->position);
	return 1;
}

static int channel_getMaxUsers(lua_State *l) {
	MumbleChannel *channel = luaL_checkclass<MumbleChannel>(l, 1, METATABLE_CHAN);
	lua_pushinteger(l, channel->max_users);
	return 1;
}

static int channel_link(lua_State *l) {
	MumbleChannel *channel = luaL_checkclass<MumbleChannel>(l, 1, METATABLE_CHAN);
	channel->link(lua_checkchannelids(l, 2), {});
	return 0;
}

static int channel_unlink(lua_State *l) {
	MumbleChannel *channel = luaL_checkclass<MumbleChannel>(l, 1, METATABLE_CHAN);
	channel->link({}, lua_checkchannelids(l, 2));
	return 0;
}

static int channel_getLinks(lua_State *l) {
	MumbleChannel *channel = luaL_checkclass<MumbleChannel>(l, 1, METATABLE_CHAN);

	lua_newtable(l);

	// Add all linked channels to the table
	for (auto &node : channel->links) {
		lua_pushinteger(l, node.index);
		channel->client->pushChannel(node.index);
		lua_settable(l, -3);
	}

	return 1;
}

static int channel_isEnterRestricted(lua_State *l) {
	MumbleChannel *channel = luaL_checkclass<MumbleChannel>(l, 1, METATABLE_CHAN);
	lua_pushboolean(l, channel->is_enter_restricted);
	return 1;
}

static int channel_canEnter(lua_State *l) {
	MumbleChannel *channel = luaL_checkclass<MumbleChannel>(l, 1, METATABLE_CHAN);
	lua_pushboolean(l, channel->can_enter);
	return 1;
}

static int channel_requestACL(lua_State *l) {
	MumbleChannel *channel = luaL_checkclass<MumbleChannel>(l, 1, METATABLE_CHAN);
	channel->requestACL();
	return 0;
}

static int channel_requestPermissions(lua_State *l) {
	MumbleChannel *channel = luaL_checkclass<MumbleChannel>(l, 1, METATABLE_CHAN);
	channel->requestPermissions();
	return 0;
}

static int channel_getPermissions(lua_State *l) {
	MumbleChannel *channel = luaL_checkclass<MumbleChannel>(l, 1, METATABLE_CHAN);
	lua_pushinteger(l, channel->permissions);
	return 1;
}

static int channel_hasPermission(lua_State *l) {
	MumbleChannel *channel = luaL_checkclass<MumbleChannel>(l, 1, METATABLE_CHAN);
	lua_pushboolean(l, channel->hasPermission(luaL_checkint(l, 2)));
	return 1;
}

static int channel_setListeningVolumeAdjustment(lua_State *l) {
	MumbleChannel *channel = luaL_checkclass<MumbleChannel>(l, 1, METATABLE_CHAN);
	channel->setListeningVolumeAdjustment(luaL_checknumber(l, 2));
	return 0;
}

static int channel_getListeningVolumeAdjustment(lua_State *l) {
	MumbleChannel *channel = luaL_checkclass<MumbleChannel>(l, 1, METATABLE_CHAN);
	lua_pushnumber(l, channel->listening_volume_adjustment);
	return 1;
}

int channel_call(lua_State *l) {
	MumbleChannel *channel = luaL_checkclass<MumbleChannel>(l, 1, METATABLE_CHAN);
	MumbleChannel *found = channel->find(luaL_optstring(l, 2, "."));
	if (found == NULL) {
		lua_pushnil(l);
	} else {
		channel->client->pushChannel(found->channel_id);
	}
	return 1;
}

static int channel_requestDescriptionBlob(lua_State *l) {
	MumbleChannel *channel = luaL_checkclass<MumbleChannel>(l, 1, METATABLE_CHAN);
	channel->requestDescriptionBlob();
	return 0;
}

static int channel_create(lua_State *l) {
	MumbleChannel *channel = luaL_checkclass<MumbleChannel>(l, 1, METATABLE_CHAN);
	const char *name = luaL_checkstring(l, 2);
	const char *description = luaL_optstring(l, 3, "");
	int32_t position = luaL_optinteger(l, 4, 0);
	bool temporary = luaL_optboolean(l, 5, false);
	uint32_t max_users = luaL_optinteger(l, 6, 0);
	channel->create(name, description, position, temporary, max_users);
	return 0;
}

static int channel_contextAction(lua_State *l) {
	MumbleChannel *channel = luaL_checkclass<MumbleChannel>(l, 1, METATABLE_CHAN);
	channel->contextAction(luaL_checkstring(l, 2));
	return 0;
}

static int channel_gc(lua_State *l) {
	MumbleChannel *channel = luaL_checkclass<MumbleChannel>(l, 1, METATABLE_CHAN);
	mumble_log(LOG_DEBUG, "%s: %p garbage collected", METATABLE_CHAN, channel);
	LuaRegistry::unrefIn(l, LuaRegistry::data, &channel->data);
	channel->~MumbleChannel();
	return 0;
}

static int channel_tostring(lua_State *l) {
	MumbleChannel *channel = luaL_checkclass<MumbleChannel>(l, 1, METATABLE_CHAN);
	lua_pushfstring(l, "%s [%d][\"%s\"] %p", METATABLE_CHAN, channel->channel_id, optcstr(channel->name), channel);
	return 1;
}

static int channel_newindex(lua_State *l) {
	MumbleChannel *channel = luaL_checkclass<MumbleChannel>(l, 1, METATABLE_CHAN);

	LuaRegistry::pushFrom(l, LuaRegistry::data, channel->data);
	lua_pushvalue(l, 2);
	lua_pushvalue(l, 3);
	lua_settable(l, -3);
	return 0;
}

static int channel_index(lua_State *l) {
	MumbleChannel *channel = luaL_checkclass<MumbleChannel>(l, 1, METATABLE_CHAN);

	LuaRegistry::pushFrom(l, LuaRegistry::data, channel->data);
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
	{"getId", channel_getId},
	{"getID", channel_getId},
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
	{"isEnterRestricted", channel_isEnterRestricted},
	{"canEnter", channel_canEnter},
	{"requestACL", channel_requestACL},
	{"requestPermissions", channel_requestPermissions},
	{"getPermissions", channel_getPermissions},
	{"hasPermission", channel_hasPermission},
	{"hasPermissions", channel_hasPermission},
	{"setListeningVolumeAdjustment", channel_setListeningVolumeAdjustment},
	{"getListeningVolumeAdjustment", channel_getListeningVolumeAdjustment},
	{"requestDescriptionBlob", channel_requestDescriptionBlob},
	{"create", channel_create},
	{"contextAction", channel_contextAction},

	{"__call", channel_call},
	{"__gc", channel_gc},
	{"__tostring", channel_tostring},
	{"__newindex", channel_newindex},
	{"__index", channel_index},
	{NULL, NULL}
};
