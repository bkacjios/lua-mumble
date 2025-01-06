#include "buffer.h"
#include "util.h"
#include "log.h"

#include <stdlib.h>
#include <string.h>

ByteBuffer* buffer_new(uint64_t size) {
	ByteBuffer* buffer = malloc(sizeof(ByteBuffer));
	if (buffer == NULL) return NULL;
	buffer->data = NULL;
	buffer_init(buffer, size);
	return buffer;
}

ByteBuffer* buffer_init(ByteBuffer* buffer, uint64_t size) {
	buffer->capacity = size;
	buffer->read_head = 0;
	buffer->write_head = 0;
	buffer->data = malloc(sizeof(uint8_t) * size);
	buffer->context = NULL;
	if (buffer->data == NULL) return NULL;
	return buffer;
}

ByteBuffer* buffer_init_data(ByteBuffer* buffer, void* data, uint64_t size) {
	buffer = buffer_init(buffer, size);
	if (buffer != NULL) {
		buffer_write(buffer, data, size);
		buffer_flip(buffer);
	}
	return buffer;
}

static int buffer_adjust(ByteBuffer* buffer, uint64_t size) {
	uint64_t new_head = buffer->write_head + size;
	if (new_head > buffer->capacity) {
		uint64_t new_capacity = buffer->capacity;
		new_capacity = new_head * 1.5 > new_capacity ? (new_head * 1.5) : new_capacity;
		void* new_data = realloc(buffer->data, new_capacity);
		if (new_data != NULL) {
			mumble_log(LOG_DEBUG, "%s: %p resizing from %llu to %llu bytes", METATABLE_BUFFER, buffer, buffer->capacity, new_capacity);
			buffer->data = new_data;
			buffer->capacity = new_capacity;
			return 1;
		} else {
			mumble_log(LOG_ERROR, "%s: %p failed to resize buffer", METATABLE_BUFFER, buffer);
			return 0;
		}
	}
	return 0;
}

void buffer_free(ByteBuffer* buffer) {
	if (buffer->context) {
		list_remove_data(&buffer->context->client->audio_pipes, buffer);
		free(buffer->context);
		buffer->context = NULL;
	}
	if (buffer->data) {
		free(buffer->data);
		buffer->data = NULL;
	}
	buffer->capacity = 0;
	buffer->write_head = 0;
	buffer->read_head = 0;
}

void buffer_pack(ByteBuffer* buffer) {
	if (buffer->read_head == buffer->write_head) {
		// Compact the buffer if all data has been read
		buffer->write_head = 0;
		buffer->read_head = 0;
	} else if (buffer->read_head > 0) {
		// Move remaining data to the front of the buffer
		uint64_t remaining = buffer->write_head - buffer->read_head;
		memmove(buffer->data, buffer->data + buffer->read_head, remaining);
		buffer->write_head = remaining;
		buffer->read_head = 0;
	}
}

void buffer_reset(ByteBuffer* buffer) {
	buffer->read_head = 0;
	buffer->write_head = 0;
}

void buffer_flip(ByteBuffer* buffer) {
	buffer->read_head = 0;
}

uint64_t buffer_length(ByteBuffer* buffer) {
	return buffer->write_head - buffer->read_head;
}

uint64_t buffer_write(ByteBuffer* buffer, const void* data, uint64_t size) {
	buffer_adjust(buffer, size);
	memcpy(buffer->data + buffer->write_head, data, size);
	buffer->write_head += size;
	return size;
}

uint64_t buffer_read(ByteBuffer* buffer, void* output, uint64_t size) {
	buffer_available(buffer, size);
	memmove(output, buffer->data + buffer->read_head, size);
	buffer->read_head += size;
	return size;
}

uint8_t buffer_writeByte(ByteBuffer* buffer, uint8_t value) {
	buffer_adjust(buffer, 1);
	buffer->data[buffer->write_head++] = value;
	return 1;
}

