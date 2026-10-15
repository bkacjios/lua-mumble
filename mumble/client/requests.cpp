// Requests the client sends to the server

#include "client/client.h"

#include "audio/audiocontext.h"
#include "audio/resampler.h"
#include "defines.h"
#include "gitversion.h"
#include "net/voicepacket.h"
#include "util/buffer.h"
#include "util/platform.h"

#include <cerrno>
#include <cstring>

void MumbleClient::authenticate(const char *username, const char *password, const std::vector<const char*> &tokens) {
	MumbleProto__Version version = MUMBLE_PROTO__VERSION__INIT;
	MumbleProto__Authenticate auth = MUMBLE_PROTO__AUTHENTICATE__INIT;

	auth.has_opus = true;
	auth.opus = true;
	auth.username = (char*) username;
	auth.password = (char*) password;
	auth.n_tokens = tokens.size();
	auth.tokens = (char**) tokens.data();
	auth.has_client_type = true;
	auth.client_type = CLIENT_TYPE_BOT;

	uv_utsname_t uname;
	uv_os_uname(&uname);

	version.has_version_v1 = true;
	version.version_v1 = versionV1();
	if (!legacy) {
		version.has_version_v2 = true;
		version.version_v2 = versionV2();
	}
	version.release = (char*) MODULE_NAME " " GIT_VERSION;
	version.os = uname.sysname;
	version.os_version = uname.release;

	send(PACKET_VERSION, &version);
	send(PACKET_AUTHENTICATE, &auth);
}

void MumbleClient::setTokens(const std::vector<const char*> &tokens) {
	MumbleProto__Authenticate auth = MUMBLE_PROTO__AUTHENTICATE__INIT;
	auth.n_tokens = tokens.size();
	auth.tokens = (char**) tokens.data();
	send(PACKET_AUTHENTICATE, &auth);
}

void MumbleClient::requestBanList() {
	MumbleProto__BanList list = MUMBLE_PROTO__BAN_LIST__INIT;
	list.has_query = true;
	list.query = true;
	send(PACKET_BANLIST, &list);
}

void MumbleClient::requestUserList() {
	MumbleProto__UserList list = MUMBLE_PROTO__USER_LIST__INIT;
	send(PACKET_USERLIST, &list);
}

void MumbleClient::sendPluginData(const char *id, const uint8_t *data, size_t length, const std::vector<uint32_t> &receivers) {
	MumbleProto__PluginDataTransmission message = MUMBLE_PROTO__PLUGIN_DATA_TRANSMISSION__INIT;
	message.has_sendersession = true;
	message.sendersession = session;
	message.dataid = (char*) id;
	message.has_data = true;
	message.data.data = (uint8_t*) data;
	message.data.len = length;
	message.n_receiversessions = receivers.size();
	message.receiversessions = (uint32_t*) receivers.data();
	send(PACKET_PLUGINDATA, &message);
}

void MumbleClient::transmit(uint8_t codec, const uint8_t *data, size_t length, bool speaking) {
	if (legacy) {
		uint16_t frame_header = length;
		if (codec == LEGACY_UDP_OPUS && !speaking) {
			// Set 14th bit to 1 to signal end of stream.
			frame_header = (1 << 13) | length;
		} else if ((codec == LEGACY_UDP_SPEEX || codec == LEGACY_UDP_CELT_ALPHA || codec == LEGACY_UDP_CELT_BETA) && speaking) {
			// Set continuation bit at the MSB if we are not at the end of the stream
			frame_header = (1 << 0) | length;
		}

		uint8_t packet_buffer[PAYLOAD_SIZE_MAX];
		VoicePacket packet(packet_buffer);
		packet.setHeader(codec, audio_target, audio_sequence);
		packet.setFrame(codec, frame_header, data, length);

		handleSpeakingHooksLegacy(packet.buffer + 1, codec, audio_target, session);

		if (tcp_udp_tunnel) {
			sendTunnel(packet_buffer, packet.getLength());
		} else {
			sendUDPOrTunnel(packet_buffer, packet.getLength());
		}
	} else {
		MumbleUDP__Audio audio = MUMBLE_UDP__AUDIO__INIT;
		audio.frame_number = audio_sequence;
		audio.opus_data.data = (uint8_t*) data;
		audio.opus_data.len = length;
		audio.is_terminator = !speaking;
		audio.header_case = MUMBLE_UDP__AUDIO__HEADER_TARGET;
		audio.target = audio_target;

		uint8_t packet_buffer[PAYLOAD_SIZE_MAX];
		packet_buffer[0] = PROTO_UDP_AUDIO;
		int len = 1 + mumble_udp__audio__pack(&audio, packet_buffer + 1);

		handleSpeakingHooksProtobuf(&audio, session);

		if (tcp_udp_tunnel) {
			sendTunnel(packet_buffer, len);
		} else {
			sendUDPOrTunnel(packet_buffer, len);
		}
	}

	// Mumble numbers audio in 10ms frames, so advance by how many this packet holds
	int samples = codec == LEGACY_UDP_OPUS ? opus_packet_get_nb_samples(data, length, AUDIO_SAMPLE_RATE) : 0;
	audio_sequence += samples > 0 ? samples / (AUDIO_SAMPLE_RATE / 100) : 1;
}

