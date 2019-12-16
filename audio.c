#include "mumble.h"

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

void audio_transmission_stop(MumbleClient *client)
{
	if (client->audio_job != NULL) {
		client->audio_job = NULL;
		client->audio_finished = true;
	}

	AudioTransmission *sound = client->audio_job;

	if (sound == NULL)
		return;
	
	ov_clear(&sound->ogg);
	fclose(sound->file);
	free(sound);
}

void audio_transmission_event(MumbleClient *client)
{
	AudioTransmission *sound = client->audio_job;

	VoicePacket packet;
	uint8_t packet_buffer[PCM_BUFFER];
	uint8_t output[PCM_BUFFER];
	opus_int32 byte_count;
	long long_ret;

	voicepacket_init(&packet, packet_buffer);
	voicepacket_setheader(&packet, VOICEPACKET_OPUS, client->audio_target, sound->sequence);

	while (sound->buffer.size < OPUS_FRAME_SIZE * sizeof(opus_int16)) {
		long_ret = ov_read_filter(&sound->ogg, sound->buffer.pcm + sound->buffer.size, PCM_BUFFER - sound->buffer.size, 0, 2, 1, NULL, audio_transmission_event_filter, sound);
		if (long_ret <= 0) {
			audio_transmission_stop(client);
			return;
		}
		sound->buffer.size += long_ret;
	}

	byte_count = opus_encode(client->encoder, (opus_int16 *)sound->buffer.pcm, OPUS_FRAME_SIZE, output, sizeof(output));
	if (byte_count < 0) {
		audio_transmission_stop(client);
		return;
	}
	sound->buffer.size -= OPUS_FRAME_SIZE * sizeof(opus_int16);
	memmove(sound->buffer.pcm, sound->buffer.pcm + OPUS_FRAME_SIZE * sizeof(opus_int16), sound->buffer.size);
	voicepacket_setframe(&packet, byte_count, output);

	packet_sendex(client, PACKET_UDPTUNNEL, packet_buffer, voicepacket_getlength(&packet));

	sound->sequence = (sound->sequence + 1) % 100000;
}