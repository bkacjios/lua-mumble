#pragma once

#include <cstddef>
#include <cstdint>

// Builder for a legacy (pre 1.5) UDP voice packet
class VoicePacket {
public:
	explicit VoicePacket(uint8_t *buffer);

	int setHeader(const uint8_t type, const uint8_t target, const uint32_t sequence);
	int setFrame(const uint8_t type, const uint16_t frame_header, const uint8_t *frame, size_t frame_len);
	int getLength() const;

	uint8_t *buffer;
	int length = 0;
	int header_length = 0;
};
