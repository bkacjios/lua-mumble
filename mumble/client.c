#include "mumble.h"

#include "audio.h"
#include "client.h"
#include "channel.h"
#include "packet.h"
#include "target.h"
#include "user.h"

#include "gitversion.h"

/*--------------------------------
	MUMBLE CLIENT META METHODS
--------------------------------*/

static void mumble_client_check(lua_State *l, bool connected, const char *status) {
	if (!connected) {
		lua_Debug ar;

		if (!lua_getstack(l, 0, &ar))
			luaL_error(l, "attempt to call method on %s mumble.client", status);

		lua_getinfo(l, "nS", &ar);
		luaL_error(l, "attempt to call %s '%s' on %s mumble.client", ar.namewhat, ar.name, status);
	}
}

static MumbleClient* mumble_client_connecting(lua_State *l, int index)
{
	MumbleClient *client = luaL_checkudata(l, index, METATABLE_CLIENT);
	mumble_client_check(l, client->connecting, "connecting");
	return client;
}

static MumbleClient* mumble_client_connected(lua_State *l, int index)
{
	MumbleClient *client = luaL_checkudata(l, index, METATABLE_CLIENT);

	const char* status;

	if (client->connecting) {
		status = "connecting";
	} else if (!client->connected) {
		status = "disconnected";
	}

	mumble_client_check(l, client->connected, status);
	return client;
}

static int client_auth(lua_State *l)
{
	MumbleClient *client = mumble_client_connected(l, 1);

	MumbleProto__Version version = MUMBLE_PROTO__VERSION__INIT;
	MumbleProto__Authenticate auth = MUMBLE_PROTO__AUTHENTICATE__INIT;

	auth.has_opus = true;
	auth.opus = true;
	auth.username = (char*) luaL_checkstring(l, 2);
	auth.n_tokens = 0;
	auth.has_client_type = true;
	auth.client_type = CLIENT_TYPE_BOT;

	if (lua_isnoneornil(l, 3) == 0) {
		auth.password = (char*) luaL_checkstring(l, 3);
	}

	if (lua_isnoneornil(l, 4) == 0) {
		luaL_checktype(l, 4, LUA_TTABLE);

		lua_pushvalue(l, 4);
		lua_pushnil(l);

		int i = 0;
		auth.n_tokens = lua_objlen(l, 4);
		auth.tokens = malloc(sizeof(char*) * auth.n_tokens);
		
		if (auth.tokens == NULL)
			return luaL_error(l, "failed to malloc: %s", strerror(errno));

		while (lua_next(l, -2)) {
			if (i < auth.n_tokens) {
				char *value = (char*) lua_tostring(l, -1);
				auth.tokens[i++] = value;
			}
			lua_pop(l, 1);
		}
		lua_pop(l, 1);
	}

	struct utsname unameData;
	uname(&unameData);

	version.has_version_v1 = true;
	version.version_v1 = MUMBLE_VERSION_V1;
	version.has_version_v2 = true;
	version.version_v2 = MUMBLE_VERSION_V2;
	version.release = MODULE_NAME " " GIT_VERSION;
	version.os = unameData.sysname;
	version.os_version = unameData.release;

	packet_send(client, PACKET_VERSION, &version);
	packet_send(client, PACKET_AUTHENTICATE, &auth);

	if (auth.tokens != NULL)
		free(auth.tokens);

	return 0;
}

static int client_setTokens(lua_State *l)
{
	MumbleClient *client = mumble_client_connected(l, 1);

	MumbleProto__Authenticate auth = MUMBLE_PROTO__AUTHENTICATE__INIT;

	luaL_checktype(l, 2, LUA_TTABLE);

	lua_pushvalue(l, 2);
	lua_pushnil(l);

	int i = 0;
	auth.n_tokens = lua_objlen(l, 2);
	auth.tokens = malloc(sizeof(char*) * auth.n_tokens);
	
	if (auth.tokens == NULL)
		return luaL_error(l, "failed to malloc: %s", strerror(errno));

	while (lua_next(l, -2)) {
		if (i < auth.n_tokens) {
			char *value = (char*) lua_tostring(l, -1);
			auth.tokens[i++] = value;
		}
		lua_pop(l, 1);
	}
	lua_pop(l, 1);

	packet_send(client, PACKET_AUTHENTICATE, &auth);
	return 0;
}

