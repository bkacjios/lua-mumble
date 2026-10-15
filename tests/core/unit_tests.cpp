// Unit tests for the standalone C++ classes

#include "test.h"

#include "audio/audiocontext.h"
#include "audio/audioqueue.h"
#include "audio/pcm.h"
#include "audio/resampler.h"
#include "net/crypt.h"
#include "net/varint.h"
#include "net/voicepacket.h"
#include "util/buffer.h"
#include "util/util.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

/*
 * VarInt
 */

TEST(varint_roundtrip_and_sizes) {
	const struct { uint64_t value; int size; } cases[] = {
		{0, 1}, {0x7F, 1},
		{0x80, 2}, {0x3FFF, 2},
		{0x4000, 3}, {0x1FFFFF, 3},
		{0x200000, 4}, {0xFFFFFFF, 4},
		{0x10000000, 5}, {0xFFFFFFFFULL, 5},
		{0x100000000ULL, 9}, {0x0123456789ABCDEFULL, 9},
	};

	for (const auto &c : cases) {
		uint8_t buffer[16] = {0};
		CHECK_EQ((int) VarInt::encode(buffer, c.value), c.size);

		int len = 0;
		CHECK_EQ(VarInt::decode(buffer, &len), c.value);
		CHECK_EQ(len, c.size);
	}
}

TEST(varint_decode_accumulates_length) {
	uint8_t buffer[16];
	int offset = VarInt::encode(buffer, 5);
	VarInt::encode(buffer + offset, 300);

	int len = 0;
	CHECK_EQ(VarInt::decode(buffer, &len), 5ULL);
	CHECK_EQ(VarInt::decode(buffer + len, &len), 300ULL);
	CHECK_EQ(len, 3);
}

/*
 * VoicePacket
 */

TEST(voicepacket_opus_frame) {
	uint8_t buffer[64];
	VoicePacket packet(buffer);
	const uint8_t opus[] = {0xfc, 0xff, 0xfe};

	CHECK_EQ(packet.setHeader(4, 3, 300), 1);
	CHECK_EQ(buffer[0], (uint8_t) ((4 << 5) | 3));
	CHECK_EQ(packet.header_length, 3); // 1 header byte + 2 byte sequence

	CHECK_EQ(packet.setFrame(4, (1 << 13) | sizeof(opus), opus, sizeof(opus)), 1);
	// Opus frame headers are varints: 0x2003 needs 2 bytes
	CHECK_EQ(packet.getLength(), 3 + 2 + 3);

	int len = 0;
	CHECK_EQ(VarInt::decode(buffer + 1, &len), 300ULL);
	len = 0;
	CHECK_EQ(VarInt::decode(buffer + 3, &len), (uint64_t) ((1 << 13) | 3));
	CHECK(memcmp(buffer + 5, opus, sizeof(opus)) == 0);
}

TEST(voicepacket_speex_frame_uses_single_byte_header) {
	uint8_t buffer[64];
	VoicePacket packet(buffer);
	const uint8_t data[] = {1, 2};

	packet.setHeader(2, 0, 1);
	CHECK_EQ(packet.setFrame(2, 0x82, data, sizeof(data)), 1);
	CHECK_EQ(buffer[packet.header_length], 0x82);
	CHECK_EQ(packet.getLength(), packet.header_length + 1 + 2);
}

TEST(voicepacket_rejects_invalid_use) {
	uint8_t buffer[64];
	const uint8_t data[] = {1};

	VoicePacket noheader(buffer);
	CHECK_EQ(noheader.setFrame(4, 1, data, 1), -2);

	VoicePacket packet(buffer);
	packet.setHeader(4, 0, 0);
	CHECK_EQ(packet.setHeader(4, 0, 0), -2);
	CHECK_EQ(packet.setFrame(4, 0, data, 0), -1);
	CHECK_EQ(packet.setFrame(4, 0, nullptr, 1), -1);
	CHECK_EQ(packet.setFrame(4, 0, data, 0x2000), -1);
	CHECK_EQ(packet.setFrame(7, 0, data, 1), -3); // Unknown codec
}

/*
 * Pcm
 */

TEST(pcm_mono_is_duplicated) {
	const float mono[] = {0.1f, -0.5f};
	std::vector<float> out = Pcm::toStereo(mono, 2, 1);
	CHECK_EQ(out.size(), (size_t) 4);
	CHECK_EQ(out[0], 0.1f);
	CHECK_EQ(out[1], 0.1f);
	CHECK_EQ(out[2], -0.5f);
	CHECK_EQ(out[3], -0.5f);
}

