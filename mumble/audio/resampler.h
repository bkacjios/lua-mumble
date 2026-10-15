#pragma once

#include <samplerate.h>
#include <vector>

// Owns a libsamplerate converter for interleaved float PCM
class Resampler {
public:
	// On failure isValid() is false and *error holds the libsamplerate error
	Resampler(int quality, int channels, int *error);
	~Resampler();

	Resampler(const Resampler&) = delete;
	Resampler& operator=(const Resampler&) = delete;

	bool isValid() const;

	// Returns a libsamplerate error code, 0 on success
	int reset();

	// Resample input_frames of audio by ratio into output.
	// Returns the number of frames written, or -1 on error.
	int process(const float *input, long input_frames, double ratio, bool end_of_input, std::vector<float> &output);

private:
	SRC_STATE *state;
	int channels;
};
