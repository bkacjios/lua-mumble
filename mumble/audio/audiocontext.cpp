#include "audio/audiocontext.h"

#include "audio/pcm.h"
#include "audio/resampler.h"
#include "client/client.h"
#include "defines.h"
#include "util/buffer.h"
#include "util/log.h"

#include <cmath>

AudioContext::AudioContext(MumbleClient *client, opus_int32 samplerate, int channels, std::unique_ptr<Resampler> resampler)
	: client(client), samplerate(samplerate), channels(channels), resampler(std::move(resampler)) {
}

int AudioContext::read(ByteBuffer *buffer, uint32_t frame_ms, float volume, std::vector<float> &output) {
	// How many 48kHz stereo frames we owe the mix this frame
	size_t needed = (size_t) frame_ms * AUDIO_SAMPLE_RATE / 1000;
	double resample_ratio = (double)AUDIO_SAMPLE_RATE / samplerate;

	// Prepare for read
	buffer->flip();

	size_t available_frames = buffer->length() / sizeof(float) / channels;
	bool had_input = available_frames > 0;
	std::vector<float> resampled;

	// Resample until we have a whole frame, carrying anything extra over to the next one.
	// The resampler doesn't produce exactly input * ratio each time, so this keeps frames gapless.
	while (pending.size() / AUDIO_PLAYBACK_CHANNELS < needed && available_frames > 0) {
		size_t missing = needed - pending.size() / AUDIO_PLAYBACK_CHANNELS;
		size_t input_frames = (size_t) ceil(missing / resample_ratio);
		if (input_frames > available_frames) {
			input_frames = available_frames;
		}
		available_frames -= input_frames;

		std::vector<float> input(input_frames * channels);
		for (size_t i = 0; i < input.size(); i++) {
			float sample;
			buffer->readValue<float>(&sample);
			input[i] = sample * volume;
		}

		std::vector<float> stereo = Pcm::toStereo(input.data(), input_frames, channels);
		int frames = resampler->process(stereo.data(), input_frames, resample_ratio, false, resampled);
		if (frames < 0) {
			break;
		}
		pending.insert(pending.end(), resampled.begin(), resampled.begin() + frames * AUDIO_PLAYBACK_CHANNELS);
	}

	// Move any remaining audio data to the front
	buffer->pack();

	if (!had_input && pending.size() / AUDIO_PLAYBACK_CHANNELS < needed) {
		// Nothing new was written, so drain what the resampler is still holding instead of losing it.
		// This also resets it, ready for whenever more audio arrives.
		int frames = resampler->process(NULL, 0, resample_ratio, true, resampled);
		if (frames > 0) {
			pending.insert(pending.end(), resampled.begin(), resampled.begin() + frames * AUDIO_PLAYBACK_CHANNELS);
		}
	}

	size_t frames = pending.size() / AUDIO_PLAYBACK_CHANNELS;
	if (frames > needed) {
		frames = needed;
	}

	output.assign(pending.begin(), pending.begin() + frames * AUDIO_PLAYBACK_CHANNELS);
	pending.erase(pending.begin(), pending.begin() + frames * AUDIO_PLAYBACK_CHANNELS);

	return frames;
}
