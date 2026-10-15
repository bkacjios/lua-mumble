#include "audio/audiostream.h"

#include "audio/pcm.h"
#include "audio/resampler.h"
#include "client/client.h"
#include "defines.h"
#include "lua/luainc.h"
#include "lua/metatables.h"
#include "lua/registry.h"
#include "util/log.h"

#include <cmath>
#include <cstdlib>
#include <cstring>

AudioStream::AudioStream(MumbleClient *client) : client(client) {
	uv_mutex_init(&mutex);
	uv_mutex_init(&decode_mutex);
}

std::string AudioStream::open(const char *path, int quality) {
	file = sf_open(path, SFM_READ, &info);
	if (!file) {
		return std::string("failed to open audio file: ") + path + " (" + sf_strerror(NULL) + ")";
	}

	buffer_size = AUDIO_SAMPLE_RATE * (AUDIO_BUFFER_SIZE / 1000.0) * AUDIO_PLAYBACK_CHANNELS;
	buffer = (float*) malloc(buffer_size * sizeof(float));
	if (buffer == NULL) {
		return std::string("failed creating audio buffer: ") + strerror(errno);
	}

	int error;
	resampler.reset(new Resampler(quality, AUDIO_PLAYBACK_CHANNELS, &error));
	if (!resampler->isValid()) {
		return std::string("failed creating audio resampler: ") + src_strerror(error);
	}
	return "";
}

bool AudioStream::resume() {
	if (refrence <= LUA_NOREF) {
		return false;
	}
	playing = true;
	return true;
}

void AudioStream::start(lua_State *l) {
	resetPlaybackState();
	playing = true;
	reference(l);
}

void AudioStream::play(lua_State *l, int index) {
	if (!resume()) {
		lua_pushvalue(l, index);
		start(l);
	}
}

void AudioStream::pause() {
	playing = false;
}

void AudioStream::stop(lua_State *l) {
	if (playing) {
		unreference(l);
	}
}

bool AudioStream::isLooping() const {
	return looping || loop_count > 0;
}

sf_count_t AudioStream::seek(sf_count_t offset, int whence) {
	uv_mutex_lock(&decode_mutex);
	sf_count_t position = sf_seek(file, offset, whence);
	if (position >= 0) {
		// Drop what was buffered from the old position, so the seek is heard straight away.
		// We're the only reader of the ring, and the buffer thread only writes to it with decode_mutex held.
		used.store(0, std::memory_order_relaxed);
		head.store(0, std::memory_order_relaxed);
		tail.store(0, std::memory_order_relaxed);
		pending.clear();
		draining = false;
		end = false;
		if (resampler) {
			resampler->reset();
		}
	}
	uv_mutex_unlock(&decode_mutex);
	return position;
}

void AudioStream::setLooping(bool loop) {
	looping = loop;
	loop_count = 0;
}

void AudioStream::setLoopCount(int count) {
	looping = false;
	loop_count = count;
}

void AudioStream::fadeTo(float volume, float seconds) {
	// Always fade over at least one frame, so a fade of 0 seconds still takes effect
	fade_frames = AUDIO_SAMPLE_RATE * seconds;
	if (fade_frames < 1) {
		fade_frames = 1;
	}
	fade_frames_left = fade_frames;
	fade_from_volume = fade_volume;
	fade_to_volume = volume;
}

void AudioStream::fadeOut(float seconds) {
	fadeTo(0, seconds);
	fade_stop = true;
}

AudioStream::~AudioStream() {
	if (usecount.load(std::memory_order_acquire) != 0) {
		mumble_log(LOG_WARN, "%s: %p destroyed with non-zero usecount", METATABLE_AUDIOSTREAM, this);
	}
	if (file) {
		sf_close(file);
		file = nullptr;
	}
	if (buffer) {
		free(buffer);
		buffer = nullptr;
	}
	uv_mutex_destroy(&mutex);
	uv_mutex_destroy(&decode_mutex);
}

bool AudioStream::tryPin() {
	// Don't pin if we are being reclaimed
	if (reclaimed.load(std::memory_order_acquire)) return false;

	// Add to the usecount
	usecount.fetch_add(1, std::memory_order_acq_rel);

	// If it became reclaimed after increment, drop the pin
	if (reclaimed.load(std::memory_order_acquire)) {
		// Subtract from the usecount
		usecount.fetch_sub(1, std::memory_order_acq_rel);
		// Failure
		return false;
	}

	// Success
	return true;
}

