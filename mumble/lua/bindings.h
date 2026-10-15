#pragma once

// Every function and method table we hand to Lua, used to build the module

#include "lua/luainc.h"

#include <cstdint>
#include <vector>

class ByteBuffer;

// audiostream
extern const luaL_Reg mumble_audiostream[];

// banentry
extern int mumble_banentry_new(lua_State *l);
extern const luaL_Reg mumble_banentry[];

// buffer
extern ByteBuffer* luabuffer_new(lua_State *l);
extern int mumble_buffer_new(lua_State *l);
extern const luaL_Reg mumble_buffer[];

// channel
extern int channel_call(lua_State *l);
extern const luaL_Reg mumble_channel[];

// client
// The sessions of users, or ids of channels, passed as a table or as every argument from index on
extern std::vector<uint32_t> lua_checksessions(lua_State *l, int index);
extern std::vector<uint32_t> lua_checkchannelids(lua_State *l, int index);
extern const luaL_Reg mumble_client[];
extern int mumble_client_new(lua_State *l);

// crypt
extern int mumble_crypt_new(lua_State* l);
extern const luaL_Reg mumble_ocb_aes128[];

// decoder
extern int mumble_decoder_new(lua_State *l);
extern const luaL_Reg mumble_decoder[];

// encoder
extern int mumble_encoder_new(lua_State *l);
extern const luaL_Reg mumble_encoder[];

// log
extern const luaL_Reg mumble_log_reg[];

// pipe
extern int mumble_pipe_new(lua_State *l);
extern const luaL_Reg mumble_pipe[];

// target
extern int mumble_target_new(lua_State *l);
extern const luaL_Reg mumble_target[];

// thread
extern int mumble_thread_new(lua_State *l);
extern const luaL_Reg mumble_thread_controller[];
extern const luaL_Reg mumble_thread_worker[];

// timer
extern int mumble_timer_new(lua_State *l);
extern const luaL_Reg mumble_timer[];

// user
extern const luaL_Reg mumble_user[];
