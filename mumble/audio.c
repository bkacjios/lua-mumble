#include "mumble.h"
#include "packet.h"
#include "audio.h"
#include "util.h"
#include "log.h"

#include <math.h>

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
			i = buffer[1] << 24 | buffer[2] << 16 | buffer[3] << 8 | buffer[4];
			*len += 5;
			break;
		case 0xF4:
			i = (uint64_t)buffer[1] << 56 | (uint64_t)buffer[2] << 48 | (uint64_t)buffer[3] << 40 | (uint64_t)buffer[4] << 32 | buffer[5] << 24 | buffer[6] << 16 | buffer[7] << 8 | buffer[8];
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
	int offset;

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
	list_add(&sound->client->stream_list, sound->refrence, sound);
}

void audio_transmission_unreference(lua_State*l, AudioStream *sound) {
	mumble_registry_unref(l, sound->client->audio_streams, sound->refrence);
	list_remove(&sound->client->stream_list, sound->refrence);
	sound->refrence = MUMBLE_UNREFERENCED;
	sound->playing = false;
	sound->fade_volume = 1.0f,
	       sound->fade_frames = 0;
	sound->fade_frames_left = 0;
	sound->fade_stop = false;
	sf_seek(sound->file, 0, SEEK_SET);
}

void mumble_audio_timer(uv_timer_t* handle) {
	MumbleClient* client = (MumbleClient*) handle->data;
	lua_State *l = client->l;

	if (client->connected) {
		audio_transmission_event(l, client);
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
		mumble_hook_call(l, client, "OnAudioStreamEnd", 1);
		audio_transmission_unreference(l, sound);
	}
}

static void convert_to_stereo_and_adjust_volume(float *input, float *output, int input_channels, int frames, float volume) {
	for (int i = 0; i < frames; i++) {
		if (input_channels == 1) {
			// Mono: Duplicate sample to both channels
			float sample = input[i] * volume;
			output[i * 2] = sample;		// Left
			output[i * 2 + 1] = sample;	// Right
		} else {
			// Stereo or multichannel: Copy first two channels
			output[i * 2] = input[i * input_channels] * volume;			// Left
			output[i * 2 + 1] = input[i * input_channels + 1] * volume;	// Right
		}
	}
}

// Threshold for soft clipping
#define SOFT_CLIP_THRESHOLD 0.9f

static float soft_clip(float sample) {
	if (sample > SOFT_CLIP_THRESHOLD) {
		return SOFT_CLIP_THRESHOLD + (sample - SOFT_CLIP_THRESHOLD) * 0.2f;
	} else if (sample < -SOFT_CLIP_THRESHOLD) {
		return -SOFT_CLIP_THRESHOLD + (sample + SOFT_CLIP_THRESHOLD) * 0.2f;
	}
	return sample;
}

