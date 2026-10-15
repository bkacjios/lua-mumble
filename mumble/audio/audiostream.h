#pragma once

#include "audio/resampler.h"
#include "lua/luainc.h"

#include <atomic>
#include <memory>
#include <sndfile.h>
#include <string>
#include <uv.h>
#include <vector>

class MumbleClient;

class AudioStream {
public:
	explicit AudioStream(MumbleClient *client);
	~AudioStream();

	// Open an audio file, resampled with the given libsamplerate quality. Returns an error message on failure.
	std::string open(const char *path, int quality);

	// Resume if we were paused, returns false if we aren't active
	bool resume();
	// Start playing from the beginning, referencing the stream on top of the Lua stack (popping it)
	void start(lua_State *l);
	// Resume if we were paused, otherwise start, referencing the stream at a Lua stack index
	void play(lua_State *l, int index);
	void pause();
	void stop(lua_State *l);
	bool isLooping() const;
	sf_count_t seek(sf_count_t offset, int whence);
	void setLooping(bool loop);
	void setLoopCount(int count);
	void fadeTo(float volume, float seconds);
	void fadeOut(float seconds);

	// Pin the stream so it can't be reclaimed while in use by a thread
	bool tryPin();
	// On the *last* unpin of a reclaimed stream, ask the main thread to release reclaim_ref
	void unpin();

	size_t ringCount() const;
	size_t ringSpace() const;
	size_t ringWrite(const float *src, size_t count);
	size_t ringRead(float *dst, size_t count);

	void reference(lua_State *l);
	void unreference(lua_State *l);
	void unreferenceLocked(lua_State *l);
	void resetPlaybackState();

	// Read, downmix and resample audio from our file to fill up to available_space bytes.
	// Returns the number of stereo frames written to output, or -1 on error.
	int process(size_t available_space, std::vector<float> &output, bool *eof_out);
	// Top up our ring from our file, handling looping and the end of the file.
	// Called by the buffer thread with decode_mutex held.
	void fill();

	MumbleClient *client;
	SNDFILE *file = nullptr;
	bool closed = false;
	std::atomic<bool> playing{false};
	float volume = 1.0f;
	SF_INFO info = {};
	int refrence = LUA_NOREF;
	int reclaim_ref = LUA_NOREF;
	std::atomic<int> loop_count{0};
	std::atomic<bool> looping{false};
	bool fade_stop = false;
	sf_count_t fade_frames = 0;
	sf_count_t fade_frames_left = 0;
	float fade_volume = 1.0f;
	float fade_from_volume = 0.0f;
	float fade_to_volume = 0.0f;
	float* buffer = nullptr;
	size_t read_position = 0;
	size_t write_position = 0;
	size_t buffer_size = 0;
	std::atomic<size_t> used{0};
	std::atomic<size_t> head{0};
	std::atomic<size_t> tail{0};
	std::atomic<int> usecount{0};
	std::atomic<bool> reclaimed{false};
	std::atomic<bool> end{false};
	uv_mutex_t mutex;
	// Guards the file, resampler and ring writes between the buffer thread and the main thread
	uv_mutex_t decode_mutex;
	std::unique_ptr<Resampler> resampler;
	// Resampled audio that didn't fit in our ring yet, guarded by decode_mutex
	std::vector<float> pending;
	// Our file ran out, and we only need to write what's pending before we end. Guarded by decode_mutex.
	bool draining = false;
};
