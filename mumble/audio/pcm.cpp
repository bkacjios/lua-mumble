#include "audio/pcm.h"


std::vector<float> Pcm::toStereo(const float* input, size_t frames, int channels) {
	if (channels == 1) {
		return monoToMulti(input, frames, 2);
	} else if (channels > 2) {
		return downmixToStereo(input, frames, channels);
	}
	return std::vector<float>(input, input + frames * channels);
}

std::vector<float> Pcm::monoToMulti(const float* input, size_t frames, int channels) {
	std::vector<float> output(frames * channels);

	for (size_t i = 0; i < frames; i++) {
		for (int ch = 0; ch < channels; ch++) {
			output[i * channels + ch] = input[i];
		}
	}
	return output;
}

std::vector<float> Pcm::downmixToStereo(const float* input, size_t frames, int channels) {
	std::vector<float> output(frames * 2);

	for (size_t i = 0; i < frames; i++) {
		float left = 0.0f, right = 0.0f;
		for (int ch = 0; ch < channels; ch++) {
			if (ch % 2 == 0)
				left += input[i * channels + ch];
			else
				right += input[i * channels + ch];
		}
		output[i * 2] = left / (channels / 2.0f);
		output[i * 2 + 1] = right / (channels / 2.0f);
	}
	return output;
}
