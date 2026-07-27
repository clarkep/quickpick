#ifndef AU_CONTAINERS_H
#define AU_CONTAINERS_H

#include "au_core.h"

#ifdef __cplusplus
extern "C" {
#endif

/************************************** Containers ************************************************/

/********* Dynarray ***********/
typedef struct dynarray {
	u8 *d;
	u64 length;
	u64 capacity;
	u64 item_size;
	/* impl */
	Arena *arena;
} Dynarray;

#define GEN_DYNARRAY_TYPE(name, item_type) struct name {\
	item_type *d;\
	u64 length;\
	u64 capacity;\
	u64 item_size;\
	Arena *arena;\
};

#define GEN_DYNARRAY_TYPEDEF(name, item_type) typedef struct {\
	item_type *d;\
	u64 length;\
	u64 capacity;\
	u64 item_size;\
	Arena *arena;\
} name;

/* NOTE: typeof is a C23 feature that requires gcc, clang, or cl with /std:clatest. */
#define dynarray_add_value(dynarray, item) {\
	typeof(item) x = item;\
	dynarray_add(dynarray, &x);\
}

struct dynarray *dynarray_from_data(Arena *arena, void *src, u64 item_size, u64 length);

struct dynarray *dynarray_create(Arena *arena, u64 item_size);

// arr must be a pointer to a struct dynarray or an equivalent created by GEN_DYNARRAY_TYPE, hence
// void * to avoid having to cast.
void *dynarray_get(void *arr, u64 i);

void dynarray_add(void *arr, void *item);

void dynarray_insert(void *arrp, void *item, u64 i);

void dynarray_remove(void *arr, u64 i);

void dynarray_copy(void *dest_arr, void *src_arr);

void dynarray_expand_to(void *dynarray, u64 new_capacity);

void dynarray_expand_by(void *dynarray, u64 added_capacity);

/******** Hash Table *********/

typedef struct hash_entry {
	u64 hash;
	void *key;
	u64 key_len;
	void *value;
	u64 value_len;
	bool alive;
} Hash_Entry;

typedef struct hash_table {
	Hash_Entry *d;
	u64 capacity;
	u64 n_entries;
	float expand_threshold;
	Arena *arena; // needed to make copies of the keys/values and to reallocate if expand_threshold is reached.
	bool copy_keys;
	bool copy_values;
} Hash_Table;

Hash_Table *hash_table_create(Arena *arena, u64 capacity, bool copy_keys, bool copy_values);

bool hash_table_init(Hash_Table *table, Arena *arena, u64 capacity, bool copy_keys, bool copy_values);

void hash_table_delete(Hash_Table *table);

void hash_table_deinit(Hash_Table *table);

// When table.copy_values is false, value_len is not used and can be 0.
// When copy_values is true, this function frees any already-present values for key.
bool hash_table_set(Hash_Table *table, const void *key, u64 key_len, const void *value, u64 value_len);

// value_len_out is ignored if it is NULL.
void *hash_table_get(Hash_Table *table, const void *key, u64 key_len, u64 *value_len_out);

// Allows detecting failure when values may be NULL
bool hash_table_get2(Hash_Table *table, const void *key, u64 key_len, void **value_out, u64 *value_len_out);

bool hash_table_remove(Hash_Table *table, const void *key, u64 key_len);

// Iterate over hash entries in the table. To start, pass NULL for cur_entry, and stop when
// this function returns NULL.
Hash_Entry *hash_table_next_entry(Hash_Table *table, Hash_Entry *cur_entry);

#ifdef __cplusplus
}
#endif

#endif