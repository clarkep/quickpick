#ifndef AU_MATH_H
#define AU_MATH_H

#include "au_core.h"

#ifdef __cplusplus
extern "C" {
#endif

/*************************************** Math *****************************************************/

typedef struct vector2 {
	float x;
	float y;
} Vector2;

typedef struct vector3 {
	float x;
	float y;
	float z;
} Vector3;

typedef struct vector4 {
	float x;
	float y;
	float z;
	float w;
} Vector4;

// Find first set bit: 0=least significant, 63=most significant, -1=none
i32 au_ffs(u64 x);
// Find last set bit: 0=least significant, 63=most significant, -1=none
i32 au_fls(u64 x);
u64 au_round_up_to_pow2(u64 x);

i32 au_positive_modulo(i32 x, i32 m);
i64 au_positive_modulo64(i64 x, i64 m);
Vector2 au_normalize_v2(Vector2 v);
Vector2 au_add_v2(Vector2 v, Vector2 w);
Vector2 au_mult_cv2(float c, Vector2 v);

#ifdef __cplusplus
}
#endif

#endif