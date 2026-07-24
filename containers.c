#include <string.h>

#include "au_core.h"
#include "au_math.h"
#include "au_containers.h"
#include <stdio.h>

Dynarray *dynarray_from_data(Arena *arena, void *copy_items, u64 item_size,
	u64 length)
{
	assertf(item_size, "dynarray: item_size cannot be zero.\n");
	assertf(length < 1ULL << (sizeof(u64)*8-1), "dynarray: integer overflow\n");
	Dynarray *res = (Dynarray *) aalloc(arena, sizeof(Dynarray));
	res->capacity = 1ULL << (au_fls(length) + 1);
	u64 capacity_bytes = res->capacity * item_size;
	assertf(item_size == capacity_bytes / res->capacity, "dynarray: integer overflow\n");
	res->d = adalloc(arena, capacity_bytes);
	res->item_size = item_size;
	res->length = length;
	res->arena = arena;
	if (copy_items)
		memmove(res->d, copy_items, length*item_size);
	else
		memset(res->d, 0, length*item_size);
	return res;
}

struct dynarray *dynarray_create(Arena *arena, u64 item_size)
{
	return dynarray_from_data(arena, NULL, item_size, 0);
}

static void dynarray_up_size(struct dynarray *arr)
{
	assertf(arr->capacity * arr->item_size < UINT64_MAX / 2, "dynarray: integer overflow\n");
	arr->capacity *= 2;
	arr->d = arealloc(arr->arena, arr->d, arr->capacity*arr->item_size);
}

void *dynarray_get(void *arrp, u64 i) {
	struct dynarray *arr = (struct dynarray *) arrp;
	assertf(i < arr->length, "dynarray_get: out of bounds\n");
	return (void *) (arr->d + i*arr->item_size);
}

void dynarray_add(void *arrp, void *item)
{
	struct dynarray *arr = (struct dynarray *) arrp;
	while ((arr->length+1) > arr->capacity) {
		dynarray_up_size(arr);
	}
	memmove(arr->d + arr->length*arr->item_size, item, arr->item_size);
	arr->length++;
}

void dynarray_insert(void *arrp, void *item, u64 i)
{
	struct dynarray *arr = (struct dynarray *) arrp;
	assertf(i < arr->length+1, "Error: tried to insert at index %llu in a dynarray of length %llu\n", i,
		arr->length);
	if (i == arr->length)
		dynarray_add(arrp, item);
	else {
		dynarray_expand_to(arr, arr->length + 1);
		memmove(arr->d + (i+1)*arr->item_size, arr->d + i*arr->item_size,
			(arr->length-i)*arr->item_size);
		memmove(arr->d + i*arr->item_size, item, arr->item_size);
		arr->length++;
	}
}

void dynarray_remove(void *arrp, u64 i)
{
	struct dynarray *arr = (struct dynarray *) arrp;
	assertf(i < arr->length, "Error: tried to remove item %llu from a dynarray of length %llu\n", i,
		arr->length);
	if (i < arr->length - 1)
		memmove(arr->d + i*arr->item_size, arr->d + (i+1)*arr->item_size,
			(arr->length - (i+1))*arr->item_size);
	arr->length--;
}


void dynarray_copy(void *dest_arrp, void *src_arrp)
{
	struct dynarray *dest_arr = (struct dynarray *) dest_arrp;
	struct dynarray *src_arr = (struct dynarray *) src_arrp;
	if (dest_arr == src_arr)
		return;
	assertf(dest_arr->item_size == src_arr->item_size, "dynarray_copy: item_size mismatch\n");
	dynarray_expand_to(dest_arr, src_arr->length);
	memmove(dest_arr->d, src_arr->d, src_arr->length * src_arr->item_size);
	dest_arr->length = src_arr->length;
}

void dynarray_expand_to(void *dynarray, u64 new_capacity)
{
	Dynarray *arr = (Dynarray *) dynarray;
	while (new_capacity > arr->capacity) {
		dynarray_up_size(arr);
	}
}

