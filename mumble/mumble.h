#pragma once

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <errno.h>

#include <uv.h>

#define LUA_COMPAT_MODULE
#define LUA_COMPAT_5_1

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

#if defined(LUA_VERSION_NUM) && LUA_VERSION_NUM == 502 && !defined(LUAJIT)
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
#include "proto/MumbleUDP.pb-c.h"

#include <sndfile.h>

#define MODULE_NAME "lua-mumble"

// You can change this to simulate older clients.
// If you change the MINOR version to be less than 5,
// we will fallback into a legacy messaging mode.
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

// How many channels the ogg file playback should handle
#define AUDIO_PLAYBACK_CHANNELS 2

// The sample rate in which all ogg files should be encoded to
#define AUDIO_SAMPLE_RATE 48000

// The size of the PCM buffer
// Technically, it should be FRAMESIZE*SAMPLERATE/1000
#define PCM_BUFFER 4096

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

/*
 * Structures
 */

typedef struct MumbleClient MumbleClient;
typedef struct AudioStream AudioStream;
typedef struct AudioFrame AudioFrame;
typedef struct MumbleChannel MumbleChannel;
typedef struct MumbleUser MumbleUser;
typedef struct LinkNode LinkNode;
typedef struct MumbleTimer MumbleTimer;
typedef struct mumble_crypt mumble_crypt;
typedef struct thread_io thread_io;
typedef struct MumbleThread MumbleThread;
typedef struct AudioTimer AudioTimer;

struct MumbleTimer {
	uv_timer_t timer;
	lua_State* l;
	bool running;
	uint32_t count;
	int self;
	int callback;
	uint64_t after;
	uint64_t repeat;
};

struct AudioFrame {
	float l;
	float r;
};

struct AudioStream {
	lua_State *lua;
	SNDFILE *file;
	MumbleClient *client;
	bool closed;
	bool playing;
	float volume;
	SF_INFO info;
	int refrence;
	int loop_count;
	bool looping;
};

struct thread_io {
	// ev_io io;
	lua_State* l;
	MumbleThread* thread;
};

struct MumbleThread {
    pthread_t pthread;
	const char *filename;
	int self;
	int finished;
	pthread_mutex_t lock;
	int pipe[2];
	thread_io event;
};

struct MumbleClient {
	lua_State*			l;
	int					self;
	uv_connect_t		tcp_connect_req;
	uv_tcp_t			socket_tcp;
	uv_os_fd_t			socket_tcp_fd;
	uv_udp_t			socket_udp;
	uv_poll_t			ssl_poll;
	BIO*				ssl_bio_read;
	BIO*				ssl_bio_write;
	struct addrinfo*	server_host_udp;
	struct addrinfo*	server_host_tcp;
	SSL_CTX				*ssl_context;
	SSL					*ssl;
	bool				connecting;
	bool				connected;
	bool				synced;
	bool				legacy;
	const char*			host;
	uint16_t			port;
	int					hooks;
	int					users;
	int					channels;
	int					audio_streams;
	uint32_t			max_bandwidth;
	double				time;
	uint32_t			session;
	float				volume;
	uv_timer_t			audio_timer;
	uv_timer_t			ping_timer;
	AudioFrame			audio_output[PCM_BUFFER];
	uint32_t			audio_sequence;
	uint32_t			audio_frames;
	OpusEncoder*		encoder;
	int					encoder_ref;
	uint8_t				audio_target;

	uint32_t			tcp_packets;
	double				tcp_ping_avg;
	double				tcp_ping_var;

	bool				tcp_udp_tunnel;

	uint8_t				udp_ping_acc;
	uint32_t			udp_packets;
	double				udp_ping_avg;
	double				udp_ping_var;

	uint32_t			resync;
	mumble_crypt*		crypt;

	LinkNode*			stream_list;
	LinkNode*			channel_list;
	LinkNode*			user_list;
};

struct LinkNode
{
	uint32_t index;
	void* data;
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
	float			volume_adjustment;
};

