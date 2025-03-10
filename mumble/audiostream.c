#include "mumble.h"

#include "audio.h"
#include "audiostream.h"
#include "util.h"
#include "log.h"

#include <math.h>

static int audiostream_isPlaying(lua_State *l) {
	AudioStream *sound = luaL_checkudata(l, 1, METATABLE_AUDIOSTREAM);
	lua_pushboolean(l, sound->playing);
	return 1;
}

static int audiostream_setVolume(lua_State *l) {
	AudioStream *sound = luaL_checkudata(l, 1, METATABLE_AUDIOSTREAM);
	sound->volume = luaL_checknumber(l, 2);
	lua_pushvalue(l, 1);
	return 1;
}

static int audiostream_getVolume(lua_State *l) {
	AudioStream *sound = luaL_checkudata(l, 1, METATABLE_AUDIOSTREAM);
	lua_pushnumber(l, sound->volume);
	return 1;
}

static int audiostream_pause(lua_State *l) {
	AudioStream *sound = luaL_checkudata(l, 1, METATABLE_AUDIOSTREAM);
	sound->playing = false;
	return 0;
}

static int audiostream_play(lua_State *l) {
	AudioStream *sound = luaL_checkudata(l, 1, METATABLE_AUDIOSTREAM);
	if (!sound->playing) {
		sound->playing = true;

		if (sound->refrence <= LUA_NOREF) {
			// Push a copy of the audio stream and save a reference
			lua_pushvalue(l, 1);
			audio_transmission_reference(l, sound);
		}
	} else {
		sf_seek(sound->file, 0, SEEK_SET);
	}
	return 0;
}

static int audiostream_stop(lua_State *l) {
	AudioStream *sound = luaL_checkudata(l, 1, METATABLE_AUDIOSTREAM);
	if (sound->playing) {
		audio_transmission_unreference(l, sound);
	}
	return 0;
}

static int audiostream_seek(lua_State *l) {
	AudioStream *sound = luaL_checkudata(l, 1, METATABLE_AUDIOSTREAM);

	enum what {SET, CUR, END};
	static const char * op[] = {"set", "cur", "end", NULL};

	int option = luaL_checkoption(l, 2, "cur", op);
	long offset = luaL_optlong(l, 3, 0);

	sf_count_t position = 0;

	switch (option) {
	case SET:
		position = sf_seek(sound->file, offset, SEEK_SET);
		break;
	case CUR:
		position = sf_seek(sound->file, offset, SEEK_CUR);
		break;
	case END:
		position = sf_seek(sound->file, offset, SEEK_END);
		break;
	}

	lua_pushinteger(l, position);
	return 1;
}

static int audiostream_getLength(lua_State *l) {
	AudioStream *sound = luaL_checkudata(l, 1, METATABLE_AUDIOSTREAM);

	enum what {SAMPLES, FRAMES, SECONDS};
	static const char * op[] = {"samples", "frames", "seconds", NULL};

	switch (luaL_checkoption(l, 2, NULL, op)) {
	case SAMPLES: {
		lua_pushinteger(l, sound->info.frames * sound->info.channels);
		return 1;
	}
	case FRAMES: {
		lua_pushinteger(l, sound->info.frames);
		return 1;
	}
	case SECONDS: {
		lua_pushnumber(l, (double) sound->info.frames / sound->info.samplerate);
		return 1;
	}
	}

	return 0;
}

static int audiostream_getInfo(lua_State *l) {
	AudioStream *sound = luaL_checkudata(l, 1, METATABLE_AUDIOSTREAM);

	lua_newtable(l);
	{
		lua_pushinteger(l, sound->info.frames);
		lua_setfield(l, -2, "frames");
		lua_pushinteger(l, sound->info.samplerate);
		lua_setfield(l, -2, "samplerate");
		lua_pushinteger(l, sound->info.channels);
		lua_setfield(l, -2, "channels");
		lua_pushinteger(l, sound->info.format);
		lua_setfield(l, -2, "format");
		lua_pushinteger(l, sound->info.sections);
		lua_setfield(l, -2, "sections");
		lua_pushinteger(l, sound->info.seekable);
		lua_setfield(l, -2, "seekable");
	}
	return 1;
}

static int audiostream_getTitle(lua_State *l) {
	AudioStream *sound = luaL_checkudata(l, 1, METATABLE_AUDIOSTREAM);
	lua_pushstring(l, sf_get_string(sound->file, SF_STR_TITLE));
	return 1;
}

