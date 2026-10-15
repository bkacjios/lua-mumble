#include "client/client.h"

#include "audio/audiocontext.h"
#include "audio/audiostream.h"
#include "audio/resampler.h"
#include "client/channel.h"
#include "client/target.h"
#include "client/user.h"
#include "defines.h"
#include "gitversion.h"
#include "lua/bindings.h"
#include "lua/luainc.h"
#include "lua/luautil.h"
#include "lua/metatables.h"
#include "lua/registry.h"
#include "net/packet.h"
#include "net/voicepacket.h"
#include "util/buffer.h"
#include "util/log.h"

#include <cstdlib>
#include <cstring>

/*--------------------------------
	MUMBLE CLIENT META METHODS
--------------------------------*/

static void mumble_client_check(lua_State *l, bool connected, const char *status) {
	if (!connected) {
		lua_Debug ar;

		if (!lua_getstack(l, 0, &ar))
			luaL_error(l, "attempt to call method on %s %s", status, METATABLE_CLIENT);

		lua_getinfo(l, "nS", &ar);
		luaL_error(l, "attempt to call %s '%s' on %s %s", ar.namewhat, ar.name, status, METATABLE_CLIENT);
	}
}

static MumbleClient* mumble_client_connecting(lua_State *l, int index) {
	MumbleClient *client = luaL_checkclass<MumbleClient>(l, index, METATABLE_CLIENT);
	mumble_client_check(l, client->connecting, "connecting");
	return client;
}

static MumbleClient* mumble_client_connected(lua_State *l, int index) {
	MumbleClient *client = luaL_checkclass<MumbleClient>(l, index, METATABLE_CLIENT);

	const char* status = NULL;

	if (client->connecting) {
		status = "connecting";
	} else if (!client->connected) {
		status = "disconnected";
	}

	mumble_client_check(l, client->connected, status);
	return client;
}

int mumble_client_new(lua_State *l) {
	// The __call passes our metatable with it
	// Remove it since we don't need it and it messes up the arument positions
	lua_remove(l, 1);

	// Arguments now start at 1
	// Get client version
	lua_Integer major = luaL_optinteger(l, 1, MUMBLE_VERSION_MAJOR);
	lua_Integer minor = luaL_optinteger(l, 2, MUMBLE_VERSION_MINOR);
	lua_Integer patch = luaL_optinteger(l, 3, MUMBLE_VERSION_PATCH);

	MumbleClient *client = lua_newclass<MumbleClient>(l, l, major, minor, patch);
	luaL_getmetatable(l, METATABLE_CLIENT);
	lua_setmetatable(l, -2);

	client->init();
	return 1;
}

static int client_connect(lua_State *l) {
	MumbleClient *client = luaL_checkclass<MumbleClient>(l, 1, METATABLE_CLIENT);
	return client->connect(l);
}

static int client_auth(lua_State *l) {
	MumbleClient *client = mumble_client_connected(l, 1);
	const char *username = luaL_checkstring(l, 2);
	const char *password = lua_isnoneornil(l, 3) ? NULL : luaL_checkstring(l, 3);
	std::vector<const char*> tokens;
	if (!lua_isnoneornil(l, 4)) tokens = luaL_checkstrings(l, 4);
	client->authenticate(username, password, tokens);
	return 0;
}

static int client_setTokens(lua_State *l) {
	MumbleClient *client = mumble_client_connected(l, 1);
	client->setTokens(luaL_checkstrings(l, 2));
	return 0;
}

static int client_disconnect(lua_State *l) {
	MumbleClient *client = mumble_client_connecting(l, 1);
	client->disconnect("connection closed by client", false);
	return 0;
}

static int client_isConnected(lua_State *l) {
	MumbleClient *client = luaL_checkclass<MumbleClient>(l, 1, METATABLE_CLIENT);
	lua_pushboolean(l, client->connected);
	return 1;
}

static int client_isSynced(lua_State *l) {
	MumbleClient *client = luaL_checkclass<MumbleClient>(l, 1, METATABLE_CLIENT);
	lua_pushboolean(l, client->synced);
	return 1;
}

