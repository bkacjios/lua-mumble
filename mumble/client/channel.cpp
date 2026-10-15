#include "client/channel.h"

#include "client/client.h"
#include "client/user.h"

#include <cstring>
#include <string>

void MumbleChannel::message(const char *text) {
	MumbleProto__TextMessage message = MUMBLE_PROTO__TEXT_MESSAGE__INIT;
	message.message = (char*) text;
	message.n_channel_id = 1;
	message.channel_id = &channel_id;
	client->send(PACKET_TEXTMESSAGE, &message);
}

void MumbleChannel::setDescription(const char *text) {
	MumbleProto__ChannelState message = MUMBLE_PROTO__CHANNEL_STATE__INIT;
	message.has_channel_id = true;
	message.channel_id = channel_id;
	message.description = (char*) text;
	client->send(PACKET_CHANNELSTATE, &message);
}

void MumbleChannel::remove() {
	MumbleProto__ChannelRemove message = MUMBLE_PROTO__CHANNEL_REMOVE__INIT;
	message.channel_id = channel_id;
	client->send(PACKET_CHANNELREMOVE, &message);
}

void MumbleChannel::link(const std::vector<uint32_t> &add, const std::vector<uint32_t> &remove) {
	MumbleProto__ChannelState message = MUMBLE_PROTO__CHANNEL_STATE__INIT;
	message.has_channel_id = true;
	message.channel_id = channel_id;
	message.n_links_add = add.size();
	message.links_add = (uint32_t*) add.data();
	message.n_links_remove = remove.size();
	message.links_remove = (uint32_t*) remove.data();
	client->send(PACKET_CHANNELSTATE, &message);
}

void MumbleChannel::requestACL() {
	MumbleProto__ACL message = MUMBLE_PROTO__ACL__INIT;
	message.channel_id = channel_id;
	message.has_query = true;
	message.query = true;
	client->send(PACKET_ACL, &message);
}

void MumbleChannel::requestPermissions() {
	MumbleProto__PermissionQuery message = MUMBLE_PROTO__PERMISSION_QUERY__INIT;
	message.has_channel_id = true;
	message.channel_id = channel_id;
	client->send(PACKET_PERMISSIONQUERY, &message);
}

void MumbleChannel::setListeningVolumeAdjustment(float volume) {
	MumbleProto__UserState__VolumeAdjustment adjustment = MUMBLE_PROTO__USER_STATE__VOLUME_ADJUSTMENT__INIT;
	adjustment.has_listening_channel = true;
	adjustment.listening_channel = channel_id;
	adjustment.has_volume_adjustment = true;
	adjustment.volume_adjustment = volume;

	// The adjustment is part of our own user state
	MumbleProto__UserState__VolumeAdjustment *adjustments[] = { &adjustment };
	MumbleProto__UserState message = MUMBLE_PROTO__USER_STATE__INIT;
	message.has_session = true;
	message.session = client->session;
	message.n_listening_volume_adjustment = 1;
	message.listening_volume_adjustment = adjustments;
	client->send(PACKET_USERSTATE, &message);
}

void MumbleChannel::requestDescriptionBlob() {
	client->requestBlobs({}, {}, {channel_id});
}

void MumbleChannel::create(const char *name, const char *description, int32_t position, bool temporary, uint32_t max_users) {
	client->createChannel(channel_id, name, description, position, temporary, max_users);
}

void MumbleChannel::contextAction(const char *action) {
	MumbleProto__ContextAction message = MUMBLE_PROTO__CONTEXT_ACTION__INIT;
	message.has_channel_id = true;
	message.channel_id = channel_id;
	message.action = (char*) action;
	client->send(PACKET_CONTEXTACTION, &message);
}

bool MumbleChannel::isRoot() const {
	return channel_id == parent;
}

std::vector<MumbleChannel*> MumbleChannel::children() const {
	std::vector<MumbleChannel*> result;
	for (auto &node : client->channel_list) {
		MumbleChannel *chan = node.data;
		if (!chan->isRoot() && chan->parent == channel_id) {
			result.push_back(chan);
		}
	}
	return result;
}

std::vector<MumbleUser*> MumbleChannel::users() const {
	std::vector<MumbleUser*> result;
	for (auto &node : client->user_list) {
		if (node.data->channel_id == channel_id) {
			result.push_back(node.data);
		}
	}
	return result;
}

MumbleChannel* MumbleChannel::find(const char *path) {
	// Tokenize a copy, since strtok writes into the string it is given
	std::string copy(path);
	MumbleChannel *channel = this;

	for (char *name = strtok(&copy[0], "/\\"); name != NULL; name = strtok(NULL, "/\\")) {
		MumbleChannel *next = NULL;

		if (strcmp(name, ".") == 0) {
			next = channel;
		} else if (strcmp(name, "..") == 0) {
			next = client->channel_list.get(channel->parent);
		} else {
			for (MumbleChannel *child : channel->children()) {
				if (child->name && strcmp(name, child->name->c_str()) == 0) {
					next = child;
				}
			}
		}

		if (next == NULL) {
			return NULL;
		}
		channel = next;
	}

	return channel;
}