bool MumbleClient::setAudioPacketSize(int ms) {
	switch (ms) {
	case AUDIO_FRAME_SIZE_TINY:
	case AUDIO_FRAME_SIZE_SMALL:
	case AUDIO_FRAME_SIZE_MEDIUM:
	case AUDIO_FRAME_SIZE_LARGE:
		audio_frames = ms;
		return true;
	default:
		return false;
	}
}

void MumbleClient::setComment(const char *comment) {
	MumbleProto__UserState message = MUMBLE_PROTO__USER_STATE__INIT;
	message.has_session = true;
	message.session = session;
	message.comment = (char*) comment;
	send(PACKET_USERSTATE, &message);
}

void MumbleClient::createChannel(uint32_t parent, const char *name, const char *description, int32_t position, bool temporary, uint32_t max_users) {
	MumbleProto__ChannelState message = MUMBLE_PROTO__CHANNEL_STATE__INIT;
	message.has_parent = true;
	message.parent = parent;
	message.name = (char*) name;
	message.description = (char*) description;
	message.has_position = true;
	message.position = position;
	message.has_temporary = true;
	message.temporary = temporary;
	message.has_max_users = true;
	message.max_users = max_users;
	send(PACKET_CHANNELSTATE, &message);
}

void MumbleClient::registerVoiceTarget(uint32_t id, const std::vector<MumbleProto__VoiceTarget__Target*> &targets) {
	MumbleProto__VoiceTarget message = MUMBLE_PROTO__VOICE_TARGET__INIT;
	message.has_id = true;
	message.id = id;
	message.n_targets = targets.size();
	message.targets = (MumbleProto__VoiceTarget__Target**) targets.data();
	send(PACKET_VOICETARGET, &message);
}

void MumbleClient::requestBlobs(const std::vector<uint32_t> &textures, const std::vector<uint32_t> &comments, const std::vector<uint32_t> &descriptions) {
	MumbleProto__RequestBlob message = MUMBLE_PROTO__REQUEST_BLOB__INIT;
	message.n_session_texture = textures.size();
	message.session_texture = (uint32_t*) textures.data();
	message.n_session_comment = comments.size();
	message.session_comment = (uint32_t*) comments.data();
	message.n_channel_description = descriptions.size();
	message.channel_description = (uint32_t*) descriptions.data();
	send(PACKET_REQUESTBLOB, &message);
}

std::string MumbleClient::addAudioBuffer(ByteBuffer *buffer, opus_int32 samplerate, int channels) {
	if (buffer->init(PCM_BUFFER * sizeof(float)) == NULL) {
		return std::string("error creating buffer: ") + strerror(errno);
	}

	int error;
	std::unique_ptr<Resampler> resampler(new Resampler(SRC_SINC_FASTEST, AUDIO_PLAYBACK_CHANNELS, &error));
	if (!resampler->isValid()) {
		return std::string("error creating resampler state: ") + src_strerror(error);
	}

	buffer->context = new AudioContext(this, samplerate, channels, std::move(resampler));

	// We keep a list of it, but we aren't referencing it.
	// ByteBuffers's __gc method will remove it from this list.
	audio_pipes.add(LUA_NOREF, buffer);
	return "";
}

std::string MumbleClient::serverAddress() const {
	char address[INET6_ADDRSTRLEN];

	if (server_host_tcp->ai_family == AF_INET) {
		inet_ntop(AF_INET, &(((struct sockaddr_in*)(server_host_tcp->ai_addr))->sin_addr), address, INET_ADDRSTRLEN);
	} else {
		inet_ntop(AF_INET6, &(((struct sockaddr_in6*)(server_host_tcp->ai_addr))->sin6_addr), address, INET6_ADDRSTRLEN);
	}

	return address;
}

double MumbleClient::uptime() const {
	return (double) (uv_now(uv_default_loop()) - time) / 1000.0;
}
