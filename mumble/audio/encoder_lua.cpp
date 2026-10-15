#include "audio/encoder.h"

#include "defines.h"
#include "lua/bindings.h"
#include "lua/luainc.h"
#include "lua/luautil.h"
#include "lua/metatables.h"
#include "util/log.h"

static MumbleOpusEncoder* encoder_check(lua_State *l, int index) {
	return luaL_checkclass<MumbleOpusEncoder>(l, index, METATABLE_ENCODER);
}

int mumble_encoder_new(lua_State *l) {
	// Argument 1 = mumble.encoder table
	lua_remove(l, 1);

	opus_int32 samplerate = luaL_optinteger(l, 1, AUDIO_SAMPLE_RATE);
	int channels = luaL_optinteger(l, 2, AUDIO_PLAYBACK_CHANNELS);

	MumbleOpusEncoder *wrapper = lua_newclass<MumbleOpusEncoder>(l);
	luaL_getmetatable(l, METATABLE_ENCODER);
	lua_setmetatable(l, -2);

	int error = wrapper->init(samplerate, channels);
	if (error != OPUS_OK) {
		return luaL_error(l, "could not initialize opus encoder: %s", opus_strerror(error));
	}
	return 1;
}

/* CTL HELPERS */

static int encoder_getinteger(lua_State *l, int request, const char *what) {
	opus_int32 value;
	if (encoder_check(l, 1)->get(request, &value) != OPUS_OK) {
		return luaL_error(l, "Failed to get %s", what);
	}
	lua_pushinteger(l, value);
	return 1;
}

static int encoder_getboolean(lua_State *l, int request, const char *what) {
	opus_int32 value;
	if (encoder_check(l, 1)->get(request, &value) != OPUS_OK) {
		return luaL_error(l, "Failed to get %s", what);
	}
	lua_pushboolean(l, value);
	return 1;
}

static int encoder_setvalue(lua_State *l, int request, const char *what, opus_int32 value) {
	if (encoder_check(l, 1)->set(request, value) != OPUS_OK) {
		return luaL_error(l, "Failed to set %s", what);
	}
	return 0;
}

static int encoder_setinteger(lua_State *l, int request, const char *what) {
	return encoder_setvalue(l, request, what, luaL_checkinteger(l, 2));
}

static int encoder_setboolean(lua_State *l, int request, const char *what) {
	return encoder_setvalue(l, request, what, luaL_checkboolean(l, 2));
}

/* GENERIC CTLS */

static int encoder_resetState(lua_State *l) {
	if (encoder_check(l, 1)->reset() != OPUS_OK) {
		return luaL_error(l, "Failed to reset encoder state");
	}
	return 0;
}

static int encoder_getFinalRange(lua_State *l) {
	opus_int32 value;
	if (encoder_check(l, 1)->get(OPUS_GET_FINAL_RANGE_REQUEST, &value) != OPUS_OK) {
		return luaL_error(l, "Failed to get final range");
	}
	// The final range is an opus_uint32
	lua_pushinteger(l, (opus_uint32) value);
	return 1;
}

static int encoder_getPitch(lua_State *l) {
	return encoder_getinteger(l, OPUS_GET_PITCH_REQUEST, "pitch");
}

static int encoder_getBandwidth(lua_State *l) {
	return encoder_getinteger(l, OPUS_GET_BANDWIDTH_REQUEST, "bandwidth");
}

/* GENERIC ENCODER/DECODER CTLS */

static int encoder_getSampleRate(lua_State *l) {
	return encoder_getinteger(l, OPUS_GET_SAMPLE_RATE_REQUEST, "sample rate");
}

static int encoder_getPhaseInversionDisabled(lua_State *l) {
	return encoder_getboolean(l, OPUS_GET_PHASE_INVERSION_DISABLED_REQUEST, "phase inversion status");
}

static int encoder_setPhaseInversionDisabled(lua_State *l) {
	return encoder_setboolean(l, OPUS_SET_PHASE_INVERSION_DISABLED_REQUEST, "phase inversion status");
}

