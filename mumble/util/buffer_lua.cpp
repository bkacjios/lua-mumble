#include "util/buffer.h"

#include "lua/bindings.h"
#include "lua/luainc.h"
#include "lua/luautil.h"
#include "lua/metatables.h"
#include "util/log.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

ByteBuffer* luabuffer_new(lua_State *l) {
	ByteBuffer* buffer = lua_newclass<ByteBuffer>(l);
	luaL_getmetatable(l, METATABLE_BUFFER);
	lua_setmetatable(l, -2);
	return buffer;
}

int mumble_buffer_new(lua_State *l) {
	int type = lua_type(l, 2);

	ByteBuffer *buffer = luabuffer_new(l);

	if (buffer == NULL) return luaL_error(l, "error creating buffer: %s", strerror(errno));

	const char *msg = NULL;

	switch (type) {
	case LUA_TNUMBER:
		// Initialize with a given allocation size
		buffer = buffer->init(lua_tointeger(l, 2));
		break;
	case LUA_TSTRING: {
		// Initialize with raw data
		size_t size;
		const char* data = lua_tolstring(l, 2, &size);
		buffer = buffer->initData(data, size);
		break;
	}
	case LUA_TNONE:
		// No argument given, allocate 1K by default
		buffer = buffer->init(1024);
		break;
	default:
		msg = lua_pushfstring(l, "%s or %s expected, got %s",
							  lua_typename(l, LUA_TNUMBER), lua_typename(l, LUA_TSTRING), luaL_typename(l, 2));
		return luaL_argerror(l, 1, msg);
	}

	if (buffer == NULL) return luaL_error(l, "error initializing buffer: %s", strerror(errno));

	// Return the buffer metatable
	return 1;
}

int luabuffer_pack(lua_State *l) {
	ByteBuffer *buffer = luaL_checkclass<ByteBuffer>(l, 1, METATABLE_BUFFER);
	buffer->pack();
	return 0;
}

int luabuffer_reset(lua_State *l) {
	ByteBuffer *buffer = luaL_checkclass<ByteBuffer>(l, 1, METATABLE_BUFFER);
	buffer->reset();
	return 0;
}

int luabuffer_flip(lua_State *l) {
	ByteBuffer *buffer = luaL_checkclass<ByteBuffer>(l, 1, METATABLE_BUFFER);
	buffer->flip();
	return 0;
}

int luabuffer_readHead(lua_State *l) {
	ByteBuffer *buffer = luaL_checkclass<ByteBuffer>(l, 1, METATABLE_BUFFER);
	lua_pushinteger(l, buffer->read_head);
	return 1;
}

int luabuffer_writeHead(lua_State *l) {
	ByteBuffer *buffer = luaL_checkclass<ByteBuffer>(l, 1, METATABLE_BUFFER);
	lua_pushinteger(l, buffer->write_head);
	return 1;
}

int luabuffer_write(lua_State *l) {
	ByteBuffer *buffer = luaL_checkclass<ByteBuffer>(l, 1, METATABLE_BUFFER);

	switch (lua_type(l, 2)) {
	case LUA_TUSERDATA: {
		ByteBuffer* in = luaL_checkclass<ByteBuffer>(l, 2, METATABLE_BUFFER);
		lua_pushinteger(l, buffer->write(in->data + in->read_head, in->length()));
		return 1;
	}
	case LUA_TSTRING: {
		size_t size;
		const char* data = lua_tolstring(l, 2, &size);
		lua_pushinteger(l, buffer->write(data, size));
		return 1;
	}
	default: {
		const char *msg = lua_pushfstring(l, "%s or %s expected, got %s",
										  lua_typename(l, LUA_TSTRING), METATABLE_BUFFER, luaL_typename(l, 2));
		return luaL_argerror(l, 1, msg);
	}
	}
}

int luabuffer_read(lua_State *l) {
	ByteBuffer *buffer = luaL_checkclass<ByteBuffer>(l, 1, METATABLE_BUFFER);
	int nargs = lua_gettop(l) - 1;  // Get number of arguments

	for (int i = 2; i < nargs + 2; i++) {
		size_t size = 0;

		if (lua_type(l, i) == LUA_TNUMBER) {
			// If the argument is a number, read that many bytes
			size = (size_t) lua_tointeger(l, i);
		} else {
			const char *arg = lua_tostring(l, i);
			if (arg && arg[0] == '*' && arg[1] == 'a') {
				// If the argument is "*a", read the entire buffer
				size = buffer->length();
			} else {
				return luaL_argerror(l, i, "invalid format");
			}
		}

		const uint8_t *read = buffer->consume(size);
		if (read == NULL) {
			return luaL_error(l, "attempt to read beyond buffer limit");
		}

		lua_pushlstring(l, (const char*) read, size);
	}

	return nargs;
}

int luabuffer_writeByte(lua_State *l) {
	ByteBuffer *buffer = luaL_checkclass<ByteBuffer>(l, 1, METATABLE_BUFFER);
	lua_pushinteger(l, buffer->writeByte(luaL_checkinteger(l, 2)));
	return 1;
}

