#pragma once

#include "defines.h"

#include <cstdio>

void mumble_log(int level, const char* fmt, ...);
// Log a message as is, with no formatting
void mumble_print(int level, const char* message);
int mumble_getLogLevel();
void mumble_setLogFile(FILE* file);
