#pragma once

#define METATABLE_PLUGINDATA	"mumble.plugindata"

extern int mumble_plugindata_new(lua_State *l);
extern const luaL_Reg mumble_plugindata[];