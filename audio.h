#pragma once

#define VOICEPACKET_NORMAL 0
#define VOICEPACKET_OPUS 4

extern int util_set_varint(uint8_t buffer[], const uint64_t value);
extern int64_t util_get_varint(uint8_t buffer[], int *len);
extern VoicePacket * voicepacket_init(VoicePacket *packet, uint8_t *buffer);
extern int voicepacket_setheader(VoicePacket *packet, const uint8_t type, const uint8_t target, const uint32_t sequence);
extern int voicepacket_setframe(VoicePacket *packet, const uint16_t length, uint8_t *buffer);
extern int voicepacket_getlength(const VoicePacket *packet);