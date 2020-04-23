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
#include <vorbis/codec.h>
#include <vorbis/vorbisfile.h>

#include "proto/Mumble.pb-c.h"

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

// The size of the PCM buffer
// Technically, it should be FRAMESIZE*SAMPLERATE/100
#define PCM_BUFFER 4096

#define PAYLOAD_SIZE_MAX (1024 * 1024 * 8 - 1)

#define PING_TIMEOUT 30

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
typedef struct my_signal my_signal;

struct my_io {
	ev_io io;
	MumbleClient* client;
};

struct my_timer {
	ev_timer timer;
	MumbleClient* client;
};

struct my_signal {
	ev_signal signal;
	MumbleClient* client;
};

struct MumbleClient {
	lua_State*			l;
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
	my_signal			signal;
	AudioTransmission*	audio_job;
	bool				audio_finished;
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
	FILE *file;
	lua_State *lua;
	OggVorbis_File ogg;
	uint32_t sequence;
	MumbleClient *client;
	float volume;
	struct {
		char pcm[PCM_BUFFER];
		uint32_t size;
	} buffer;
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

void mumble_create_audio_timer(MumbleClient *client, int bitspersec);
void mumble_disconnect(MumbleClient *client);

void mumble_client_raw_get(MumbleClient* client);
MumbleUser* mumble_user_get(MumbleClient* client, uint32_t session);
void mumble_user_raw_get(MumbleClient* client, uint32_t session);
void mumble_user_remove(MumbleClient* client, uint32_t session);

MumbleChannel* mumble_channel_get(MumbleClient* client, uint32_t channel_id);
MumbleChannel* mumble_channel_raw_get(MumbleClient* client, uint32_t channel_id);
void mumble_channel_remove(MumbleClient* client, uint32_t channel_id);

void mumble_hook_call(MumbleClient *client, const char* hook, int nargs);

void audio_transmission_event(MumbleClient *client);
void audio_transmission_stop(MumbleClient *client);

#define METATABLE_CLIENT		"mumble.client"
#define METATABLE_USER			"mumble.user"
#define METATABLE_CHAN			"mumble.channel"
#define METATABLE_ENCODER		"mumble.encoder"
#define METATABLE_VOICETARGET	"mumble.voicetarget"

/*--------------------------------
	MUMBLE PACKET FUNCTIONS
--------------------------------*/

enum {
	PACKET_VERSION          = 0,
	PACKET_UDPTUNNEL        = 1,
	PACKET_AUTHENTICATE     = 2,
	PACKET_PING             = 3,
	PACKET_CHANNELREMOVE    = 6,
	PACKET_CHANNELSTATE     = 7,
	PACKET_USERREMOVE       = 8,
	PACKET_USERSTATE        = 9,
	PACKET_TEXTMESSAGE      = 11,
	PACKET_VOICETARGET      = 19,
	PACKET_CODECVERSION     = 21,
	PACKET_USERSTATS        = 22,
	PACKET_REQUESTBLOB      = 23,
};

#define packet_send(client, type, message) packet_sendex(client, type, message, 0)
int packet_sendex(MumbleClient* client, const int type, const void *message, const int length);

typedef void (*Packet_Handler_Func)(MumbleClient *client, lua_State *lua, Packet *packet);

const Packet_Handler_Func packet_handler[26];