static int audiostream_getArtist(lua_State *l) {
	AudioStream *sound = luaL_checkudata(l, 1, METATABLE_AUDIOSTREAM);
	lua_pushstring(l, sf_get_string(sound->file, SF_STR_ARTIST));
	return 1;
}

static int audiostream_getCopyright(lua_State *l) {
	AudioStream *sound = luaL_checkudata(l, 1, METATABLE_AUDIOSTREAM);
	lua_pushstring(l, sf_get_string(sound->file, SF_STR_COPYRIGHT));
	return 1;
}

static int audiostream_getSoftware(lua_State *l) {
	AudioStream *sound = luaL_checkudata(l, 1, METATABLE_AUDIOSTREAM);
	lua_pushstring(l, sf_get_string(sound->file, SF_STR_SOFTWARE));
	return 1;
}

static int audiostream_getComments(lua_State *l) {
	AudioStream *sound = luaL_checkudata(l, 1, METATABLE_AUDIOSTREAM);
	lua_pushstring(l, sf_get_string(sound->file, SF_STR_COMMENT));
	return 1;
}

static int audiostream_setLooping(lua_State *l) {
	AudioStream *sound = luaL_checkudata(l, 1, METATABLE_AUDIOSTREAM);

	const char *msg = NULL;

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
		msg = lua_pushfstring(l, "%s or %s expected, got %s",
		                      lua_typename(l, LUA_TNUMBER), lua_typename(l, LUA_TBOOLEAN), luaL_typename(l, 2));
		return luaL_argerror(l, 2, msg);
	}

	lua_pushvalue(l, 1);
	return 1;
}

static int audiostream_isLooping(lua_State *l) {
	AudioStream *sound = luaL_checkudata(l, 1, METATABLE_AUDIOSTREAM);
	lua_pushboolean(l, sound->looping || sound->loop_count > 0);
	return 1;
}

static int audiostream_getLoopCount(lua_State *l) {
	AudioStream *sound = luaL_checkudata(l, 1, METATABLE_AUDIOSTREAM);
	if (sound->looping) {
		// push math.huge (inf)
		lua_pushnumber(l, HUGE_VAL);
	} else {
		lua_pushinteger(l, sound->loop_count);
	}
	return 1;
}

static int audiostream_fadeTo(lua_State *l) {
	AudioStream *sound = luaL_checkudata(l, 1, METATABLE_AUDIOSTREAM);
	float volume = luaL_checknumber(l, 2);
	float time = luaL_optnumber(l, 3, 1);
	sound->fade_frames = AUDIO_SAMPLE_RATE * time;
	sound->fade_frames_left = sound->fade_frames;
	sound->fade_from_volume = sound->fade_volume;
	sound->fade_to_volume = volume;
	return 0;
}

static int audiostream_fadeOut(lua_State *l) {
	AudioStream *sound = luaL_checkudata(l, 1, METATABLE_AUDIOSTREAM);
	float time = luaL_optnumber(l, 2, 1);
	sound->fade_frames = AUDIO_SAMPLE_RATE * time;
	sound->fade_frames_left = sound->fade_frames;
	sound->fade_from_volume = sound->fade_volume;
	sound->fade_to_volume = 0;
	sound->fade_stop = true;
	return 0;
}

static int audiostream_gc(lua_State *l) {
	AudioStream *sound = luaL_checkudata(l, 1, METATABLE_AUDIOSTREAM);
	mumble_log(LOG_DEBUG, "%s: %p garbage collected", METATABLE_AUDIOSTREAM, sound);
	if (sound->file) {
		sf_close(sound->file);
		sound->file = NULL;
	}
	if (sound->buffer) {
		free(sound->buffer);
	}
	if (sound->src_state) {
		src_delete(sound->src_state);
	}
	uv_mutex_destroy(&sound->mutex);
	return 0;
}

static int audiostream_tostring(lua_State *l) {
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
	{"getTitle", audiostream_getTitle},
	{"getArtist", audiostream_getArtist},
	{"getCopyright", audiostream_getCopyright},
	{"getSoftware", audiostream_getSoftware},
	{"getComments", audiostream_getComments},
	{"setLooping", audiostream_setLooping},
	{"isLooping", audiostream_isLooping},
	{"getLoopCount", audiostream_getLoopCount},
	{"fadeTo", audiostream_fadeTo},
	{"fadeOut", audiostream_fadeOut},
	{"__gc", audiostream_gc},
	{"__tostring", audiostream_tostring},
	{NULL, NULL}
};
