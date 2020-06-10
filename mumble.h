#pragma once

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <errno.h>

#include <ev.h>

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

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

int MUMBLE_CONNECTIONS;

double gettime();

void bin_to_strhex(char *bin, size_t binsz, char **result);

void debugstack(lua_State *l, const char* text);
int luaL_checkboolean(lua_State *L, int i);
int luaL_optboolean(lua_State *L, int i, int d);
const char* eztype(lua_State *L, int i);

int64_t util_get_varint(uint8_t buffer[], int *len);

void mumble_create_audio_timer(MumbleClient *client, int bitspersec);
void mumble_disconnect(lua_State* l, MumbleClient *client);

void mumble_client_raw_get(lua_State* l, MumbleClient* client);
MumbleUser* mumble_user_get(lua_State* l, MumbleClient* client, uint32_t session);
void mumble_user_raw_get(lua_State* l, MumbleClient* client, uint32_t session);
void mumble_user_remove(lua_State* l, MumbleClient* client, uint32_t session);

MumbleChannel* mumble_channel_get(lua_State* l, MumbleClient* client, uint32_t channel_id);
MumbleChannel* mumble_channel_raw_get(lua_State* l, MumbleClient* client, uint32_t channel_id);
void mumble_channel_remove(lua_State* l, MumbleClient* client, uint32_t channel_id);

int mumble_traceback(lua_State *l);
void mumble_hook_call(lua_State* l, MumbleClient *client, const char* hook, int nargs);

void audio_transmission_event(lua_State* l, MumbleClient *client);
void audio_transmission_stop(AudioTransmission* sound);

#define METATABLE_CLIENT		"mumble.client"
#define METATABLE_USER			"mumble.user"
#define METATABLE_CHAN			"mumble.channel"
#define METATABLE_ENCODER		"mumble.encoder"
#define METATABLE_VOICETARGET	"mumble.voicetarget"
#define METATABLE_TIMER			"mumble.timer"

/*--------------------------------
	ACL PERMISSIONS
--------------------------------*/

enum {
	ACL_NONE = 0x0,
	ACL_WRITE = 0x1,
	ACL_TRAVERSE = 0x2,
	ACL_ENTER = 0x4,
	ACL_SPEAK = 0x8,
	ACL_MUTE_DEAFEN = 0x10,
	ACL_MOVE = 0x20,
	ACL_MAKE_CHANNEL = 0x40,
	ACL_LINK_CHANNEL = 0x80,
	ACL_WHISPER = 0x100,
	ACL_TEXT_MESSAGE = 0x200,
	ACL_MAKE_TEMP_CHANNEL = 0x400,
	ACL_LISTEN = 0x800,

	// Root channel only
	ACL_KICK = 0x10000,
	ACL_BAN = 0x20000,
	ACL_REGISTER = 0x40000,
	ACL_SELF_REGISTER = 0x80000,
	ACL_RESET_USER_CONTENT = 0x100000,

	ACL_CACHED = 0x8000000,
	ACL_ALL = ACL_WRITE + ACL_TRAVERSE + ACL_ENTER + ACL_SPEAK + ACL_MUTE_DEAFEN + ACL_MOVE
			+ ACL_MAKE_CHANNEL + ACL_LINK_CHANNEL + ACL_WHISPER + ACL_TEXT_MESSAGE + ACL_MAKE_TEMP_CHANNEL + ACL_LISTEN
			+ ACL_KICK + ACL_BAN + ACL_REGISTER + ACL_SELF_REGISTER + ACL_RESET_USER_CONTENT,
};

/*--------------------------------
	MUMBLE PACKET FUNCTIONS
--------------------------------*/

#define NUM_PACKETS 27

enum {
	PACKET_VERSION          = 0,
	PACKET_UDPTUNNEL        = 1,
	PACKET_AUTHENTICATE     = 2,
	PACKET_PING             = 3,
	PACKET_SERVERREJECT     = 4,
	PACKET_SERVERSYNC       = 5,
	PACKET_CHANNELREMOVE    = 6,
	PACKET_CHANNELSTATE     = 7,
	PACKET_USERREMOVE       = 8,
	PACKET_USERSTATE        = 9,
	PACKET_BANLIST          = 10,
	PACKET_TEXTMESSAGE      = 11,
	PACKET_PERMISSIONDENIED = 12,
	PACKET_ACL              = 13,
	PACKET_QUERYUSERS       = 14,
	PACKET_CRYPTSETUP       = 15,
	PACKET_CONTEXTACTIONADD = 16,
	PACKET_CONTEXTACTION    = 17,
	PACKET_USERLIST         = 18,
	PACKET_VOICETARGET      = 19,
	PACKET_PERMISSIONQUERY  = 20,
	PACKET_CODECVERSION     = 21,
	PACKET_USERSTATS        = 22,
	PACKET_REQUESTBLOB      = 23,
	PACKET_SERVERCONFIG     = 24,
	PACKET_SUGGESTCONFIG    = 25,
	PACKET_PLUGINDATA       = 26,
};

#define packet_send(client, type, message) packet_sendex(client, type, message, 0)
int packet_sendex(MumbleClient* client, const int type, const void *message, const int length);

typedef void (*Packet_Handler_Func)(lua_State *lua, MumbleClient *client, Packet *packet);

const Packet_Handler_Func packet_handler[NUM_PACKETS];