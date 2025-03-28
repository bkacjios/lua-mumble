#include "mumble.h"

#include "channel.h"
#include "user.h"
#include "packet.h"
#include "util.h"
#include "log.h"

/*--------------------------------
	MUMBLE USER META METHODS
--------------------------------*/

static int user_message(lua_State *l) {
	MumbleUser *user = luaL_checkudata(l, 1, METATABLE_USER);

	MumbleProto__TextMessage msg = MUMBLE_PROTO__TEXT_MESSAGE__INIT;

	msg.message = (char*) luaL_checkstring(l, 2);

	msg.n_session = 1;
	msg.session = malloc(sizeof(uint32_t) * msg.n_session);

	if (msg.session == NULL)
		return luaL_error(l, "failed to malloc: %s", strerror(errno));

	msg.session[0] = user->session;

	packet_send(user->client, PACKET_TEXTMESSAGE, &msg);
	free(msg.session);
	return 0;
}

static int user_kick(lua_State *l) {
	MumbleUser *user = luaL_checkudata(l, 1, METATABLE_USER);

	MumbleProto__UserRemove msg = MUMBLE_PROTO__USER_REMOVE__INIT;

	msg.session = user->session;

	if (lua_isnil(l, 2) == 0) {
		msg.reason = (char*) luaL_checkstring(l, 2);
	}

	packet_send(user->client, PACKET_USERREMOVE, &msg);
	return 0;
}

static int user_ban(lua_State *l) {
	MumbleUser *user = luaL_checkudata(l, 1, METATABLE_USER);

	MumbleProto__UserRemove msg = MUMBLE_PROTO__USER_REMOVE__INIT;

	msg.session = user->session;

	if (lua_isnil(l, 2) == 0) {
		msg.reason = (char *)luaL_checkstring(l, 2);
	}

	msg.has_ban = true;
	msg.ban = true;

	packet_send(user->client, PACKET_USERREMOVE, &msg);
	return 0;
}

static int user_move(lua_State *l) {
	MumbleUser *user = luaL_checkudata(l, 1, METATABLE_USER);
	MumbleChannel *channel = luaL_checkudata(l, 2, METATABLE_CHAN);

	MumbleProto__UserState msg = MUMBLE_PROTO__USER_STATE__INIT;

	msg.has_session = true;
	msg.session = user->session;

	msg.has_channel_id = true;
	msg.channel_id = channel->channel_id;

	packet_send(user->client, PACKET_USERSTATE, &msg);
	return 0;
}

static int user_setMuted(lua_State *l) {
	MumbleUser *user = luaL_checkudata(l, 1, METATABLE_USER);

	MumbleProto__UserState msg = MUMBLE_PROTO__USER_STATE__INIT;

	msg.has_session = true;
	msg.session = user->session;

	msg.has_mute = true;
	msg.mute = lua_toboolean(l, 2);

	packet_send(user->client, PACKET_USERSTATE, &msg);
	return 0;
}

static int user_setDeaf(lua_State *l) {
	MumbleUser *user = luaL_checkudata(l, 1, METATABLE_USER);

	MumbleProto__UserState msg = MUMBLE_PROTO__USER_STATE__INIT;

	msg.has_session = true;
	msg.session = user->session;

	msg.has_mute = true;
	msg.mute = lua_toboolean(l, 2);

	msg.has_deaf = true;
	msg.deaf = lua_toboolean(l, 2);

	packet_send(user->client, PACKET_USERSTATE, &msg);
	return 0;
}

static int user_register(lua_State *l) {
	MumbleUser *user = luaL_checkudata(l, 1, METATABLE_USER);

	MumbleProto__UserState msg = MUMBLE_PROTO__USER_STATE__INIT;
	msg.has_user_id = true;
	msg.user_id = 0;

	msg.has_session = true;
	msg.session = user->session;

	packet_send(user->client, PACKET_USERSTATE, &msg);
	return 0;
}

static int user_request_stats(lua_State *l) {
	MumbleUser *user = luaL_checkudata(l, 1, METATABLE_USER);

	MumbleProto__UserStats msg = MUMBLE_PROTO__USER_STATS__INIT;

	msg.has_session = true;
	msg.session = user->session;

	msg.stats_only = lua_toboolean(l, 2);

	packet_send(user->client, PACKET_USERSTATS, &msg);
	return 0;
}

