// Tests for the client's audio pipeline (client/audio.cpp), driven through ClientFixture

#include "fixture.h"

#include "net/crypt.h"

#include <csignal>
#include <optional>

// Every audio packet except the first and last should carry the tone at about the expected level
static void check_levels(const std::vector<SentAudio> &audio, double expected) {
	for (size_t i = 1; i + 1 < audio.size(); i++) {
		CHECK_NEAR(audio[i].rms, expected, expected * 0.25);
	}
}

// Sequence numbers count 10ms frames, so each packet advances by how many it holds
static void check_sequence(const std::vector<SentAudio> &packets, int frame_ms) {
	for (size_t i = 1; i < packets.size(); i++) {
		CHECK_EQ(packets[i].sequence - packets[i - 1].sequence, (uint64_t)(frame_ms / 10));
	}
}

/*
 * Audio files
 */

TEST(audio_file_streams_every_frame) {
	ClientFixture f;
	f.open("stream", 0.5);
	f.lua("stream:play()");
	f.run(1.0);

	std::vector<SentAudio> audio = f.audio();
	CHECK(audio.size() >= 24 && audio.size() <= 27); // 0.5s of 20ms frames
	for (const SentAudio &packet : audio) {
		CHECK_EQ(packet.samples, 960);
		CHECK_EQ(packet.target, 0u);
	}
	check_levels(audio, TONE_RMS);
	CHECK_EQ(f.gaps(), 0);
}

TEST(audio_file_sequence_counts_10ms_frames) {
	ClientFixture f;
	f.open("stream", 0.5);
	f.lua("stream:play()");
	f.run(1.0);
	check_sequence(f.sent(), 20);
}

TEST(audio_file_end_sends_terminator) {
	ClientFixture f;
	f.open("stream", 0.3);
	f.lua("stream:play()");
	f.run(0.8);

	std::vector<SentAudio> sent = f.sent();
	CHECK(!sent.empty());
	CHECK_EQ(f.terminators(), 1);
	CHECK(!sent.empty() && sent.back().terminator);
}

TEST(audio_file_end_hook_and_state) {
	ClientFixture f;
	f.open("stream", 0.3);
	f.lua("stream:play()");
	f.run(0.8);

	CHECK_EQ(f.number("ends"), 1.0);
	CHECK(!f.boolean("stream:isPlaying()"));
}

TEST(audio_file_resampled) {
	ClientFixture f;
	f.open("stream", 0.5, 44100);
	f.lua("stream:play()");
	f.run(1.0);

	std::vector<SentAudio> audio = f.audio();
	CHECK(audio.size() >= 24 && audio.size() <= 27);
	check_levels(audio, TONE_RMS);
	CHECK_EQ(f.gaps(), 0);
	CHECK_EQ(f.terminators(), 1);
}

TEST(audio_file_voice_target) {
	ClientFixture f;
	f.open("stream", 0.2);
	f.lua("client:setVoiceTarget(5) stream:play()");
	f.run(0.5);
	CHECK(!f.sent().empty());
	for (const SentAudio &packet : f.sent()) {
		CHECK_EQ(packet.target, 5u);
	}
}

TEST(audio_packet_sizes) {
	for (int ms : {10, 20, 40, 60}) {
		ClientFixture f;
		f.open("stream", 0.6);
		f.lua("client:setAudioPacketSize(" + std::to_string(ms) + ") stream:play()");
		f.run(1.0);

		std::vector<SentAudio> audio = f.audio();
		CHECK(!audio.empty());
		for (const SentAudio &packet : audio) {
			CHECK_EQ(packet.samples, ms * 48);
		}
		CHECK_NEAR((double) audio.size(), 600.0 / ms, 2);
		check_sequence(f.sent(), ms);
		CHECK_EQ(f.terminators(), 1);
	}
}

TEST(audio_file_volume) {
	ClientFixture f;
	f.open("stream", 0.5);
	f.lua("stream:setVolume(0.5):play()");
	f.run(0.4);
	check_levels(f.audio(), TONE_RMS * 0.5);

	f.lua("client:setVolume(1)");
	size_t before = f.audio().size();
	f.run(0.2);
	std::vector<SentAudio> audio = f.audio();
	audio.erase(audio.begin(), audio.begin() + before + 1);
	check_levels(audio, TONE_RMS);
}

