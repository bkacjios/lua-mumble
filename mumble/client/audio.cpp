#include "client/client.h"

#include "audio/audiocontext.h"
#include "audio/audioqueue.h"
#include "audio/audiostream.h"
#include "audio/encoder.h"
#include "defines.h"
#include "lua/luainc.h"
#include "lua/registry.h"
#include "net/voicepacket.h"
#include "util/buffer.h"
#include "util/log.h"

#include <cerrno>
#include <cinttypes>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <pthread.h>

void MumbleClient::audioBufferThread(void *arg) {
	pthread_setname_np(pthread_self(), "buffer");

	MumbleClient* client = (MumbleClient*) arg;

	while (true) {
		uv_mutex_lock(&client->main_mutex);
		bool running = client->audio_buffer_thread_running;
		uv_mutex_unlock(&client->main_mutex);
		if (!running) break;

		uv_mutex_lock(&client->inner_mutex);
		auto current = client->stream_list.begin();

		while (current != client->stream_list.end()) {
			AudioStream *sound = current->data;

			if (sound && sound->tryPin()) {
				uv_mutex_unlock(&client->inner_mutex);

				uv_mutex_lock(&sound->mutex);
				bool playing = sound->playing;
				bool end     = sound->end;
				size_t bsz   = sound->buffer_size;
				uv_mutex_unlock(&sound->mutex);

				// Hold the decode lock while touching the file, resampler and ring,
				// so the main thread can't reset them out from under us
				uv_mutex_lock(&sound->decode_mutex);

				if (playing && !end && !sound->end) {
					size_t used_samples  = sound->ringCount();

					if (used_samples < (bsz / 2)) {
						uint64_t start = uv_hrtime();

						sound->fill();

						float ms = (uv_hrtime() - start) / 1e6;
						if (ms > AUDIO_BUFFER_SIZE) {
							// A stutter will occur, so show a warning
							mumble_log(LOG_WARN,
							           "audio processing took %.3f ms (buffer size is %d ms)",
							           ms, AUDIO_BUFFER_SIZE);
						} else {
							mumble_log(LOG_CODE, "audio processing took %.3f ms", ms);
						}
					}
				}

				uv_mutex_unlock(&sound->decode_mutex);

				sound->unpin();

				uv_mutex_lock(&client->inner_mutex);
				auto n = client->stream_list.find(sound);
				current = (n != client->stream_list.end()) ? std::next(n) : client->stream_list.begin();
				continue;
			}

			++current;
		}

		uv_mutex_unlock(&client->inner_mutex);

		uv_sleep(client->audio_frames);
	}
}

void MumbleClient::audioPlaybackThread(void* arg) {
	pthread_setname_np(pthread_self(), "playback");

	MumbleClient* client = (MumbleClient*)arg;

	// How long we're currently waiting for, so the tick we fire produces exactly that much audio
	uint32_t scheduled_frames = client->audio_frames;

	// Use this thread as a high-accuracy timer, triggering the main thread every as often as we need it
	while (true) {
		uv_mutex_lock(&client->main_mutex);
		bool running = client->audio_playback_thread_running;
		uv_mutex_unlock(&client->main_mutex);

		if (!running) break; // shutdown

		uint64_t now = uv_hrtime();

		if (now >= client->audio_playback_next) {
			uint64_t last_us = (now - client->audio_playback_last) / 1000;
			client->audio_playback_last = now;

			mumble_log(LOG_CODE, "last audio event: %.3f ms ago", (double)last_us / 1000);

			client->audio_tick_frames = scheduled_frames;

			// Our playback async only exists while connected, and disconnect() clears connected under main_mutex before closing it
			uv_mutex_lock(&client->main_mutex);
			if (client->connected) {
				client->audio_playback_async_pending = true;
				// Signal main thread that we're ready for playback
				uv_async_send(&client->audio_playback_async);
			}
			uv_mutex_unlock(&client->main_mutex);

			// Pick up any new packet size for the next wait
			scheduled_frames = client->audio_frames;

			// Increment audio_playback_next in discrete steps to prevent falling behind
			uint64_t frame_interval_ns = (uint64_t) scheduled_frames * 1000000;

			while (now >= client->audio_playback_next) {
				client->audio_playback_next += frame_interval_ns;
			}
		}

		// Sleep until client->audio_playback_next
		struct timespec ts;
		uint64_t wake_time = client->audio_playback_next;
		ts.tv_sec = wake_time / 1000000000;
		ts.tv_nsec = wake_time % 1000000000;

		clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, NULL);
	}
}

