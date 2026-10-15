#include "lua/luautil.h"

#include "defines.h"
#include "lua/luainc.h"
#include "util/log.h"
#include "util/platform.h"

#include <cctype>
#include <cstdio>

void lua_pushoptstring(lua_State *l, const std::optional<std::string> &str) {
	if (str) {
		lua_pushstring(l, str->c_str());
	} else {
		lua_pushnil(l);
	}
}

/*
static void iterate_and_print(lua_State *L, int index) {
	// Push another reference to the table on top of the stack (so we know
	// where it is, and this function can work for negative, positive and
	// pseudo indices
	lua_pushvalue(L, index);
	// stack now contains: -1 => table
	lua_pushnil(L);
	// stack now contains: -1 => nil; -2 => table
	while (lua_next(L, -2)) {
		// stack now contains: -1 => value; -2 => key; -3 => table
		// copy the key so that lua_tostring does not modify the original
		lua_pushvalue(L, -2);
		// stack now contains: -1 => key; -2 => value; -3 => key; -4 => table
		const char *key = lua_tostring(L, -1);
		const char *value = lua_tostring(L, -2);
		printf("%s => %s" NEWLINE, key, value);
		// pop value + copy of key, leaving original key
		lua_pop(L, 2);
		// stack now contains: -1 => key; -2 => table
	}
	// stack now contains: -1 => table (when lua_next returns 0 it pops the key
	// but does not push anything.)
	// Pop table
	lua_pop(L, 1);
	// Stack is now the same as it was on entry to this function
}

*/

void print_unescaped(const char* ptr, int len) {
	if (!ptr) return;
	for (int i = 0; i < len; i++, ptr++) {
		switch (*ptr) {
		case '\0': printf("\\0");  break;
		case '\a': printf("\\a");  break;
		case '\b': printf("\\b");  break;
		case '\f': printf("\\f");  break;
		case '\n': printf("\\n");  break;
		case '\r': printf("\\r");  break;
		case '\t': printf("\\t");  break;
		case '\v': printf("\\v");  break;
		case '\\': printf("\\\\"); break;
		case '\?': printf("\\\?"); break;
		case '\'': printf("\\\'"); break;
		case '\"': printf("\\\""); break;
		default:
			if (isprint(*ptr)) printf("%c",     *ptr);
			else               printf("\\%03o", *ptr);
		}
	}
}

#if (defined(LUA_VERSION_NUM) && LUA_VERSION_NUM < 502) && !defined(LUAJIT)

// luaL_testudata and luaL_traceback were introduced in Lua 5.2
// LuaJit also has their own implementations

void* luaL_testudata(lua_State* L, int index, const char* tname) {
	if (!lua_isuserdata(L, index)) {
		return NULL; // Not userdata
	}

	if (!lua_getmetatable(L, index)) {
		return NULL; // No metatable
	}

	luaL_getmetatable(L, tname); // Push the expected metatable

	if (!lua_rawequal(L, -1, -2)) {
		lua_pop(L, 2); // Pop both metatables
		return NULL;   // Metatables do not match
	}

	lua_pop(L, 2); // Pop both metatables
	return lua_touserdata(L, index); // Return the userdata
}

void luaL_traceback(lua_State* L, lua_State* L1, const char* msg, int level) {
	lua_Debug ar;
	luaL_Buffer b;
	luaL_buffinit(L, &b);

	if (msg) {
		luaL_addstring(&b, msg);
		luaL_addstring(&b, NEWLINE);
	}

	luaL_addstring(&b, "stack traceback:" NEWLINE);

	// Start traversing the call stack from the specified level
	while (lua_getstack(L1, level++, &ar)) {
		if (lua_getinfo(L1, "Slnf", &ar)) {
			luaL_addstring(&b, "\t"); // Indent for stack trace entries

			// Add source and line number
			if (ar.currentline > 0) {
				luaL_addstring(&b, ar.short_src);
				luaL_addstring(&b, ":");
				luaL_addstring(&b, lua_pushfstring(L, "%d", ar.currentline));
				lua_pop(L, 1); // Pop the formatted string
			} else {
				luaL_addstring(&b, "[C]");
			}

			// Add function information
			if (*ar.namewhat != '\0') {
				luaL_addstring(&b, ": in function '");
				luaL_addstring(&b, (ar.name ? ar.name : "?"));
				luaL_addstring(&b, "'");
			} else if (*ar.what == 'm') {
				luaL_addstring(&b, ": in main chunk");
			} else if (*ar.what == 'C') {
				const void* func_ptr = lua_topointer(L1, -1); // Function is already on the stack
				luaL_addstring(&b, ": at ");
				luaL_addstring(&b, lua_pushfstring(L, "%p", func_ptr));
				lua_pop(L, 1); // Pop the formatted string
			} else {
				luaL_addstring(&b, ": in function <");
				luaL_addstring(&b, ar.short_src);
				luaL_addstring(&b, ":");
				luaL_addstring(&b, lua_pushfstring(L, "%d", ar.linedefined));
				lua_pop(L, 1); // Pop the formatted string
				luaL_addstring(&b, ">");
			}

			luaL_addstring(&b, NEWLINE);
			lua_pop(L1, 1); // Pop the function pushed by "f" from the stack
		}
	}

	luaL_pushresult(&b); // Push the accumulated buffer as a string
}