TEST(audio_files_mix) {
	ClientFixture f;
	f.open("a", 0.5);
	f.open("b", 0.5);
	f.lua("a:play() b:play()");
	f.run(0.4);
	// The same tone in phase twice is twice as loud
	check_levels(f.audio(), TONE_RMS * 2);
}

TEST(audio_file_loops_without_gaps) {
	ClientFixture f;
	f.open("stream", 0.2);
	f.lua("stream:setLooping(2):play()");
	f.run(1.2);

	std::vector<SentAudio> audio = f.audio();
	CHECK(audio.size() >= 29 && audio.size() <= 32); // Played 3 times
	CHECK_EQ(f.gaps(), 0);
	CHECK_EQ(f.number("ends"), 1.0);
	CHECK_EQ(f.terminators(), 1);
	check_sequence(f.sent(), 20);
}

TEST(audio_file_fade_out_stops) {
	ClientFixture f;
	f.open("stream", 2.0);
	f.lua("stream:play()");
	f.run(0.2);
	f.lua("stream:fadeOut(0.1)");
	f.run(0.8);

	CHECK(!f.boolean("stream:isPlaying()"));
	CHECK_EQ(f.number("ends"), 1.0);
	CHECK(f.audio().size() < 30); // Stopped well before the 2s file ended
	CHECK_EQ(f.terminators(), 1);
}

TEST(audio_file_fade_out_stops_looping) {
	ClientFixture f;
	f.open("stream", 0.3);
	f.lua("stream:setLooping(true):play()");
	f.run(0.2);
	f.lua("stream:fadeOut(0.1)");
	f.run(1.0);

	CHECK(!f.boolean("stream:isPlaying()"));
	CHECK_EQ(f.number("ends"), 1.0);
	CHECK_EQ(f.terminators(), 1);
}

TEST(audio_file_fade_to) {
	ClientFixture f;
	f.open("stream", 1.0);
	f.lua("stream:play()");
	f.run(0.1);
	f.lua("stream:fadeTo(0.5, 0.1)");
	f.run(0.4);

	// The fade finished, so the end of what we sent is at half volume, and the stream keeps going
	std::vector<SentAudio> audio = f.audio();
	CHECK(audio.size() > 5);
	if (audio.size() > 5) {
		CHECK_NEAR(audio[audio.size() - 2].rms, TONE_RMS * 0.5, TONE_RMS * 0.15);
	}
	CHECK(f.boolean("stream:isPlaying()"));
}

TEST(audio_file_fade_out_instantly) {
	ClientFixture f;
	f.open("stream", 2.0);
	f.lua("stream:play()");
	f.run(0.2);
	f.lua("stream:fadeOut(0)");
	f.run(0.2);

	CHECK(!f.boolean("stream:isPlaying()"));
	CHECK_EQ(f.number("ends"), 1.0);
}

TEST(audio_file_fade_to_instantly) {
	ClientFixture f;
	f.open("stream", 1.0);
	f.lua("stream:play()");
	f.run(0.1);
	f.lua("stream:fadeTo(0.5, 0)");
	size_t before = f.audio().size();
	f.run(0.2);

	std::vector<SentAudio> audio = f.audio();
	audio.erase(audio.begin(), audio.begin() + before + 1);
	check_levels(audio, TONE_RMS * 0.5);
	CHECK(f.boolean("stream:isPlaying()"));
}

TEST(audio_file_seek_is_immediate) {
	ClientFixture f;
	f.open("stream", 2.0);
	f.lua("stream:play()");
	f.run(0.2);
	// Jump to 100ms before the end, instead of playing out what was already buffered
	f.lua("stream:seek('set', 48000 * 1.9)");
	f.run(0.3);

	CHECK_EQ(f.number("ends"), 1.0);
}

TEST(audio_file_pause_and_resume) {
	ClientFixture f;
	f.open("stream", 1.0);
	f.lua("stream:play()");
	f.run(0.3);
	f.lua("stream:pause()");
	f.run(0.1); // Lets the frame already being encoded go out
	size_t before_pause = f.audio().size();
	f.run(0.3);
	CHECK_EQ(f.audio().size(), before_pause); // Nothing while paused
	f.lua("stream:play()");
	f.run(1.2);

	// Resuming continues where we paused, so the whole file plays once
	CHECK_NEAR((double) f.audio().size(), 50, 3);
	CHECK_EQ(f.number("ends"), 1.0);
}