void MumbleClient::handleAudioStreamsEnd(const std::vector<AudioStream*> &ended) {
	if (ended.empty()) return;

	// Push every stream while they're still referenced, since stopping hands their references over for reclaiming
	luaL_checkstack(l, ended.size(), "too many audio streams ended");
	int base = lua_gettop(l);
	for (AudioStream *sound : ended) {
		LuaRegistry::pushFrom(l, audio_streams, sound->refrence);
	}

	// Looping is handled by the buffer thread, so this is the real end.
	// Stop them all before any hook runs, so a hook can play or stop any of them again.
	for (AudioStream *sound : ended) {
		sound->unreference(l);
	}

	for (size_t i = 0; i < ended.size(); i++) {
		lua_pushvalue(l, base + 1 + i);
		hookCall("OnAudioStreamEnd", 1);
	}

	lua_settop(l, base);
}

bool MumbleClient::processAudioFile(AudioStream *sound, uint32_t sample_size, sf_count_t *biggest_read) {
	if (sound->reclaimed.load(std::memory_order_acquire)) {
		return false; // Already stopped; skip processing
	}

	if (sample_size > PCM_BUFFER) {
		// Our sample size is somehow too large to fit within the input buffer
		return false;
	}

	float input_buffer[PCM_BUFFER];

	// Calculate available frames from the lock-free ring
	size_t samples_avail = sound->ringCount();
	sf_count_t frames_available = (sf_count_t)(samples_avail / AUDIO_PLAYBACK_CHANNELS);

	// Cap by requested sample_size
	sf_count_t frames_to_read = (frames_available < (sf_count_t)sample_size)
	                            ? frames_available
	                            : (sf_count_t)sample_size;

	sf_count_t read = 0;

	if (frames_to_read > 0) {
		// Convert frames to samples and cap to input_buffer capacity
		size_t samples_to_read = (size_t)frames_to_read * AUDIO_PLAYBACK_CHANNELS;
		if (samples_to_read > PCM_BUFFER) {
			samples_to_read = PCM_BUFFER;
		}

		// Pull from the ring directly into input_buffer (lock-free)
		size_t got = sound->ringRead(input_buffer, samples_to_read);

		// Convert samples actually read to frames actually read
		read = (sf_count_t)(got / AUDIO_PLAYBACK_CHANNELS);
	}

	bool faded_out = false;

	if (sound->fade_frames > 0) {
		// Sound has a volume fade adjustment
		for (int i = 0; i < read; i++) {
			if (sound->fade_frames_left > 0) {
				sound->fade_frames_left = sound->fade_frames_left - 1;
				sound->fade_volume = sound->fade_to_volume + (sound->fade_from_volume - sound->fade_to_volume) * ((float) sound->fade_frames_left / sound->fade_frames);
			} else if (sound->fade_stop) {
				// Faded out, so the stream ends here
				read = i;
				faded_out = true;
				break;
			}

			float vol = sound->volume * volume * sound->fade_volume;
			audio_output[i].l += input_buffer[i * 2] * vol;
			audio_output[i].r += input_buffer[i * 2 + 1] * vol;
		}
	} else {
		// No fade needed, just adjust volume levels
		for (int i = 0; i < read; i++) {
			float vol = sound->volume * volume;
			audio_output[i].l += input_buffer[i * 2] * vol;
			audio_output[i].r += input_buffer[i * 2 + 1] * vol;
		}
	}

	if (read > *biggest_read) {
		*biggest_read = read;
	}

	if (faded_out || (sound->end && read < sample_size)) {
		// We reached the end of the stream
		mumble_log(LOG_CODE,
		           "reached end of audio stream: %" PRId64 " (%" PRIu32 ")",
		           (int64_t)read,
		           sample_size);
		return true;
	}
	return false;
}