void AudioStream::unpin() {
	int prev = usecount.fetch_sub(1, std::memory_order_acq_rel);
	if (prev == 1 && reclaimed.load(std::memory_order_acquire)) {
		// Queue for main-thread unref of reclaim_ref
		if (reclaim_ref > LUA_NOREF) {
			uv_mutex_lock(&client->inner_mutex);
			// Use reclaim_ref as the node index for easy removal
			client->reclaim_list.add(reclaim_ref, this);
			uv_mutex_unlock(&client->inner_mutex);
		}
	}
}

size_t AudioStream::ringCount() const {
	return used.load(std::memory_order_acquire);
}

size_t AudioStream::ringSpace() const {
	// Full when used == buffer_size; empty when used == 0
	size_t count = ringCount();
	return (count >= buffer_size) ? 0 : (buffer_size - count);
}

size_t AudioStream::ringWrite(const float *src, size_t count) {
	// single producer
	size_t position = head.load(std::memory_order_relaxed);
	size_t space = ringSpace();
	if (count > space) count = space;
	if (count == 0) return 0;

	size_t first = buffer_size - position;
	if (first > count) first = count;

	memcpy(buffer + position, src, first * sizeof(float));
	if (count > first) {
		memcpy(buffer, src + first, (count - first) * sizeof(float));
	}

	// Publish new head and occupancy (release orders publish written samples)
	head.store((position + count) % buffer_size, std::memory_order_release);
	used.fetch_add(count, std::memory_order_release);
	return count;
}

size_t AudioStream::ringRead(float *dst, size_t count) {
	// single consumer
	size_t position = tail.load(std::memory_order_relaxed);
	size_t avail = ringCount(); // acquire pairs with producer's release
	if (count > avail) count = avail;
	if (count == 0) return 0;

	size_t first = buffer_size - position;
	if (first > count) first = count;

	memcpy(dst, buffer + position, first * sizeof(float));
	if (count > first) {
		memcpy(dst + first, buffer, (count - first) * sizeof(float));
	}

	// Advance tail and reduce occupancy (release so subsequent loads see progress)
	tail.store((position + count) % buffer_size, std::memory_order_release);
	used.fetch_sub(count, std::memory_order_release);
	return count;
}

void AudioStream::reference(lua_State *l) {
	// If it was in reclamation, cancel that and reuse the held reclaim_ref.
	bool was_reclaimed = reclaimed.load(std::memory_order_acquire);

	if (was_reclaimed) {
		mumble_log(LOG_CODE, "%s: %p was marked as reclaimed", METATABLE_AUDIOSTREAM, this);
		reclaimed.store(false, std::memory_order_release);

		if (reclaim_ref > LUA_NOREF) {
			mumble_log(LOG_CODE, "%s: %p has a reclaimed ref: %d", METATABLE_AUDIOSTREAM, this, reclaim_ref);
			refrence = reclaim_ref;
			reclaim_ref = LUA_NOREF;

			// Remove from reclaim_list if it was queued
			uv_mutex_lock(&client->inner_mutex);
			client->reclaim_list.remove(refrence);
			uv_mutex_unlock(&client->inner_mutex);
		}
	}

	// Ensure we have a valid active reference
	if (refrence <= LUA_NOREF) {
		mumble_log(LOG_CODE, "%s: %p pushing sound to audio stream registry", METATABLE_AUDIOSTREAM, this);
		refrence = LuaRegistry::refIn(l, client->audio_streams);
	}

	// Put it on the active list (id = refrence)
	uv_mutex_lock(&client->inner_mutex);
	client->stream_list.add(refrence, this);
	uv_mutex_unlock(&client->inner_mutex);
}

