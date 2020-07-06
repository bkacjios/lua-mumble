#include "mumble.h"

#include "audio.h"
#include "client.h"
#include "channel.h"
#include "packet.h"
#include "target.h"

/*--------------------------------
	MUMBLE CLIENT META METHODS
--------------------------------*/

static int client_auth(lua_State *l)
{
	MumbleClient *client = luaL_checkudata(l, 1, METATABLE_CLIENT);

	MumbleProto__Version version = MUMBLE_PROTO__VERSION__INIT;
	MumbleProto__Authenticate auth = MUMBLE_PROTO__AUTHENTICATE__INIT;

	auth.has_opus = true;
	auth.opus = true;
	auth.username = (char*) luaL_checkstring(l, 2);
	auth.n_tokens = 0;

	if (lua_isnoneornil(l, 3) == 0) {
		auth.password = (char*) luaL_checkstring(l, 3);
	}

	if (lua_isnoneornil(l, 4) == 0) {
		luaL_checktype(l, 4, LUA_TTABLE);

		lua_pushvalue(l, 4);
		lua_pushnil(l);

		int i = 0;
		int len = lua_objlen(l, 4);

		auth.tokens = malloc(sizeof(char*) * len);
		auth.n_tokens = len;

		while (lua_next(l, -2)) {
			lua_pushvalue(l, -2);
			if (i < len) {
				char *value = (char*) lua_tostring(l, -2);
				auth.tokens[i++] = value;
			}
			lua_pop(l, 2);
		}
		lua_pop(l, 1);
	}

	struct utsname unameData;
	uname(&unameData);

	version.has_version = true;
	version.version = 1 << 16 | 2 << 8 | 8;
	version.release = MODULE_NAME " " MODULE_VERSION;
	version.os = unameData.sysname;
	version.os_version = unameData.release;

	packet_send(client, PACKET_VERSION, &version);
	packet_send(client, PACKET_AUTHENTICATE, &auth);

	if (auth.tokens != NULL)
		free(auth.tokens);

	return 0;
}

static int client_disconnect(lua_State *l)
{
	MumbleClient *client = luaL_checkudata(l, 1, METATABLE_CLIENT);
	mumble_disconnect(l, client);
	return 0;
}

static int client_isConnected(lua_State *l)
{
	MumbleClient *client = luaL_checkudata(l, 1, METATABLE_CLIENT);
	lua_pushboolean(l, client->connected);
	return 1;
}

static int client_isSynced(lua_State *l)
{
	MumbleClient *client = luaL_checkudata(l, 1, METATABLE_CLIENT);
	lua_pushboolean(l, client->synced);
	return 1;
}

static int client_sendPluginData(lua_State *l)
{
	MumbleClient *client = luaL_checkudata(l, 1, METATABLE_CLIENT);

	MumbleProto__PluginDataTransmission data = MUMBLE_PROTO__PLUGIN_DATA_TRANSMISSION__INIT;

	data.has_sendersession = true;
	data.sendersession = client->session;

	data.dataid = (char*) luaL_checkstring(l, 2);

	ProtobufCBinaryData cbdata;
	cbdata.data = (uint8_t*) luaL_checklstring(l, 3, &cbdata.len);

	data.has_data = true;
	data.data = cbdata;

	luaL_checktype(l, 4, LUA_TTABLE);
	lua_pushvalue(l, 4);
	lua_pushnil(l);

	int i = 0;
	int len = lua_objlen(l, 4);

	data.receiversessions = malloc(sizeof(uint32_t) * len);
	data.n_receiversessions = len;

	while (lua_next(l, -2)) {
		lua_pushvalue(l, -2);
		if (i < len) {
			// Allow a table of users or session IDs
			if (lua_isuserdata(l, -2)) {
				MumbleUser *user = lua_touserdata(l, -2);
				data.receiversessions[i++] = user->session;
			} else if (lua_isnumber(l, -2)) {
				uint32_t session = (uint32_t) lua_tonumber(l, -2);
				data.receiversessions[i++] = session;
			}
		}
		lua_pop(l, 2);
	}
	lua_pop(l, 1);

	packet_send(client, PACKET_PLUGINDATA, &data);

	free(data.receiversessions);
	return 0;
}

