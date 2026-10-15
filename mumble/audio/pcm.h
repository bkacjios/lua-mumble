#pragma once

#include <cstddef>
#include <vector>

// Helpers for interleaved float PCM
class Pcm {
public:
	// Convert interleaved PCM with any channel count into interleaved stereo
	static std::vector<float> toStereo(const float* input, size_t frames, int channels);

private:
	static std::vector<float> monoToMulti(const float* input, size_t frames, int channels);
	static std::vector<float> downmixToStereo(const float* input, size_t frames, int channels);
};
