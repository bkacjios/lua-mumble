// Tests for the requests the client sends to the server, checked on the wire through ClientFixture

#include "fixture.h"

#include "client/channel.h"
#include "proto/Mumble.pb-c.h"

// Unpack the last message of a type the client sent, or NULL
template <typename Message>
static Message* last_sent(ClientFixture &f, uint16_t type, Message* (*unpack)(ProtobufCAllocator*, size_t, const uint8_t*)) {
	f.settle();
	auto sent = f.sentOfType(type);
	CHECK(!sent.empty());
	if (sent.empty()) return nullptr;
	return unpack(NULL, sent.back().size(), sent.back().data());
}

// Make the client know about a channel, as if the server had told us about it
static void add_channel(ClientFixture &f, uint32_t id, const char *name) {
	MumbleChannel *channel = f.client->getChannel(id);
	channel->name = std::string(name);
}

TEST(request_create_channel_sends_every_field) {
	ClientFixture f;
	f.lua("client:createChannel('Top', 'desc', 3, true, 7)");

	MumbleProto__ChannelState *state = last_sent(f, PACKET_CHANNELSTATE, mumble_proto__channel_state__unpack);
	if (!state) return;
	CHECK(state->has_parent);
	CHECK_EQ(state->parent, 0u);
	CHECK(strcmp(state->name, "Top") == 0);
	CHECK(strcmp(state->description, "desc") == 0);
	CHECK(state->has_position);
	CHECK_EQ(state->position, 3);
	CHECK(state->has_temporary);
	CHECK(state->temporary);
	CHECK(state->has_max_users);
	CHECK_EQ(state->max_users, 7u);
	CHECK(!state->has_channel_id); // New channels have no id yet
	mumble_proto__channel_state__free_unpacked(state, NULL);
}

TEST(request_create_sub_channel) {
	ClientFixture f;
	add_channel(f, 4, "Lobby");
	f.lua("client:getChannels()[1]:create('Child')");

	MumbleProto__ChannelState *state = last_sent(f, PACKET_CHANNELSTATE, mumble_proto__channel_state__unpack);
	if (!state) return;
	CHECK(state->has_parent);
	CHECK_EQ(state->parent, 4u);
	CHECK(strcmp(state->name, "Child") == 0);
	CHECK(state->has_temporary && !state->temporary);
	mumble_proto__channel_state__free_unpacked(state, NULL);
}

TEST(request_user_stats_only) {
	ClientFixture f;
	f.client->getUser(2);
	f.lua("for _, u in pairs(client:getUsers()) do if u:getSession() == 2 then u:requestStats(true) end end");

	MumbleProto__UserStats *stats = last_sent(f, PACKET_USERSTATS, mumble_proto__user_stats__unpack);
	if (!stats) return;
	CHECK_EQ(stats->session, 2u);
	CHECK(stats->has_stats_only);
	CHECK(stats->stats_only);
	mumble_proto__user_stats__free_unpacked(stats, NULL);
}

TEST(request_listening_volume_adjustment) {
	ClientFixture f;
	add_channel(f, 4, "Lobby");
	f.lua("client:getChannels()[1]:setListeningVolumeAdjustment(0.5)");

	// It's part of our own user state
	MumbleProto__UserState *state = last_sent(f, PACKET_USERSTATE, mumble_proto__user_state__unpack);
	if (!state) return;
	CHECK_EQ(state->session, 1u);
	CHECK_EQ(state->n_listening_volume_adjustment, (size_t) 1);
	if (state->n_listening_volume_adjustment == 1) {
		CHECK_EQ(state->listening_volume_adjustment[0]->listening_channel, 4u);
		CHECK_NEAR(state->listening_volume_adjustment[0]->volume_adjustment, 0.5, 1e-6);
	}
	mumble_proto__user_state__free_unpacked(state, NULL);
}

TEST(request_listen_with_arguments) {
	ClientFixture f;
	add_channel(f, 0, "Root");
	add_channel(f, 4, "Lobby");
	add_channel(f, 5, "AFK");
	f.client->getUser(1);
	f.lua(R"(
		local lobby, afk = client:getChannel('Lobby'), client:getChannel('AFK')
		client:getMe():listen(lobby, afk)
	)");

	MumbleProto__UserState *state = last_sent(f, PACKET_USERSTATE, mumble_proto__user_state__unpack);
	if (!state) return;
	CHECK_EQ(state->n_listening_channel_add, (size_t) 2);
	if (state->n_listening_channel_add == 2) {
		CHECK_EQ(state->listening_channel_add[0], 4u);
		CHECK_EQ(state->listening_channel_add[1], 5u);
	}
	mumble_proto__user_state__free_unpacked(state, NULL);
}

TEST(request_auth) {
	ClientFixture f;
	f.lua("client:auth('Bot', 'secret', {'a', 'b'})");

	MumbleProto__Authenticate *auth = last_sent(f, PACKET_AUTHENTICATE, mumble_proto__authenticate__unpack);
	if (!auth) return;
	CHECK(strcmp(auth->username, "Bot") == 0);
	CHECK(strcmp(auth->password, "secret") == 0);
	CHECK_EQ(auth->n_tokens, (size_t) 2);
	CHECK(auth->has_opus && auth->opus);
	mumble_proto__authenticate__free_unpacked(auth, NULL);

	MumbleProto__Version *version = last_sent(f, PACKET_VERSION, mumble_proto__version__unpack);
	if (!version) return;
	CHECK(version->has_version_v1);
	CHECK(version->has_version_v2);
	mumble_proto__version__free_unpacked(version, NULL);
}

TEST(request_voice_target) {
	ClientFixture f;
	f.client->getUser(2);
	f.client->getUser(3);
	f.lua(R"(
		local target = mumble.voicetarget()
		for _, user in pairs(client:getUsers()) do target:addUser(user) end
		target:setGroup('admin' .. tostring(12345))
		collectgarbage()
		client:registerVoiceTarget(2, target)
	)");

	MumbleProto__VoiceTarget *message = last_sent(f, PACKET_VOICETARGET, mumble_proto__voice_target__unpack);
	if (!message) return;
	CHECK_EQ(message->id, 2u);
	CHECK_EQ(message->n_targets, (size_t) 1);
	if (message->n_targets == 1) {
		CHECK_EQ(message->targets[0]->n_session, (size_t) 2);
		CHECK(strcmp(message->targets[0]->group, "admin12345") == 0);
	}
	mumble_proto__voice_target__free_unpacked(message, NULL);
}

TEST(request_start_recording_twice) {
	ClientFixture f;
	f.client->getUser(2);
	char path[] = "/tmp/mumble-record-test-XXXXXX";
	close(mkstemp(path));
	f.lua(std::string(R"(
		for _, u in pairs(client:getUsers()) do user = u end
		first, first_err = user:startRecord(')") + path + R"(')
		second, second_err = user:startRecord(')" + path + R"(')
		user:stopRecord()
	)");
	unlink(path);

	CHECK(f.boolean("first == true"));
	CHECK(f.boolean("second == nil"));
	CHECK(f.boolean("second_err == 'user is already being recorded'"));
}
