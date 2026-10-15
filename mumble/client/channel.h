#pragma once

#include "lua/luainc.h"
#include "util/util.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

class MumbleClient;
class MumbleUser;

class MumbleChannel {
public:
	MumbleChannel(MumbleClient *client, uint32_t channel_id, int data) : client(client), data(data), channel_id(channel_id) {}

	void message(const char *text);
	void setDescription(const char *text);
	void remove();
	void link(const std::vector<uint32_t> &add, const std::vector<uint32_t> &remove);
	void requestACL();
	void requestPermissions();
	// Whether we have any of the permissions in a mask of ACL permission flags
	bool hasPermission(uint32_t permission) const { return (permissions & permission) != 0; }
	void setListeningVolumeAdjustment(float volume);
	void requestDescriptionBlob();
	// Create a sub channel
	void create(const char *name, const char *description, int32_t position, bool temporary, uint32_t max_users);
	void contextAction(const char *action);

	// The root channel is its own parent
	bool isRoot() const;
	std::vector<MumbleChannel*> children() const;
	std::vector<MumbleUser*> users() const;
	// Find a channel by a path relative to this one, such as "./Lobby/AFK" or "../Other", or NULL
	MumbleChannel* find(const char *path);

	MumbleClient*	client;
	int				data;
	std::optional<std::string> name;
	uint32_t		channel_id;
	uint32_t		parent = 0;
	std::optional<std::string> description;
	std::string		description_hash;
	bool			temporary = false;
	int32_t			position = 0;
	uint32_t		max_users = 0;
	LinkList<void>	links;
	bool			is_enter_restricted = false;
	bool			can_enter = false;
	uint32_t		permissions = 0;
	float			listening_volume_adjustment = 0.0f;
};