void dynarray_expand_by(void *dynarray, u64 added_capacity)
{
	Dynarray *arr = (Dynarray *) dynarray;
	assertf(arr->length <= UINT64_MAX - added_capacity, "dynarray_expand_by: integer overflow\n");
	u64 new_capacity = arr->length + added_capacity;
	while (new_capacity > arr->capacity) {
		dynarray_up_size(arr);
	}
}

/************************************** Hash Table ************************************************/

u64 fnv1a_64(const void *data, u64 len)
{
	u8 *p = (u8 *) data;
	u64 prime = 0x00000100000001b3;
	u64 hash = 0xcbf29ce484222325;
	for (size_t i=0; i<len; i++) {
		hash ^= p[i];
		hash *= prime;
	}
	return hash;
}

bool hash_table_init(Hash_Table *table, Arena *arena, u64 capacity, bool copy_keys, bool copy_values)
{
	assertf(capacity && sizeof(Hash_Table) < UINT64_MAX / capacity, "hash table: invalid capacity.\n");
	table->d = (Hash_Entry *) adalloc(arena, capacity*sizeof(Hash_Entry));
	if (!table->d)
		return false;
	table->capacity = capacity;
	table->n_entries = 0;
	table->expand_threshold = 0.75f;
	table->copy_keys = copy_keys;
	table->copy_values = copy_values;
	table->arena = arena;
	return true;
}

Hash_Table *hash_table_create(Arena *arena, u64 capacity, bool copy_keys, bool copy_values)
{
	Hash_Table *res = aalloc(arena, sizeof(Hash_Table));
	if (hash_table_init(res, arena, capacity, copy_keys, copy_values)) {
		return res;
	} else {
		afree(arena, res);
		return NULL;
	}
}

void hash_table_deinit(Hash_Table *table)
{
	Arena *arena = table->arena;
	Hash_Entry *cur_entry = NULL;
	cur_entry = hash_table_next_entry(table, cur_entry);
	while (cur_entry) {
		if (table->copy_keys)
			afree(arena, cur_entry->key);
		if(table->copy_values)
			afree(arena, cur_entry->value);
		cur_entry = hash_table_next_entry(table, cur_entry);
	}
	afree(arena, table->d);
}

void hash_table_delete(Hash_Table *table)
{
	Arena *arena = table->arena;
	hash_table_deinit(table);
	afree(arena, table);
}

bool keys_equal(const void *key1, u64 key1_length, const void *key2, u64 key2_length)
{
	return key1_length == key2_length && !memcmp(key1, key2, key1_length);
}

// Inner conflict-resolution loop for set and get
Hash_Entry *entry_for_hash(Hash_Table *table, u64 hash, const void *key, u64 key_len)
{
	u64 n = table->capacity;
	u64 h_i_start = hash % n;
	u64 h_i = h_i_start;
	Hash_Entry *e = &table->d[h_i];
	while (e->key && !keys_equal(key, key_len, e->key, e->key_len)) {
		h_i = (h_i + 1) % n;
		if (h_i == h_i_start) {
			return NULL;
		}
		e = &table->d[h_i];
	}
	return e;
}

void expand_hash_table(Hash_Table *table)
{
	u64 old_capacity = table->capacity;
	table->capacity *= 2;
	assertf(sizeof(Hash_Entry) < UINT64_MAX / table->capacity, "hash table: integer overflow.\n");
	u64 n = table->capacity;
	Hash_Entry *new_d = adalloc(table->arena, n*sizeof(Hash_Entry));
	memset(new_d, 0, n * sizeof(Hash_Entry));
	for (u64 i=0; i<old_capacity; i++) {
		Hash_Entry *old_e = &table->d[i];
		if (old_e->alive) {
			u64 h = old_e->hash;
			u64 h_i_start = h % n;
			u64 h_i = h_i_start;
			Hash_Entry *e = &new_d[h_i];
			// no equality check; we are definitely inserting for the first time
			while(e->key) {
				h_i = (h_i + 1) % n;
				// Also no wrap around check: It should be impossible to wrap back to where we
				// started while expanding.
				e = &new_d[h_i];
			}
			memcpy(e, old_e, sizeof(Hash_Entry));
		}
	}
	afree(table->arena, table->d);
	table->d = new_d;
}

