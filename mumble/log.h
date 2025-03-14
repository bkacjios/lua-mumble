#pragma once

#include "defines.h"
#include <lauxlib.h>

void mumble_log(int level, const char* fmt, ...);

extern const luaL_Reg mumble_log_reg[];