#endif

void luaL_debugstack(lua_State *l, const char* text) {
	mumble_log(LOG_DEBUG, "%s stack dump", text);
	for (int i = 1; i <= lua_gettop(l); i++) {
		int t = lua_type(l, i);
		size_t len;
		const char* tname = lua_typename(l, t);
		switch (t) {
		case LUA_TSTRING: { /* strings */
			const char* str = lua_tolstring(l, i, &len);
			if (len < 64) {
				printf("\t%d - %s[\"", i, tname);
				print_unescaped(str, len);
				printf("\"]" NEWLINE);
				print_unescaped(str, len);
				printf("\"]" NEWLINE);
			} else {
				printf("\t%d - %s[%zu]" NEWLINE, i, tname, len);
			}
			break;
		}
		case LUA_TBOOLEAN:  /* booleans */
			printf("\t%d - %s[%s]" NEWLINE, i, tname, lua_toboolean(l, i) ? "true" : "false");
			break;
		case LUA_TNUMBER:  /* numbers */
			printf("\t%d - %s[%g]" NEWLINE, i, tname, lua_tonumber(l, i));
			break;
		default:  /* other values */
			printf("\t%d - %s[%p]" NEWLINE, i, tname, lua_topointer(l, i));
			break;
		}
	}
}

int luaL_typerror(lua_State *L, int narg, const char *tname) {
	const char *msg = lua_pushfstring(L, "%s expected, got %s", tname, luaL_typename(L, narg));
	return luaL_argerror(L, narg, msg);
}

int luaL_typerror_table(lua_State *L, int narg, int nkey, int nvalue, const char *tname) {
	const char *msg = lua_pushfstring(L, "bad table entry '%s' (%s expected, got %s)", lua_tostring(L, nkey), tname, luaL_typename(L, nvalue));
	return luaL_argerror(L, narg, msg);
}

int luaL_checkfunction(lua_State *L, int i) {
	if (!lua_isfunction(L, i))
		luaL_typerror(L, i, lua_typename(L, LUA_TFUNCTION));
	return lua_isfunction(L, i);
}

int luaL_checkboolean(lua_State *L, int i) {
	if (!lua_isboolean(L, i))
		luaL_typerror(L, i, lua_typename(L, LUA_TBOOLEAN));
	return lua_toboolean(L, i);
}

int luaL_optboolean(lua_State *L, int i, int d) {
	if (lua_type(L, i) < 1)
		return d;
	else
		return luaL_checkboolean(L, i);
}

int luaL_isudata(lua_State *L, int ud, const char *tname) {
	if (lua_isuserdata(L, ud)) { // value is a userdata?
		if (lua_getmetatable(L, ud)) { // does it have a metatable?
			int equal;
			lua_getfield(L, LUA_REGISTRYINDEX, tname); // get correct metatable
			equal = lua_rawequal(L, -1, -2); // does it have the correct mt?
			lua_pop(L, 2); // remove both metatables
			return equal;
		}
	}
	return 0; // else false
}

int lua_pushaddress(lua_State* l, ProtobufCBinaryData address) {
	lua_newtable(l);
	uint8_t* bytes = (uint8_t*) address.data;
	uint64_t* addr = (uint64_t*) address.data;
	uint16_t* shorts = (uint16_t*) address.data;

	if (addr[0] != 0ULL || shorts[4] != 0 || shorts[5] != 0xFFFF) {
		char ipv6[INET6_ADDRSTRLEN];

		if (!inet_ntop(AF_INET6, address.data, ipv6, sizeof(ipv6))) {
			// Fallback
			sprintf(ipv6, "%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x",
			        bytes[0], bytes[1], bytes[2], bytes[3],
			        bytes[4], bytes[5], bytes[5], bytes[7],
			        bytes[8], bytes[9], bytes[10], bytes[11],
			        bytes[12], bytes[13], bytes[14], bytes[15]);
		}

		lua_pushboolean(l, true);
		lua_setfield(l, -2, "ipv6");
		lua_pushstring(l, ipv6);
		lua_setfield(l, -2, "string");
	} else {
		char ipv4[INET_ADDRSTRLEN];

		if (!inet_ntop(AF_INET, address.data + 12, ipv4, sizeof(ipv4))) {
			// Fallback
			sprintf(ipv4, "%d.%d.%d.%d", bytes[12], bytes[13], bytes[14], bytes[15]);
		}

		lua_pushboolean(l, true);
		lua_setfield(l, -2, "ipv4");
		lua_pushstring(l, ipv4);
		lua_setfield(l, -2, "string");
	}

	lua_newtable(l);
	for (uint32_t k = 0; k < address.len; k++) {
		lua_pushinteger(l, k + 1);
		lua_pushinteger(l, address.data[k]);
		lua_settable(l, -3);
	}
	lua_setfield(l, -2, "data");

	return 1;
}

std::vector<const char*> luaL_checkstrings(lua_State *l, int index) {
	luaL_checktype(l, index, LUA_TTABLE);
	std::vector<const char*> strings;
	lua_pushvalue(l, index);
	lua_pushnil(l);
	while (lua_next(l, -2)) {
		const char *value = lua_tostring(l, -1);
		if (value) strings.push_back(value);
		lua_pop(l, 1);
	}
	lua_pop(l, 1);
	return strings;
}
