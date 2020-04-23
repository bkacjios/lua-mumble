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
	opus_encoder_ctl(encoder, OPUS_SET_BITRATE(40000));

	return 1;
}

static int encoder_setBitRate(lua_State *l)
{
	OpusEncoder *encoder = luaL_checkudata(l, 1, METATABLE_ENCODER);
	opus_encoder_ctl(encoder, OPUS_SET_BITRATE(luaL_checkinteger(l, 2)));
	return 0;
}

static int encoder_tostring(lua_State *l)
{
	OpusEncoder *encoder = luaL_checkudata(l, 1, METATABLE_ENCODER);
	lua_pushfstring(l, "%s: %p", METATABLE_ENCODER, encoder);
	return 1;
}

const luaL_Reg mumble_encoder[] = {
	{"setBitRate", encoder_setBitRate},
	{"__tostring", encoder_tostring},
	{NULL, NULL}
};