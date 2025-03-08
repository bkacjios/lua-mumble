#pragma once

#include <lauxlib.h>
#include <stdint.h>

#include <opus/opus.h>
#include <stdbool.h>
#include <samplerate.h>

typedef struct MumbleClient MumbleClient;

typedef struct {
	MumbleClient* client;
	opus_int32 samplerate;
	SRC_STATE *src_state;
	int channels;
	bool playing;
} AudioContext;

typedef struct {
	uint64_t capacity;
	uint64_t read_head;
	uint64_t write_head;
	uint8_t* data;
	AudioContext* context;
} ByteBuffer;

#define buffer_available(buffer, size) \
	if(buffer == NULL || buffer->read_head + size > buffer->write_head) return 0; \

#define buffer_rwh(name,type) \
	uint8_t buffer_read##name(ByteBuffer* buffer, type* output); \

#define buffer_rw(name, type) \
uint8_t buffer_read##name(ByteBuffer* buffer, type* output) { \
    return (uint8_t) buffer_read(buffer, output, sizeof(type)); \
} \
uint8_t buffer_write##name(ByteBuffer* buffer, double value) { \
	size_t size = sizeof(type); \
	union { \
		type value; \
		uint8_t bytes[sizeof(type)]; \
	} convert; \
	convert.value = value; \
	buffer_write(buffer, convert.bytes, size); \
	return size; \
} \

ByteBuffer* buffer_new(uint64_t size);
ByteBuffer* buffer_init(ByteBuffer* buffer, uint64_t size);
ByteBuffer* buffer_init_data(ByteBuffer* buffer, void* data, uint64_t size);

void buffer_free(ByteBuffer* buffer);
void buffer_pack(ByteBuffer* buffer);
void buffer_reset(ByteBuffer* buffer);
void buffer_flip(ByteBuffer* buffer);

uint64_t buffer_length(ByteBuffer* buffer);

uint64_t buffer_write(ByteBuffer* buffer, const void* data, uint64_t size);
uint64_t buffer_read(ByteBuffer* buffer, void* output, uint64_t size);

uint8_t buffer_writeByte(ByteBuffer* buffer, uint8_t value);
uint8_t buffer_readByte(ByteBuffer* buffer, uint8_t* output);

buffer_rwh(UnsignedByte, uint8_t);
buffer_rwh(Short, int16_t);
buffer_rwh(UnsignedShort, uint16_t);
buffer_rwh(Int, int32_t);
buffer_rwh(UnsignedInt, uint32_t);
buffer_rwh(Long, int64_t);
buffer_rwh(Float, float);
buffer_rwh(Double, double);

uint8_t buffer_writeVarInt(ByteBuffer* buffer, uint64_t value);
uint8_t buffer_readVarInt(ByteBuffer* buffer, uint64_t* output);

#define METATABLE_BUFFER			"mumble.buffer"

ByteBuffer* luabuffer_new(lua_State *l);

extern int mumble_buffer_new(lua_State *l);
extern const luaL_Reg mumble_buffer[];