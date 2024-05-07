#pragma once

#define VOICEPACKET_NORMAL 0
#define VOICEPACKET_OPUS 4

#define METATABLE_AUDIOSTREAM	"mumble.audiostream"

extern const luaL_Reg mumble_audiostream[];

extern void audio_transmission_event(lua_State* l, MumbleClient *client);
extern void audio_transmission_stop(lua_State*l, MumbleClient *client, int channel);

extern uint8_t util_set_varint_size(const uint64_t value);
extern uint8_t util_set_varint(uint8_t buffer[], const uint64_t value);
extern uint64_t util_get_varint(uint8_t buffer[], int *len);
extern VoicePacket * voicepacket_init(VoicePacket *packet, uint8_t *buffer);
extern int voicepacket_setheader(VoicePacket *packet, const uint8_t type, const uint8_t target, const uint32_t sequence);
extern int voicepacket_setframe(VoicePacket *packet, const uint8_t type, const uint16_t frame_header, uint8_t *buffer, size_t buffer_len);
extern int voicepacket_getlength(const VoicePacket *packet);