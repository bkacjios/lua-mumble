#include "audio/audioqueue.h"

#include "client/client.h"

AudioQueue::AudioQueue() {
	uv_mutex_init(&mutex);
	uv_cond_init(&cond);
}

void AudioQueue::push(AudioWork *item) {
	uv_mutex_lock(&mutex);
	work.push_back(item);
	uv_cond_signal(&cond);
	uv_mutex_unlock(&mutex);
}

AudioWork* AudioQueue::pop(bool *running_flag) {
	uv_mutex_lock(&mutex);

	while (work.empty() && *running_flag) {
		uv_cond_wait(&cond, &mutex);
	}

	if (!*running_flag && work.empty()) {
		// Shutdown and queue empty: no more work
		uv_mutex_unlock(&mutex);
		return NULL;
	}

	// Pop node as usual
	AudioWork *item = work.front();
	work.pop_front();
	uv_mutex_unlock(&mutex);
	return item;
}

AudioWork* AudioQueue::popNonBlocking() {
	uv_mutex_lock(&mutex);

	if (work.empty()) {
		uv_mutex_unlock(&mutex);
		return NULL;
	}

	AudioWork *item = work.front();
	work.pop_front();
	uv_mutex_unlock(&mutex);
	return item;
}

void AudioQueue::clear() {
	uv_mutex_lock(&mutex);
	for (AudioWork *item : work) {
		delete item;
	}
	work.clear();
	uv_mutex_unlock(&mutex);
}

void AudioQueue::cleanup() {
	// Lock the queue to safely walk it
	uv_mutex_lock(&mutex);

	for (AudioWork *item : work) {
		delete item;
	}
	work.clear();

	uv_mutex_unlock(&mutex);

	// Destroy mutex and cond
	uv_mutex_destroy(&mutex);
	uv_cond_destroy(&cond);
}
