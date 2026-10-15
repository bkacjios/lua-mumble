#include "client/user.h"

#include "client/channel.h"
#include "client/client.h"
#include "defines.h"
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
	MUMBLE USER META METHODS
--------------------------------*/

static int user_message(lua_State *l) {
	MumbleUser *user = luaL_checkclass<MumbleUser>(l, 1, METATABLE_USER);
	user->message(luaL_checkstring(l, 2));
	return 0;
}

static int user_kick(lua_State *l) {
	MumbleUser *user = luaL_checkclass<MumbleUser>(l, 1, METATABLE_USER);
	user->kick(lua_isnil(l, 2) ? NULL : luaL_checkstring(l, 2), false);
	return 0;
}

static int user_ban(lua_State *l) {
	MumbleUser *user = luaL_checkclass<MumbleUser>(l, 1, METATABLE_USER);
	user->kick(lua_isnil(l, 2) ? NULL : luaL_checkstring(l, 2), true);
	return 0;
}

static int user_move(lua_State *l) {
	MumbleUser *user = luaL_checkclass<MumbleUser>(l, 1, METATABLE_USER);
	MumbleChannel *channel = luaL_checkclass<MumbleChannel>(l, 2, METATABLE_CHAN);
	user->move(channel->channel_id);
	return 0;
}

static int user_setMuted(lua_State *l) {
	MumbleUser *user = luaL_checkclass<MumbleUser>(l, 1, METATABLE_USER);
	user->setMuted(lua_toboolean(l, 2));
	return 0;
}

static int user_setDeaf(lua_State *l) {
	MumbleUser *user = luaL_checkclass<MumbleUser>(l, 1, METATABLE_USER);
	user->setDeaf(lua_toboolean(l, 2));
	return 0;
}

static int user_register(lua_State *l) {
	MumbleUser *user = luaL_checkclass<MumbleUser>(l, 1, METATABLE_USER);
	user->registerUser();
	return 0;
}

static int user_request_stats(lua_State *l) {
	MumbleUser *user = luaL_checkclass<MumbleUser>(l, 1, METATABLE_USER);
	user->requestStats(lua_toboolean(l, 2));
	return 0;
}

static int user_getClient(lua_State *l) {
	MumbleUser *user = luaL_checkclass<MumbleUser>(l, 1, METATABLE_USER);
	user->client->pushSelf();
	return 1;
}

static int user_getSession(lua_State *l) {
	MumbleUser *user = luaL_checkclass<MumbleUser>(l, 1, METATABLE_USER);
	lua_pushinteger(l, user->session);
	return 1;
}

static int user_getName(lua_State *l) {
	MumbleUser *user = luaL_checkclass<MumbleUser>(l, 1, METATABLE_USER);
	lua_pushoptstring(l, user->name);
	return 1;
}

static int user_getChannel(lua_State *l) {
	MumbleUser *user = luaL_checkclass<MumbleUser>(l, 1, METATABLE_USER);
	user->client->pushChannel(user->channel_id);
	return 1;
}

static int user_getId(lua_State *l) {
	MumbleUser *user = luaL_checkclass<MumbleUser>(l, 1, METATABLE_USER);
	lua_pushinteger(l, user->user_id);
	return 1;
}

static int user_isRegistered(lua_State *l) {
	MumbleUser *user = luaL_checkclass<MumbleUser>(l, 1, METATABLE_USER);
	lua_pushboolean(l, user->user_id > 0);
	return 1;
}

static int user_isMute(lua_State *l) {
	MumbleUser *user = luaL_checkclass<MumbleUser>(l, 1, METATABLE_USER);
	lua_pushboolean(l, user->mute);
	return 1;
}

static int user_isDeaf(lua_State *l) {
	MumbleUser *user = luaL_checkclass<MumbleUser>(l, 1, METATABLE_USER);
	lua_pushboolean(l, user->deaf);
	return 1;
}

static int user_isSelfMute(lua_State *l) {
	MumbleUser *user = luaL_checkclass<MumbleUser>(l, 1, METATABLE_USER);
	lua_pushboolean(l, user->self_mute);
	return 1;
}

static int user_isSelfDeaf(lua_State *l) {
	MumbleUser *user = luaL_checkclass<MumbleUser>(l, 1, METATABLE_USER);
	lua_pushboolean(l, user->self_deaf);
	return 1;
}

static int user_isSuppressed(lua_State *l) {
	MumbleUser *user = luaL_checkclass<MumbleUser>(l, 1, METATABLE_USER);
	lua_pushboolean(l, user->suppress);
	return 1;
}

