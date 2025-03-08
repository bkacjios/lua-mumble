#include "mumble.h"
#include "packet.h"
#include "audio.h"
#include "util.h"
#include "log.h"

#include <math.h>
#include <unistd.h>

uint8_t util_set_varint(uint8_t buffer[], const uint64_t value) {
	if (value < 0x80) {
		buffer[0] = value;
		return 1;
	} else if (value < 0x4000) {
		buffer[0] = (value >> 8) | 0x80;
		buffer[1] = value & 0xFF;
		return 2;
	} else if (value < 0x200000) {
		buffer[0] = (value >> 16) | 0xC0;
		buffer[1] = (value >> 8) & 0xFF;
		buffer[2] = value & 0xFF;
		return 3;
	} else if (value < 0x10000000) {
		buffer[0] = (value >> 24) | 0xE0;
		buffer[1] = (value >> 16) & 0xFF;
		buffer[2] = (value >> 8) & 0xFF;
		buffer[3] = value & 0xFF;
		return 4;
	} else if (value < 0x100000000) {
		buffer[0] = 0xF0;
		buffer[1] = (value >> 24) & 0xFF;
		buffer[2] = (value >> 16) & 0xFF;
		buffer[3] = (value >> 8) & 0xFF;
		buffer[4] = value & 0xFF;
		return 5;
	} else {
		buffer[0] = 0xF4;
		buffer[1] = (value >> 56) & 0xFF;
		buffer[2] = (value >> 48) & 0xFF;
		buffer[3] = (value >> 40) & 0xFF;
		buffer[4] = (value >> 32) & 0xFF;
		buffer[5] = (value >> 24) & 0xFF;
		buffer[6] = (value >> 16) & 0xFF;
		buffer[7] = (value >> 8) & 0xFF;
		buffer[8] = value & 0xFF;
		return 9;
	}
	return 0;
}

uint64_t util_get_varint(uint8_t buffer[], int *len) {
	uint8_t v = buffer[0];
	uint64_t i = 0;

	if ((v & 0x80) == 0x00) {
		i = (v & 0x7F);
		*len += 1;
	} else if ((v & 0xC0) == 0x80) {
		i = (v & 0x3F) << 8 | buffer[1];
		*len += 2;
	} else if ((v & 0xF0) == 0xF0) {
		switch (v & 0xFC) {
		case 0xF0:
			i = (uint64_t)buffer[1] << 24 | (uint64_t)buffer[2] << 16 |
			    (uint64_t)buffer[3] << 8 | (uint64_t)buffer[4];
			*len += 5;
			break;
		case 0xF4:
			i = (uint64_t)buffer[1] << 56 | (uint64_t)buffer[2] << 48 |
			    (uint64_t)buffer[3] << 40 | (uint64_t)buffer[4] << 32 |
			    (uint64_t)buffer[5] << 24 | (uint64_t)buffer[6] << 16 |
			    (uint64_t)buffer[7] << 8 | (uint64_t)buffer[8];
			*len += 9;
			break;
		case 0xF8:
			i = ~i;
			*len += 1;
			break;
		case 0xFC:
			i = v & 0x03;
			i = ~i;
			*len += 1;
			break;
		default:
			i = 0;
			*len += 1;
			break;
		}
	} else if ((v & 0xF0) == 0xE0) {
		i = (v & 0x0F) << 24 | buffer[1] << 16 | buffer[2] << 8 | buffer[3];
		*len += 4;
	} else if ((v & 0xE0) == 0xC0) {
		i = (v & 0x1F) << 16 | buffer[1] << 8 | buffer[2];
		*len += 3;
	}

	return i;
}

VoicePacket * voicepacket_init(VoicePacket *packet, uint8_t *buffer) {
	if (packet == NULL || buffer == NULL) {
		return NULL;
	}
	packet->buffer = buffer;
	packet->length = 0;
	packet->header_length = 0;
	return packet;
}

int voicepacket_setheader(VoicePacket *packet, const uint8_t type, const uint8_t target, const uint32_t sequence) {
	int offset;
	if (packet == NULL) {
		return -1;
	}
	if (packet->length > 0) {
		return -2;
	}
	packet->buffer[0] = ((type & 0x7) << 5) | (target & 0x1F);
	offset = util_set_varint(packet->buffer + 1, sequence);
	packet->length = packet->header_length = 1 + offset;
	return 1;
}

int voicepacket_setframe(VoicePacket *packet, const uint8_t type, const uint16_t frame_header, uint8_t *buffer, size_t buffer_len) {
	int offset = 0;

	if (packet == NULL || buffer == NULL || buffer_len <= 0 || buffer_len >= 0x2000) {
		return -1;
	}
	if (packet->header_length <= 0) {
		return -2;
	}

	if (type == LEGACY_UDP_OPUS) {
		// Opus uses a varint for the frame header
		offset = util_set_varint(packet->buffer + packet->header_length, frame_header);
	} else if (type == LEGACY_UDP_SPEEX || type == LEGACY_UDP_CELT_ALPHA || type == LEGACY_UDP_CELT_BETA) {
		// Every other codec uses a single byte as the frame header
		offset = 1;
		packet->buffer[packet->header_length] = frame_header;
	}

	if (offset <= 0) {
		return -3;
	}
	memmove(packet->buffer + packet->header_length + offset, buffer, buffer_len);
	packet->length = packet->header_length + buffer_len + offset;
	return 1;
}