TEST(pcm_stereo_is_copied) {
	const float stereo[] = {0.1f, 0.2f, 0.3f, 0.4f};
	std::vector<float> out = Pcm::toStereo(stereo, 2, 2);
	CHECK(out == std::vector<float>(stereo, stereo + 4));
}

TEST(pcm_multichannel_is_downmixed) {
	// 4 channels: even channels go left, odd go right, averaged over channels / 2
	const float quad[] = {0.2f, 0.4f, 0.6f, 0.8f};
	std::vector<float> out = Pcm::toStereo(quad, 1, 4);
	CHECK_EQ(out.size(), (size_t) 2);
	CHECK_NEAR(out[0], (0.2 + 0.6) / 2, 1e-6);
	CHECK_NEAR(out[1], (0.4 + 0.8) / 2, 1e-6);
}

/*
 * Resampler
 */

TEST(resampler_passthrough_at_same_rate) {
	int error = 0;
	Resampler resampler(SRC_SINC_FASTEST, 2, &error);
	CHECK(resampler.isValid());

	const float input[] = {0.1f, 0.2f, 0.3f, 0.4f};
	std::vector<float> output;
	CHECK_EQ(resampler.process(input, 2, 1.0, false, output), 2);
	CHECK(output == std::vector<float>(input, input + 4));
}

TEST(resampler_converts_rate) {
	int error = 0;
	Resampler resampler(SRC_SINC_FASTEST, 2, &error);

	// One second of 44.1kHz silence becomes about one second of 48kHz audio once flushed
	std::vector<float> input(44100 * 2, 0.0f);
	std::vector<float> output;
	int frames = resampler.process(input.data(), 44100, 48000.0 / 44100.0, true, output);
	CHECK(frames > 47000 && frames <= (int) ceil(44100 * (48000.0 / 44100.0)));
	CHECK(output.size() >= (size_t) frames * 2);
	CHECK_EQ(resampler.reset(), 0);
}

TEST(resampler_drains_everything_when_flushed) {
	int error = 0;
	Resampler resampler(SRC_SINC_FASTEST, 2, &error);
	double ratio = 48000.0 / 11025.0;

	// Feed one second in 20ms chunks, then flush with no input, like a stream ending exactly on a read
	std::vector<float> input(220 * 2, 0.5f);
	std::vector<float> output;
	long total = 0;
	for (int i = 0; i < 50; i++) {
		total += resampler.process(input.data(), 220, ratio, false, output);
	}
	total += resampler.process(NULL, 0, ratio, true, output);

	// Nothing is left behind in the converter
	CHECK_NEAR((double) total, 50 * 220 * ratio, 2);
}

TEST(audio_context_reads_whole_frames) {
	int error = 0;
	std::unique_ptr<Resampler> resampler(new Resampler(SRC_SINC_FASTEST, 2, &error));
	AudioContext context(nullptr, 11025, 1, std::move(resampler));

	// One second of 11.025kHz audio, which isn't a whole number of frames per 20ms
	ByteBuffer buffer;
	buffer.init(1024);
	for (int i = 0; i < 11025; i++) {
		buffer.writeValue<float>(0.5);
	}

	std::vector<float> output;
	int whole = 0;
	long total = 0;
	for (int tick = 0; tick < 60; tick++) {
		int frames = context.read(&buffer, 20, 1.0f, output);
		if (frames == 960) whole++;
		total += frames;
	}

	// Every frame is whole until the audio runs out, apart from the resampler warming up
	CHECK(whole >= 48);
	CHECK_NEAR((double) total, 48000, 2);
	buffer.free();
}

TEST(resampler_invalid_quality) {
	int error = 0;
	Resampler resampler(12345, 2, &error);
	CHECK(!resampler.isValid());
	CHECK(error != 0);
}

/*
 * util
 */

TEST(bin_to_strhex_formats_bytes) {
	CHECK(bin_to_strhex("\x01\xab\xff", 3) == "01abff");
	CHECK(bin_to_strhex(std::string("\0\x10", 2)) == "0010");
	CHECK(bin_to_strhex("", 0) == "");
}

TEST(linklist_keeps_newest_first) {
	int a = 1, b = 2, c = 3;
	LinkList<int> list;
	list.add(1, &a);
	list.add(2, &b);
	list.add(3, &c);

	std::vector<uint32_t> order;
	for (auto &node : list) order.push_back(node.index);
	CHECK((order == std::vector<uint32_t>{3, 2, 1}));
	CHECK_EQ(list.count(), (size_t) 3);
	CHECK(list.get(2) == &b);
	CHECK(list.get(9) == nullptr);
	CHECK(list.contains(1));
	CHECK(list.find(&c) == list.begin());
	CHECK(list.front().data == &c);
}

