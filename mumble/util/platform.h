#pragma once

// What differs between operating systems, so the rest of the module doesn't need to know

#include <cstdint>

// Sockets, addresses and byte order (inet_ntop, getaddrinfo, htonl, ...)
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netdb.h>
#endif

// Name the calling thread, for debuggers and profilers
void platform_thread_setname(const char *name);

// Sleep the calling thread until a uv_hrtime() timestamp, as precisely as the OS allows
void platform_sleep_until(uint64_t hrtime);
