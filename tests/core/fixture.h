#pragma once

// A real MumbleClient in a real Lua state, "connected" over an in-process TLS socket pair to a reader that
// records and decodes every packet the client sends. The playback tick can be driven at real frame pacing,
// so the buffer and encode threads behave as they do live.

#include "test.h"

#include "audio/audiostream.h"
#include "audio/encoder.h"
#include "client/client.h"
#include "lua/registry.h"
#include "module.h"
#include "net/packet.h"
#include "net/varint.h"
#include "proto/MumbleUDP.pb-c.h"

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <opus/opus.h>
#include <openssl/evp.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <sndfile.h>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

// Calls the private parts of the client that the playback timer normally drives
class ClientTestAccess {
public:
	// Open the handles a real connection would, for a client we connect by hand
	static void open(MumbleClient *client) {
		uv_async_init(uv_default_loop(), &client->audio_playback_async, MumbleClient::onAudioPlaybackAsync);
		client->openHandle((uv_handle_t*) &client->audio_playback_async);
	}

	// One playback tick after waiting frame_ms, exactly as the playback thread and
	// MumbleClient::onAudioPlaybackAsync run it. Returns true if a frame of audio was queued for encoding.
	static bool tick(MumbleClient *client, uint32_t frame_ms) {
		client->audio_tick_frames = frame_ms;
		uint32_t before = client->audio_sequence;
		client->cleanReclaimed();
		client->encodeEvent();
		client->sendEvent();
		return client->audio_sequence != before;
	}
};

/*
 * Helpers
 */

// The amplitude of the test tone, and what it should sound like after the client's default 0.5 volume
constexpr double TONE = 0.5;
constexpr double TONE_RMS = TONE * 0.5 / std::sqrt(2.0);

// Write a 440Hz stereo tone as a 48kHz float WAV file, returning its path
inline std::string write_tone(double seconds, int samplerate = 48000) {
	char path[] = "/tmp/mumble-audio-test-XXXXXX";
	int fd = mkstemp(path);
	close(fd);

	SF_INFO info = {};
	info.samplerate = samplerate;
	info.channels = 2;
	info.format = SF_FORMAT_WAV | SF_FORMAT_FLOAT;
	SNDFILE *file = sf_open(path, SFM_WRITE, &info);

	sf_count_t frames = (sf_count_t)(seconds * samplerate);
	std::vector<float> pcm(frames * 2);
	for (sf_count_t i = 0; i < frames; i++) {
		pcm[i * 2] = pcm[i * 2 + 1] = (float)(TONE * std::sin(2 * M_PI * 440.0 * i / samplerate));
	}
	sf_writef_float(file, pcm.data(), frames);
	sf_close(file);
	return path;
}

// A self signed certificate for the fake server end of the connection
inline void make_certificate(EVP_PKEY **pkey, X509 **cert) {
	*pkey = EVP_RSA_gen(2048);
	*cert = X509_new();
	ASN1_INTEGER_set(X509_get_serialNumber(*cert), 1);
	X509_gmtime_adj(X509_getm_notBefore(*cert), 0);
	X509_gmtime_adj(X509_getm_notAfter(*cert), 3600);
	X509_set_pubkey(*cert, *pkey);
	X509_NAME *name = X509_get_subject_name(*cert);
	X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC, (const unsigned char*) "test", -1, -1, 0);
	X509_set_issuer_name(*cert, name);
	X509_sign(*cert, *pkey, EVP_sha256());
}

// A TCP packet the client sent
struct SentPacket {
	uint16_t type;
	std::vector<uint8_t> body;
};

// A voice packet the client sent, decoded
struct SentAudio {
	uint64_t sequence = 0;
	bool terminator = false;
	uint32_t target = 0;
	size_t opus_len = 0;
	int samples = 0;   // Decoded samples per channel
	double rms = 0;    // Of the decoded audio
};

/*
 * Fixture
 */

