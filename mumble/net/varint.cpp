#include "net/varint.h"


uint8_t VarInt::encode(uint8_t buffer[], const uint64_t value) {
	if (value < 0x80) {
		buffer[0] = value;
		return 1;
	} else if (value < 0x4000) {
		buffer[0] = (value >> 8) | 0x80;
		buffer[1] = value & 0xFF;
		return 2;
	} else if (value < 0x200000) {
		buffer[0] = (value >> 16) | 0xC0;
		buffer[1] = (value >> 8) & 0xFF;
		buffer[2] = value & 0xFF;
		return 3;
	} else if (value < 0x10000000) {
		buffer[0] = (value >> 24) | 0xE0;
		buffer[1] = (value >> 16) & 0xFF;
		buffer[2] = (value >> 8) & 0xFF;
		buffer[3] = value & 0xFF;
		return 4;
	} else if (value < 0x100000000) {
		buffer[0] = 0xF0;
		buffer[1] = (value >> 24) & 0xFF;
		buffer[2] = (value >> 16) & 0xFF;
		buffer[3] = (value >> 8) & 0xFF;
		buffer[4] = value & 0xFF;
		return 5;
	} else {
		buffer[0] = 0xF4;
		buffer[1] = (value >> 56) & 0xFF;
		buffer[2] = (value >> 48) & 0xFF;
		buffer[3] = (value >> 40) & 0xFF;
		buffer[4] = (value >> 32) & 0xFF;
		buffer[5] = (value >> 24) & 0xFF;
		buffer[6] = (value >> 16) & 0xFF;
		buffer[7] = (value >> 8) & 0xFF;
		buffer[8] = value & 0xFF;
		return 9;
	}
	return 0;
}

uint64_t VarInt::decode(const uint8_t buffer[], int *len) {
	uint8_t v = buffer[0];
	uint64_t i = 0;

	if ((v & 0x80) == 0x00) {
		i = (v & 0x7F);
		*len += 1;
	} else if ((v & 0xC0) == 0x80) {
		i = (v & 0x3F) << 8 | buffer[1];
		*len += 2;
	} else if ((v & 0xF0) == 0xF0) {
		switch (v & 0xFC) {
		case 0xF0:
			i = (uint64_t)buffer[1] << 24 | (uint64_t)buffer[2] << 16 |
			    (uint64_t)buffer[3] << 8 | (uint64_t)buffer[4];
			*len += 5;
			break;
		case 0xF4:
			i = (uint64_t)buffer[1] << 56 | (uint64_t)buffer[2] << 48 |
			    (uint64_t)buffer[3] << 40 | (uint64_t)buffer[4] << 32 |
			    (uint64_t)buffer[5] << 24 | (uint64_t)buffer[6] << 16 |
			    (uint64_t)buffer[7] << 8 | (uint64_t)buffer[8];
			*len += 9;
			break;
		case 0xF8:
			i = ~i;
			*len += 1;
			break;
		case 0xFC:
			i = v & 0x03;
			i = ~i;
			*len += 1;
			break;
		default:
			i = 0;
			*len += 1;
			break;
		}
	} else if ((v & 0xF0) == 0xE0) {
		i = (v & 0x0F) << 24 | buffer[1] << 16 | buffer[2] << 8 | buffer[3];
		*len += 4;
	} else if ((v & 0xE0) == 0xC0) {
		i = (v & 0x1F) << 16 | buffer[1] << 8 | buffer[2];
		*len += 3;
	}

	return i;
}
