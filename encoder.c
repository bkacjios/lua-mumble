#include "mumble.h"

#include "encoder.h"

int mumble_encoder_new(lua_State *l)
{
	opus_int32 samplerate = luaL_optinteger(l, 1, 48000);

	OpusEncoder *encoder = lua_newuserdata(l, opus_encoder_get_size(1));
	luaL_getmetatable(l, METATABLE_ENCODER);
	lua_setmetatable(l, -2);

	int error = opus_encoder_init(encoder, samplerate, 1, OPUS_APPLICATION_AUDIO);

	if (error != OPUS_OK) {
		lua_pushnil(l);
		lua_pushfstring(l, "could not initialize the Opus encoder: %s", opus_strerror(error));
		return 2;
	}

	opus_encoder_ctl(encoder, OPUS_SET_VBR(0));
	opus_encoder_ctl(encoder, OPUS_SET_BITRATE(AUDIO_DEFAULT_BITRATE));
	return 1;
}

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
	OpusEncoder *encoder = luaL_checkudata(l, 1, METATABLE_ENCODER);
	lua_pushfstring(l, "%s: %p", METATABLE_ENCODER, encoder);
	return 1;
}

const luaL_Reg mumble_encoder[] = {
	{"setBitRate", encoder_setBitRate},
	{"getBitRate", encoder_getBitRate},
	{"encode", encoder_encode},
	{"encode_float", encoder_encode_float},
	{"__tostring", encoder_tostring},
	{NULL, NULL}
};