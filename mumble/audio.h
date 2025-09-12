#pragma once

#include "types.h"

void audio_transmission_reference(lua_State *l, AudioStream *sound);
void audio_transmission_unreference(lua_State*l, AudioStream *sound);
void audiostream_reset_playback_state(AudioStream *sound);

uint8_t util_set_varint_size(const uint64_t value);
uint8_t util_set_varint(uint8_t buffer[], const uint64_t value);
uint64_t util_get_varint(uint8_t buffer[], int *len);
VoicePacket * voicepacket_init(VoicePacket *packet, uint8_t *buffer);
int voicepacket_setheader(VoicePacket *packet, const uint8_t type, const uint8_t target, const uint32_t sequence);
int voicepacket_setframe(VoicePacket *packet, const uint8_t type, const uint16_t frame_header, uint8_t *buffer, size_t buffer_len);
int voicepacket_getlength(const VoicePacket *packet);