static int client_transmit(lua_State *l) {
	MumbleClient *client = luaL_checkudata(l, 1, METATABLE_CLIENT);

	uint8_t codec = (uint8_t) luaL_checkinteger(l, 2);

	size_t outputlen;
	uint8_t* output = (uint8_t*) luaL_checklstring(l, 3, &outputlen);

	bool speaking = luaL_optboolean(l, 4, true);

	uint8_t packet_buffer[PAYLOAD_SIZE_MAX];

	uint16_t frame_header = outputlen;
	if (codec == UDP_OPUS && !speaking) {
		// Set 14th bit to 1 to signal end of stream.
		frame_header = (1 << 13) | outputlen;
	} else if ((codec == UDP_SPEEX || codec == UDP_CELT_ALPHA || codec == UDP_CELT_BETA) && speaking) {
		// Set continuation bit at the MSB if we are not at the end of the stream
		frame_header = (1 << 0) | outputlen;
	}

	VoicePacket packet;
	voicepacket_init(&packet, packet_buffer);
	voicepacket_setheader(&packet, codec, client->audio_target, client->audio_sequence);
	voicepacket_setframe(&packet, codec, frame_header, output);
	packet_sendex(client, PACKET_UDPTUNNEL, packet_buffer, voicepacket_getlength(&packet));

	client->audio_sequence = (client->audio_sequence + 1) % 100000;
	return 0;
}

static int client_play(lua_State *l)
{
	MumbleClient *client 	= luaL_checkudata(l, 1, METATABLE_CLIENT);
	const char* filepath	= luaL_checkstring(l, 2);
	float volume			= (float) luaL_optnumber(l, 3, 1);
	int channel				= luaL_optinteger(l, 4, 1);

	if (channel < 1)
		channel = 1;

	if (channel > AUDIO_MAX_CHANNELS)
		channel = AUDIO_MAX_CHANNELS;

	audio_transmission_stop(client->audio_jobs[channel - 1]);

	//AudioTransmission *sound = lua_newuserdata(l, sizeof(AudioTransmission));
	//luaL_getmetatable(l, METATABLE_AUDIO);
	//lua_setmetatable(l, -2);

	AudioTransmission *sound = malloc(sizeof(AudioTransmission));

	sound->playing = true;
	sound->client = client;
	sound->volume = volume;

	int error;
	sound->ogg = stb_vorbis_open_filename(filepath, &error, NULL);

	if (error != VORBIS__no_error) {
		lua_pushnil(l);
		lua_pushfstring(l, "error opening file %s (%d)", filepath, error);
		return 2;
	}

	sound->info = stb_vorbis_get_info(sound->ogg);

	/*if (sound->info.sample_rate != AUDIO_SAMPLE_RATE) {
		lua_pushnil(l);
		lua_pushfstring(l, "audio has invalid sample rate (%d expected, got %d)", AUDIO_SAMPLE_RATE, sound->info.sample_rate);
		return 2;
	}*/

	client->audio_jobs[channel - 1] = sound;

	lua_pushboolean(l, true);
	return 1;
}

static int client_isPlaying(lua_State *l)
{
	MumbleClient *client 	= luaL_checkudata(l, 1, METATABLE_CLIENT);
	int channel = luaL_optinteger(l, 1, 1);
	lua_pushboolean(l, client->audio_jobs[channel - 1] != NULL);
	return 1;
}

static int client_stopPlaying(lua_State *l)
{
	MumbleClient *client 	= luaL_checkudata(l, 1, METATABLE_CLIENT);
	int channel				= luaL_optinteger(l, 2, 1);

	audio_transmission_stop(client->audio_jobs[channel - 1]);
	return 1;
}

