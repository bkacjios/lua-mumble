/*
 * piepie - bot framework for Mumble
 *
 * Author: Tim Cooper <tim.cooper@layeh.com>
 * License: MIT (see LICENSE)
 *
 */

#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <errno.h>

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

#define OPUS_FRAME_SIZE 480
#define PCM_BUFFER 4096

#define PAYLOAD_SIZE_MAX (1024 * 1024 * 8 - 1)

#define PING_TIMEOUT 15

/*
 * Structures
 */

typedef struct MumbleClient MumbleClient;
typedef struct AudioTransmission AudioTransmission;

struct MumbleClient {
	int					socket;
	SSL_CTX				*ssl_context;
	SSL					*ssl;
	bool				connected;
	char*				host;
	uint16_t			port;
	char*				username;
	char*				password;
	int					hooks;
	int					users;
	int					channels;
	double				nextping;
	uint32_t			session;
	float				volume;
	pthread_t			audio_thread;
	pthread_mutex_t		lock;
	pthread_cond_t		cond;
	AudioTransmission*	audio_job;
	bool				audio_finished;
	OpusEncoder*		encoder;
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
		int size;
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

double gettime();
void debugstack(lua_State *l, const char* text);
MumbleClient* mumble_check_meta(lua_State *L, int i, const char* meta);
int luaL_checkboolean(lua_State *L, int i);
int luaL_optboolean(lua_State *L, int i, int d);
void luaL_checktablemeta(lua_State *L, int i, const char* m);
const char* eztype(lua_State *L, int i);

void mumble_disconnect(MumbleClient *client);
void mumble_user_get(lua_State *l, uint32_t session);
void mumble_user_remove(lua_State *l, uint32_t session);
void mumble_channel_get(lua_State *l, uint32_t channel_id);
void mumble_channel_remove(lua_State *l, uint32_t channel_id);
void mumble_hook_call(lua_State *l, const char* hook, int nargs);

void audio_transmission_event(MumbleClient *client);
void audio_transmission_stop(MumbleClient *client);

/*--------------------------------
	MUMBLE CLIENT META METHODS
--------------------------------*/

#define METATABLE_CLIENT	"mumble.client"

int client_connect(lua_State *l);
int client_auth(lua_State *l);
int client_update(lua_State *l);
int client_disconnect(lua_State *l);
int client_isConnected(lua_State *l);
int client_play(lua_State *l);
int client_isPlaying(lua_State *l);
int client_stopPlaying(lua_State *l);
int client_setComment(lua_State *l);
int client_setVolume(lua_State *l);
int client_getVolume(lua_State *l);
int client_hook(lua_State *l);
int client_call(lua_State *l);
int client_getHooks(lua_State *l);
int client_getUsers(lua_State *l);
int client_getChannels(lua_State *l);
int client_gc(lua_State *l);
int client_tostring(lua_State *l);
int client_index(lua_State *l);

/*--------------------------------
	MUMBLE USER META METHODS
--------------------------------*/

#define METATABLE_USER		"mumble.user"

int user_message(lua_State *l);
int user_kick(lua_State *l);
int user_ban(lua_State *l);
int user_move(lua_State *l);
int user_setMuted(lua_State *l);
int user_setDeaf(lua_State *l);
int user_register(lua_State *l);
int user_request_stats(lua_State *l);
int user_tostring(lua_State *l);

/*--------------------------------
	MUMBLE CHANNEL META METHODS
--------------------------------*/

#define METATABLE_CHAN		"mumble.channel"

int channel_message(lua_State *l);
int channel_setDescription(lua_State *l);
int channel_remove(lua_State *l);
int channel_tostring(lua_State *l);

/*--------------------------------
	MUMBLE ENCODER META METHODS
--------------------------------*/

#define METATABLE_ENCODER		"mumble.opus"

int encoder_new(lua_State *l);
int encoder_setBitRate(lua_State *l);
int encoder_tostring(lua_State *l);

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

typedef void (*Packet_Handler_Func)(lua_State *lua, Packet *packet);

extern const Packet_Handler_Func packet_handler[26];