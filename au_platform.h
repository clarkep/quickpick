#ifndef AUTIL_PLATFORM_H
#define AUTIL_PLATFORM_H

#include "au_core.h"

#ifdef __cplusplus
extern "C" {
#endif

/************************************** Platform abstractions *************************************/

/* Basic threading */

typedef void (*au_thread_func)(void *arg);

struct au_thread {
	au_thread_func f;
	void *arg;
	bool running;
	// implementation specific fields...
};

struct au_thread *au_create_thread(Arena *arena, au_thread_func f, void *arg);

bool au_start_thread(struct au_thread *thread);

void au_join_thread(struct au_thread *thread, i32 timeout_ms);

struct au_mutex;

struct au_mutex *au_create_mutex(Arena *arena);

bool au_lock_mutex(struct au_mutex *mutex);

bool au_unlock_mutex(struct au_mutex *mutex);

/* Basic timing */

#ifdef __aarch64__
	u64 au_cpu_time_();
	#define au_cpu_time au_cpu_time_
#else
	#ifdef MSC_VER
		#include <intrin.h>
		#define au_cpu_time __rdtsc
	#elif defined(__GNUC__) || defined(__clang__)
		#include <x86intrin.h>
		#define au_cpu_time __rdtsc
	#else
		#define au_cpu_time() 0
	#endif
#endif

bool au_pin_to_core(int core);

// time in nanoseconds since arbitrary start time
u64 au_os_time();

// time in seconds
double au_os_time_s();

// Get time in ticks since arbitrary start time, in a platform dependent "native" unit.
u64 au_os_time_native();

// Return the conversion factor from native os ticks to nanoseconds
double au_os_time_native_to_nanoseconds();

// Return the conversion factor from native os ticks to seconds
double au_os_time_native_to_seconds();

// Return the conversion factor from cpu counter ticks to nanoseconds
double au_cpu_time_to_nanoseconds();

// Return the conversion factor from cpu counter ticks to seconds
double au_cpu_time_to_seconds();

// Measure the calibration between cpu time and wall time by sleeping for ms milliseconds. If this
// is not called, it will happen automatically the first time au_cpu_time_to_[*] is called,
// which can also be from inside profiler.h functions.
void au_calibrate_by_sleeping(i32 ms);

void au_sleep(i32 millis);

/* Building, dynamic library loading */

u64 au_get_modification_time(char **filenames, int n);

void au_run_command_in_dir(char *command, char *dir);

void *au_load_library(char *path);

void au_unload_library(void *library);

void *au_get_function(void *library, char *func_name);


#ifdef __cplusplus
}
#endif

#endif