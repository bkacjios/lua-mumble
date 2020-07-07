#pragma once

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <errno.h>

#include <ev.h>

#define LUA_COMPAT_MODULE
#define LUA_COMPAT_5_1

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

#include <signal.h>
#include <netdb.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/utsname.h>
#include <sys/time.h>

#include <netinet/ip.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>

#include <openssl/ssl.h>

#include <math.h>

#include <opus/opus.h>

#include "proto/Mumble.pb-c.h"

#define STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"

#define MODULE_NAME "lua-mumble"
#define MODULE_VERSION "0.0.1"

// The default audio quality the encoder will try to use.
// If the servers maximum bandwidth doesn't allow for such
// a high value, it will try to auto ajust.
#define AUDIO_DEFAULT_BITRATE 96000

// Number of frames to send per packet
// 1 = Lower latency, 4 = Better quality
#define AUDIO_DEFAULT_FRAMES 2

// The sample rate in which all ogg files should be encoded to
#define AUDIO_SAMPLE_RATE 48000

// Number of audio "channels" for playing multiple sounds at once
#define AUDIO_MAX_CHANNELS 128

// Maximum length an encoded opus packet can be
#define AUDIO_MAX_FRAME_SIZE 6*960

// The size of the PCM buffer
// Technically, it should be FRAMESIZE*SAMPLERATE/100
#define PCM_BUFFER 4096

#define PAYLOAD_SIZE_MAX (1024 * 8 - 1)

#define PING_TIMEOUT 30

#define UDP_CELT_ALPHA 0
#define UDP_PING 1
#define UDP_SPEEX 2
#define UDP_CELT_BETA 3
#define UDP_OPUS 4

/*
 * Structures
 */

typedef struct MumbleClient MumbleClient;
typedef struct AudioTransmission AudioTransmission;
typedef struct MumbleChannel MumbleChannel;
typedef struct MumbleUser MumbleUser;
typedef struct LinkNode LinkNode;
typedef struct my_io my_io;
typedef struct my_timer my_timer;
typedef struct lua_timer lua_timer;
typedef struct my_signal my_signal;

struct my_io {
	ev_io io;
	MumbleClient* client;
	lua_State* l;
};

struct my_timer {
	ev_timer timer;
	MumbleClient* client;
	lua_State* l;
};

struct lua_timer {
	ev_timer timer;
	lua_State* l;
	int self;
	int callback;
};

struct my_signal {
	ev_signal signal;
	MumbleClient* client;
	lua_State* l;
};

struct MumbleClient {
	int					self;
	int					socket;
	SSL_CTX				*ssl_context;
	SSL					*ssl;
	bool				connected;
	bool				synced;
	const char*			host;
	uint16_t			port;
	const char*			username;
	const char*			password;
	int					hooks;
	int					users;
	int					channels;
	double				time;
	uint32_t			session;
	float				volume;
	my_io				socket_io;
	my_timer			audio_timer;
	my_timer			ping_timer;
	my_signal           signal;
	float				audio_buffer[PCM_BUFFER];
	float				audio_rebuffer[PCM_BUFFER];
	uint32_t			audio_sequence;
	AudioTransmission*	audio_jobs[AUDIO_MAX_CHANNELS];
	int					audio_frames;
	OpusEncoder*		encoder;
	uint8_t				audio_target;

	uint32_t	tcp_packets;
	double		tcp_ping_total;
	float		tcp_ping_avg;
	float		tcp_ping_var; 
};

struct LinkNode
{
	uint32_t data;
	struct LinkNode	*next;
};

struct MumbleChannel {
	MumbleClient*	client;
	int				data;
	char*			name;
	uint32_t		channel_id;
	uint32_t		parent;
	char*			description;
	char*			description_hash;
	size_t			description_hash_len;
	bool			temporary;
	int32_t			position;
	uint32_t		max_users;
	LinkNode*		links;
	bool			is_enter_restricted;
	bool			can_enter;
	uint32_t        permissions;
};

void list_add(LinkNode** head_ref, uint32_t value);
void list_remove(LinkNode **head_ref, uint32_t value);
void list_clear(LinkNode** head_ref);

struct MumbleUser
{
	MumbleClient*	client;
	int				data;
	bool			connected;
	uint32_t		session;
	uint32_t		user_id;
	char*			name;
	uint32_t		channel_id;
	bool			mute;
	bool			deaf;
	bool			self_mute;
	bool			self_deaf;
	bool			suppress;
	char*			comment;
	char*			comment_hash;
	size_t			comment_hash_len;
	bool			speaking;
	bool			recording;
	bool			priority_speaker;
	char*			texture;
	char*			texture_hash;
	size_t			texture_hash_len;
	char*			hash;
};

typedef struct {
	uint16_t type;
	uint32_t length;
	uint8_t buffer[PAYLOAD_SIZE_MAX + 6];
} Packet;

struct AudioTransmission {
	lua_State *lua;
	stb_vorbis *ogg;
	MumbleClient *client;
	bool playing;
	float volume;
	float buffer[PCM_BUFFER];
	stb_vorbis_info info;
};

typedef struct {
	uint8_t *buffer;
	int length;
	int header_length;
} VoicePacket;

/*--------------------------------
	UTIL FUNCTIONS
--------------------------------*/

extern int MUMBLE_CONNECTIONS;

extern double gettime();

extern void bin_to_strhex(char *bin, size_t binsz, char **result);

extern void debugstack(lua_State *l, const char* text);
extern int luaL_typerror(lua_State *L, int narg, const char *tname);
extern int luaL_checkboolean(lua_State *L, int i);
extern int luaL_optboolean(lua_State *L, int i, int d);
extern const char* eztype(lua_State *L, int i);

extern int64_t util_get_varint(uint8_t buffer[], int *len);

extern void mumble_create_audio_timer(MumbleClient *client, int bitspersec);
extern void mumble_disconnect(lua_State* l, MumbleClient *client);

extern void mumble_client_raw_get(lua_State* l, MumbleClient* client);
extern MumbleUser* mumble_user_get(lua_State* l, MumbleClient* client, uint32_t session);
extern void mumble_user_raw_get(lua_State* l, MumbleClient* client, uint32_t session);
extern void mumble_user_remove(lua_State* l, MumbleClient* client, uint32_t session);

extern MumbleChannel* mumble_channel_get(lua_State* l, MumbleClient* client, uint32_t channel_id);
extern MumbleChannel* mumble_channel_raw_get(lua_State* l, MumbleClient* client, uint32_t channel_id);
extern void mumble_channel_remove(lua_State* l, MumbleClient* client, uint32_t channel_id);

extern int mumble_traceback(lua_State *l);
extern void mumble_hook_call(lua_State* l, MumbleClient *client, const char* hook, int nargs);

extern void audio_transmission_event(lua_State* l, MumbleClient *client);
extern void audio_transmission_stop(AudioTransmission* sound);