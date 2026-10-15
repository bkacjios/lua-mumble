#pragma once

#include "lua/luainc.h"
#include "util/log.h"

#include <opus/opus.h>

// The most samples per channel a single opus packet can hold (120ms at 48kHz)
#define OPUS_MAX_FRAME_SIZE 5760

class MumbleOpusDecoder {
public:
	~MumbleOpusDecoder() {
		if (decoder != nullptr) {
			opus_decoder_destroy(decoder);
		}
	}

	// Create our decoder with phase inversion disabled. Returns an opus error code.
	int init(opus_int32 samplerate, int channels) {
		int error;
		decoder = opus_decoder_create(samplerate, channels, &error);
		if (error != OPUS_OK) {
			return error;
		}
		this->channels = channels;
		opus_decoder_ctl(decoder, OPUS_SET_PHASE_INVERSION_DISABLED(1));
		return OPUS_OK;
	}

	int reset() {
		return opus_decoder_ctl(decoder, OPUS_RESET_STATE);
	}

	// Get or set a decoder ctl by its OPUS_*_REQUEST id. Returns an opus error code.
	int get(int request, opus_int32 *value) {
		return opus_decoder_ctl(decoder, request, value);
	}

	int set(int request, opus_int32 value) {
		return opus_decoder_ctl(decoder, request, value);
	}

	// How many samples per channel an opus packet decodes to, or an opus error code
	int samples(const unsigned char *packet, size_t length) const {
		return opus_decoder_get_nb_samples(decoder, packet, length);
	}

	// Decode a whole opus packet into interleaved PCM, which must have room for OPUS_MAX_FRAME_SIZE samples per channel.
	// Returns samples per channel, 0 when the packet holds no samples, or an opus error code.
	int decode(const unsigned char *packet, size_t length, opus_int16 *pcm) {
		int frame_size = samples(packet, length);
		if (frame_size <= 0) return 0;
		return opus_decode(decoder, packet, length, pcm, frame_size, 0);
	}

	int decodeFloat(const unsigned char *packet, size_t length, float *pcm) {
		int frame_size = samples(packet, length);
		if (frame_size <= 0) return 0;
		return opus_decode_float(decoder, packet, length, pcm, frame_size, 0);
	}

	OpusDecoder *decoder = nullptr;
	int channels = 0;
};
