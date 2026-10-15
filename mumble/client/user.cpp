#include "client/user.h"

#include "client/client.h"
#include "net/packet.h"
#include "defines.h"
#include "util/log.h"

#include <cstdlib>

MumbleUser::MumbleUser(MumbleClient *client, uint32_t session, int data) : client(client), data(data), session(session) {
}

MumbleUser::~MumbleUser() {
	if (recording_file != NULL) {
		closeRecording();
	}
}

void MumbleUser::recordSilence() {
	if (recording_file != NULL) {
		uint64_t now = uv_now(uv_default_loop());
		uint64_t silence_duration = now - last_spoke;

		// Convert duration to number of samples
		int silence_samples = (silence_duration / 1000.0) * AUDIO_SAMPLE_RATE * AUDIO_PLAYBACK_CHANNELS;

		if (silence_samples > 0) {
			float *silence_buffer = (float *)calloc(silence_samples, sizeof(float));
			if (silence_buffer) {
				sf_write_float(recording_file, silence_buffer, silence_samples);
				free(silence_buffer);
			} else {
				mumble_log(LOG_ERROR, "Failed to allocate memory for silence buffer (%d samples)", silence_samples);
			}
		}
	}
}

void MumbleUser::record(const unsigned char* encoded, size_t encoded_len) {
	if (recording_file != NULL) {
		int samples_per_channel = opus_decoder_get_nb_samples(client->decoder, encoded, encoded_len);
		int total_samples = samples_per_channel * AUDIO_PLAYBACK_CHANNELS;

		float *pcm = (float *)malloc(sizeof(float) * total_samples);
		if (!pcm) {
			mumble_log(LOG_ERROR, "failed to allocate PCM buffer");
			return;
		}

		opus_int32 pcmlen = opus_decode_float(client->decoder, encoded, encoded_len, pcm, samples_per_channel, 0);
		if (pcmlen < 0) {
			mumble_log(LOG_ERROR, "opus decoding error: %s", opus_strerror(pcmlen));
			free(pcm);
			return;
		}

		sf_write_float(recording_file, pcm, pcmlen * AUDIO_PLAYBACK_CHANNELS);

		free(pcm);
	}
}

void MumbleUser::closeRecording() {
	// Handle any silence, from the time the user last stopped talking, until the end of the recording
	recordSilence();
	sf_close(recording_file);
	recording_file = NULL;
}

bool MumbleUser::stopRecording() {
	if (recording_file == NULL) {
		return false;
	}
	closeRecording();
	client->updateRecordingStatus();
	return true;
}

void MumbleUser::message(const char *text) {
	MumbleProto__TextMessage message = MUMBLE_PROTO__TEXT_MESSAGE__INIT;
	message.message = (char*) text;
	message.n_session = 1;
	message.session = &session;
	client->send(PACKET_TEXTMESSAGE, &message);
}

void MumbleUser::kick(const char *reason, bool ban) {
	MumbleProto__UserRemove message = MUMBLE_PROTO__USER_REMOVE__INIT;
	message.session = session;
	message.reason = (char*) reason;
	if (ban) {
		message.has_ban = true;
		message.ban = true;
	}
	client->send(PACKET_USERREMOVE, &message);
}

void MumbleUser::move(uint32_t channel) {
	MumbleProto__UserState message = MUMBLE_PROTO__USER_STATE__INIT;
	message.has_session = true;
	message.session = session;
	message.has_channel_id = true;
	message.channel_id = channel;
	client->send(PACKET_USERSTATE, &message);
}

void MumbleUser::setMuted(bool muted) {
	MumbleProto__UserState message = MUMBLE_PROTO__USER_STATE__INIT;
	message.has_session = true;
	message.session = session;
	message.has_mute = true;
	message.mute = muted;
	client->send(PACKET_USERSTATE, &message);
}

void MumbleUser::setDeaf(bool deafened) {
	// Deafening someone also mutes them
	MumbleProto__UserState message = MUMBLE_PROTO__USER_STATE__INIT;
	message.has_session = true;
	message.session = session;
	message.has_mute = true;
	message.mute = deafened;
	message.has_deaf = true;
	message.deaf = deafened;
	client->send(PACKET_USERSTATE, &message);
}

void MumbleUser::registerUser() {
	MumbleProto__UserState message = MUMBLE_PROTO__USER_STATE__INIT;
	message.has_session = true;
	message.session = session;
	message.has_user_id = true;
	message.user_id = 0;
	client->send(PACKET_USERSTATE, &message);
}

void MumbleUser::requestStats(bool stats_only) {
	MumbleProto__UserStats message = MUMBLE_PROTO__USER_STATS__INIT;
	message.has_session = true;
	message.session = session;
	message.has_stats_only = true;
	message.stats_only = stats_only;
	client->send(PACKET_USERSTATS, &message);
}

void MumbleUser::setTexture(const uint8_t *data, size_t length) {
	MumbleProto__UserState message = MUMBLE_PROTO__USER_STATE__INIT;
	message.has_session = true;
	message.session = session;
	message.has_texture = true;
	message.texture.data = (uint8_t*) data;
	message.texture.len = length;
	client->send(PACKET_USERSTATE, &message);
}

void MumbleUser::listen(const std::vector<uint32_t> &add, const std::vector<uint32_t> &remove) {
	MumbleProto__UserState message = MUMBLE_PROTO__USER_STATE__INIT;
	message.has_session = true;
	message.session = session;
	message.n_listening_channel_add = add.size();
	message.listening_channel_add = (uint32_t*) add.data();
	message.n_listening_channel_remove = remove.size();
	message.listening_channel_remove = (uint32_t*) remove.data();
	client->send(PACKET_USERSTATE, &message);
}

bool MumbleUser::isListening(uint32_t channel) const {
	return listens.contains(channel);
}

void MumbleUser::sendPluginData(const char *id, const uint8_t *data, size_t length) {
	client->sendPluginData(id, data, length, {session});
}

void MumbleUser::requestTextureBlob() {
	client->requestBlobs({session}, {}, {});
}

void MumbleUser::requestCommentBlob() {
	client->requestBlobs({}, {session}, {});
}

void MumbleUser::contextAction(const char *action) {
	MumbleProto__ContextAction message = MUMBLE_PROTO__CONTEXT_ACTION__INIT;
	message.has_session = true;
	message.session = session;
	message.action = (char*) action;
	client->send(PACKET_CONTEXTACTION, &message);
}

std::string MumbleUser::startRecording(const char *path) {
	if (recording_file != NULL) {
		return "user is already being recorded";
	}

	SF_INFO info = {};
	info.samplerate = AUDIO_SAMPLE_RATE;
	info.channels = AUDIO_PLAYBACK_CHANNELS;
	info.format = SF_FORMAT_OGG | SF_FORMAT_VORBIS;

	SNDFILE *file = sf_open(path, SFM_WRITE, &info);
	if (!file) {
		return std::string("error opening file \"") + path + "\" (" + sf_strerror(NULL) + ")";
	}

	// Start recording at current timestamp
	last_spoke = uv_now(uv_default_loop());
	recording_file = file;

	client->updateRecordingStatus();
	return "";
}
