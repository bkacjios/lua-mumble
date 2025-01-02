#include "mumble.h"

#include "decoder.h"
#include "util.h"
#include "log.h"

int mumble_decoder_new(lua_State *l) {
	// Argument 1 = mumble.decoder table
	opus_int32 samplerate = luaL_optinteger(l, 2, AUDIO_SAMPLE_RATE);
	int channels = luaL_optinteger(l, 3, AUDIO_PLAYBACK_CHANNELS);

	MumbleOpusDecoder *wrapper = lua_newuserdata(l, sizeof(MumbleOpusDecoder));
	wrapper->decoder = NULL;
	luaL_getmetatable(l, METATABLE_DECODER);
	lua_setmetatable(l, -2);

	int error;
	OpusDecoder* decoder = opus_decoder_create(samplerate, channels, &error);

	if (error != OPUS_OK) {
		return luaL_error(l, "could not initialize opus decoder: %s", opus_strerror(error));
	}

	wrapper->decoder = decoder;
	wrapper->channels = channels;

	opus_decoder_ctl(wrapper->decoder, OPUS_SET_PHASE_INVERSION_DISABLED(1));

	return 1;
}


/* GENERIC CTLS */

static int decoder_resetState(lua_State *l) {
	MumbleOpusDecoder *wrapper = luaL_checkudata(l, 1, METATABLE_DECODER);
	if (opus_decoder_ctl(wrapper->decoder, OPUS_RESET_STATE) != OPUS_OK) {
		return luaL_error(l, "Failed to reset decoder state");
	}
	return 0;
}

static int decoder_getFinalRange(lua_State *l) {
	MumbleOpusDecoder *wrapper = luaL_checkudata(l, 1, METATABLE_DECODER);
	opus_uint32 value;
	if (opus_decoder_ctl(wrapper->decoder, OPUS_GET_FINAL_RANGE(&value)) != OPUS_OK) {
		return luaL_error(l, "Failed to get final range");
	}
	lua_pushinteger(l, value);
	return 1;
}

static int decoder_getPitch(lua_State *l) {
	MumbleOpusDecoder *wrapper = luaL_checkudata(l, 1, METATABLE_DECODER);
	opus_int32 value;
	if (opus_decoder_ctl(wrapper->decoder, OPUS_GET_PITCH(&value)) != OPUS_OK) {
		return luaL_error(l, "Failed to get pitch");
	}
	lua_pushinteger(l, value);
	return 1;
}

static int decoder_getBandwidth(lua_State *l) {
	MumbleOpusDecoder *wrapper = luaL_checkudata(l, 1, METATABLE_DECODER);
	opus_int32 value;
	if (opus_decoder_ctl(wrapper->decoder, OPUS_GET_BANDWIDTH(&value)) != OPUS_OK) {
		return luaL_error(l, "Failed to get bandwidth");
	}
	lua_pushinteger(l, value);
	return 1;
}

/* GENERIC decoder/DECODER CTLS */

static int decoder_getSampleRate(lua_State *l) {
	MumbleOpusDecoder *wrapper = luaL_checkudata(l, 1, METATABLE_DECODER);
	opus_int32 value;
	if (opus_decoder_ctl(wrapper->decoder, OPUS_GET_SAMPLE_RATE(&value)) != OPUS_OK) {
		return luaL_error(l, "Failed to get sample rate");
	}
	lua_pushinteger(l, value);
	return 1;
}

static int decoder_getPhaseInversionDisabled(lua_State *l) {
	MumbleOpusDecoder *wrapper = luaL_checkudata(l, 1, METATABLE_DECODER);
	opus_int32 value;
	if (opus_decoder_ctl(wrapper->decoder, OPUS_GET_PHASE_INVERSION_DISABLED(&value)) != OPUS_OK) {
		return luaL_error(l, "Failed to get phase inversion status");
	}
	lua_pushinteger(l, value);
	return 1;
}

static int decoder_setPhaseInversionDisabled(lua_State *l) {
	MumbleOpusDecoder *wrapper = luaL_checkudata(l, 1, METATABLE_DECODER);
	opus_int32 value = luaL_checkinteger(l, 2);
	if (opus_decoder_ctl(wrapper->decoder, OPUS_SET_PHASE_INVERSION_DISABLED(value)) != OPUS_OK) {
		return luaL_error(l, "Failed to set phase inversion status");
	}
	return 0;
}