TEST(linklist_removes_first_match_only) {
	int a = 1, b = 2;
	LinkList<int> list;
	list.add(7, &a);
	list.add(7, &b);

	list.remove(7);
	CHECK_EQ(list.count(), (size_t) 1);
	CHECK(list.get(7) == &a);

	list.removeData(&a);
	CHECK(list.empty());

	list.remove(42); // No-op on missing entries
	list.add(1, nullptr);
	list.clear();
	CHECK(list.empty());
}

TEST(messagequeue_is_fifo_and_binary_safe) {
	MessageQueue queue;
	CHECK(queue.empty());
	queue.push(std::string("a\0b", 3));
	queue.push("second");
	CHECK(queue.pop() == std::string("a\0b", 3));
	CHECK(queue.pop() == "second");
	CHECK(queue.empty());
}

/*
 * ByteBuffer
 */

TEST(bytebuffer_write_read_values) {
	ByteBuffer buffer;
	CHECK(buffer.init(4) == &buffer);

	CHECK_EQ(buffer.writeByte(200), 1);
	CHECK_EQ(buffer.writeValue<int16_t>(-1234), 2);
	CHECK_EQ(buffer.writeValue<int32_t>(-123456789), 4);
	CHECK_EQ(buffer.writeValue<float>(1.5), 4);
	CHECK_EQ(buffer.writeValue<double>(M_PI), 8);
	CHECK(buffer.capacity >= 19); // Grew past the initial 4 bytes
	CHECK_EQ(buffer.length(), 19ULL);

	uint8_t byte; int16_t s; int32_t i; float f; double d;
	CHECK_EQ(buffer.readByte(&byte), 1);
	CHECK_EQ(buffer.readValue(&s), 2);
	CHECK_EQ(buffer.readValue(&i), 4);
	CHECK_EQ(buffer.readValue(&f), 4);
	CHECK_EQ(buffer.readValue(&d), 8);
	CHECK_EQ(byte, 200);
	CHECK_EQ(s, -1234);
	CHECK_EQ(i, -123456789);
	CHECK_EQ(f, 1.5f);
	CHECK_EQ(d, M_PI);
	CHECK(buffer.isEmpty());

	// Reading past the end fails without moving the read head
	CHECK_EQ(buffer.readByte(&byte), 0);
	CHECK_EQ(buffer.read_head, 19ULL);
	buffer.free();
}

TEST(bytebuffer_varints) {
	const uint64_t values[] = {0, 127, 128, 0x3FFF, 0x4000, 0x200000, 0x10000000, 0xFFFFFFFFULL, 0x100000000ULL,
	                           (uint64_t) -1, (uint64_t) -4, (uint64_t) -5, (uint64_t) -100000};
	ByteBuffer buffer;
	buffer.init(1);

	for (uint64_t value : values) {
		buffer.writeVarInt(value);
	}
	for (uint64_t value : values) {
		uint64_t out;
		CHECK(buffer.readVarInt(&out) > 0);
		CHECK_EQ(out, value);
	}

	uint64_t out;
	CHECK_EQ(buffer.readVarInt(&out), 0);
	buffer.free();
}

TEST(bytebuffer_pack_reset_shrink) {
	ByteBuffer buffer;
	buffer.initData("abcdef", 6);
	CHECK_EQ(buffer.length(), 6ULL);

	char out[2];
	buffer.read(out, 2);
	buffer.pack();
	CHECK_EQ(buffer.read_head, 0ULL);
	CHECK_EQ(buffer.write_head, 4ULL);
	CHECK(memcmp(buffer.data, "cdef", 4) == 0);

	buffer.write("0123456789", 10);
	CHECK(buffer.capacity > 6);
	CHECK_EQ(buffer.shrink(), 1);
	CHECK_EQ(buffer.capacity, 6ULL);

	buffer.reset();
	CHECK(buffer.isEmpty());
	buffer.free();
	CHECK(buffer.data == nullptr);
}

/*
 * CryptState
 */

static void crypt_pair(CryptState &server, CryptState &client) {
	server.genKey();
	CHECK(client.setKey(server.getRawKey(), AES_KEY_SIZE_BYTES,
	                    server.getDecryptIV(), AES_BLOCK_SIZE,
	                    server.getEncryptIV(), AES_BLOCK_SIZE));
}