int voicepacket_getlength(const VoicePacket *packet) {
	if (packet == NULL) {
		return -1;
	}
	return packet->length;
}

static void audio_transmission_reference(lua_State *l, AudioStream *sound) {
	sound->refrence = mumble_registry_ref(l, sound->client->audio_streams);
	uv_mutex_lock(&sound->client->inner_mutex);
	list_add(&sound->client->stream_list, sound->refrence, sound);
	uv_mutex_unlock(&sound->client->inner_mutex);
}

void audio_transmission_unreference(lua_State*l, AudioStream *sound) {
	uv_mutex_lock(&sound->client->inner_mutex);
	list_remove(&sound->client->stream_list, sound->refrence);
	uv_mutex_unlock(&sound->client->inner_mutex);
	mumble_registry_unref(l, sound->client->audio_streams, &sound->refrence);
	sound->playing = false;
	sound->fade_volume = 1.0f;
	sound->fade_frames = 0;
	sound->fade_frames_left = 0;
	sound->fade_stop = false;
	if (sound->file) {
		sf_seek(sound->file, 0, SEEK_SET);
	}
}

float* convert_mono_to_multi(const float* input_buffer, sf_count_t frames_read, int channels) {
	float *multi_buffer = (float *)malloc(frames_read * channels * sizeof(float));
	if (!multi_buffer) return NULL;

	for (int i = 0; i < frames_read; i++) {
		for (int ch = 0; ch < channels; ch++) {
			multi_buffer[i * channels + ch] = input_buffer[i];
		}
	}
	return multi_buffer;
}

float* downmix_to_stereo(const float* input_buffer, sf_count_t frames_read, int input_channels) {
	float *stereo_buffer = (float *)malloc(frames_read * 2 * sizeof(float));
	if (!stereo_buffer) return NULL;

	for (sf_count_t i = 0; i < frames_read; i++) {
		float left = 0.0f, right = 0.0f;
		for (int ch = 0; ch < input_channels; ch++) {
			if (ch % 2 == 0)
				left += input_buffer[i * input_channels + ch];
			else
				right += input_buffer[i * input_channels + ch];
		}
		stereo_buffer[i * 2] = left / input_channels;
		stereo_buffer[i * 2 + 1] = right / input_channels;
	}
	return stereo_buffer;
}

int resample_audio(SRC_STATE *src_state, const float *input_buffer, float **output_buffer, sf_count_t input_frames, sf_count_t output_frames, double resample_ratio, bool end_of_input) {
	*output_buffer = (float *)malloc(output_frames * AUDIO_PLAYBACK_CHANNELS * sizeof(float));

	if (!*output_buffer) {
		return -1;
	}

	if (resample_ratio == 1.0) {
		memcpy(*output_buffer, input_buffer, input_frames * AUDIO_PLAYBACK_CHANNELS * sizeof(float));
		return input_frames;
	}

	SRC_DATA src_data = {
		.data_in = input_buffer,
		.data_out = *output_buffer,
		.input_frames = input_frames,
		.output_frames = output_frames,
		.end_of_input = end_of_input,
		.src_ratio = resample_ratio
	};

	int error = src_process(src_state, &src_data);
	if (error != 0) {
		mumble_log(LOG_ERROR, "error resampling audio: %s", src_strerror(error));
		free(*output_buffer);
		return -1;
	}

	if (end_of_input) {
		error = src_reset(src_state);
		if (error != 0) {
			mumble_log(LOG_ERROR, "error resetting audio file resampler state: %s", src_strerror(error));
			free(*output_buffer);
			return -1;
		}
	}

	return src_data.output_frames_gen;
}

