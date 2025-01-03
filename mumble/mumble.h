#pragma once

#include "proto/Mumble.pb-c.h"
#include "proto/MumbleUDP.pb-c.h"

#include "types.h"

/*--------------------------------
	UTIL FUNCTIONS
--------------------------------*/

extern int MUMBLE_CLIENTS;
extern int MUMBLE_REGISTRY;
extern int MUMBLE_TIMER_REG;
extern int MUMBLE_THREAD_REG;

void mumble_init(lua_State *l);
extern int luaopen_mumble(lua_State *l);

void mumble_audio_timer(uv_timer_t* handle);
void mumble_ping_timer(uv_timer_t* handle);

uint64_t util_get_varint(uint8_t buffer[], int *len);

void mumble_ping(lua_State* l, MumbleClient* client);

uint64_t mumble_adjust_audio_bandwidth(MumbleClient *client);
void mumble_create_audio_timer(MumbleClient *client);
int mumble_client_connect(lua_State *l);
void mumble_disconnect(lua_State* l, MumbleClient *client, const char* reason, bool garbagecollected);

void mumble_client_raw_get(lua_State* l, MumbleClient* client);
MumbleUser* mumble_user_get(lua_State* l, MumbleClient* client, uint32_t session);
void mumble_user_raw_get(lua_State* l, MumbleClient* client, uint32_t session);
void mumble_user_remove(lua_State* l, MumbleClient* client, uint32_t session);

MumbleChannel* mumble_channel_get(lua_State* l, MumbleClient* client, uint32_t channel_id);
void mumble_channel_raw_get(lua_State* l, MumbleClient* client, uint32_t channel_id);
void mumble_channel_remove(lua_State* l, MumbleClient* client, uint32_t channel_id);

int mumble_push_address(lua_State* l, ProtobufCBinaryData address);

void mumble_handle_udp_packet(lua_State* l, MumbleClient* client, unsigned char* unencrypted, ssize_t size, bool udp);
void mumble_handle_speaking_hooks_legacy(lua_State* l, MumbleClient* client, uint8_t buffer[], uint8_t codec, uint8_t target, uint32_t session);
void mumble_handle_speaking_hooks_protobuf(lua_State* l, MumbleClient* client, MumbleUDP__Audio *audio, uint32_t session);

int mumble_traceback(lua_State *l);
int mumble_hook_call(lua_State* l, MumbleClient *client, const char* hook, int nargs);
int mumble_hook_call_ret(lua_State* l, MumbleClient *client, const char* hook, int nargs, int nresults);

int mumble_immutable(lua_State *l);
void mumble_weak_table(lua_State *l);
int mumble_ref(lua_State *l);
void mumble_pushref(lua_State *l, int ref);
void mumble_unref(lua_State *l, int ref);

int mumble_registry_ref(lua_State *l, int t);
void mumble_registry_pushref(lua_State *l, int t, int ref);
void mumble_registry_unref(lua_State *l, int t, int ref);