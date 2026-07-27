#define WIN32_LEAN_AND_MEAN
#define VC_EXTRALEAN
#include <windows.h>
#include <stdlib.h>
#include <stdio.h>
#include <direct.h>

#include "au_core.h"
#include "au_platform.h"

struct au_windows_thread {
	/* keep in line autil.h: */
	au_thread_func f;
	void *arg;
	bool running;
	/* Windows specific: */
	HANDLE handle;
};

static unsigned long au_windows_thread_wrapper(void *arg)
{
	struct au_windows_thread *thr = (struct au_windows_thread *) arg;
	thr->running = true;
	thr->f(thr->arg);
	thr->running = false;
	return 0;
}

struct au_thread *au_create_thread(Arena *arena, au_thread_func f, void *arg)
{
	struct au_windows_thread *res = aalloc(arena, sizeof(struct au_windows_thread));
	res->f = f;
	res->arg = arg;
	return (struct au_thread *) res;
}

bool au_start_thread(struct au_thread *thread)
{
	struct au_windows_thread *thr = (struct au_windows_thread *) thread;
	thr->handle = CreateThread(0, 0, au_windows_thread_wrapper, (void *) thr, 0, 0);
	return thr->handle ? true : false;
}

void au_join_thread(struct au_thread *thread, i32 timeout_ms)
{
	struct au_windows_thread *thr = (struct au_windows_thread *) thread;
	if (timeout_ms >= 0)
		WaitForSingleObject(thr->handle, timeout_ms);
	else
		WaitForSingleObject(thr->handle, INFINITE);
}

// xx au_windows_mutex?
struct au_mutex {
	HANDLE handle;
};

struct au_mutex *au_create_mutex(Arena *arena)
{
	struct au_mutex *res = aalloc(arena, sizeof(struct au_mutex));
	res->handle = CreateMutex(NULL, FALSE, NULL);
	return res;
}

bool au_lock_mutex(struct au_mutex *mutex)
{
	if (WaitForSingleObject(mutex->handle, INFINITE) == WAIT_OBJECT_0) {
		return true;
	} else {
		return false;
	}
}

bool au_unlock_mutex(struct au_mutex *mutex)
{
	return ReleaseMutex(mutex->handle);
}

double os_timer_frequency = 0;

bool au_pin_to_core(int core)
{
    BOOL ok = SetProcessAffinityMask(GetCurrentProcess(), 1ULL << core);
    return ok;
}

u64 au_os_time()
{
	if (!os_timer_frequency) {
		LARGE_INTEGER t;
		QueryPerformanceFrequency(&t);
		os_timer_frequency = t.QuadPart;
	}
	LARGE_INTEGER t;
	QueryPerformanceCounter(&t);
	return t.QuadPart * ((double) 1000000000 / os_timer_frequency);
}

double au_os_time_s()
{
	if (!os_timer_frequency) {
		LARGE_INTEGER t;
		QueryPerformanceFrequency(&t);
		os_timer_frequency = t.QuadPart;
	}
	LARGE_INTEGER t;
	QueryPerformanceCounter(&t);
	return t.QuadPart / os_timer_frequency;
}

u64 au_os_time_native()
{
	LARGE_INTEGER t;
	QueryPerformanceCounter(&t);
	return t.QuadPart;
}

double au_os_time_native_to_nanoseconds()
{
	if (!os_timer_frequency) {
		LARGE_INTEGER t;
		QueryPerformanceFrequency(&t);
		os_timer_frequency = t.QuadPart;
	}
	return 1E9 / os_timer_frequency;
}

double au_os_time_native_to_seconds()
{
	if (!os_timer_frequency) {
		LARGE_INTEGER t;
		QueryPerformanceFrequency(&t);
		os_timer_frequency = t.QuadPart;
	}
	return 1.0 / os_timer_frequency;
}

#ifdef __aarch64__
u64 au_cpu_time_() {
	errexit("cpu_time is not supported on Windows-ARM.\n");
}
#endif

double cpu_time_to_ns = 0.0;

double au_cpu_time_to_nanoseconds()
{
	if (!cpu_time_to_ns) {
		au_calibrate_by_sleeping(15);
	}
	return cpu_time_to_ns;
}

double au_cpu_time_to_seconds()
{
	if (!cpu_time_to_ns) {
		au_calibrate_by_sleeping(15);
	}
	return cpu_time_to_ns * 1E-9;
}

void au_calibrate_by_sleeping(i32 ms)
{
	i64	os_time_start = au_os_time_native();
	i64 cpu_time_start = au_cpu_time();
	au_sleep(ms);
	i64 os_time_stop = au_os_time_native();
	i64 cpu_time_stop = au_cpu_time();

	cpu_time_to_ns = ((os_time_stop - os_time_start)*au_os_time_native_to_nanoseconds())
		/ (double) (cpu_time_stop - cpu_time_start);
}

void au_sleep(i32 millis) {
	Sleep(millis);
}

u64 au_get_modification_time(char **filenames, int n)
{
	u64 max_time = 0;
	for (int i=0; i<n; i++) {
	    WIN32_FIND_DATA find_data;
	    HANDLE find_handle = FindFirstFile(filenames[i], &find_data);

	    if (find_handle == INVALID_HANDLE_VALUE) {
	        return 0;
	    }

	    ULARGE_INTEGER file_time;
	    file_time.LowPart = find_data.ftLastWriteTime.dwLowDateTime;
	    file_time.HighPart = find_data.ftLastWriteTime.dwHighDateTime;

	    FindClose(find_handle);

	    max_time = MAX(max_time, file_time.QuadPart);
	}
	return max_time;
}

void au_run_command_in_dir(char *command, char *dir)
{
	char old_dir[1024];
	assertf(_getcwd(old_dir, 1024), "au_run_command_in_dir: Failed to get current directory");
	_chdir(dir);
	system(command);
	_chdir(old_dir);
}

void *au_load_library(char *path)
{
	i32 len = strlen(path);
	if (strcmp(&path[len-4], ".dll") != 0)
		return NULL;
	char alt_path[1028];
	memcpy(alt_path, path, len-4);
	sprintf(&alt_path[len-4], "_alt.dll");
	i32 retries = 0;
	while (!CopyFile(path, alt_path, false) && retries < 10) {
		Sleep(5);
		retries++;
	}
	if (retries >= 10)
		return NULL;
	HMODULE library = LoadLibrary(alt_path);
	return (void *) library;
}

void au_unload_library(void *library)
{
	if (!FreeLibrary((HMODULE) library)) {
		printf("WARNING: dll failed to free\n");
	}
}

void *au_get_function(void *library, char *func_name)
{
	return (void *) GetProcAddress(library, func_name);
}
