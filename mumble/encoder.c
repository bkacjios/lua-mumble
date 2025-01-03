#include "mumble.h"

#include "encoder.h"
#include "util.h"
#include "log.h"

int mumble_encoder_new(lua_State *l) {
	// Argument 1 = mumble.encoder table
	opus_int32 samplerate = luaL_optinteger(l, 2, AUDIO_SAMPLE_RATE);
	int channels = luaL_optinteger(l, 3, AUDIO_PLAYBACK_CHANNELS);

	OpusEncoder *encoder = lua_newuserdata(l, opus_encoder_get_size(channels));
	luaL_getmetatable(l, METATABLE_ENCODER);
	lua_setmetatable(l, -2);

	int error = opus_encoder_init(encoder, samplerate, channels, OPUS_APPLICATION_AUDIO);

	if (error != OPUS_OK) {
		return luaL_error(l, "could not initialize opus encoder: %s", opus_strerror(error));
	}

	opus_encoder_ctl(encoder, OPUS_SET_VBR(0));
	opus_encoder_ctl(encoder, OPUS_SET_BITRATE(AUDIO_DEFAULT_BITRATE));
	return 1;
}

/* GENERIC CTLS */

static int encoder_resetState(lua_State *l) {
	OpusEncoder *encoder = luaL_checkudata(l, 1, METATABLE_ENCODER);
	if (opus_encoder_ctl(encoder, OPUS_RESET_STATE) != OPUS_OK) {
		return luaL_error(l, "Failed to reset encoder state");
	}
	return 0;
}

static int encoder_getFinalRange(lua_State *l) {
	OpusEncoder *encoder = luaL_checkudata(l, 1, METATABLE_ENCODER);
	opus_uint32 value;
	if (opus_encoder_ctl(encoder, OPUS_GET_FINAL_RANGE(&value)) != OPUS_OK) {
		return luaL_error(l, "Failed to get final range");
	}
	lua_pushinteger(l, value);
	return 1;
}

static int encoder_getPitch(lua_State *l) {
	OpusEncoder *encoder = luaL_checkudata(l, 1, METATABLE_ENCODER);
	opus_int32 value;
	if (opus_encoder_ctl(encoder, OPUS_GET_PITCH(&value)) != OPUS_OK) {
		return luaL_error(l, "Failed to get pitch");
	}
	lua_pushinteger(l, value);
	return 1;
}

static int encoder_getBandwidth(lua_State *l) {
	OpusEncoder *encoder = luaL_checkudata(l, 1, METATABLE_ENCODER);
	opus_int32 value;
	if (opus_encoder_ctl(encoder, OPUS_GET_BANDWIDTH(&value)) != OPUS_OK) {
		return luaL_error(l, "Failed to get bandwidth");
	}
	lua_pushinteger(l, value);
	return 1;
}

/* GENERIC ENCODER/DECODER CTLS */

static int encoder_getSampleRate(lua_State *l) {
	OpusEncoder *encoder = luaL_checkudata(l, 1, METATABLE_ENCODER);
	opus_int32 value;
	if (opus_encoder_ctl(encoder, OPUS_GET_SAMPLE_RATE(&value)) != OPUS_OK) {
		return luaL_error(l, "Failed to get sample rate");
	}
	lua_pushinteger(l, value);
	return 1;
}

static int encoder_getPhaseInversionDisabled(lua_State *l) {
	OpusEncoder *encoder = luaL_checkudata(l, 1, METATABLE_ENCODER);
	opus_int32 value;
	if (opus_encoder_ctl(encoder, OPUS_GET_PHASE_INVERSION_DISABLED(&value)) != OPUS_OK) {
		return luaL_error(l, "Failed to get phase inversion status");
	}
	lua_pushboolean(l, value);
	return 1;
}

static int encoder_setPhaseInversionDisabled(lua_State *l) {
	OpusEncoder *encoder = luaL_checkudata(l, 1, METATABLE_ENCODER);
	opus_int32 value = luaL_checkboolean(l, 2);
	if (opus_encoder_ctl(encoder, OPUS_SET_PHASE_INVERSION_DISABLED(value)) != OPUS_OK) {
		return luaL_error(l, "Failed to set phase inversion status");
	}
	return 0;
}

