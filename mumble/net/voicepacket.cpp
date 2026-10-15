#include "net/voicepacket.h"

#include "defines.h"
#include "net/varint.h"

#include <cstring>

VoicePacket::VoicePacket(uint8_t *buffer) : buffer(buffer) {
}

int VoicePacket::setHeader(const uint8_t type, const uint8_t target, const uint32_t sequence) {
	int offset;
	if (length > 0) {
		return -2;
	}
	buffer[0] = ((type & 0x7) << 5) | (target & 0x1F);
	offset = VarInt::encode(buffer + 1, sequence);
	length = header_length = 1 + offset;
	return 1;
}

int VoicePacket::setFrame(const uint8_t type, const uint16_t frame_header, const uint8_t *frame, size_t frame_len) {
	int offset = 0;

	if (frame == NULL || frame_len <= 0 || frame_len >= 0x2000) {
		return -1;
	}
	if (header_length <= 0) {
		return -2;
	}

	if (type == LEGACY_UDP_OPUS) {
		// Opus uses a varint for the frame header
		offset = VarInt::encode(buffer + header_length, frame_header);
	} else if (type == LEGACY_UDP_SPEEX || type == LEGACY_UDP_CELT_ALPHA || type == LEGACY_UDP_CELT_BETA) {
		// Every other codec uses a single byte as the frame header
		offset = 1;
		buffer[header_length] = frame_header;
	}

	if (offset <= 0) {
		return -3;
	}
	memmove(buffer + header_length + offset, frame, frame_len);
	length = header_length + frame_len + offset;
	return 1;
}

int VoicePacket::getLength() const {
	return length;
}
