#pragma once

#include "audio/audioprocess.h"
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
	// Play the raw float PCM the last of a pipeline of commands writes to stdout. Returns an error message on failure.
	// The commands start straight away, buffering ahead until we play, and run again whenever we're played after stopping.
	std::string openProcess(const std::vector<AudioProcess::Command> &commands, int samplerate, int channels);
	// Whether we play a command's output, rather than a file
	bool isProcess() const { return command != nullptr; }

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

	// Move audio held back from our command into our ring, as it makes room, and keep our command reading
	void pump();

private:
	// Make our ring and resampler. Returns an error message on failure.
	std::string allocate(int quality);
	void onProcessData(const char *data, size_t length);
	void onProcessEnd();
	// Write our command's resampled audio to our ring, holding back what doesn't fit
	void queueProcessAudio(const float *samples, size_t count);

public:

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

	// The commands we play the output of, when we aren't a file
	std::unique_ptr<AudioProcess> command;
	std::vector<AudioProcess::Command> commands;
	int process_samplerate = 0;
	int process_channels = 0;
	// The start of a frame from our command that hasn't fully arrived yet
	std::string partial;
	// Whether we've been played since our command started, so playing again runs it again
	bool process_played = false;
	// Our file ran out, and we only need to write what's pending before we end. Guarded by decode_mutex.
	bool draining = false;
};