static int encoder_getInDTX(lua_State *l) {
	OpusEncoder *encoder = luaL_checkudata(l, 1, METATABLE_ENCODER);
	opus_int32 value;
	if (opus_encoder_ctl(encoder, OPUS_GET_IN_DTX(&value)) != OPUS_OK) {
		return luaL_error(l, "Failed to get DTX state");
	}
	lua_pushboolean(l, value);
	return 1;
}

/* ENCODER CTLS */

static int encoder_getComplexity(lua_State *l) {
	OpusEncoder *encoder = luaL_checkudata(l, 1, METATABLE_ENCODER);
	opus_int32 value;
	if (opus_encoder_ctl(encoder, OPUS_GET_COMPLEXITY(&value)) != OPUS_OK) {
		return luaL_error(l, "Failed to get complexity");
	}
	lua_pushinteger(l, value);
	return 1;
}

static int encoder_setComplexity(lua_State *l) {
	OpusEncoder *encoder = luaL_checkudata(l, 1, METATABLE_ENCODER);
	opus_int32 value = luaL_checkinteger(l, 2);
	if (opus_encoder_ctl(encoder, OPUS_SET_COMPLEXITY(value)) != OPUS_OK) {
		return luaL_error(l, "Failed to set complexity");
	}
	return 0;
}

static int encoder_getBitRate(lua_State *l) {
	OpusEncoder *encoder = luaL_checkudata(l, 1, METATABLE_ENCODER);
	opus_int32 value;
	if (opus_encoder_ctl(encoder, OPUS_GET_BITRATE(&value)) != OPUS_OK) {
		return luaL_error(l, "Failed to get bitrate");
	}
	lua_pushinteger(l, value);
	return 1;
}

static int encoder_setBitRate(lua_State *l) {
	OpusEncoder *encoder = luaL_checkudata(l, 1, METATABLE_ENCODER);
	opus_int32 value = luaL_checkinteger(l, 2);
	if (opus_encoder_ctl(encoder, OPUS_SET_BITRATE(value)) != OPUS_OK) {
		return luaL_error(l, "Failed to set bitrate");
	}
	return 0;
}

static int encoder_getVBR(lua_State *l) {
	OpusEncoder *encoder = luaL_checkudata(l, 1, METATABLE_ENCODER);
	opus_int32 value;
	if (opus_encoder_ctl(encoder, OPUS_GET_VBR(&value)) != OPUS_OK) {
		return luaL_error(l, "Failed to get VBR");
	}
	lua_pushboolean(l, value);
	return 1;
}

static int encoder_setVBR(lua_State *l) {
	OpusEncoder *encoder = luaL_checkudata(l, 1, METATABLE_ENCODER);
	opus_int32 value = luaL_checkboolean(l, 2);
	if (opus_encoder_ctl(encoder, OPUS_SET_VBR(value)) != OPUS_OK) {
		return luaL_error(l, "Failed to set VBR");
	}
	return 0;
}

static int encoder_getMaxBandwidth(lua_State *l) {
	OpusEncoder *encoder = luaL_checkudata(l, 1, METATABLE_ENCODER);
	opus_int32 value;
	if (opus_encoder_ctl(encoder, OPUS_GET_MAX_BANDWIDTH(&value)) != OPUS_OK) {
		return luaL_error(l, "Failed to get maximum bandwidth");
	}
	lua_pushinteger(l, value);
	return 1;
}

static int encoder_setMaxBandwidth(lua_State *l) {
	OpusEncoder *encoder = luaL_checkudata(l, 1, METATABLE_ENCODER);
	opus_int32 value = luaL_checkinteger(l, 2);
	if (opus_encoder_ctl(encoder, OPUS_SET_MAX_BANDWIDTH(value)) != OPUS_OK) {
		return luaL_error(l, "Failed to set maximum bandwidth");
	}
	return 0;
}

static int encoder_getVBRConstraint(lua_State *l) {
	OpusEncoder *encoder = luaL_checkudata(l, 1, METATABLE_ENCODER);
	opus_int32 value;
	if (opus_encoder_ctl(encoder, OPUS_GET_VBR_CONSTRAINT(&value)) != OPUS_OK) {
		return luaL_error(l, "Failed to get VBR constraint");
	}
	lua_pushinteger(l, value);
	return 1;
}