TEST(audio_file_pause_sends_terminator) {
	ClientFixture f;
	f.open("stream", 1.0);
	f.lua("stream:play()");
	f.run(0.3);
	f.lua("stream:pause()");
	f.run(0.2);
	CHECK_EQ(f.terminators(), 1);
	CHECK(f.sent().back().terminator);
}

TEST(audio_file_stop_sends_terminator) {
	ClientFixture f;
	f.open("stream", 1.0);
	f.lua("stream:play()");
	f.run(0.3);
	f.lua("stream:stop()");
	f.run(0.2);
	CHECK_EQ(f.terminators(), 1);
	CHECK(f.sent().back().terminator);
	CHECK_EQ(f.number("ends"), 0.0); // Stopping isn't the stream ending
}

TEST(audio_streams_play_back_to_back) {
	ClientFixture f;
	f.open("a", 0.3);
	f.open("b", 0.3);
	f.lua("client:hook('OnAudioStreamEnd', 'next', function(c, s) if s == a then b:play() end end) a:play()");
	f.run(1.2);

	CHECK_EQ(f.number("ends"), 2.0);
	std::vector<SentAudio> sent = f.sent();
	for (size_t i = 1; i < sent.size(); i++) {
		CHECK(sent[i].sequence > sent[i - 1].sequence);
	}
}

TEST(audio_stream_replays_from_its_end_hook) {
	ClientFixture f;
	f.open("a", 0.2);
	f.lua("replays = 0 client:hook('OnAudioStreamEnd', 'again', function(c, s) if replays < 1 then replays = replays + 1 s:play() end end) a:play()");
	f.run(1.0);

	CHECK_EQ(f.number("ends"), 2.0);
}

TEST(audio_stream_stops_from_its_end_hook) {
	ClientFixture f;
	f.open("a", 0.2);
	// Stopping a stream that just ended used to deadlock on its mutex
	f.lua("client:hook('OnAudioStreamEnd', 'stop', function(c, s) s:stop() end) a:play()");
	f.run(0.6);

	CHECK_EQ(f.number("ends"), 1.0);
	CHECK(!f.boolean("a:isPlaying()"));
}

TEST(audio_end_hook_stops_other_streams) {
	ClientFixture f;
	f.open("a", 0.2);
	f.open("b", 1.0);
	f.open("d", 1.0);
	// a's end hook stops the streams after it in the list, while they're being mixed
	f.lua("client:hook('OnAudioStreamEnd', 'others', function(c, s) if s == a then b:stop() d:stop() end end) a:play() b:play() d:play()");
	f.run(0.6);

	CHECK_EQ(f.number("ends"), 1.0);
	CHECK(!f.boolean("b:isPlaying()"));
	CHECK(!f.boolean("d:isPlaying()"));
}

TEST(audio_packet_size_changes_mid_stream) {
	ClientFixture f;
	f.open("stream", 2.0);
	// Change the packet size while the timer is already waiting, both up and down
	f.lua(R"(
		count = 0
		client:hook('OnAudioStream', function()
			count = count + 1
			if count == 10 then client:setAudioPacketSize(40) end
			if count == 20 then client:setAudioPacketSize(10) end
		end)
		stream:play()
	)");
	f.run(1.2);

	// Every packet holds exactly as much audio as the timer waited for it, so none were sent early or left a gap
	std::vector<SentAudio> sent = f.sent();
	CHECK(sent.size() > 30);
	for (size_t i = 0; i < sent.size() && i < f.queued_ms.size(); i++) {
		CHECK_EQ(sent[i].samples, (int) f.queued_ms[i] * AUDIO_SAMPLE_RATE / 1000);
	}
	CHECK_EQ(f.gaps(), 0);
}

TEST(audio_tunnels_packets_udp_cannot_send) {
	ClientFixture f;
	// Sending over UDP, but without a crypt key, so every UDP send fails
	f.client->crypt = new CryptState();
	f.client->tcp_udp_tunnel = false;
	f.open("stream", 0.5);
	f.lua("stream:play()");
	f.run(0.4);

	// Nothing was lost, since every packet that couldn't go over UDP went through TCP instead.
	// Like Mumble, failing to send doesn't change our mode though, only our TCP pings do.
	CHECK(!f.client->tcp_udp_tunnel);
	std::vector<SentAudio> audio = f.audio();
	CHECK(audio.size() >= 15);
	check_levels(audio, TONE_RMS);
	CHECK_EQ(f.gaps(), 0);
}

