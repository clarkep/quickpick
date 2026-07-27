#ifndef AUTIL_STRING_H
#define AUTIL_STRING_H

#include "au_core.h"

#ifdef __cplusplus
extern "C" {
#endif

/*************************************** Strings **************************************************/

typedef struct string {
	char *d;
	u64 length;
} String;

#define NULLSTRING ((String) { 0, 0 })

String string_copy_from(Arena *arena, char *from);

String string_ncopy_from(Arena *arena, char *from, u64 n);

String string_ncopy_from_string(Arena *arena, String from, u64 n);

String string_format(Arena *arena, const char *fmt, ...);

String string_formatv(Arena *arena, const char *fmt, va_list args);

String string_append(Arena *arena, String s1, String s2);

bool string_endswith(String s, String suffix);

bool string_startswith(String s, String prefix);

String string_array_join(Arena *arena, String *strings, u64 length, String delim);

u32 *decode_utf8(Arena *arena, const char *s, u64 *out_len);

// String String replace(Arena *area, String *s1, String *replace_this, String *with_this);

#ifdef __cplusplus
}
#endif

#endif