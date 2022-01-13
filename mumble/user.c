#include "mumble.h"

#include "channel.h"
#include "user.h"
#include "packet.h"

/*--------------------------------
	MUMBLE USER META METHODS
--------------------------------*/

static int user_message(lua_State *l)
{
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

static int user_kick(lua_State *l)
{
	MumbleUser *user = luaL_checkudata(l, 1, METATABLE_USER);

	MumbleProto__UserRemove msg = MUMBLE_PROTO__USER_REMOVE__INIT;

	msg.session = user->session;

	if (lua_isnil(l, 2) == 0) {
		msg.reason = (char*) luaL_checkstring(l, 2);
	}

	packet_send(user->client, PACKET_USERREMOVE, &msg);
	return 0;
}

static int user_ban(lua_State *l)
{
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

static int user_move(lua_State *l)
{
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

static int user_setMuted(lua_State *l)
{
	MumbleUser *user = luaL_checkudata(l, 1, METATABLE_USER);

	MumbleProto__UserState msg = MUMBLE_PROTO__USER_STATE__INIT;

	msg.has_session = true;
	msg.session = user->session;

	msg.has_mute = true;
	msg.mute = lua_toboolean(l, 2);

	packet_send(user->client, PACKET_USERSTATE, &msg);
	return 0;
}

static int user_setDeaf(lua_State *l)
{
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

static int user_register(lua_State *l)
{
	MumbleUser *user = luaL_checkudata(l, 1, METATABLE_USER);
	
	MumbleProto__UserState msg = MUMBLE_PROTO__USER_STATE__INIT;
	msg.has_user_id = true;
	msg.user_id = 0;

	msg.has_session = true;
	msg.session = user->session;

	packet_send(user->client, PACKET_USERSTATE, &msg);
	return 0;
}

static int user_request_stats(lua_State *l)
{
	MumbleUser *user = luaL_checkudata(l, 1, METATABLE_USER);

	MumbleProto__UserStats msg = MUMBLE_PROTO__USER_STATS__INIT;

	msg.has_session = true;
	msg.session = user->session;

	msg.stats_only = lua_toboolean(l, 2);

	packet_send(user->client, PACKET_USERSTATS, &msg);
	return 0;
}

static int user_getClient(lua_State *l)
{
	MumbleUser *user = luaL_checkudata(l, 1, METATABLE_USER);
	mumble_client_raw_get(l, user->client);
	return 1;
}

static int user_getSession(lua_State *l)
{
	MumbleUser *user = luaL_checkudata(l, 1, METATABLE_USER);
	lua_pushinteger(l, user->session);
	return 1;
}

static int user_getName(lua_State *l)
{
	MumbleUser *user = luaL_checkudata(l, 1, METATABLE_USER);
	lua_pushstring(l, user->name);
	return 1;
}

static int user_getChannel(lua_State *l)
{
	MumbleUser *user = luaL_checkudata(l, 1, METATABLE_USER);
	mumble_channel_raw_get(l, user->client, user->channel_id);
	return 1;
}

static int user_getID(lua_State *l)
{
	MumbleUser *user = luaL_checkudata(l, 1, METATABLE_USER);
	lua_pushinteger(l, user->user_id);
	return 1;
}

static int user_isMute(lua_State *l)
{
	MumbleUser *user = luaL_checkudata(l, 1, METATABLE_USER);
	lua_pushboolean(l, user->mute);
	return 1;
}

static int user_isDeaf(lua_State *l)
{
	MumbleUser *user = luaL_checkudata(l, 1, METATABLE_USER);
	lua_pushboolean(l, user->deaf);
	return 1;
}

static int user_isSelfMute(lua_State *l)
{
	MumbleUser *user = luaL_checkudata(l, 1, METATABLE_USER);
	lua_pushboolean(l, user->self_mute);
	return 1;
}

static int user_isSelfDeaf(lua_State *l)
{
	MumbleUser *user = luaL_checkudata(l, 1, METATABLE_USER);
	lua_pushboolean(l, user->self_deaf);
	return 1;
}

static int user_isSuppressed(lua_State *l)
{
	MumbleUser *user = luaL_checkudata(l, 1, METATABLE_USER);
	lua_pushboolean(l, user->suppress);
	return 1;
}

static int user_getComment(lua_State *l)
{
	MumbleUser *user = luaL_checkudata(l, 1, METATABLE_USER);
	lua_pushstring(l, user->comment);
	return 1;
}

static int user_getCommentHash(lua_State *l)
{
	MumbleUser *user = luaL_checkudata(l, 1, METATABLE_USER);
	char* result;
	bin_to_strhex(user->comment_hash, user->comment_hash_len, &result);
	lua_pushstring(l, result);
	free(result);
	return 1;
}

static int user_isSpeaking(lua_State *l)
{
	MumbleUser *user = luaL_checkudata(l, 1, METATABLE_USER);
	lua_pushboolean(l, user->speaking);
	return 1;
}

static int user_isRecording(lua_State *l)
{
	MumbleUser *user = luaL_checkudata(l, 1, METATABLE_USER);
	lua_pushboolean(l, user->recording);
	return 1;
}

static int user_isPrioritySpeaker(lua_State *l)
{
	MumbleUser *user = luaL_checkudata(l, 1, METATABLE_USER);
	lua_pushboolean(l, user->priority_speaker);
	return 1;
}

static int user_getTexture(lua_State *l)
{
	MumbleUser *user = luaL_checkudata(l, 1, METATABLE_USER);
	lua_pushstring(l, user->texture);
	return 1;
}

static int user_getTextureHash(lua_State *l)
{
	MumbleUser *user = luaL_checkudata(l, 1, METATABLE_USER);
	char* result;
	bin_to_strhex(user->texture_hash, user->texture_hash_len, &result);
	lua_pushstring(l, result);
	free(result);
	return 1;
}

static int user_getHash(lua_State *l)
{
	MumbleUser *user = luaL_checkudata(l, 1, METATABLE_USER);
	lua_pushstring(l, user->hash);
	return 1;
}

static int user_setTexture(lua_State *l)
{
	MumbleUser *user = luaL_checkudata(l, 1, METATABLE_USER);

	MumbleProto__UserState msg = MUMBLE_PROTO__USER_STATE__INIT;

	msg.has_session = true;
	msg.session = user->session;
	
	msg.has_texture = true;
	msg.texture.data = (uint8_t *) luaL_checklstring(l, 2, &msg.texture.len);

	packet_send(user->client, PACKET_USERSTATE, &msg);
	return 0;
}

static int user_tostring(lua_State *l)
{	
	MumbleUser *user = luaL_checkudata(l, 1, METATABLE_USER);
	lua_pushfstring(l, "%s [%d][%s]", METATABLE_USER, user->session, user->name);
	return 1;
}

static int user_listen(lua_State *l)
{
	MumbleUser *user = luaL_checkudata(l, 1, METATABLE_USER);

	MumbleProto__UserState msg = MUMBLE_PROTO__USER_STATE__INIT;

	msg.has_session = true;
	msg.session = user->session;

	// Get the number of channels we want to listen to
	msg.n_listening_channel_add = lua_gettop(l) - 1;
	msg.listening_channel_add = malloc(sizeof(uint32_t) * msg.n_listening_channel_add);

	if (msg.listening_channel_add == NULL)
		return luaL_error(l, "failed to malloc: %s", strerror(errno));

	// Loop through each argument and add the channel_id to the array
	for (int i = 0; i < msg.n_listening_channel_add; i++) {
		MumbleChannel *listen = luaL_checkudata(l, i+2, METATABLE_CHAN);
		msg.listening_channel_add[i] = listen->channel_id;
	}

	packet_send(user->client, PACKET_USERSTATE, &msg);
	free(msg.listening_channel_add);
	return 0;
}

static int user_unlisten(lua_State *l)
{
	MumbleUser *user = luaL_checkudata(l, 1, METATABLE_USER);

	MumbleProto__UserState msg = MUMBLE_PROTO__USER_STATE__INIT;

	msg.has_session = true;
	msg.session = user->session;

	// Get the number of channels we want to unlink
	msg.n_listening_channel_remove = lua_gettop(l) - 1;
	msg.listening_channel_remove = malloc(sizeof(uint32_t) * msg.n_listening_channel_remove);

	if (msg.listening_channel_remove == NULL)
		return luaL_error(l, "failed to malloc: %s", strerror(errno));

	for (int i = 0; i < msg.n_listening_channel_remove; i++) {
		MumbleChannel *listen = luaL_checkudata(l, i+2, METATABLE_CHAN);
		msg.listening_channel_remove[i] = listen->channel_id;
	}

	packet_send(user->client, PACKET_USERSTATE, &msg);
	free(msg.listening_channel_remove);
	return 0;
}

static int user_getListens(lua_State *l)
{
	MumbleUser *user = luaL_checkudata(l, 1, METATABLE_USER);

	lua_newtable(l);

	LinkNode * current = user->listens;

	// Add all linked channels to the table
    while (current != NULL) {
		lua_pushinteger(l, current->data);
		mumble_channel_raw_get(l, user->client, current->data);
		lua_settable(l, -3);
        current = current->next;
    }

	return 1;
}

static int user_sendPluginData(lua_State *l)
{
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

static int user_requestTextureBlob(lua_State *l)
{
	MumbleUser *user = luaL_checkudata(l, 1, METATABLE_USER);

	MumbleProto__RequestBlob msg = MUMBLE_PROTO__REQUEST_BLOB__INIT;

	msg.n_session_texture = 1;
	msg.session_texture = malloc(sizeof(uint32_t) * msg.n_session_texture);

	if (msg.session_texture == NULL)
		return luaL_error(l, "failed to malloc: %s", strerror(errno));

	msg.session_texture[0] = user->session;

	packet_send(user->client, PACKET_USERSTATE, &msg);
	free(msg.session_texture);
	return 0;
}

static int user_requestCommentBlob(lua_State *l)
{
	MumbleUser *user = luaL_checkudata(l, 1, METATABLE_USER);

	MumbleProto__RequestBlob msg = MUMBLE_PROTO__REQUEST_BLOB__INIT;

	msg.n_session_comment = 1;
	msg.session_comment = malloc(sizeof(uint32_t) * msg.n_session_comment);

	if (msg.session_comment == NULL)
		return luaL_error(l, "failed to malloc: %s", strerror(errno));

	msg.session_comment[0] = user->session;

	packet_send(user->client, PACKET_USERSTATE, &msg);
	free(msg.session_comment);
	return 0;
}

static int user_gc(lua_State *l)
{
	MumbleUser *user = luaL_checkudata(l, 1, METATABLE_USER);
	mumble_unref(l, user->data);
	return 0;
}

static int user_newindex(lua_State *l)
{
	MumbleUser *user = luaL_checkudata(l, 1, METATABLE_USER);

	mumble_pushref(l, user->data);
	lua_pushvalue(l, 2);
	lua_pushvalue(l, 3);
	lua_settable(l, -3);
	return 0;
}

static int user_index(lua_State *l)
{
	MumbleUser *user = luaL_checkudata(l, 1, METATABLE_USER);

	mumble_pushref(l, user->data);
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
	{"getID", user_getID},
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
	{"getListens", user_getListens},
	{"sendPluginData", user_sendPluginData},
	{"requestTextureBlob", user_requestTextureBlob},
	{"requestCommentBlob", user_requestCommentBlob},

	{"__gc", user_gc},
	{"__tostring", user_tostring},
	{"__newindex", user_newindex},
	{"__index", user_index},
	{NULL, NULL}
};
