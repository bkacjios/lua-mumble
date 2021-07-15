#include "mumble.h"

#include "plugindata.h"
#include "channel.h"
#include "user.h"

int mumble_plugindata_new(lua_State *l)
{
	PluginData *pdata = lua_newuserdata(l, sizeof(PluginData));
	pdata->session_list = NULL;

	pdata->data_id = luaL_checklstring(l, 1, &pdata->data_id_len);

	luaL_getmetatable(l, METATABLE_PLUGINDATA);
	lua_setmetatable(l, -2);
	return 1;
}

static int plugindata_setData(lua_State *l)
{
	PluginData *pdata = luaL_checkudata(l, 1, METATABLE_PLUGINDATA);
	pdata->cbdata.data = (uint8_t*) luaL_checklstring(l, 2, &pdata->cbdata.len);
	return 0;
}

static int plugindata_addUser(lua_State *l)
{
	PluginData *pdata = luaL_checkudata(l, 1, METATABLE_PLUGINDATA);

	int type = lua_type(l, 2);
	switch (type) {
		case LUA_TNUMBER:
		{
			// Use direct session number
			uint32_t session = (uint32_t) luaL_checkinteger(l, 2);
			list_add(&pdata->session_list, session);
			break;
		}
		case LUA_TUSERDATA:
		{
			// Make sure the userdata has a user metatable
			MumbleUser *user = luaL_checkudata(l, 2, METATABLE_USER);
			list_add(&pdata->session_list, user->session);
			break;
		}
		default:
			return luaL_argerror(l, 2, "expected " METATABLE_USER " object or session number");
	}

	return 0;
}

static int plugindata_clear(lua_State *l)
{	
	PluginData *pdata = luaL_checkudata(l, 1, METATABLE_PLUGINDATA);
	list_clear(&pdata->session_list);
	return 0;
}

static int plugindata_getSessions(lua_State *l)
{	
	PluginData *pdata = luaL_checkudata(l, 1, METATABLE_PLUGINDATA);

	LinkNode* current = pdata->session_list;

	lua_newtable(l);
	int i = 1;

	while (current != NULL)
	{
		lua_pushnumber(l, i++);
		lua_pushnumber(l, current->data);
		lua_settable(l, -3);
		current = current->next;
	}
	return 1;
}

static int plugindata_tostring(lua_State *l)
{	
	PluginData *pdata = luaL_checkudata(l, 1, METATABLE_PLUGINDATA);
	lua_pushfstring(l, "%s: %p", METATABLE_PLUGINDATA, pdata);
	return 1;
}

const luaL_Reg mumble_plugindata[] = {
	{"setData", plugindata_setData},
	{"addUser", plugindata_addUser},
	{"clear", plugindata_clear},
	{"getSessions", plugindata_getSessions},
	{"__tostring", plugindata_tostring},
	{NULL, NULL}
};