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

#if defined(LUA_VERSION_NUM) && LUA_VERSION_NUM >= 502
#define lua_objlen lua_rawlen
#endif

#define lua_stackguard_entry(L) int __lua_stackguard_entry=lua_gettop(L);
#define lua_stackguard_exit(L) assert(__lua_stackguard_entry == lua_gettop(L));

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

// Enables UDP packets
#define ENABLE_UDP

#define STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"

#define MODULE_NAME "lua-mumble"

#define MUMBLE_VER_MAJOR	1
#define MUMBLE_VER_MINOR	4
#define MUMBLE_VER_REVISION	0

// The default audio quality the encoder will try to use.
// If the servers maximum bandwidth doesn't allow for such
// a high value, it will try to auto ajust.
#define AUDIO_DEFAULT_BITRATE 96000

// Number of frames to send per packet
// Allowed values (10, 20 40, 60)
// 10 = Lower latency, 40 = Better quality
#define AUDIO_DEFAULT_FRAMES 40

// How many channels the ogg file playback should handle
#define AUDIO_PLAYBACK_CHANNELS 2

// The sample rate in which all ogg files should be encoded to
#define AUDIO_SAMPLE_RATE 48000

// Number of audio "streams" for playing multiple sounds at once
#define AUDIO_MAX_STREAMS 128

// Maximum length an encoded opus packet can be
#define AUDIO_MAX_FRAME_SIZE 6*960

// The size of the PCM buffer
// Technically, it should be FRAMESIZE*SAMPLERATE/100
#define PCM_BUFFER 4096

#define PAYLOAD_SIZE_MAX (1024 * 8 - 1)

#define PING_TIMEOUT 30

#define UDP_BUFFER_MAX 1024

#define UDP_CELT_ALPHA 0
#define UDP_PING 1
#define UDP_SPEEX 2
#define UDP_CELT_BETA 3
#define UDP_OPUS 4

/*
 * Structures
 */

typedef struct MumbleClient MumbleClient;
typedef struct AudioStream AudioStream;
typedef struct AudioFrame AudioFrame;
typedef struct MumbleChannel MumbleChannel;
typedef struct MumbleUser MumbleUser;
typedef struct LinkNode LinkNode;
typedef struct my_io my_io;
typedef struct my_timer my_timer;
typedef struct lua_timer lua_timer;
typedef struct my_signal my_signal;
typedef struct mumble_crypt mumble_crypt;

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

struct AudioFrame {
	float l;
	float r;
};

struct AudioStream {
	lua_State *lua;
	stb_vorbis *ogg;
	MumbleClient *client;
	bool playing;
	float volume;
	AudioFrame buffer[PCM_BUFFER];
	stb_vorbis_info info;
	int stream;
	int loop_count;
	bool looping;
};

struct MumbleClient {
	int					self;
	int					socket_tcp;
	int					socket_udp;
	struct addrinfo*	server_host_udp;
	SSL_CTX				*ssl_context;
	SSL					*ssl;
	bool				connected;
	bool				synced;
	const char*			host;
	uint16_t			port;
	int					hooks;
	int					users;
	int					channels;
	int					audio_streams;
	double				time;
	uint32_t			session;
	float				volume;
	my_io				socket_tcp_io;
	my_io				socket_udp_io;
	my_timer			audio_timer;
	my_timer			ping_timer;
	my_signal			signal;
	AudioFrame			audio_buffer[PCM_BUFFER];
	AudioFrame			audio_rebuffer[PCM_BUFFER];
	uint32_t			audio_sequence;
	AudioStream*		audio_jobs[AUDIO_MAX_STREAMS];
	int					audio_frames;
	OpusEncoder*		encoder;
	int					encoder_ref;
	uint8_t				audio_target;

	uint32_t			tcp_packets;
	float				tcp_ping_avg;
	float				tcp_ping_var;

	bool				udp_tunnel;

	uint8_t				udp_ping_acc;
	uint32_t			udp_packets;
	float				udp_ping_avg;
	float				udp_ping_var;

	uint32_t			resync;
	mumble_crypt*		crypt;

	LinkNode*			session_list;
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
	uint32_t		permissions;
};

void list_add(LinkNode** head_ref, uint32_t value);
void list_remove(LinkNode **head_ref, uint32_t value);
void list_clear(LinkNode** head_ref);
size_t list_count(LinkNode** head_ref);

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
	LinkNode*		listens;
};

typedef struct {
	uint16_t type;
	uint32_t length;
	uint8_t buffer[PAYLOAD_SIZE_MAX + 6];
} Packet;

typedef struct {
	uint8_t *buffer;
	int length;
	int header_length;
} VoicePacket;

typedef struct {
	const char*	data_id;
	size_t	data_id_len;
	ProtobufCBinaryData cbdata;
	LinkNode* session_list;
} PluginData;

/*--------------------------------
	UTIL FUNCTIONS
--------------------------------*/

extern int MUMBLE_CLIENTS;

extern double gettime(clockid_t mode);

extern void bin_to_strhex(char *bin, size_t binsz, char **result);

extern void debugstack(lua_State *l, const char* text);
extern int luaL_typerror(lua_State *L, int narg, const char *tname);
extern int luaL_checkboolean(lua_State *L, int i);
extern int luaL_optboolean(lua_State *L, int i, int d);
extern int luaL_isudata(lua_State *L, int ud, const char *tname);
extern const char* eztype(lua_State *L, int i);

extern uint64_t util_get_varint(uint8_t buffer[], int *len);

extern void mumble_ping_udp(lua_State* l, MumbleClient* client);

extern void mumble_create_audio_timer(MumbleClient *client, int bitspersec);
extern void mumble_disconnect(lua_State* l, MumbleClient *client, const char* reason);

extern void mumble_client_raw_get(lua_State* l, MumbleClient* client);
extern MumbleUser* mumble_user_get(lua_State* l, MumbleClient* client, uint32_t session);
extern void mumble_user_raw_get(lua_State* l, MumbleClient* client, uint32_t session);
extern void mumble_user_remove(lua_State* l, MumbleClient* client, uint32_t session);

extern MumbleChannel* mumble_channel_get(lua_State* l, MumbleClient* client, uint32_t channel_id);
extern MumbleChannel* mumble_channel_raw_get(lua_State* l, MumbleClient* client, uint32_t channel_id);
extern void mumble_channel_remove(lua_State* l, MumbleClient* client, uint32_t channel_id);

extern int mumble_push_address(lua_State* l, ProtobufCBinaryData address);
extern int mumble_handle_speaking_hooks(lua_State* l, MumbleClient* client, uint8_t buffer[], uint8_t codec, uint8_t target, uint32_t session);

extern int mumble_traceback(lua_State *l);
extern void mumble_hook_call(lua_State* l, MumbleClient *client, const char* hook, int nargs);