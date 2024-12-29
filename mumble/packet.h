#pragma once

#include "client.h"

#define NUM_PACKETS 27

enum {
	PACKET_VERSION          = 0,
	PACKET_UDPTUNNEL        = 1,
	PACKET_AUTHENTICATE     = 2,
	PACKET_PING             = 3,
	PACKET_SERVERREJECT     = 4,
	PACKET_SERVERSYNC       = 5,
	PACKET_CHANNELREMOVE    = 6,
	PACKET_CHANNELSTATE     = 7,
	PACKET_USERREMOVE       = 8,
	PACKET_USERSTATE        = 9,
	PACKET_BANLIST          = 10,
	PACKET_TEXTMESSAGE      = 11,
	PACKET_PERMISSIONDENIED = 12,
	PACKET_ACL              = 13,
	PACKET_QUERYUSERS       = 14,
	PACKET_CRYPTSETUP       = 15,
	PACKET_CONTEXTACTIONADD = 16,
	PACKET_CONTEXTACTION    = 17,
	PACKET_USERLIST         = 18,
	PACKET_VOICETARGET      = 19,
	PACKET_PERMISSIONQUERY  = 20,
	PACKET_CODECVERSION     = 21,
	PACKET_USERSTATS        = 22,
	PACKET_REQUESTBLOB      = 23,
	PACKET_SERVERCONFIG     = 24,
	PACKET_SUGGESTCONFIG    = 25,
	PACKET_PLUGINDATA       = 26,
};

enum {
	UDP_PACKET_AUDIO	= 0,
	UDP_PACKET_PING		= 1,
};

#define packet_send(client, type, message) packet_sendex(client, type, message, message.base, 0)
int packet_sendex(MumbleClient* client, const int type, const void *message, const ProtobufCMessage* base, const int length);
int packet_sendudp(MumbleClient* client, const void *message, const int length);

typedef void (*Packet_Handler_Func)(lua_State *lua, MumbleClient *client, MumblePacket *packet);

extern const Packet_Handler_Func packet_handler[NUM_PACKETS];