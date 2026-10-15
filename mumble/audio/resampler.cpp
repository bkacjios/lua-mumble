#include "audio/resampler.h"

#include "util/log.h"

#include <cmath>
#include <cstring>

Resampler::Resampler(int quality, int channels, int *error) : channels(channels) {
	state = src_new(quality, channels, error);
}

Resampler::~Resampler() {
	if (state) {
		src_delete(state);
	}
}

bool Resampler::isValid() const {
	return state != nullptr;
}

int Resampler::reset() {
	return src_reset(state);
}

int Resampler::process(const float *input, long input_frames, double ratio, bool end_of_input, std::vector<float> &output) {
	long output_frames = (long) ceil((double) input_frames * ratio);

	if (ratio == 1.0) {
		output.resize(input_frames * channels);
		if (input_frames > 0) {
			memcpy(output.data(), input, input_frames * channels * sizeof(float));
		}
		return input_frames;
	}

	// Flushing also drains what the converter is still holding, so leave room for that
	if (end_of_input && output_frames < 1024) {
		output_frames = 1024;
	}
	output.resize(output_frames * channels);

	// libsamplerate rejects a NULL input, even when there's nothing to read
	static const float empty = 0.0f;
	if (input == NULL) {
		input = &empty;
	}

	long used = 0;
	long generated = 0;

	while (true) {
		SRC_DATA data = {};
		data.data_in = input + used * channels;
		data.input_frames = input_frames - used;
		data.data_out = output.data() + generated * channels;
		data.output_frames = output_frames - generated;
		data.end_of_input = end_of_input;
		data.src_ratio = ratio;

		int error = src_process(state, &data);
		if (error != 0) {
			mumble_log(LOG_ERROR, "error resampling audio: %s", src_strerror(error));
			return -1;
		}

		used += data.input_frames_used;
		generated += data.output_frames_gen;

		bool more = used < input_frames || end_of_input;

		if (generated == output_frames) {
			if (!more) break;
			// Out of room before consuming all our input, or while draining, so grow and keep going
			output_frames += output_frames / 2 + 1;
			output.resize(output_frames * channels);
		} else if (used >= input_frames || (data.input_frames_used == 0 && data.output_frames_gen == 0)) {
			// Everything was consumed (and drained, if flushing), or the converter can't make progress
			break;
		}
	}

	if (end_of_input) {
		int error = src_reset(state);
		if (error != 0) {
			mumble_log(LOG_ERROR, "error resetting audio file resampler state: %s", src_strerror(error));
			return -1;
		}
	}

	return generated;
}
