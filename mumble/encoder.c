#include "mumble.h"

#include "encoder.h"
#include "util.h"

int mumble_encoder_new(lua_State *l)
{
	// Argument 1 = mumble.encoder table
	opus_int32 samplerate = luaL_optinteger(l, 2, AUDIO_SAMPLE_RATE);
	int channels = luaL_optinteger(l, 3, AUDIO_PLAYBACK_CHANNELS);

	OpusEncoder *encoder = lua_newuserdata(l, opus_encoder_get_size(channels));
	luaL_getmetatable(l, METATABLE_ENCODER);
	lua_setmetatable(l, -2);

	int error = opus_encoder_init(encoder, samplerate, channels, OPUS_APPLICATION_AUDIO);

	if (error != OPUS_OK) {
		lua_pushnil(l);
		lua_pushfstring(l, "could not initialize opus encoder: %s", opus_strerror(error));
		return 2;
	}

	opus_encoder_ctl(encoder, OPUS_SET_VBR(0));
	opus_encoder_ctl(encoder, OPUS_SET_BITRATE(AUDIO_DEFAULT_BITRATE));
	opus_encoder_ctl(encoder, OPUS_SET_SIGNAL(OPUS_SIGNAL_MUSIC));
	return 1;
}

/* GENERIC CTLS */

static int encoder_reset(lua_State *l)
{
	OpusEncoder *encoder = luaL_checkudata(l, 1, METATABLE_ENCODER);
	opus_encoder_ctl(encoder, OPUS_RESET_STATE);
	return 0;
}

static int encoder_getFinalRange(lua_State *l)
{
	OpusEncoder *encoder = luaL_checkudata(l, 1, METATABLE_ENCODER);
	opus_uint32 range;
	opus_encoder_ctl(encoder, OPUS_GET_FINAL_RANGE(&range));
	lua_pushboolean(l, !range);
	return 1;
}

static int encoder_getBandwidth(lua_State *l)
{
	OpusEncoder *encoder = luaL_checkudata(l, 1, METATABLE_ENCODER);
	opus_int32 width;
	opus_encoder_ctl(encoder, OPUS_GET_BANDWIDTH(&width));
	lua_pushinteger(l, width);
	return 1;
}

static int encoder_getSampleRate(lua_State *l)
{
	OpusEncoder *encoder = luaL_checkudata(l, 1, METATABLE_ENCODER);
	opus_int32 rate;
	opus_encoder_ctl(encoder, OPUS_GET_SAMPLE_RATE(&rate));
	lua_pushinteger(l, rate);
	return 1;
}

/* ENCODER CTLS */

static int encoder_setBitRate(lua_State *l)
{
	OpusEncoder *encoder = luaL_checkudata(l, 1, METATABLE_ENCODER);
	opus_encoder_ctl(encoder, OPUS_SET_BITRATE(luaL_checkinteger(l, 2)));
	return 0;
}

static int encoder_getBitRate(lua_State *l)
{
	OpusEncoder *encoder = luaL_checkudata(l, 1, METATABLE_ENCODER);
	opus_int32 rate;
	opus_encoder_ctl(encoder, OPUS_GET_BITRATE(&rate));
	lua_pushinteger(l, rate);
	return 1;
}

static int encoder_encode(lua_State *l)
{
	OpusEncoder *encoder = luaL_checkudata(l, 1, METATABLE_ENCODER);
	int frames = luaL_checkinteger(l, 2);

	size_t len;
	const opus_int16* pcm = (opus_int16*) luaL_checklstring(l, 3, &len);

	const uint32_t enc_frame_size = frames * AUDIO_SAMPLE_RATE / 100;

	uint8_t output[0x1FFF];
	opus_int32 outlen = opus_encode(encoder, pcm, enc_frame_size, output, len);

	lua_pushlstring(l, output, outlen);
	return 1;
}

static int encoder_encode_float(lua_State *l)
{
	OpusEncoder *encoder = luaL_checkudata(l, 1, METATABLE_ENCODER);
	int frames = luaL_checkinteger(l, 2);

	size_t len;
	const float* pcm = (float*) luaL_checklstring(l, 3, &len);

	const uint32_t enc_frame_size = frames * AUDIO_SAMPLE_RATE / 100;

	uint8_t output[0x1FFF];
	opus_int32 outlen = opus_encode_float(encoder, pcm, enc_frame_size, output, len);

	lua_pushlstring(l, output, outlen);
	return 1;
}

static int encoder_tostring(lua_State *l)
{
	lua_pushfstring(l, "%s: %p", METATABLE_ENCODER, lua_topointer(l, 1));
	return 1;
}

static int encoder_gc(lua_State *l)
{
	OpusEncoder *encoder = luaL_checkudata(l, 1, METATABLE_ENCODER);
	// no need to destroy since we allocated ourselves via lua_newuserdata and used opus_encoder_init
	mumble_log(LOG_DEBUG, "%s: %p garbage collected\n", METATABLE_ENCODER, encoder);
	return 0;
}

const luaL_Reg mumble_encoder[] = {
	{"reset", encoder_reset},
	{"getFinalRange", encoder_getFinalRange},
	{"getBandwidth", encoder_getBandwidth},
	{"getSampleRate", encoder_getSampleRate},
	{"getSamplerate", encoder_getSampleRate},

	{"setBitRate", encoder_setBitRate},
	{"setBitrate", encoder_setBitRate},
	{"getBitRate", encoder_getBitRate},
	{"getBitrate", encoder_getBitRate},

	{"encode", encoder_encode},
	{"encode_float", encoder_encode_float},
	{"__tostring", encoder_tostring},
	{"__gc", encoder_gc},
	{NULL, NULL}
};