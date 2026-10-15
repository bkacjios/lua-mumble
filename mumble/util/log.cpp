#include "util/log.h"

#include "defines.h"

#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <sys/time.h>

static int LEVEL = LOG_LEVEL;
static FILE* LOG_FILE = NULL;

static const char* get_log_level_info(int level, const char** lcolor) {
	switch (level) {
	case LOG_CODE: *lcolor = "\x1b[34;1m"; return "CODE";
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

	// Get current time with microseconds resolution
	struct timeval tv;
	gettimeofday(&tv, NULL);
	struct tm *tm_info = localtime(&tv.tv_sec);
	char time_buffer[30]; // Buffer for "YYYY-MM-DD HH:MM:SS"
	strftime(time_buffer, sizeof(time_buffer), "%Y-%m-%d %H:%M:%S", tm_info);

	// Print the log type and color information
	fprintf(out, "[\x1b[1m%s.%03ld\x1b[0m \x1b[34;1mMUMBLE\x1b[0m - %s%5s\x1b[0m] ", time_buffer, tv.tv_usec / 1000, lcolor, llevel);
	vfprintf(out, fmt, va);
	fprintf(out, NEWLINE);

	if (LOG_FILE) {
		fprintf(LOG_FILE, "[%s.%03ld MUMBLE - %5s] ", time_buffer, tv.tv_usec / 1000, llevel);
		vfprintf(LOG_FILE, fmt, vac);
		fprintf(LOG_FILE, NEWLINE);
	}

	va_end(vac);
}

void mumble_print(int level, const char* message) {
	if (level > LEVEL) return;

	const char* lcolor;
	const char* llevel = get_log_level_info(level, &lcolor);
	FILE* out = (level == LOG_ERROR) ? stderr : stdout;

	// Get current time with microseconds resolution
	struct timeval tv;
	gettimeofday(&tv, NULL);
	struct tm *tm_info = localtime(&tv.tv_sec);
	char time_buffer[30]; // Buffer for "YYYY-MM-DD HH:MM:SS"
	strftime(time_buffer, sizeof(time_buffer), "%Y-%m-%d %H:%M:%S", tm_info);

	// Print the log type and color information without formatting
	fprintf(out, "[\x1b[1m%s.%03ld\x1b[0m \x1b[34;1mMUMBLE\x1b[0m - %s%5s\x1b[0m] ", time_buffer, tv.tv_usec / 1000, lcolor, llevel);
	fprintf(out, "%s", message);  // Direct message printing with no formatting
	fprintf(out, NEWLINE);

	if (LOG_FILE) {
		fprintf(LOG_FILE, "[%s.%03ld MUMBLE - %5s] ", time_buffer, tv.tv_usec / 1000, llevel);
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

void mumble_setLogFile(FILE* file) {
	LOG_FILE = file;
}