static int encoder_setVBRConstraint(lua_State *l) {
	OpusEncoder *encoder = luaL_checkudata(l, 1, METATABLE_ENCODER);
	opus_int32 value = luaL_checkinteger(l, 2);
	if (opus_encoder_ctl(encoder, OPUS_SET_VBR_CONSTRAINT(value)) != OPUS_OK) {
		return luaL_error(l, "Failed to set VBR constraint");
	}
	return 0;
}

static int encoder_getForceChannels(lua_State *l) {
	OpusEncoder *encoder = luaL_checkudata(l, 1, METATABLE_ENCODER);
	opus_int32 value;
	if (opus_encoder_ctl(encoder, OPUS_GET_FORCE_CHANNELS(&value)) != OPUS_OK) {
		return luaL_error(l, "Failed to get forced channels");
	}
	lua_pushinteger(l, value);
	return 1;
}

static int encoder_setForceChannels(lua_State *l) {
	OpusEncoder *encoder = luaL_checkudata(l, 1, METATABLE_ENCODER);
	opus_int32 value = luaL_checkinteger(l, 2);
	if (opus_encoder_ctl(encoder, OPUS_SET_FORCE_CHANNELS(value)) != OPUS_OK) {
		return luaL_error(l, "Failed to set forced channels");
	}
	return 0;
}

static int encoder_setBandwidth(lua_State *l) {
	OpusEncoder *encoder = luaL_checkudata(l, 1, METATABLE_ENCODER);
	opus_int32 value = luaL_checkinteger(l, 2);
	if (opus_encoder_ctl(encoder, OPUS_SET_BANDWIDTH(value)) != OPUS_OK) {
		return luaL_error(l, "Failed to set bandwidth");
	}
	return 0;
}

static int encoder_getSignal(lua_State *l) {
	OpusEncoder *encoder = luaL_checkudata(l, 1, METATABLE_ENCODER);
	opus_int32 value;
	if (opus_encoder_ctl(encoder, OPUS_GET_SIGNAL(&value)) != OPUS_OK) {
		return luaL_error(l, "Failed to get signal type");
	}
	lua_pushinteger(l, value);
	return 1;
}

static int encoder_setSignal(lua_State *l) {
	OpusEncoder *encoder = luaL_checkudata(l, 1, METATABLE_ENCODER);
	opus_int32 value = luaL_checkinteger(l, 2);
	if (opus_encoder_ctl(encoder, OPUS_SET_SIGNAL(value)) != OPUS_OK) {
		return luaL_error(l, "Failed to set signal type");
	}
	return 0;
}

static int encoder_getApplication(lua_State *l) {
	OpusEncoder *encoder = luaL_checkudata(l, 1, METATABLE_ENCODER);
	opus_int32 value;
	if (opus_encoder_ctl(encoder, OPUS_GET_APPLICATION(&value)) != OPUS_OK) {
		return luaL_error(l, "Failed to get application");
	}
	lua_pushinteger(l, value);
	return 1;
}

static int encoder_setApplication(lua_State *l) {
	OpusEncoder *encoder = luaL_checkudata(l, 1, METATABLE_ENCODER);
	opus_int32 value = luaL_checkinteger(l, 2);
	if (opus_encoder_ctl(encoder, OPUS_SET_APPLICATION(value)) != OPUS_OK) {
		return luaL_error(l, "Failed to set application");
	}
	return 0;
}

static int encoder_getLookahead(lua_State *l) {
	OpusEncoder *encoder = luaL_checkudata(l, 1, METATABLE_ENCODER);
	opus_int32 value;
	if (opus_encoder_ctl(encoder, OPUS_GET_LOOKAHEAD(&value)) != OPUS_OK) {
		return luaL_error(l, "Failed to get lookahead");
	}
	lua_pushinteger(l, value);
	return 1;
}

