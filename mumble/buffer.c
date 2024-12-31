#include "mumble.h"

#include "buffer.h"
#include "util.h"
#include "log.h"

#include <stdlib.h>
#include <string.h>

ByteBuffer* buffer_new(uint64_t size)
{
	ByteBuffer* buffer = malloc(sizeof(ByteBuffer));
	if(buffer == NULL) return NULL;
	buffer_init(buffer, size);
	return buffer;
}

ByteBuffer* buffer_init(ByteBuffer* buffer, uint64_t size)
{
	buffer->capacity = size;
	buffer->limit = 0;
	buffer->position = 0;
	buffer->data = malloc(sizeof(uint8_t) * size);
	if(buffer->data == NULL) return NULL;
	return buffer;
}

ByteBuffer* buffer_init_data(ByteBuffer* buffer, const char* data, uint64_t size) {
	buffer = buffer_init(buffer, size);
	if (buffer != NULL) {
		buffer_write(buffer, data, size);
		buffer_flip(buffer);
	}
	return buffer;
}

static void buffer_adjust(ByteBuffer* buffer, uint64_t size)
{
	uint64_t new_position = buffer->position + size;
	if (new_position > buffer->capacity) {
		uint64_t new_capacity = buffer->capacity + (buffer->capacity / 2);
		while (new_position > new_capacity) {
			new_capacity += (new_capacity / 2);
		}
		buffer->data = realloc(buffer->data, new_capacity);
		if (buffer->data != NULL) {
			mumble_log(LOG_DEBUG, "%s: %p resizing from %llu to %llu bytes", METATABLE_BUFFER, buffer, buffer->capacity, new_capacity);
			buffer->capacity = new_capacity;
		} else {
			mumble_log(LOG_ERROR, "%s: %p failed to resize buffer", METATABLE_BUFFER, buffer);
		}
	}
}

void buffer_free(ByteBuffer* buffer)
{
	free(buffer->data);
	free(buffer);
}

void buffer_clear(ByteBuffer* buffer)
{
	memset(buffer->data, 0, buffer->capacity);
	buffer->position = 0;
	buffer->limit = 0;
}

void buffer_compact(ByteBuffer* buffer)
{
	uint64_t remaining = buffer->limit - buffer->position;
	if (remaining > 0) {
		memmove(buffer->data, buffer->data + buffer->position, remaining);
		buffer->position = remaining;
	}
	buffer->limit = buffer->capacity;
}

void buffer_reset(ByteBuffer* buffer)
{
	buffer->position = 0;
	buffer->limit = buffer->capacity;
}

void buffer_flip(ByteBuffer* buffer)
{
	buffer->limit = buffer->position;
	buffer->position = 0;
}

uint64_t buffer_write(ByteBuffer* buffer, const void* data, uint64_t size)
{
	buffer_adjust(buffer, size);
	memcpy(buffer->data + buffer->position, data, size);
	buffer->position += size;
	buffer->limit = buffer->position;
	return size;
}

uint64_t buffer_read(ByteBuffer* buffer, void* output, uint64_t size)
{
	buffer_available(buffer, size);
	memmove(output, buffer->data + buffer->position, size);
	buffer->position += size;
	return size;
}

uint8_t buffer_writeByte(ByteBuffer* buffer, uint8_t value)
{
	buffer_adjust(buffer, 1);
	buffer->data[buffer->position++] = value;
	return 1;
}

uint8_t buffer_readByte(ByteBuffer* buffer, int8_t* output)
{
	buffer_available(buffer, 1);
	*output = buffer->data[buffer->position++];
	return 1;
}

buffer_rw(UnsignedByte,uint8_t);
buffer_rw(Short,int16_t);
buffer_rw(UnsignedShort,uint16_t);
buffer_rw(Int,int32_t);
buffer_rw(UnsignedInt,uint32_t);
buffer_rw(Long,int64_t);
buffer_rw(Float,float);
buffer_rw(Double,double);