void MumbleClient::bitrateWarning(size_t length) {
	uv_mutex_lock(&inner_mutex);
	auto current = stream_list.begin();
	uv_mutex_unlock(&inner_mutex);

	while (current != stream_list.end()) {
		uv_mutex_lock(&inner_mutex);
		AudioStream *sound = current->data;
		++current;
		bool pinned = false;
		if (sound) pinned = sound->tryPin();
		uv_mutex_unlock(&inner_mutex);

		if (pinned) {
			sound->unreference(l);
			sound->unpin();
		}
	}

	mumble_log(LOG_WARN, "Audio packet length %u greater than maximum of %u, stopping all audio streams. Try reducing the bitrate.", length, UDP_BUFFER_MAX);
}

void MumbleClient::sendLegacyAudio(uint8_t *encoded, opus_int32 encoded_len, bool end_frame, uint32_t sequence) {
	uint32_t frame_header = encoded_len;
	if (end_frame) {
		frame_header |= (1 << 13);
	}

	uint8_t packet_buffer[PCM_BUFFER];
	VoicePacket packet(packet_buffer);
	packet.setHeader(LEGACY_UDP_OPUS, audio_target, sequence);
	packet.setFrame(LEGACY_UDP_OPUS, frame_header, encoded, encoded_len);

	int len = packet.getLength();

	if (len > UDP_BUFFER_MAX) {
		bitrateWarning(len);
		return;
	}

	if (tcp_udp_tunnel) {
		mumble_log(LOG_TRACE, "[TCP] Sending legacy audio packet (size=%u, id=%u, target=%u, session=%u, sequence=%u)",
		           len, LEGACY_UDP_OPUS, audio_target, session, sequence);
		sendTunnel(packet_buffer, len);
	} else {
		mumble_log(LOG_TRACE, "[UDP] Sending legacy audio packet (size=%u, id=%u, target=%u, session=%u, sequence=%u)",
		           len, LEGACY_UDP_OPUS, audio_target, session, sequence);
		sendUDPOrTunnel(packet_buffer, len);
	}

	handleSpeakingHooksLegacy(packet_buffer + 1, LEGACY_UDP_OPUS, audio_target, session);
}

void MumbleClient::sendProtobufAudio(uint8_t *encoded, opus_int32 encoded_len, bool end_frame, uint32_t sequence) {
	MumbleUDP__Audio audio = MUMBLE_UDP__AUDIO__INIT;
	ProtobufCBinaryData audio_data = { (size_t)encoded_len, encoded };

	audio.frame_number = sequence;
	audio.opus_data = audio_data;
	audio.is_terminator = end_frame;
	audio.header_case = MUMBLE_UDP__AUDIO__HEADER_TARGET;
	audio.target = audio_target;
	audio.n_positional_data = 0;

	uint8_t packet_buffer[PCM_BUFFER];
	packet_buffer[0] = PROTO_UDP_AUDIO;

	int len = 1 + mumble_udp__audio__pack(&audio, packet_buffer + 1);

	if (len > UDP_BUFFER_MAX) {
		bitrateWarning(len);
		return;
	}

	if (tcp_udp_tunnel) {
		mumble_log(LOG_TRACE, "[TCP] Sending protobuf TCP audio packet (size=%u, id=%u, target=%u, session=%u, sequence=%u)",
		           len, LEGACY_UDP_OPUS, audio_target, session, sequence);
		sendTunnel(packet_buffer, len);
	} else {
		mumble_log(LOG_TRACE, "[UDP] Sending protobuf UDP audio packet (size=%u, id=%u, target=%u, session=%u, sequence=%u)",
		           len, LEGACY_UDP_OPUS, audio_target, session, sequence);
		sendUDPOrTunnel(packet_buffer, len);
	}

	handleSpeakingHooksProtobuf(&audio, session);
}

void MumbleClient::shutdownAudioQueues() {
	// Signal encode thread to stop
	uv_mutex_lock(&audio_encode_queue.mutex);
	audio_encode_thread_running = false;
	uv_cond_signal(&audio_encode_queue.cond);
	uv_mutex_unlock(&audio_encode_queue.mutex);

	// Join encode thread
	uv_thread_join(&audio_encode_thread);

	// Cleanup encode and send queues
	audio_encode_queue.cleanup();
	audio_send_queue.cleanup();
}