static int encoder_getInbandFEC(lua_State *l) {
	OpusEncoder *encoder = luaL_checkudata(l, 1, METATABLE_ENCODER);
	opus_int32 value;
	if (opus_encoder_ctl(encoder, OPUS_GET_INBAND_FEC(&value)) != OPUS_OK) {
		return luaL_error(l, "Failed to get inband FEC");
	}
	lua_pushinteger(l, value);
	return 1;
}

static int encoder_setInbandFEC(lua_State *l) {
	OpusEncoder *encoder = luaL_checkudata(l, 1, METATABLE_ENCODER);
	opus_int32 value = luaL_checkinteger(l, 2);
	if (opus_encoder_ctl(encoder, OPUS_SET_INBAND_FEC(value)) != OPUS_OK) {
		return luaL_error(l, "Failed to set inband FEC");
	}
	return 0;
}

static int encoder_getPacketLossPerc(lua_State *l) {
	OpusEncoder *encoder = luaL_checkudata(l, 1, METATABLE_ENCODER);
	opus_int32 value;
	if (opus_encoder_ctl(encoder, OPUS_GET_PACKET_LOSS_PERC(&value)) != OPUS_OK) {
		return luaL_error(l, "Failed to get packet loss percentage");
	}
	lua_pushinteger(l, value);
	return 1;
}

static int encoder_setPacketLossPerc(lua_State *l) {
	OpusEncoder *encoder = luaL_checkudata(l, 1, METATABLE_ENCODER);
	opus_int32 value = luaL_checkinteger(l, 2);
	if (opus_encoder_ctl(encoder, OPUS_SET_PACKET_LOSS_PERC(value)) != OPUS_OK) {
		return luaL_error(l, "Failed to set packet loss percentage");
	}
	return 0;
}

static int encoder_getDTX(lua_State *l) {
	OpusEncoder *encoder = luaL_checkudata(l, 1, METATABLE_ENCODER);
	opus_int32 value;
	if (opus_encoder_ctl(encoder, OPUS_GET_DTX(&value)) != OPUS_OK) {
		return luaL_error(l, "Failed to get DTX");
	}
	lua_pushinteger(l, value);
	return 1;
}

static int encoder_setDTX(lua_State *l) {
	OpusEncoder *encoder = luaL_checkudata(l, 1, METATABLE_ENCODER);
	opus_int32 value = luaL_checkinteger(l, 2);
	if (opus_encoder_ctl(encoder, OPUS_SET_DTX(value)) != OPUS_OK) {
		return luaL_error(l, "Failed to set DTX");
	}
	return 0;
}

/* ENCODE */

static int encoder_encode(lua_State *l) {
	OpusEncoder *encoder = luaL_checkudata(l, 1, METATABLE_ENCODER);
	int frames = luaL_checkinteger(l, 2);

	size_t len;
	const opus_int16* pcm = (opus_int16*) luaL_checklstring(l, 3, &len);

	const uint32_t enc_frame_size = frames * AUDIO_SAMPLE_RATE / 100;

	unsigned char output[0x1FFF];
	opus_int32 outlen = opus_encode(encoder, pcm, enc_frame_size, output, len);

	lua_pushlstring(l, (char*) output, outlen);
	return 1;
}

static int encoder_encode_float(lua_State *l) {
	OpusEncoder *encoder = luaL_checkudata(l, 1, METATABLE_ENCODER);
	int frames = luaL_checkinteger(l, 2);

	size_t len;
	const float* pcm = (float*) luaL_checklstring(l, 3, &len);

	const uint32_t enc_frame_size = frames * AUDIO_SAMPLE_RATE / 100;

	unsigned char output[0x1FFF];
	opus_int32 outlen = opus_encode_float(encoder, pcm, enc_frame_size, output, len);

	lua_pushlstring(l, (char*) output, outlen);
	return 1;
}

/* METAMETHODS */

static int encoder_tostring(lua_State *l) {
	lua_pushfstring(l, "%s: %p", METATABLE_ENCODER, lua_topointer(l, 1));
	return 1;
}

static int encoder_gc(lua_State *l) {
	OpusEncoder *encoder = luaL_checkudata(l, 1, METATABLE_ENCODER);
	mumble_log(LOG_DEBUG, "%s: %p garbage collected", METATABLE_ENCODER, encoder);
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