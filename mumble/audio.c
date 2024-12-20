#include "mumble.h"
#include "packet.h"
#include "audio.h"

uint8_t util_set_varint(uint8_t buffer[], const uint64_t value)
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

uint64_t util_get_varint(uint8_t buffer[], int *len)
{
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
		i=(v & 0x0F) << 24 | buffer[1] << 16 | buffer[2] << 8 | buffer[3];
		*len += 4;
	} else if ((v & 0xE0) == 0xC0) {
		i=(v & 0x1F) << 16 | buffer[1] << 8 | buffer[2];
		*len += 3;
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

	if (type == LEGACY_UDP_OPUS) {
		// Opus uses a varint for the frame header
		offset = util_set_varint(packet->buffer + packet->header_length, frame_header);
	} else if (type == LEGACY_UDP_SPEEX || type == LEGACY_UDP_CELT_ALPHA || type == LEGACY_UDP_CELT_BETA) {
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

void audio_transmission_reference(lua_State *l, AudioStream *sound) {
	sound->refrence = mumble_registry_ref(l, sound->client->audio_streams);
	list_add(&sound->client->stream_list, sound->refrence, sound);
}

void audio_transmission_unreference(lua_State*l, AudioStream *sound)
{
	mumble_registry_unref(l, sound->client->audio_streams, sound->refrence);
	list_remove(&sound->client->stream_list, sound->refrence);
}

void mumble_audio_timer(EV_P_ ev_timer *w_, int revents)
{
	my_timer *w = (my_timer *) w_;
	MumbleClient *client = w->client;

	if (client->connected) {
		audio_transmission_event(w->l, client);
	}
}

void handle_audio_stream_end(lua_State *l, MumbleClient *client, AudioStream *sound, bool *didLoop) {
	if (sound->looping) {
		*didLoop = true;
		stb_vorbis_seek_start(sound->ogg);
	} else if (sound->loop_count > 0) {
		*didLoop = true;
		sound->loop_count--;
		stb_vorbis_seek_start(sound->ogg);
	} else {
		sound->playing = false;
		mumble_registry_pushref(l, client->audio_streams, sound->refrence);
		mumble_hook_call(l, client, "OnAudioStreamEnd", 1);
		audio_transmission_unreference(l, sound);
	}
}

void process_audio_stream(lua_State *l, MumbleClient *client, AudioStream *sound, uint32_t frame_size, long *biggest_read, bool *didLoop) {
	if (sound == NULL || !sound->playing) return;

	const int channels = sound->info.channels;
	const uint32_t source_rate = sound->info.sample_rate;
	uint32_t sample_size = client->audio_frames * source_rate / 1000;

	if (sample_size > PCM_BUFFER) {
		return;
	}

	memset(sound->buffer, 0, sizeof(sound->buffer));

	long read = stb_vorbis_get_samples_float_interleaved(
		sound->ogg, AUDIO_PLAYBACK_CHANNELS, (float *)sound->buffer, sample_size * AUDIO_PLAYBACK_CHANNELS
	);

	if (channels == 1 && read > 0) {
		// The audio is mono, so make both channels the same
		for (int i = 0; i < read; i++) {
			sound->buffer[i].r = sound->buffer[i].l;
		}
	}

	if (source_rate != AUDIO_SAMPLE_RATE) {
		// Resample using linear interpolation
		float resample_ratio = (float)AUDIO_SAMPLE_RATE / source_rate;
		sample_size = (uint32_t)(sample_size * resample_ratio);
		read = (long)(read * resample_ratio);

		for (int t = 0; t < read; t++) {
			float source_idx = (float)t * source_rate / AUDIO_SAMPLE_RATE;
			int idx1 = (int)source_idx;
			int idx2 = (idx1 + 1 < sample_size) ? (idx1 + 1) : idx1;

			float alpha = source_idx - idx1;
			client->audio_rebuffer[t * 2].l = sound->buffer[idx1 * 2].l * (1.0f - alpha) + sound->buffer[idx2 * 2].l * alpha;
			client->audio_rebuffer[t * 2].r = sound->buffer[idx1 * 2].r * (1.0f - alpha) + sound->buffer[idx2 * 2].r * alpha;
		}

		memcpy(sound->buffer, client->audio_rebuffer, sizeof(client->audio_rebuffer));
	}

	for (int i = 0; i < read; i++) {
		client->audio_buffer[i].l += sound->buffer[i].l * sound->volume * client->volume;
		client->audio_buffer[i].r += sound->buffer[i].r * sound->volume * client->volume;
	}

	if (read < sample_size) {
		// We reached the end of the stream
		handle_audio_stream_end(l, client, sound, didLoop);
	}

	if (read > *biggest_read) {
		*biggest_read = read;
	}
}

void send_legacy_audio(lua_State *l, MumbleClient *client, uint8_t *encoded, opus_int32 encoded_len, bool end_frame) {
	uint32_t frame_header = encoded_len;
	if (end_frame) {
		frame_header |= (1 << 13);
	}

	VoicePacket packet;
	uint8_t packet_buffer[UDP_BUFFER_MAX];
	voicepacket_init(&packet, packet_buffer);
	voicepacket_setheader(&packet, LEGACY_UDP_OPUS, client->audio_target, client->audio_sequence);
	voicepacket_setframe(&packet, LEGACY_UDP_OPUS, frame_header, encoded, encoded_len);

	mumble_handle_speaking_hooks_legacy(l, client, packet_buffer + 1, LEGACY_UDP_OPUS, client->audio_target, client->session);

	int len = voicepacket_getlength(&packet);
	if (client->tcp_udp_tunnel) {
		mumble_log(LOG_TRACE, "[TCP] Sending legacy audio packet (size=%u, id=%u, target=%u, session=%u, sequence=%u)\n",
			len, LEGACY_UDP_OPUS, client->audio_target, client->session, client->audio_sequence);
		packet_sendex(client, PACKET_UDPTUNNEL, packet_buffer, NULL, len);
	} else {
		mumble_log(LOG_TRACE, "[UDP] Sending legacy audio packet (size=%u, id=%u, target=%u, session=%u, sequence=%u)\n",
			len, LEGACY_UDP_OPUS, client->audio_target, client->session, client->audio_sequence);
		packet_sendudp(client, packet_buffer, len);
	}
}

void send_protobuf_audio(lua_State *l, MumbleClient *client, uint8_t *encoded, opus_int32 encoded_len, bool end_frame) {
	MumbleUDP__Audio audio = MUMBLE_UDP__AUDIO__INIT;
	ProtobufCBinaryData audio_data = { .data = encoded, .len = (size_t)encoded_len };

	audio.frame_number = client->audio_sequence;
	audio.opus_data = audio_data;
	audio.is_terminator = end_frame;
	audio.target = client->audio_target;
	audio.n_positional_data = 0;

	uint8_t packet_buffer[UDP_BUFFER_MAX];
	packet_buffer[0] = PROTO_UDP_AUDIO;

	mumble_handle_speaking_hooks_protobuf(l, client, &audio, client->session);
	int len = 1 + mumble_udp__audio__pack(&audio, packet_buffer + 1);

	if (client->tcp_udp_tunnel) {
		mumble_log(LOG_TRACE, "[TCP] Sending protobuf TCP audio packet (size=%u, id=%u, target=%u, session=%u, sequence=%u)\n",
			len, LEGACY_UDP_OPUS, client->audio_target, client->session, client->audio_sequence);
		packet_sendex(client, PACKET_UDPTUNNEL, packet_buffer, NULL, len);
	} else {
		mumble_log(LOG_TRACE, "[UDP] Sending protobuf UDP audio packet (size=%u, id=%u, target=%u, session=%u, sequence=%u)\n",
			len, LEGACY_UDP_OPUS, client->audio_target, client->session, client->audio_sequence);
		packet_sendudp(client, packet_buffer, len);
	}
}

void encode_and_send_audio(lua_State *l, MumbleClient *client, uint32_t frame_size, bool end_frame) {
	uint8_t encoded[PAYLOAD_SIZE_MAX];
	opus_int32 encoded_len = opus_encode_float(client->encoder, (float *)client->audio_buffer, frame_size, encoded, PAYLOAD_SIZE_MAX);

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

	long biggest_read = 0;
	const uint32_t frame_size = client->audio_frames * AUDIO_SAMPLE_RATE / 1000;
	memset(client->audio_buffer, 0, sizeof(client->audio_buffer));

	bool didLoop = false;
	LinkNode *current = client->stream_list;

	while (current != NULL) {
		AudioStream *sound = current->data;
		current = current->next;
		process_audio_stream(l, client, sound, frame_size, &biggest_read, &didLoop);
	}

	if (biggest_read > 0) {
		bool end_frame = !didLoop && biggest_read < frame_size;
		encode_and_send_audio(l, client, frame_size, end_frame);
	} else {
		client->audio_sequence = 0;
	}

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
	lua_pushvalue(l, 1);
	return 1;
}

static int audiostream_getVolume(lua_State *l)
{
	AudioStream *sound = luaL_checkudata(l, 1, METATABLE_AUDIOSTREAM);
	lua_pushnumber(l, sound->volume);
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
	if (!sound->playing) {
		sound->playing = true;

		// Push a copy of the audio stream and save a reference
		lua_pushvalue(l, 1);
		sound->refrence = mumble_registry_ref(l, sound->client->audio_streams);

		// Add to our stream list
		list_add(&sound->client->stream_list, sound->refrence, sound);
	} else {
		stb_vorbis_seek_start(sound->ogg);
	}
	return 0;
}

static int audiostream_stop(lua_State *l)
{
	AudioStream *sound = luaL_checkudata(l, 1, METATABLE_AUDIOSTREAM);
	if (sound->playing) {
		sound->playing = false;
		stb_vorbis_seek_start(sound->ogg);
		audio_transmission_unreference(l, sound);
	}
	return 0;
}

static int audiostream_seek(lua_State *l)
{
	AudioStream *sound = luaL_checkudata(l, 1, METATABLE_AUDIOSTREAM);

	enum what {START, CUR, END};
	static const char * op[] = {"start", "cur", "end", NULL};

	int option = luaL_checkoption(l, 2, "cur", op);
	long offset = luaL_optlong(l, 3, 0);

	switch (option) {
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

static int audiostream_getComments(lua_State *l)
{
	AudioStream *sound = luaL_checkudata(l, 1, METATABLE_AUDIOSTREAM);

	stb_vorbis_comment cmnt = stb_vorbis_get_comment(sound->ogg);

	lua_newtable(l);
	{
		for (int i=0; i < cmnt.comment_list_length; i++) {
			lua_pushinteger(l, i+1);
			lua_pushstring(l, cmnt.comment_list[i]);
			lua_settable(l, -3);
		}
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
		default:
			const char *msg = lua_pushfstring(l, "%s or %s expected, got %s",
				lua_typename(l, LUA_TNUMBER), lua_typename(l, LUA_TBOOLEAN), luaL_typename(l, 2));
			return luaL_argerror(l, 2, msg);
	}

	lua_pushvalue(l, 1);
	return 1;
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
	if (sound->looping) {
		// push math.huge (inf)
		lua_pushnumber(l, HUGE_VAL);
	} else {
		lua_pushinteger(l, sound->loop_count);
	}
	return 1;
}

static int audiostream_gc(lua_State *l)
{
	AudioStream *sound = luaL_checkudata(l, 1, METATABLE_AUDIOSTREAM);
	stb_vorbis_close(sound->ogg);
	mumble_log(LOG_DEBUG, "%s: %p garbage collected\n", METATABLE_AUDIOSTREAM, sound);
	return 0;
}

static int audiostream_tostring(lua_State *l)
{
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
	{"getComments", audiostream_getComments},
	{"setLooping", audiostream_setLooping},
	{"isLooping", audiostream_isLooping},
	{"getLoopCount", audiostream_getLoopCount},
	{"__gc", audiostream_gc},
	{"__tostring", audiostream_tostring},
	{NULL, NULL}
};