static int client_disconnect(lua_State *l)
{
	MumbleClient *client = mumble_client_connecting(l, 1);
	mumble_disconnect(l, client, "connection closed by client", false);
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

static int client_requestBanList(lua_State *l)
{
	MumbleClient *client = mumble_client_connected(l, 1);

	MumbleProto__BanList list = MUMBLE_PROTO__BAN_LIST__INIT;
	list.has_query = true;
	list.query = true;
	packet_send(client, PACKET_BANLIST, &list);
	return 0;
}

static int client_requestUserList(lua_State *l)
{
	MumbleClient *client = mumble_client_connected(l, 1);

	MumbleProto__UserList list = MUMBLE_PROTO__USER_LIST__INIT;
	packet_send(client, PACKET_USERLIST, &list);
	return 0;
}

static int client_sendPluginData(lua_State *l)
{
	MumbleClient *client = mumble_client_connected(l, 1);

	// Initialize the data to be sent
	MumbleProto__PluginDataTransmission data = MUMBLE_PROTO__PLUGIN_DATA_TRANSMISSION__INIT;
	data.has_sendersession = true;
	data.sendersession = client->session;
	data.dataid = (char*) luaL_checkstring(l, 2);

	ProtobufCBinaryData cbdata;
	cbdata.data = (uint8_t*) luaL_checklstring(l, 3, &cbdata.len);
	data.has_data = true;
	data.data = cbdata;

	int n_receiversessions = 0;
	uint32_t *receiversessions = NULL;

	// Check if the 4th argument is a table (a list of userdata)
	if (lua_istable(l, 4)) {
		n_receiversessions = lua_objlen(l, 4);  // Get the length of the table
		receiversessions = malloc(sizeof(uint32_t) * n_receiversessions);
		if (!receiversessions)
			return luaL_error(l, "failed to malloc: %s", strerror(errno));

		// Iterate over the table and extract sessions
		lua_pushvalue(l, 4); // Push the table onto the stack
		lua_pushnil(l);
		int i = 0;
		while (lua_next(l, -2)) { // Start iterating over the table
			if (luaL_isudata(l, -1, METATABLE_USER)) {
				// If userdata is of the expected type, extract the session
				MumbleUser *user = lua_touserdata(l, -1);
				receiversessions[i++] = user->session;
			} else {
				// If not userdata of the expected type, return an error
				free(receiversessions);
				return luaL_typerror_table(l, 4, -2, -1, METATABLE_USER);
			}
			lua_pop(l, 1);  // Pop the value, keep the key
		}
		lua_pop(l, 1);  // Pop the table
	} else {
		// Otherwise, process varargs (userdata arguments)
		int top = lua_gettop(l);
		n_receiversessions = top - 3;  // Subtract first 3 parameters
		receiversessions = malloc(sizeof(uint32_t) * n_receiversessions);
		if (!receiversessions)
			return luaL_error(l, "failed to malloc: %s", strerror(errno));

		// Extract session info from each userdata vararg
		for (int i = 0; i < n_receiversessions; ++i) {
			if (luaL_isudata(l, 4 + i, METATABLE_USER)) {
				MumbleUser *user = lua_touserdata(l, 4 + i);
				receiversessions[i] = user->session;
			} else {
				free(receiversessions);
				return luaL_typerror(l, 4 + i, METATABLE_USER);
			}
		}
	}

	// Fill in the receiver session data
	data.n_receiversessions = n_receiversessions;
	data.receiversessions = receiversessions;

	// Send the data packet
	packet_send(client, PACKET_PLUGINDATA, &data);
	free(receiversessions);
	return 0;
}

static int client_transmit(lua_State *l) {
	MumbleClient *client = mumble_client_connected(l, 1);

	uint8_t codec = (uint8_t) luaL_checkinteger(l, 2);

	size_t outputlen;
	uint8_t* output = (uint8_t*) luaL_checklstring(l, 3, &outputlen);

	bool speaking = luaL_optboolean(l, 4, true);

	if (client->legacy) {
		uint16_t frame_header = outputlen;
		if (codec == LEGACY_UDP_OPUS && !speaking) {
			// Set 14th bit to 1 to signal end of stream.
			frame_header = (1 << 13) | outputlen;
		} else if ((codec == LEGACY_UDP_SPEEX || codec == LEGACY_UDP_CELT_ALPHA || codec == LEGACY_UDP_CELT_BETA) && speaking) {
			// Set continuation bit at the MSB if we are not at the end of the stream
			frame_header = (1 << 0) | outputlen;
		}

		VoicePacket packet;
		uint8_t packet_buffer[PAYLOAD_SIZE_MAX];
		voicepacket_init(&packet, packet_buffer);
		voicepacket_setheader(&packet, codec, client->audio_target, client->audio_sequence);
		voicepacket_setframe(&packet, codec, frame_header, output, outputlen);

		mumble_handle_speaking_hooks_legacy(l, client, packet.buffer + 1, codec, client->audio_target, client->session);

		if (client->tcp_udp_tunnel) {
			packet_sendex(client, PACKET_UDPTUNNEL, packet_buffer, NULL, voicepacket_getlength(&packet));
		} else {
			packet_sendudp(client, packet_buffer, voicepacket_getlength(&packet));
		}
	} else {
		MumbleUDP__Audio audio = MUMBLE_UDP__AUDIO__INIT;

		ProtobufCBinaryData audio_data;
		audio_data.data = output;
		audio_data.len = outputlen;

		audio.frame_number = client->audio_sequence;
		audio.opus_data = audio_data;

		audio.is_terminator = !speaking;

		audio.target = client->audio_target;

		audio.n_positional_data = 0;

		uint8_t packet_buffer[PAYLOAD_SIZE_MAX];
		packet_buffer[0] = PROTO_UDP_AUDIO;

		int len = 1 + mumble_udp__audio__pack(&audio, packet_buffer + 1);

		mumble_handle_speaking_hooks_protobuf(l, client, &audio, client->session);

		if (client->tcp_udp_tunnel) {
			packet_sendex(client, PACKET_UDPTUNNEL, packet_buffer, NULL, len);
		} else {
			packet_sendudp(client, packet_buffer, len);
		}
	}

	client->audio_sequence++;
	return 0;
}

static int client_openAudio(lua_State *l)
{
	MumbleClient *client = luaL_checkudata(l, 1, METATABLE_CLIENT);
	const char* filepath	= luaL_checkstring(l, 2);
	float volume			= (float) luaL_optnumber(l, 3, 1);

	AudioStream *sound = lua_newuserdata(l, sizeof(AudioStream));
	luaL_getmetatable(l, METATABLE_AUDIOSTREAM);
	lua_setmetatable(l, -2);

	sound->file = sf_open(filepath, SFM_READ, &sound->info);;
	sound->client = client;
	sound->playing = false;
	sound->looping = false;
	sound->refrence = -1;
	sound->loop_count = 0;
	sound->volume = volume;

	if (!sound->file) {
		lua_pushnil(l);
		lua_pushfstring(l, "error opening file \"%s\" (%s)", filepath, sf_strerror(NULL));
		return 2;
	}

	return 1;
}

static int client_getAudioStreams(lua_State *l)
{
	MumbleClient *client = luaL_checkudata(l, 1, METATABLE_CLIENT);
	mumble_pushref(l, client->audio_streams);
	return 1;
}

static int client_setAudioPacketSize(lua_State *l) {
	MumbleClient *client = luaL_checkudata(l, 1, METATABLE_CLIENT);

	int size = luaL_checkinteger(l, 2);

	int frames;

	switch (size) {
		case AUDIO_FRAME_SIZE_TINY:
			frames = AUDIO_FRAME_SIZE_TINY;
			break;
		case AUDIO_FRAME_SIZE_SMALL:
			frames = AUDIO_FRAME_SIZE_SMALL;
			break;
		case AUDIO_FRAME_SIZE_MEDIUM:
			frames = AUDIO_FRAME_SIZE_MEDIUM;
			break;
		case AUDIO_FRAME_SIZE_LARGE:
			frames = AUDIO_FRAME_SIZE_LARGE;
			break;
		default:
			return luaL_error(l, "invalid value \"%d\" (must be one the following values: 10, 20, 40, 60)", size);
	}

	client->audio_frames = frames;
	uv_timer_stop(&client->audio_timer);

	if (client->connected) {
		uv_timer_stop(&client->audio_timer);
		uv_timer_start(&client->audio_timer, mumble_audio_timer, frames, frames);
	}
	return 0;
}

static int client_getAudioPacketSize(lua_State *l) {
	MumbleClient *client = luaL_checkudata(l, 1, METATABLE_CLIENT);
	lua_pushinteger(l, client->audio_frames);
	return 1;
}

static int client_setVolume(lua_State *l)
{
	MumbleClient *client = luaL_checkudata(l, 1, METATABLE_CLIENT);
	client->volume = luaL_checknumber(l, 2);
	return 0;
}

static int client_getVolume(lua_State *l)
{
	MumbleClient *client = luaL_checkudata(l, 1, METATABLE_CLIENT);
	lua_pushnumber(l, client->volume);
	return 1;
}

static int client_setComment(lua_State *l)
{
	MumbleClient *client = mumble_client_connected(l, 1);
	
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
		// If the callback isn't in argument #3, assume that this is the custom hook name instead
		hook = luaL_checkstring(l,2);
		name = lua_tostring(l,3);
		funcIndex = 4;
	}

	// Check our callback argument is a function
	luaL_checktype(l, funcIndex, LUA_TFUNCTION);

	// Push our reference for the hooks table
	mumble_pushref(l, client->hooks);

	// Get the callback table for this hook
	lua_getfield(l, -1, hook);

	if (!lua_istable(l, -1)) {
		// No callback table exists, create one
		lua_pop(l, 1);
		lua_newtable(l);
		lua_setfield(l, -2, hook);
		lua_getfield(l, -1, hook);
	}

	// Push our callback value and add it to our callback table
	lua_pushvalue(l, funcIndex);
	lua_setfield(l, -2, name);

	return 0;
}