// Give the client's crypt state `good` successfully decrypted packets from a server
static void decrypt_packets(MumbleClient *client, int good) {
	CryptState server;
	server.genKey();
	client->crypt->setKey(server.getRawKey(), AES_KEY_SIZE_BYTES,
	                      server.getDecryptIV(), AES_BLOCK_SIZE,
	                      server.getEncryptIV(), AES_BLOCK_SIZE);
	for (int i = 0; i < good; i++) {
		std::optional<std::string> packet = server.encrypt((const uint8_t*) "ping", 4);
		client->crypt->decrypt((const uint8_t*) packet->data(), packet->size());
	}
}

TEST(udp_mode_waits_before_falling_back) {
	ClientFixture f;
	f.client->crypt = new CryptState();
	f.client->tcp_udp_tunnel = false;
	f.client->time = uv_now(uv_default_loop());

	// Nothing over UDP yet, but we only just connected
	f.client->updateUDPMode(0);
	CHECK(!f.client->tcp_udp_tunnel);

	// Still nothing after 20 seconds
	f.client->time -= UDP_TCP_FALLBACK_MS + 1;
	f.client->updateUDPMode(0);
	CHECK(f.client->tcp_udp_tunnel);
}

TEST(udp_mode_falls_back_when_either_side_gets_nothing) {
	ClientFixture f;
	f.client->crypt = new CryptState();
	f.client->time = uv_now(uv_default_loop()) - UDP_TCP_FALLBACK_MS - 1;
	decrypt_packets(f.client, 10);

	// We hear the server, but the server never heard us
	f.client->tcp_udp_tunnel = false;
	f.client->updateUDPMode(0);
	CHECK(f.client->tcp_udp_tunnel);

	// Both sides are getting packets, so UDP stays
	f.client->tcp_udp_tunnel = false;
	f.client->updateUDPMode(10);
	CHECK(!f.client->tcp_udp_tunnel);
}

TEST(udp_mode_switches_back_once_both_sides_get_packets) {
	ClientFixture f;
	f.client->crypt = new CryptState();
	f.client->tcp_udp_tunnel = true;
	decrypt_packets(f.client, 3);

	// Three packets each way isn't enough
	f.client->updateUDPMode(3);
	CHECK(f.client->tcp_udp_tunnel);

	decrypt_packets(f.client, 4);
	f.client->updateUDPMode(4);
	CHECK(!f.client->tcp_udp_tunnel);
}

/*
 * Audio processes
 */

// A command that writes `seconds` of our test tone as raw float PCM
static std::string tone_command(double seconds, int samplerate = 48000, int channels = 2) {
	char script[256];
	snprintf(script, sizeof(script),
	         "import math, struct, sys; r = %d; c = %d; "
	         "sys.stdout.buffer.write(b''.join(struct.pack('<f', %f * math.sin(2 * math.pi * 440 * i / r)) * c for i in range(int(r * %f))))",
	         samplerate, channels, TONE, seconds);
	return std::string("{ 'python3', '-c', \"") + script + "\" }";
}

TEST(audio_process_streams_every_frame) {
	ClientFixture f;
	f.lua("stream = assert(client:openAudioProcess(" + tone_command(0.5) + "))");
	f.run(0.1); // Let it buffer before we play
	f.lua("stream:play()");
	f.run(0.8);

	std::vector<SentAudio> audio = f.audio();
	CHECK(audio.size() >= 23 && audio.size() <= 27);
	check_levels(audio, TONE_RMS);
	CHECK_EQ(f.gaps(), 0);
	CHECK_EQ(f.terminators(), 1);
	CHECK_EQ(f.number("ends"), 1.0);
}

TEST(audio_process_resamples_and_mixes_to_stereo) {
	ClientFixture f;
	f.lua("stream = assert(client:openAudioProcess(" + tone_command(0.5, 22050, 1) + ", 22050, 1))");
	f.run(0.1);
	f.lua("stream:play()");
	f.run(0.8);

	std::vector<SentAudio> audio = f.audio();
	CHECK(audio.size() >= 23 && audio.size() <= 27);
	check_levels(audio, TONE_RMS);
	CHECK_EQ(f.number("ends"), 1.0);
}

TEST(audio_process_pipeline) {
	ClientFixture f;
	// The tone goes through cat, so we only hear it if the commands are piped together
	f.lua("stream = assert(client:openAudioProcess({ " + tone_command(0.5) + ", { 'cat' } }))");
	f.run(0.1);
	f.lua("stream:play()");
	f.run(0.8);

	std::vector<SentAudio> audio = f.audio();
	CHECK(audio.size() >= 23 && audio.size() <= 27);
	check_levels(audio, TONE_RMS);
	CHECK_EQ(f.number("ends"), 1.0);
}

