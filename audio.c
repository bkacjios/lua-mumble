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

	// Strip out the 14th bit that contains end of stream flag to get actual length of packet
	const uint16_t actual_length = (length & 0x1FFF);

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

void audio_transmission_stop(AudioTransmission* sound)
{
	if (sound == NULL) return;
	if (sound->ogg != NULL) {
		stb_vorbis_close(sound->ogg);
	}
}

void audio_transmission_event(lua_State* l, MumbleClient *client)
{
	VoicePacket packet;

	long read;
	long biggest_read = 0;

	const uint32_t enc_frame_size = client->audio_frames * AUDIO_SAMPLE_RATE / 100;

	memset(client->audio_buffer, 0, sizeof(client->audio_buffer));

	// Loop through all available audio channels
	for (int i = 0; i < AUDIO_MAX_CHANNELS; i++) {
		AudioTransmission *sound = client->audio_jobs[i];

		// No sound playing = skip
		if (sound == NULL) continue;

		const int channels = sound->info.channels;
		const uint32_t source_rate = sound->info.sample_rate;

		uint32_t sample_size = client->audio_frames * source_rate / 100;

		memset(sound->buffer, 0, sizeof(sound->buffer));

		read = stb_vorbis_get_samples_float_interleaved(sound->ogg, channels, sound->buffer, sample_size * channels);

		// Downmix all PCM data into a single channel
		for (int i=0, j=0; i < sample_size * channels; i+=channels) {
			float total = 0;
			for (int c=0; c < channels; c++) {
				// Add all the channels together
				total += sound->buffer[i + c];
			}
			// Average the channels out and apply volume
			client->audio_rebuffer[j] = total * sound->volume * client->volume / channels;
			j++;
		}

		memcpy(sound->buffer, client->audio_rebuffer, sizeof(client->audio_rebuffer));

		// Very very awful resampling, but at least it's something..
		if (source_rate != AUDIO_SAMPLE_RATE) {
			// Clear the rebuffer so we can use it again
			memset(client->audio_rebuffer, 0, sizeof(client->audio_rebuffer));

			// Resample the audio
			float scale = (float) read / (float) sample_size;

			// Adjust these values
			sample_size = sample_size * (float) AUDIO_SAMPLE_RATE / (float) source_rate;
			read = ceil(sample_size * scale);

			for (int t=0; t < read; t++) {
				// Resample the audio to fit within the requested sample_rate
				client->audio_rebuffer[t*2] = sound->buffer[(int) floor((float) t / AUDIO_SAMPLE_RATE * source_rate) * 2] * 2;
			}

			// Copy resampled audio back into the main buffer
			memcpy(sound->buffer, client->audio_rebuffer, sizeof(client->audio_rebuffer));
		}

		for (int i = 0; i < read; i++) {
			// Mix all channels together in the buffer
			client->audio_buffer[i] = client->audio_buffer[i] + sound->buffer[i];
		}

		// If the number of samples we read from the OGG file are less than the request sample size, it must be the last bit of audio
		if (read < sample_size) {
			lua_pushinteger(l, i + 1); // Push the channel number
			mumble_hook_call(l, client, "OnAudioFinished", 1);
			audio_transmission_stop(sound);
			client->audio_jobs[i] = NULL;
		}

		if (read > biggest_read) {
			// We need to save the biggest PCM length for later.
			// If we didn't do this, we could be cutting off some audio if one
			// stream ends while another is still playing.
			biggest_read = read;
		}
	}

	// Nothing to do..
	if (biggest_read <= 0) return;

	uint8_t output[0x1FFF];
	opus_int32 encoded_len = opus_encode_float(client->encoder, client->audio_buffer, enc_frame_size, output, sizeof(output));

	if (encoded_len <= 0) return;

	uint8_t packet_buffer[PAYLOAD_SIZE_MAX];

	voicepacket_init(&packet, packet_buffer);
	voicepacket_setheader(&packet, VOICEPACKET_OPUS, client->audio_target, client->audio_sequence);

	// If the largest PCM buffer is smaller than our frame size, it has to be the last frame available
	if (biggest_read < enc_frame_size) {
		// Set 14th bit to 1 to signal end of stream.
		encoded_len = ((1 << 13) | encoded_len);
		mumble_hook_call(l, client, "OnAudioStreamEnd", 0);
	}

	voicepacket_setframe(&packet, encoded_len, output);

	packet_sendex(client, PACKET_UDPTUNNEL, packet_buffer, voicepacket_getlength(&packet));

	client->audio_sequence = (client->audio_sequence + 1) % 100000;
}