static void process_audio_stream(lua_State *l, MumbleClient *client, AudioStream *sound, uint32_t frame_size, sf_count_t *biggest_read, bool *didLoop) {
	if (sound == NULL || !sound->playing) return;

	const int channels = sound->info.channels;
	const sf_count_t source_rate = sound->info.samplerate;

	sf_count_t sample_size = client->audio_frames * source_rate / 1000;

	if (sample_size > PCM_BUFFER) {
		return;
	}

	float input_buffer[PCM_BUFFER];
	float output_buffer[PCM_BUFFER];

	sf_count_t read = sf_readf_float(sound->file, input_buffer, sample_size);

	convert_to_stereo_and_adjust_volume(input_buffer, output_buffer, channels, read, sound->volume * client->volume);

	if (source_rate != AUDIO_SAMPLE_RATE) {
		// Resample using linear interpolation
		float resample_ratio = (float)AUDIO_SAMPLE_RATE / source_rate;
		int new_sample_count = (int)(read * resample_ratio); // Total number of output samples

		for (int t = 0; t < new_sample_count; t++) {
			// Calculate the position in the source buffer
			float source_idx = (float)t / resample_ratio;	// Index in source samples
			int idx1 = (int)source_idx;						// Base index
			int idx2 = idx1 + 1;							// Next index

			// Ensure indices are within bounds
			if (idx2 >= sample_size) {
				idx2 = idx1; // Avoid accessing out-of-bounds
			}

			// Interpolation factor
			float alpha = source_idx - idx1;

			float left = output_buffer[idx1 * 2] * (1.0f - alpha) + output_buffer[idx2 * 2] * alpha;
			float right = output_buffer[idx1 * 2 + 1] * (1.0f - alpha) + output_buffer[idx2 * 2 + 1] * alpha;

			// Interpolate left and right channels
			client->audio_output[t].l = soft_clip(client->audio_output[t].l + left);
			client->audio_output[t].r = soft_clip(client->audio_output[t].r + right);
		}

		// Update the number of samples processed
		read = new_sample_count;
	} else {
		// We don't need to resample, so just move it to our output buffer
		for (int i = 0; i < read; i++) {
			client->audio_output[i].l = soft_clip(client->audio_output[i].l + output_buffer[i * 2]);
			client->audio_output[i].r = soft_clip(client->audio_output[i].r + output_buffer[i * 2 + 1]);
		}
	}

	if (sound->fade_frames > 0) {
		for (int i = 0; i < read; i++) {
			if (sound->fade_frames_left > 0) {
				sound->fade_frames_left = sound->fade_frames_left - 1;
				sound->fade_volume = sound->fade_to_volume + (sound->fade_from_volume - sound->fade_to_volume) * ((float) sound->fade_frames_left / sound->fade_frames);
			} else if (sound->fade_stop) {
				// Fake end of stream
				read = 0;
				sound->fade_volume = 0.0f;
			}

			client->audio_output[i].l = soft_clip(client->audio_output[i].l * sound->fade_volume);
			client->audio_output[i].r = soft_clip(client->audio_output[i].r * sound->fade_volume);
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

static void send_legacy_audio(lua_State *l, MumbleClient *client, uint8_t *encoded, opus_int32 encoded_len, bool end_frame) {
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

	mumble_handle_speaking_hooks_legacy(l, client, packet_buffer + 1, LEGACY_UDP_OPUS, client->audio_target, client->session);
}

static void send_protobuf_audio(lua_State *l, MumbleClient *client, uint8_t *encoded, opus_int32 encoded_len, bool end_frame) {
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

	mumble_handle_speaking_hooks_protobuf(l, client, &audio, client->session);
}

static void encode_and_send_audio(lua_State *l, MumbleClient *client, sf_count_t frame_size, bool end_frame) {
	uint8_t encoded[PAYLOAD_SIZE_MAX];
	opus_int32 encoded_len = opus_encode_float(client->encoder, (float *)client->audio_output, frame_size, encoded, PAYLOAD_SIZE_MAX);

	if (encoded_len <= 0) return;

	if (client->legacy) {
		send_legacy_audio(l, client, encoded, encoded_len, end_frame);
	} else {
		send_protobuf_audio(l, client, encoded, encoded_len, end_frame);
	}

	client->audio_sequence++;
}

void audio_transmission_event(lua_State *l, MumbleClient *client) {
	lua_stackguard_entry(l);

	sf_count_t biggest_read = 0;
	const sf_count_t frame_size = client->audio_frames * AUDIO_SAMPLE_RATE / 1000;

	bool didLoop = false;
	LinkNode *current = client->stream_list;

	memset(client->audio_output, 0, sizeof(client->audio_output));

	while (current != NULL) {
		AudioStream *sound = current->data;
		process_audio_stream(l, client, sound, frame_size, &biggest_read, &didLoop);
		current = current->next;
	}

	current = client->audio_pipes;

	// Hook allows for feeding raw PCM data into an audio buffer, mixing it into other playing audio
	lua_pushinteger(l, AUDIO_SAMPLE_RATE);
	lua_pushinteger(l, AUDIO_PLAYBACK_CHANNELS);
	lua_pushinteger(l, frame_size);
	mumble_hook_call(l, client, "OnAudioStream", 3);

	// Keep track of when an audio buffer is outputting data
	static bool stream_active = false;
	bool streamed_audio = false;

	while (current != NULL) {
		ByteBuffer *buffer = current->data;
		current = current->next;

		// Prepare for read
		buffer_flip(buffer);

		size_t length = buffer_length(buffer);

		if (length <= 0) {
			continue;
		}

		streamed_audio = true;
		size_t total_frames = length / sizeof(float) / AUDIO_PLAYBACK_CHANNELS;
		size_t streamed_frames = total_frames > frame_size ? frame_size : total_frames;
		size_t missing_frames = (didLoop || biggest_read > 0) ? 0 : (frame_size - streamed_frames);

		// Handle the audio output for streamed frames
		for (int i = missing_frames; i < streamed_frames + missing_frames; i++) {
			float left;
			float right;
			buffer_readFloat(buffer, &left);
			buffer_readFloat(buffer, &right);
			client->audio_output[i].l = soft_clip(client->audio_output[i].l + left * client->volume);
			client->audio_output[i].r = soft_clip(client->audio_output[i].r + right * client->volume);
		}

		// Move any remaining audio data to the front
		buffer_pack(buffer);

		// Update biggest_read if necessary
		if (streamed_frames > biggest_read) {
			biggest_read = streamed_frames + missing_frames;
		}
	}

	// All streams output nothing
	bool stream_ended = stream_active && !streamed_audio;

	// Something isn't looping, and either we stopped reading data or a buffer stream stopped sening data.
	bool end_frame = !didLoop && (biggest_read < frame_size && stream_ended);

	if (biggest_read > 0 || end_frame) {
		// Encode and transmit until the end
		encode_and_send_audio(l, client, frame_size, end_frame);
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

		if (sound->refrence <= MUMBLE_UNREFERENCED) {
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

	sf_count_t position;

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
	sf_close(sound->file);
	mumble_log(LOG_DEBUG, "%s: %p garbage collected", METATABLE_AUDIOSTREAM, sound);
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