static int encoder_getInDTX(lua_State *l) {
	return encoder_getboolean(l, OPUS_GET_IN_DTX_REQUEST, "DTX state");
}

/* ENCODER CTLS */

static int encoder_getComplexity(lua_State *l) {
	return encoder_getinteger(l, OPUS_GET_COMPLEXITY_REQUEST, "complexity");
}

static int encoder_setComplexity(lua_State *l) {
	return encoder_setinteger(l, OPUS_SET_COMPLEXITY_REQUEST, "complexity");
}

static int encoder_getBitRate(lua_State *l) {
	return encoder_getinteger(l, OPUS_GET_BITRATE_REQUEST, "bitrate");
}

static int encoder_setBitRate(lua_State *l) {
	return encoder_setinteger(l, OPUS_SET_BITRATE_REQUEST, "bitrate");
}

static int encoder_getVBR(lua_State *l) {
	return encoder_getboolean(l, OPUS_GET_VBR_REQUEST, "VBR");
}

static int encoder_setVBR(lua_State *l) {
	return encoder_setboolean(l, OPUS_SET_VBR_REQUEST, "VBR");
}

static int encoder_getMaxBandwidth(lua_State *l) {
	return encoder_getinteger(l, OPUS_GET_MAX_BANDWIDTH_REQUEST, "maximum bandwidth");
}

static int encoder_setMaxBandwidth(lua_State *l) {
	return encoder_setinteger(l, OPUS_SET_MAX_BANDWIDTH_REQUEST, "maximum bandwidth");
}

static int encoder_getVBRConstraint(lua_State *l) {
	return encoder_getinteger(l, OPUS_GET_VBR_CONSTRAINT_REQUEST, "VBR constraint");
}

static int encoder_setVBRConstraint(lua_State *l) {
	return encoder_setinteger(l, OPUS_SET_VBR_CONSTRAINT_REQUEST, "VBR constraint");
}

static int encoder_getForceChannels(lua_State *l) {
	return encoder_getinteger(l, OPUS_GET_FORCE_CHANNELS_REQUEST, "forced channels");
}

static int encoder_setForceChannels(lua_State *l) {
	return encoder_setinteger(l, OPUS_SET_FORCE_CHANNELS_REQUEST, "forced channels");
}

static int encoder_setBandwidth(lua_State *l) {
	return encoder_setinteger(l, OPUS_SET_BANDWIDTH_REQUEST, "bandwidth");
}

static int encoder_getSignal(lua_State *l) {
	return encoder_getinteger(l, OPUS_GET_SIGNAL_REQUEST, "signal type");
}

static int encoder_setSignal(lua_State *l) {
	return encoder_setinteger(l, OPUS_SET_SIGNAL_REQUEST, "signal type");
}

static int encoder_getApplication(lua_State *l) {
	return encoder_getinteger(l, OPUS_GET_APPLICATION_REQUEST, "application");
}

static int encoder_setApplication(lua_State *l) {
	return encoder_setinteger(l, OPUS_SET_APPLICATION_REQUEST, "application");
}

static int encoder_getLookahead(lua_State *l) {
	return encoder_getinteger(l, OPUS_GET_LOOKAHEAD_REQUEST, "lookahead");
}

static int encoder_getInbandFEC(lua_State *l) {
	return encoder_getinteger(l, OPUS_GET_INBAND_FEC_REQUEST, "inband FEC");
}

static int encoder_setInbandFEC(lua_State *l) {
	return encoder_setinteger(l, OPUS_SET_INBAND_FEC_REQUEST, "inband FEC");
}

static int encoder_getPacketLossPerc(lua_State *l) {
	return encoder_getinteger(l, OPUS_GET_PACKET_LOSS_PERC_REQUEST, "packet loss percentage");
}

static int encoder_setPacketLossPerc(lua_State *l) {
	return encoder_setinteger(l, OPUS_SET_PACKET_LOSS_PERC_REQUEST, "packet loss percentage");
}

static int encoder_getDTX(lua_State *l) {
	return encoder_getinteger(l, OPUS_GET_DTX_REQUEST, "DTX");
}

static int encoder_setDTX(lua_State *l) {
	return encoder_setinteger(l, OPUS_SET_DTX_REQUEST, "DTX");
}

