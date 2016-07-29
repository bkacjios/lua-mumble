#include "mumble.h"

int mumble_new(lua_State *l)
{
	const char* certificate_file = luaL_checkstring(l, 1);
	const char* key_file = luaL_checkstring(l, 2);

	MumbleClient *client = lua_newuserdata(l, sizeof(MumbleClient));
	luaL_getmetatable(l, METATABLE_CLIENT);
	lua_setmetatable(l, -2);

	lua_newtable(l);
	client->hooksref = luaL_ref(l, LUA_REGISTRYINDEX);

	lua_newtable(l);
	client->usersref = luaL_ref(l, LUA_REGISTRYINDEX);

	lua_newtable(l);
	client->channelsref = luaL_ref(l, LUA_REGISTRYINDEX);

	client->socket = socket(AF_INET, SOCK_STREAM, 0);
	if (client->socket < 0) {
		lua_pushboolean(l, false);
		lua_pushstring(l, "could not create socket");
		return 2;
	}

	client->ssl_context = SSL_CTX_new(SSLv23_client_method());

	if (client->ssl_context == NULL) {
		lua_pushboolean(l, false);
		lua_pushstring(l, "could not create SSL context");
		return 2;
	}

	if (certificate_file != NULL) {
		if (!SSL_CTX_use_certificate_chain_file(client->ssl_context, certificate_file) ||
			!SSL_CTX_use_PrivateKey_file(client->ssl_context, key_file, SSL_FILETYPE_PEM) ||
			!SSL_CTX_check_private_key(client->ssl_context)) {

			lua_pushboolean(l, false);
			lua_pushstring(l, "could not load certificate and/or key file");
			return 2;
		}
	}
	return 1;
}

void mumble_hook_call(lua_State *l, const char* hook, int nargs)
{
	MumbleClient *client = luaL_checkudata(l, 1, METATABLE_CLIENT);

	lua_rawgeti(l, LUA_REGISTRYINDEX, client->hooksref);
	lua_getfield(l, -1, hook);

	if (lua_isnil(l, -1) == 0 && lua_istable(l, -1)) {
		lua_pushnil(l);

		int i = 0;

		while (lua_next(l, -2)) {
			lua_pushvalue(l, -2);

			if (lua_isfunction(l, -2)) {
				lua_pushvalue(l, -2);
				for (int i = 0; i < nargs; i++) {
					lua_pushvalue(l, 2+i);
				}
				lua_call(l, 1, 0);
			}

			lua_pop(l, 2);
		}
		lua_pop(l, 1);
	}
	lua_pop(l, 1);
}

void mumble_user_get(lua_State *l, uint32_t session)
{
	MumbleClient *client = luaL_checkudata(l, 1, METATABLE_CLIENT);
	lua_rawgeti(l, LUA_REGISTRYINDEX, client->usersref);

	lua_pushinteger(l, session);
	lua_gettable(l, -2);

	if (lua_istable(l, -1) == 0) {
		lua_pop(l, 1);

		lua_pushinteger(l, session);
		lua_newtable(l);
			lua_pushinteger(l, session);
			lua_setfield(l, -2, "session");

			lua_pushvalue(l, 1);
			lua_setfield(l, -2, "client");

			luaL_getmetatable(l, METATABLE_USER);
			lua_setmetatable(l, -2);
		lua_settable(l, -3);

		lua_pushinteger(l, session);
		lua_gettable(l, -2);
	}
	lua_remove(l, -2);
}

void mumble_user_remove(lua_State *l, uint32_t session)
{
	MumbleClient *client = luaL_checkudata(l, 1, METATABLE_CLIENT);
	lua_rawgeti(l, LUA_REGISTRYINDEX, client->usersref);
		lua_pushinteger(l, session);
		lua_pushnil(l);
		lua_settable(l, -3);
	lua_pop(l, 1);
}