static int user_getComment(lua_State *l) {
	MumbleUser *user = luaL_checkclass<MumbleUser>(l, 1, METATABLE_USER);
	lua_pushoptstring(l, user->comment);
	return 1;
}

static int user_getCommentHash(lua_State *l) {
	MumbleUser *user = luaL_checkclass<MumbleUser>(l, 1, METATABLE_USER);
	lua_pushstring(l, bin_to_strhex(user->comment_hash).c_str());
	return 1;
}

static int user_isSpeaking(lua_State *l) {
	MumbleUser *user = luaL_checkclass<MumbleUser>(l, 1, METATABLE_USER);
	lua_pushboolean(l, user->speaking);
	return 1;
}

static int user_isRecording(lua_State *l) {
	MumbleUser *user = luaL_checkclass<MumbleUser>(l, 1, METATABLE_USER);
	lua_pushboolean(l, user->recording);
	return 1;
}

static int user_isPrioritySpeaker(lua_State *l) {
	MumbleUser *user = luaL_checkclass<MumbleUser>(l, 1, METATABLE_USER);
	lua_pushboolean(l, user->priority_speaker);
	return 1;
}

static int user_getTexture(lua_State *l) {
	MumbleUser *user = luaL_checkclass<MumbleUser>(l, 1, METATABLE_USER);
	lua_pushoptstring(l, user->texture);
	return 1;
}

static int user_getTextureHash(lua_State *l) {
	MumbleUser *user = luaL_checkclass<MumbleUser>(l, 1, METATABLE_USER);
	lua_pushstring(l, bin_to_strhex(user->texture_hash).c_str());
	return 1;
}

static int user_getHash(lua_State *l) {
	MumbleUser *user = luaL_checkclass<MumbleUser>(l, 1, METATABLE_USER);
	lua_pushoptstring(l, user->hash);
	return 1;
}

static int user_setTexture(lua_State *l) {
	MumbleUser *user = luaL_checkclass<MumbleUser>(l, 1, METATABLE_USER);
	size_t length;
	const char *data = luaL_checklstring(l, 2, &length);
	user->setTexture((const uint8_t*) data, length);
	return 0;
}

static int user_tostring(lua_State *l) {
	MumbleUser *user = luaL_checkclass<MumbleUser>(l, 1, METATABLE_USER);
	lua_pushfstring(l, "%s [%d][\"%s\"] %p", METATABLE_USER, user->session, optcstr(user->name), user);
	return 1;
}

static int user_listen(lua_State *l) {
	MumbleUser *user = luaL_checkclass<MumbleUser>(l, 1, METATABLE_USER);
	user->listen(lua_checkchannelids(l, 2), {});
	return 0;
}

static int user_unlisten(lua_State *l) {
	MumbleUser *user = luaL_checkclass<MumbleUser>(l, 1, METATABLE_USER);
	user->listen({}, lua_checkchannelids(l, 2));
	return 0;
}

static int user_isListening(lua_State *l) {
	MumbleUser *user = luaL_checkclass<MumbleUser>(l, 1, METATABLE_USER);
	MumbleChannel *channel = luaL_checkclass<MumbleChannel>(l, 2, METATABLE_CHAN);
	lua_pushboolean(l, user->isListening(channel->channel_id));
	return 1;
}

static int user_getListens(lua_State *l) {
	MumbleUser *user = luaL_checkclass<MumbleUser>(l, 1, METATABLE_USER);

	lua_newtable(l);

	// Add all linked channels to the table
	for (auto &node : user->listens) {
		lua_pushinteger(l, node.index);
		user->client->pushChannel(node.index);
		lua_settable(l, -3);
	}

	return 1;
}

static int user_sendPluginData(lua_State *l) {
	MumbleUser *user = luaL_checkclass<MumbleUser>(l, 1, METATABLE_USER);
	const char *id = luaL_checkstring(l, 2);
	size_t length;
	const char *data = luaL_checklstring(l, 3, &length);
	user->sendPluginData(id, (const uint8_t*) data, length);
	return 0;
}

static int user_requestTextureBlob(lua_State *l) {
	MumbleUser *user = luaL_checkclass<MumbleUser>(l, 1, METATABLE_USER);
	user->requestTextureBlob();
	return 0;
}

static int user_requestCommentBlob(lua_State *l) {
	MumbleUser *user = luaL_checkclass<MumbleUser>(l, 1, METATABLE_USER);
	user->requestCommentBlob();
	return 0;
}