static int client_setVolume(lua_State *l)
{
	MumbleClient *client 	= luaL_checkudata(l, 1, METATABLE_CLIENT);
	client->volume = luaL_checknumber(l, 2);
	return 0;
}

static int client_getVolume(lua_State *l)
{
	MumbleClient *client 	= luaL_checkudata(l, 1, METATABLE_CLIENT);
	lua_pushnumber(l, client->volume);
	return 1;
}

static int client_setComment(lua_State *l)
{
	MumbleClient *client = luaL_checkudata(l, 1, METATABLE_CLIENT);
	
	MumbleProto__UserState msg = MUMBLE_PROTO__USER_STATE__INIT;

	msg.has_session = true;
	msg.session = client->session;

	msg.comment = (char*) luaL_checkstring(l, 2);

	packet_send(client, PACKET_USERSTATE, &msg);
	return 0;
}

static int client_hook(lua_State *l)
{
	MumbleClient *client = luaL_checkudata(l, 1, METATABLE_CLIENT);

	const char* hook = luaL_checkstring(l,2);
	const char* name = "hook";

	int funcIndex = 3;

	if (lua_isfunction(l, 3) == 0) {
		hook = luaL_checkstring(l,2);
		name = lua_tostring(l,3);
		funcIndex = 4;
	}

	luaL_checktype(l, funcIndex, LUA_TFUNCTION);

	lua_rawgeti(l, LUA_REGISTRYINDEX, client->hooks);
	lua_getfield(l, -1, hook);

	if (lua_istable(l, -1) == 0) {
		lua_pop(l, 1);
		lua_newtable(l);
		lua_setfield(l, -2, hook);
		lua_getfield(l, -1, hook);
	}

	lua_pushvalue(l, funcIndex);
	lua_setfield(l, -2, name);

	return 0;
}

static int client_call(lua_State *l)
{
	MumbleClient *client = luaL_checkudata(l, 1, METATABLE_CLIENT);
	const char* hook = luaL_checkstring(l, 2);
	int nargs = lua_gettop(l) - 2;
	mumble_hook_call(l, client, hook, nargs);
	return 0;
}

static int client_getHooks(lua_State *l)
{
	MumbleClient *client = luaL_checkudata(l, 1, METATABLE_CLIENT);
	lua_rawgeti(l, LUA_REGISTRYINDEX, client->hooks);
	return 1;
}

static int client_getUsers(lua_State *l)
{
	MumbleClient *client = luaL_checkudata(l, 1, METATABLE_CLIENT);
	lua_rawgeti(l, LUA_REGISTRYINDEX, client->users);
	return 1;
}

static int client_getChannels(lua_State *l)
{
	MumbleClient *client = luaL_checkudata(l, 1, METATABLE_CLIENT);
	lua_rawgeti(l, LUA_REGISTRYINDEX, client->channels);
	return 1;
}

static int client_getChannel(lua_State *l)
{
	MumbleClient *client = luaL_checkudata(l, 1, METATABLE_CLIENT);
	//char* path = (char*) luaL_checkstring(l, 2);

	lua_rawgeti(l, LUA_REGISTRYINDEX, client->channels);
	lua_pushinteger(l, 0);
	lua_gettable(l, -2);
	lua_remove(l, -2);

	if (lua_isnoneornil(l, -1) == 0) {
		lua_replace(l, 1);
		return channel_call(l);
	}

	return 0;
}

static int client_registerVoiceTarget(lua_State *l)
{
	MumbleClient *client = luaL_checkudata(l, 1, METATABLE_CLIENT);

	MumbleProto__VoiceTarget msg = MUMBLE_PROTO__VOICE_TARGET__INIT;

	msg.has_id = true;
	msg.id = luaL_optinteger(l, 2, 0);

	int n_targets = lua_gettop(l) - 2;

	msg.n_targets = n_targets;
	msg.targets = malloc(sizeof(MumbleProto__VoiceTarget__Target) * n_targets);

	for (int i=0; i < n_targets; i++) {
		msg.targets[i] = luaL_checkudata(l, i + 3, METATABLE_VOICETARGET);
	}

	packet_send(client, PACKET_VOICETARGET, &msg);

	free(msg.targets);
	return 0;
}