int process_audio(AudioStream* sound, float **output_data, size_t available_space, size_t *frames_out) {
	*frames_out = 0;
	int input_rate = sound->info.samplerate;
	int input_channels = sound->info.channels;
	double resample_ratio = (double)AUDIO_SAMPLE_RATE / input_rate;

	size_t available_frames = available_space / input_channels;
	sf_count_t input_frames = available_frames / resample_ratio;
	sf_count_t output_frames = (sf_count_t) input_frames * resample_ratio;

	float *input_buffer = (float *)malloc(input_frames * input_channels * sizeof(float));
	if (!input_buffer) return -1;

	sf_count_t frames_read = sf_readf_float(sound->file, input_buffer, input_frames);
	if (frames_read <= 0) {
		free(input_buffer);
		return 0;
	}

	if (input_channels == 1) {
		float *stereo_buffer = convert_mono_to_multi(input_buffer, frames_read, 2);
		if (!stereo_buffer) {
			free(input_buffer);
			return -1;
		}
		free(input_buffer);
		input_buffer = stereo_buffer;
		input_channels = 2;
	} else if (input_channels > 2) {
		float *stereo_buffer = downmix_to_stereo(input_buffer, frames_read, input_channels);
		if (!stereo_buffer) {
			free(input_buffer);
			return -1;
		}
		free(input_buffer);
		input_buffer = stereo_buffer;
		input_channels = 2;
	}

	int resampled_frames = resample_audio(sound->src_state, input_buffer, output_data, frames_read, output_frames, resample_ratio, frames_read < input_frames);
	if (!output_data) {
		free(input_buffer);
		return -1;
	}

	free(input_buffer);
	if (resampled_frames < 0) {
		return -1;
	}

	*frames_out = resampled_frames;
	return 0;
}

void mumble_audio_thread(void *arg) {
	MumbleClient* client = (MumbleClient*) arg;

	while (true) {
		uv_mutex_lock(&client->main_mutex);
		bool running = client->audio_thread_running;

		uv_mutex_lock(&client->inner_mutex);
		LinkNode* current = client->stream_list;
		uv_mutex_unlock(&client->inner_mutex);

		if (!running) {
			uv_mutex_unlock(&client->main_mutex);
			break;
		}

		while (current) {
			uv_mutex_lock(&client->inner_mutex);
			AudioStream *sound = current->data;
			current = current->next;
			uv_mutex_unlock(&client->inner_mutex);

			if (sound) {
				uv_mutex_lock(&sound->mutex);
				bool playing = sound->playing;
				size_t buffer_size = sound->buffer_size;
				size_t available_space = (sound->read_position > sound->write_position) ?
				                         (sound->read_position - sound->write_position - 1) :
				                         (sound->buffer_size - sound->write_position + sound->read_position - 1);
				size_t first_chunk = (sound->buffer_size - sound->write_position) / sound->info.channels;
				uv_mutex_unlock(&sound->mutex);

				if (playing) {
					// Only buffer when it's half empty
					if (available_space <= buffer_size / 2) {
						continue;
					}

					size_t frames_read;

					float* output_audio = NULL;
					process_audio(sound, &output_audio, available_space, &frames_read);

					if (frames_read > 0 && output_audio) {
						uv_mutex_lock(&sound->mutex);
						if (first_chunk >= frames_read) {
							// Simple case: Copy all at once
							memcpy(sound->buffer + sound->write_position, output_audio, frames_read * sound->info.channels * sizeof(float));
						} else {
							// Copy first part until the end of the buffer
							memcpy(sound->buffer + sound->write_position, output_audio, first_chunk * sound->info.channels * sizeof(float));

							// Copy second part from the beginning of the buffer
							memcpy(sound->buffer, output_audio + first_chunk * sound->info.channels, (frames_read - first_chunk) * sound->info.channels * sizeof(float));
						}

						// Correctly wrap around the write position
						sound->write_position = (sound->write_position + frames_read * sound->info.channels) % sound->buffer_size;
						uv_mutex_unlock(&sound->mutex);
					}

					if (output_audio) {
						free(output_audio);
					}
				}
			}
		}
		uv_mutex_unlock(&client->main_mutex);

		// Sleep at the same rate we are outputting audio
		uv_sleep(client->audio_frames);
	}
}

void mumble_audio_idle(uv_idle_t* handle) {
	uint64_t current_time = uv_hrtime();

	MumbleClient* client = (MumbleClient*) handle->data;
	lua_State *l = client->l;

	if (current_time >= client->audio_idle_next) {

		uint64_t last_time_us = (current_time - client->audio_idle_last) / 1000;

		client->audio_idle_last = current_time;

		mumble_log(LOG_TRACE, "last audio event: %.3f ms ago", (double)last_time_us / 1000);

		if (client->connected) {
			audio_transmission_event(l, client);
		}

		uint64_t end_time = uv_hrtime();

		// Convert processing time from nanoseconds to microseconds
		uint64_t process_time_us = (end_time - current_time) / 1000;

		mumble_log(LOG_TRACE, "audio transmission took: %.3f ms", (double)process_time_us / 1000);

		// Calculate next trigger time based on fixed intervals
		uint64_t frame_interval = client->audio_frames * 1000000;

		// Increment audio_idle_next in discrete steps to prevent falling behind
		while (current_time >= client->audio_idle_next) {
			client->audio_idle_next += frame_interval;
		}

	} else {
		uint64_t time_until_next = (client->audio_idle_next - current_time) / 1000;

		struct timespec ts;
		ts.tv_sec = 0;

		// Adaptive sleep
		if (time_until_next > 1000) {
			// If more than 1 ms away, sleep more
			ts.tv_nsec = 1000000;	// 1 ms
		} else {
			// Sleep less while close
			ts.tv_nsec = 500000;	// 0.5ms
		}

		nanosleep(&ts, NULL);
	}
}

