#pragma once

#include "audio/audioqueue.h"
#include "defines.h"
#include "lua/luainc.h"
#include "net/crypt.h"
#include "net/packet.h"
#include "proto/Mumble.pb-c.h"
#include "proto/MumbleUDP.pb-c.h"
#include "util/buffer.h"
#include "util/util.h"

#include <atomic>
#include <netdb.h>
#include <optional>
#include <openssl/ssl.h>
#include <opus/opus.h>
#include <sndfile.h>
#include <string>
#include <uv.h>
#include <vector>

class AudioStream;
class MumbleOpusEncoder;
class MumbleChannel;
class MumbleUser;

class MumbleClient {
	// Lets the tests in tests/core drive the audio pipeline directly
	friend class ClientTestAccess;

public:
	MumbleClient(lua_State *l, uint64_t major, uint64_t minor, uint64_t patch);

	// Create our Lua tables and start our audio threads
	void init();
	// Disconnect, stop our threads and release our Lua references, before being garbage collected
	void destroy();

	// Every client that is connecting or connected
	static LinkList<MumbleClient> connections;

	/* Connection (client.cpp) */
	// Connect to a server. While a previous connection is still closing, it's queued to start once that's done.
	int connect(lua_State *l);
	void disconnect(const char* reason, bool garbagecollected);
	// Release our users, channels and streams, and stop being a connected client
	void cleanup();
	// Free what a connection allocated, so we can connect again
	void freeConnection();
	// Stop our audio threads and free what we keep across connections, once we're garbage collected
	void shutdown();

	uint64_t versionV1() const;
	uint64_t versionV2() const;

	/* Hooks (client.cpp) */
	int hookCall(const char* hook, int nargs);
	int hookCallRet(const char* hook, int nargs, int nresults);
	// Add the function at a Lua stack index as the callback named `name` for a hook
	void addHook(const char* hook, const char* name, int index);
	void removeHook(const char* hook, const char* name);

	/* Users and channels (client.cpp) */
	void pushSelf();
	MumbleUser* getUser(uint32_t session);
	void pushUser(uint32_t session);
	void removeUser(uint32_t session);
	MumbleChannel* getChannel(uint32_t channel_id);
	void pushChannel(uint32_t channel_id);
	void removeChannel(uint32_t channel_id);
	// Find a channel by its path relative to the root channel, or NULL
	MumbleChannel* findChannel(const char *path);
	void updateRecordingStatus();

	/* Pings (client.cpp) */
	void ping();
	double updatePingTCP(uint64_t timestamp);
	void updatePingUDP(uint64_t timestamp, bool udp);

	/* Incoming voice (client.cpp) */
	void handleUDPPacket(unsigned char* unencrypted, ssize_t size, bool udp);
	void handleSpeakingHooksLegacy(uint8_t buffer[], uint8_t codec, uint8_t target, uint32_t session);
	void handleSpeakingHooksProtobuf(MumbleUDP__Audio *audio, uint32_t session);

	// Lower our bitrate and/or raise our packet size to fit within the server's max_bandwidth.
	// Our audio threads pick up a new packet size on their next frame.
	void adjustAudioBandwidth();

	/* Requests to the server (requests.cpp) */
	void authenticate(const char *username, const char *password, const std::vector<const char*> &tokens);
	void setTokens(const std::vector<const char*> &tokens);
	void requestBanList();
	void requestUserList();
	void sendPluginData(const char *id, const uint8_t *data, size_t length, const std::vector<uint32_t> &receivers);
	// Send an already encoded frame of audio
	void transmit(uint8_t codec, const uint8_t *data, size_t length, bool speaking);
	// Returns false if ms isn't one of the allowed packet sizes (10, 20, 40, 60)
	bool setAudioPacketSize(int ms);
	void setComment(const char *comment);
	void createChannel(uint32_t parent, const char *name, const char *description, int32_t position, bool temporary, uint32_t max_users);
	void registerVoiceTarget(uint32_t id, const std::vector<MumbleProto__VoiceTarget__Target*> &targets);
	void requestBlobs(const std::vector<uint32_t> &textures, const std::vector<uint32_t> &comments, const std::vector<uint32_t> &descriptions);
	// Allocate a buffer that mixes raw PCM written to it into our audio, returns an error message on failure
	std::string addAudioBuffer(ByteBuffer *buffer, opus_int32 samplerate, int channels);
	std::string serverAddress() const;
	double uptime() const;

	/* Receiving (packets.cpp) */
	// Dispatch a fully read TCP packet, returns false for unknown packet types
	bool handlePacket(MumblePacket *packet);