int luabuffer_readByte(lua_State *l) {
	ByteBuffer *buffer = luaL_checkclass<ByteBuffer>(l, 1, METATABLE_BUFFER);
	uint8_t value;

	if (!buffer->readByte(&value)) {
		return luaL_error(l, "attempt to read beyond buffer limit");
	}

	lua_pushinteger(l, value);
	return 1;
}

int luabuffer_writeShort(lua_State *l) {
	ByteBuffer *buffer = luaL_checkclass<ByteBuffer>(l, 1, METATABLE_BUFFER);
	lua_pushinteger(l, buffer->writeValue<int16_t>(luaL_checkinteger(l, 2)));
	return 1;
}

int luabuffer_readShort(lua_State *l) {
	ByteBuffer *buffer = luaL_checkclass<ByteBuffer>(l, 1, METATABLE_BUFFER);
	int16_t value;

	if (!buffer->readValue<int16_t>(&value)) {
		return luaL_error(l, "attempt to read beyond buffer limit");
	}

	lua_pushinteger(l, value);
	return 1;
}

int luabuffer_writeInt(lua_State *l) {
	ByteBuffer *buffer = luaL_checkclass<ByteBuffer>(l, 1, METATABLE_BUFFER);
	lua_pushinteger(l, buffer->writeValue<int32_t>(luaL_checkinteger(l, 2)));
	return 1;
}

int luabuffer_readInt(lua_State *l) {
	ByteBuffer *buffer = luaL_checkclass<ByteBuffer>(l, 1, METATABLE_BUFFER);
	int32_t value;

	if (!buffer->readValue<int32_t>(&value)) {
		return luaL_error(l, "attempt to read beyond buffer limit");
	}

	lua_pushinteger(l, value);
	return 1;
}

int luabuffer_writeVarInt(lua_State *l) {
	ByteBuffer *buffer = luaL_checkclass<ByteBuffer>(l, 1, METATABLE_BUFFER);
	lua_pushinteger(l, buffer->writeVarInt(luaL_checkinteger(l, 2)));
	return 1;
}

int luabuffer_readVarInt(lua_State *l) {
	ByteBuffer *buffer = luaL_checkclass<ByteBuffer>(l, 1, METATABLE_BUFFER);
	uint64_t value;

	if (!buffer->readVarInt(&value)) {
		return luaL_error(l, "attempt to read beyond buffer limit");
	}

	lua_pushinteger(l, value);
	return 1;
}

int luabuffer_writeFloat(lua_State *l) {
	ByteBuffer *buffer = luaL_checkclass<ByteBuffer>(l, 1, METATABLE_BUFFER);
	lua_pushinteger(l, buffer->writeValue<float>(luaL_checknumber(l, 2)));
	return 1;
}

int luabuffer_readFloat(lua_State *l) {
	ByteBuffer *buffer = luaL_checkclass<ByteBuffer>(l, 1, METATABLE_BUFFER);
	float value;

	if (!buffer->readValue<float>(&value)) {
		return luaL_error(l, "attempt to read beyond buffer limit");
	}

	lua_pushnumber(l, value);
	return 1;
}

int luabuffer_writeDouble(lua_State *l) {
	ByteBuffer *buffer = luaL_checkclass<ByteBuffer>(l, 1, METATABLE_BUFFER);
	lua_pushinteger(l, buffer->writeValue<double>(luaL_checknumber(l, 2)));
	return 1;
}

int luabuffer_readDouble(lua_State *l) {
	ByteBuffer *buffer = luaL_checkclass<ByteBuffer>(l, 1, METATABLE_BUFFER);
	double value;

	if (!buffer->readValue<double>(&value)) {
		return luaL_error(l, "attempt to read beyond buffer limit");
	}

	lua_pushnumber(l, value);
	return 1;
}

int luabuffer_writeString(lua_State *l) {
	ByteBuffer *buffer = luaL_checkclass<ByteBuffer>(l, 1, METATABLE_BUFFER);
	size_t size;
	const char* string = luaL_checklstring(l, 2, &size);
	lua_pushinteger(l, buffer->writeString(string, size));
	return 1;
}

int luabuffer_readString(lua_State *l) {
	ByteBuffer *buffer = luaL_checkclass<ByteBuffer>(l, 1, METATABLE_BUFFER);
	uint64_t size;
	const uint8_t *string = buffer->readString(&size);

	if (string == NULL) {
		return luaL_error(l, "attempt to read beyond buffer limit");
	}

	lua_pushlstring(l, (const char*) string, size);
	return 1;
}

int luabuffer_writeBool(lua_State *l) {
	ByteBuffer *buffer = luaL_checkclass<ByteBuffer>(l, 1, METATABLE_BUFFER);
	lua_pushinteger(l, buffer->writeByte(luaL_checkboolean(l, 2)));
	return 1;
}

int luabuffer_readBool(lua_State *l) {
	ByteBuffer *buffer = luaL_checkclass<ByteBuffer>(l, 1, METATABLE_BUFFER);
	uint8_t value;

	if (!buffer->readByte(&value)) {
		return luaL_error(l, "attempt to read beyond buffer limit");
	}

	lua_pushboolean(l, value);
	return 1;
}

