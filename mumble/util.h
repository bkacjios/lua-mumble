#pragma once

#include <lua.h>
#include "types.h"

void bin_to_strhex(char *bin, size_t binsz, char **result);

#if (defined(LUA_VERSION_NUM) && LUA_VERSION_NUM < 502) && !defined(LUAJIT)
void* luaL_testudata(lua_State* L, int index, const char* tname);
void luaL_traceback(lua_State* L, lua_State* L1, const char* msg, int level);
#endif

void luaL_debugstack(lua_State *l, const char* text);
int luaL_typerror(lua_State *L, int narg, const char *tname);
int luaL_typerror_table(lua_State *L, int narg, int nkey, int nvalue, const char *tname);
int luaL_checkfunction(lua_State *L, int i);
int luaL_checkboolean(lua_State *L, int i);
int luaL_optboolean(lua_State *L, int i, int d);
int luaL_isudata(lua_State *L, int ud, const char *tname);

LinkQueue *queue_new();
void queue_push(LinkQueue* queue, char* data, size_t size);
QueueNode* queue_pop(LinkQueue *queue);

void list_add(LinkNode** head_ref, uint32_t index, void *data);
void list_remove(LinkNode **head_ref, uint32_t index);
void list_remove_data(LinkNode **head_ref, void *data);
void list_clear(LinkNode** head_ref);
size_t list_count(LinkNode** head_ref);
void* list_get(LinkNode* current, uint32_t index);