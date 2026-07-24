#define AUTIL_IMPLEMENTATION
#include "au_core.h"
#include "tlsf.h"

// don't use _debug_mem macros internally
#undef aalloc
#undef asalloc
#undef adalloc
#undef arealloc
#undef afree
void *aalloc(Arena *arena, u64 size);
void *asalloc(Arena *arena, u64 size);
void *adalloc(Arena *arena, u64 size);
void *arealloc(Arena *arena, const void *ptr, u64 size);
void afree(Arena *arena, const void *ptr);

#ifdef __has_feature
	#if __has_feature(address_sanitizer) // gcc, clang
		#define AU_ASAN_ENABLED
		#define AU_NO_ASAN __attribute__((no_sanitize("address")))
	#endif
#elif defined(__SANITIZE_ADDRESS__) // msvc
	#define AU_ASAN_ENABLED
	#define AU_NO_ASAN __declspec(no_sanitize_address)
#endif

#ifdef AU_ASAN_ENABLED
	#include <sanitizer/asan_interface.h>
#else
	#define AU_NO_ASAN
#endif

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#ifdef _WIN32
#pragma intrinsic(_BitScanForward64)
#pragma intrinsic(_BitScanReverse64)
#define WIN32_LEAN_AND_MEAN
#define VC_EXTRALEAN
#include <windows.h>
#else
#endif

#define MAX_RETRIES 3

DLL_LINK Arena app_arena = { 0 };

void errexit(const char *format, ...)
{
	va_list args;
	va_start(args, format);
	vfprintf(stderr, format, args );
	va_end(args);
	exit(1);
}

void fire_assert(const char *format, va_list args)
 {
	if (format) {
		vfprintf(stderr, format, args );
	} else {
		fprintf(stderr, "Programmer error, have to stop.\n");
	}
	exit(1);
}

void errexit_unless(bool condition, const char *format, ...)
{
	va_list args;
	va_start(args, format);
	if (!condition) {
		fire_assert(format, args);
	}
	va_end(args);
}

/************************************** Core math ************************************************/

// Redefine core math functions so this does not requre au_math.h

#ifdef _MSC_VER

#pragma intrinsic(_BitScanForward64, _BitScanReverse64)

i32 core_ffs(u64 x)
{
	unsigned long i;
	if (_BitScanForward64(&i, x))
		return i;
	return -1;
}

i32 core_fls(u64 x)
{
	unsigned long i;
	if (_BitScanReverse64(&i, x))
		return i;
	return -1;
}

#elif defined(__GNUC__) || defined(__clang__)

i32 core_ffs(u64 x)
{
	if (x == 0) return -1;
	return (63 - __builtin_ctzll(x));
}
i32 core_fls(u64 x)
{
	if (x == 0) return -1;
	return (63 - __builtin_clzll(x));
}

#else

static i32 _ctz32(u32 x) {
   i32 n;
   if (x == 0) return 32;
   n = 1;
   if ((x & 0x0000FFFF) == 0) {n += 16; x = x >>16;}
   if ((x & 0x000000FF) == 0) {n += 8; x = x >> 8;}
   if ((x & 0x0000000F) == 0) {n += 4; x = x >> 4;}
   if ((x & 0x00000003) == 0) {n += 2; x = x >> 2;}
   return n - (x & 1);
}

static i32 _clz32(u32 x) {
     i32 n = 0;
     if (x == 0) return 32;
     if (x <= 0x0000FFFFu) { n += 16; x <<= 16; }
     if (x <= 0x00FFFFFFu) { n += 8;  x <<= 8;  }
     if (x <= 0x0FFFFFFFu) { n += 4;  x <<= 4;  }
     if (x <= 0x3FFFFFFFu) { n += 2;  x <<= 2;  }
     if (x <= 0x7FFFFFFFu) { n += 1; }
     return n;
 }

 static i32 _ctz64(u64 x) {
 	u32 lo = (u32) x;
 	u32 hi = (u32) (x >> 32);
 	return lo ? _ctz32(lo) : 32 + _ctz32(hi);
 }