int luabuffer_isEmpty(lua_State *l) {
	ByteBuffer *buffer = luaL_checkclass<ByteBuffer>(l, 1, METATABLE_BUFFER);
	lua_pushboolean(l, buffer->isEmpty());
	return 1;
}

static int luabuffer_seek(lua_State *l) {
	ByteBuffer *buffer = luaL_checkclass<ByteBuffer>(l, 1, METATABLE_BUFFER);

	static const char *const modes[] = {"read", "write", "both", NULL};
	static const int heads[] = {ByteBuffer::SEEK_READ_HEAD, ByteBuffer::SEEK_WRITE_HEAD, ByteBuffer::SEEK_READ_HEAD | ByteBuffer::SEEK_WRITE_HEAD};
	static const char *const options[] = {"set", "cur", "end", NULL};
	static const int whence[] = {SEEK_SET, SEEK_CUR, SEEK_END};

	int mode = heads[luaL_checkoption(l, 2, "read", modes)];
	int option = whence[luaL_checkoption(l, 3, "cur", options)];
	buffer->seek(mode, option, luaL_optlong(l, 4, 0));

	if (mode & ByteBuffer::SEEK_READ_HEAD) lua_pushinteger(l, buffer->read_head);
	if (mode & ByteBuffer::SEEK_WRITE_HEAD) lua_pushinteger(l, buffer->write_head);
	return mode == (ByteBuffer::SEEK_READ_HEAD | ByteBuffer::SEEK_WRITE_HEAD) ? 2 : 1;
}

int luabuffer_len(lua_State *l) {
	ByteBuffer *buffer = luaL_checkclass<ByteBuffer>(l, 1, METATABLE_BUFFER);
	lua_pushinteger(l, buffer->length());
	return 1;
}

int luabuffer_index(lua_State *l) {
	ByteBuffer *buffer = luaL_checkclass<ByteBuffer>(l, 1, METATABLE_BUFFER);

	switch (lua_type(l, 2)) {
	case LUA_TNUMBER: {
		// Read specific character
		const uint8_t *byte = buffer->at(lua_tointeger(l, 2));
		if (byte != NULL) {
			lua_pushlstring(l, (const char*) byte, 1);
		} else {
			lua_pushnil(l);
		}
		return 1;
	}
	case LUA_TSTRING: {
		const char *key = lua_tostring(l, 2);
		// Custom length and capactity index
		if (strcmp(key, "length") == 0) {
			lua_pushinteger(l, buffer->length());
			return 1;
		} else if (strcmp(key, "capacity") == 0) {
			lua_pushinteger(l, buffer->capacity);
			return 1;
		} else if (strcmp(key, "read_head") == 0) {
			lua_pushinteger(l, buffer->read_head);
			return 1;
		} else if (strcmp(key, "write_head") == 0) {
			lua_pushinteger(l, buffer->write_head);
			return 1;
		}
		// Metatable lookups
		lua_getmetatable(l, 1);
		lua_getfield(l, -1, key);
		if (!lua_isnil(l, -1)) {
			return 1;
		}
		return 0;
	}
	default:
		return 0;
	}
}

int luabuffer_tostring(lua_State *l) {
	lua_pushfstring(l, "%s: %p", METATABLE_BUFFER, lua_topointer(l, 1));
	return 1;
}

int luabuffer_gc(lua_State *l) {
	ByteBuffer *buffer = luaL_checkclass<ByteBuffer>(l, 1, METATABLE_BUFFER);
	mumble_log(LOG_DEBUG, "%s: %p garbage collected", METATABLE_BUFFER, buffer);
	buffer->free();
	buffer->~ByteBuffer();
	return 0;
}

const luaL_Reg mumble_buffer[] = {
	{"pack", luabuffer_pack},
	{"reset", luabuffer_reset},
	{"flip", luabuffer_flip},
	{"write", luabuffer_write},
	{"read", luabuffer_read},
	{"writeByte", luabuffer_writeByte},
	{"readByte", luabuffer_readByte},
	{"writeShort", luabuffer_writeShort},
	{"readShort", luabuffer_readShort},
	{"writeInt", luabuffer_writeInt},
	{"readInt", luabuffer_readInt},
	{"writeVarInt", luabuffer_writeVarInt},
	{"readVarInt", luabuffer_readVarInt},
	{"writeFloat", luabuffer_writeFloat},
	{"readFloat", luabuffer_readFloat},
	{"writeDouble", luabuffer_writeDouble},
	{"readDouble", luabuffer_readDouble},
	{"writeString", luabuffer_writeString},
	{"readString", luabuffer_readString},
	{"writeBool", luabuffer_writeBool},
	{"readBool", luabuffer_readBool},
	{"writeBoolean", luabuffer_writeBool},
	{"readBoolean", luabuffer_readBool},
	{"isEmpty", luabuffer_isEmpty},
	{"seek", luabuffer_seek},
	{"__len", luabuffer_len},
	{"__index", luabuffer_index},
	{"__tostring", luabuffer_tostring},
	{"__gc", luabuffer_gc},
	{NULL, NULL}
};