uint8_t buffer_writeVarInt(ByteBuffer* buffer, uint64_t value)
{
	if (value < 0x80) {
		buffer_adjust(buffer, 1);
		buffer->data[buffer->position++] = value;
		return 1;
	} else if (value < 0x4000) {
		buffer_adjust(buffer, 2);
		buffer->data[buffer->position++] = (value >> 8) | 0x80;
		buffer->data[buffer->position++] = value & 0xFF;
		return 2;
	} else if (value < 0x200000) {
		buffer_adjust(buffer, 3);
		buffer->data[buffer->position++] = (value >> 16) | 0xC0;
		buffer->data[buffer->position++] = (value >> 8) & 0xFF;
		buffer->data[buffer->position++] = value & 0xFF;
		return 3;
	} else if (value < 0x10000000) {
		buffer_adjust(buffer, 4);
		buffer->data[buffer->position++] = (value >> 24) | 0xE0;
		buffer->data[buffer->position++] = (value >> 16) & 0xFF;
		buffer->data[buffer->position++] = (value >> 8) & 0xFF;
		buffer->data[buffer->position++] = value & 0xFF;
		return 4;
	} else if (value < 0x100000000) {
		buffer_adjust(buffer, 5);
		buffer->data[buffer->position++] = 0xF0;
		buffer->data[buffer->position++] = (value >> 24) & 0xFF;
		buffer->data[buffer->position++] = (value >> 16) & 0xFF;
		buffer->data[buffer->position++] = (value >> 8) & 0xFF;
		buffer->data[buffer->position++] = value & 0xFF;
		return 5;
	} else {
		buffer_adjust(buffer, 9);
		buffer->data[buffer->position++] = 0xF4;
		buffer->data[buffer->position++] = (value >> 56) & 0xFF;
		buffer->data[buffer->position++] = (value >> 48) & 0xFF;
		buffer->data[buffer->position++] = (value >> 40) & 0xFF;
		buffer->data[buffer->position++] = (value >> 32) & 0xFF;
		buffer->data[buffer->position++] = (value >> 24) & 0xFF;
		buffer->data[buffer->position++] = (value >> 16) & 0xFF;
		buffer->data[buffer->position++] = (value >> 8) & 0xFF;
		buffer->data[buffer->position++] = value & 0xFF;
		return 9;
	}
}

uint8_t buffer_readVarInt(ByteBuffer* buffer, uint64_t* output)
{
	*output = 0;

	buffer_available(buffer, 1);
	uint8_t v = buffer->data[buffer->position++];

	uint8_t size = 0;

	if ((v & 0x80) == 0x00) {
		*output = (v & 0x7F);
		size += 1;
	} else if ((v & 0xC0) == 0x80) {
		buffer_available(buffer, 1);
		*output = (v & 0x3F) << 8 | buffer->data[buffer->position++];
		size += 2;
	} else if ((v & 0xF0) == 0xF0) {
		switch (v & 0xFC) {
			case 0xF0:
				buffer_available(buffer, 4);
				*output = buffer->data[buffer->position++] << 24 
				| buffer->data[buffer->position++] << 16 
				| buffer->data[buffer->position++] << 8 
				| buffer->data[buffer->position++];
				size += 5;
				break;
			case 0xF4:
				buffer_available(buffer, 8);
				*output = (uint64_t)buffer->data[buffer->position++] << 56 
				| (uint64_t)buffer->data[buffer->position++] << 48 
				| (uint64_t)buffer->data[buffer->position++] << 40 
				| (uint64_t)buffer->data[buffer->position++] << 32 
				| buffer->data[buffer->position++] << 24 
				| buffer->data[buffer->position++] << 16 
				| buffer->data[buffer->position++] << 8 
				| buffer->data[buffer->position++];
				size += 9;
				break;
			case 0xF8:
				*output = ~*output;
				size += 1;
				break;
			case 0xFC:
				*output = v & 0x03;
				*output = ~*output;
				size += 1;
				break;
			default:
				*output = 0;
				size += 1;
				break;
		}
	} else if ((v & 0xF0) == 0xE0) {
		buffer_available(buffer, 3);
		*output=(v & 0x0F) << 24 | buffer->data[buffer->position++] << 16 | buffer->data[buffer->position++] << 8 | buffer->data[buffer->position++];
		size += 4;
	} else if ((v & 0xE0) == 0xC0) {
		buffer_available(buffer, 2);
		*output=(v & 0x1F) << 16 | buffer->data[buffer->position++] << 8 | buffer->data[buffer->position++];
		size += 3;
	}

	return size;
}

int mumble_buffer_new(lua_State *l)
{
	int type = lua_type(l, 2);

	ByteBuffer *buffer = lua_newuserdata(l, sizeof(ByteBuffer));
	buffer->data = NULL;
	luaL_getmetatable(l, METATABLE_BUFFER);
	lua_setmetatable(l, -2);

	if(buffer == NULL) return luaL_error(l, "error creating buffer: %s", strerror(errno));

	switch (type) {
		case LUA_TNUMBER:
			// Initialize with a given allocation size
			buffer = buffer_init(buffer, luaL_checkinteger(l, 2));
			break;
		case LUA_TSTRING:
			// Initialize with raw data
			size_t size;
			const char* data = luaL_checklstring(l, 2, &size);
			buffer = buffer_init_data(buffer, data, size);
			break;
		case LUA_TNONE:
			// No argument given, allocate 1K by default
			buffer = buffer_init(buffer, 1024);
			break;
		default:
			const char *msg = lua_pushfstring(l, "%s or %s expected, got %s",
				lua_typename(l, LUA_TNUMBER), lua_typename(l, LUA_TSTRING), luaL_typename(l, 2));
			return luaL_argerror(l, 1, msg);
	}

	if(buffer == NULL) return luaL_error(l, "error initializing buffer: %s", strerror(errno));

	// Return the buffer metatable
	return 1;
}

