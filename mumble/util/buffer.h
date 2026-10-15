#pragma once

#include "lua/luainc.h"

#include <cstdint>

class AudioContext;

class ByteBuffer {
public:
	uint64_t original_capacity = 0;
	uint64_t capacity = 0;
	uint64_t read_head = 0;
	uint64_t write_head = 0;
	uint8_t* data = nullptr;
	AudioContext* context = nullptr;

	// Allocates storage, returns nullptr on failure
	ByteBuffer* init(uint64_t size);
	ByteBuffer* initData(const void* data, uint64_t size);

	int resize(size_t new_capacity);
	int shrink();
	void free();
	void pack();
	void reset();
	void flip();
	uint64_t length() const;

	enum SeekHeads { SEEK_READ_HEAD = 1, SEEK_WRITE_HEAD = 2 };
	// Move the read and/or write head relative to the start, their current position or our capacity (SEEK_SET/CUR/END)
	void seek(int heads, int whence, long offset);
	bool isEmpty() const;

	uint64_t write(const void* data, uint64_t size);
	uint64_t read(void* output, uint64_t size);
	// Returns the next size unread bytes and moves the read head past them, or nullptr if there aren't that many
	const uint8_t* consume(uint64_t size);
	// The unread byte at an absolute index, or nullptr if it's out of range
	const uint8_t* at(uint64_t index) const;

	uint8_t writeByte(uint8_t value);
	uint8_t readByte(uint8_t* output);

	template <typename T>
	uint8_t readValue(T* output) {
		return (uint8_t) read(output, sizeof(T));
	}

	template <typename T>
	uint8_t writeValue(double value) {
		T converted = value;
		write(&converted, sizeof(T));
		return sizeof(T);
	}

	uint8_t writeVarInt(uint64_t value);
	uint8_t readVarInt(uint64_t* output);

	// A string prefixed by its length as a varint. Returns how many bytes of the string were written.
	uint64_t writeString(const void* data, uint64_t size);
	// Returns the string and moves the read head past it, or nullptr if it goes beyond the buffer
	const uint8_t* readString(uint64_t* size);

private:
	bool available(uint64_t size) const;
	int adjust(uint64_t size);
};