static int client_requestBanList(lua_State *l) {
	mumble_client_connected(l, 1)->requestBanList();
	return 0;
}

static int client_requestUserList(lua_State *l) {
	mumble_client_connected(l, 1)->requestUserList();
	return 0;
}

// The sessions of users passed as a table, or as every argument from index on
std::vector<uint32_t> lua_checksessions(lua_State *l, int index) {
	std::vector<uint32_t> sessions;
	for (MumbleUser *user : luaL_checkobjects<MumbleUser>(l, index, METATABLE_USER)) {
		sessions.push_back(user->session);
	}
	return sessions;
}

// The ids of channels passed as a table, or as every argument from index on
std::vector<uint32_t> lua_checkchannelids(lua_State *l, int index) {
	std::vector<uint32_t> ids;
	for (MumbleChannel *channel : luaL_checkobjects<MumbleChannel>(l, index, METATABLE_CHAN)) {
		ids.push_back(channel->channel_id);
	}
	return ids;
}

static int client_sendPluginData(lua_State *l) {
	MumbleClient *client = mumble_client_connected(l, 1);
	const char *id = luaL_checkstring(l, 2);
	size_t length;
	const char *data = luaL_checklstring(l, 3, &length);
	client->sendPluginData(id, (const uint8_t*) data, length, lua_checksessions(l, 4));
	return 0;
}

static int client_transmit(lua_State *l) {
	MumbleClient *client = mumble_client_connected(l, 1);
	uint8_t codec = (uint8_t) luaL_checkinteger(l, 2);
	size_t length;
	const char *data = luaL_checklstring(l, 3, &length);
	bool speaking = luaL_optboolean(l, 4, true);
	client->transmit(codec, (const uint8_t*) data, length, speaking);
	return 0;
}

static const char *const quality_names[] = {
	"best",    // SRC_SINC_BEST_QUALITY
	"medium",  // SRC_SINC_MEDIUM_QUALITY
	"fastest", // SRC_SINC_FASTEST
	"zero",    // SRC_ZERO_ORDER_HOLD
	"linear",  // SRC_LINEAR
	NULL
};

static const int quality_vals[] = {
	SRC_SINC_BEST_QUALITY,
	SRC_SINC_MEDIUM_QUALITY,
	SRC_SINC_FASTEST,
	SRC_ZERO_ORDER_HOLD,
	SRC_LINEAR
};

static int client_openAudio(lua_State *l) {
	MumbleClient *client = luaL_checkclass<MumbleClient>(l, 1, METATABLE_CLIENT);
	const char *path = luaL_checkstring(l, 2);
	int quality = quality_vals[luaL_checkoption(l, 3, "medium", quality_names)];

	AudioStream *stream = lua_newclass<AudioStream>(l, client);
	std::string error = stream->open(path, quality);
	if (!error.empty()) {
		stream->~AudioStream();
		lua_pop(l, 1);
		lua_pushnil(l);
		lua_pushstring(l, error.c_str());
		return 2;
	}

	luaL_getmetatable(l, METATABLE_AUDIOSTREAM);
	lua_setmetatable(l, -2);
	return 1;
}

static int client_getAudioStreams(lua_State *l) {
	MumbleClient *client = luaL_checkclass<MumbleClient>(l, 1, METATABLE_CLIENT);
	LuaRegistry::push(l, client->audio_streams);
	return 1;
}

static int client_setAudioPacketSize(lua_State *l) {
	MumbleClient *client = luaL_checkclass<MumbleClient>(l, 1, METATABLE_CLIENT);
	int size = luaL_checkinteger(l, 2);
	if (!client->setAudioPacketSize(size)) {
		return luaL_error(l, "invalid value \"%d\" (must be one the following values: 10, 20, 40, 60)", size);
	}
	return 0;
}

static int client_getAudioPacketSize(lua_State *l) {
	MumbleClient *client = luaL_checkclass<MumbleClient>(l, 1, METATABLE_CLIENT);
	lua_pushinteger(l, client->audio_frames);
	return 1;
}