static int client_unhook(lua_State *l)
{
	MumbleClient *client = luaL_checkudata(l, 1, METATABLE_CLIENT);

	const char *hook = luaL_checkstring(l, 2);
	const char *name = "hook";

	int index = 3;

	if (!lua_isnoneornil(l, 3) && lua_isstring(l, 3)) {
		// If a custom name for the hook is provided, adjust
		name = lua_tostring(l, 3);
		index = 4;
	}

	// Push our reference for the hooks table
	mumble_pushref(l, client->hooks);

	// Get the callback table for this hook
	lua_getfield(l, -1, hook);

	if (!lua_istable(l, -1)) {
		// No callback table exists, nothing to remove
		lua_pop(l, 2); // Remove callback table and nil
		return 0;
	}

	// Remove the field from the hook table by pushing a nil to it
	lua_pushnil(l);
	lua_setfield(l, -2, name);

	lua_pop(l, 2); // Pop callback table and hook table

	return 0;
}

static int client_call(lua_State *l)
{
	MumbleClient *client = luaL_checkudata(l, 1, METATABLE_CLIENT);
	const char* hook = luaL_checkstring(l, 2);
	int nargs = lua_gettop(l) - 2;
	return mumble_hook_call_ret(l, client, hook, nargs, LUA_MULTRET);
}