static void handle_audio_stream_end(lua_State *l, MumbleClient *client, AudioStream *sound, bool *didLoop) {
	sf_seek(sound->file, 0, SEEK_SET);
	if (sound->looping) {
		*didLoop = true;
	} else if (sound->loop_count > 0) {
		*didLoop = true;
		sound->loop_count--;
	} else {
		mumble_registry_pushref(l, client->audio_streams, sound->refrence);
		mumble_hook_call(client, "OnAudioStreamEnd", 1);
		audio_transmission_unreference(l, sound);
	}
}

static void process_audio_file(lua_State *l, MumbleClient *client, AudioStream *sound, uint32_t frame_size, sf_count_t *biggest_read, bool *didLoop) {
	sf_count_t sample_size = (sf_count_t) client->audio_frames * (float) AUDIO_SAMPLE_RATE / 1000;

	if (sample_size > PCM_BUFFER || sound->write_position == sound->read_position) {
		// Our sample size is somehow too large to fit within the input buffer,
		// or our sound file buffer is empty..
		return;
	}

	float input_buffer[PCM_BUFFER];

	// Calculate how much data is available
	size_t used_space = (sound->write_position >= sound->read_position) ?
	                    (sound->write_position - sound->read_position) :
	                    (sound->buffer_size - sound->read_position + sound->write_position);

	sf_count_t frames_available = used_space / AUDIO_PLAYBACK_CHANNELS;
	sf_count_t frames_to_read = (frames_available < sample_size) ? frames_available : sample_size;

	sf_count_t read = 0;

	if (frames_to_read > 0) {
		size_t samples_to_read = frames_to_read * AUDIO_PLAYBACK_CHANNELS;
		size_t first_chunk = (sound->buffer_size - sound->read_position < samples_to_read) ?
		                     (sound->buffer_size - sound->read_position) : samples_to_read;
		size_t second_chunk = samples_to_read - first_chunk;

		// Read first chunk from buffer
		memcpy(input_buffer, sound->buffer + sound->read_position, first_chunk * sizeof(float));

		// Read second chunk if wrap-around occurs
		if (second_chunk > 0) {
			memcpy(input_buffer + first_chunk, sound->buffer, second_chunk * sizeof(float));
		}

		// Update read position
		sound->read_position = (sound->read_position + samples_to_read) % sound->buffer_size;
		read = frames_to_read; // Store actual frames read
	}

	if (sound->fade_frames > 0) {
		// Sound has a volume fade adjustment
		for (int i = 0; i < read; i++) {
			if (sound->fade_frames_left > 0) {
				sound->fade_frames_left = sound->fade_frames_left - 1;
				sound->fade_volume = sound->fade_to_volume + (sound->fade_from_volume - sound->fade_to_volume) * ((float) sound->fade_frames_left / sound->fade_frames);
			} else if (sound->fade_stop) {
				// Fake end of stream
				read = 0;
				sound->fade_volume = 0.0f;
			}

			float volume = sound->volume * client->volume * sound->fade_volume;
			client->audio_output[i].l += input_buffer[i * 2] * volume;
			client->audio_output[i].r += input_buffer[i * 2 + 1] * volume;
		}
	} else {
		// No fade needed, just adjust volume levels
		for (int i = 0; i < read; i++) {
			float volume = sound->volume * client->volume;
			client->audio_output[i].l += input_buffer[i * 2] * volume;
			client->audio_output[i].r += input_buffer[i * 2 + 1] * volume;
		}
	}

	if (read < sample_size) {
		// We reached the end of the stream
		handle_audio_stream_end(l, client, sound, didLoop);
	}

	if (read > *biggest_read) {
		*biggest_read = read;
	}
}

static void audio_transmission_bitrate_warning(MumbleClient *client, size_t length) {
	LinkNode* current = client->stream_list;

	if (current) {
		// Cleanup any active audio transmissions
		while (current != NULL) {
			audio_transmission_unreference(client->l, current->data);
			current = current->next;
		}
	}

	mumble_log(LOG_WARN, "Audio packet length %u greater than maximum of %u, stopping all audio streams. Try reducing the bitrate.", length, UDP_BUFFER_MAX);
}