int luabuffer_clear(lua_State *l)
{
	ByteBuffer *buffer = luaL_checkudata(l, 1, METATABLE_BUFFER);
	buffer_clear(buffer);
	return 0;
}

int luabuffer_compact(lua_State *l)
{
	ByteBuffer *buffer = luaL_checkudata(l, 1, METATABLE_BUFFER);
	buffer_compact(buffer);
	return 0;
}

int luabuffer_reset(lua_State *l)
{
	ByteBuffer *buffer = luaL_checkudata(l, 1, METATABLE_BUFFER);
	buffer_reset(buffer);
	return 0;
}

int luabuffer_flip(lua_State *l)
{
	ByteBuffer *buffer = luaL_checkudata(l, 1, METATABLE_BUFFER);
	buffer_flip(buffer);
	return 0;
}

int luabuffer_capacity(lua_State *l)
{
	ByteBuffer *buffer = luaL_checkudata(l, 1, METATABLE_BUFFER);
	lua_pushinteger(l, buffer->capacity);
	return 1;
}

int luabuffer_position(lua_State *l)
{
	ByteBuffer *buffer = luaL_checkudata(l, 1, METATABLE_BUFFER);
	lua_pushinteger(l, buffer->position);
	return 1;
}

int luabuffer_limit(lua_State *l)
{
	ByteBuffer *buffer = luaL_checkudata(l, 1, METATABLE_BUFFER);
	lua_pushinteger(l, buffer->limit);
	return 1;
}

int luabuffer_write(lua_State *l)
{
	ByteBuffer *buffer = luaL_checkudata(l, 1, METATABLE_BUFFER);
	size_t size;
	const char* data = luaL_checklstring(l, 2, &size);
	lua_pushinteger(l, buffer_write(buffer, data, size));
	return 1;
}

int luabuffer_read(lua_State *l)
{
	ByteBuffer *buffer = luaL_checkudata(l, 1, METATABLE_BUFFER);
	uint64_t size = luaL_checkinteger(l, 2);
	char read[size];

	if (!buffer_read(buffer, &read, size)) {
		return luaL_error(l, "attempt to read beyond buffer limit ");
	}

	lua_pushlstring(l, read, size);
	return 1;
}

int luabuffer_writeByte(lua_State *l)
{
	ByteBuffer *buffer = luaL_checkudata(l, 1, METATABLE_BUFFER);
	lua_pushinteger(l, buffer_writeByte(buffer, luaL_checkinteger(l, 2)));
	return 1;
}

int luabuffer_readByte(lua_State *l)
{
	ByteBuffer *buffer = luaL_checkudata(l, 1, METATABLE_BUFFER);
	uint8_t value;

	if (!buffer_readByte(buffer, &value)) {
		return luaL_error(l, "attempt to read beyond buffer limit ");
	}

	lua_pushinteger(l, value);
	return 1;
}

int luabuffer_writeShort(lua_State *l)
{
	ByteBuffer *buffer = luaL_checkudata(l, 1, METATABLE_BUFFER);
	lua_pushinteger(l, buffer_writeShort(buffer, luaL_checkinteger(l, 2)));
	return 1;
}

int luabuffer_readShort(lua_State *l)
{
	ByteBuffer *buffer = luaL_checkudata(l, 1, METATABLE_BUFFER);
	uint16_t value;

	if (!buffer_readShort(buffer, &value)) {
		return luaL_error(l, "attempt to read beyond buffer limit ");
	}

	lua_pushinteger(l, value);
	return 1;
}

int luabuffer_writeInt(lua_State *l)
{
	ByteBuffer *buffer = luaL_checkudata(l, 1, METATABLE_BUFFER);
	lua_pushinteger(l, buffer_writeInt(buffer, luaL_checkinteger(l, 2)));
	return 1;
}

int luabuffer_readInt(lua_State *l)
{
	ByteBuffer *buffer = luaL_checkudata(l, 1, METATABLE_BUFFER);
	int32_t value;

	if (!buffer_readInt(buffer, &value)) {
		return luaL_error(l, "attempt to read beyond buffer limit ");
	}

	lua_pushinteger(l, value);
	return 1;
}

