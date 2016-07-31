/*
 * piepie - bot framework for Mumble
 *
 * Author: Tim Cooper <tim.cooper@layeh.com>
 * License: MIT (see LICENSE)
 *
 * This file contains utility functions.
 */

 #include "mumble.h"

int audio_stop(lua_State *l)
{
	AudioTransmission *sound = luaL_checkudata(l, 1, METATABLE_AUDIO);
	sound->done = true;
	return 0;
}

int audio_setVolume(lua_State *l)
{
	AudioTransmission *sound = luaL_checkudata(l, 1, METATABLE_AUDIO);
	sound->volume = luaL_checknumber(l, 2);
	return 0;
}

int audio_getVolume(lua_State *l)
{
	AudioTransmission *sound = luaL_checkudata(l, 1, METATABLE_AUDIO);
	lua_pushnumber(l, sound->volume);
	return 1;
}

int audio_tostring(lua_State *l)
{
	AudioTransmission *sound = luaL_checkudata(l, 1, METATABLE_AUDIO);
	lua_pushfstring(l, "%s: %p", METATABLE_AUDIO, sound);
	return 0;
}

int util_set_varint(uint8_t buffer[], const uint64_t value)
{
	if (value < 0x80) {
		buffer[0] = value;
		return 1;
	} else if (value < 0x4000) {
		buffer[0] = (value >> 8) | 0x80;
		buffer[1] = value & 0xFF;
		return 2;
	}
	return -1;
}

#define VOICEPACKET_NORMAL 0
#define VOICEPACKET_OPUS 4

VoicePacket * voicepacket_init(VoicePacket *packet, uint8_t *buffer)
{
	if (packet == NULL || buffer == NULL) {
		return NULL;
	}
	packet->buffer = buffer;
	packet->length = 0;
	packet->header_length = 0;
	return packet;
}

int voicepacket_setheader(VoicePacket *packet, const uint8_t type, const uint8_t target, const uint32_t sequence)
{
	int offset;
	if (packet == NULL) {
		return -1;
	}
	if (packet->length > 0) {
		return -2;
	}
	packet->buffer[0] = ((type & 0x7) << 5) | (target & 0x1F);
	offset = util_set_varint(packet->buffer + 1, sequence);
	packet->length = packet->header_length = 1 + offset;
	return 1;
}

int voicepacket_setframe(VoicePacket *packet, const uint16_t length, uint8_t *buffer)
{
	int offset;
	if (packet == NULL || buffer == NULL || length <= 0 || length >= 0x2000) {
		return -1;
	}
	if (packet->header_length <= 0) {
		return -2;
	}
	offset = util_set_varint(packet->buffer + packet->header_length, length);
	if (offset <= 0) {
		return -3;
	}
	memmove(packet->buffer + packet->header_length + offset, buffer, length);
	packet->length = packet->header_length + length + offset;
	return 1;
}

int voicepacket_getlength(const VoicePacket *packet)
{
	if (packet == NULL) {
		return -1;
	}
	return packet->length;
}

static void audio_transmission_event_filter(float **pcm, long channels, long samples, void *param) {
	AudioTransmission *sound = (AudioTransmission *)param;
	int channel, sample;
	for (channel = 0; channel < channels; channel++) {
		for (sample = 0; sample < samples; sample++) {
			pcm[channel][sample] *= sound->volume * sound->client->volume;
		}
	}
}

void audioTransmission_stop(AudioTransmission *sound, lua_State *lua)
{
	if (sound == NULL || lua == NULL) {
		return;
	}
	ov_clear(&sound->ogg);
	fclose(sound->file);
}

struct timespec timer_start(){
    struct timespec start_time;
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &start_time);
    return start_time;
}

long timer_end(struct timespec start_time){
    struct timespec end_time;
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &end_time);
    long diffInNanos = end_time.tv_nsec - start_time.tv_nsec;
    return diffInNanos;
}

void audio_transmission_event(AudioTransmission *sound)
{
	VoicePacket packet;
	uint8_t packet_buffer[1024];
	uint8_t output[1024];
	opus_int32 byte_count;
	long long_ret;

	voicepacket_init(&packet, packet_buffer);
	voicepacket_setheader(&packet, VOICEPACKET_OPUS, VOICEPACKET_NORMAL, sound->sequence);

	while (sound->buffer.size < OPUS_FRAME_SIZE * sizeof(opus_int16)) {
		long_ret = ov_read_filter(&sound->ogg, sound->buffer.pcm + sound->buffer.size, PCM_BUFFER - sound->buffer.size, 0, 2, 1, NULL, audio_transmission_event_filter, sound);
		if (long_ret <= 0) {
			sound->done = true;
			audioTransmission_stop(sound, sound->lua);
			return;
		}
		sound->buffer.size += long_ret;
	}

	byte_count = opus_encode(sound->encoder, (opus_int16 *)sound->buffer.pcm, OPUS_FRAME_SIZE, output, sizeof(output));
	if (byte_count < 0) {
		sound->done = true;
		audioTransmission_stop(sound, sound->lua);
		return;
	}
	sound->buffer.size -= OPUS_FRAME_SIZE * sizeof(opus_int16);
	memmove(sound->buffer.pcm, sound->buffer.pcm + OPUS_FRAME_SIZE * sizeof(opus_int16), sound->buffer.size);
	voicepacket_setframe(&packet, byte_count, output);

	packet_sendex(sound->client, PACKET_UDPTUNNEL, packet_buffer, voicepacket_getlength(&packet));

	sound->sequence = (sound->sequence + 1) % 10000;
}