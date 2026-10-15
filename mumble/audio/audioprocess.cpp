#include "audio/audioprocess.h"

#include "util/log.h"

#include <algorithm>
#include <csignal>
#include <cstdlib>

AudioProcess::AudioProcess(std::function<void(const char *data, size_t length)> on_data, std::function<void()> on_end)
	: on_data(std::move(on_data)), on_end(std::move(on_end)) {
}

AudioProcess::~AudioProcess() {
	kill();
}

void AudioProcess::closePipe(uv_pipe_t *pipe) {
	uv_read_stop((uv_stream_t*) pipe);
	if (!uv_is_closing((uv_handle_t*) pipe)) {
		uv_close((uv_handle_t*) pipe, [](uv_handle_t *handle) {
			delete (uv_pipe_t*) handle;
		});
	}
}

void AudioProcess::closeOutput() {
	if (output) {
		closePipe(output);
		output = nullptr;
	}
}

// A command as it would be typed, for logging
static std::string describe(const AudioProcess::Command &command) {
	std::string text;
	for (const std::string &arg : command) {
		if (!text.empty()) text += ' ';
		text += arg;
	}
	return text;
}

std::string AudioProcess::start(const std::vector<Command> &commands) {
	kill();
	paused = false;
	bytes_read = 0;

	if (commands.empty()) {
		return "no command to run";
	}

	uv_loop_t *loop = uv_default_loop();

	// The previous command's stdout, which becomes the next one's stdin
	uv_pipe_t *previous = nullptr;

	for (size_t i = 0; i < commands.size(); i++) {
		const Command &command = commands[i];
		if (command.empty()) {
			if (previous) closePipe(previous);
			kill();
			return "empty command";
		}

		std::vector<char*> args;
		for (const std::string &arg : command) {
			args.push_back((char*) arg.c_str());
		}
		args.push_back(nullptr);

		uv_pipe_t *out = new uv_pipe_t;
		uv_pipe_init(loop, out, 0);

		uv_stdio_container_t stdio[3];
		if (previous) {
			stdio[0].flags = UV_INHERIT_STREAM;
			stdio[0].data.stream = (uv_stream_t*) previous;
		} else {
			stdio[0].flags = UV_IGNORE;
		}
		stdio[1].flags = (uv_stdio_flags) (UV_CREATE_PIPE | UV_WRITABLE_PIPE);
		stdio[1].data.stream = (uv_stream_t*) out;
		// Let the commands report their problems where ours go
		stdio[2].flags = UV_INHERIT_FD;
		stdio[2].data.fd = 2;

		uv_process_options_t options = {};
		options.file = args[0];
		options.args = args.data();
		options.exit_cb = AudioProcess::onExit;
		options.stdio_count = 3;
		options.stdio = stdio;

		Child *child = new Child{{}, this, command[0]};
		child->handle.data = child;

		int error = uv_spawn(loop, &child->handle, &options);

		// The next command has its own copy of this now, so the previous one sees it close when the next one exits
		if (previous) {
			closePipe(previous);
			previous = nullptr;
		}

		if (error != 0) {
			// The handle has to be closed even though it never started
			uv_close((uv_handle_t*) &child->handle, [](uv_handle_t *handle) {
				delete (Child*) handle->data;
			});
			closePipe(out);
			kill();
			return "failed to run " + command[0] + ": " + uv_strerror(error);
		}

		children.push_back(child);
		previous = out;

		if (mumble_getLogLevel() >= LOG_DEBUG) {
			mumble_log(LOG_DEBUG, "audio process: started \"%s\" (pid %d)", describe(command).c_str(), child->handle.pid);
		}
	}

	// We read what the last command writes
	output = previous;
	output->data = this;
	uv_read_start((uv_stream_t*) output, AudioProcess::onAlloc, AudioProcess::onRead);
	return "";
}

void AudioProcess::kill() {
	for (Child *child : children) {
		mumble_log(LOG_DEBUG, "audio process: killing \"%s\" (pid %d)", child->name.c_str(), child->handle.pid);
		uv_process_kill(&child->handle, SIGTERM);
		// It may exit after we're gone, so it must forget about us
		child->owner = nullptr;
	}
	children.clear();
	closeOutput();
}

void AudioProcess::pause() {
	if (!paused && output) {
		mumble_log(LOG_CODE, "audio process: buffer full, pausing output after %llu bytes", (unsigned long long) bytes_read);
		uv_read_stop((uv_stream_t*) output);
	}
	paused = true;
}

void AudioProcess::resume() {
	if (paused && output) {
		mumble_log(LOG_CODE, "audio process: buffer has room, resuming output");
		uv_read_start((uv_stream_t*) output, AudioProcess::onAlloc, AudioProcess::onRead);
	}
	paused = false;
}

void AudioProcess::onExit(uv_process_t *handle, int64_t status, int signal) {
	Child *child = (Child*) handle->data;

	mumble_log(LOG_DEBUG, "audio process: \"%s\" (pid %d) exited with status %lld (signal %d)%s",
	           child->name.c_str(), child->handle.pid, (long long) status, signal, child->owner ? "" : " after being killed");

	if (child->owner) {
		// Exited on its own, rather than being killed by us
		std::vector<Child*> &children = child->owner->children;
		children.erase(std::remove(children.begin(), children.end(), child), children.end());

		if (status != 0 || signal != 0) {
			mumble_log(LOG_WARN, "audio process \"%s\" exited with status %lld (signal %d)", child->name.c_str(), (long long) status, signal);
		}
	}

	uv_close((uv_handle_t*) handle, [](uv_handle_t *handle) {
		delete (Child*) handle->data;
	});
}

void AudioProcess::onAlloc(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf) {
	buf->base = (char*) malloc(suggested_size);
	buf->len = buf->base ? suggested_size : 0;
}

void AudioProcess::onRead(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buf) {
	AudioProcess *process = (AudioProcess*) stream->data;

	if (nread > 0) {
		process->bytes_read += nread;
		mumble_log(LOG_TRACE, "audio process: read %zd bytes (%llu total)", nread, (unsigned long long) process->bytes_read);
		process->on_data(buf->base, nread);
	} else if (nread < 0) {
		if (nread != UV_EOF) {
			mumble_log(LOG_ERROR, "error reading audio process output: %s", uv_strerror(nread));
		}
		mumble_log(LOG_DEBUG, "audio process: output ended after %llu bytes", (unsigned long long) process->bytes_read);
		// The last command closed its stdout, which normally means it exited
		process->closeOutput();
		process->on_end();
	}

	free(buf->base);
}