	/* Sending (packets.cpp) */
	template <typename Message>
	int send(int type, const Message *message) {
		return sendMessage(type, &message->base);
	}
	int sendMessage(int type, const ProtobufCMessage *message);
	int sendTunnel(const uint8_t *data, int length);
	// Encrypt and send a packet over UDP, returns false if it couldn't be sent
	bool sendUDP(const void *message, int length);
	// Send voice data over UDP, tunneling just this packet through TCP if it couldn't be sent
	void sendUDPOrTunnel(const uint8_t *data, int length);
	// Switch between sending voice over UDP and tunneling it through TCP, like Mumble does on every TCP ping reply,
	// from how many of our UDP packets the server says it decrypted
	void updateUDPMode(uint32_t remote_good);

	static void onPingTimer(uv_timer_t* handle);

	/* Audio (audio.cpp) */
	void shutdownAudioQueues();
	static void audioBufferThread(void *arg);
	static void audioPlaybackThread(void *arg);
	static void audioEncodeThread(void *arg);
	static void onAudioPlaybackAsync(uv_async_t* handle);

	lua_State*			l;
	int					self = LUA_NOREF;

	uint64_t			version_major;
	uint64_t			version_minor;
	uint64_t			version_patch;

	uv_connect_t		tcp_connect_req{};
	uv_tcp_t			socket_tcp{};
	uv_os_fd_t			socket_tcp_fd = -1;
	uv_udp_t			socket_udp{};
	uv_poll_t			ssl_poll{};
	// Every handle (these, ping_timer and audio_playback_async) the current connection initialized, to close before reuse
	std::vector<uv_handle_t*>	connection_handles;
	// How many handles from our last connection are still closing
	int					closing_handles = 0;

	struct addrinfo*	server_host_udp = nullptr;
	struct addrinfo*	server_host_tcp = nullptr;

	SSL_CTX				*ssl_context = nullptr;
	SSL					*ssl = nullptr;

	bool				connecting = false;
	// Read by our audio threads
	std::atomic<bool>	connected{false};
	bool				synced = false;
	bool				legacy;
	char*				host = nullptr;
	uint16_t			port = 0;
	int					hooks = LUA_NOREF;
	int					commands = LUA_NOREF;
	int					users = LUA_NOREF;
	int					channels = LUA_NOREF;
	int					audio_streams = LUA_NOREF;
	uint32_t			max_bandwidth = 0;
	uint64_t			time = 0;
	uint32_t			session = 0;
	double				volume = 0.5;
	bool				ducking = false;
	double				ducking_volume = 0;

	uv_timer_t			ping_timer{};

	AudioFrame			audio_output[MAX_PCM_FRAMES];
	uint32_t			audio_sequence = 0;
	// Read by our audio threads
	std::atomic<uint32_t>	audio_frames{AUDIO_DEFAULT_FRAMES};
	// How many ms of audio the current playback tick produces, which is how long the timer waited for it.
	// Set by the playback thread, so a new audio_frames only takes effect once the timer has waited that long.
	std::atomic<uint32_t>	audio_tick_frames{AUDIO_DEFAULT_FRAMES};

	// Shared by the main thread and the encode thread, so only use it through its locking methods
	MumbleOpusEncoder*	encoder = nullptr;
	int					encoder_ref = LUA_NOREF;

	OpusDecoder*		decoder = nullptr;

	uint8_t				audio_target = 0;

	MumblePacket		tcp_read_packet;

	uint32_t			tcp_packets = 0;
	double				tcp_ping_avg = 0;
	double				tcp_ping_var = 0;

	bool				tcp_udp_tunnel = true;

	uint32_t			udp_packets = 0;
	double				udp_ping_avg = 0;
	double				udp_ping_var = 0;

	uint32_t			resync = 0;
	CryptState*			crypt = nullptr;

	LinkList<AudioStream>	stream_list;
	LinkList<AudioStream>	reclaim_list;

	LinkList<MumbleChannel>	channel_list;
	LinkList<MumbleUser>	user_list;
	LinkList<ByteBuffer>	audio_pipes;

	bool				recording = false;

	uv_thread_t			audio_buffer_thread = 0;
	bool				audio_buffer_thread_running = false;

	uv_thread_t			audio_playback_thread = 0;
	uv_async_t			audio_playback_async{};
	std::atomic<bool>	audio_playback_async_pending{false};
	bool				audio_playback_thread_running = false;
	uint64_t			audio_playback_next = 0;
	uint64_t			audio_playback_last = 0;

	AudioQueue			audio_encode_queue;
	uv_thread_t			audio_encode_thread = 0;
	// Guarded by audio_encode_queue.mutex
	bool				audio_encode_thread_running = false;

