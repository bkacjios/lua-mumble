#include "mumble.h"
#include "packet.h"
#include "audio.h"

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
		buffer[1] = (value >> 24) | 0xFF;
		buffer[2] = (value >> 16) & 0xFF;
		buffer[3] = (value >> 8) & 0xFF;
		buffer[4] = value & 0xFF;
		return 5;
	}
	return -1;
}

int64_t util_get_varint(uint8_t buffer[], int *len)
{
	uint8_t v = buffer[0];
	int64_t i = 0;

	if ((v & 0x80) == 0x00) {
		i = (v & 0x7F);
		*len += 1;
	} else if ((v & 0xC0) == 0x80) {
		i = (v & 0x3F) << 8 | buffer[1];
		*len += 2;
	} else if ((v & 0xF0) == 0xF0) {
		switch (v & 0xFC) {
			case 0xF0:
				i = buffer[1] << 24 | buffer[2] << 16 | buffer[3] << 8 | buffer[4];
				*len += 5;
				break;
			case 0xF4:
				i = (uint64_t)buffer[1] << 56 | (uint64_t)buffer[2] << 48 | (uint64_t)buffer[3] << 40 | (uint64_t)buffer[4] << 32 | buffer[5] << 24 | buffer[6] << 16 | buffer[7] << 8 | buffer[8];
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
		i=(v & 0x0F) << 24 | buffer[1] << 16 | buffer[2] << 8 | buffer[3];
		*len += 4;
	} else if ((v & 0xE0) == 0xC0) {
		i=(v & 0x1F) << 16 | buffer[1] << 8 | buffer[2];
		*len += 3;
	} else {
		*len += 1;
	}

	return i;
}

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

int voicepacket_setframe(VoicePacket *packet, const uint8_t type, const uint16_t frame_header, uint8_t *buffer, size_t buffer_len)
{
	int offset;

	if (packet == NULL || buffer == NULL || buffer_len <= 0 || buffer_len >= 0x2000) {
		return -1;
	}
	if (packet->header_length <= 0) {
		return -2;
	}

	if (type == UDP_OPUS) {
		// Opus uses a varint for the frame header
		offset = util_set_varint(packet->buffer + packet->header_length, frame_header);
	} else if (type == UDP_SPEEX || type == UDP_CELT_ALPHA || type == UDP_CELT_BETA) {
		// Every other codec uses a single byte as the frame header
		offset += 1;
		packet->buffer[packet->header_length] = frame_header;
	}
	
	if (offset <= 0) {
		return -3;
	}
	memmove(packet->buffer + packet->header_length + offset, buffer, buffer_len);
	packet->length = packet->header_length + buffer_len + offset;
	return 1;
}

int voicepacket_getlength(const VoicePacket *packet)
{
	if (packet == NULL) {
		return -1;
	}
	return packet->length;
}

void audio_transmission_stop(lua_State*l, MumbleClient *client, int channel)
{
	int index = channel - 1;
	AudioTransmission* sound = client->audio_jobs[index];
	if (sound == NULL) return;
	if (sound->ogg != NULL) {
		lua_pushinteger(l, channel); // Push the channel number
		mumble_hook_call(l, client, "OnAudioFinished", 1);
		stb_vorbis_close(sound->ogg);
	}
	free(sound);
	client->audio_jobs[index] = NULL;
}

void audio_transmission_event(lua_State* l, MumbleClient *client)
{
	VoicePacket packet;

	long read;
	long biggest_read = 0;

	// How big each frame of audio data should be
	const uint32_t frame_size = client->audio_frames * AUDIO_SAMPLE_RATE / 1000;

	memset(client->audio_buffer, 0, sizeof(client->audio_buffer));

	// Loop through all available audio channels
	for (int i = 0; i < AUDIO_MAX_STREAMS; i++) {
		AudioTransmission *sound = client->audio_jobs[i];

		// No sound playing = skip
		if (sound == NULL) continue;

		// How many channels the audio file has
		const int channels = sound->info.channels;
		const uint32_t source_rate = sound->info.sample_rate;

		uint32_t sample_size = client->audio_frames * source_rate / 1000;

		memset(sound->buffer, 0, sizeof(sound->buffer));

		read = stb_vorbis_get_samples_float_interleaved(sound->ogg, AUDIO_PLAYBACK_CHANNELS, (float*) sound->buffer, sample_size * AUDIO_PLAYBACK_CHANNELS);

		if (channels == 1 && read > 0) {
			//mix mono to stereo
			for (int i = 0; i < read; i++) {
				sound->buffer[i].r = sound->buffer[i].l;
			}
		}

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
				int idx = (int) floor((float) t / AUDIO_SAMPLE_RATE * source_rate) * 2;
				client->audio_rebuffer[t*2].l = sound->buffer[idx].l * 2;
				client->audio_rebuffer[t*2].r = sound->buffer[idx].r * 2;
			}

			// Copy resampled audio back into the main buffer
			memcpy(sound->buffer, client->audio_rebuffer, sizeof(client->audio_rebuffer));
		}

		for (int i = 0; i < read; i++) {
			// Mix all streams together in the output buffer
			client->audio_buffer[i].l += sound->buffer[i].l * sound->volume * client->volume;
			client->audio_buffer[i].r += sound->buffer[i].r * sound->volume * client->volume;
		}

		// If the number of samples we read from the OGG file are less than the request sample size, it must be the last bit of audio
		if (read < sample_size) {
			audio_transmission_stop(l, client, i + 1);
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

	uint8_t encoded[0x1FFF];
	opus_int32 encoded_len = opus_encode_float(client->encoder, (float*) client->audio_buffer, frame_size, encoded, sizeof(encoded));

	if (encoded_len <= 0) return;

	uint8_t packet_buffer[PAYLOAD_SIZE_MAX];

	voicepacket_init(&packet, packet_buffer);
	voicepacket_setheader(&packet, UDP_OPUS, client->audio_target, client->audio_sequence);

	uint32_t frame_header = encoded_len;

	// If the largest PCM buffer is smaller than our frame size, it has to be the last frame available
	if (biggest_read < frame_size) {
		// Set 14th bit to 1 to signal end of stream.
		frame_header = ((1 << 13) | frame_header);
		mumble_hook_call(l, client, "OnAudioStreamEnd", 0);
	}

	voicepacket_setframe(&packet, UDP_OPUS, frame_header, encoded, encoded_len);

	packet_sendex(client, PACKET_UDPTUNNEL, packet_buffer, voicepacket_getlength(&packet));

	client->audio_sequence = (client->audio_sequence + 1) % 100000;

	// Safety stack clear
	lua_settop(l, 0);
}