static int client_setVoiceTarget(lua_State *l)
{
	MumbleClient *client = luaL_checkudata(l, 1, METATABLE_CLIENT);
	client->audio_target = luaL_optinteger(l, 2, 0);
	return 0;
}

static int client_getVoiceTarget(lua_State *l)
{
	MumbleClient *client = luaL_checkudata(l, 1, METATABLE_CLIENT);
	lua_pushinteger(l, client->audio_target);
	return 1;
}

static int client_setBitrate(lua_State *l)
{
	MumbleClient *client = luaL_checkudata(l, 1, METATABLE_CLIENT);
	opus_encoder_ctl(client->encoder, OPUS_SET_BITRATE(luaL_checkinteger(l, 2)));
	return 1;
}

static int client_getBitrate(lua_State *l)
{
	MumbleClient *client = luaL_checkudata(l, 1, METATABLE_CLIENT);
	int bitrate;
	opus_encoder_ctl(client->encoder, OPUS_GET_BITRATE(&bitrate));
	lua_pushinteger(l, bitrate);
	return 1;
}

static int client_getPing(lua_State *l)
{
	MumbleClient *client = luaL_checkudata(l, 1, METATABLE_CLIENT);
	lua_pushnumber(l, client->tcp_ping_avg);
	return 1;
}

static int client_getUpTime(lua_State *l)
{
	MumbleClient *client = luaL_checkudata(l, 1, METATABLE_CLIENT);
	lua_pushnumber(l, gettime() - client->time);
	return 1;
}

static int client_gc(lua_State *l)
{
	MumbleClient *client = luaL_checkudata(l, 1, METATABLE_CLIENT);

	mumble_disconnect(l, client);

	luaL_unref(l, LUA_REGISTRYINDEX, client->self);
	luaL_unref(l, LUA_REGISTRYINDEX, client->hooks);
	luaL_unref(l, LUA_REGISTRYINDEX, client->users);
	luaL_unref(l, LUA_REGISTRYINDEX, client->channels);

	opus_encoder_destroy(client->encoder);
	return 0;
}

static int client_tostring(lua_State *l)
{
	MumbleClient *client = luaL_checkudata(l, 1, METATABLE_CLIENT);
	lua_pushfstring(l, "%s: %p", METATABLE_CLIENT, client);
	return 1;
}

static int client_index(lua_State *l)
{
	MumbleClient *client = luaL_checkudata(l, 1, METATABLE_CLIENT);

	if (strcmp(lua_tostring(l, 2), "me") == 0 && client->session) {
		mumble_user_raw_get(l, client, client->session);
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
	{"disconnect", client_disconnect},
	{"isConnected", client_isConnected},
	{"isSynced", client_isSynced},
	{"sendPluginData", client_sendPluginData},
	{"transmit", client_transmit},
	{"play", client_play},
	{"isPlaying", client_isPlaying},
	{"stopPlaying", client_stopPlaying},
	{"setComment", client_setComment},
	{"setVolume", client_setVolume},
	{"getVolume", client_getVolume},
	{"hook", client_hook},
	{"call", client_call},
	{"getHooks", client_getHooks},
	{"getUsers", client_getUsers},
	{"getChannels", client_getChannels},
	{"getChannel", client_getChannel},
	{"registerVoiceTarget", client_registerVoiceTarget},
	{"setVoiceTarget", client_setVoiceTarget},
	{"getVoiceTarget", client_getVoiceTarget},
	{"setBitrate", client_setBitrate},
	{"getBitrate", client_getBitrate},
	{"getPing", client_getPing},
	{"getUpTime", client_getUpTime},
	{"__gc", client_gc},
	{"__tostring", client_tostring},
	{"__index", client_index},
	{NULL, NULL}
};