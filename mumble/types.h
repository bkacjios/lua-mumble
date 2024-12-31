#pragma once

#include <lauxlib.h>
#include <stdint.h>
#include <string.h>
#include <uv.h>
#include <sndfile.h>
#include <opus/opus.h>
#include <openssl/evp.h>

#include <stdbool.h>

#include "defines.h"

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
typedef struct MumbleThreadWorker MumbleThreadWorker;
typedef struct MumbleThreadController MumbleThreadController;
typedef struct AudioTimer AudioTimer;
typedef struct QueueNode QueueNode;
typedef struct LinkQueue LinkQueue;

struct MumbleTimer {
	uv_timer_t timer;
	lua_State* l;
	bool running;
	uint32_t count;
	int self;
	int callback;
	uint64_t after;
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

struct MumbleThreadWorker {
	lua_State* l;
	uv_loop_t loop;
	MumbleThreadController* controller;
	uv_async_t async_message;
	pthread_mutex_t mutex;
	pthread_cond_t cond;
	volatile bool finished;
	LinkQueue*	message_queue;
	int message;
	int self;
};

struct QueueNode {
	char* data;
	size_t size;
	QueueNode* next;
};

typedef struct LinkQueue {
	QueueNode* front;	// Pointer to the front (oldest message)
	QueueNode* rear;	// Pointer to the rear (newest message)
} MessageQueue;

struct MumbleThreadController {
	lua_State* l;
	MumbleThreadWorker* worker;
	uv_thread_t thread;
	uv_async_t async_finish;
	uv_async_t async_message;
	pthread_mutex_t mutex;
	pthread_cond_t cond;
	volatile bool started;
	char* bytecode;
	const char* filename;
	size_t bytecode_size;
	int self;
	int finish;
	int message;
	LinkQueue*	message_queue;
};

struct MumbleClient {
	lua_State*			l;
	int					self;
	uv_connect_t		tcp_connect_req;
	uv_tcp_t			socket_tcp;
	uv_os_fd_t			socket_tcp_fd;
	uv_udp_t			socket_udp;
	uv_poll_t			ssl_poll;
	struct addrinfo*	server_host_udp;
	struct addrinfo*	server_host_tcp;
	SSL_CTX				*ssl_context;
	SSL					*ssl;
	bool				connecting;
	bool				connected;
	bool				synced;
	bool				legacy;
	char*				host;
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

#define PACKET_HEADER_SIZE 6

typedef struct {
	uint16_t type;
	size_t length;
	size_t header_len;
	uint8_t *header;
	size_t body_len;
	uint8_t *body;
} MumblePacket;

typedef struct {
	uint8_t *buffer;
	int length;
	int header_length;
} VoicePacket;