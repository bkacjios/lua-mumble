#include "defines.h"

#include <lauxlib.h>
#include <stdarg.h>
#include <stdio.h>

#include "util.h"

static int LEVEL = LOG_LEVEL;
static FILE* LOG_FILE = NULL;

static const char* get_log_level_info(int level, const char** lcolor) {
	switch (level) {
	case LOG_TRACE: *lcolor = "\x1b[36;1m"; return "TRACE";
	case LOG_DEBUG: *lcolor = "\x1b[35;1m"; return "DEBUG";
	case LOG_INFO:  *lcolor = "\x1b[32;1m"; return "INFO";
	case LOG_WARN:  *lcolor = "\x1b[33;1m"; return "WARN";
	case LOG_ERROR: *lcolor = "\x1b[31;1m"; return "ERROR";
	default:        *lcolor = "\x1b[0m";     return "UNKWN";
	}
}

static void mumble_printf(int level, const char* fmt, va_list va) {
	if (level > LEVEL) return;

	const char* lcolor;
	const char* llevel = get_log_level_info(level, &lcolor);
	FILE* out = (level == LOG_ERROR) ? stderr : stdout;

	va_list vac;
	va_copy(vac, va);

	// Print the log type and color information
	fprintf(out, "[\x1b[34;1mMUMBLE\x1b[0m - %s%5s\x1b[0m] ", lcolor, llevel);
	vfprintf(out, fmt, va);
	fprintf(out, NEWLINE);

	if (LOG_FILE) {
		fprintf(LOG_FILE, "[MUMBLE - %5s] ", llevel);
		vfprintf(LOG_FILE, fmt, vac);
		fprintf(LOG_FILE, NEWLINE);
	}

	va_end(vac);
}

static void mumble_print(int level, const char* message) {
	if (level > LEVEL) return;

	const char* lcolor;
	const char* llevel = get_log_level_info(level, &lcolor);
	FILE* out = (level == LOG_ERROR) ? stderr : stdout;

	// Print the log type and color information without formatting
	fprintf(out, "[\x1b[34;1mMUMBLE\x1b[0m - %s%5s\x1b[0m] ", lcolor, llevel);
	fprintf(out, "%s", message);  // Direct message printing with no formatting
	fprintf(out, NEWLINE);

	if (LOG_FILE) {
		fprintf(LOG_FILE, "[MUMBLE - %5s] ", llevel);
		fprintf(LOG_FILE, "%s", message);  // Direct message printing with no formatting
		fprintf(LOG_FILE, NEWLINE);
	}
}

void mumble_log(int level, const char* fmt, ...) {
	if (level > LEVEL) return;

	va_list va;
	va_start(va, fmt);
	mumble_printf(level, fmt, va);
	va_end(va);
}

int mumble_getLogLevel() {
	return LEVEL;
}

int mumble_setLogFile(FILE* file) {
	LOG_FILE = file;
}

#define lua_format_string(l, start_index) \
	int num_args = lua_gettop(l) - (start_index) + 1; \
	luaL_checktype(l, (start_index), LUA_TSTRING); \
	lua_getglobal(l, "string"); \
	lua_getfield(l, -1, "format"); \
	lua_remove(l, -2); \
	const char *format = luaL_checkstring(l, (start_index)); \
	/* Push all arguments onto the stack */ \
	for (int i = 0; i < num_args; i++) { \
		lua_pushvalue(l, (i) + (start_index)); \
	} \
	/* Call string.format with the arguments */ \
	if (lua_pcall(l, num_args, 1, 0) != 0) { \
		/* Return an error if pcall fails */ \
		return luaL_error(l, lua_tostring(l, -1)); \
	}


static int log_trace(lua_State *l) {
	lua_format_string(l, 1);
	const char* message = lua_tostring(l, -1);
	mumble_print(LOG_TRACE, message);
	lua_pop(l, 1);
	return 0;
}

static int log_debug(lua_State *l) {
	lua_format_string(l, 1);
	const char* message = lua_tostring(l, -1);
	mumble_print(LOG_DEBUG, message);
	lua_pop(l, 1);
	return 0;
}

static int log_info(lua_State *l) {
	lua_format_string(l, 1);
	const char* message = lua_tostring(l, -1);
	mumble_print(LOG_INFO, message);
	lua_pop(l, 1);
	return 0;
}

static int log_warn(lua_State *l) {
	lua_format_string(l, 1);
	const char* message = lua_tostring(l, -1);
	mumble_print(LOG_WARN, message);
	lua_pop(l, 1);
	return 0;
}

static int log_error(lua_State *l) {
	lua_format_string(l, 1);
	const char* message = lua_tostring(l, -1);
	mumble_print(LOG_ERROR, message);
	lua_pop(l, 1);
	return 0;
}

static int log_setLogFile(lua_State *l) {
	FILE** file = (FILE**)luaL_checkudata(l, 1, "FILE*");

	// Check if the file pointer is valid
	if (*file == NULL) {
		return luaL_error(l, "attempted to set an invalid log file.");
	}

	// Store the file pointer in the static logfile variable
	LOG_FILE = *file;
	return 0;
}

const luaL_Reg mumble_log_reg[] = {
	{"trace", log_trace},
	{"debug", log_debug},
	{"info", log_info},
	{"warn", log_warn},
	{"error", log_error},
	{"setLogFile", log_setLogFile},
	{NULL, NULL}
};