TEST(audio_process_long_output_waits_for_us) {
	ClientFixture f;
	// Far more than our buffer holds, so the command has to wait while we play
	f.lua("stream = assert(client:openAudioProcess(" + tone_command(3) + "))");
	f.run(0.5);
	f.lua("stream:play()");
	f.run(1.0);

	std::vector<SentAudio> audio = f.audio();
	CHECK(audio.size() >= 45);
	check_levels(audio, TONE_RMS);
	CHECK_EQ(f.gaps(), 0);
	CHECK(f.boolean("stream:isPlaying()"));
}

TEST(audio_process_pauses_while_full) {
	ClientFixture f;
	// Endless output, which would pile up in memory if we kept reading it
	f.lua("stream = assert(client:openAudioProcess({ 'cat', '/dev/zero' }))");
	f.run(0.5);

	lua_getglobal(f.L, "stream");
	AudioStream *stream = (AudioStream*) lua_touserdata(f.L, -1);
	lua_pop(f.L, 1);

	// Our ring is full, and we stopped reading after at most one more read's worth
	CHECK_EQ(stream->ringSpace(), (size_t) 0);
	CHECK(stream->pending.size() < 65536);
	f.lua("stream:stop()");
}

TEST(audio_process_stop_kills_it_and_play_runs_it_again) {
	ClientFixture f;
	char pidfile[] = "/tmp/mumble-process-test-XXXXXX";
	close(mkstemp(pidfile));
	f.lua(std::string("stream = assert(client:openAudioProcess({ 'sh', '-c', 'echo $$ > ") + pidfile + "; exec cat /dev/zero' }))");
	f.lua("stream:play()");
	f.run(0.2);
	f.lua("stream:stop()");
	f.run(0.2);

	// Our command was killed
	FILE *file = fopen(pidfile, "r");
	int pid = 0;
	if (file) {
		CHECK(fscanf(file, "%d", &pid) == 1);
		fclose(file);
	}
	CHECK(pid > 0);
	CHECK(::kill(pid, 0) != 0);

	// Playing again runs it again
	f.lua("stream:play()");
	f.run(0.2);
	CHECK(f.boolean("stream:isPlaying()"));
	file = fopen(pidfile, "r");
	int again = 0;
	if (file) {
		CHECK(fscanf(file, "%d", &again) == 1);
		fclose(file);
	}
	CHECK(again > 0 && again != pid);
	f.lua("stream:stop()");
	unlink(pidfile);
}

TEST(audio_stream_paused_then_stopped_is_released) {
	ClientFixture f;
	char pidfile[] = "/tmp/mumble-process-test-XXXXXX";
	close(mkstemp(pidfile));
	f.lua(std::string("stream = assert(client:openAudioProcess({ 'sh', '-c', 'echo $$ > ") + pidfile + "; exec cat /dev/zero' }))");
	f.lua("weak = setmetatable({ stream }, { __mode = 'v' }) stream:play()");
	f.run(0.2);
	f.lua("stream:pause() stream:stop()");
	f.run(0.2);

	// Its command was killed
	FILE *file = fopen(pidfile, "r");
	int pid = 0;
	if (file) {
		CHECK(fscanf(file, "%d", &pid) == 1);
		fclose(file);
	}
	CHECK(pid > 0);
	CHECK(::kill(pid, 0) != 0);
	unlink(pidfile);

	// And nothing keeps it alive any more
	f.lua("stream = nil collectgarbage() collectgarbage()");
	f.lua("assert(weak[1] == nil, 'paused then stopped stream was never released')");
}

TEST(audio_process_errors) {
	ClientFixture f;
	f.lua("stream, err = client:openAudioProcess({ 'no-such-program-for-mumble' })");
	f.lua("assert(stream == nil and err:find('failed to run no-such-program-for-mumble', 1, true), err)");
	f.lua("assert(not pcall(client.openAudioProcess, client, {}))");
	f.lua("assert(not pcall(client.openAudioProcess, client, { 'cat', 12 }))");
	f.lua("assert(not pcall(client.openAudioProcess, client, { { 'cat' }, {} }))");

	// A command's output has no file behind it
	f.lua("stream = assert(client:openAudioProcess({ 'cat' }))");
	f.lua("assert(stream:getLength('seconds') == nil and stream:getInfo() == nil and stream:getTitle() == nil)");
	f.lua("assert(stream:seek('set', 0) == nil)");
	f.lua("assert(not pcall(stream.setLooping, stream, true))");
}

