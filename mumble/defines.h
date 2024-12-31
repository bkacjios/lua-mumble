#pragma once

#define MODULE_NAME "lua-mumble"

// You can change this to simulate older clients.
// If you change the version to be less than 1.5, we will fallback into a legacy messaging mode.
// In legacy mode, UDP pings and audio data are handled differently.
#define MUMBLE_VERSION_MAJOR	(uint64_t) 1
#define MUMBLE_VERSION_MINOR	(uint64_t) 5
#define MUMBLE_VERSION_PATCH 	(uint64_t) 735

#define MUMBLE_VERSION_V1 MUMBLE_VERSION_MAJOR << 16 | MUMBLE_VERSION_MINOR << 8 | MUMBLE_VERSION_PATCH
#define MUMBLE_VERSION_V2 MUMBLE_VERSION_MAJOR << 48 | MUMBLE_VERSION_MINOR << 32 | MUMBLE_VERSION_PATCH << 16

#define CLIENT_TYPE_USER 0
#define CLIENT_TYPE_BOT  1

// The default audio quality the encoder will try to use.
// If the servers maximum bandwidth doesn't allow for such
// a high value, it will try to auto ajust.
#define AUDIO_DEFAULT_BITRATE 128000

// Number of frames to send per packet
// Allowed values (10, 20, 40, 60)
#define AUDIO_FRAME_SIZE_TINY	10
#define AUDIO_FRAME_SIZE_SMALL	20
#define AUDIO_FRAME_SIZE_MEDIUM	40
#define AUDIO_FRAME_SIZE_LARGE	60

// 10 = Lower latency, 60 = Better quality
#define AUDIO_DEFAULT_FRAMES AUDIO_FRAME_SIZE_MEDIUM

// How many channels the audio file playback should handle
#define AUDIO_PLAYBACK_CHANNELS 2

// The sample rate in which all audio files should be encoded to
#define AUDIO_SAMPLE_RATE 48000

// The max amount of PCM frames we will ever have
#define MAX_PCM_FRAMES AUDIO_FRAME_SIZE_LARGE * AUDIO_SAMPLE_RATE / 1000
// The max buffer size we will ever need for handling raw bytes
// FRAMESIZE * SAMPLERATE * CHANNELS / 1000
#define PCM_BUFFER MAX_PCM_FRAMES * AUDIO_PLAYBACK_CHANNELS

#define PAYLOAD_SIZE_MAX (1024 * 8 - 1)

#define PING_TIME 30000

// How many dropped UDP pings will result in falling back to TCP tunnel
#define UDP_TCP_FALLBACK 2

#define UDP_BUFFER_MAX 1024

#define LEGACY_UDP_CELT_ALPHA 0
#define LEGACY_PROTO_UDP_PING 1
#define LEGACY_UDP_SPEEX 2
#define LEGACY_UDP_CELT_BETA 3
#define LEGACY_UDP_OPUS 4

#define PROTO_UDP_AUDIO 0
#define PROTO_UDP_PING 1

#define LOG_INFO 1
#define LOG_WARN 2
#define LOG_ERROR 3
#define LOG_DEBUG 4
#define LOG_TRACE 5

#ifdef DEBUG
#define LOG_LEVEL LOG_DEBUG
#else
#define LOG_LEVEL LOG_ERROR
#endif

#define MUMBLE_UNREFERENCED 0

#define LUA_COMPAT_MODULE
#define LUA_COMPAT_5_1

#if defined(LUA_VERSION_NUM) && LUA_VERSION_NUM == 502 && !defined(LUAJIT)
#define lua_objlen lua_rawlen
#endif

#define lua_stackguard_entry(L) int __lua_stackguard_entry = lua_gettop(L);
#define lua_stackguard_exit(L) assert(__lua_stackguard_entry == lua_gettop(L));

#ifdef _WIN32
#define NEWLINE "\r\n"
#else
#define NEWLINE "\n"
#endif