class ClientFixture {
public:
	explicit ClientFixture(bool legacy = false) : legacy(legacy) {
		L = luaL_newstate();
		luaL_openlibs(L);
		lua_pushcfunction(L, luaopen_mumble);
		lua_call(L, 0, 1);
		lua_setglobal(L, "mumble");

		lua(legacy ? "client = mumble.client(1, 4, 0)" : "client = mumble.client()");
		lua("ends = 0 client:hook('OnAudioStreamEnd', function() ends = ends + 1 end)");

		lua_getglobal(L, "client");
		client = (MumbleClient*) lua_touserdata(L, -1);
		// Hooks only run for clients registered as connected
		client->self = LuaRegistry::refIn(L, LuaRegistry::clients);

		connect();

		int error;
		client->encoder = new MumbleOpusEncoder();
		client->encoder->init(AUDIO_SAMPLE_RATE, AUDIO_PLAYBACK_CHANNELS);
		client->decoder = opus_decoder_create(AUDIO_SAMPLE_RATE, AUDIO_PLAYBACK_CHANNELS, &error);
		client->session = 1;
		ClientTestAccess::open(client);
		client->connecting = client->connected = client->synced = true;

		decoder = opus_decoder_create(AUDIO_SAMPLE_RATE, AUDIO_PLAYBACK_CHANNELS, &error);
	}

	~ClientFixture() {
		client->disconnect("test finished", true);
		uv_run(uv_default_loop(), UV_RUN_NOWAIT);
		// The client's audio threads run until it's collected, so only free the encoder after that
		MumbleOpusEncoder *encoder = client->encoder;

		// The client closed its end, so the reader sees EOF
		reader.join();
		SSL_free(server);
		SSL_CTX_free(server_context);
		close(server_fd);
		close(client_fd);
		opus_decoder_destroy(decoder);
		lua_close(L);
		delete encoder;

		for (const std::string &path : files) {
			unlink(path.c_str());
		}
	}

	// Run Lua code, failing the test on errors
	void lua(const std::string &code) {
		if (luaL_dostring(L, code.c_str()) != 0) {
			failures++;
			fprintf(stderr, "  lua error: %s\n", lua_tostring(L, -1));
			lua_pop(L, 1);
		}
	}

	double number(const std::string &expression) {
		lua("__result = " + expression);
		lua_getglobal(L, "__result");
		double value = lua_tonumber(L, -1);
		lua_pop(L, 1);
		return value;
	}

	bool boolean(const std::string &expression) {
		lua("__result = " + expression);
		lua_getglobal(L, "__result");
		bool value = lua_toboolean(L, -1);
		lua_pop(L, 1);
		return value;
	}

	// Write a tone and open it as the global Lua variable "name"
	void open(const std::string &name, double seconds, int samplerate = 48000) {
		std::string path = write_tone(seconds, samplerate);
		files.push_back(path);
		lua(name + " = assert(client:openAudio('" + path + "'))");
	}

	// Run the playback timer in real time
	void run(double seconds) {
		auto next = std::chrono::steady_clock::now();
		auto end = next + std::chrono::milliseconds((int)(seconds * 1000));
		uint32_t scheduled = client->audio_frames;
		while (next < end) {
			next += std::chrono::milliseconds(scheduled);
			std::this_thread::sleep_until(next);
			// Like the playback thread, schedule the next wait before the main thread runs this tick
			uint32_t waited = scheduled;
			scheduled = client->audio_frames;
			if (ClientTestAccess::tick(client, waited)) {
				ticks.push_back(true);
				queued_ms.push_back(waited);
			} else {
				ticks.push_back(false);
			}
		}
		// Give the reader a moment to receive what was sent
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
	}

	// Every packet of a type the client has sent so far
	std::vector<std::vector<uint8_t>> sentOfType(uint16_t type) {
		std::lock_guard<std::mutex> guard(lock);
		std::vector<std::vector<uint8_t>> result;
		for (const SentPacket &packet : all) {
			if (packet.type == type) result.push_back(packet.body);
		}
		return result;
	}

	// Wait for the reader to see what was sent
	void settle() {
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
	}

	std::vector<SentAudio> sent() {
		std::lock_guard<std::mutex> guard(lock);
		return packets;
	}

	// Packets that carry audio, ignoring an empty terminator
	std::vector<SentAudio> audio() {
		std::vector<SentAudio> result;
		for (const SentAudio &packet : sent()) {
			if (packet.samples > 0) result.push_back(packet);
		}
		return result;
	}

	// Ticks with no audio between the first and last ticks that had audio
	int gaps() const {
		int first = -1, last = -1;
		for (size_t i = 0; i < ticks.size(); i++) {
			if (ticks[i]) {
				if (first < 0) first = i;
				last = i;
			}
		}
		int count = 0;
		for (int i = first; i >= 0 && i < last; i++) {
			if (!ticks[i]) count++;
		}
		return count;
	}