static i32 _clz64(u64 x) {
     u32 hi = (u32)(x >> 32);
     return hi ? _clz32(hi) : 32 + _clz32((u32)x);
 }

i32 core_ffs(u64 x)
{
	return (63 - _ctz64(x));
}

i32 core_fls(u64 x)
{
	return (63 - _clz64(x));
}

#endif

static u64 core_round_up_to_pow2(u64 x)
{
	if (x <= 1)
		return 1;
	return (u64) 1 << (core_fls(x-1) + 1);
}

/***************************** Memory management ******************************/

typedef struct allocation_record {
	void *addr;
	u64 size;
	bool freed;
	char *file;
	i32 line;
	i32 type;
	struct allocation_record *prev;
	struct allocation_record *next;
} Allocation_Record;

typedef struct arena_record {
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
	struct arena_record *link_prev;
	struct arena_record *link_next;
} Arena_Record;

typedef struct allocation_log_arena_header {
	void *base;
	Arena_Record *arenas;
	Arena_Record *arenas_end;
	Allocation_Record *allocations;
	Allocation_Record *allocations_end;
} Allocation_Log_Arena_Header;

Arena _allocation_log_arena;

static void *try_reserve_and_commit_anywhere(u64 reserve_size, u64 commit_size);

static void init_allocation_log(void)
{
	u64 commit_size = AU_DEFAULT_COMMIT_SIZE;
	void *map_addr = try_reserve_and_commit_anywhere(AU_DEFAULT_RESERVE_SIZE, commit_size);
	errexit_unless(map_addr, "Failed to map allocation log.\n");

	_allocation_log_arena.type = 1;
	_allocation_log_arena.default_alloc = AU_ALLOC_STATIC;
	_allocation_log_arena.expand_policy = AU_EXPAND_POLICY_EXTEND_OR_CRASH;
	_allocation_log_arena.start = map_addr;
	_allocation_log_arena.next = (u8 *) map_addr + sizeof(Allocation_Log_Arena_Header);
	_allocation_log_arena.end = (u8 *) map_addr + commit_size;
	_allocation_log_arena.static_commit_size = commit_size;
	_allocation_log_arena.static_reserve_size = AU_DEFAULT_RESERVE_SIZE;

	Allocation_Log_Arena_Header *header = (Allocation_Log_Arena_Header *) map_addr;

	Arena_Record *arena_rec = aalloc(&_allocation_log_arena, sizeof(Arena_Record));
	memcpy(arena_rec, &_allocation_log_arena, sizeof(Arena));
	arena_rec->link_prev = NULL;
	arena_rec->link_next = NULL;

	header->base = map_addr;
	header->arenas = arena_rec;
	header->arenas_end = arena_rec;
	header->allocations = NULL;
	header->allocations_end = NULL;
}

static void log_allocation(void *addr, u64 size, char *file, i32 line)
{
	if (!_allocation_log_arena.start) {
		init_allocation_log();
	}
	Allocation_Record *record = aalloc(&_allocation_log_arena, sizeof(Allocation_Record));
	record->addr = addr;
	record->size = size;
	record->file = file;
	record->line = line;
	Allocation_Log_Arena_Header *header =
		(Allocation_Log_Arena_Header *) _allocation_log_arena.start;
	if (header->allocations) {
		header->allocations_end->next = record;
		record->prev = header->allocations_end;
	} else {
		header->allocations = record;
	}
	header->allocations_end = record;
}

static void log_free(const void *addr)
{
	if (!_allocation_log_arena.start) {
		init_allocation_log();
	}
	// xxx slow linked list traversal
	Allocation_Log_Arena_Header *header =
		(Allocation_Log_Arena_Header *) _allocation_log_arena.start;
	Allocation_Record *a = header->allocations;
	for (a = header->allocations; a; a = a->next) {
		if (a->addr == addr)
			a->freed = true;
	}
}