static int client_getHooks(lua_State *l)
{
	MumbleClient *client = luaL_checkudata(l, 1, METATABLE_CLIENT);
	mumble_pushref(l, client->hooks);
	return 1;
}

static int client_getUsers(lua_State *l)
{
	MumbleClient *client = luaL_checkudata(l, 1, METATABLE_CLIENT);
	lua_newtable(l);

	LinkNode* current = client->user_list;

	lua_newtable(l);
	int i = 1;

	while (current != NULL)
	{
		lua_pushnumber(l, i++);
		mumble_user_raw_get(l, client, current->index);
		lua_settable(l, -3);
		current = current->next;
	}
	return 1;
}

static int client_getChannels(lua_State *l)
{
	MumbleClient *client = luaL_checkudata(l, 1, METATABLE_CLIENT);
	mumble_pushref(l, client->channels);
	return 1;
}

static int client_getChannel(lua_State *l)
{
	MumbleClient *client = mumble_client_connecting(l, 1);
	//char* path = (char*) luaL_checkstring(l, 2);

	mumble_pushref(l, client->channels);
	lua_pushinteger(l, 0);	// root channel id is always 0
	lua_gettable(l, -2);	// index the channel table by the root channel id
	lua_remove(l, -2);		// remove table of channels from the stack

	if (lua_isnoneornil(l, -1) == 0) {
		// if we successfully indexed the root channel..
		lua_replace(l, 1); // set the root channel in argument position 1
		return channel_call(l); // call the channel_call function on root channel
	}

	/*
	-- Pseudocode on how this works..

	function mumble.client.getChannel(client, path)
		local channels = client:getChannels()			-- lua_rawgeti(l, LUA_REGISTRYINDEX, client->channels);

														-- lua_pushinteger(l, 0);
		local root = channels[0]						-- lua_gettable(l, -2);

		channels = nil									-- lua_remove(l, -2)
		
		if root ~= nil then								-- if (lua_isnoneornil(l, -1) == 0) {
			client = root								-- 	lua_replace(l, 1);
			return channel_call(client, path)			-- 	return channel_call(l);
		end												-- }
	end
	*/

	return 0;
}