static int user_getClient(lua_State *l) {
	MumbleUser *user = luaL_checkudata(l, 1, METATABLE_USER);
	mumble_client_raw_get(user->client);
	return 1;
}

static int user_getSession(lua_State *l) {
	MumbleUser *user = luaL_checkudata(l, 1, METATABLE_USER);
	lua_pushinteger(l, user->session);
	return 1;
}

static int user_getName(lua_State *l) {
	MumbleUser *user = luaL_checkudata(l, 1, METATABLE_USER);
	lua_pushstring(l, user->name);
	return 1;
}

static int user_getChannel(lua_State *l) {
	MumbleUser *user = luaL_checkudata(l, 1, METATABLE_USER);
	mumble_channel_raw_get(user->client, user->channel_id);
	return 1;
}

static int user_getId(lua_State *l) {
	MumbleUser *user = luaL_checkudata(l, 1, METATABLE_USER);
	lua_pushinteger(l, user->user_id);
	return 1;
}

static int user_isRegistered(lua_State *l) {
	MumbleUser *user = luaL_checkudata(l, 1, METATABLE_USER);
	lua_pushboolean(l, user->user_id > 0);
	return 1;
}

static int user_isMute(lua_State *l) {
	MumbleUser *user = luaL_checkudata(l, 1, METATABLE_USER);
	lua_pushboolean(l, user->mute);
	return 1;
}

static int user_isDeaf(lua_State *l) {
	MumbleUser *user = luaL_checkudata(l, 1, METATABLE_USER);
	lua_pushboolean(l, user->deaf);
	return 1;
}

static int user_isSelfMute(lua_State *l) {
	MumbleUser *user = luaL_checkudata(l, 1, METATABLE_USER);
	lua_pushboolean(l, user->self_mute);
	return 1;
}

static int user_isSelfDeaf(lua_State *l) {
	MumbleUser *user = luaL_checkudata(l, 1, METATABLE_USER);
	lua_pushboolean(l, user->self_deaf);
	return 1;
}

static int user_isSuppressed(lua_State *l) {
	MumbleUser *user = luaL_checkudata(l, 1, METATABLE_USER);
	lua_pushboolean(l, user->suppress);
	return 1;
}

static int user_getComment(lua_State *l) {
	MumbleUser *user = luaL_checkudata(l, 1, METATABLE_USER);
	lua_pushstring(l, user->comment);
	return 1;
}

static int user_getCommentHash(lua_State *l) {
	MumbleUser *user = luaL_checkudata(l, 1, METATABLE_USER);
	char* result;
	bin_to_strhex(user->comment_hash, user->comment_hash_len, &result);
	lua_pushstring(l, result);
	free(result);
	return 1;
}

static int user_isSpeaking(lua_State *l) {
	MumbleUser *user = luaL_checkudata(l, 1, METATABLE_USER);
	lua_pushboolean(l, user->speaking);
	return 1;
}

static int user_isRecording(lua_State *l) {
	MumbleUser *user = luaL_checkudata(l, 1, METATABLE_USER);
	lua_pushboolean(l, user->recording);
	return 1;
}

static int user_isPrioritySpeaker(lua_State *l) {
	MumbleUser *user = luaL_checkudata(l, 1, METATABLE_USER);
	lua_pushboolean(l, user->priority_speaker);
	return 1;
}

static int user_getTexture(lua_State *l) {
	MumbleUser *user = luaL_checkudata(l, 1, METATABLE_USER);
	lua_pushstring(l, user->texture);
	return 1;
}

static int user_getTextureHash(lua_State *l) {
	MumbleUser *user = luaL_checkudata(l, 1, METATABLE_USER);
	char* result;
	bin_to_strhex(user->texture_hash, user->texture_hash_len, &result);
	lua_pushstring(l, result);
	free(result);
	return 1;
}

static int user_getHash(lua_State *l) {
	MumbleUser *user = luaL_checkudata(l, 1, METATABLE_USER);
	lua_pushstring(l, user->hash);
	return 1;
}