static int decoder_getInDTX(lua_State *l) {
	MumbleOpusDecoder *wrapper = luaL_checkudata(l, 1, METATABLE_DECODER);
	opus_int32 value;
	if (opus_decoder_ctl(wrapper->decoder, OPUS_GET_IN_DTX(&value)) != OPUS_OK) {
		return luaL_error(l, "Failed to get DTX state");
	}
	lua_pushinteger(l, value);
	return 1;
}

/* DECODE */

static int decoder_decode(lua_State *l) {
	MumbleOpusDecoder *wrapper = luaL_checkudata(l, 1, METATABLE_DECODER);

	size_t encoded_len;
	const unsigned char* encoded = (unsigned char*) luaL_checklstring(l, 2, &encoded_len);

	int samples = opus_decoder_get_nb_samples(wrapper->decoder, encoded, encoded_len) * wrapper->channels;

	opus_int16 pcm[samples];
	opus_int32 pcmlen = opus_decode(wrapper->decoder, encoded, encoded_len, pcm, samples, 0);

	if (pcmlen < 0) {
		return luaL_error(l, "opus decoding error: %s", opus_strerror(pcmlen));
	}

	size_t outputlen = pcmlen * wrapper->channels * sizeof(opus_int16);

	char byte_data[outputlen];
	memcpy(byte_data, pcm, outputlen);

	lua_pushlstring(l, byte_data, outputlen);
	return 1;
}

static int decoder_decode_float(lua_State *l) {
	MumbleOpusDecoder *wrapper = luaL_checkudata(l, 1, METATABLE_DECODER);

	size_t encoded_len;
	const unsigned char* encoded = (unsigned char*) luaL_checklstring(l, 2, &encoded_len);

	int samples = opus_decoder_get_nb_samples(wrapper->decoder, encoded, encoded_len) * wrapper->channels;

	float pcm[samples];
	opus_int32 pcmlen = opus_decode_float(wrapper->decoder, encoded, encoded_len, pcm, samples, 0);

	if (pcmlen < 0) {
		return luaL_error(l, "opus decoding error: %s", opus_strerror(pcmlen));
	}

	size_t outputlen = pcmlen * wrapper->channels * sizeof(float);

	char byte_data[outputlen];
	memcpy(byte_data, pcm, outputlen);

	lua_pushlstring(l, byte_data, outputlen);
	return 1;
}

static int decoder_getNumSamples(lua_State *l) {
	MumbleOpusDecoder *wrapper = luaL_checkudata(l, 1, METATABLE_DECODER);

	size_t encoded_len;
	const unsigned char* encoded = (unsigned char*) luaL_checklstring(l, 2, &encoded_len);

	int samples = opus_decoder_get_nb_samples(wrapper->decoder, encoded, encoded_len) * wrapper->channels;

	lua_pushinteger(l, samples);
	return 1;
}

static int decoder_getChannels(lua_State *l) {
	MumbleOpusDecoder *wrapper = luaL_checkudata(l, 1, METATABLE_DECODER);
	lua_pushinteger(l, wrapper->channels);
	return 1;
}

/* METAMETHODS */

static int decoder_tostring(lua_State *l) {
	lua_pushfstring(l, "%s: %p", METATABLE_DECODER, lua_topointer(l, 1));
	return 1;
}

static int decoder_gc(lua_State *l) {
	MumbleOpusDecoder *wrapper = luaL_checkudata(l, 1, METATABLE_DECODER);
	if (wrapper->decoder != NULL) {
		mumble_log(LOG_DEBUG, "destroying wrapper->decoder: %p", wrapper->decoder);
		opus_decoder_destroy(wrapper->decoder);
	}
	mumble_log(LOG_DEBUG, "%s: %p garbage collected", METATABLE_DECODER, wrapper);
	return 0;
}

const luaL_Reg mumble_decoder[] = {
	{"resetState", decoder_resetState},
	{"getFinalRange", decoder_getFinalRange},
	{"getPitch", decoder_getPitch},
	{"getBandwidth", decoder_getBandwidth},

	{"getSampleRate", decoder_getSampleRate},
	{"getPhaseInversionDisabled", decoder_getPhaseInversionDisabled},
	{"setPhaseInversionDisabled", decoder_setPhaseInversionDisabled},
	{"getInDTX", decoder_getInDTX},

	{"decode", decoder_decode},
	{"decodeFloat", decoder_decode_float},
	{"decode_float", decoder_decode_float},

	{"getNumSamples", decoder_getNumSamples},
	{"getChannels", decoder_getChannels},

	{"__tostring", decoder_tostring},
	{"__gc", decoder_gc},
	{NULL, NULL}
};