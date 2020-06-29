#include "mumble.h"

#include "decoder.h"

int mumble_decoder_new(lua_State *l)
{
	// Argument 1 = mumble.decoder table
	opus_int32 samplerate = luaL_optinteger(l, 2, AUDIO_SAMPLE_RATE);
	int channels = luaL_optinteger(l, 3, 1);

	OpusDecoder *decoder = lua_newuserdata(l, opus_decoder_get_size(channels));
	luaL_getmetatable(l, METATABLE_DECODER);
	lua_setmetatable(l, -2);

	int error = opus_decoder_init(decoder, samplerate, channels);

	if (error != OPUS_OK) {
		lua_pushnil(l);
		lua_pushfstring(l, "could not initialize the Opus decoder: %s", opus_strerror(error));
		return 2;
	}

	opus_decoder_ctl(decoder, OPUS_SET_VBR(0));
	opus_decoder_ctl(decoder, OPUS_SET_BITRATE(AUDIO_DEFAULT_BITRATE));
	return 1;
}

static int decoder_setBitRate(lua_State *l)
{
	OpusDecoder *decoder = luaL_checkudata(l, 1, METATABLE_DECODER);
	opus_decoder_ctl(decoder, OPUS_SET_BITRATE(luaL_checkinteger(l, 2)));
	return 0;
}

static int decoder_getBitRate(lua_State *l)
{
	OpusDecoder *decoder = luaL_checkudata(l, 1, METATABLE_DECODER);
	opus_int32 rate;
	opus_decoder_ctl(decoder, OPUS_GET_BITRATE(&rate));
	lua_pushinteger(l, rate);
	return 1;
}

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
	//opus_decoder_destroy(decoder); // This calls 'free' which Lua is already doing after a __gc call
	return 0;
}

const luaL_Reg mumble_decoder[] = {
	{"setBitRate", decoder_setBitRate},
	{"getBitRate", decoder_getBitRate},
	{"decode", decoder_decode},
	{"decode_float", decoder_decode_float},
	{"__tostring", decoder_tostring},
	{"__gc", decoder_gc},
	{NULL, NULL}
};