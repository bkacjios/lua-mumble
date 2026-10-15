#pragma once

#include "lua/luainc.h"
#include "util/util.h"

#include <cstdint>
#include <optional>
#include <sndfile.h>
#include <string>
#include <vector>

class MumbleClient;

class MumbleUser {
public:
	MumbleUser(MumbleClient *client, uint32_t session, int data);
	~MumbleUser();

	// Write silence to our recording, from the last time we spoke until now
	void recordSilence();
	// Decode and write audio to our recording
	void record(const unsigned char* encoded, size_t encoded_len);
	// Start recording what the user says to an ogg file, returns an error message on failure
	std::string startRecording(const char *path);
	// Finish our recording and update our recording status, returns false if we weren't recording
	bool stopRecording();

	void message(const char *text);
	// Kick the user, with an optional reason
	void kick(const char *reason, bool ban);
	void move(uint32_t channel);
	void setMuted(bool muted);
	void setDeaf(bool deafened);
	void registerUser();
	void requestStats(bool stats_only);
	void setTexture(const uint8_t *data, size_t length);
	void listen(const std::vector<uint32_t> &add, const std::vector<uint32_t> &remove);
	bool isListening(uint32_t channel) const;
	void sendPluginData(const char *id, const uint8_t *data, size_t length);
	void requestTextureBlob();
	void requestCommentBlob();
	void contextAction(const char *action);

private:
	// Write any trailing silence and close our recording file
	void closeRecording();

public:
	MumbleClient*	client;
	int				data;
	bool			connected = false;
	uint32_t		session;
	uint32_t		user_id = 0;
	std::optional<std::string> name;
	uint32_t		channel_id = 0;
	bool			mute = false;
	bool			deaf = false;
	bool			self_mute = false;
	bool			self_deaf = false;
	bool			suppress = false;
	std::optional<std::string> comment;
	std::string		comment_hash;
	bool			speaking = false;
	bool			recording = false;
	bool			priority_speaker = false;
	std::optional<std::string> texture;
	std::string		texture_hash;
	std::optional<std::string> hash;
	LinkList<void>	listens;
	SNDFILE*		recording_file = nullptr;
	uint64_t		last_spoke = 0;
};
