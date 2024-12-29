#pragma once

#include <stdint.h>

typedef struct
{
	uint64_t capacity;
	uint64_t position;
	uint64_t limit;
	uint8_t* data;
} ByteBuffer;

#define buffer_available(buffer, size) \
	if(buffer == NULL || buffer->position + size > buffer->limit) return 0; \

#define buffer_rwh(name,type) \
	uint8_t buffer_read##name(ByteBuffer* buffer, type* output); \

#define buffer_rw(name,type) \
uint8_t buffer_read##name(ByteBuffer* buffer, type* output) { \
	size_t size = sizeof(type); \
	buffer_available(buffer, size); \
	*output = *((type*)(buffer->data + buffer->position)); \
	buffer->position += size; \
	return size; \
} \
uint8_t buffer_write##name(ByteBuffer* buffer, double value) { \
	size_t size = sizeof(type); \
	union { \
		type value; \
		uint8_t bytes[size]; \
	} convert; \
	convert.value = value; \
	buffer_write(buffer, convert.bytes, size); \
	return size; \
} \

ByteBuffer* buffer_new(uint64_t size);
ByteBuffer* buffer_init(ByteBuffer* buffer, uint64_t size);

void buffer_free(ByteBuffer* buffer);
void buffer_clear(ByteBuffer* buffer);
void buffer_compact(ByteBuffer* buffer);
void buffer_reset(ByteBuffer* buffer);
void buffer_flip(ByteBuffer* buffer);

uint64_t buffer_write(ByteBuffer* buffer, const void* data, uint64_t size);
uint64_t buffer_read(ByteBuffer* buffer, void* output, uint64_t size);

uint8_t buffer_writeByte(ByteBuffer* buffer, uint8_t value);
uint8_t buffer_readByte(ByteBuffer* buffer, int8_t* output);

buffer_rwh(UnsignedByte,uint8_t);
buffer_rwh(Short,int16_t);
buffer_rwh(UnsignedShort,uint16_t);
buffer_rwh(Int,int32_t);
buffer_rwh(UnsignedInt,uint32_t);
buffer_rwh(Long,int64_t);
buffer_rwh(Float,float);
buffer_rwh(Double,double);

uint8_t buffer_writeVarInt(ByteBuffer* buffer, uint64_t value);
uint8_t buffer_readVarInt(ByteBuffer* buffer, uint64_t* output);

#define METATABLE_BUFFER			"mumble.buffer"

extern int mumble_buffer_new(lua_State *l);
extern const luaL_Reg mumble_buffer[];