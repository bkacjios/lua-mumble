#include "util/platform.h"

#include <uv.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <ctime>
#include <pthread.h>
#endif

void platform_thread_setname(const char *name) {
#if UV_VERSION_MAJOR > 1 || (UV_VERSION_MAJOR == 1 && UV_VERSION_MINOR >= 50)
	uv_thread_setname(name);
#elif defined(__APPLE__)
	pthread_setname_np(name);
#elif !defined(_WIN32)
	pthread_setname_np(pthread_self(), name);
#else
	(void) name;
#endif
}

void platform_sleep_until(uint64_t hrtime) {
#if defined(_WIN32)
	uint64_t now = uv_hrtime();
	if (hrtime <= now) return;

	// A high resolution waitable timer, since Sleep() is only accurate to the system timer tick
	static thread_local HANDLE timer = NULL;
	if (timer == NULL) {
		timer = CreateWaitableTimerExW(NULL, NULL, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
		if (timer == NULL) {
			// Older than Windows 10 1803
			timer = CreateWaitableTimerExW(NULL, NULL, 0, TIMER_ALL_ACCESS);
		}
	}

	// Negative due times are relative, in 100ns intervals
	LARGE_INTEGER due;
	due.QuadPart = -(LONGLONG) ((hrtime - now) / 100);
	if (timer != NULL && SetWaitableTimer(timer, &due, 0, NULL, NULL, FALSE)) {
		WaitForSingleObject(timer, INFINITE);
	} else {
		Sleep((DWORD) ((hrtime - now) / 1000000));
	}
#elif defined(__APPLE__)
	// No clock_nanosleep, so sleep for however long is left
	uint64_t now = uv_hrtime();
	if (hrtime <= now) return;
	struct timespec ts;
	ts.tv_sec = (hrtime - now) / 1000000000;
	ts.tv_nsec = (hrtime - now) % 1000000000;
	nanosleep(&ts, NULL);
#else
	// uv_hrtime() is CLOCK_MONOTONIC here, so we can sleep until the exact moment
	struct timespec ts;
	ts.tv_sec = hrtime / 1000000000;
	ts.tv_nsec = hrtime % 1000000000;
	clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, NULL);
#endif
}