TEST(audio_oversized_packets_stop_streams) {
	ClientFixture f;
	f.client->encoder->set(OPUS_SET_BITRATE_REQUEST, 510000);
	f.open("stream", 1.0);
	f.lua("client:setAudioPacketSize(60) stream:play()");
	f.run(0.5);

	CHECK(!f.boolean("stream:isPlaying()"));
	CHECK(f.audio().empty());
}

/*
 * Audio buffers
 */

static opus_int32 adjusted_bitrate(ClientFixture &f, uint32_t max_bandwidth) {
	f.client->max_bandwidth = max_bandwidth;
	f.client->adjustAudioBandwidth();
	opus_int32 bitrate;
	f.client->encoder->get(OPUS_GET_BITRATE_REQUEST, &bitrate);
	return bitrate;
}

TEST(audio_bandwidth_unlimited_or_plenty) {
	ClientFixture f;
	CHECK_EQ(adjusted_bitrate(f, (uint32_t) -1), AUDIO_DEFAULT_BITRATE);
	CHECK_EQ(adjusted_bitrate(f, 558000), AUDIO_DEFAULT_BITRATE);
	CHECK_EQ(f.client->audio_frames.load(), 20u);
}

TEST(audio_bandwidth_lowers_bitrate) {
	ClientFixture f;
	// 20ms packets, allowing for TCP, cost (20 + 8 + 4 + 1 + 2 + 12 + 2) * 400 = 19600 bits/s of overhead
	CHECK_EQ(adjusted_bitrate(f, 72000), 52000);
	CHECK_EQ(f.client->audio_frames.load(), 20u);
}

TEST(audio_bandwidth_uses_bigger_packets) {
	ClientFixture f;
	// Low limits switch to 40ms packets, which (allowing for TCP) cost (20 + 8 + 4 + 1 + 2 + 12 + 4) * 200 = 10200 bits/s of overhead
	CHECK_EQ(adjusted_bitrate(f, 30000), 19000);
	CHECK_EQ(f.client->audio_frames.load(), 40u);
}

TEST(audio_bandwidth_bigger_packets_alone) {
	ClientFixture f;
	f.client->encoder->set(OPUS_SET_BITRATE_REQUEST, 32000);
	f.lua("client:setAudioPacketSize(10)");
	// 10ms packets need 32000 + 40000, but 20ms ones only 32000 + 19600, so only the packet size changes
	CHECK_EQ(adjusted_bitrate(f, 60000), 32000);
	CHECK_EQ(f.client->audio_frames.load(), 20u);
}

TEST(audio_buffer_streams_and_terminates) {
	ClientFixture f;
	f.lua(R"(
		buffer = client:createAudioBuffer(48000, 2)
		written = 0
		client:hook('OnAudioStream', function(c, rate, channels, frames)
			if written >= 25 then return end
			written = written + 1
			for i = 0, frames - 1 do
				local sample = 0.5 * math.sin(2 * math.pi * 440 * ((written - 1) * frames + i) / rate)
				buffer:writeFloat(sample)
				buffer:writeFloat(sample)
			end
		end)
	)");
	f.run(1.0);

	std::vector<SentAudio> audio = f.audio();
	CHECK_NEAR((double) audio.size(), 25, 2);
	check_levels(audio, TONE_RMS);
	CHECK_EQ(f.gaps(), 0);
	CHECK_EQ(f.terminators(), 1);
	check_sequence(f.sent(), 20);
}

/*
 * Legacy (pre 1.5) servers
 */

TEST(audio_legacy_packets) {
	ClientFixture f(true);
	CHECK(f.client->legacy);
	f.open("stream", 0.4);
	f.lua("client:setVoiceTarget(3) stream:play()");
	f.run(0.8);

	std::vector<SentAudio> audio = f.audio();
	CHECK(audio.size() >= 19 && audio.size() <= 22);
	check_levels(audio, TONE_RMS);
	check_sequence(f.sent(), 20);
	CHECK_EQ(f.terminators(), 1);
	CHECK(f.sent().back().terminator);
	for (const SentAudio &packet : f.sent()) {
		CHECK_EQ(packet.target, 3u);
	}
}