static int client_setVolume(lua_State *l) {
	MumbleClient *client = luaL_checkclass<MumbleClient>(l, 1, METATABLE_CLIENT);
	client->volume = luaL_checknumber(l, 2);
	return 0;
}

static int client_getVolume(lua_State *l) {
	MumbleClient *client = luaL_checkclass<MumbleClient>(l, 1, METATABLE_CLIENT);
	lua_pushnumber(l, client->volume);
	return 1;
}

static int client_setComment(lua_State *l) {
	MumbleClient *client = mumble_client_connected(l, 1);
	client->setComment(luaL_checkstring(l, 2));
	return 0;
}

static int client_hook(lua_State *l) {
	MumbleClient *client = luaL_checkclass<MumbleClient>(l, 1, METATABLE_CLIENT);

	const char* hook = luaL_checkstring(l, 2);
	const char* name = "hook";

	int funcIndex = 3;

	if (lua_isfunction(l, 3) == 0) {
		// If the callback isn't in argument #3, assume that this is the custom hook name instead
		name = lua_tostring(l, 3);
		funcIndex = 4;
	}

	// Check our callback argument is a function
	luaL_checktype(l, funcIndex, LUA_TFUNCTION);

	client->addHook(hook, name, funcIndex);
	return 0;
}

static int client_unhook(lua_State *l) {
	MumbleClient *client = luaL_checkclass<MumbleClient>(l, 1, METATABLE_CLIENT);

	const char *hook = luaL_checkstring(l, 2);
	const char *name = "hook";

	if (!lua_isnoneornil(l, 3) && lua_isstring(l, 3)) {
		// If a custom name for the hook is provided, adjust
		name = lua_tostring(l, 3);
	}

	client->removeHook(hook, name);
	return 0;
}

static int client_call(lua_State *l) {
	MumbleClient *client = luaL_checkclass<MumbleClient>(l, 1, METATABLE_CLIENT);
	const char* hook = luaL_checkstring(l, 2);
	int nargs = lua_gettop(l) - 2;
	return client->hookCallRet(hook, nargs, LUA_MULTRET);
}

static int client_getHooks(lua_State *l) {
	MumbleClient *client = luaL_checkclass<MumbleClient>(l, 1, METATABLE_CLIENT);
	LuaRegistry::push(l, client->hooks);
	return 1;
}

static int client_getUsers(lua_State *l) {
	MumbleClient *client = luaL_checkclass<MumbleClient>(l, 1, METATABLE_CLIENT);
	lua_newtable(l);
	int i = 1;

	for (auto &node : client->user_list) {
		lua_pushinteger(l, i++);
		client->pushUser(node.index);
		lua_settable(l, -3);
	}
	return 1;
}

static int client_getChannels(lua_State *l) {
	MumbleClient *client = luaL_checkclass<MumbleClient>(l, 1, METATABLE_CLIENT);
	lua_newtable(l);
	int i = 1;

	for (auto &node : client->channel_list) {
		lua_pushinteger(l, i++);
		client->pushChannel(node.index);
		lua_settable(l, -3);
	}
	return 1;
}

static int client_getChannel(lua_State *l) {
	MumbleClient *client = mumble_client_connecting(l, 1);
	MumbleChannel *channel = client->findChannel(luaL_optstring(l, 2, "."));
	if (channel == NULL) {
		lua_pushnil(l);
	} else {
		client->pushChannel(channel->channel_id);
	}
	return 1;
}

static int client_registerVoiceTarget(lua_State *l) {
	MumbleClient *client = mumble_client_connected(l, 1);
	uint32_t id = luaL_checkinteger(l, 2);
	std::vector<MumbleProto__VoiceTarget__Target*> targets;
	for (VoiceTarget *target : luaL_checkobjects<VoiceTarget>(l, 3, METATABLE_VOICETARGET)) {
		targets.push_back(&target->target);
	}
	client->registerVoiceTarget(id, targets);
	return 0;
}

