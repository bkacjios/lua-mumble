#include "mumble.h"

/*--------------------------------
	MUMBLE CLIENT META METHODS
--------------------------------*/

int mumble_connect(lua_State *l)
{
	MumbleClient *client = luaL_checkudata(l, 1, METATABLE_CLIENT);

	const char* server_host_str = luaL_checkstring(l,2);
	int port = luaL_optnumber(l,3, 64738);

	struct hostent *server_host;

	struct sockaddr_in server_addr;

	memset(&server_addr, 0, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(port);

	server_host = gethostbyname(server_host_str);
	if (server_host == NULL || server_host->h_addr_list[0] == NULL || server_host->h_addrtype != AF_INET) {
		lua_pushboolean(l, false);
		lua_pushstring(l, "could not parse server address");
		return 2;
	}
	memmove(&server_addr.sin_addr, server_host->h_addr_list[0], server_host->h_length);

	int ret = connect(client->socket, (struct sockaddr *) &server_addr, sizeof(server_addr));
	if (ret != 0) {
		lua_pushboolean(l, false);
		lua_pushstring(l, "could not connect to server");
		return 2;
	}

	client->ssl = SSL_new(client->ssl_context);

	if (client->ssl == NULL) {
		lua_pushboolean(l, false);
		lua_pushstring(l, "could not create SSL object");
		return 2;
	}

	if (SSL_set_fd(client->ssl, client->socket) == 0) {
		lua_pushboolean(l, false);
		lua_pushstring(l, "could not set SSL file descriptor");
		return 2;
	}

	if (SSL_connect(client->ssl) != 1) {
		lua_pushboolean(l, false);
		lua_pushstring(l, "could not create secure connection");
		return 2;
	}

	int flags = fcntl(client->socket, F_GETFL, 0);
	fcntl(client->socket, F_SETFL, flags | O_NONBLOCK);

	return 1;
}

int mumble_auth(lua_State *l)
{
	MumbleClient *client = luaL_checkudata(l, 1, METATABLE_CLIENT);

	MumbleProto__Version version = MUMBLE_PROTO__VERSION__INIT;
	MumbleProto__Authenticate auth = MUMBLE_PROTO__AUTHENTICATE__INIT;

	auth.has_opus = true;
	auth.opus = true;
	auth.username = (char*) luaL_checkstring(l, 2);
	auth.n_tokens = 0;

	if (lua_isnil(l, 3) != 0) {
		auth.password = (char*) luaL_checkstring(l, 3);
	}

	if (lua_isnil(l, 4) != 0) {
		luaL_checktype(l, 4, LUA_TTABLE);

		lua_pushvalue(l, 4);
		lua_pushnil(l);

		int i = 0;

		while (lua_next(l, -2)) {
			lua_pushvalue(l, -2);

			char *value = (char*) lua_tostring(l, -2);

			auth.tokens[i] = value;

			lua_pop(l, 2);
		}
		lua_pop(l, 1);

		auth.n_tokens = i;
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
	return 0;
}

int mumble_update(lua_State *l)
{
	MumbleClient *client = luaL_checkudata(l, 1, METATABLE_CLIENT);

	double curTime = gettime();

	if (client->nextping < curTime) {
		client->nextping = curTime + PING_TIMEOUT;
		MumbleProto__Ping ping = MUMBLE_PROTO__PING__INIT;
		packet_send(client, PACKET_PING, &ping);
	}

	static Packet packet_read;

	int total_read = 0;

	int ret = SSL_read(client->ssl, packet_read.buffer, 6);

	if (ret <= 0) {
		return 0;
	}
	if (ret != 6) {
		return 0;
	}

	packet_read.type = ntohs(*(uint16_t *)packet_read.buffer);
	if (packet_read.type >= sizeof(packet_handler) / sizeof(Packet_Handler_Func)) {
		return 0;
	}
	packet_read.length = ntohl(*(uint32_t *)(packet_read.buffer + 2));
	if (packet_read.length > PAYLOAD_SIZE_MAX) {
		return 0;
	}

	while (total_read < packet_read.length) {
		ret = SSL_read(client->ssl, packet_read.buffer + total_read, packet_read.length - total_read);
		if (ret <= 0) {
			return 0;
		}
		total_read += ret;
	}

	if (total_read != packet_read.length) {
		return 0;
	}

	Packet_Handler_Func handler = packet_handler[packet_read.type];

	if (handler != NULL) {
		handler(l, &packet_read);
	}

	return 0;
}

int mumble_disconnect(lua_State *l)
{
	MumbleClient *client = luaL_checkudata(l, 1, METATABLE_CLIENT);
	SSL_shutdown(client->ssl);
	close(client->socket);
	return 0;
}

int mumble_hook(lua_State *l)
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

	lua_rawgeti(l, LUA_REGISTRYINDEX, client->hooksref);
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

int mumble_getHooks(lua_State *l)
{
	MumbleClient *client = luaL_checkudata(l, 1, METATABLE_CLIENT);
	lua_rawgeti(l, LUA_REGISTRYINDEX, client->hooksref);
	return 1;
}

int mumble_getUsers(lua_State *l)
{
	MumbleClient *client = luaL_checkudata(l, 1, METATABLE_CLIENT);
	lua_rawgeti(l, LUA_REGISTRYINDEX, client->usersref);
	return 1;
}

int mumble_getChannels(lua_State *l)
{
	MumbleClient *client = luaL_checkudata(l, 1, METATABLE_CLIENT);
	lua_rawgeti(l, LUA_REGISTRYINDEX, client->channelsref);
	return 1;
}

int mumble_gettime(lua_State *l)
{
	lua_pushnumber(l, gettime());
	return 1;
}

int mumble_gc(lua_State *l)
{
	MumbleClient *client = luaL_checkudata(l, 1, METATABLE_CLIENT);
	luaL_unref(l, LUA_REGISTRYINDEX, client->hooksref);
	luaL_unref(l, LUA_REGISTRYINDEX, client->usersref);
	luaL_unref(l, LUA_REGISTRYINDEX, client->channelsref);

	pthread_mutex_lock(&client->lock);
	if (client->audiojob != NULL)
		client->audiojob->done = true;
	pthread_mutex_unlock(&client->lock);
	
	pthread_join(client->audiothread, NULL);

	printf("mumble_gc\n");
	return 0;
}

int mumble_tostring(lua_State *l)
{
	MumbleClient *client = luaL_checkudata(l, 1, METATABLE_CLIENT);
	lua_pushfstring(l, "%s: %p", METATABLE_CLIENT, client);
	return 1;
}

int mumble_index(lua_State *l)
{
	MumbleClient *client = luaL_checkudata(l, 1, METATABLE_CLIENT);

	if (strcmp(lua_tostring(l, 2), "me") == 0 && client->session) {
		mumble_user_get(l, client->session);
		return 1;
	}

	lua_getmetatable(l, 1);
	lua_pushvalue(l, 2);
	lua_gettable(l, -2);
	return 1;
}