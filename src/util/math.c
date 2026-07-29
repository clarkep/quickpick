#include <math.h>

#include "au_core.h"
#include "au_math.h"

#ifdef _MSC_VER

#pragma intrinsic(_BitScanForward64, _BitScanReverse64)

i32 au_ffs(u64 x)
{
	unsigned long i;
	if (_BitScanForward64(&i, x))
		return i;
	return -1;
}

i32 au_fls(u64 x)
{
	unsigned long i;
	if (_BitScanReverse64(&i, x))
		return i;
	return -1;
}

#elif defined(__GNUC__) || defined(__clang__)

i32 au_ffs(u64 x)
{
	if (x == 0) return -1;
	return (63 - __builtin_ctzll(x));
}

i32 au_fls(u64 x)
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

i32 au_ffs(u64 x)
{
	return (63 - _ctz64(x));
}

i32 au_fls(u64 x)
{
	return (63 - _clz64(x));
}

#endif

u64 au_round_up_to_pow2(u64 x)
{
	if (x <= 1)
		return 1;
	return (u64) 1 << (au_fls(x-1) + 1);
}

i32 au_positive_modulo(i32 x, i32 m)
{
	return ((x % m) + m) % m;
}

i64 au_positive_modulo64(i64 x, i64 m)
{
	return ((x % m) + m) % m;
}

Vector2 au_normalize_v2(Vector2 v)
{
	float d = sqrtf(v.x*v.x + v.y*v.y);
	return (Vector2) { v.x / d, v.y / d };
}

Vector2 au_add_v2(Vector2 v, Vector2 w)
{
	return (Vector2) { v.x + w.x, v.y + w.y };
}

Vector2 au_mult_cv2(float c, Vector2 v)
{
	return (Vector2) { c*v.x, c*v.y };
}