static void send_legacy_audio(MumbleClient *client, uint8_t *encoded, opus_int32 encoded_len, bool end_frame) {
	uint32_t frame_header = encoded_len;
	if (end_frame) {
		frame_header |= (1 << 13);
	}

	VoicePacket packet;
	uint8_t packet_buffer[PCM_BUFFER];
	voicepacket_init(&packet, packet_buffer);
	voicepacket_setheader(&packet, LEGACY_UDP_OPUS, client->audio_target, client->audio_sequence);
	voicepacket_setframe(&packet, LEGACY_UDP_OPUS, frame_header, encoded, encoded_len);

	int len = voicepacket_getlength(&packet);

	if (len > UDP_BUFFER_MAX) {
		audio_transmission_bitrate_warning(client, len);
		return;
	}

	if (client->tcp_udp_tunnel) {
		mumble_log(LOG_TRACE, "[TCP] Sending legacy audio packet (size=%u, id=%u, target=%u, session=%u, sequence=%u)",
		           len, LEGACY_UDP_OPUS, client->audio_target, client->session, client->audio_sequence);
		packet_sendex(client, PACKET_UDPTUNNEL, packet_buffer, NULL, len);
	} else {
		mumble_log(LOG_TRACE, "[UDP] Sending legacy audio packet (size=%u, id=%u, target=%u, session=%u, sequence=%u)",
		           len, LEGACY_UDP_OPUS, client->audio_target, client->session, client->audio_sequence);
		packet_sendudp(client, packet_buffer, len);
	}

	mumble_handle_speaking_hooks_legacy(client, packet_buffer + 1, LEGACY_UDP_OPUS, client->audio_target, client->session);
}

static void send_protobuf_audio(MumbleClient *client, uint8_t *encoded, opus_int32 encoded_len, bool end_frame) {
	MumbleUDP__Audio audio = MUMBLE_UDP__AUDIO__INIT;
	ProtobufCBinaryData audio_data = { .data = encoded, .len = (size_t)encoded_len };

	audio.frame_number = client->audio_sequence;
	audio.opus_data = audio_data;
	audio.is_terminator = end_frame;
	audio.target = client->audio_target;
	audio.n_positional_data = 0;

	uint8_t packet_buffer[PCM_BUFFER];
	packet_buffer[0] = PROTO_UDP_AUDIO;

	int len = 1 + mumble_udp__audio__pack(&audio, packet_buffer + 1);

	if (len > UDP_BUFFER_MAX) {
		audio_transmission_bitrate_warning(client, len);
		return;
	}

	if (client->tcp_udp_tunnel) {
		mumble_log(LOG_TRACE, "[TCP] Sending protobuf TCP audio packet (size=%u, id=%u, target=%u, session=%u, sequence=%u)",
		           len, LEGACY_UDP_OPUS, client->audio_target, client->session, client->audio_sequence);
		packet_sendex(client, PACKET_UDPTUNNEL, packet_buffer, NULL, len);
	} else {
		mumble_log(LOG_TRACE, "[UDP] Sending protobuf UDP audio packet (size=%u, id=%u, target=%u, session=%u, sequence=%u)",
		           len, LEGACY_UDP_OPUS, client->audio_target, client->session, client->audio_sequence);
		packet_sendudp(client, packet_buffer, len);
	}

	mumble_handle_speaking_hooks_protobuf(client, &audio, client->session);
}

static void encode_audio_work(uv_work_t *req) {
	audio_work_t *work = (audio_work_t *)req->data;
	uint64_t start = uv_hrtime();

	work->encoded_len = opus_encode_float(work->client->encoder,
	                                      (float *)work->client->audio_output,
	                                      work->frame_size,
	                                      work->encoded,
	                                      PAYLOAD_SIZE_MAX);

	work->encode_time = (uv_hrtime() - start) / 1e6;
}

static void send_audio_after(uv_work_t *req, int status) {
	audio_work_t *work = (audio_work_t *)req->data;

	if (status == 0 && work->encoded_len > 0) {
		mumble_log(LOG_TRACE, "audio encode: %.3f ms", work->encode_time);

		uint64_t start = uv_hrtime();
		if (work->client->legacy) {
			send_legacy_audio(work->client, work->encoded, work->encoded_len, work->end_frame);
		} else {
			send_protobuf_audio(work->client, work->encoded, work->encoded_len, work->end_frame);
		}
		mumble_log(LOG_TRACE, "audio send: %.3f ms", (uv_hrtime() - start) / 1e6);
	}

	free(work);
	free(req);
}

static void encode_and_send_audio(MumbleClient *client, sf_count_t frame_size, bool end_frame) {
	audio_work_t *work = malloc(sizeof(audio_work_t));
	if (!work) {
		mumble_log(LOG_ERROR, "failed to allocate memory for audio work");
		return;
	}

	uv_work_t *req = malloc(sizeof(uv_work_t));
	if (!req) {
		free(work);
		mumble_log(LOG_ERROR, "failed to allocate memory for work request");
		return;
	}

	req->data = work;
	work->req = *req;
	work->client = client;
	work->frame_size = frame_size;
	work->end_frame = end_frame;
	work->encoded_len = 0;
	work->encode_time = 0;
	work->client->audio_sequence++;

	int status = uv_queue_work(uv_default_loop(), req, encode_audio_work, send_audio_after);
	if (status != 0) {
		mumble_log(LOG_ERROR, "failed to queue work for encoding and sending audio: %s", uv_strerror(status));
		free(work);
		free(req);
	}
}