	int terminators() {
		int count = 0;
		for (const SentAudio &packet : sent()) {
			if (packet.terminator) count++;
		}
		return count;
	}

	lua_State *L;
	MumbleClient *client;
	std::vector<bool> ticks;
	// How long the timer waited before each tick that queued audio, in the order they were queued
	std::vector<uint32_t> queued_ms;

private:
	void connect() {
		static EVP_PKEY *pkey = nullptr;
		static X509 *cert = nullptr;
		if (!pkey) make_certificate(&pkey, &cert);

		int fds[2];
		socketpair(AF_UNIX, SOCK_STREAM, 0, fds);
		server_fd = fds[0];
		client_fd = fds[1];

		server_context = SSL_CTX_new(TLS_server_method());
		SSL_CTX_use_certificate(server_context, cert);
		SSL_CTX_use_PrivateKey(server_context, pkey);
		server = SSL_new(server_context);
		SSL_set_fd(server, server_fd);

		client->ssl_context = SSL_CTX_new(TLS_client_method());
		client->ssl = SSL_new(client->ssl_context);
		SSL_set_fd(client->ssl, client_fd);

		std::thread accept([this]() { SSL_accept(server); });
		SSL_connect(client->ssl);
		accept.join();

		reader = std::thread([this]() { read(); });
	}

	bool readExactly(uint8_t *data, size_t length) {
		size_t done = 0;
		while (done < length) {
			int ret = SSL_read(server, data + done, length - done);
			if (ret <= 0) return false;
			done += ret;
		}
		return true;
	}

	// Collect every voice packet the client tunnels to us
	void read() {
		uint8_t header[6];
		while (readExactly(header, sizeof(header))) {
			uint16_t type = (header[0] << 8) | header[1];
			uint32_t length = (header[2] << 24) | (header[3] << 16) | (header[4] << 8) | header[5];
			std::vector<uint8_t> body(length);
			if (!readExactly(body.data(), length)) break;
			std::lock_guard<std::mutex> guard(lock);
			all.push_back({type, body});
			if (type == PACKET_UDPTUNNEL && length > 0) {
				packets.push_back(parse(body));
			}
		}
	}

	SentAudio parse(const std::vector<uint8_t> &body) {
		SentAudio packet;
		const uint8_t *opus = nullptr;

		if (legacy) {
			packet.target = body[0] & 0x1F;
			CHECK_EQ(body[0] >> 5, LEGACY_UDP_OPUS);
			int read = 1;
			packet.sequence = VarInt::decode(body.data() + read, &read);
			uint64_t frame_header = VarInt::decode(body.data() + read, &read);
			packet.opus_len = frame_header & 0x1FFF;
			packet.terminator = (frame_header & 0x2000) != 0;
			opus = body.data() + read;
		} else {
			CHECK_EQ(body[0], PROTO_UDP_AUDIO);
			MumbleUDP__Audio *audio = mumble_udp__audio__unpack(NULL, body.size() - 1, body.data() + 1);
			CHECK(audio != NULL);
			if (!audio) return packet;
			packet.sequence = audio->frame_number;
			packet.terminator = audio->is_terminator;
			packet.target = audio->target;
			packet.opus_len = audio->opus_data.len;
			scratch.assign(audio->opus_data.data, audio->opus_data.data + audio->opus_data.len);
			opus = scratch.data();
			mumble_udp__audio__free_unpacked(audio, NULL);
		}

		if (packet.opus_len > 0) {
			float pcm[5760 * 2];
			packet.samples = opus_decode_float(decoder, opus, packet.opus_len, pcm, 5760, 0);
			double sum = 0;
			for (int i = 0; i < packet.samples * 2; i++) sum += pcm[i] * pcm[i];
			packet.rms = packet.samples > 0 ? std::sqrt(sum / (packet.samples * 2)) : 0;
		}
		return packet;
	}

	bool legacy;
	int server_fd = -1;
	int client_fd = -1;
	SSL_CTX *server_context = nullptr;
	SSL *server = nullptr;
	OpusDecoder *decoder = nullptr;
	std::thread reader;
	std::mutex lock;
	std::vector<SentAudio> packets;
	std::vector<SentPacket> all;
	std::vector<uint8_t> scratch;
	std::vector<std::string> files;
};

