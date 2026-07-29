#include <string.h>
#include <stdio.h>

#include "au_core.h"
#include "au_string.h"

String string_from(char *from)
{
	String res = { from, strlen(from) };
	return res;
}

String string_copy_from(Arena *arena, char *from)
{
	String res;
	res.length = strlen(from);
	res.d = aalloc(arena, res.length+1);
	strcpy(res.d, from);
	return res;
}

String string_ncopy_from(Arena *arena, char *from, u64 n)
{
	String res;
	res.d = (char *) aalloc(arena, n+1);
	memset(res.d, 0, n+1);
	res.length = strnlen(from, n);
	strncpy(res.d, from, n);
	return res;
}

String string_ncopy_from_string(Arena *arena, String from, u64 n)
{
	String res;
	res.d = (char *) aalloc(arena, n+1);
	memset(res.d, 0, n+1);
	res.length = from.length;
	memcpy(res.d, from.d, from.length);
	return res;
}

String string_format(Arena *arena, const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	String ret = string_formatv(arena, fmt, args);
	va_end(args);
	return ret;
}

String string_formatv(Arena *arena, const char *fmt, va_list args)
{
	u64 capacity = 16;
	// Todo: use aalloc once static allocator gets realloc optimization
	char *result_d = (char *) adalloc(arena, capacity);
	i32 n;
	while (1) {
		va_list args2;
		va_copy(args2, args);
		n = vsnprintf(result_d, capacity, fmt, args2);
		if (n < 0) {
			afree(arena, result_d);
			return NULLSTRING;
		} else if (n < capacity) {
			break;
		} else {
			capacity *= 2;
			arealloc(arena, result_d, capacity);
		}
	}
	arealloc(arena, result_d, n+1);
	result_d[n] = '\0';
	return (String) { result_d, n };
}

String string_append(Arena *arena, String s1, String s2)
{
	String res;
	res.length = s1.length + s2.length;
	res.d = (char *) aalloc(arena, res.length+1);
	memcpy(res.d, s1.d, s1.length);
	memcpy(res.d + s1.length, s2.d, s2.length);
	res.d[res.length] = '\0';
	return res;
}

bool string_endswith(String s, String suffix)
{
	if (suffix.length > s.length) {
		return false;
	}
	u64 suf_i = 0;
	for(u64 i = s.length - suffix.length; i < s.length; i++) {
		if (s.d[i] != suffix.d[suf_i]) {
			return false;
		}
		suf_i++;
	}
	return true;
}

bool string_startswith(String s, String prefix)
{
	if (prefix.length > s.length)
		return false;
	u64 pref_i = 0;
	for (u64 i = 0; i<prefix.length; i++) {
		if (s.d[i] != prefix.d[i]) {
			return false;
		}
	}
	return true;
}

String string_array_join(Arena *arena, String *strings, u64 length, String delim)
{
	String res;
	res.length = 0;
	for (u64 i=0; i<length; i++) {
		if (strings[i].d) {
			res.length += strings[i].length;
			if (i != length - 1) res.length += delim.length;
		}
	}
	res.d = aalloc(arena, res.length + 1);
	u64 res_i = 0;
	for (u64 i=0; i<length; i++) {
		String s = strings[i];
		if (s.d) {
			memcpy(res.d+res_i, s.d, s.length);
			res_i += s.length;
			if (i != length - 1) {
				memcpy(res.d + res_i, delim.d, delim.length);
				res_i += delim.length;
			}
		}
	}
	res.d[res.length] = '\0';
	return res;
}

/************************************** UTF-8 decoding ********************************************/

// Copyright (c) 2008-2010 Bjoern Hoehrmann <bjoern@hoehrmann.de>
// See http://bjoern.hoehrmann.de/utf-8/decoder/dfa/ for details.

#define UTIL_DECODE_MAX_STRING 500000
#define UTIL_UTF8_ACCEPT 0
#define UTIL_UTF8_REJECT 12

static const uint8_t utf8d[] = {
  // The first part of the table maps bytes to character classes that
  // to reduce the size of the transition table and create bitmasks.
   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,  9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,
   7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,  7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
   8,8,2,2,2,2,2,2,2,2,2,2,2,2,2,2,  2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
  10,3,3,3,3,3,3,3,3,3,3,3,3,4,3,3, 11,6,6,6,5,8,8,8,8,8,8,8,8,8,8,8,

  // The second part is a transition table that maps a combination
  // of a state of the automaton and a character class to a state.
   0,12,24,36,60,96,84,12,12,12,48,72, 12,12,12,12,12,12,12,12,12,12,12,12,
  12, 0,12,12,12,12,12, 0,12, 0,12,12, 12,24,12,12,12,12,12,24,12,24,12,12,
  12,12,12,12,12,12,12,24,12,12,12,12, 12,24,12,12,12,12,12,12,12,24,12,12,
  12,12,12,12,12,12,12,36,12,36,12,12, 12,36,12,12,12,12,12,36,12,36,12,12,
  12,36,12,12,12,12,12,12,12,12,12,12,
};

static inline uint32_t utf8_decoder(uint32_t* state, uint32_t* codep, uint32_t byte) {
  uint32_t type = utf8d[byte];

  *codep = (*state != UTIL_UTF8_ACCEPT) ?
    (byte & 0x3fu) | (*codep << 6) :
    (0xff >> type) & (byte);

  *state = utf8d[256 + *state + type];
  return *state;
}

u32 *decode_utf8(Arena *arena, const char *s, u64 *out_len)
{
	u64 len = strnlen(s, UTIL_DECODE_MAX_STRING);
	u32 *res = adalloc(arena, (len+1)*sizeof(u32));
	if (!res)
		return NULL;
	u64 out_i = 0;
	u32 state = UTIL_UTF8_ACCEPT, codep = 0;
	for (u64 char_i=0; char_i<len; char_i++) {
		utf8_decoder(&state, &codep, (u8) s[char_i]);
		if (state == UTIL_UTF8_ACCEPT)
			res[out_i++] = codep;
		else if (state == UTIL_UTF8_REJECT) {
			afree(arena, res);
			return NULL;
		}
	}
	if (state != UTIL_UTF8_ACCEPT) {
			afree(arena, res);
	    return NULL;
	}
	res[out_i] = 0;
	*out_len = out_i;
	return res;
}
/*
#define String_to_stackbuf(name, sz, src)
	char name[sz] = {0}; \
	strncpy(name, src.d, MIN(sz, src.length));
*/