void audio_transmission_event(lua_State *l, MumbleClient *client) {
	lua_stackguard_entry(l);

	sf_count_t biggest_read = 0;
	const sf_count_t frame_size = client->audio_frames * AUDIO_SAMPLE_RATE / 1000;

	bool didLoop = false;

	uv_mutex_lock(&client->inner_mutex);
	LinkNode *current = client->stream_list;
	uv_mutex_unlock(&client->inner_mutex);

	memset(client->audio_output, 0, sizeof(client->audio_output));

	while (current != NULL) {
		uv_mutex_lock(&client->inner_mutex);
		AudioStream *sound = current->data;
		current = current->next;
		uv_mutex_unlock(&client->inner_mutex);
		if (sound != NULL) {
			if (sound->playing) {
				uv_mutex_lock(&sound->mutex);
				process_audio_file(l, client, sound, frame_size, &biggest_read, &didLoop);
				uv_mutex_unlock(&sound->mutex);
			}
		}
	}

	current = client->audio_pipes;

	// Hook allows for feeding raw PCM data into an audio buffer, mixing it into other playing audio
	lua_pushinteger(l, AUDIO_SAMPLE_RATE);
	lua_pushinteger(l, AUDIO_PLAYBACK_CHANNELS);
	lua_pushinteger(l, frame_size);
	mumble_hook_call(client, "OnAudioStream", 3);

	// Keep track of when an audio buffer is outputting data
	static bool stream_active = false;
	bool streamed_audio = false;

	while (current != NULL) {
		ByteBuffer *buffer = current->data;
		current = current->next;

		AudioContext* context = buffer->context;

		// No context is bad..
		if (!context) continue;

		// Prepare for read
		buffer_flip(buffer);

		// How many bytes of data we are streaming in
		size_t length = buffer_length(buffer);

		// No more audio to output
		if (length <= 0) {
			int error = src_reset(context->src_state);
			if (error != 0) {
				mumble_log(LOG_WARN, "error resetting audio buffer resampler state: %s", src_strerror(error));
			}
			continue;
		}

		float* input_buffer = malloc(sizeof(float) * client->audio_frames * context->samplerate / 1000 * context->channels);

		if (!input_buffer) {
			continue;
		}

		streamed_audio = true;
		sf_count_t input_frame_size = (sf_count_t)(client->audio_frames * (float)context->samplerate / 1000.0);
		size_t input_frames_actual = length / sizeof(float) / context->channels;
		sf_count_t input_frames = input_frames_actual > input_frame_size ? input_frame_size : input_frames_actual;
		size_t missing_frames = (didLoop || biggest_read > 0) ? 0 : (input_frame_size - input_frames);
		double resample_ratio = (double)AUDIO_SAMPLE_RATE / context->samplerate;

		// Handle the audio output for streamed frames
		for (int i = 0; i < input_frames + missing_frames; i++) {
			if (i < missing_frames) {
				// Insert silence for each channel
				for (int j = 0; j < context->channels; j++) {
					input_buffer[i * context->channels + j] = 0.0;
				}
			} else {
				for (int j = 0; j < context->channels; j++) {
					float sample;
					buffer_readFloat(buffer, &sample);
					input_buffer[i * context->channels + j] = sample;
				}
			}
		}

		if (context->channels == 1) {
			float *stereo_buffer = convert_mono_to_multi(input_buffer, input_frames, 2);
			if (!stereo_buffer) {
				free(input_buffer);
				continue;
			}
			free(input_buffer);
			input_buffer = stereo_buffer;
		} else if (context->channels > 2) {
			float *stereo_buffer = downmix_to_stereo(input_buffer, input_frames, context->channels);
			if (!stereo_buffer) {
				free(input_buffer);
				continue;
			}
			free(input_buffer);
			input_buffer = stereo_buffer;
		}

		float *output_audio = NULL;
		int resampled_frames = resample_audio(context->src_state, input_buffer, &output_audio, input_frames + missing_frames, frame_size, resample_ratio, true);
		free(input_buffer);
		if (resampled_frames > 0) {
			for (int i = 0; i < resampled_frames; i++) {
				client->audio_output[i].l += output_audio[i * 2];
				client->audio_output[i].r += output_audio[i * 2 + 1];
			}
			free(output_audio);
		}

		// Move any remaining audio data to the front
		buffer_pack(buffer);

		// Update biggest_read if necessary
		if (input_frames > biggest_read) {
			biggest_read = input_frames + missing_frames;
		}
	}

	// All streams output nothing
	bool stream_ended = stream_active && !streamed_audio;

	// Something isn't looping, and either we stopped reading data or a buffer stream stopped sening data.
	bool end_frame = !didLoop && (biggest_read < frame_size && stream_ended);

	if (biggest_read > 0 || end_frame) {
		// Encode and transmit until the end
		encode_and_send_audio(client, frame_size, end_frame);
	}

	stream_active = streamed_audio;

	lua_stackguard_exit(l);
}