static int client_setVoiceTarget(lua_State *l) {
	MumbleClient *client = luaL_checkclass<MumbleClient>(l, 1, METATABLE_CLIENT);
	client->audio_target = luaL_optinteger(l, 2, 0);
	return 0;
}

static int client_getVoiceTarget(lua_State *l) {
	MumbleClient *client = luaL_checkclass<MumbleClient>(l, 1, METATABLE_CLIENT);
	lua_pushinteger(l, client->audio_target);
	return 1;
}

static int client_getEncoder(lua_State *l) {
	MumbleClient *client = mumble_client_connecting(l, 1);
	LuaRegistry::push(l, client->encoder_ref);
	return 1;
}

static int client_getPing(lua_State *l) {
	MumbleClient *client = mumble_client_connecting(l, 1);
	lua_pushnumber(l, client->tcp_ping_avg);
	return 1;
}

static int client_getUpTime(lua_State *l) {
	MumbleClient *client = mumble_client_connecting(l, 1);
	lua_pushnumber(l, client->uptime());
	return 1;
}

static int client_getHost(lua_State *l) {
	MumbleClient *client = mumble_client_connecting(l, 1);
	lua_pushstring(l, client->host);
	return 1;
}

static int client_getPort(lua_State *l) {
	MumbleClient *client = mumble_client_connecting(l, 1);
	lua_pushinteger(l, client->port);
	return 1;
}

static int client_getAddress(lua_State *l) {
	MumbleClient *client = mumble_client_connecting(l, 1);
	lua_pushstring(l, client->serverAddress().c_str());
	return 1;
}

static int client_requestTextureBlob(lua_State *l) {
	MumbleClient *client = mumble_client_connected(l, 1);
	client->requestBlobs(lua_checksessions(l, 2), {}, {});
	return 0;
}

static int client_requestCommentBlob(lua_State *l) {
	MumbleClient *client = mumble_client_connected(l, 1);
	luaL_checktype(l, 2, LUA_TTABLE);
	client->requestBlobs({}, lua_checksessions(l, 2), {});
	return 0;
}

static int client_requestDescriptionBlob(lua_State *l) {
	MumbleClient *client = mumble_client_connected(l, 1);
	luaL_checktype(l, 2, LUA_TTABLE);
	client->requestBlobs({}, {}, lua_checkchannelids(l, 2));
	return 0;
}

static int client_createChannel(lua_State *l) {
	MumbleClient *client = mumble_client_connected(l, 1);
	const char *name = luaL_checkstring(l, 2);
	const char *description = luaL_optstring(l, 3, "");
	int32_t position = luaL_optinteger(l, 4, 0);
	bool temporary = luaL_optboolean(l, 5, false);
	uint32_t max_users = luaL_optinteger(l, 6, 0);
	client->createChannel(0, name, description, position, temporary, max_users);
	return 0;
}

static int client_createAudioBuffer(lua_State *l) {
	MumbleClient *client = luaL_checkclass<MumbleClient>(l, 1, METATABLE_CLIENT);
	opus_int32 samplerate = luaL_optinteger(l, 2, AUDIO_SAMPLE_RATE);
	int channels = luaL_optinteger(l, 3, AUDIO_PLAYBACK_CHANNELS);

	ByteBuffer *buffer = luabuffer_new(l);

	bool failed;
	{
		// Scoped, so the error string is freed before luaL_error can longjmp
		std::string error = client->addAudioBuffer(buffer, samplerate, channels);
		failed = !error.empty();
		if (failed) lua_pushstring(l, error.c_str());
	}
	if (failed) {
		return luaL_error(l, "%s", lua_tostring(l, -1));
	}
	return 1;
}

static int client_getMe(lua_State *l) {
	MumbleClient *client = luaL_checkclass<MumbleClient>(l, 1, METATABLE_CLIENT);
	client->pushUser(client->session);
	return 1;
}