TEST(crypt_roundtrip) {
	CryptState server, client;
	CHECK(!client.isValid());
	crypt_pair(server, client);
	CHECK(client.isValid());

	// Various lengths, including multiple blocks and a partial block
	for (size_t len : {0, 1, 15, 16, 17, 100}) {
		std::vector<uint8_t> plain(len);
		for (size_t i = 0; i < len; i++) plain[i] = (uint8_t) i;

		std::vector<uint8_t> encrypted(len + 4), decrypted(len + 4);
		CHECK(client.encrypt(plain.data(), encrypted.data(), len));
		CHECK(server.decrypt(encrypted.data(), decrypted.data(), len + 4));
		CHECK(memcmp(decrypted.data(), plain.data(), len) == 0);
	}
	CHECK_EQ(server.getGood(), (size_t) 6);
}

TEST(crypt_rejects_tampering_and_replays) {
	CryptState server, client;
	crypt_pair(server, client);

	const uint8_t plain[] = "hello world";
	uint8_t encrypted[sizeof(plain) + 4], decrypted[sizeof(plain) + 4];
	client.encrypt(plain, encrypted, sizeof(plain));

	uint8_t tampered[sizeof(encrypted)];
	memcpy(tampered, encrypted, sizeof(encrypted));
	tampered[6] ^= 1;
	CHECK(!server.decrypt(tampered, decrypted, sizeof(tampered)));

	CHECK(server.decrypt(encrypted, decrypted, sizeof(encrypted)));
	CHECK(!server.decrypt(encrypted, decrypted, sizeof(encrypted))); // Replay
	CHECK(!server.decrypt(encrypted, decrypted, 3)); // Too short
}

TEST(crypt_tracks_lost_and_late_packets) {
	// Fixed IVs, since a random IV with a zero second byte looks like a replay to an out of order packet
	CryptState server, client;
	uint8_t key[AES_KEY_SIZE_BYTES], a[AES_BLOCK_SIZE], b[AES_BLOCK_SIZE];
	memset(key, 'k', sizeof(key));
	memset(a, 'a', sizeof(a));
	memset(b, 'b', sizeof(b));
	CHECK(server.setKey(key, sizeof(key), a, sizeof(a), b, sizeof(b)));
	CHECK(client.setKey(key, sizeof(key), b, sizeof(b), a, sizeof(a)));

	const uint8_t plain[] = "x";
	uint8_t packets[4][sizeof(plain) + 4], out[sizeof(plain) + 4];
	for (auto &packet : packets) {
		client.encrypt(plain, packet, sizeof(plain));
	}

	CHECK(server.decrypt(packets[0], out, sizeof(packets[0])));
	CHECK(server.decrypt(packets[3], out, sizeof(packets[3]))); // 2 lost
	CHECK_EQ(server.getLost(), (size_t) 2);
	CHECK(server.decrypt(packets[1], out, sizeof(packets[1]))); // Late, so 1 less lost
	CHECK_EQ(server.getLate(), (size_t) 1);
	CHECK_EQ(server.getLost(), (size_t) 1);
	CHECK_EQ(server.getGood(), (size_t) 3);
}

TEST(crypt_key_setters_validate_lengths) {
	CryptState crypt;
	const uint8_t key[AES_KEY_SIZE_BYTES] = {0};
	CHECK(!crypt.setKey(key, 3, key, AES_BLOCK_SIZE, key, AES_BLOCK_SIZE));
	CHECK(!crypt.setRawKey(key, 3));
	CHECK(!crypt.setEncryptIV(key, 3));
	CHECK(!crypt.setDecryptIV(key, 3));
	CHECK(crypt.setRawKey(key, AES_KEY_SIZE_BYTES));
	CHECK(crypt.setEncryptIV(key, AES_BLOCK_SIZE));
	CHECK(crypt.setDecryptIV(key, AES_BLOCK_SIZE));
	CHECK(!crypt.isValid()); // Only setKey/genKey make it usable
}

/*
 * AudioQueue
 */

TEST(audioqueue_fifo_and_shutdown) {
	AudioQueue queue;
	CHECK(queue.popNonBlocking() == nullptr);

	AudioWork *first = new AudioWork();
	AudioWork *second = new AudioWork();
	queue.push(first);
	queue.push(second);

	bool running = true;
	CHECK(queue.pop(&running) == first);
	CHECK(queue.popNonBlocking() == second);
	delete first;
	delete second;

	// Once stopped, an empty queue returns instead of blocking
	running = false;
	CHECK(queue.pop(&running) == nullptr);

	queue.push(new AudioWork());
	queue.cleanup(); // Frees pending work
}