void mumble_channel_get(lua_State *l, uint32_t channel_id)
{
	MumbleClient *client = luaL_checkudata(l, 1, METATABLE_CLIENT);
	lua_rawgeti(l, LUA_REGISTRYINDEX, client->channelsref);

	lua_pushinteger(l, channel_id);
	lua_gettable(l, -2);

	if (lua_istable(l, -1) == 0) {
		lua_pop(l, 1);

		lua_pushinteger(l, channel_id);
		lua_newtable(l);
			lua_pushinteger(l, channel_id);
			lua_setfield(l, -2, "channel_id");

			lua_pushvalue(l, 1);
			lua_setfield(l, -2, "client");

			luaL_getmetatable(l, METATABLE_CHAN);
			lua_setmetatable(l, -2);
		lua_settable(l, -3);

		lua_pushinteger(l, channel_id);
		lua_gettable(l, -2);
	}
	lua_remove(l, -2);
}

void mumble_channel_remove(lua_State *l, uint32_t channel_id)
{
	MumbleClient *client = luaL_checkudata(l, 1, METATABLE_CLIENT);
	lua_rawgeti(l, LUA_REGISTRYINDEX, client->channelsref);
		lua_pushinteger(l, channel_id);
		lua_pushnil(l);
		lua_settable(l, -3);
	lua_pop(l, 1);
}

const luaL_reg mumble[] = {
	{"new", mumble_new},
	{NULL, NULL}
};

const luaL_reg mumble_client[] = {
	{"connect", mumble_connect},
	{"auth", mumble_auth},
	{"update", mumble_update},
	{"disconnect", mumble_disconnect},
	{"hook", mumble_hook},
	{"getHooks", mumble_getHooks},
	{"getUsers", mumble_getUsers},
	{"getChannels", mumble_getChannels},
	{"gettime", mumble_gettime},
	{"__gc", mumble_gc},
	{"__tostring", mumble_tostring},
	{"__index", mumble_index},
	{NULL, NULL}
};

const luaL_reg mumble_user[] = {
	{"message", user_message},
	{"kick", user_kick},
	{"ban", user_ban},
	{"move", user_move},
	{"mute", user_mute},
	{"deafen", user_deafen},
	{"comment", user_comment},
	{"requestStats", user_request_stats},
	{"__tostring", user_tostring},
	{NULL, NULL}
};

const luaL_reg mumble_channel[] = {
	{"message", channel_message},
	{"setDescription", channel_setDescription},
	{"remove", channel_remove},
	{"__tostring", channel_tostring},
	{NULL, NULL}
};

int luaopen_mumble(lua_State *l)
{
	SSL_library_init();

	luaL_register(l, "mumble", mumble);
	{
		lua_newtable(l);
		for (int i = 0; i < mumble_proto__reject__reject_type__descriptor.n_values; i++) {
			ProtobufCEnumValueIndex reject = mumble_proto__reject__reject_type__descriptor.values_by_name[i];
			lua_pushnumber(l, reject.index);
			lua_setfield(l, -2, reject.name);
		}
		lua_setfield(l, -2, "reject");

		luaL_newmetatable(l, METATABLE_CLIENT);
		{
			lua_pushvalue(l, -1);
			lua_setfield(l, -2, "__index");
		}
		luaL_register(l, NULL, mumble_client);
		lua_setfield(l, -2, "client");

		luaL_newmetatable(l, METATABLE_USER);
		{
			lua_pushvalue(l, -1);
			lua_setfield(l, -2, "__index");
		}
		luaL_register(l, NULL, mumble_user);
		lua_setfield(l, -2, "user");

		luaL_newmetatable(l, METATABLE_CHAN);
		{
			lua_pushvalue(l, -1);
			lua_setfield(l, -2, "__index");
		}
		luaL_register(l, NULL, mumble_channel);
		lua_setfield(l, -2, "channel");
	}

	return 0;
}