void arena_print_logged_allocations(Arena *arena)
{
	Allocation_Record *a = (Allocation_Record *) _allocation_log_arena.start;
	printf("Static allocations:\n");
	for (; (u8 *) a<_allocation_log_arena.end; a++) {
		u8 *addr = (u8 *) a->addr;
		if (addr >= arena->start && addr < arena->end) {
			printf("%p: size: %llu origin:%s:%d\n", a->addr, a->size, a->file, a->line);
		}
	}
	struct dyn_header *dyn = (struct dyn_header *) arena->dyn_data;
	printf("Dynamic allocations:\n");
	for (; (u8 *) a<_allocation_log_arena.end; a++) {
		u8 *addr = (u8 *) a->addr;
		// if (addr >= arena->dyn_data && addr < dyn->end) {
		// 	printf("%llx: size: %llu origin:%s:%d\n", a->addr, a->size, a->file, a->line);
		// }
	}
}

static bool au_commit_memory(void *loc, u64 size)
{
#ifdef _WIN32
	void *result = VirtualAlloc(loc, size, MEM_COMMIT, PAGE_READWRITE);
	if (result != loc) {
		DWORD err = GetLastError();
		printf("au_commit_memory: VirtualAlloc failed with error: %lu\n", err);
		return false;
	} else {
		return true;
	}
#else
	i32 retc = mprotect(loc, size, PROT_READ | PROT_WRITE)
	if (retc < 0) {
		printf("au_commit_memory: mprotect failed with errno: %d\n", errno);
		return false;
	} else {
		return true;
	}
#endif
}

static void au_decommit_memory(void *loc, u64 size)
{
#ifdef _WIN32
	VirtualFree(loc, size, MEM_DECOMMIT);
#else
  madvise(loc, size, MADV_DONTNEED);
  mprotect(loc, size, PROT_NONE);
#endif
}

static void *au_reserve_memory(void *loc, u64 size)
{
#ifdef _WIN32
	void *result = VirtualAlloc(loc, size, MEM_RESERVE, PAGE_READWRITE);
	if (!result) {
		DWORD err = GetLastError();
		printf("au_reserve_memory: VirtualAlloc failed with error: %lu\n", err);
	}
	return result;
#else
	void *result = mmap(loc, size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS,
		-1, 0);
	if (result == MAP_FAILED)
		printf("au_reserve_memory: mmap failed with errno: %d\n", errno);
	result = (result == (u8 *) MAP_FAILED) ? NULL : result;
#endif
}

void au_release_memory(void *loc, u64 size)
{
#ifdef _WIN32
	// Size passed to VirtualFree with MEM_RELEASE must be zero.
	VirtualFree(loc, 0, MEM_RELEASE);
#else
	munmap(loc, size);
#endif
}

static bool try_reserve_and_commit_at(void *loc, u64 reserve_size, u64 commit_size)
{
	void *reserved_location = au_reserve_memory(loc, reserve_size);
	if (reserved_location == loc) {
		bool ok = au_commit_memory(reserved_location, commit_size);
		if (ok) {
			return true;
		} else {
			au_release_memory(reserved_location, reserve_size);
			return false;
		}
	} else {
		if (reserved_location)
			au_release_memory(reserved_location, reserve_size);
		return false;
	}
}

static void *try_reserve_and_commit_anywhere(u64 reserve_size, u64 commit_size)
{
	void *reserved_location = au_reserve_memory(NULL, reserve_size);
	if (!reserved_location)
		return NULL;
	else {
		bool ok = au_commit_memory(reserved_location, commit_size);
		if (ok) {
			return reserved_location;
		} else {
			au_release_memory(reserved_location, reserve_size);
			return NULL;
		}
	}
}

