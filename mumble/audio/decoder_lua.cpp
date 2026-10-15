#include "audio/decoder.h"

#include "defines.h"
#include "lua/bindings.h"
#include "lua/luainc.h"
#include "lua/metatables.h"
#include "util/log.h"

static MumbleOpusDecoder* decoder_check(lua_State *l, int index) {
	return luaL_checkclass<MumbleOpusDecoder>(l, index, METATABLE_DECODER);
}

int mumble_decoder_new(lua_State *l) {
	// Argument 1 = mumble.decoder table
	lua_remove(l, 1);

	opus_int32 samplerate = luaL_optinteger(l, 1, AUDIO_SAMPLE_RATE);
	int channels = luaL_optinteger(l, 2, AUDIO_PLAYBACK_CHANNELS);

	MumbleOpusDecoder *wrapper = lua_newclass<MumbleOpusDecoder>(l);
	luaL_getmetatable(l, METATABLE_DECODER);
	lua_setmetatable(l, -2);

	int error = wrapper->init(samplerate, channels);
	if (error != OPUS_OK) {
		return luaL_error(l, "could not initialize opus decoder: %s", opus_strerror(error));
	}
	return 1;
}

/* CTL HELPERS */

static int decoder_getinteger(lua_State *l, int request, const char *what) {
	opus_int32 value;
	if (decoder_check(l, 1)->get(request, &value) != OPUS_OK) {
		return luaL_error(l, "Failed to get %s", what);
	}
	lua_pushinteger(l, value);
	return 1;
}

/* GENERIC CTLS */

static int decoder_resetState(lua_State *l) {
	if (decoder_check(l, 1)->reset() != OPUS_OK) {
		return luaL_error(l, "Failed to reset decoder state");
	}
	return 0;
}

static int decoder_getFinalRange(lua_State *l) {
	opus_int32 value;
	if (decoder_check(l, 1)->get(OPUS_GET_FINAL_RANGE_REQUEST, &value) != OPUS_OK) {
		return luaL_error(l, "Failed to get final range");
	}
	// The final range is an opus_uint32
	lua_pushinteger(l, (opus_uint32) value);
	return 1;
}

static int decoder_getPitch(lua_State *l) {
	return decoder_getinteger(l, OPUS_GET_PITCH_REQUEST, "pitch");
}

static int decoder_getBandwidth(lua_State *l) {
	return decoder_getinteger(l, OPUS_GET_BANDWIDTH_REQUEST, "bandwidth");
}

/* GENERIC ENCODER/DECODER CTLS */

static int decoder_getSampleRate(lua_State *l) {
	return decoder_getinteger(l, OPUS_GET_SAMPLE_RATE_REQUEST, "sample rate");
}

static int decoder_getPhaseInversionDisabled(lua_State *l) {
	return decoder_getinteger(l, OPUS_GET_PHASE_INVERSION_DISABLED_REQUEST, "phase inversion status");
}

static int decoder_setPhaseInversionDisabled(lua_State *l) {
	if (decoder_check(l, 1)->set(OPUS_SET_PHASE_INVERSION_DISABLED_REQUEST, luaL_checkinteger(l, 2)) != OPUS_OK) {
		return luaL_error(l, "Failed to set phase inversion status");
	}
	return 0;
}

static int decoder_getInDTX(lua_State *l) {
	return decoder_getinteger(l, OPUS_GET_IN_DTX_REQUEST, "DTX state");
}

/* DECODE */

template <typename T>
static int decoder_decodepacket(lua_State *l, int (MumbleOpusDecoder::*decode)(const unsigned char*, size_t, T*)) {
	MumbleOpusDecoder *wrapper = decoder_check(l, 1);

	size_t encoded_len;
	const unsigned char* encoded = (const unsigned char*) luaL_checklstring(l, 2, &encoded_len);

	// Opus decoders are mono or stereo
	T pcm[OPUS_MAX_FRAME_SIZE * 2];
	int samples_per_channel = (wrapper->*decode)(encoded, encoded_len, pcm);

	if (samples_per_channel == 0) {
		return luaL_error(l, "invalid number of samples from opus_decoder_get_nb_samples");
	} else if (samples_per_channel < 0) {
		return luaL_error(l, "opus decoding error: %s", opus_strerror(samples_per_channel));
	}

	// Push the decoded PCM as a Lua string (binary blob)
	lua_pushlstring(l, (const char*) pcm, samples_per_channel * wrapper->channels * sizeof(T));
	return 1;
}

static int decoder_decode(lua_State *l) {
	return decoder_decodepacket<opus_int16>(l, &MumbleOpusDecoder::decode);
}

static int decoder_decode_float(lua_State *l) {
	return decoder_decodepacket<float>(l, &MumbleOpusDecoder::decodeFloat);
}

static int decoder_getNumSamples(lua_State *l) {
	MumbleOpusDecoder *wrapper = decoder_check(l, 1);

	size_t encoded_len;
	const unsigned char* encoded = (unsigned char*) luaL_checklstring(l, 2, &encoded_len);

	lua_pushinteger(l, wrapper->samples(encoded, encoded_len) * wrapper->channels);
	return 1;
}

static int decoder_getChannels(lua_State *l) {
	lua_pushinteger(l, decoder_check(l, 1)->channels);
	return 1;
}

/* METAMETHODS */

static int decoder_tostring(lua_State *l) {
	lua_pushfstring(l, "%s: %p", METATABLE_DECODER, lua_topointer(l, 1));
	return 1;
}

static int decoder_gc(lua_State *l) {
	MumbleOpusDecoder *wrapper = decoder_check(l, 1);
	mumble_log(LOG_DEBUG, "%s: %p garbage collected", METATABLE_DECODER, wrapper);
	wrapper->~MumbleOpusDecoder();
	return 0;
}

const luaL_Reg mumble_decoder[] = {
	{"reset", decoder_resetState},
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
