#ifndef AUTIL_H
#define AUTIL_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdarg.h>

#if defined(_WIN32) && defined(AUTIL_IS_DLL)
	#ifdef AUTIL_IMPLEMENTATION
		#define DLL_LINK __declspec(dllexport)
	#elif AUTIL_IN_DLL
		#define DLL_LINK
	#else
		#define DLL_LINK __declspec(dllimport)
	#endif
#else
	#define DLL_LINK
#endif

#ifndef M_PI
#define M_PI 3.1415926535897932385
#endif
#define F_PI 3.1415926535897932385f

#undef MIN
#undef MAX
#undef ABS
#define MIN(x, y) ((x)<(y) ? (x) : (y))
#define MAX(x, y) ((x)>(y) ? (x) : (y))
#define ABS(x) ((x)<0 ? -(x) : (x))
#define CLAMP(x, min, max) ((x) < (min) ? (min) : ((x) > (max) ? (max) : (x)))

typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

#ifdef __cplusplus
extern "C" {
#endif

void errexit(const char *format, ...);

void errexit_unless(bool condition, const char *format, ...);

#define assertf errexit_unless
#define dev_assertf assertf

/************************************** Memory management *****************************************/

#define AU_DEFAULT_RESERVE_SIZE (1ULL << 36)
#define AU_DEFAULT_COMMIT_SIZE (1ULL << 20)

#define AU_EXPAND_POLICY_ALWAYS_CRASH 1
#define AU_EXPAND_POLICY_EXTEND_OR_CRASH 2

#define AU_ALLOC_STATIC 0
#define AU_ALLOC_DYNAMIC 1

/*
** An Arena represents a region of memory. Arenas are not just attached to a static (bump) allocator: they can
** have a static allocator, a dynamic allocator, or both, each with its own subregion of the arena. Allocations
** made with the dynamic allocator can be individually freed and reused, while allocations made with the
** static allocator cannot be. However, the entire arena can be freed as a group with arena_reset.
*/
typedef struct arena {
	i32 type;
	i32 default_alloc;
	i32 expand_policy;
	u64 static_reserve_size;
	u64 static_commit_size;
	u64 dynamic_reserve_size;
	u64 dynamic_commit_size;
	u8 *start;
	u8 *next;
	u8 *end;
	u8 *dyn_data;
	u8 *dyn_end;
} Arena;

DLL_LINK extern Arena app_arena;

bool arena_init_at(Arena *arena, void *static_start, u64 static_reserve_size, u64 static_commit_size,
	void *dynamic_start, u64 dynamic_reserve_size, u64 dynamic_commit_size, i32 default_alloc,
	i32 expand_policy);

bool arena_init_ex(Arena *arena, u64 static_reserve_size, u64 static_commit_size,
	u64 dynamic_reserve_size, u64 dynamic_commit_size, i32 default_alloc, i32 expand_policy);

// Initialize an arena with both a static and dynamic allocator and the default reserve and commit sizes.
// The default allocator will be static.
bool arena_init(Arena *arena);

// Initialize an arena with only a static allocator.
bool arena_init_static(Arena *arena);

// Initialize an arena with only a dynamic allocator.
bool arena_init_dynamic(Arena *arena);

void arena_reset(Arena *arena);

void arena_align(Arena *arena, i16 align);

Arena arena_copy(Arena *arena);

Arena *temp_arena_create();

void temp_arena_delete(Arena *arena);

#ifdef AU_DEBUG_MEM

void *aalloc_debug_mem(Arena *arena, u64 size, char *file, i32 line);

void *asalloc_debug_mem(Arena *arena, u64 size, char *file, i32 line);

void *adalloc_debug_mem(Arena *arena, u64 size, char *file, i32 line);

void *arealloc_debug_mem(Arena *arena, const void *ptr, u64 size, char *file, i32 line);

void afree_debug_mem(Arena *arena, const void *ptr, char *file, i32 line);

void arena_print_usage(Arena *arena);

// Print allocations logged with a _debug_mem function.
void arena_print_logged_allocations(Arena *arena);

#define aalloc(a, n) aalloc_debug_mem((a), (n), __FILE__, __LINE__)

#define asalloc(a, n) asalloc_debug_mem((a), (n), __FILE__, __LINE__)

#define adalloc(a,n) adalloc_debug_mem((a), (n), __FILE__, __LINE__)

#define arealloc(a,p,n) arealloc_debug_mem((a), (p), (n), __FILE__, __LINE__)

#define afree(a, n) afree_debug_mem((a), (n), __FILE__, __LINE__)

#else

// Allocate with the default allocator of the arena (usually static).
void *aalloc(Arena *arena, u64 size);

// Allocate with the static alloctor if present; otherwise, fall back to the dynamic allocator.
void *asalloc(Arena *arena, u64 size);

// Allocate with the dynamic alloctor if present; otherwise, fall back to the static allocator.
void *adalloc(Arena *arena, u64 size);

// Resize an allocation. If 'ptr' was allocated with the dynamic allocator, the old allocation will be freed.
void *arealloc(Arena *arena, const void *ptr, u64 size);

// Free an allocation. If 'ptr' was allocated with the dynamic allocator, the old allocation will be reusable.
// If it was allocated with the static allocator, this function does nothing.
void afree(Arena *arena, const void *ptr);

#endif

#ifdef __cplusplus
}
#endif

#endif
