#include "audio/audiostream.h"

#include "client/client.h"
#include "defines.h"
#include "lua/bindings.h"
#include "lua/luainc.h"
#include "lua/luautil.h"
#include "lua/metatables.h"
#include "util/log.h"

#include <cmath>
#include <cstdlib>
#include <cstring>

static int audiostream_isPlaying(lua_State *l) {
	AudioStream *sound = luaL_checkclass<AudioStream>(l, 1, METATABLE_AUDIOSTREAM);
	lua_pushboolean(l, sound->playing);
	return 1;
}

static int audiostream_setVolume(lua_State *l) {
	AudioStream *sound = luaL_checkclass<AudioStream>(l, 1, METATABLE_AUDIOSTREAM);
	sound->volume = luaL_checknumber(l, 2);
	lua_pushvalue(l, 1);
	return 1;
}

static int audiostream_getVolume(lua_State *l) {
	AudioStream *sound = luaL_checkclass<AudioStream>(l, 1, METATABLE_AUDIOSTREAM);
	lua_pushnumber(l, sound->volume);
	return 1;
}

static int audiostream_pause(lua_State *l) {
	AudioStream *sound = luaL_checkclass<AudioStream>(l, 1, METATABLE_AUDIOSTREAM);
	sound->pause();
	return 0;
}

static int audiostream_play(lua_State *l) {
	AudioStream *sound = luaL_checkclass<AudioStream>(l, 1, METATABLE_AUDIOSTREAM);
	sound->play(l, 1);
	return 0;
}

static int audiostream_stop(lua_State *l) {
	AudioStream *sound = luaL_checkclass<AudioStream>(l, 1, METATABLE_AUDIOSTREAM);
	sound->stop(l);
	return 0;
}

// File only methods return nil for streams playing a command's output
#define audiostream_checkfile(sound) \
	if (!(sound)->file) { \
		lua_pushnil(l); \
		return 1; \
	}

static int audiostream_seek(lua_State *l) {
	AudioStream *sound = luaL_checkclass<AudioStream>(l, 1, METATABLE_AUDIOSTREAM);
	audiostream_checkfile(sound);
	static const char * const options[] = {"set", "cur", "end", NULL};
	static const int whence[] = {SEEK_SET, SEEK_CUR, SEEK_END};

	int option = luaL_checkoption(l, 2, "cur", options);
	long offset = luaL_optlong(l, 3, 0);
	lua_pushinteger(l, sound->seek(offset, whence[option]));
	return 1;
}

static int audiostream_getLength(lua_State *l) {
	AudioStream *sound = luaL_checkclass<AudioStream>(l, 1, METATABLE_AUDIOSTREAM);
	audiostream_checkfile(sound);

	enum what {SAMPLES, FRAMES, SECONDS};
	static const char * const op[] = {"samples", "frames", "seconds", NULL};

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
	AudioStream *sound = luaL_checkclass<AudioStream>(l, 1, METATABLE_AUDIOSTREAM);
	audiostream_checkfile(sound);

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
	AudioStream *sound = luaL_checkclass<AudioStream>(l, 1, METATABLE_AUDIOSTREAM);
	audiostream_checkfile(sound);
	lua_pushstring(l, sf_get_string(sound->file, SF_STR_TITLE));
	return 1;
}

static int audiostream_getArtist(lua_State *l) {
	AudioStream *sound = luaL_checkclass<AudioStream>(l, 1, METATABLE_AUDIOSTREAM);
	audiostream_checkfile(sound);
	lua_pushstring(l, sf_get_string(sound->file, SF_STR_ARTIST));
	return 1;
}

static int audiostream_getCopyright(lua_State *l) {
	AudioStream *sound = luaL_checkclass<AudioStream>(l, 1, METATABLE_AUDIOSTREAM);
	audiostream_checkfile(sound);
	lua_pushstring(l, sf_get_string(sound->file, SF_STR_COPYRIGHT));
	return 1;
}

static int audiostream_getSoftware(lua_State *l) {
	AudioStream *sound = luaL_checkclass<AudioStream>(l, 1, METATABLE_AUDIOSTREAM);
	audiostream_checkfile(sound);
	lua_pushstring(l, sf_get_string(sound->file, SF_STR_SOFTWARE));
	return 1;
}

static int audiostream_getComments(lua_State *l) {
	AudioStream *sound = luaL_checkclass<AudioStream>(l, 1, METATABLE_AUDIOSTREAM);
	audiostream_checkfile(sound);
	lua_pushstring(l, sf_get_string(sound->file, SF_STR_COMMENT));
	return 1;
}

static int audiostream_setLooping(lua_State *l) {
	AudioStream *sound = luaL_checkclass<AudioStream>(l, 1, METATABLE_AUDIOSTREAM);
	if (sound->isProcess()) {
		return luaL_error(l, "a command's output can't loop");
	}
	switch (lua_type(l, 2)) {
	case LUA_TNUMBER:
		sound->setLoopCount(luaL_checkinteger(l, 2));
		break;
	case LUA_TBOOLEAN:
		sound->setLooping(luaL_checkboolean(l, 2));
		break;
	default: {
		const char *msg = lua_pushfstring(l, "%s or %s expected, got %s",
		                                  lua_typename(l, LUA_TNUMBER), lua_typename(l, LUA_TBOOLEAN), luaL_typename(l, 2));
		return luaL_argerror(l, 2, msg);
	}
	}

	lua_pushvalue(l, 1);
	return 1;
}

static int audiostream_isLooping(lua_State *l) {
	AudioStream *sound = luaL_checkclass<AudioStream>(l, 1, METATABLE_AUDIOSTREAM);
	lua_pushboolean(l, sound->isLooping());
	return 1;
}

static int audiostream_getLoopCount(lua_State *l) {
	AudioStream *sound = luaL_checkclass<AudioStream>(l, 1, METATABLE_AUDIOSTREAM);
	if (sound->looping) {
		// push math.huge (inf)
		lua_pushnumber(l, HUGE_VAL);
	} else {
		lua_pushinteger(l, sound->loop_count);
	}
	return 1;
}

static int audiostream_fadeTo(lua_State *l) {
	AudioStream *sound = luaL_checkclass<AudioStream>(l, 1, METATABLE_AUDIOSTREAM);
	sound->fadeTo(luaL_checknumber(l, 2), luaL_optnumber(l, 3, 1));
	return 0;
}

static int audiostream_fadeOut(lua_State *l) {
	AudioStream *sound = luaL_checkclass<AudioStream>(l, 1, METATABLE_AUDIOSTREAM);
	sound->fadeOut(luaL_optnumber(l, 2, 1));
	return 0;
}

static int audiostream_gc(lua_State *l) {
	AudioStream *sound = luaL_checkclass<AudioStream>(l, 1, METATABLE_AUDIOSTREAM);
	mumble_log(LOG_DEBUG, "%s: %p garbage collected", METATABLE_AUDIOSTREAM, sound);
	sound->~AudioStream();
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