void MumbleClient::encodeAudio(sf_count_t frame_size, bool end_frame) {
	if (frame_size > MAX_PCM_FRAMES) {
		mumble_log(LOG_ERROR, "frame_size %zu exceeds MAX_PCM_FRAMES %d", frame_size, MAX_PCM_FRAMES);
		return;
	}

	AudioWork *work = new (std::nothrow) AudioWork;
	if (!work) {
		mumble_log(LOG_ERROR, "failed to allocate audio work");
		return;
	}

	work->client = this;
	work->frame_size = frame_size;
	work->end_frame = end_frame;
	work->audio_sequence = audio_sequence;
	work->connection_id = connection_id;
	// Mumble numbers audio in 10ms frames, so advance by how many this packet holds
	audio_sequence += frame_size / (AUDIO_SAMPLE_RATE / 100);
	memcpy(work->pcm, audio_output, frame_size * sizeof(AudioFrame));

	audio_encode_queue.push(work);
	mumble_log(LOG_CODE, "queued %zu frames of audio for encoding", frame_size);
}

void MumbleClient::audioEncodeThread(void *arg) {
	pthread_setname_np(pthread_self(), "encode");

	MumbleClient *client = (MumbleClient *)arg;

	while (true) {
		// Block and pop from encode queue, which checks our running flag under its lock
		AudioWork *work = client->audio_encode_queue.pop(&client->audio_encode_thread_running);
		if (!work) {
			// Only will happen on shutdown
			break;
		}

		uint64_t start = uv_hrtime();

		// Encode audio
		work->encoded_len = client->encoder->encodeFloat((float *)work->pcm,
		                                                 work->frame_size,
		                                                 work->encoded,
		                                                 PAYLOAD_SIZE_MAX);

		work->encode_time = (uv_hrtime() - start) / 1e6;

		mumble_log(LOG_CODE, "audio encode: %.3f ms", work->encode_time);

		// Push to send queue
		client->audio_send_queue.push(work);
	}
}

void MumbleClient::encodeEvent() {
	lua_stackguard_entry(l);

	sf_count_t biggest_read = 0;
	// Produce as much audio as the playback timer waited for, even if audio_frames changed in the meantime
	const uint32_t frame_ms = audio_tick_frames;
	const sf_count_t output_frames = frame_ms * AUDIO_SAMPLE_RATE / 1000;

	// clear the mix buffer for this frame
	memset(audio_output, 0, sizeof(audio_output));

	// Handle any playing audio files.
	// No Lua runs while we walk the list, so nothing on this thread can change it under us.
	std::vector<AudioStream*> ended;

	uv_mutex_lock(&inner_mutex);
	auto current = stream_list.begin();

	while (current != stream_list.end()) {
		AudioStream *sound = current->data;
		++current;
		if (sound && sound->tryPin()) {
			uv_mutex_unlock(&inner_mutex);
			if (sound->playing) {
				uv_mutex_lock(&sound->mutex);
				if (processAudioFile(sound, output_frames, &biggest_read)) {
					ended.push_back(sound);
				}
				uv_mutex_unlock(&sound->mutex);
			}
			sound->unpin();
			uv_mutex_lock(&inner_mutex);
		}
	}

	uv_mutex_unlock(&inner_mutex);

	// Now that we hold no locks, it's safe to call into Lua
	handleAudioStreamsEnd(ended);

	// Hook allows for feeding raw PCM data into an audio buffer, mixing it into other playing audio
	lua_pushinteger(l, AUDIO_SAMPLE_RATE);
	lua_pushinteger(l, AUDIO_PLAYBACK_CHANNELS);
	lua_pushinteger(l, output_frames);
	hookCall("OnAudioStream", 3);

	// Get our list of audio pipes
	uv_mutex_lock(&inner_mutex);
	auto pipe = audio_pipes.begin();
	uv_mutex_unlock(&inner_mutex);

	std::vector<float> resampled_audio;

	while (pipe != audio_pipes.end()) {
		ByteBuffer *buffer = pipe->data;
		++pipe;

		AudioContext* context = buffer->context;

		// No context is bad..
		if (!context) continue;

		int resampled_frames = context->read(buffer, frame_ms, volume, resampled_audio);

		if (resampled_frames > 0) {
			for (int i = 0; i < resampled_frames; i++) {
				audio_output[i].l += resampled_audio[i * 2];
				audio_output[i].r += resampled_audio[i * 2 + 1];
			}
		}

		// Update biggest_read if necessary
		if (resampled_frames > biggest_read) {
			biggest_read = resampled_frames;
		}
	}

	// We were transmitting, and this frame is short or empty, so every source has run out (ended, paused or stopped).
	// Mark it as the end of the transmission, so listeners stop showing us as talking.
	bool end_frame = audio_stream_active && biggest_read < output_frames;

	if (biggest_read > 0 || end_frame) {
		// Encode audio and queue it up for sending
		encodeAudio(output_frames, end_frame);
	}

	audio_stream_active = biggest_read > 0 && !end_frame;

	lua_stackguard_exit(l);
}

