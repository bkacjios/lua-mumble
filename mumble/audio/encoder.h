#pragma once

#include "lua/luainc.h"

#include "defines.h"

#include <opus/opus.h>
#include <uv.h>

// Every method locks, so the encode thread and the main thread can share one encoder
class MumbleOpusEncoder {
public:
	MumbleOpusEncoder() {
		uv_mutex_init(&mutex);
	}

	~MumbleOpusEncoder() {
		if (encoder != nullptr) {
			opus_encoder_destroy(encoder);
		}
		uv_mutex_destroy(&mutex);
	}

	MumbleOpusEncoder(const MumbleOpusEncoder&) = delete;
	MumbleOpusEncoder& operator=(const MumbleOpusEncoder&) = delete;

	// Create our encoder with VBR off and our default bitrate. Returns an opus error code.
	int init(opus_int32 samplerate, int channels) {
		int error;
		encoder = opus_encoder_create(samplerate, channels, OPUS_APPLICATION_AUDIO, &error);
		if (error != OPUS_OK) {
			return error;
		}
		set(OPUS_SET_VBR_REQUEST, 0);
		set(OPUS_SET_BITRATE_REQUEST, AUDIO_DEFAULT_BITRATE);
		return OPUS_OK;
	}

	int reset() {
		uv_mutex_lock(&mutex);
		int result = opus_encoder_ctl(encoder, OPUS_RESET_STATE);
		uv_mutex_unlock(&mutex);
		return result;
	}

	// Get or set an encoder ctl by its OPUS_*_REQUEST id. Returns an opus error code.
	int get(int request, opus_int32 *value) {
		uv_mutex_lock(&mutex);
		int result = opus_encoder_ctl(encoder, request, value);
		uv_mutex_unlock(&mutex);
		return result;
	}

	int set(int request, opus_int32 value) {
		uv_mutex_lock(&mutex);
		int result = opus_encoder_ctl(encoder, request, value);
		uv_mutex_unlock(&mutex);
		return result;
	}

	// Encode frame_size samples per channel of interleaved PCM. Returns the packet length, or an opus error code.
	opus_int32 encode(const opus_int16 *pcm, int frame_size, unsigned char *output, opus_int32 max_bytes) {
		uv_mutex_lock(&mutex);
		opus_int32 result = opus_encode(encoder, pcm, frame_size, output, max_bytes);
		uv_mutex_unlock(&mutex);
		return result;
	}

	opus_int32 encodeFloat(const float *pcm, int frame_size, unsigned char *output, opus_int32 max_bytes) {
		uv_mutex_lock(&mutex);
		opus_int32 result = opus_encode_float(encoder, pcm, frame_size, output, max_bytes);
		uv_mutex_unlock(&mutex);
		return result;
	}

	OpusEncoder *encoder = nullptr;

private:
	uv_mutex_t mutex;
};