static int user_setTexture(lua_State *l) {
	MumbleUser *user = luaL_checkudata(l, 1, METATABLE_USER);

	MumbleProto__UserState msg = MUMBLE_PROTO__USER_STATE__INIT;

	msg.has_session = true;
	msg.session = user->session;

	msg.has_texture = true;
	msg.texture.data = (uint8_t *) luaL_checklstring(l, 2, &msg.texture.len);

	packet_send(user->client, PACKET_USERSTATE, &msg);
	return 0;
}

static int user_tostring(lua_State *l) {
	MumbleUser *user = luaL_checkudata(l, 1, METATABLE_USER);
	lua_pushfstring(l, "%s [%d][\"%s\"] %p", METATABLE_USER, user->session, user->name, user);
	return 1;
}

static int user_listen(lua_State *l) {
	MumbleUser *user = luaL_checkudata(l, 1, METATABLE_USER);

	MumbleProto__UserState msg = MUMBLE_PROTO__USER_STATE__INIT;

	msg.has_session = true;
	msg.session = user->session;

	size_t n_listening_channel_add = 0;
	uint32_t *listening_channel_add = NULL;

	// Check if the 2nd argument is a table (a list of userdata)
	if (lua_istable(l, 2)) {
		// If it's a table, get the length of the table
		n_listening_channel_add = lua_objlen(l, 2);
		listening_channel_add = malloc(sizeof(uint32_t) * n_listening_channel_add);
		if (!listening_channel_add)
			return luaL_error(l, "failed to malloc: %s", strerror(errno));

		// Iterate over the table and extract the channel->channel_id
		lua_pushvalue(l, 2); // Push the table onto the stack
		lua_pushnil(l);
		int i = 0;
		while (lua_next(l, -2)) { // Iterate over the table
			if (luaL_isudata(l, -1, METATABLE_CHAN)) {
				MumbleChannel *channel = lua_touserdata(l, -1);
				listening_channel_add[i++] = channel->channel_id;
			} else {
				// If not userdata of the expected type, return an error
				free(listening_channel_add);
				return luaL_typerror_table(l, 2, -2, -1, METATABLE_CHAN);
			}
			lua_pop(l, 1);  // Pop the value, keep the key
		}
		lua_pop(l, 1);  // Pop the table
	} else {
		// Otherwise, process varargs (userdata arguments)
		int top = lua_gettop(l);
		n_listening_channel_add = top - 1;  // Subtract first parameter (user)
		listening_channel_add = malloc(sizeof(uint32_t) * n_listening_channel_add);
		if (!listening_channel_add)
			return luaL_error(l, "failed to malloc: %s", strerror(errno));

		// Extract channel->channel_id from each userdata vararg
		for (int i = 0; i < n_listening_channel_add; ++i) {
			if (luaL_isudata(l, 2 + i, METATABLE_CHAN)) {
				MumbleChannel *channel = lua_touserdata(l, -1);
				listening_channel_add[i] = channel->channel_id;
			} else {
				free(listening_channel_add);
				return luaL_typerror(l, 2 + i, METATABLE_CHAN);
			}
		}
	}

	msg.listening_channel_add = listening_channel_add;
	msg.n_listening_channel_add = n_listening_channel_add;

	packet_send(user->client, PACKET_USERSTATE, &msg);
	free(msg.listening_channel_add);
	return 0;
}