uint8_t buffer_readByte(ByteBuffer* buffer, uint8_t* output) {
	buffer_available(buffer, 1);
	*output = buffer->data[buffer->read_head++];
	return 1;
}

buffer_rw(UnsignedByte, uint8_t);
buffer_rw(Short, int16_t);
buffer_rw(UnsignedShort, uint16_t);
buffer_rw(Int, int32_t);
buffer_rw(UnsignedInt, uint32_t);
buffer_rw(Long, int64_t);
buffer_rw(Float, float);
buffer_rw(Double, double);

uint8_t buffer_writeVarInt(ByteBuffer* buffer, uint64_t value) {
	int flag = 0;
	if ((value & 0x8000000000000000LL) && (~value < 0x100000000LL)) {
		value = ~value;
		if (value <= 0x3) {
			// Special case for -1 to -4. The most significant bits of the first byte must be (in binary) 111111
			// followed by the 2 bits representing the absolute value of the encoded number. Shortcase for -1 to -4
			buffer_adjust(buffer, 1);
			buffer->data[buffer->write_head++] = 0xFC | value;
			return 1;
		} else {
			// Add flag byte, whose most significant bits are (in binary) 111110 that indicates
			// that what follows is the varint encoding of the absolute value of i, but that the
			// value itself is supposed to be negative.
			buffer_adjust(buffer, 1);
			buffer->data[buffer->write_head++] = 0xF8;
			flag = 1;
		}
	}
	if (value < 0x80) {
		// Encode as 7-bit, positive number -> most significant bit of first byte must be zero
		buffer_adjust(buffer, 1);
		buffer->data[buffer->write_head++] = value;
		return 1 + flag;
	} else if (value < 0x4000) {
		// Encode as 14-bit, positive number -> most significant bits of first byte must be (in binary) 10
		buffer_adjust(buffer, 2);
		buffer->data[buffer->write_head++] = (value >> 8) | 0x80;
		buffer->data[buffer->write_head++] = value & 0xFF;
		return 2 + flag;
	} else if (value < 0x200000) {
		// Encode as 21-bit, positive number -> most significant bits of first byte must be (in binary) 110
		buffer_adjust(buffer, 3);
		buffer->data[buffer->write_head++] = (value >> 16) | 0xC0;
		buffer->data[buffer->write_head++] = (value >> 8) & 0xFF;
		buffer->data[buffer->write_head++] = value & 0xFF;
		return 3 + flag;
	} else if (value < 0x10000000) {
		// Encode as 28-bit, positive number -> most significant bits of first byte must be (in binary) 1110
		buffer_adjust(buffer, 4);
		buffer->data[buffer->write_head++] = (value >> 24) | 0xE0;
		buffer->data[buffer->write_head++] = (value >> 16) & 0xFF;
		buffer->data[buffer->write_head++] = (value >> 8) & 0xFF;
		buffer->data[buffer->write_head++] = value & 0xFF;
		return 4 + flag;
	} else if (value < 0x100000000) {
		// Encode as 32-bit, positive number -> most significant bits of first byte must be (in binary) 111100
		// Remaining bits in first byte remain unused
		buffer_adjust(buffer, 5);
		buffer->data[buffer->write_head++] = 0xF0;
		buffer->data[buffer->write_head++] = (value >> 24) & 0xFF;
		buffer->data[buffer->write_head++] = (value >> 16) & 0xFF;
		buffer->data[buffer->write_head++] = (value >> 8) & 0xFF;
		buffer->data[buffer->write_head++] = value & 0xFF;
		return 5 + flag;
	} else {
		// Encode as 64-bit, positive number -> most significant bits of first byte must be (in binary) 111101
		// Remaining bits in first byte remain unused
		buffer_adjust(buffer, 9);
		buffer->data[buffer->write_head++] = 0xF4;
		buffer->data[buffer->write_head++] = (value >> 56) & 0xFF;
		buffer->data[buffer->write_head++] = (value >> 48) & 0xFF;
		buffer->data[buffer->write_head++] = (value >> 40) & 0xFF;
		buffer->data[buffer->write_head++] = (value >> 32) & 0xFF;
		buffer->data[buffer->write_head++] = (value >> 24) & 0xFF;
		buffer->data[buffer->write_head++] = (value >> 16) & 0xFF;
		buffer->data[buffer->write_head++] = (value >> 8) & 0xFF;
		buffer->data[buffer->write_head++] = value & 0xFF;
		return 9 + flag;
	}
}