void AudioStream::resetPlaybackState() {
	// Wait for the buffer thread to finish with our file and resampler
	uv_mutex_lock(&decode_mutex);

	playing = false;
	end = false;
	fade_volume = 1.0f;
	fade_frames = 0;
	fade_frames_left = 0;
	fade_stop = false;

	// Clear the buffer
	pending.clear();
	draining = false;
	used.store(0, std::memory_order_relaxed);
	head.store(0, std::memory_order_relaxed);
	tail.store(0, std::memory_order_relaxed);

	// Reset resampler state if present
	if (resampler) {
		int err = resampler->reset();
		if (err != 0) {
			mumble_log(LOG_WARN, "error resetting audio file resampler state: %s", src_strerror(err));
		}
	}

	// Rewind
	if (file) {
		sf_seek(file, 0, SEEK_SET);
	}

	uv_mutex_unlock(&decode_mutex);
}

void AudioStream::unreferenceLocked(lua_State *l) {
	uv_mutex_lock(&client->inner_mutex);
	if (refrence > LUA_NOREF) {
		client->stream_list.remove(refrence);
	}
	uv_mutex_unlock(&client->inner_mutex);

	// Mark reclaimed, will no longer be pinnable
	reclaimed.store(true, std::memory_order_release);

	// Transfer the active ref to reclaim_ref so Lua won't GC too early
	if (refrence > LUA_NOREF) {
		reclaim_ref = refrence;
		refrence = LUA_NOREF;
	}

	resetPlaybackState();

	// If no one is holding a pin, queue immediately for reclaim
	if (usecount.load(std::memory_order_acquire) == 0 && reclaim_ref > LUA_NOREF) {
		uv_mutex_lock(&client->inner_mutex);
		client->reclaim_list.add(reclaim_ref, this);
		uv_mutex_unlock(&client->inner_mutex);
	}
}

void AudioStream::unreference(lua_State *l) {
	uv_mutex_lock(&mutex);
	unreferenceLocked(l);
	uv_mutex_unlock(&mutex);
}

int AudioStream::process(size_t available_space, std::vector<float> &output, bool *eof_out) {
	*eof_out = false;
	int input_rate = info.samplerate;
	int input_channels = info.channels;

	double resample_ratio = (double)AUDIO_SAMPLE_RATE / input_rate;

	// How many frames are available to be written to in our 48k stereo buffer
	size_t available_output_frames = available_space / (AUDIO_PLAYBACK_CHANNELS * sizeof(float));
	// How many input frames we should read before resampling to 48k and mixing to stereo
	sf_count_t input_frames = (sf_count_t)((double)available_output_frames / resample_ratio);

	std::vector<float> input(input_frames * input_channels);

	sf_count_t frames_read = sf_readf_float(file, input.data(), input_frames);

	if (frames_read <= 0) {
		// The file ended exactly on our last read, so drain what the resampler is still holding
		*eof_out = true;
		return resampler->process(NULL, 0, resample_ratio, true, output);
	}

	std::vector<float> stereo = Pcm::toStereo(input.data(), frames_read, input_channels);

	// Flush the resampler once the source really hit the end (read less than requested)
	bool flush = frames_read < input_frames;
	int frames = resampler->process(stereo.data(), frames_read, resample_ratio, flush, output);
	if (frames < 0) {
		return -1;
	}

	// We only mark EOF if the source really hit the end,
	// not if the resampler chose to emit 0 this cycle.
	if (flush) {
		*eof_out = true;
	}
	return frames;
}

void AudioStream::fill() {
	// Write what didn't fit last time first, so nothing is skipped
	if (!pending.empty()) {
		size_t written = ringWrite(pending.data(), pending.size());
		pending.erase(pending.begin(), pending.begin() + written);
		if (!pending.empty()) {
			return;
		}
	}

	if (draining) {
		// Everything from our file is in the ring now
		draining = false;
		end = true;
		return;
	}

	std::vector<float> output;
	bool eof = false;

	int frames = process(ringSpace() * sizeof(float), output, &eof);

	if (frames > 0) {
		size_t samples = (size_t) frames * AUDIO_PLAYBACK_CHANNELS;
		size_t written = ringWrite(output.data(), samples);
		pending.assign(output.begin() + written, output.begin() + samples);
	}

	if (eof) {
		if (looping || loop_count > 0) {
			// Rewind straight away, so the ring never runs dry between loops
			if (!looping) loop_count--;
			sf_seek(file, 0, SEEK_SET);
		} else if (pending.empty()) {
			end = true;
		} else {
			draining = true;
		}
	}
}