	AudioQueue			audio_send_queue;

	// Whether the last frame we encoded had audio, and wasn't the end of a transmission
	bool				audio_stream_active = false;

	// Set once shutdown() has stopped our threads and released our resources
	bool				shut_down = false;

	// Keeps us from being garbage collected while connected, or until our connection has finished closing
	int					alive = LUA_NOREF;
	// Set while disconnect() runs, so a connect() from the OnDisconnect hook waits for it to finish
	bool				disconnecting = false;
	// Incremented every connection, so audio encoded for an old connection is never sent on a new one
	uint32_t			connection_id = 0;

	// A connect() that's waiting for our last connection to finish closing
	struct PendingConnect {
		std::string host;
		uint16_t port;
		std::string certificate;
		std::string key;
	};
	std::optional<PendingConnect> pending_connect;
	// Starts a pending connect on the next loop iteration. Heap allocated, so it can outlive us while closing.
	uv_timer_t*			connect_timer = nullptr;

	uv_mutex_t			main_mutex;
	uv_mutex_t			inner_mutex;

private:
	/* Packet handlers (packets.cpp) */
	typedef void (MumbleClient::*PacketHandler)(MumblePacket *packet);
	static const PacketHandler packetHandlers[NUM_PACKETS];

	void onServerVersion(MumblePacket *packet);
	void onUDPTunnel(MumblePacket *packet);
	void onPing(MumblePacket *packet);
	void onReject(MumblePacket *packet);
	void onServerSync(MumblePacket *packet);
	void onChannelRemove(MumblePacket *packet);
	void onChannelState(MumblePacket *packet);
	void onUserRemove(MumblePacket *packet);
	void onUserState(MumblePacket *packet);
	void onBanList(MumblePacket *packet);
	void onTextMessage(MumblePacket *packet);
	void onPermissionDenied(MumblePacket *packet);
	void onACL(MumblePacket *packet);
	void onQueryUsers(MumblePacket *packet);
	void onCryptSetup(MumblePacket *packet);
	void onContextActionModify(MumblePacket *packet);
	void onUserList(MumblePacket *packet);
	void onPermissionQuery(MumblePacket *packet);
	void onCodecVersion(MumblePacket *packet);
	void onUserStats(MumblePacket *packet);
	void onServerConfig(MumblePacket *packet);
	void onSuggestConfig(MumblePacket *packet);
	void onPluginData(MumblePacket *packet);

	void pingTCP();
	void pingUDP();
	uint64_t pingUDPLegacy();
	uint64_t pingUDPProtobuf();

	/* Audio (audio.cpp) */
	void cleanReclaimed();
	void encodeEvent();
	void sendEvent();
	void encodeAudio(sf_count_t frame_size, bool end_frame);
	// Mix a stream into our audio output, returns true if it reached its end
	bool processAudioFile(AudioStream *sound, uint32_t sample_size, sf_count_t *biggest_read);
	// Stop streams that reached their end, then call OnAudioStreamEnd for each of them
	void handleAudioStreamsEnd(const std::vector<AudioStream*> &ended);
	void bitrateWarning(size_t length);
	void sendLegacyAudio(uint8_t *encoded, opus_int32 encoded_len, bool end_frame, uint32_t audio_sequence);
	void sendProtobufAudio(uint8_t *encoded, opus_int32 encoded_len, bool end_frame, uint32_t audio_sequence);

	/* Connection lifetime (client.cpp) */
	// Start connecting to a server, with our userdata at a Lua stack index. Returns an error message on failure.
	std::string start(int index, const char *host, uint16_t port, const char *certificate, const char *key);
	// Track a handle the current connection initialized
	void openHandle(uv_handle_t *handle);
	// Close every handle our connection opened, and free what it allocated
	void closeConnection();
	// Reset what a connection learns about the server back to how a new client starts
	void resetConnectionState();
	// Our last handle closed, so start any pending connect
	void finishClosing();
	// Stop keeping ourself alive once there's no connection, closing handle or pending connect left
	void releaseIfIdle();
	static void onHandleClosed(uv_handle_t *handle);
	static void onConnectTimer(uv_timer_t *handle);

	/* Socket callbacks (client.cpp) */
	static void onConnectedTCP(uv_connect_t *req, int status);
	static void onWritableTCP(uv_poll_t* handle, int status, int events);
	static void onReadableTCP(uv_poll_t* handle, int status, int events);
	static void onReadUDP(uv_udp_t* handle, ssize_t nread, const uv_buf_t* buf, const struct sockaddr* addr, unsigned flags);
	// Returns true when the read should be retried once the socket is ready again
	bool handleSSLReadError(int ret);
};