static int user_unlisten(lua_State *l) {
	MumbleUser *user = luaL_checkudata(l, 1, METATABLE_USER);

	MumbleProto__UserState msg = MUMBLE_PROTO__USER_STATE__INIT;

	msg.has_session = true;
	msg.session = user->session;

	size_t n_listening_channel_remove = 0;
	uint32_t *listening_channel_remove = NULL;

	// Check if the 2nd argument is a table (a list of userdata)
	if (lua_istable(l, 2)) {
		// If it's a table, get the length of the table
		n_listening_channel_remove = lua_objlen(l, 2);
		listening_channel_remove = malloc(sizeof(uint32_t) * n_listening_channel_remove);
		if (!listening_channel_remove)
			return luaL_error(l, "failed to malloc: %s", strerror(errno));

		// Iterate over the table and extract the channel->channel_id
		lua_pushvalue(l, 2); // Push the table onto the stack
		lua_pushnil(l);
		int i = 0;
		while (lua_next(l, -2)) { // Iterate over the table
			if (luaL_isudata(l, -1, METATABLE_CHAN)) {
				MumbleChannel *channel = lua_touserdata(l, -1);
				listening_channel_remove[i++] = channel->channel_id;
			} else {
				// If not userdata of the expected type, return an error
				free(listening_channel_remove);
				return luaL_typerror_table(l, 2, -2, -1, METATABLE_CHAN);
			}
			lua_pop(l, 1);  // Pop the value, keep the key
		}
		lua_pop(l, 1);  // Pop the table
	} else {
		// Otherwise, process varargs (userdata arguments)
		int top = lua_gettop(l);
		n_listening_channel_remove = top - 1;  // Subtract first parameter (user)
		listening_channel_remove = malloc(sizeof(uint32_t) * n_listening_channel_remove);
		if (!listening_channel_remove)
			return luaL_error(l, "failed to malloc: %s", strerror(errno));

		// Extract channel->channel_id from each userdata vararg
		for (int i = 0; i < n_listening_channel_remove; ++i) {
			if (luaL_isudata(l, 2 + i, METATABLE_CHAN)) {
				MumbleChannel *channel = lua_touserdata(l, -1);
				listening_channel_remove[i] = channel->channel_id;
			} else {
				free(listening_channel_remove);
				return luaL_typerror(l, 2 + i, METATABLE_CHAN);
			}
		}
	}

	msg.listening_channel_remove = listening_channel_remove;
	msg.n_listening_channel_remove = n_listening_channel_remove;

	packet_send(user->client, PACKET_USERSTATE, &msg);
	free(msg.listening_channel_remove);
	return 0;
}

static int user_isListening(lua_State *l) {
	MumbleUser *user = luaL_checkudata(l, 1, METATABLE_USER);
	MumbleChannel *channel = luaL_checkudata(l, 2, METATABLE_CHAN);

	LinkNode * current = user->listens;

	while (current != NULL) {
		if (current->index == channel->channel_id) {
			lua_pushboolean(l, true);
			return 1;
		}
		current = current->next;
	}

	lua_pushboolean(l, false);
	return 1;
}

static int user_getListens(lua_State *l) {
	MumbleUser *user = luaL_checkudata(l, 1, METATABLE_USER);

	lua_newtable(l);

	LinkNode * current = user->listens;

	// Add all linked channels to the table
	while (current != NULL) {
		lua_pushinteger(l, current->index);
		mumble_channel_raw_get(user->client, current->index);
		lua_settable(l, -3);
		current = current->next;
	}

	return 1;
}

static int user_sendPluginData(lua_State *l) {
	MumbleUser *user = luaL_checkudata(l, 1, METATABLE_USER);

	MumbleProto__PluginDataTransmission data = MUMBLE_PROTO__PLUGIN_DATA_TRANSMISSION__INIT;

	data.has_sendersession = true;
	data.sendersession = user->client->session;

	data.dataid = (char*) luaL_checkstring(l, 2);

	ProtobufCBinaryData cbdata;
	cbdata.data = (uint8_t*) luaL_checklstring(l, 3, &cbdata.len);

	data.has_data = true;
	data.data = cbdata;

	data.n_receiversessions = 1;
	data.receiversessions = malloc(sizeof(uint32_t) * data.n_receiversessions);

	if (data.receiversessions == NULL)
		return luaL_error(l, "failed to malloc: %s", strerror(errno));

	data.receiversessions[0] = user->session;

	packet_send(user->client, PACKET_PLUGINDATA, &data);
	free(data.receiversessions);
	return 0;
}

static int user_requestTextureBlob(lua_State *l) {
	MumbleUser *user = luaL_checkudata(l, 1, METATABLE_USER);

	MumbleProto__RequestBlob msg = MUMBLE_PROTO__REQUEST_BLOB__INIT;

	msg.n_session_texture = 1;
	msg.session_texture = malloc(sizeof(uint32_t) * msg.n_session_texture);

	if (msg.session_texture == NULL)
		return luaL_error(l, "failed to malloc: %s", strerror(errno));

	msg.session_texture[0] = user->session;

	packet_send(user->client, PACKET_REQUESTBLOB, &msg);
	free(msg.session_texture);
	return 0;
}