bool hash_table_set(Hash_Table *table, const void *key, u64 key_len, const void *value, u64 value_len)
{
	if (!key)
		return false;

	if ((float) table->n_entries + 1.0f > (float) table->capacity * table->expand_threshold)
		expand_hash_table(table);

	u64 h = fnv1a_64(key, key_len);
	Hash_Entry *e = entry_for_hash(table, h, key, key_len);
	if (!e) { // no slots available
		return false;
	} else if (!e->key) { // empty slot
		if (table->copy_keys) {
			e->key = adalloc(table->arena, key_len);
			memcpy(e->key, key, key_len);
		} else {
			e->key = (void *) key;
		}
		e->hash = h;
		e->key_len = key_len;
		if (table->copy_values) {
			e->value = adalloc(table->arena, value_len);
			memcpy(e->value, value, value_len);
		} else {
			e->value = (void *) value;
		}
		e->alive = true;
		table->n_entries += 1;
		return true;
	} else { // either dead or already in hash table
		if (table->copy_values) {
			afree(table->arena, e->value);
			e->value = adalloc(table->arena, value_len);
			memcpy(e->value, value, value_len);
		} else {
			e->value = (void *) value;
		}
		e->alive = true;
		return true;
	}
}

void *hash_table_get(Hash_Table *table, const void *key, u64 key_len, u64 *value_len)
{
	if (!key) {
		return NULL;
	}
	u64 h = fnv1a_64(key, key_len);
	Hash_Entry *e = entry_for_hash(table, h, key, key_len);
	if (!e || !e->key || !e->alive) {
		return NULL;
	}
	if (value_len)
		*value_len = e->value_len;
	return e->value;
}

bool hash_table_get2(Hash_Table *table, const void *key, u64 key_len, void **value, u64 *value_len)
{
	if (!key) {
		return false;
	}
	u64 h = fnv1a_64(key, key_len);
	Hash_Entry *e = entry_for_hash(table, h, key, key_len);
	if (!e || !e->key || !e->alive) {
		return false;
	}
	*value = e->value;
	if (value_len)
		*value_len = e->value_len;
	return true;
}

bool hash_table_remove(Hash_Table *table, const void *key, u64 key_len)
{
	if (!key)
		return false;
	u64 h = fnv1a_64(key, key_len);
	u64 n = table->capacity;
	u64 h_i_start = h % n;
	u64 h_i = h_i_start;
	Hash_Entry *e = &table->d[h_i];
	while (e->key && !keys_equal(key, key_len, e->key, e->key_len)) {
		h_i = (h_i + 1) % n;
		if (h_i == h_i_start) {
			return false;
		}
		e = &table->d[h_i];
	}
	if (!e->key || !e->alive) {
  		return false;
	} else {
		table->n_entries--;
		// Deletion with linear probing, essentially Knuth algorithm 6.4R
		u64 next_h_i = h_i;
		while (true) {
			next_h_i = (next_h_i + 1) % n;
		 	Hash_Entry *next = &table->d[next_h_i];
		 	if (!next->key) {
		 		memset(e, 0, sizeof(Hash_Entry));
		 		return true;
		 	}
			u64 next_hash = next->hash % n;
			// Does h_i lie cyclically between next_hash and next_hash_i? If so, h_i is earlier on the
			// probe path for next_hash than next_h_i.
			if ((next_hash <= h_i && h_i < next_h_i)
				|| (next_h_i < next_hash && next_hash <= h_i)
				|| (h_i < next_h_i && next_h_i < next_hash)) {
				// shift next back
				memcpy(e, next, sizeof(Hash_Entry));
				e = next;
				h_i = next_h_i;
			}
		}
	}
}

Hash_Entry *hash_table_next_entry(Hash_Table *table, Hash_Entry *cur_entry)
{
	u64 n = table->capacity;
	Hash_Entry *e = cur_entry ? cur_entry : table->d;
	u64 h_i = (e - table->d);

	do {
		h_i = (h_i + 1) % n;
		if (h_i == 0) {
			return NULL;
		}
		e = &table->d[h_i];
	} while (!e->alive);
	return e;
}