int luabuffer_writeVarInt(lua_State *l)
{
	ByteBuffer *buffer = luaL_checkudata(l, 1, METATABLE_BUFFER);
	lua_pushinteger(l, buffer_writeVarInt(buffer, luaL_checkinteger(l, 2)));
	return 1;
}

int luabuffer_readVarInt(lua_State *l)
{
	ByteBuffer *buffer = luaL_checkudata(l, 1, METATABLE_BUFFER);
	uint64_t value;

	if (!buffer_readVarInt(buffer, &value)) {
		return luaL_error(l, "attempt to read beyond buffer limit ");
	}

	lua_pushinteger(l, value);
	return 1;
}

int luabuffer_writeFloat(lua_State *l)
{
	ByteBuffer *buffer = luaL_checkudata(l, 1, METATABLE_BUFFER);
	lua_pushinteger(l, buffer_writeFloat(buffer, luaL_checknumber(l, 2)));
	return 1;
}

int luabuffer_readFloat(lua_State *l)
{
	ByteBuffer *buffer = luaL_checkudata(l, 1, METATABLE_BUFFER);
	float value;

	if (!buffer_readFloat(buffer, &value)) {
		return luaL_error(l, "attempt to read beyond buffer limit ");
	}

	lua_pushnumber(l, value);
	return 1;
}

int luabuffer_writeDouble(lua_State *l)
{
	ByteBuffer *buffer = luaL_checkudata(l, 1, METATABLE_BUFFER);
	lua_pushinteger(l, buffer_writeDouble(buffer, luaL_checknumber(l, 2)));
	return 1;
}

int luabuffer_readDouble(lua_State *l)
{
	ByteBuffer *buffer = luaL_checkudata(l, 1, METATABLE_BUFFER);
	double value;

	if (!buffer_readDouble(buffer, &value)) {
		return luaL_error(l, "attempt to read beyond buffer limit ");
	}

	lua_pushnumber(l, value);
	return 1;
}

int luabuffer_writeString(lua_State *l)
{
	ByteBuffer *buffer = luaL_checkudata(l, 1, METATABLE_BUFFER);
	size_t size;
	const char* string = luaL_checklstring(l, 2, &size);
	buffer_writeVarInt(buffer, size);
	lua_pushinteger(l, buffer_write(buffer, string, size));
	return 1;
}

int luabuffer_readString(lua_State *l)
{
	ByteBuffer *buffer = luaL_checkudata(l, 1, METATABLE_BUFFER);
	uint64_t size;
	uint8_t read = buffer_readVarInt(buffer, &size);
	char string[size+1];

	if (!buffer_read(buffer, string, size)) {
		return luaL_error(l, "attempt to read beyond buffer limit ");
	}

	lua_pushlstring(l, string, size);
	return 1;
}

int luabuffer_writeBool(lua_State *l)
{
	ByteBuffer *buffer = luaL_checkudata(l, 1, METATABLE_BUFFER);
	lua_pushinteger(l, buffer_writeByte(buffer, luaL_checkboolean(l, 2)));
	return 1;
}

int luabuffer_readBool(lua_State *l)
{
	ByteBuffer *buffer = luaL_checkudata(l, 1, METATABLE_BUFFER);
	uint8_t value;

	if (!buffer_readByte(buffer, &value)) {
		return luaL_error(l, "attempt to read beyond buffer limit ");
	}

	lua_pushboolean(l, value);
	return 1;
}

int buffer_tostring(lua_State *l)
{
	ByteBuffer *buffer = luaL_checkudata(l, 1, METATABLE_BUFFER);
	lua_pushlstring(l, buffer->data, buffer->limit);
	return 1;
}

int buffer_gc(lua_State *l)
{
	ByteBuffer *buffer = luaL_checkudata(l, 1, METATABLE_BUFFER);
	if (buffer->data) {
		free(buffer->data);
	}
	mumble_log(LOG_DEBUG, "%s: %p garbage collected", METATABLE_BUFFER, buffer);
	return 0;
}

const luaL_Reg mumble_buffer[] = {
	{"clear", luabuffer_clear},
	{"compact", luabuffer_compact},
	{"reset", luabuffer_reset},
	{"flip", luabuffer_flip},
	{"capacity", luabuffer_capacity},
	{"getCapacity", luabuffer_capacity},
	{"position", luabuffer_position},
	{"getPosition", luabuffer_position},
	{"limit", luabuffer_limit},
	{"getLimit", luabuffer_limit},
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
	{"__tostring", buffer_tostring},
	{"__gc", buffer_gc},
	{NULL, NULL}
};