#pragma once

#include <cstdint>

// The variable length integer encoding used by Mumble's legacy UDP packets
class VarInt {
public:
	// Writes value into buffer, returning how many bytes were written
	static uint8_t encode(uint8_t buffer[], const uint64_t value);
	// Reads a value from buffer, adding how many bytes were read to *len
	static uint64_t decode(const uint8_t buffer[], int *len);
};