/* ENCODE */

static int encoder_encode(lua_State *l) {
	MumbleOpusEncoder *wrapper = encoder_check(l, 1);
	// How many 10ms frames to encode
	int frames = luaL_checkinteger(l, 2);
	const opus_int16 *pcm = (const opus_int16*) luaL_checkstring(l, 3);

	unsigned char output[0x1FFF];
	opus_int32 length = wrapper->encode(pcm, frames * AUDIO_SAMPLE_RATE / 100, output, sizeof(output));
	if (length < 0) {
		return luaL_error(l, "opus encoding error: %s", opus_strerror(length));
	}

	lua_pushlstring(l, (char*) output, length);
	return 1;
}

static int encoder_encode_float(lua_State *l) {
	MumbleOpusEncoder *wrapper = encoder_check(l, 1);
	int frames = luaL_checkinteger(l, 2);
	const float *pcm = (const float*) luaL_checkstring(l, 3);

	unsigned char output[0x1FFF];
	opus_int32 length = wrapper->encodeFloat(pcm, frames * AUDIO_SAMPLE_RATE / 100, output, sizeof(output));
	if (length < 0) {
		return luaL_error(l, "opus encoding error: %s", opus_strerror(length));
	}

	lua_pushlstring(l, (char*) output, length);
	return 1;
}

/* METAMETHODS */

static int encoder_tostring(lua_State *l) {
	lua_pushfstring(l, "%s: %p", METATABLE_ENCODER, lua_topointer(l, 1));
	return 1;
}

static int encoder_gc(lua_State *l) {
	MumbleOpusEncoder *wrapper = encoder_check(l, 1);
	mumble_log(LOG_DEBUG, "%s: %p garbage collected", METATABLE_ENCODER, wrapper->encoder);
	wrapper->~MumbleOpusEncoder();
	return 0;
}

const luaL_Reg mumble_encoder[] = {
	{"reset", encoder_resetState},
	{"resetState", encoder_resetState},
	{"getFinalRange", encoder_getFinalRange},
	{"getPitch", encoder_getPitch},
	{"getBandwidth", encoder_getBandwidth},

	{"getSampleRate", encoder_getSampleRate},
	{"getPhaseInversionDisabled", encoder_getPhaseInversionDisabled},
	{"setPhaseInversionDisabled", encoder_setPhaseInversionDisabled},
	{"getInDTX", encoder_getInDTX},

	{"getComplexity", encoder_getComplexity},
	{"setComplexity", encoder_setComplexity},
	{"getBitRate", encoder_getBitRate},
	{"setBitRate", encoder_setBitRate},
	{"getBitrate", encoder_getBitRate},
	{"setBitrate", encoder_setBitRate},
	{"getVBR", encoder_getVBR},
	{"setVBR", encoder_setVBR},
	{"getVBRConstraint", encoder_getVBRConstraint},
	{"setVBRConstraint", encoder_setVBRConstraint},
	{"getForceChannels", encoder_getForceChannels},
	{"setForceChannels", encoder_setForceChannels},
	{"getMaxBandwidth", encoder_getMaxBandwidth},
	{"setMaxBandwidth", encoder_setMaxBandwidth},
	{"setBandwidth", encoder_setBandwidth},
	{"getSignal", encoder_getSignal},
	{"setSignal", encoder_setSignal},
	{"getApplication", encoder_getApplication},
	{"setApplication", encoder_setApplication},
	{"getLookahead", encoder_getLookahead},
	{"getInbandFEC", encoder_getInbandFEC},
	{"setInbandFEC", encoder_setInbandFEC},
	{"getPacketLossPerc", encoder_getPacketLossPerc},
	{"setPacketLossPerc", encoder_setPacketLossPerc},
	{"getDTX", encoder_getDTX},
	{"setDTX", encoder_setDTX},

	{"encode", encoder_encode},
	{"encodeFloat", encoder_encode_float},
	{"encode_float", encoder_encode_float},

	{"__tostring", encoder_tostring},
	{"__gc", encoder_gc},
	{NULL, NULL}
};
