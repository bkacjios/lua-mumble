#include "util/buffer.h"

#include "audio/audiocontext.h"
#include "client/client.h"
#include "lua/metatables.h"
#include "util/log.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

ByteBuffer* ByteBuffer::init(uint64_t size) {
	original_capacity = size;
	capacity = size;
	read_head = 0;
	write_head = 0;
	data = (uint8_t*) malloc(sizeof(uint8_t) * size);
	context = nullptr;
	if (data == nullptr) return nullptr;
	return this;
}

ByteBuffer* ByteBuffer::initData(const void* input, uint64_t size) {
	ByteBuffer* buffer = init(size);
	if (buffer != nullptr) {
		buffer->write(input, size);
		buffer->flip();
	}
	return buffer;
}

bool ByteBuffer::available(uint64_t size) const {
	return read_head + size <= write_head;
}

int ByteBuffer::adjust(uint64_t size) {
	uint64_t new_head = write_head + size;
	if (new_head > capacity) {
		uint64_t grow_1_5x = capacity + (capacity >> 1); // 1.5x
		uint64_t grow_2x = capacity * 2; // 2x growth
		uint64_t grow_headroom = new_head + (new_head >> 1); // new_head * 1.5

		// Choose the maximum value that ensures we have enough space
		uint64_t new_capacity = grow_1_5x;
		if (new_capacity < new_head) new_capacity = grow_2x;
		if (new_capacity < new_head) new_capacity = grow_headroom;

		return resize(new_capacity);
	}
	return 0;
}

int ByteBuffer::resize(size_t new_capacity) {
	void* new_data = realloc(data, new_capacity);
	if (new_data != nullptr) {
		mumble_log(LOG_DEBUG, "%s: %p resizing from %llu to %llu bytes", METATABLE_BUFFER, this, (unsigned long long) capacity, (unsigned long long) new_capacity);
		data = (uint8_t*) new_data;
		capacity = new_capacity;
		return 1;
	} else {
		mumble_log(LOG_ERROR, "%s: %p failed to resize buffer", METATABLE_BUFFER, this);
		return 0;
	}
}

int ByteBuffer::shrink() {
	if (capacity > original_capacity) {
		return resize(original_capacity);
	}
	return 0;
}

void ByteBuffer::free() {
	AudioContext *ctx = context;
	if (ctx) {
		// The client detaches its buffers when it's freed
		if (ctx->client) {
			// Lock the same mutex the audio loop uses for audio_pipes
			uv_mutex_lock(&ctx->client->inner_mutex);
			ctx->client->audio_pipes.removeData(this);
			uv_mutex_unlock(&ctx->client->inner_mutex);
		}

		delete ctx;
		context = nullptr;
	}

	if (data) {
		::free(data);
		data = nullptr;
	}
	original_capacity = 0;
	capacity          = 0;
	write_head        = 0;
	read_head         = 0;
}

void ByteBuffer::pack() {
	if (read_head == write_head) {
		// Reset the buffer if all data has been read
		reset();
	} else if (read_head > 0) {
		// Move remaining data to the front of the buffer
		uint64_t remaining = write_head - read_head;
		memmove(data, data + read_head, remaining);
		write_head = remaining;
		read_head = 0;
	}
}

void ByteBuffer::reset() {
	read_head = 0;
	write_head = 0;
}

void ByteBuffer::flip() {
	read_head = 0;
}

void ByteBuffer::seek(int heads, int whence, long offset) {
	if (heads & SEEK_READ_HEAD) {
		read_head = (whence == SEEK_SET ? 0 : whence == SEEK_CUR ? read_head : capacity) + offset;
	}
	if (heads & SEEK_WRITE_HEAD) {
		write_head = (whence == SEEK_SET ? 0 : whence == SEEK_CUR ? write_head : capacity) + offset;
	}
}

uint64_t ByteBuffer::length() const {
	return write_head - read_head;
}

bool ByteBuffer::isEmpty() const {
	return write_head == read_head;
}

uint64_t ByteBuffer::write(const void* input, uint64_t size) {
	adjust(size);
	memcpy(data + write_head, input, size);
	write_head += size;
	return size;
}

uint64_t ByteBuffer::read(void* output, uint64_t size) {
	if (!available(size)) return 0;
	memmove(output, data + read_head, size);
	read_head += size;
	return size;
}

const uint8_t* ByteBuffer::consume(uint64_t size) {
	if (!available(size)) return nullptr;
	const uint8_t* bytes = data + read_head;
	read_head += size;
	return bytes;
}

const uint8_t* ByteBuffer::at(uint64_t index) const {
	if (index < read_head || index >= length()) return nullptr;
	return data + index;
}

uint8_t ByteBuffer::writeByte(uint8_t value) {
	adjust(1);
	data[write_head++] = value;
	return 1;
}

uint8_t ByteBuffer::readByte(uint8_t* output) {
	if (!available(1)) return 0;
	*output = data[read_head++];
	return 1;
}

