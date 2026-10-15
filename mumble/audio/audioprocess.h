#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <uv.h>
#include <vector>

// Runs a pipeline of commands without a shell, piping each one's stdout into the next one's stdin,
// and hands whatever the last one writes to stdout to on_data
class AudioProcess {
public:
	// A program and its arguments
	using Command = std::vector<std::string>;

	// on_data is called with the last command's output as it arrives, and on_end once it stops writing
	AudioProcess(std::function<void(const char *data, size_t length)> on_data, std::function<void()> on_end);
	~AudioProcess();

	AudioProcess(const AudioProcess&) = delete;
	AudioProcess& operator=(const AudioProcess&) = delete;

	// Start every command. Returns an error message on failure, with nothing left running.
	std::string start(const std::vector<Command> &commands);
	// Kill every command that's still running, and stop reading
	void kill();

	// Stop or resume reading, so the commands wait while we have nowhere to put their output
	void pause();
	void resume();

private:
	// A running command, which can outlive us until it has exited
	struct Child {
		uv_process_t handle;
		AudioProcess *owner;
		std::string name;
	};

	static void onExit(uv_process_t *handle, int64_t status, int signal);
	static void onAlloc(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf);
	static void onRead(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buf);
	static void closePipe(uv_pipe_t *pipe);
	void closeOutput();

	std::function<void(const char *data, size_t length)> on_data;
	std::function<void()> on_end;

	std::vector<Child*> children;
	// The last command's stdout
	uv_pipe_t *output = nullptr;
	bool paused = false;
	// How much the last command has written since we started it, for debugging
	uint64_t bytes_read = 0;
};