uint8_t buffer_readVarInt(ByteBuffer* buffer, uint64_t* output) {
	*output = 0;

	buffer_available(buffer, 1);
	uint8_t v = buffer->data[buffer->read_head++];

	uint8_t size = 0;

	uint8_t byte1, byte2, byte3, byte4, byte5, byte6, byte7, byte8;

	if ((v & 0x80) == 0x00) {
		*output = (v & 0x7F);
		size += 1;
	} else if ((v & 0xC0) == 0x80) {
		buffer_available(buffer, 1);
		byte1 = buffer->data[buffer->read_head++];
		*output = (v & 0x3F) << 8 | byte1;
		size += 2;
	} else if ((v & 0xF0) == 0xF0) {
		switch (v & 0xFC) {
		case 0xF0:
			buffer_available(buffer, 4);
			byte1 = buffer->data[buffer->read_head++];
			byte2 = buffer->data[buffer->read_head++];
			byte3 = buffer->data[buffer->read_head++];
			byte4 = buffer->data[buffer->read_head++];
			*output = (uint64_t)byte1 << 24 | (uint64_t)byte2 << 16 |
			          (uint64_t)byte3 << 8 | (uint64_t)byte4;
			size += 5;
			break;
		case 0xF4:
			buffer_available(buffer, 8);
			byte1 = buffer->data[buffer->read_head++];
			byte2 = buffer->data[buffer->read_head++];
			byte3 = buffer->data[buffer->read_head++];
			byte4 = buffer->data[buffer->read_head++];
			byte5 = buffer->data[buffer->read_head++];
			byte6 = buffer->data[buffer->read_head++];
			byte7 = buffer->data[buffer->read_head++];
			byte8 = buffer->data[buffer->read_head++];
			*output = (uint64_t)byte1 << 56 | (uint64_t)byte2 << 48 |
			          (uint64_t)byte3 << 40 | (uint64_t)byte4 << 32 |
			          (uint64_t)byte5 << 24 | (uint64_t)byte6 << 16 |
			          (uint64_t)byte7 << 8 | (uint64_t)byte8;
			size += 9;
			break;
		case 0xF8:
			// Handle negative flag
			buffer_available(buffer, 1);
			size += buffer_readVarInt(buffer, output); // Decode the positive value
			*output = ~*output; // Negate to restore the original value
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
		byte1 = buffer->data[buffer->read_head++];
		byte2 = buffer->data[buffer->read_head++];
		byte3 = buffer->data[buffer->read_head++];
		*output = (v & 0x0F) << 24 | byte1 << 16 | byte2 << 8 | byte3;
		size += 4;
	} else if ((v & 0xE0) == 0xC0) {
		buffer_available(buffer, 2);
		byte1 = buffer->data[buffer->read_head++];
		byte2 = buffer->data[buffer->read_head++];
		*output = (v & 0x1F) << 16 | byte1 << 8 | byte2;
		size += 3;
	}

	return size;
}

ByteBuffer* luabuffer_new(lua_State *l) {
	ByteBuffer* buffer = lua_newuserdata(l, sizeof(ByteBuffer));
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
		buffer = buffer_init(buffer, lua_tointeger(l, 2));
		break;
	case LUA_TSTRING:
		// Initialize with raw data
		size_t size;
		const char* data = lua_tolstring(l, 2, &size);
		buffer = buffer_init_data(buffer, (void*) data, size);
		break;
	case LUA_TNONE:
		// No argument given, allocate 1K by default
		buffer = buffer_init(buffer, 1024);
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
	ByteBuffer *buffer = luaL_checkudata(l, 1, METATABLE_BUFFER);
	buffer_pack(buffer);
	return 0;
}

int luabuffer_reset(lua_State *l) {
	ByteBuffer *buffer = luaL_checkudata(l, 1, METATABLE_BUFFER);
	buffer_reset(buffer);
	return 0;
}

int luabuffer_flip(lua_State *l) {
	ByteBuffer *buffer = luaL_checkudata(l, 1, METATABLE_BUFFER);
	buffer_flip(buffer);
	return 0;
}

int luabuffer_readHead(lua_State *l) {
	ByteBuffer *buffer = luaL_checkudata(l, 1, METATABLE_BUFFER);
	lua_pushinteger(l, buffer->read_head);
	return 1;
}

int luabuffer_writeHead(lua_State *l) {
	ByteBuffer *buffer = luaL_checkudata(l, 1, METATABLE_BUFFER);
	lua_pushinteger(l, buffer->write_head);
	return 1;
}

int luabuffer_write(lua_State *l) {
	ByteBuffer *buffer = luaL_checkudata(l, 1, METATABLE_BUFFER);

	switch (lua_type(l, 2)) {
	case LUA_TUSERDATA: {
		ByteBuffer* in = luaL_checkudata(l, 2, METATABLE_BUFFER);
		lua_pushinteger(l, buffer_write(buffer, in + in->read_head, buffer_length(in)));
		return 1;
	}
	case LUA_TSTRING: {
		size_t size;
		const char* data = lua_tolstring(l, 2, &size);
		lua_pushinteger(l, buffer_write(buffer, data, size));
		return 1;
	}
	default:
		const char *msg = lua_pushfstring(l, "%s or %s expected, got %s",
		                                  lua_typename(l, LUA_TSTRING), METATABLE_BUFFER, luaL_typename(l, 2));
		return luaL_argerror(l, 1, msg);
	}
}

int luabuffer_read(lua_State *l) {
	ByteBuffer *buffer = luaL_checkudata(l, 1, METATABLE_BUFFER);
	int nargs = lua_gettop(l) - 1;  // Get number of arguments
	char *read;

	for (int i = 2; i < nargs + 2; i++) {
		size_t size = 0;

		if (lua_type(l, i) == LUA_TNUMBER) {
			// If the argument is a number, read that many bytes
			size = (size_t) lua_tointeger(l, i);
		} else {
			const char *arg = lua_tostring(l, i);
			if (arg && arg[0] == '*' && arg[1] == 'a') {
				// If the argument is "*a", read the entire buffer
				size = buffer_length(buffer);
			} else {
				return luaL_argerror(l, i, "invalid format");
			}
		}

		// Allocate memory and perform the read operation
		read = malloc(size);
		if (!read) {
			return luaL_error(l, "failed to allocate memory for read buffer");
		}

		if (!buffer_read(buffer, read, size)) {
			free(read);
			return luaL_error(l, "attempt to read beyond buffer limit");
		}

		lua_pushlstring(l, read, size);
		free(read);
	}

	return nargs;
}


int luabuffer_writeByte(lua_State *l) {
	ByteBuffer *buffer = luaL_checkudata(l, 1, METATABLE_BUFFER);
	lua_pushinteger(l, buffer_writeByte(buffer, luaL_checkinteger(l, 2)));
	return 1;
}

int luabuffer_readByte(lua_State *l) {
	ByteBuffer *buffer = luaL_checkudata(l, 1, METATABLE_BUFFER);
	uint8_t value;

	if (!buffer_readByte(buffer, &value)) {
		return luaL_error(l, "attempt to read beyond buffer limit");
	}

	lua_pushinteger(l, value);
	return 1;
}

int luabuffer_writeShort(lua_State *l) {
	ByteBuffer *buffer = luaL_checkudata(l, 1, METATABLE_BUFFER);
	lua_pushinteger(l, buffer_writeShort(buffer, luaL_checkinteger(l, 2)));
	return 1;
}

int luabuffer_readShort(lua_State *l) {
	ByteBuffer *buffer = luaL_checkudata(l, 1, METATABLE_BUFFER);
	int16_t value;

	if (!buffer_readShort(buffer, &value)) {
		return luaL_error(l, "attempt to read beyond buffer limit");
	}

	lua_pushinteger(l, value);
	return 1;
}

int luabuffer_writeInt(lua_State *l) {
	ByteBuffer *buffer = luaL_checkudata(l, 1, METATABLE_BUFFER);
	lua_pushinteger(l, buffer_writeInt(buffer, luaL_checkinteger(l, 2)));
	return 1;
}

int luabuffer_readInt(lua_State *l) {
	ByteBuffer *buffer = luaL_checkudata(l, 1, METATABLE_BUFFER);
	int32_t value;

	if (!buffer_readInt(buffer, &value)) {
		return luaL_error(l, "attempt to read beyond buffer limit");
	}

	lua_pushinteger(l, value);
	return 1;
}

int luabuffer_writeVarInt(lua_State *l) {
	ByteBuffer *buffer = luaL_checkudata(l, 1, METATABLE_BUFFER);
	lua_pushinteger(l, buffer_writeVarInt(buffer, luaL_checkinteger(l, 2)));
	return 1;
}

int luabuffer_readVarInt(lua_State *l) {
	ByteBuffer *buffer = luaL_checkudata(l, 1, METATABLE_BUFFER);
	uint64_t value;

	if (!buffer_readVarInt(buffer, &value)) {
		return luaL_error(l, "attempt to read beyond buffer limit");
	}

	lua_pushinteger(l, value);
	return 1;
}

int luabuffer_writeFloat(lua_State *l) {
	ByteBuffer *buffer = luaL_checkudata(l, 1, METATABLE_BUFFER);
	lua_pushinteger(l, buffer_writeFloat(buffer, luaL_checknumber(l, 2)));
	return 1;
}

int luabuffer_readFloat(lua_State *l) {
	ByteBuffer *buffer = luaL_checkudata(l, 1, METATABLE_BUFFER);
	float value;

	if (!buffer_readFloat(buffer, &value)) {
		return luaL_error(l, "attempt to read beyond buffer limit");
	}

	lua_pushnumber(l, value);
	return 1;
}

int luabuffer_writeDouble(lua_State *l) {
	ByteBuffer *buffer = luaL_checkudata(l, 1, METATABLE_BUFFER);
	lua_pushinteger(l, buffer_writeDouble(buffer, luaL_checknumber(l, 2)));
	return 1;
}

int luabuffer_readDouble(lua_State *l) {
	ByteBuffer *buffer = luaL_checkudata(l, 1, METATABLE_BUFFER);
	double value;

	if (!buffer_readDouble(buffer, &value)) {
		return luaL_error(l, "attempt to read beyond buffer limit");
	}

	lua_pushnumber(l, value);
	return 1;
}

int luabuffer_writeString(lua_State *l) {
	ByteBuffer *buffer = luaL_checkudata(l, 1, METATABLE_BUFFER);
	size_t size;
	const char* string = luaL_checklstring(l, 2, &size);
	buffer_writeVarInt(buffer, size);
	lua_pushinteger(l, buffer_write(buffer, string, size));
	return 1;
}

int luabuffer_readString(lua_State *l) {
	ByteBuffer *buffer = luaL_checkudata(l, 1, METATABLE_BUFFER);
	uint64_t size;

	if (!buffer_readVarInt(buffer, &size)) {
		return luaL_error(l, "attempt to read beyond buffer limit");
	}

	char string[size + 1];

	if (!buffer_read(buffer, string, size)) {
		return luaL_error(l, "attempt to read beyond buffer limit");
	}

	lua_pushlstring(l, string, size);
	return 1;
}

int luabuffer_writeBool(lua_State *l) {
	ByteBuffer *buffer = luaL_checkudata(l, 1, METATABLE_BUFFER);
	lua_pushinteger(l, buffer_writeByte(buffer, luaL_checkboolean(l, 2)));
	return 1;
}

int luabuffer_readBool(lua_State *l) {
	ByteBuffer *buffer = luaL_checkudata(l, 1, METATABLE_BUFFER);
	uint8_t value;

	if (!buffer_readByte(buffer, &value)) {
		return luaL_error(l, "attempt to read beyond buffer limit");
	}

	lua_pushboolean(l, value);
	return 1;
}

int luabuffer_isEmpty(lua_State *l) {
	ByteBuffer *buffer = luaL_checkudata(l, 1, METATABLE_BUFFER);
	lua_pushboolean(l, buffer->write_head == buffer->read_head);
	return 1;
}

static int luabuffer_seek(lua_State *l) {
	ByteBuffer *buffer = luaL_checkudata(l, 1, METATABLE_BUFFER);

	enum mode {READ, WRITE, BOTH};
	static const char *md[] = {"read", "write", "both", NULL};

	enum what {SET, CUR, END};
	static const char *op[] = {"set", "cur", "end", NULL};

	int mode_option = luaL_checkoption(l, 2, "read", md);
	int position_option = luaL_checkoption(l, 3, "cur", op);
	long offset = luaL_optlong(l, 4, 0);

	uint64_t old_read_head = buffer->read_head;
	uint64_t old_write_head = buffer->write_head;
	uint64_t new_read_head = 0;
	uint64_t new_write_head = 0;

	// Calculate the new heads based on position option
	switch (position_option) {
	case SET:
		new_read_head = offset;
		new_write_head = offset;
		break;
	case CUR:
		new_read_head = old_read_head + offset;
		new_write_head = old_write_head + offset;
		break;
	case END:
		new_read_head = buffer->capacity + offset;
		new_write_head = buffer->capacity + offset;
		break;
	}

	// Update the heads based on mode
	switch (mode_option) {
	case READ:
		buffer->read_head = new_read_head;
		lua_pushinteger(l, new_read_head);
		return 1;
	case WRITE:
		buffer->write_head = new_write_head;
		lua_pushinteger(l, new_write_head);
		return 1;
	case BOTH:
		buffer->read_head = new_read_head;
		buffer->write_head = new_write_head;
		lua_pushinteger(l, new_read_head);
		lua_pushinteger(l, new_write_head);
		return 2;
	}

	return 0;
}


int luabuffer_len(lua_State *l) {
	ByteBuffer *buffer = luaL_checkudata(l, 1, METATABLE_BUFFER);
	lua_pushinteger(l, buffer_length(buffer));
	return 1;
}

int luabuffer_index(lua_State *l) {
	ByteBuffer *buffer = luaL_checkudata(l, 1, METATABLE_BUFFER);

	switch (lua_type(l, 2)) {
	case LUA_TNUMBER: {
		// Read specific character
		int index = lua_tointeger(l, 2);
		if (index >= buffer->read_head && index < buffer_length(buffer)) {
			lua_pushlstring(l, (char*) buffer->data + index, 1);
		} else {
			lua_pushnil(l);
		}
		return 1;
	}
	case LUA_TSTRING: {
		const char *key = lua_tostring(l, 2);
		// Custom length and capactity index
		if (strcmp(key, "length") == 0) {
			lua_pushinteger(l, buffer_length(buffer));
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
	ByteBuffer *buffer = luaL_checkudata(l, 1, METATABLE_BUFFER);
	buffer_free(buffer);
	mumble_log(LOG_DEBUG, "%s: %p garbage collected", METATABLE_BUFFER, buffer);
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