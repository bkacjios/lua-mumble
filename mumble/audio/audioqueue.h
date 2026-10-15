#pragma once

#include "defines.h"

#include <cstdint>
#include <list>
#include <opus/opus.h>
#include <sndfile.h>
#include <uv.h>

class MumbleClient;

struct AudioFrame {
	float l;
	float r;
};

// A chunk of mixed PCM audio on its way through the encoder and out to the server
struct AudioWork {
	MumbleClient* client;
	sf_count_t frame_size;
	bool end_frame;
	AudioFrame pcm[MAX_PCM_FRAMES];
	uint8_t encoded[PAYLOAD_SIZE_MAX];
	opus_int32 encoded_len;
	float encode_time;
	uint32_t audio_sequence;
	// Which of the client's connections this audio was mixed for
	uint32_t connection_id;
};

// A blocking FIFO of AudioWork shared between threads
class AudioQueue {
public:
	AudioQueue();

	void push(AudioWork *work);
	// Blocks until work is available, or returns nullptr once *running_flag is false and the queue is empty
	AudioWork* pop(bool *running_flag);
	AudioWork* popNonBlocking();
	// Frees all pending work
	void clear();
	// Frees all pending work, and destroys the queue's mutex and condition
	void cleanup();

	uv_mutex_t mutex;
	uv_cond_t cond;

private:
	std::list<AudioWork*> work;
};
