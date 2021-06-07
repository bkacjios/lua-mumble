#include "mumble.h"

#include "decoder.h"

int mumble_decoder_new(lua_State *l)
{
	// Argument 1 = mumble.decoder table
	opus_int32 samplerate = luaL_optinteger(l, 2, AUDIO_SAMPLE_RATE);
	int channels = luaL_checkinteger(l, 3);

	OpusDecoder *decoder = lua_newuserdata(l, opus_decoder_get_size(channels));
	luaL_getmetatable(l, METATABLE_DECODER);
	lua_setmetatable(l, -2);

	int error = opus_decoder_init(decoder, samplerate, channels);

	if (error != OPUS_OK) {
		lua_pushnil(l);
		lua_pushfstring(l, "could not initialize opus decoder: %s", opus_strerror(error));
		return 2;
	}

	return 1;
}

/* GENERIC CTLS */

static int decoder_reset(lua_State *l)
{
	OpusDecoder *decoder = luaL_checkudata(l, 1, METATABLE_DECODER);
	opus_decoder_ctl(decoder, OPUS_RESET_STATE);
	return 0;
}

static int decoder_getFinalRange(lua_State *l)
{
	OpusDecoder *decoder = luaL_checkudata(l, 1, METATABLE_DECODER);
	opus_uint32 range;
	opus_decoder_ctl(decoder, OPUS_GET_FINAL_RANGE(&range));
	lua_pushboolean(l, !range);
	return 1;
}

static int decoder_getBandwidth(lua_State *l)
{
	OpusDecoder *decoder = luaL_checkudata(l, 1, METATABLE_DECODER);
	opus_int32 width;
	opus_decoder_ctl(decoder, OPUS_GET_BANDWIDTH(&width));
	lua_pushinteger(l, width);
	return 1;
}

static int decoder_getSampleRate(lua_State *l)
{
	OpusDecoder *decoder = luaL_checkudata(l, 1, METATABLE_DECODER);
	opus_int32 rate;
	opus_decoder_ctl(decoder, OPUS_GET_SAMPLE_RATE(&rate));
	lua_pushinteger(l, rate);
	return 1;
}

/* DECODER CTLS */



static int decoder_decode(lua_State *l)
{
	OpusDecoder *decoder = luaL_checkudata(l, 1, METATABLE_DECODER);

	size_t encoded_len;
	const unsigned char* encoded = (unsigned char*) luaL_checklstring(l, 2, &encoded_len);

	const uint32_t pcm_size = AUDIO_SAMPLE_RATE * 60 / 1000;

	opus_int16 pcm[pcm_size];
	opus_int32 pcmlen = opus_decode(decoder, encoded, encoded_len, pcm, pcm_size, 0);

	lua_pushlstring(l, (const char *) pcm, pcmlen);
	return 1;
}

static int decoder_decode_float(lua_State *l)
{
	OpusDecoder *decoder = luaL_checkudata(l, 1, METATABLE_DECODER);

	size_t encoded_len;
	const unsigned char* encoded = (unsigned char*) luaL_checklstring(l, 2, &encoded_len);

	const uint32_t pcm_size = AUDIO_SAMPLE_RATE * 60 / 1000;

	float pcm[pcm_size];
	opus_int32 pcmlen = opus_decode_float(decoder, encoded, encoded_len, pcm, pcm_size, 0);

	lua_pushlstring(l, (const char *) pcm, pcmlen);
	return 1;
}

static int decoder_tostring(lua_State *l)
{
	OpusDecoder *decoder = luaL_checkudata(l, 1, METATABLE_DECODER);
	lua_pushfstring(l, "%s: %p", METATABLE_DECODER, decoder);
	return 1;
}

static int decoder_gc(lua_State *l)
{
	OpusDecoder *decoder = luaL_checkudata(l, 1, METATABLE_DECODER);
	// no need to destroy since we allocated ourselves via lua_newuserdata and used opus_decoder_init
	return 0;
}

const luaL_Reg mumble_decoder[] = {
	{"reset", decoder_reset},
	{"getFinalRange", decoder_getFinalRange},
	{"getBandwidth", decoder_getBandwidth},
	{"getSampleRate", decoder_getSampleRate},
	{"getSamplerate", decoder_getSampleRate},

	{"decode", decoder_decode},
	{"decode_float", decoder_decode_float},

	{"__tostring", decoder_tostring},
	{"__gc", decoder_gc},
	{NULL, NULL}
};