void MumbleClient::sendEvent() {
	AudioWork *work = audio_send_queue.popNonBlocking();

	if (work != NULL && work->connection_id != connection_id) {
		// Mixed before we reconnected, so it doesn't belong on this connection
		delete work;
	} else if (work != NULL && work->encoded_len < 0) {
		mumble_log(LOG_ERROR, "opus encoding error: %s", opus_strerror(work->encoded_len));
		delete work;
	} else if (work != NULL) {
		// We have something to send
		if (legacy) {
			sendLegacyAudio(work->encoded, work->encoded_len,
			                work->end_frame, work->audio_sequence);
		} else {
			sendProtobufAudio(work->encoded, work->encoded_len,
			                  work->end_frame, work->audio_sequence);
		}
		// We can finally free our work
		delete work;
	}
}

void MumbleClient::cleanReclaimed() {
	// Handle any deferred unrefs safely on the main thread
	while (true) {
		uv_mutex_lock(&inner_mutex);
		if (!reclaim_list.empty()) {
			// Pop one by index so remove frees the node
			auto &node = reclaim_list.front();
			int idx = node.index;
			AudioStream *sound = node.data;
			reclaim_list.remove(idx);
			uv_mutex_unlock(&inner_mutex);
			// Now it's safe to release the Lua registry ref; GC may run.
			if (sound->reclaim_ref > LUA_NOREF) {
				LuaRegistry::unrefIn(l, audio_streams, &sound->reclaim_ref);
			}
		} else {
			uv_mutex_unlock(&inner_mutex);
			break;
		}
	}
}

void MumbleClient::onAudioPlaybackAsync(uv_async_t* handle) {
	MumbleClient* client = (MumbleClient*)handle->data;

	client->cleanReclaimed();

	// This playback async callback is called every frame duration
	if (client->audio_playback_async_pending) {
		client->audio_playback_async_pending = false;

		if (client->connected) {
			// Encode audio if we can
			client->encodeEvent();
			// Send audio if we can
			client->sendEvent();
		}
	}
}

/*
	General info on how this all works.
	Lua and opus encoding is not thread safe.
	Create multiple threads to handle all data processing,
	eventually handing off the results back to our main thread.

	MumbleClient::audioBufferThread
		- Reads 2 seconds of PCM data from any playing audio file
		- Resamples PCM data to 48000hz, and converts to stereo if needed
		- Saves PCM data in a circular buffer on the AudioStream struct

	MumbleClient::audioPlaybackThread
		- Triggers "MumbleClient::onAudioPlaybackAsync" asynchronously on a high-accuracy timer
		- If we are sending audio in 20ms chunks, this timer will trigger as close to every 20ms it can

	MumbleClient::onAudioPlaybackAsync
		- Ran on our main thread, so Lua is safe to be only be called from here
		- encodeEvent
			* Loops through all active audio sources and queues up 20ms of PCM audio to be encoded
		- sendEvent
			* Pops a single chunk of encoded audio from our send queue, then transmits it

	MumbleClient::audioEncodeThread
		- Encodes PCM data sent to the queue from MumbleClient::onAudioPlaybackAsync
		- Queues it up to be sent in MumbleClient::onAudioPlaybackAsync
*/
