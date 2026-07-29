// sched_setaffinity/CPU_ZERO/CPU_SET are glibc extensions.
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <dlfcn.h>
#include <sched.h>
#include <pthread.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#include "au_core.h"
#include "au_platform.h"

struct au_unix_thread {
	/* keep in line with autil.h: */
	au_thread_func f;
	void *arg;
	bool running;
	/* unix specific: */
	pthread_t pthread;
};

static void *au_unix_thread_wrapper(void *arg) {
	struct au_unix_thread *thr = (struct au_unix_thread *)arg;
	thr->running = true;
	thr->f(thr->arg);
	thr->running = false;
}

struct au_thread *au_create_thread(Arena *arena, au_thread_func f, void *arg) {
	struct au_unix_thread *res = aalloc(arena, sizeof(struct au_unix_thread));
	res->f = f;
	res->arg = arg;
	return (struct au_thread *)res;
}

bool au_start_thread(struct au_thread *thread) {
	struct au_unix_thread *thr = (struct au_unix_thread *)thread;
	// todo: error code
	return !pthread_create(&thr->pthread, NULL, au_unix_thread_wrapper,
												 (void *)thr);
}

void au_join_thread(struct au_thread *thread, i32 timeout_ms) {
	struct au_unix_thread *thr = (struct au_unix_thread *)thread;
	if (timeout_ms >= 0) {
		errexit("au_join_thread with timeout >= 0 is unimplemented.\n");
		/*
		// TODO: Need cross-platform implementation
		struct timespec ts = { timeout_ms / 1000, (timeout_ms % 1000) * 1000000 };
		pthread_timedjoin_np(thr->pthread, NULL, &ts);
		*/
	} else {
		pthread_join(thr->pthread, NULL);
	}
}

// xx au_unix_mutex?
struct au_mutex {
	pthread_mutex_t pthread_mutex;
};

struct au_mutex *au_create_mutex(Arena *arena) {
	// Did you know? If you try to use a pthread_mutex_t on linux at an unaligned
	// address, your program crashes with an undiagnosable error message.
	arena_align(arena, 16);
	struct au_mutex *res = aalloc(arena, sizeof(struct au_mutex));
	pthread_mutex_init(&res->pthread_mutex, NULL);
	return res;
}

bool au_lock_mutex(struct au_mutex *mutex) {
	return !pthread_mutex_lock(&mutex->pthread_mutex);
}

bool au_unlock_mutex(struct au_mutex *mutex) {
	// todo: error code
	return !pthread_mutex_unlock(&mutex->pthread_mutex);
}

bool au_pin_to_core(int core)
{
#ifdef MACOS
	return false;
#else
	cpu_set_t set;
	CPU_ZERO(&set);
	CPU_SET(core, &set);
	int code = sched_setaffinity(0, sizeof(cpu_set_t), &set);
	return !code;
#endif
}

u64 au_os_time() {
	struct timeval t;
	gettimeofday(&t, NULL);
	return t.tv_sec * 1000000000 + t.tv_usec * 1000;
}

double au_os_time_s() {
	struct timeval t;
	gettimeofday(&t, NULL);
	return (double)t.tv_sec + t.tv_usec / 1e6;
}

u64 au_os_time_native() {
	// xx should nanos be considered the native unit? micros are the posix
	// standard.
	struct timeval t;
	gettimeofday(&t, NULL);
	return t.tv_sec * 1000000 + t.tv_usec;
}

double au_os_time_native_to_nanoseconds() { return 1E3; }

double au_os_time_native_to_seconds() { return 1E-6; }

double cpu_time_to_ns = 0.0;

#ifdef __aarch64
u64 au_cpu_time_() {
	// xx for now, use CLOCK_MONOTONIC as cpu time
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return 1000000000ULL * ts.tv_sec + ts.tv_nsec;
}
#endif

double au_cpu_time_to_nanoseconds() {
#ifdef __aarch64__
	return 1.0;
#else
	if (!cpu_time_to_ns) {
		au_calibrate_by_sleeping(15);
	}
	return cpu_time_to_ns;
#endif
}

double au_cpu_time_to_seconds() {
#ifdef __aarch64__
	return 1E-9;
#else
	if (!cpu_time_to_ns) {
		au_calibrate_by_sleeping(15);
	}
	return cpu_time_to_ns * 1E-9;
#endif
}

void au_calibrate_by_sleeping(i32 ms) {
#ifdef __aarch64__
	// nop
#else
	i64 os_time_start = au_os_time_native();
	i64 cpu_time_start = au_cpu_time();
	au_sleep(ms);
	i64 os_time_stop = au_os_time_native();
	i64 cpu_time_stop = au_cpu_time();

	cpu_time_to_ns =
			((os_time_stop - os_time_start) * au_os_time_native_to_nanoseconds()) /
			(double)(cpu_time_stop - cpu_time_start);
#endif
}

void au_sleep(i32 millis) { usleep(millis * 1000); }

u64 au_get_modification_time(char **filenames, int n) {
	u64 latest_time = 0;
	for (int i = 0; i < n; i++) {
		struct stat info;
		i32 retries = 0;
		while (retries < 10 && stat(filenames[i], &info) < 0) {
			retries++;
			au_sleep(10);
		}
		assertf(retries >= 10,
						"au_get_modification_time: Failed to stat file: %s\n",
						filenames[i]);
#ifdef __APPLE__
		u64 time =
				info.st_mtimespec.tv_sec * 1000 + info.st_mtimespec.tv_nsec / 1000000;
#else
		u64 time = info.st_mtim.tv_sec * 1000 + info.st_mtim.tv_nsec / 1000000;
#endif
		latest_time = MAX(time, latest_time);
	}
	return latest_time;
}

void au_run_command_in_dir(char *command, char *dir) {
	char old_dir[1024];
	assertf(getcwd(old_dir, 1024),
					"au_run_command_in_dir: Failed to get current directory");
	chdir(dir);
	system(command);
	chdir(old_dir);
}

void *au_load_library(char *path) { return dlopen(path, RTLD_LAZY); }

void au_unload_library(void *library) { dlclose(library); }

void *au_get_function(void *library, char *func_name) {
	return dlsym(library, func_name);
}