static int user_requestCommentBlob(lua_State *l) {
	MumbleUser *user = luaL_checkudata(l, 1, METATABLE_USER);

	MumbleProto__RequestBlob msg = MUMBLE_PROTO__REQUEST_BLOB__INIT;

	msg.n_session_comment = 1;
	msg.session_comment = malloc(sizeof(uint32_t) * msg.n_session_comment);

	if (msg.session_comment == NULL)
		return luaL_error(l, "failed to malloc: %s", strerror(errno));

	msg.session_comment[0] = user->session;

	packet_send(user->client, PACKET_REQUESTBLOB, &msg);
	free(msg.session_comment);
	return 0;
}

static int user_startRecord(lua_State *l) {
	MumbleUser *user = luaL_checkudata(l, 1, METATABLE_USER);

	if (user->recording_file != NULL) {
		lua_pushnil(l);
		lua_pushstring(l, "user is already being recorded");
		return 1;
	}

	const char *filepath = luaL_checkstring(l, 2);

	SF_INFO sfinfo;
	sfinfo.samplerate = AUDIO_SAMPLE_RATE;
	sfinfo.channels = AUDIO_PLAYBACK_CHANNELS;
	sfinfo.format = SF_FORMAT_OGG | SF_FORMAT_VORBIS;

	SNDFILE *outfile = sf_open(filepath, SFM_WRITE, &sfinfo);
	if (!outfile) {
		lua_pushnil(l);
		lua_pushfstring(l, "error opening file \"%s\" (%s)", filepath, sf_strerror(NULL));
		return 2;
	}

	// Start recording at current timestamp
	user->last_spoke = uv_now(uv_default_loop());
	user->recording_file = outfile;

	mumble_update_recording_status(user->client);

	lua_pushboolean(l, true);
	return 1;
}

static void user_handle_stop_recording(lua_State *l, MumbleUser *user) {
	// Handle any silence, from the time the user last stopped talking, until the end of the recording
	mumble_handle_record_silence(user->client, user);
	sf_close(user->recording_file);
	user->recording_file = NULL;
}

static int user_stopRecord(lua_State *l) {
	MumbleUser *user = luaL_checkudata(l, 1, METATABLE_USER);

	if (user->recording_file != NULL) {
		user_handle_stop_recording(l, user);
		mumble_update_recording_status(user->client);
		lua_pushboolean(l, true);
	} else {
		lua_pushboolean(l, false);
	}

	return 1;
}

static int user_isBeingRecorded(lua_State *l) {
	MumbleUser *user = luaL_checkudata(l, 1, METATABLE_USER);
	lua_pushboolean(l, user->recording_file != NULL);
	return 1;
}

static int user_contextAction(lua_State *l) {
	MumbleUser *user = luaL_checkudata(l, 1, METATABLE_USER);

	MumbleProto__ContextAction msg = MUMBLE_PROTO__CONTEXT_ACTION__INIT;

	msg.has_session = true;
	msg.session = user->session;

	msg.action = (char*) luaL_checkstring(l, 2);

	packet_send(user->client, PACKET_CONTEXTACTION, &msg);
	return 0;
}

static int user_gc(lua_State *l) {
	MumbleUser *user = luaL_checkudata(l, 1, METATABLE_USER);
	mumble_log(LOG_DEBUG, "%s: %p garbage collected", METATABLE_USER, user);
	if (user->recording_file) {
		user_handle_stop_recording(l, user);
	}
	if (user->name) {
		free(user->name);
	}
	if (user->comment) {
		free(user->comment);
	}
	if (user->texture) {
		free(user->texture);
	}
	if (user->hash) {
		free(user->hash);
	}
	if (user->comment_hash) {
		free(user->comment_hash);
	}
	if (user->texture_hash) {
		free(user->texture_hash);
	}
	list_clear(&user->listens);
	mumble_registry_unref(l, MUMBLE_DATA_REG, &user->data);
	return 0;
}

static int user_newindex(lua_State *l) {
	MumbleUser *user = luaL_checkudata(l, 1, METATABLE_USER);

	mumble_registry_pushref(l, MUMBLE_DATA_REG, user->data);
	lua_pushvalue(l, 2);
	lua_pushvalue(l, 3);
	lua_settable(l, -3);
	return 0;
}

static int user_index(lua_State *l) {
	MumbleUser *user = luaL_checkudata(l, 1, METATABLE_USER);

	mumble_registry_pushref(l, MUMBLE_DATA_REG, user->data);
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
