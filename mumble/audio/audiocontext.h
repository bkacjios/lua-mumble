#pragma once

#include "audio/resampler.h"

#include <cstdint>
#include <memory>
#include <opus/opus.h>
#include <vector>

class ByteBuffer;
class MumbleClient;

// Extra state a buffer carries when it is used as a PCM input pipe for a client
class AudioContext {
public:
	AudioContext(MumbleClient *client, opus_int32 samplerate, int channels, std::unique_ptr<Resampler> resampler);

	// Pull frame_ms worth of float PCM out of buffer, convert it to 48kHz stereo and store it in output.
	// Returns the number of stereo frames produced, which is only short of frame_ms when the buffer runs dry.
	int read(ByteBuffer *buffer, uint32_t frame_ms, float volume, std::vector<float> &output);

	MumbleClient* client;
	opus_int32 samplerate;
	int channels;
	bool playing = true;
	std::unique_ptr<Resampler> resampler;
	// Resampled 48kHz stereo audio that's waiting for the next frame
	std::vector<float> pending;
};