static int user_startRecord(lua_State *l) {
	MumbleUser *user = luaL_checkclass<MumbleUser>(l, 1, METATABLE_USER);
	const char *path = user->recording_file ? NULL : luaL_checkstring(l, 2);
	std::string error = user->startRecording(path);
	if (!error.empty()) {
		lua_pushnil(l);
		lua_pushstring(l, error.c_str());
		return 2;
	}
	lua_pushboolean(l, true);
	return 1;
}

static int user_stopRecord(lua_State *l) {
	MumbleUser *user = luaL_checkclass<MumbleUser>(l, 1, METATABLE_USER);

	lua_pushboolean(l, user->stopRecording());
	return 1;
}

static int user_isBeingRecorded(lua_State *l) {
	MumbleUser *user = luaL_checkclass<MumbleUser>(l, 1, METATABLE_USER);
	lua_pushboolean(l, user->recording_file != NULL);
	return 1;
}

static int user_contextAction(lua_State *l) {
	MumbleUser *user = luaL_checkclass<MumbleUser>(l, 1, METATABLE_USER);
	user->contextAction(luaL_checkstring(l, 2));
	return 0;
}

static int user_gc(lua_State *l) {
	MumbleUser *user = luaL_checkclass<MumbleUser>(l, 1, METATABLE_USER);
	mumble_log(LOG_DEBUG, "%s: %p garbage collected", METATABLE_USER, user);
	LuaRegistry::unrefIn(l, LuaRegistry::data, &user->data);
	user->~MumbleUser();
	return 0;
}

static int user_newindex(lua_State *l) {
	MumbleUser *user = luaL_checkclass<MumbleUser>(l, 1, METATABLE_USER);

	LuaRegistry::pushFrom(l, LuaRegistry::data, user->data);
	lua_pushvalue(l, 2);
	lua_pushvalue(l, 3);
	lua_settable(l, -3);
	return 0;
}

static int user_index(lua_State *l) {
	MumbleUser *user = luaL_checkclass<MumbleUser>(l, 1, METATABLE_USER);

	LuaRegistry::pushFrom(l, LuaRegistry::data, user->data);
	lua_pushvalue(l, 2);
	lua_gettable(l, -2);

	if (lua_isnil(l, -1)) {
		lua_getmetatable(l, 1);
		lua_pushvalue(l, 2);
		lua_gettable(l, -2);
	}
	return 1;
}

const luaL_Reg mumble_user[] = {
	{"message", user_message},
	{"kick", user_kick},
	{"ban", user_ban},
	{"move", user_move},
	{"setMuted", user_setMuted},
	{"setDeaf", user_setDeaf},
	{"register", user_register},
	{"requestStats", user_request_stats},

	{"getClient", user_getClient},
	{"getSession", user_getSession},
	{"getName", user_getName},
	{"getChannel", user_getChannel},
	{"getId", user_getId},
	{"getID", user_getId},
	{"isRegistered", user_isRegistered},
	{"isMute", user_isMute},
	{"isMuted", user_isMute},
	{"isDeaf", user_isDeaf},
	{"isDeafened", user_isDeaf},
	{"isSelfMute", user_isSelfMute},
	{"isSelfMuted", user_isSelfMute},
	{"isSelfDeaf", user_isSelfDeaf},
	{"isSelfDeafened", user_isSelfDeaf},
	{"isSuppressed", user_isSuppressed},
	{"getComment", user_getComment},
	{"getCommentHash", user_getCommentHash},
	{"isSpeaking", user_isSpeaking},
	{"isRecording", user_isRecording},
	{"isPrioritySpeaker", user_isPrioritySpeaker},
	{"getTexture", user_getTexture},
	{"getTextureHash", user_getTextureHash},
	{"getHash", user_getHash},
	{"setTexture", user_setTexture},
	{"listen", user_listen},
	{"unlisten", user_unlisten},
	{"isListening", user_isListening},
	{"getListens", user_getListens},
	{"getListening", user_getListens},
	{"sendPluginData", user_sendPluginData},
	{"requestTextureBlob", user_requestTextureBlob},
	{"requestCommentBlob", user_requestCommentBlob},
	{"startRecord", user_startRecord},
	{"startRecording", user_startRecord},
	{"stopRecord", user_stopRecord},
	{"stopRecording", user_stopRecord},
	{"isBeingRecorded", user_isBeingRecorded},
	{"contextAction", user_contextAction},

	{"__gc", user_gc},
	{"__tostring", user_tostring},
	{"__newindex", user_newindex},
	{"__index", user_index},
	{NULL, NULL}
};