static int audiostream_isPlaying(lua_State *l) {
	AudioStream *sound = luaL_checkudata(l, 1, METATABLE_AUDIOSTREAM);
	lua_pushboolean(l, sound->playing);
	return 1;
}

static int audiostream_setVolume(lua_State *l) {
	AudioStream *sound = luaL_checkudata(l, 1, METATABLE_AUDIOSTREAM);
	sound->volume = luaL_checknumber(l, 2);
	lua_pushvalue(l, 1);
	return 1;
}

static int audiostream_getVolume(lua_State *l) {
	AudioStream *sound = luaL_checkudata(l, 1, METATABLE_AUDIOSTREAM);
	lua_pushnumber(l, sound->volume);
	return 1;
}

static int audiostream_pause(lua_State *l) {
	AudioStream *sound = luaL_checkudata(l, 1, METATABLE_AUDIOSTREAM);
	sound->playing = false;
	return 0;
}

static int audiostream_play(lua_State *l) {
	AudioStream *sound = luaL_checkudata(l, 1, METATABLE_AUDIOSTREAM);
	if (!sound->playing) {
		sound->playing = true;

		if (sound->refrence <= LUA_NOREF) {
			// Push a copy of the audio stream and save a reference
			lua_pushvalue(l, 1);
			audio_transmission_reference(l, sound);
		}
	} else {
		sf_seek(sound->file, 0, SEEK_SET);
	}
	return 0;
}

static int audiostream_stop(lua_State *l) {
	AudioStream *sound = luaL_checkudata(l, 1, METATABLE_AUDIOSTREAM);
	if (sound->playing) {
		audio_transmission_unreference(l, sound);
	}
	return 0;
}

static int audiostream_seek(lua_State *l) {
	AudioStream *sound = luaL_checkudata(l, 1, METATABLE_AUDIOSTREAM);

	enum what {SET, CUR, END};
	static const char * op[] = {"set", "cur", "end", NULL};

	int option = luaL_checkoption(l, 2, "cur", op);
	long offset = luaL_optlong(l, 3, 0);

	sf_count_t position = 0;

	switch (option) {
	case SET:
		position = sf_seek(sound->file, offset, SEEK_SET);
		break;
	case CUR:
		position = sf_seek(sound->file, offset, SEEK_CUR);
		break;
	case END:
		position = sf_seek(sound->file, offset, SEEK_END);
		break;
	}

	lua_pushinteger(l, position);
	return 1;
}

static int audiostream_getLength(lua_State *l) {
	AudioStream *sound = luaL_checkudata(l, 1, METATABLE_AUDIOSTREAM);

	enum what {SAMPLES, FRAMES, SECONDS};
	static const char * op[] = {"samples", "frames", "seconds", NULL};

	switch (luaL_checkoption(l, 2, NULL, op)) {
	case SAMPLES: {
		lua_pushinteger(l, sound->info.frames * sound->info.channels);
		return 1;
	}
	case FRAMES: {
		lua_pushinteger(l, sound->info.frames);
		return 1;
	}
	case SECONDS: {
		lua_pushnumber(l, (double) sound->info.frames / sound->info.samplerate);
		return 1;
	}
	}

	return 0;
}

static int audiostream_getInfo(lua_State *l) {
	AudioStream *sound = luaL_checkudata(l, 1, METATABLE_AUDIOSTREAM);

	lua_newtable(l);
	{
		lua_pushinteger(l, sound->info.frames);
		lua_setfield(l, -2, "frames");
		lua_pushinteger(l, sound->info.samplerate);
		lua_setfield(l, -2, "samplerate");
		lua_pushinteger(l, sound->info.channels);
		lua_setfield(l, -2, "channels");
		lua_pushinteger(l, sound->info.format);
		lua_setfield(l, -2, "format");
		lua_pushinteger(l, sound->info.sections);
		lua_setfield(l, -2, "sections");
		lua_pushinteger(l, sound->info.seekable);
		lua_setfield(l, -2, "seekable");
	}
	return 1;
}

static int audiostream_getTitle(lua_State *l) {
	AudioStream *sound = luaL_checkudata(l, 1, METATABLE_AUDIOSTREAM);
	lua_pushstring(l, sf_get_string(sound->file, SF_STR_TITLE));
	return 1;
}

static int audiostream_getArtist(lua_State *l) {
	AudioStream *sound = luaL_checkudata(l, 1, METATABLE_AUDIOSTREAM);
	lua_pushstring(l, sf_get_string(sound->file, SF_STR_ARTIST));
	return 1;
}

static int audiostream_getCopyright(lua_State *l) {
	AudioStream *sound = luaL_checkudata(l, 1, METATABLE_AUDIOSTREAM);
	lua_pushstring(l, sf_get_string(sound->file, SF_STR_COPYRIGHT));
	return 1;
}