static int client_registerVoiceTarget(lua_State *l)
{
	MumbleClient *client = mumble_client_connected(l, 1);

	MumbleProto__VoiceTarget msg = MUMBLE_PROTO__VOICE_TARGET__INIT;

	msg.has_id = true;
	msg.id = luaL_checkinteger(l, 2);

	size_t n_targets = 0;
	MumbleProto__VoiceTarget__Target **targets = NULL;

	// Check if the 3rd argument is a table (a list of userdata)
	if (lua_istable(l, 3)) {
		// If it's a table, get the length of the table
		n_targets = lua_objlen(l, 3);
		targets = malloc(sizeof(MumbleProto__VoiceTarget__Target) * n_targets);
		if (!targets)
			return luaL_error(l, "failed to malloc: %s", strerror(errno));

		// Iterate over the table and extract the targets
		lua_pushvalue(l, 3); // Push the table onto the stack
		lua_pushnil(l);
		int i = 0;
		while (lua_next(l, -2)) { // Iterate over the table
			if (luaL_isudata(l, -1, METATABLE_VOICETARGET)) {
				// If userdata is of the expected type, extract the target
				targets[i++] = lua_touserdata(l, -1);
			} else {
				// If not userdata of the expected type, return an error
				free(targets);
				return luaL_typerror_table(l, 3, -2, -1, METATABLE_VOICETARGET);
			}
			lua_pop(l, 1);  // Pop the value, keep the key
		}
		lua_pop(l, 1);  // Pop the table
	} else {
		// Otherwise, process varargs (userdata arguments)
		int top = lua_gettop(l);
		n_targets = top - 2;  // Subtract first 2 parameters (client and id)
		targets = malloc(sizeof(MumbleProto__VoiceTarget__Target) * n_targets);
		if (!targets)
			return luaL_error(l, "failed to malloc: %s", strerror(errno));

		// Extract target info from each userdata vararg
		for (int i = 0; i < n_targets; ++i) {
			if (luaL_isudata(l, 3 + i, METATABLE_VOICETARGET)) {
				targets[i] = lua_touserdata(l, 3 + i);
			} else {
				free(targets);
				return luaL_typerror(l, 3 + i, METATABLE_VOICETARGET);
			}
		}
	}

	msg.targets = targets;
	msg.n_targets = n_targets;

	packet_send(client, PACKET_VOICETARGET, &msg);
	free(targets);
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

static int client_getEncoder(lua_State *l)
{
	MumbleClient *client = mumble_client_connecting(l, 1);
	mumble_pushref(l, client->encoder_ref);
	return 1;
}

static int client_getPing(lua_State *l)
{
	MumbleClient *client = mumble_client_connecting(l, 1);
	lua_pushnumber(l, client->tcp_ping_avg);
	return 1;
}

static int client_getUpTime(lua_State *l)
{
	MumbleClient *client = mumble_client_connecting(l, 1);
	lua_pushnumber(l, gettime(CLOCK_MONOTONIC) - client->time);
	return 1;
}

static int client_getHost(lua_State *l)
{
	MumbleClient *client = mumble_client_connecting(l, 1);
	lua_pushstring(l, client->host);
	return 1;
}

static int client_getPort(lua_State *l)
{
	MumbleClient *client = mumble_client_connecting(l, 1);
	lua_pushinteger(l, client->port);
	return 1;
}

static int client_getAddress(lua_State *l)
{
	MumbleClient *client = mumble_client_connecting(l, 1);

	char address[INET6_ADDRSTRLEN];

	if (client->server_host_tcp->ai_family == AF_INET) {
		inet_ntop(AF_INET, &(((struct sockaddr_in*)(client->server_host_tcp->ai_addr))->sin_addr), address, INET_ADDRSTRLEN);
	} else {
		inet_ntop(AF_INET6, &(((struct sockaddr_in6*)(client->server_host_tcp->ai_addr))->sin6_addr), address, INET6_ADDRSTRLEN);
	}

	lua_pushstring(l, address);
	return 1;
}

static int client_requestTextureBlob(lua_State *l)
{
	MumbleClient *client = mumble_client_connected(l, 1);

	MumbleProto__RequestBlob msg = MUMBLE_PROTO__REQUEST_BLOB__INIT;

	int n_session_texture = 0;
	uint32_t *session_texture = NULL;

	// Check if the 2nd argument is a table (a list of userdata)
	if (lua_istable(l, 2)) {
		n_session_texture = lua_objlen(l, 2);  // Get the length of the table
		session_texture = malloc(sizeof(uint32_t) * n_session_texture);
		if (!session_texture)
			return luaL_error(l, "failed to malloc: %s", strerror(errno));

		// Iterate over the table and extract sessions
		lua_pushvalue(l, 2); // Push the table onto the stack
		lua_pushnil(l);
		int i = 0;
		while (lua_next(l, -2)) { // Start iterating over the table
			if (luaL_isudata(l, -1, METATABLE_USER)) {
				// If userdata is of the expected type, extract the session
				MumbleUser *user = lua_touserdata(l, -1);
				session_texture[i++] = user->session;
			} else {
				// If not userdata of the expected type, return an error
				free(session_texture);
				return luaL_typerror_table(l, 2, -2, -1, METATABLE_USER);
			}
			lua_pop(l, 1);  // Pop the value, keep the key
		}
		lua_pop(l, 1);  // Pop the table
	} else {
		// Otherwise, process varargs (userdata arguments)
		int top = lua_gettop(l);
		n_session_texture = top - 1;  // Subtract first parameter
		session_texture = malloc(sizeof(uint32_t) * n_session_texture);
		if (!session_texture)
			return luaL_error(l, "failed to malloc: %s", strerror(errno));

		// Extract session info from each userdata vararg
		for (int i = 0; i < n_session_texture; ++i) {
			if (luaL_isudata(l, 2 + i, METATABLE_USER)) {
				MumbleUser *user = lua_touserdata(l, 2 + i);
				session_texture[i] = user->session;
			} else {
				free(session_texture);
				return luaL_typerror(l, 2 + i, METATABLE_USER);
			}
		}
	}

	msg.n_session_texture = n_session_texture;
	msg.session_texture = session_texture;

	packet_send(client, PACKET_REQUESTBLOB, &msg);
	free(session_texture);
	return 0;
}

static int client_requestCommentBlob(lua_State *l)
{
	MumbleClient *client = mumble_client_connected(l, 1);
	luaL_checktype(l, 2, LUA_TTABLE);

	MumbleProto__RequestBlob msg = MUMBLE_PROTO__REQUEST_BLOB__INIT;

	int n_session_comment = 0;
	uint32_t *session_comment = NULL;

	// Check if the 2nd argument is a table (a list of userdata)
	if (lua_istable(l, 2)) {
		n_session_comment = lua_objlen(l, 2);  // Get the length of the table
		session_comment = malloc(sizeof(uint32_t) * n_session_comment);
		if (!session_comment)
			return luaL_error(l, "failed to malloc: %s", strerror(errno));

		// Iterate over the table and extract sessions
		lua_pushvalue(l, 2); // Push the table onto the stack
		lua_pushnil(l);
		int i = 0;
		while (lua_next(l, -2)) { // Start iterating over the table
			if (luaL_isudata(l, -1, METATABLE_USER)) {
				// If userdata is of the expected type, extract the session
				MumbleUser *user = lua_touserdata(l, -1);
				session_comment[i++] = user->session;
			} else {
				// If not userdata of the expected type, return an error
				free(session_comment);
				return luaL_typerror_table(l, 2, -2, -1, METATABLE_USER);
			}
			lua_pop(l, 1);  // Pop the value, keep the key
		}
		lua_pop(l, 1);  // Pop the table
	} else {
		// Otherwise, process varargs (userdata arguments)
		int top = lua_gettop(l);
		n_session_comment = top - 1;  // Subtract first parameter
		session_comment = malloc(sizeof(uint32_t) * n_session_comment);
		if (!session_comment)
			return luaL_error(l, "failed to malloc: %s", strerror(errno));

		// Extract session info from each userdata vararg
		for (int i = 0; i < n_session_comment; ++i) {
			if (luaL_isudata(l, 2 + i, METATABLE_USER)) {
				MumbleUser *user = lua_touserdata(l, 2 + i);
				session_comment[i] = user->session;
			} else {
				free(session_comment);
				return luaL_typerror(l, 2 + i, METATABLE_USER);
			}
		}
	}

	msg.n_session_comment = n_session_comment;
	msg.session_comment = session_comment;

	packet_send(client, PACKET_REQUESTBLOB, &msg);
	free(session_comment);
	return 0;
}

static int client_requestDescriptionBlob(lua_State *l)
{
	MumbleClient *client = mumble_client_connected(l, 1);
	luaL_checktype(l, 2, LUA_TTABLE);

	MumbleProto__RequestBlob msg = MUMBLE_PROTO__REQUEST_BLOB__INIT;

	int n_channel_description = 0;
	uint32_t *channel_description = NULL;

	// Check if the 2nd argument is a table (a list of userdata)
	if (lua_istable(l, 2)) {
		n_channel_description = lua_objlen(l, 2);  // Get the length of the table
		channel_description = malloc(sizeof(uint32_t) * n_channel_description);
		if (!channel_description)
			return luaL_error(l, "failed to malloc: %s", strerror(errno));

		// Iterate over the table and extract sessions
		lua_pushvalue(l, 2); // Push the table onto the stack
		lua_pushnil(l);
		int i = 0;
		while (lua_next(l, -2)) { // Start iterating over the table
			if (luaL_isudata(l, -1, METATABLE_CHAN)) {
				// If userdata is of the expected type, extract the session
				MumbleChannel *channel = lua_touserdata(l, -1);
				channel_description[i++] = channel->channel_id;
			} else {
				// If not userdata of the expected type, return an error
				free(channel_description);
				return luaL_typerror_table(l, 2, -2, -1, METATABLE_CHAN);
			}
			lua_pop(l, 1);  // Pop the value, keep the key
		}
		lua_pop(l, 1);  // Pop the table
	} else {
		// Otherwise, process varargs (userdata arguments)
		int top = lua_gettop(l);
		n_channel_description = top - 1;  // Subtract first parameter
		channel_description = malloc(sizeof(uint32_t) * n_channel_description);
		if (!channel_description)
			return luaL_error(l, "failed to malloc: %s", strerror(errno));

		// Extract session info from each userdata vararg
		for (int i = 0; i < n_channel_description; ++i) {
			if (luaL_isudata(l, 2 + i, METATABLE_CHAN)) {
				MumbleChannel *channel = lua_touserdata(l, 2 + i);
				channel_description[i] = channel->channel_id;
			} else {
				free(channel_description);
				return luaL_typerror(l, 2 + i, METATABLE_CHAN);
			}
		}
	}

	msg.n_channel_description = n_channel_description;
	msg.channel_description = channel_description;

	packet_send(client, PACKET_REQUESTBLOB, &msg);
	free(channel_description);
	return 0;
}

static int client_createChannel(lua_State *l)
{
	MumbleClient *client = mumble_client_connected(l, 1);

	MumbleProto__ChannelState msg = MUMBLE_PROTO__CHANNEL_STATE__INIT;

	msg.parent = 0;
	msg.name = (char*) luaL_checkstring(l, 2);
	msg.description = (char*) luaL_optstring(l, 3, "");
	msg.position = luaL_optinteger(l, 4, 0);
	msg.temporary = luaL_optboolean(l, 5, false);
	msg.max_users = luaL_optinteger(l, 6, 0);

	packet_send(client, PACKET_CHANNELSTATE, &msg);
	return 0;
}

static int client_getMe(lua_State *l)
{
	MumbleClient *client = luaL_checkudata(l, 1, METATABLE_CLIENT);
	mumble_user_raw_get(l, client, client->session);
	return 1;
}

static int client_isTunnelingUDP(lua_State *l)
{
	MumbleClient *client = luaL_checkudata(l, 1, METATABLE_CLIENT);
	lua_pushboolean(l, client->tcp_udp_tunnel);
	return 1;
}

static int client_gc(lua_State *l)
{
	MumbleClient *client = luaL_checkudata(l, 1, METATABLE_CLIENT);
	mumble_disconnect(l, client, "garbage collected", true);

	mumble_unref(l, client->hooks);
	mumble_unref(l, client->users);
	mumble_unref(l, client->channels);
	mumble_unref(l, client->audio_streams);
	mumble_unref(l, client->encoder_ref);
	
	mumble_log(LOG_DEBUG, "%s: %p garbage collected\n", METATABLE_CLIENT, client);
	return 0;
}

static int client_tostring(lua_State *l)
{
	MumbleClient *client = luaL_checkudata(l, 1, METATABLE_CLIENT);
	if (client->connecting) {
		lua_pushfstring(l, "%s [%d][\"%s:%d\"] %p", METATABLE_CLIENT, client->self, client->host, client->port, client);
	} else {
		lua_pushfstring(l, "%s: %p", METATABLE_CLIENT, client);
	}
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
	{"setTokens", client_setTokens},
	{"connect", mumble_client_connect},
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
	{"getMe", client_getMe},
	{"getSelf", client_getMe},
	{"isTunnelingUDP", client_isTunnelingUDP},
	{"__gc", client_gc},
	{"__tostring", client_tostring},
	{"__index", client_index},
	{NULL, NULL}
};