static int client_isTunnelingUDP(lua_State *l) {
	MumbleClient *client = luaL_checkclass<MumbleClient>(l, 1, METATABLE_CLIENT);
	lua_pushboolean(l, client->tcp_udp_tunnel);
	return 1;
}

static int client_isLegacy(lua_State *l) {
	MumbleClient *client = luaL_checkclass<MumbleClient>(l, 1, METATABLE_CLIENT);
	lua_pushboolean(l, client->legacy);
	return 1;
}

static int client_gc(lua_State *l) {
	MumbleClient *client = luaL_checkclass<MumbleClient>(l, 1, METATABLE_CLIENT);
	mumble_log(LOG_DEBUG, "%s: %p garbage collected", METATABLE_CLIENT, client);
	client->destroy();
	client->~MumbleClient();
	return 0;
}

static int client_tostring(lua_State *l) {
	MumbleClient *client = luaL_checkclass<MumbleClient>(l, 1, METATABLE_CLIENT);
	if (client->connecting) {
		lua_pushfstring(l, "%s [%d][\"%s:%d\"] %p", METATABLE_CLIENT, client->self, client->host, client->port, client);
	} else {
		lua_pushfstring(l, "%s: %p", METATABLE_CLIENT, client);
	}
	return 1;
}

static int client_index(lua_State *l) {
	MumbleClient *client = luaL_checkclass<MumbleClient>(l, 1, METATABLE_CLIENT);

	if (strcmp(lua_tostring(l, 2), "me") == 0 && client->session) {
		client->pushUser(client->session);
		return 1;
	} else if (strcmp(lua_tostring(l, 2), "host") == 0 && client->host) {
		lua_pushstring(l, client->host);
		return 1;
	} else if (strcmp(lua_tostring(l, 2), "port") == 0 && client->port) {
		lua_pushinteger(l, client->port);
		return 1;
	}

	lua_getmetatable(l, 1);
	lua_pushvalue(l, 2);
	lua_gettable(l, -2);
	return 1;
}

const luaL_Reg mumble_client[] = {
	{"auth", client_auth},
	{"setTokens", client_setTokens},
	{"connect", client_connect},
	{"disconnect", client_disconnect},
	{"isConnected", client_isConnected},
	{"isSynced", client_isSynced},
	{"isSynched", client_isSynced},
	{"requestBanList", client_requestBanList},
	{"requestUserList", client_requestUserList},
	{"sendPluginData", client_sendPluginData},
	{"transmit", client_transmit},
	{"openAudio", client_openAudio},
	{"getAudioStreams", client_getAudioStreams},
	{"setAudioPacketSize", client_setAudioPacketSize},
	{"getAudioPacketSize", client_getAudioPacketSize},
	{"setComment", client_setComment},
	{"setVolume", client_setVolume},
	{"getVolume", client_getVolume},
	{"hook", client_hook},
	{"unhook", client_unhook},
	{"call", client_call},
	{"getHooks", client_getHooks},
	{"getUsers", client_getUsers},
	{"getChannels", client_getChannels},
	{"getChannel", client_getChannel},
	{"registerVoiceTarget", client_registerVoiceTarget},
	{"setVoiceTarget", client_setVoiceTarget},
	{"getVoiceTarget", client_getVoiceTarget},
	{"getEncoder", client_getEncoder},
	{"getPing", client_getPing},
	{"getUpTime", client_getUpTime},
	{"getHost", client_getHost},
	{"getAddress", client_getAddress},
	{"getPort", client_getPort},
	{"requestTextureBlob", client_requestTextureBlob},
	{"requestCommentBlob", client_requestCommentBlob},
	{"requestDescriptionBlob", client_requestDescriptionBlob},
	{"createChannel", client_createChannel},
	{"createAudioBuffer", client_createAudioBuffer},
	{"getMe", client_getMe},
	{"getSelf", client_getMe},
	{"isTunnelingUDP", client_isTunnelingUDP},
	{"isLegacy", client_isLegacy},
	{"__gc", client_gc},
	{"__tostring", client_tostring},
	{"__index", client_index},
	{NULL, NULL}
};