static int audiostream_getSoftware(lua_State *l) {
	AudioStream *sound = luaL_checkudata(l, 1, METATABLE_AUDIOSTREAM);
	lua_pushstring(l, sf_get_string(sound->file, SF_STR_SOFTWARE));
	return 1;
}

static int audiostream_getComments(lua_State *l) {
	AudioStream *sound = luaL_checkudata(l, 1, METATABLE_AUDIOSTREAM);
	lua_pushstring(l, sf_get_string(sound->file, SF_STR_COMMENT));
	return 1;
}

static int audiostream_setLooping(lua_State *l) {
	AudioStream *sound = luaL_checkudata(l, 1, METATABLE_AUDIOSTREAM);

	const char *msg = NULL;

	switch (lua_type(l, 2)) {
	case LUA_TNUMBER:
		sound->looping = false;
		sound->loop_count = luaL_checkinteger(l, 2);
		break;
	case LUA_TBOOLEAN:
		sound->looping = luaL_checkboolean(l, 2);
		sound->loop_count = 0;
		break;
	default:
		msg = lua_pushfstring(l, "%s or %s expected, got %s",
		                      lua_typename(l, LUA_TNUMBER), lua_typename(l, LUA_TBOOLEAN), luaL_typename(l, 2));
		return luaL_argerror(l, 2, msg);
	}

	lua_pushvalue(l, 1);
	return 1;
}

static int audiostream_isLooping(lua_State *l) {
	AudioStream *sound = luaL_checkudata(l, 1, METATABLE_AUDIOSTREAM);
	lua_pushboolean(l, sound->looping || sound->loop_count > 0);
	return 1;
}

static int audiostream_getLoopCount(lua_State *l) {
	AudioStream *sound = luaL_checkudata(l, 1, METATABLE_AUDIOSTREAM);
	if (sound->looping) {
		// push math.huge (inf)
		lua_pushnumber(l, HUGE_VAL);
	} else {
		lua_pushinteger(l, sound->loop_count);
	}
	return 1;
}

static int audiostream_fadeTo(lua_State *l) {
	AudioStream *sound = luaL_checkudata(l, 1, METATABLE_AUDIOSTREAM);
	float volume = luaL_checknumber(l, 2);
	float time = luaL_optnumber(l, 3, 1);
	sound->fade_frames = AUDIO_SAMPLE_RATE * time;
	sound->fade_frames_left = sound->fade_frames;
	sound->fade_from_volume = sound->fade_volume;
	sound->fade_to_volume = volume;
	return 0;
}

static int audiostream_fadeOut(lua_State *l) {
	AudioStream *sound = luaL_checkudata(l, 1, METATABLE_AUDIOSTREAM);
	float time = luaL_optnumber(l, 2, 1);
	sound->fade_frames = AUDIO_SAMPLE_RATE * time;
	sound->fade_frames_left = sound->fade_frames;
	sound->fade_from_volume = sound->fade_volume;
	sound->fade_to_volume = 0;
	sound->fade_stop = true;
	return 0;
}

static int audiostream_gc(lua_State *l) {
	AudioStream *sound = luaL_checkudata(l, 1, METATABLE_AUDIOSTREAM);
	mumble_log(LOG_DEBUG, "%s: %p garbage collected", METATABLE_AUDIOSTREAM, sound);
	if (sound->file) {
		sf_close(sound->file);
		sound->file = NULL;
	}
	if (sound->buffer) {
		free(sound->buffer);
	}
	if (sound->src_state) {
		src_delete(sound->src_state);
	}
	uv_mutex_destroy(&sound->mutex);
	return 0;
}

static int audiostream_tostring(lua_State *l) {
	lua_pushfstring(l, "%s: %p", METATABLE_AUDIOSTREAM, lua_topointer(l, 1));
	return 1;
}

const luaL_Reg mumble_audiostream[] = {
	{"isPlaying", audiostream_isPlaying},
	{"setVolume", audiostream_setVolume},
	{"getVolume", audiostream_getVolume},
	{"pause", audiostream_pause},
	{"play", audiostream_play},
	{"stop", audiostream_stop},
	{"seek", audiostream_seek},
	{"getLength", audiostream_getLength},
	{"getDuration", audiostream_getLength},
	{"getInfo", audiostream_getInfo},
	{"getTitle", audiostream_getTitle},
	{"getArtist", audiostream_getArtist},
	{"getCopyright", audiostream_getCopyright},
	{"getSoftware", audiostream_getSoftware},
	{"getComments", audiostream_getComments},
	{"setLooping", audiostream_setLooping},
	{"isLooping", audiostream_isLooping},
	{"getLoopCount", audiostream_getLoopCount},
	{"fadeTo", audiostream_fadeTo},
	{"fadeOut", audiostream_fadeOut},
	{"__gc", audiostream_gc},
	{"__tostring", audiostream_tostring},
	{NULL, NULL}
};