void list_add(LinkNode** head_ref, uint32_t index, void *data);
void list_remove(LinkNode **head_ref, uint32_t index);
void list_clear(LinkNode** head_ref);
size_t list_count(LinkNode** head_ref);
void* list_get(LinkNode* current, uint32_t index);

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
	uint8_t *buffer;
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
extern int MUMBLE_REGISTRY;
extern int MUMBLE_TIMER_REG;
extern int MUMBLE_THREAD_REG;

extern void mumble_init(lua_State *l);
extern int luaopen_mumble(lua_State *l);

extern void mumble_log(int level, const char* fmt, ...);

extern void mumble_audio_timer(uv_timer_t* handle);
extern void mumble_ping_timer(uv_timer_t* handle);
//extern void mumble_thread_event(struct ev_loop *loop, ev_io *w, int revents);

extern double gettime(clockid_t mode);

extern void bin_to_strhex(char *bin, size_t binsz, char **result);

#if (defined(LUA_VERSION_NUM) && LUA_VERSION_NUM < 502) && !defined(LUAJIT)
extern void* luaL_testudata(lua_State* L, int index, const char* tname);
extern void luaL_traceback(lua_State* L, lua_State* L1, const char* msg, int level);
#endif

extern void luaL_debugstack(lua_State *l, const char* text);
extern int luaL_typerror(lua_State *L, int narg, const char *tname);
extern int luaL_typerror_table(lua_State *L, int narg, int nkey, int nvalue, const char *tname);
extern int luaL_checkboolean(lua_State *L, int i);
extern int luaL_optboolean(lua_State *L, int i, int d);
extern int luaL_isudata(lua_State *L, int ud, const char *tname);

extern uint64_t util_get_varint(uint8_t buffer[], int *len);

extern void mumble_ping(lua_State* l, MumbleClient* client);

extern uint64_t mumble_adjust_audio_bandwidth(MumbleClient *client);
extern void mumble_create_audio_timer(MumbleClient *client);
extern int mumble_client_connect(lua_State *l);
extern void mumble_disconnect(lua_State* l, MumbleClient *client, const char* reason, bool garbagecollected);

extern void mumble_client_raw_get(lua_State* l, MumbleClient* client);
extern MumbleUser* mumble_user_get(lua_State* l, MumbleClient* client, uint32_t session);
extern void mumble_user_raw_get(lua_State* l, MumbleClient* client, uint32_t session);
extern void mumble_user_remove(lua_State* l, MumbleClient* client, uint32_t session);

extern MumbleChannel* mumble_channel_get(lua_State* l, MumbleClient* client, uint32_t channel_id);
extern void mumble_channel_raw_get(lua_State* l, MumbleClient* client, uint32_t channel_id);
extern void mumble_channel_remove(lua_State* l, MumbleClient* client, uint32_t channel_id);

extern int mumble_push_address(lua_State* l, ProtobufCBinaryData address);

extern void mumble_handle_udp_packet(lua_State* l, MumbleClient* client, char* unencrypted, ssize_t size, bool udp);
extern void mumble_handle_speaking_hooks_legacy(lua_State* l, MumbleClient* client, uint8_t buffer[], uint8_t codec, uint8_t target, uint32_t session);
extern void mumble_handle_speaking_hooks_protobuf(lua_State* l, MumbleClient* client, MumbleUDP__Audio *audio, uint32_t session);

extern int mumble_traceback(lua_State *l);
extern int mumble_hook_call(lua_State* l, MumbleClient *client, const char* hook, int nargs);
extern int mumble_hook_call_ret(lua_State* l, MumbleClient *client, const char* hook, int nargs, int nresults);

extern int mumble_immutable(lua_State *l);
extern void mumble_weak_table(lua_State *l);
extern int mumble_ref(lua_State *l);
extern void mumble_pushref(lua_State *l, int ref);
extern void mumble_unref(lua_State *l, int ref);

extern int mumble_registry_ref(lua_State *l, int t);
extern void mumble_registry_pushref(lua_State *l, int t, int ref);
extern void mumble_registry_unref(lua_State *l, int t, int ref);