uint8_t ByteBuffer::writeVarInt(uint64_t value) {
	int flag = 0;
	if ((value & 0x8000000000000000LL) && (~value < 0x100000000LL)) {
		value = ~value;
		if (value <= 0x3) {
			// Special case for -1 to -4. The most significant bits of the first byte must be (in binary) 111111
			// followed by the 2 bits representing the absolute value of the encoded number. Shortcase for -1 to -4
			adjust(1);
			data[write_head++] = 0xFC | value;
			return 1;
		} else {
			// Add flag byte, whose most significant bits are (in binary) 111110 that indicates
			// that what follows is the varint encoding of the absolute value of i, but that the
			// value itself is supposed to be negative.
			adjust(1);
			data[write_head++] = 0xF8;
			flag = 1;
		}
	}
	if (value < 0x80) {
		// Encode as 7-bit, positive number -> most significant bit of first byte must be zero
		adjust(1);
		data[write_head++] = value;
		return 1 + flag;
	} else if (value < 0x4000) {
		// Encode as 14-bit, positive number -> most significant bits of first byte must be (in binary) 10
		adjust(2);
		data[write_head++] = (value >> 8) | 0x80;
		data[write_head++] = value & 0xFF;
		return 2 + flag;
	} else if (value < 0x200000) {
		// Encode as 21-bit, positive number -> most significant bits of first byte must be (in binary) 110
		adjust(3);
		data[write_head++] = (value >> 16) | 0xC0;
		data[write_head++] = (value >> 8) & 0xFF;
		data[write_head++] = value & 0xFF;
		return 3 + flag;
	} else if (value < 0x10000000) {
		// Encode as 28-bit, positive number -> most significant bits of first byte must be (in binary) 1110
		adjust(4);
		data[write_head++] = (value >> 24) | 0xE0;
		data[write_head++] = (value >> 16) & 0xFF;
		data[write_head++] = (value >> 8) & 0xFF;
		data[write_head++] = value & 0xFF;
		return 4 + flag;
	} else if (value < 0x100000000) {
		// Encode as 32-bit, positive number -> most significant bits of first byte must be (in binary) 111100
		// Remaining bits in first byte remain unused
		adjust(5);
		data[write_head++] = 0xF0;
		data[write_head++] = (value >> 24) & 0xFF;
		data[write_head++] = (value >> 16) & 0xFF;
		data[write_head++] = (value >> 8) & 0xFF;
		data[write_head++] = value & 0xFF;
		return 5 + flag;
	} else {
		// Encode as 64-bit, positive number -> most significant bits of first byte must be (in binary) 111101
		// Remaining bits in first byte remain unused
		adjust(9);
		data[write_head++] = 0xF4;
		data[write_head++] = (value >> 56) & 0xFF;
		data[write_head++] = (value >> 48) & 0xFF;
		data[write_head++] = (value >> 40) & 0xFF;
		data[write_head++] = (value >> 32) & 0xFF;
		data[write_head++] = (value >> 24) & 0xFF;
		data[write_head++] = (value >> 16) & 0xFF;
		data[write_head++] = (value >> 8) & 0xFF;
		data[write_head++] = value & 0xFF;
		return 9 + flag;
	}
}

uint8_t ByteBuffer::readVarInt(uint64_t* output) {
	*output = 0;

	if (!available(1)) return 0;
	uint8_t v = data[read_head++];

	uint8_t size = 0;

	uint8_t byte1, byte2, byte3, byte4, byte5, byte6, byte7, byte8;

	if ((v & 0x80) == 0x00) {
		*output = (v & 0x7F);
		size += 1;
	} else if ((v & 0xC0) == 0x80) {
		if (!available(1)) return 0;
		byte1 = data[read_head++];
		*output = (v & 0x3F) << 8 | byte1;
		size += 2;
	} else if ((v & 0xF0) == 0xF0) {
		switch (v & 0xFC) {
		case 0xF0:
			if (!available(4)) return 0;
			byte1 = data[read_head++];
			byte2 = data[read_head++];
			byte3 = data[read_head++];
			byte4 = data[read_head++];
			*output = (uint64_t)byte1 << 24 | (uint64_t)byte2 << 16 |
					  (uint64_t)byte3 << 8 | (uint64_t)byte4;
			size += 5;
			break;
		case 0xF4:
			if (!available(8)) return 0;
			byte1 = data[read_head++];
			byte2 = data[read_head++];
			byte3 = data[read_head++];
			byte4 = data[read_head++];
			byte5 = data[read_head++];
			byte6 = data[read_head++];
			byte7 = data[read_head++];
			byte8 = data[read_head++];
			*output = (uint64_t)byte1 << 56 | (uint64_t)byte2 << 48 |
					  (uint64_t)byte3 << 40 | (uint64_t)byte4 << 32 |
					  (uint64_t)byte5 << 24 | (uint64_t)byte6 << 16 |
					  (uint64_t)byte7 << 8 | (uint64_t)byte8;
			size += 9;
			break;
		case 0xF8:
			// Handle negative flag
			if (!available(1)) return 0;
			size += readVarInt(output); // Decode the positive value
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
		if (!available(3)) return 0;
		byte1 = data[read_head++];
		byte2 = data[read_head++];
		byte3 = data[read_head++];
		*output = (v & 0x0F) << 24 | byte1 << 16 | byte2 << 8 | byte3;
		size += 4;
	} else if ((v & 0xE0) == 0xC0) {
		if (!available(2)) return 0;
		byte1 = data[read_head++];
		byte2 = data[read_head++];
		*output = (v & 0x1F) << 16 | byte1 << 8 | byte2;
		size += 3;
	}

	return size;
}

uint64_t ByteBuffer::writeString(const void* input, uint64_t size) {
	writeVarInt(size);
	return write(input, size);
}

const uint8_t* ByteBuffer::readString(uint64_t* size) {
	if (!readVarInt(size)) return nullptr;
	return consume(*size);
}
