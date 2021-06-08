#include "mumble.h"
#include "packet.h"
#include "audio.h"

int util_set_varint(uint8_t buffer[], const uint64_t value)
{
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
		buffer[1] = (value >> 24) | 0xFF;
		buffer[2] = (value >> 16) & 0xFF;
		buffer[3] = (value >> 8) & 0xFF;
		buffer[4] = value & 0xFF;
		return 5;
	}
	return -1;
}

int64_t util_get_varint(uint8_t buffer[], int *len)
{
	uint8_t v = buffer[0];
	int64_t i = 0;

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
		i=(v & 0x0F) << 24 | buffer[1] << 16 | buffer[2] << 8 | buffer[3];
		*len += 4;
	} else if ((v & 0xE0) == 0xC0) {
		i=(v & 0x1F) << 16 | buffer[1] << 8 | buffer[2];
		*len += 3;
	} else {
		*len += 1;
	}

	return i;
}

VoicePacket * voicepacket_init(VoicePacket *packet, uint8_t *buffer)
{
	if (packet == NULL || buffer == NULL) {
		return NULL;
	}
	packet->buffer = buffer;
	packet->length = 0;
	packet->header_length = 0;
	return packet;
}

int voicepacket_setheader(VoicePacket *packet, const uint8_t type, const uint8_t target, const uint32_t sequence)
{
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

int voicepacket_setframe(VoicePacket *packet, const uint8_t type, const uint16_t frame_header, uint8_t *buffer, size_t buffer_len)
{
	int offset;

	if (packet == NULL || buffer == NULL || buffer_len <= 0 || buffer_len >= 0x2000) {
		return -1;
	}
	if (packet->header_length <= 0) {
		return -2;
	}

	if (type == UDP_OPUS) {
		// Opus uses a varint for the frame header
		offset = util_set_varint(packet->buffer + packet->header_length, frame_header);
	} else if (type == UDP_SPEEX || type == UDP_CELT_ALPHA || type == UDP_CELT_BETA) {
		// Every other codec uses a single byte as the frame header
		offset += 1;
		packet->buffer[packet->header_length] = frame_header;
	}
	
	if (offset <= 0) {
		return -3;
	}
	memmove(packet->buffer + packet->header_length + offset, buffer, buffer_len);
	packet->length = packet->header_length + buffer_len + offset;
	return 1;
}

int voicepacket_getlength(const VoicePacket *packet)
{
	if (packet == NULL) {
		return -1;
	}
	return packet->length;
}

void audio_transmission_stop(lua_State*l, MumbleClient *client, int stream)
{
	int index = stream - 1;
	AudioStream* sound = client->audio_jobs[index];

	if (sound == NULL) return;

	lua_rawgeti(l, LUA_REGISTRYINDEX, client->audio_streams);
		lua_pushinteger(l, sound->stream); // index streams table by stream id
		lua_gettable(l, -2); // push the audio stream object

		mumble_hook_call(l, client, "OnAudioStreamEnd", 1);
	
		lua_pushinteger(l, sound->stream);
		lua_pushnil(l); // Set the stream index to nil
		lua_settable(l, -3);
	lua_pop(l, 1);

	client->audio_jobs[index] = NULL;
}

void audio_transmission_event(lua_State* l, MumbleClient *client)
{
	lua_stackguard_entry(l);

	VoicePacket packet;

	long read;
	long biggest_read = 0;

	// How big each frame of audio data should be
	const uint32_t frame_size = client->audio_frames * AUDIO_SAMPLE_RATE / 1000;

	memset(client->audio_buffer, 0, sizeof(client->audio_buffer));

	bool didLoop = false;

	// Loop through all available audio channels
	for (int i = 0; i < AUDIO_MAX_STREAMS; i++) {
		AudioStream *sound = client->audio_jobs[i];

		// No sound playing = skip
		if (sound == NULL || !sound->playing) continue;

		// How many channels the audio file has
		const int channels = sound->info.channels;
		const uint32_t source_rate = sound->info.sample_rate;

		uint32_t sample_size = client->audio_frames * source_rate / 1000;

		memset(sound->buffer, 0, sizeof(sound->buffer));

		read = stb_vorbis_get_samples_float_interleaved(sound->ogg, AUDIO_PLAYBACK_CHANNELS, (float*) sound->buffer, sample_size * AUDIO_PLAYBACK_CHANNELS);

		if (channels == 1 && read > 0) {
			//mix mono to stereo
			for (int i = 0; i < read; i++) {
				sound->buffer[i].r = sound->buffer[i].l;
			}
		}

		// Very very awful resampling, but at least it's something..
		if (source_rate != AUDIO_SAMPLE_RATE) {
			// Clear the rebuffer so we can use it again
			memset(client->audio_rebuffer, 0, sizeof(client->audio_rebuffer));

			// Resample the audio
			float scale = (float) read / (float) sample_size;

			// Adjust these values
			sample_size = sample_size * (float) AUDIO_SAMPLE_RATE / (float) source_rate;
			read = ceil(sample_size * scale);

			for (int t=0; t < read; t++) {
				// Resample the audio to fit within the requested sample_rate
				int idx = (int) floor((float) t / AUDIO_SAMPLE_RATE * source_rate) * 2;
				client->audio_rebuffer[t*2].l = sound->buffer[idx].l * 2;
				client->audio_rebuffer[t*2].r = sound->buffer[idx].r * 2;
			}

			// Copy resampled audio back into the main buffer
			memcpy(sound->buffer, client->audio_rebuffer, sizeof(client->audio_rebuffer));
		}

		for (int i = 0; i < read; i++) {
			// Mix all streams together in the output buffer
			client->audio_buffer[i].l += sound->buffer[i].l * sound->volume * client->volume;
			client->audio_buffer[i].r += sound->buffer[i].r * sound->volume * client->volume;
		}

		// If the number of samples we read from the OGG file are less than the request sample size, it must be the last bit of audio
		if (read < sample_size) {
			if (sound->looping) {
				didLoop = true;
				stb_vorbis_seek_start(sound->ogg);
			} else if (sound->loop_count > 0) {
				didLoop = true;
				sound->loop_count--;
				stb_vorbis_seek_start(sound->ogg);
			} else {
				audio_transmission_stop(l, client, i + 1);
			}
		}

		if (read > biggest_read) {
			// We need to save the biggest PCM length for later.
			// If we didn't do this, we could be cutting off some audio if one
			// stream ends while another is still playing.
			biggest_read = read;
		}
	}

	// Nothing to do..
	if (biggest_read <= 0) return;

	uint8_t encoded[0x1FFF];
	opus_int32 encoded_len = opus_encode_float(client->encoder, (float*) client->audio_buffer, frame_size, encoded, sizeof(encoded));

	if (encoded_len <= 0) return;

	uint32_t frame_header = encoded_len;
	// If the largest PCM buffer is smaller than our frame size, it has to be the last frame available
	if (!didLoop && biggest_read < frame_size) {
		// Set 14th bit to 1 to signal end of stream.
		frame_header = ((1 << 13) | frame_header);
	}

	uint8_t packet_buffer[PAYLOAD_SIZE_MAX];
	voicepacket_init(&packet, packet_buffer);
	voicepacket_setheader(&packet, UDP_OPUS, client->audio_target, client->audio_sequence);
	voicepacket_setframe(&packet, UDP_OPUS, frame_header, encoded, encoded_len);

	mumble_handle_speaking_hooks(l, client, packet.buffer + 1, UDP_OPUS, client->audio_target, client->session);

	packet_sendex(client, PACKET_UDPTUNNEL, packet_buffer, voicepacket_getlength(&packet));

	client->audio_sequence = (client->audio_sequence + 1) % 100000;

	lua_stackguard_exit(l);
}

static int audiostream_isPlaying(lua_State *l)
{
	AudioStream *sound = luaL_checkudata(l, 1, METATABLE_AUDIOSTREAM);
	lua_pushboolean(l, sound->playing);
	return 1;
}

static int audiostream_setVolume(lua_State *l)
{
	AudioStream *sound = luaL_checkudata(l, 1, METATABLE_AUDIOSTREAM);
	sound->volume = luaL_checknumber(l, 2);
	return 0;
}

static int audiostream_getVolume(lua_State *l)
{
	AudioStream *sound = luaL_checkudata(l, 1, METATABLE_AUDIOSTREAM);
	lua_pushnumber(l, sound->volume);
	return 1;
}

static int audiostream_getStream(lua_State *l)
{
	AudioStream *sound = luaL_checkudata(l, 1, METATABLE_AUDIOSTREAM);
	lua_pushnumber(l, sound->stream);
	return 1;
}

static int audiostream_pause(lua_State *l)
{
	AudioStream *sound = luaL_checkudata(l, 1, METATABLE_AUDIOSTREAM);
	sound->playing = false;
	return 0;
}

static int audiostream_play(lua_State *l)
{
	AudioStream *sound = luaL_checkudata(l, 1, METATABLE_AUDIOSTREAM);
	sound->playing = true;
	return 0;
}

static int audiostream_stop(lua_State *l)
{
	AudioStream *sound = luaL_checkudata(l, 1, METATABLE_AUDIOSTREAM);
	sound->playing = false;
	stb_vorbis_seek_start(sound->ogg);
	return 0;
}

static int audiostream_seek(lua_State *l)
{
	AudioStream *sound = luaL_checkudata(l, 1, METATABLE_AUDIOSTREAM);

	const char* whence = lua_tostring(l, 2);
	unsigned int offset = luaL_optinteger(l, 3, 0);

	enum what {START, CUR, END};
	static const char * op[] = {"start", "cur", "end", NULL};

	switch (luaL_checkoption(l, 1, NULL, op)) {
		case START:
			stb_vorbis_seek(sound->ogg, offset);
			break;
		case CUR:
		{
			unsigned int samples = stb_vorbis_get_sample_offset(sound->ogg);
			stb_vorbis_seek(sound->ogg, samples + offset);
			break;
		}
		case END:
		{
			unsigned int samples = stb_vorbis_stream_length_in_samples(sound->ogg);
			stb_vorbis_seek(sound->ogg, samples + offset);
			break;
		}
	}

	lua_pushinteger(l, stb_vorbis_get_sample_offset(sound->ogg));
	return 1;
}

static int audiostream_getLength(lua_State *l)
{
	AudioStream *sound = luaL_checkudata(l, 1, METATABLE_AUDIOSTREAM);

	enum what {SAMPLES, SECONDS};
	static const char * op[] = {"samples", "seconds", NULL};

	switch (luaL_checkoption(l, 2, NULL, op)) {
		case SAMPLES:
		{
			unsigned int samples = stb_vorbis_stream_length_in_samples(sound->ogg);
			lua_pushinteger(l, samples);
			return 1;
		}
		case SECONDS:
		{
			float seconds = stb_vorbis_stream_length_in_seconds(sound->ogg);
			lua_pushnumber(l, seconds);
			return 1;
		}
	}

	return 0;
}

static int audiostream_getInfo(lua_State *l)
{
	AudioStream *sound = luaL_checkudata(l, 1, METATABLE_AUDIOSTREAM);

	stb_vorbis_info info = stb_vorbis_get_info(sound->ogg);

	lua_newtable(l);
	{
		lua_pushinteger(l, info.channels);
		lua_setfield(l, -2, "channels");
		lua_pushinteger(l, info.sample_rate);
		lua_setfield(l, -2, "sample_rate");
		lua_pushinteger(l, info.setup_memory_required);
		lua_setfield(l, -2, "setup_memory_required");
		lua_pushinteger(l, info.setup_temp_memory_required);
		lua_setfield(l, -2, "setup_temp_memory_required");
		lua_pushinteger(l, info.temp_memory_required);
		lua_setfield(l, -2, "temp_memory_required");
		lua_pushinteger(l, info.max_frame_size);
		lua_setfield(l, -2, "max_frame_size");
	}
	return 1;
}

static int audiostream_getComment(lua_State *l)
{
	AudioStream *sound = luaL_checkudata(l, 1, METATABLE_AUDIOSTREAM);

	stb_vorbis_comment cmnt = stb_vorbis_get_comment(sound->ogg);

	lua_newtable(l);
	{
		lua_pushstring(l, cmnt.vendor);
		lua_setfield(l, -2, "vendor");
		lua_newtable(l);
		{
			for (int i=0; i < cmnt.comment_list_length; i++) {
				lua_pushinteger(l, i+1);
				lua_pushstring(l, cmnt.comment_list[i]);
				lua_settable(l, -3);
			}
		}
		lua_setfield(l, -2, "comment_list");
	}
	return 1;
}

static int audiostream_setLooping(lua_State *l)
{
	AudioStream *sound = luaL_checkudata(l, 1, METATABLE_AUDIOSTREAM);
	
	switch (lua_type(l, 2)) {
		case LUA_TNUMBER:
			sound->looping = false;
			sound->loop_count = luaL_checkinteger(l, 2);
			break;
		case LUA_TBOOLEAN:
			sound->looping = luaL_checkboolean(l, 2);
			sound->loop_count = 0;
			break;
	}

	return 0;
}

static int audiostream_isLooping(lua_State *l)
{
	AudioStream *sound = luaL_checkudata(l, 1, METATABLE_AUDIOSTREAM);
	lua_pushboolean(l, sound->looping || sound->loop_count > 0);
	return 1;
}

static int audiostream_getLoopCount(lua_State *l)
{
	AudioStream *sound = luaL_checkudata(l, 1, METATABLE_AUDIOSTREAM);
	lua_pushinteger(l, sound->loop_count);
	return 1;
}

static int audiostream_close(lua_State *l)
{
	AudioStream *sound = luaL_checkudata(l, 1, METATABLE_AUDIOSTREAM);
	audio_transmission_stop(l, sound->client, sound->stream);
	return 0;
}

static int audiostream_gc(lua_State *l)
{
	AudioStream *sound = luaL_checkudata(l, 1, METATABLE_AUDIOSTREAM);
	stb_vorbis_close(sound->ogg);
	return 0;
}

static int audiostream_tostring(lua_State *l)
{
	AudioStream *sound = luaL_checkudata(l, 1, METATABLE_AUDIOSTREAM);
	lua_pushfstring(l, "%s: %p", METATABLE_AUDIOSTREAM, sound);
	return 1;
}

const luaL_Reg mumble_audiostream[] = {
	{"isPlaying", audiostream_isPlaying},
	{"setVolume", audiostream_setVolume},
	{"getVolume", audiostream_getVolume},
	{"getStream", audiostream_getStream},
	{"pause", audiostream_pause},
	{"play", audiostream_play},
	{"stop", audiostream_stop},
	{"seek", audiostream_seek},
	{"getLength", audiostream_getLength},
	{"getDuration", audiostream_getLength},
	{"getInfo", audiostream_getInfo},
	{"setLooping", audiostream_setLooping},
	{"isLooping", audiostream_isLooping},
	{"getLoopCount", audiostream_getLoopCount},
	{"close", audiostream_close},
	{"__gc", audiostream_gc},
	{"__tostring", audiostream_tostring},
	{NULL, NULL}
};