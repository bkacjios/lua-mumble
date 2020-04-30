#include "mumble.h"

#define STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"

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

	// The 14th bit of our length value contains a flag for determining end of stream
	uint16_t actual_length = (length & 0x1FFF);

	if (packet == NULL || buffer == NULL || actual_length <= 0 || actual_length >= 0x2000) {
		return -1;
	}
	if (packet->header_length <= 0) {
		return -2;
	}
	offset = util_set_varint(packet->buffer + packet->header_length, length);
	if (offset <= 0) {
		return -3;
	}
	memmove(packet->buffer + packet->header_length + offset, buffer, actual_length);
	packet->length = packet->header_length + actual_length + offset;
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

void audio_transmission_stop(AudioTransmission* sound)
{
	if (sound == NULL) return;
	if (sound->ogg != NULL) {
		stb_vorbis_close(sound->ogg);
	}
}

void audio_transmission_event(MumbleClient *client)
{
	VoicePacket packet;

	uint8_t packet_buffer[PAYLOAD_SIZE_MAX];
	uint8_t output[0x1FFF];
	opus_int32 encoded_len;
	long read;
	long biggest_read = 0;

	uint32_t frame_size = client->audio_frames * AUDIO_SAMPLE_RATE / 100;

	memset(client->audio_buffer, 0, sizeof(client->audio_buffer));

	// Loop through all available audio channels
	for (int i = 0; i < AUDIO_MAX_CHANNELS; i++) {
		AudioTransmission *sound = client->audio_jobs[i];

		// No sound playing = skip
		if (sound == NULL) continue;

		memset(sound->buffer, 0, sizeof(sound->buffer));

		read = stb_vorbis_get_samples_short_interleaved(sound->ogg, 1, sound->buffer, frame_size);

		if (read < frame_size) {
			lua_pushinteger(client->l, i + 1); // Push the channel number
			mumble_hook_call(client, "OnAudioFinished", 1);
			audio_transmission_stop(sound);
			client->audio_jobs[i] = NULL;
		}

		if (read > biggest_read) {
			// We need to save the biggest PCM length for later.
			// If we didn't do this, we could be cutting off some audio if one
			// stream ends while another is still playing.
			biggest_read = read;
		}

		for (int i = 0; i < read; i++) {
			// Mix all channels together in the buffer
			client->audio_buffer[i] = client->audio_buffer[i] + sound->buffer[i];
		}
	}

	// Nothing to do..
	if (biggest_read <= 0) return;

	encoded_len = opus_encode(client->encoder, (opus_int16 *)client->audio_buffer, frame_size, output, sizeof(output));

	if (encoded_len <= 0) return;

	voicepacket_init(&packet, packet_buffer);
	voicepacket_setheader(&packet, VOICEPACKET_OPUS, client->audio_target, client->audio_sequence);

	// If the largest PCM buffer is smaller than our frame size, it has to be the last frame available
	if (biggest_read < frame_size) {
		// Set 14th bit to 1 to signal end of stream.
		encoded_len = ((1 << 13) | encoded_len);
		mumble_hook_call(client, "OnAudioStreamEnd", 0);
	}

	voicepacket_setframe(&packet, encoded_len, output);

	packet_sendex(client, PACKET_UDPTUNNEL, packet_buffer, voicepacket_getlength(&packet));

	client->audio_sequence = (client->audio_sequence + 1) % 100000;
}