void *amap_memory(void *loc, u64 size)
{
#ifdef _WIN32
	void *result = VirtualAlloc(loc, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	DWORD err = GetLastError();
	if (!result)
		printf("err: %lu\n", err);
#else
	void *result = mmap(loc, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS,
		-1, 0);
	if (result == MAP_FAILED)
		printf("errno: %d\n", errno);
	result = (result == (u8 *) MAP_FAILED) ? NULL : result;
#endif
	return result;
}

bool aunmap_memory(void *loc, u64 size)
{
#ifdef _WIN32
	return VirtualFree(loc, 0, MEM_RELEASE);
#else
	return munmap(loc, size) + 1;
#endif
}

bool arena_init_at(Arena *arena, void *static_start, u64 static_reserve_size, u64 static_commit_size,
	void *dynamic_start, u64 dynamic_reserve_size, u64 dynamic_commit_size, i32 default_alloc,
	i32 expand_policy)
{
#ifdef AU_DEBUG_MEM
	if (!_allocation_log_arena.start) {
		init_allocation_log();
	}
	Allocation_Log_Arena_Header *header =
		(Allocation_Log_Arena_Header *) _allocation_log_arena.start;
#endif
	if (static_reserve_size && static_commit_size) {
		bool ok = try_reserve_and_commit_at(static_start, static_reserve_size, static_commit_size);
		if (!ok)
			return false;
		arena->start = (u8 *) static_start;
		arena->next = arena->start;
		arena->end = arena->start + static_commit_size;
	} else {
		arena->start = arena->next = arena->end = NULL;
	}
	if (dynamic_commit_size) {
		bool ok = try_reserve_and_commit_at(dynamic_start, dynamic_reserve_size, dynamic_commit_size);
		if (!ok)
			return false;
		arena->dyn_data = (u8 *) dynamic_start;
		arena->dyn_end = arena->dyn_data + dynamic_commit_size;
		tlsf_create_with_pool(arena->dyn_data, dynamic_commit_size);
	} else {
		arena->dyn_data = arena->dyn_end = NULL;
	}
	arena->type = 1;
	arena->default_alloc = default_alloc;
	arena->expand_policy = expand_policy;
	arena->static_reserve_size = static_reserve_size;
	arena->static_commit_size = static_commit_size;
	arena->dynamic_reserve_size = dynamic_reserve_size;
	arena->dynamic_commit_size = dynamic_commit_size;
#ifdef AU_DEBUG_MEM
	Arena_Record *arena_rec = aalloc(&_allocation_log_arena, sizeof(Arena_Record));
	memcpy(arena_rec, arena, sizeof(Arena));
	// allocation log area recored guaranteed to already be on the list.
	header->arenas_end->link_next = arena_rec;
	arena_rec->link_prev = header->arenas_end;
	header->arenas_end = arena_rec;
#endif
	return true;
}

u64 toplevel_temp_location = 0x180000000000;
Arena toplevel_temp_arena;
bool toplevel_temp_initialized = false;
Arena *current_temp_arena = NULL;

// Windows requires 64k alignment for VirtualAlloc
u64 arena_static_locations[] = {
	0x1a0000010000,
	0x1b0000020000,
	0x1c0000030000,
	0x1d0000040000,
	0x1e0000050000,
	0x1f0000060000,
};
i32 arena_static_locations_length = sizeof(arena_static_locations)/sizeof(u64);
i32 arena_static_locations_i = 0;

u64 arena_dyn_locations[] = {
	0x1ad000010000,
	0x1bd000020000,
	0x1cd000030000,
	0x1dd000040000,
	0x1ed000050000,
	0x1fd000060000,
};
i32 arena_dyn_locations_length = sizeof(arena_dyn_locations)/sizeof(u64);
i32 arena_dyn_locations_i = 0;

bool arena_init_ex(Arena *arena, u64 static_reserve_size, u64 static_commit_size,
	u64 dynamic_reserve_size, u64 dynamic_commit_size, i32 default_alloc, i32 expand_policy)
{
#ifdef AU_DEBUG_MEM
	if (!_allocation_log_arena.start) {
		init_allocation_log();
	}
	Allocation_Log_Arena_Header *header =
		(Allocation_Log_Arena_Header *) _allocation_log_arena.start;
#endif
	if (static_reserve_size && static_commit_size) {
		// Try one of the preset addresses, then system-assigned address.
		arena->start = NULL;
		while (!arena->start && arena_static_locations_i < arena_static_locations_length) {
			void *location = (void *) arena_static_locations[arena_static_locations_i++];
			bool ok = try_reserve_and_commit_at(location, static_reserve_size, static_commit_size);
			if (ok) {
				arena->start = (u8 *) location;
				break;
			}
		}
		if (!arena->start) {
			arena->start = (u8 *) try_reserve_and_commit_anywhere(static_reserve_size, static_commit_size);
			if (!arena->start)
				return false;
		}
		arena->next = arena->start;
		arena->end = arena->start + static_commit_size;
	} else {
		arena->start = arena->next = arena->end = NULL;
	}
	if (dynamic_reserve_size && dynamic_commit_size) {
		arena->dyn_data = NULL;
		while (!arena->dyn_data && arena_dyn_locations_i < arena_dyn_locations_length) {
			void *dyn_location = (void *) arena_dyn_locations[arena_dyn_locations_i++];
			bool ok = try_reserve_and_commit_at(dyn_location, dynamic_reserve_size, dynamic_commit_size);
			if (ok) {
				arena->dyn_data = (u8 *) dyn_location;
				break;
			}
		}
		if (!arena->dyn_data) {
			arena->dyn_data =
				(u8 *) try_reserve_and_commit_anywhere(dynamic_reserve_size, dynamic_commit_size);
			if (!arena->dyn_data)
				return false;
		}
		arena->dyn_end = arena->dyn_data + dynamic_commit_size;
		tlsf_create_with_pool(arena->dyn_data, dynamic_commit_size);
	} else {
		arena->dyn_data = arena->dyn_end = NULL;
	}
	arena->type = 1;
	arena->default_alloc = default_alloc;
	arena->expand_policy = expand_policy;
	arena->static_reserve_size = static_reserve_size;
	arena->static_commit_size = static_commit_size;
	arena->dynamic_reserve_size = dynamic_reserve_size;
	arena->dynamic_commit_size = dynamic_commit_size;
#ifdef AU_DEBUG_MEM
	Arena_Record *arena_rec = aalloc(&_allocation_log_arena, sizeof(Arena_Record));
	memcpy(arena_rec, arena, sizeof(Arena));
	// allocation log area recored guaranteed to already be on the list.
	header->arenas_end->link_next = arena_rec;
	arena_rec->link_prev = header->arenas_end;
	header->arenas_end = arena_rec;
#endif
	return true;
}

bool arena_init(Arena *arena)
{
	return arena_init_ex(arena, AU_DEFAULT_RESERVE_SIZE, AU_DEFAULT_COMMIT_SIZE,
		AU_DEFAULT_RESERVE_SIZE, AU_DEFAULT_COMMIT_SIZE, AU_ALLOC_STATIC, AU_EXPAND_POLICY_EXTEND_OR_CRASH);
}

bool arena_init_static(Arena *arena)
{
	return arena_init_ex(arena, AU_DEFAULT_RESERVE_SIZE, AU_DEFAULT_COMMIT_SIZE, 0, 0, AU_ALLOC_STATIC,
		AU_EXPAND_POLICY_EXTEND_OR_CRASH);
}

bool arena_init_dynamic(Arena *arena)
{
	return arena_init_ex(arena, 0, 0, AU_DEFAULT_RESERVE_SIZE, AU_DEFAULT_COMMIT_SIZE, AU_ALLOC_DYNAMIC,
		AU_EXPAND_POLICY_EXTEND_OR_CRASH);
}

void arena_reset(Arena *arena)
{
	if (arena->start) {
		arena->next = arena->start;
		memset(arena->start, 0, arena->end - arena->start);
	}
	if (arena->dyn_data) {
		u64 dyn_size = arena->dyn_end - arena->dyn_data;
#ifdef AU_ASAN_ENABLED
		ASAN_UNPOISON_MEMORY_REGION(arena->dyn_data, dyn_size);
#endif
		memset(arena->dyn_data, 0, dyn_size);
		tlsf_create_with_pool(arena->dyn_data, dyn_size);
	}
}

// 'align' must be a power of 2
void arena_align(Arena *arena, i16 align)
{
	// RHS == align - (arena->start % align). Also, u64 could be uintptr_t.
	arena->next += -(u64)arena->next & (align-1);
}

Arena arena_copy(Arena *arena)
{
	Arena res;
	res.start = arena->start;
	res.next = arena->next;
	res.end = arena->end;
	return res;
}

static void initialize_toplevel_temp()
{
	bool ok = arena_init_at(&toplevel_temp_arena,
		(void *) toplevel_temp_location, AU_DEFAULT_RESERVE_SIZE, 1 << 26,
		NULL, 0, 0, AU_ALLOC_STATIC, AU_EXPAND_POLICY_EXTEND_OR_CRASH);
	if (!ok) {
		errexit_unless(arena_init_static(&toplevel_temp_arena), "Could not initialize temp arenas.\n");
	}
	toplevel_temp_initialized = true;
}

Arena *temp_arena_create()
{
	if (!toplevel_temp_initialized)
		initialize_toplevel_temp();
	if (!current_temp_arena) {
		current_temp_arena = aalloc(&toplevel_temp_arena, sizeof(Arena));
		current_temp_arena->dyn_end = NULL;
	} else {
		// Arena *new_temp_arena = aalloc(current_temp_arena, sizeof(Arena));
		toplevel_temp_arena.next = current_temp_arena->next;
		toplevel_temp_arena.end = current_temp_arena->end;
		Arena *previous_temp_arena = current_temp_arena;
		current_temp_arena = aalloc(&toplevel_temp_arena, sizeof(Arena));
		// sneak a linked list of temp arenas in unused dyn_end field.
		current_temp_arena->dyn_end = (u8 *) previous_temp_arena;
	}
	current_temp_arena->start = toplevel_temp_arena.next;
	current_temp_arena->next = toplevel_temp_arena.next;
	current_temp_arena->end = toplevel_temp_arena.end;
	current_temp_arena->dyn_data = NULL;
	current_temp_arena->type = 1;
	current_temp_arena->default_alloc = AU_ALLOC_STATIC;
	current_temp_arena->expand_policy = AU_EXPAND_POLICY_EXTEND_OR_CRASH;
	current_temp_arena->static_commit_size = toplevel_temp_arena.static_commit_size;
	return current_temp_arena;
}

void temp_arena_delete(Arena *arena)
{
	errexit_unless(arena == current_temp_arena, "Arena: Mismatched create/destroy temp arena calls.\n");
	u8 *temp_next = arena->next;
	// temp arena may have resized
	toplevel_temp_arena.end = current_temp_arena->end;
	// reset to before we allocated current_temp_arena
	toplevel_temp_arena.next = (void *) current_temp_arena;

	// jump back to previous temp arena, and also resize that if necessary
	current_temp_arena = (Arena *) current_temp_arena->dyn_end;
	if (current_temp_arena)
		current_temp_arena->end = toplevel_temp_arena.end;

	memset(toplevel_temp_arena.next, 0, temp_next - toplevel_temp_arena.next);
}

// Expand arena based on policy and expand amount
static void *arena_expand(Arena *arena, bool expand_dynamic, u64 expand_size) {
	void *ret;
	if (arena->expand_policy == AU_EXPAND_POLICY_ALWAYS_CRASH) {
		errexit("Arena: out of memory.\n");
	} else {
		tlsf_t tlsf = (tlsf_t) arena->dyn_data;
		void *try_expand_addr = expand_dynamic ? arena->dyn_end : arena->end;
		errexit_unless(try_expand_addr, "Arena: cannot expand unitialized arena.\n");

		bool ok = au_commit_memory(try_expand_addr, expand_size);
		if (!ok) {
				errexit("Arena: out of memory and could not extend "
					"contiguously(AU_EXPAND_POLICY_EXTEND_OR_CRASH).\n");
		}
		if (expand_dynamic) {
			arena->dyn_end += expand_size;
		} else {
			arena->end += expand_size;
		}
		if (expand_dynamic) {
			ret = tlsf_add_pool(tlsf, try_expand_addr, expand_size);
		} else {
			ret = NULL;
		}
	}
#ifdef AU_DEBUG_MEM
	// Find and update the arena record for this arena.
	Allocation_Log_Arena_Header *header =
		(Allocation_Log_Arena_Header *) _allocation_log_arena.start;
	Arena_Record *record = header->arenas;
	while (record) {
		if (arena->start && record->start == arena->start)
			break;
		if (arena->dyn_data && record->dyn_data == arena->dyn_data)
			break;
		record = record->link_next;
	}
	if (record) {
		memcpy(record, arena, sizeof(Arena));
	}
#endif
	return ret;
}

static void arena_unexpand_dynamic(Arena *arena, u64 expand_size, void *pool)
{
	tlsf_t tlsf = (tlsf_t) arena->dyn_data;
	arena->dyn_end -= expand_size;
	tlsf_remove_pool(tlsf, pool);
	au_decommit_memory(arena->dyn_end, expand_size);
}

static void *asalloc_internal(Arena *arena, u64 size)
{
	arena_align(arena, 8);
	u64 advance_size = size + sizeof(u64);
	if (arena->end - arena->next < advance_size) {
		u64 expand_size = MAX(arena->static_commit_size, core_round_up_to_pow2(advance_size));
		// xx Can crash
		arena_expand(arena, false, expand_size);
	}
	void *ret = (void *) (arena->next + sizeof(u64));
	errexit_unless(arena->next + advance_size > arena->next, "Arena: pointer arithmetic overflow.\n");
	* (u64 *) arena->next = size; // store size memo
	arena->next += advance_size;
	return ret;
}

static void *adalloc_internal(Arena *arena, u64 size)
{
	tlsf_t tlsf = (tlsf_t) arena->dyn_data;
	i32 retries = 0;
	void *ret = tlsf_malloc(tlsf, size);
	u64 expand_size = 0;
	void *expand_pool = NULL;
	while (!ret && retries < MAX_RETRIES) {
		if (!expand_size)
			expand_size = MAX(arena->dynamic_commit_size, core_round_up_to_pow2(size));
		expand_pool = arena_expand(arena, true, expand_size);
		if (expand_pool) {
			ret = tlsf_malloc(tlsf, size);
			if (!ret) {
				// if the last expand succeeded but the malloc still failed, the expand wasn't big enough.
				// Unexpand and try again.
				arena_unexpand_dynamic(arena, expand_size, expand_pool);
				expand_size *= 2;
			}
		}
		retries++;
	}
	if (ret)
		memset(ret, 0, size);
	return ret;
}

void *aalloc(Arena *arena, u64 size)
{
	if (!size)
		return NULL;
	if (arena) {
		if (arena->start && (arena->default_alloc == AU_ALLOC_STATIC || !arena->dyn_data)) {
			return asalloc_internal(arena, size);
		} else if (arena->dyn_data) {
			return adalloc_internal(arena, size);
		} else {
			return NULL;
		}
	} else {
		void *ret = calloc(size, 1);
		errexit_unless(ret, "Arena: calloc fallback failed.\n");
		return ret;
	}
}

void *aalloc_debug_mem(Arena *arena, u64 size, char *file, i32 line)
{
	void *res = aalloc(arena, size);
	log_allocation(res, size, file, line);
	return res;
}

void *asalloc(Arena *arena, u64 size)
{
	if (!size)
		return NULL;
	if (arena) {
		if (arena->start) {
			return asalloc_internal(arena, size);
		} else if (arena->dyn_data) {
			return adalloc_internal(arena, size);
		} else {
			return NULL;
		}
	} else {
		void *ret = calloc(size, 1);
		errexit_unless(ret, "Arena: calloc fallback failed.\n");
		return ret;
	}
}

void *asalloc_debug_mem(Arena *arena, u64 size, char *file, i32 line)
{
	void *ret = asalloc(arena, size);
	log_allocation(ret, size, file, line);
	return ret;
}

void *adalloc(Arena *arena, u64 size)
{
	if (!size)
		return NULL;
	if (arena) {
		if (arena->dyn_data) {
			return adalloc_internal(arena, size);
		} else if (arena->start) {
			return asalloc_internal(arena, size);
		} else {
			return NULL;
		}
	} else {
		void *ret = calloc(size, 1);
		errexit_unless(ret, "Arena: calloc fallback failed.\n");
		return ret;
	}
}

void *adalloc_debug_mem(Arena *arena, u64 size, char *file, i32 line)
{
	void *ret = adalloc(arena, size);
	log_allocation(ret, size, file, line);
	return ret;
}

void afree(Arena *arena, const void *ptr)
{
	// xx assumes contiguous
	if ((u8 *) ptr >= arena->dyn_data && (u8 *) ptr < arena->dyn_end) {
		tlsf_t tlsf = (tlsf_t) arena->dyn_data;
		tlsf_free(tlsf, (void *) ptr);
	} else {
		errexit_unless(!ptr || ((u8 *) ptr >= arena->start && (u8 *) ptr < arena->end),
			"Error: attempted to afree a pointer not in arena.\n");
		// free on static does nothing
	}
}

void afree_debug_mem(Arena *arena, const void *ptr, char *file, i32 line)
{
	afree(arena, ptr);
	log_free(ptr);
}

void *arealloc(Arena *arena, const void *ptr, u64 size)
{
	if ((u8 *) ptr >= arena->dyn_data && (u8 *) ptr < arena->dyn_end) {
		tlsf_t tlsf = (tlsf_t) arena->dyn_data;
		i32 retries = 0;
		void *ret = tlsf_realloc(tlsf, (void *) ptr, size);
		u64 expand_size = 0;
		void *expand_pool = NULL;
		while (!ret && retries < MAX_RETRIES) {
			if (!expand_size)
				expand_size = MAX(arena->dynamic_commit_size, core_round_up_to_pow2(size));
			expand_pool = arena_expand(arena, true, expand_size);
			if (expand_pool) {
				ret = tlsf_realloc(tlsf, (void *) ptr, size);
				if (!ret) {
					// if the last expand succeeded but the realloc still failed, the expand wasn't big enough.
					// Unexpand and try again.
					arena_unexpand_dynamic(arena, expand_size, expand_pool);
					expand_size *= 2;
				}
			}
			retries++;
		}
		return ret;
	} else if ((u8 *) ptr >= arena->start + sizeof(u64) && (u8 *) ptr < arena->end) {
		u64 old_size = * (u64 *) ((u8 *) ptr - sizeof(u64));
		// asalloc_internal will do the expansion for us if necessary.
		void *ret = asalloc_internal(arena, size);
		memcpy(ret, ptr, old_size);
		return ret;
	} else {
		return NULL;
	}
}

void *arealloc_debug_mem(Arena *arena, const void *ptr, u64 size, char *file, i32 line)
{
	void *ret = arealloc(arena, ptr, size);
	log_free(ptr);
	log_allocation(ret, size, file, line);
	return ret;
}

void arena_print_usage(Arena *arena)
{
}

// Refs:
// [1] https://github.com/aerospike/jemArena/blob/master/include/msvc_compat/strings.h
