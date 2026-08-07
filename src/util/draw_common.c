#include "au_core.h"
#include "au_math.h"
#include "au_window.h"
#include "au_draw.h"
#include "au_string.h"

/************************************** Color blending *******************************************/

static inline i32 au__div255(i32 x)
{
	return ((x+1) * 257) >> 16;
}

static inline u32 au__unpremul_u8(u32 cp, u32 a) {
    if (a == 0) return 0;
    i32 v = (cp * 255u + (a >> 1)) / a;
    if (v > 255u) v = 255u;
    return v;
}

// c1: destination. c2: source
u32 au_alpha_blend(u32 c1, u32 c2)
{
	u32 c2a = c2 >> 24;
	if (c2a == 255) {
		return c2;
	} else if (c2a == 0) {
		return c1;
	} else {
		u32 c1a = c1 >> 24;
		u32 c1r = (c1 >> 16) & 0xff;
		u32 c1g = (c1 >> 8) & 0xff;
		u32 c1b = c1 & 0xff;
		// premultiply
		c1r = au__div255(c1r * c1a);
		c1g = au__div255(c1g * c1a);
		c1b = au__div255(c1b * c1a);

		u32 c2r = (c2 >> 16) & 0xff;
		u32 c2g = (c2 >> 8) & 0xff;
		u32 c2b = c2 & 0xff;
		// premultiply
		c2r = au__div255(c2r * c2a);
		c2g = au__div255(c2g * c2a);
		c2b = au__div255(c2b * c2a);

		u32 ia = 255 - c2a;
		u32 r = c2r + au__div255(c1r * ia);
		u32 g = c2g + au__div255(c1g * ia);
		u32 b = c2b + au__div255(c1b * ia);
		u32 a = c2a + au__div255(c1a * ia);

		r = au__unpremul_u8(r, a);
		g = au__unpremul_u8(g, a);
		b = au__unpremul_u8(b, a);
		return (a << 24) | (r << 16) | (g << 8) | b;
	}
}

// c1: destination. c2: source
u32 au_alpha_blend_fade_in(u32 c1, u32 c2, float fade_in)
{
	i32 c2a = (c2 >> 24) * CLAMP(fade_in, 0.0f, 1.0f);
	if (c2a == 255) {
		return c2;
	} else if (c2a == 0) {
		return c1;
	} else {
		i32 c1a = (c1 >> 24);
		i32 c1r = (c1 >> 16) & 0xff;
		i32 c1g = (c1 >> 8) & 0xff;
		i32 c1b = c1 & 0xff;
		// premultiply
		c1r = au__div255(c1r * c1a);
		c1g = au__div255(c1g * c1a);
		c1b = au__div255(c1b * c1a);

		i32 c2r = (c2 >> 16) & 0xff;
		i32 c2g = (c2 >> 8) & 0xff;
		i32 c2b = c2 & 0xff;
		// premultiply
		c2r = au__div255(c2r * c2a);
		c2g = au__div255(c2g * c2a);
		c2b = au__div255(c2b * c2a);

		i32 ia = 255 - c2a;
		u32 r = c2r + au__div255(c1r * ia);
		u32 g = c2g + au__div255(c1g * ia);
		u32 b = c2b + au__div255(c1b * ia);
		u32 a = c2a + au__div255(c1a * ia);

		r = au__unpremul_u8(r, a);
		g = au__unpremul_u8(g, a);
		b = au__unpremul_u8(b, a);
		return (a << 24) | (r << 16) | (g << 8) | b;
	}
}

// c1: destination. c2: source
u32 au_alpha_blend_u8_fade_in(u32 c1, u32 c2, u8 fade_in)
{
	i32 c2a = au__div255((c2 >> 24) * fade_in);
	if (c2a == 255) {
		return c2;
	} else if (c2a == 0) {
		return c1;
	} else {
		i32 c1a = (c1 >> 24);
		i32 c1r = (c1 >> 16) & 0xff;
		i32 c1g = (c1 >> 8) & 0xff;
		i32 c1b = c1 & 0xff;
		// premultiply
		c1r = au__div255(c1r * c1a);
		c1g = au__div255(c1g * c1a);
		c1b = au__div255(c1b * c1a);

		i32 c2r = (c2 >> 16) & 0xff;
		i32 c2g = (c2 >> 8) & 0xff;
		i32 c2b = c2 & 0xff;
		// premultiply
		c2r = au__div255(c2r * c2a);
		c2g = au__div255(c2g * c2a);
		c2b = au__div255(c2b * c2a);

		i32 ia = 255 - c2a;
		u32 r = c2r + au__div255(c1r * ia);
		u32 g = c2g + au__div255(c1g * ia);
		u32 b = c2b + au__div255(c1b * ia);
		u32 a = c2a + au__div255(c1a * ia);

		r = au__unpremul_u8(r, a);
		g = au__unpremul_u8(g, a);
		b = au__unpremul_u8(b, a);
		return (a << 24) | (r << 16) | (g << 8) | b;
	}
}

u32 au_blend_colors(u32 c1, u32 c2, float p)
{
	p = CLAMP(p, 0.0f, 1.0f);
	u8 c1a = c1 >> 24;
	u8 c1r = (c1 >> 16) & 0xff;
	u8 c1g = (c1 >> 8) & 0xff;
	u8 c1b = c1 & 0xff;
	u8 c2a = c2 >> 24;
	u8 c2r = (c2 >> 16) & 0xff;
	u8 c2g = (c2 >> 8) & 0xff;
	u8 c2b = c2 & 0xff;
	u32 a = c1a * (1-p) + c2a*p + 0.5f;
	u32 r = c1r * (1-p) + c2r*p + 0.5f;
	u32 g = c1g * (1-p) + c2g*p + 0.5f;
	u32 b = c1b * (1-p) + c2b*p + 0.5f;
	return (a << 24) | (r << 16) | (g << 8) | b;
}

/************************************** Scene setup *******************************************/

Scene *scene_create_child(Scene *parent, Arena *arena, float x, float y, float w, float h)
{
	Scene *scene = aalloc(arena, sizeof(Scene));
	*scene = (Scene) { AU_SCENE_TYPE_SCENE, w, h, parent->x + x, parent->y + y,
		parent->context, parent, parent->window };
	return scene;
}

Scene scene_make_child(Scene *parent, float x, float y, float w, float h)
{
	Scene ret = { AU_SCENE_TYPE_SCENE, w, h, parent->x + x, parent->y + y,
		parent->context, parent, parent->window };
	return ret;
}

Vector2 scene_coords_to_window(Scene *scene, float x, float y)
{
	// This could become a real 3d transformation later
	return (Vector2) { scene->x + x, scene->y + y };
}

Vector4 scene_get_window_bounds(Scene *scene)
{
	return (Vector4) { scene->x, scene->y, scene->x + scene->w, scene->y + scene->h };
}

Vector2 window_coords_to_scene(Scene *scene, float x, float y)
{
	return (Vector2) { x - scene->x, y - scene->y };
}

void add_textf(Scene *scene, i32 font_i, i32 size, float x, float y, Vector4 color,
	const char *fmt, ...)
{
	Arena *temp_arena = temp_arena_create();
	va_list args;
	va_start(args, fmt);
	String text = string_formatv(temp_arena, fmt, args);
	va_end(args);
	add_text(scene, font_i, size, text.d, x, y, color);
	temp_arena_delete(temp_arena);
}

float measure_text_widthf(Scene *scene, i32 font_i, i32 size, const char *fmt, ...)
{
	Arena *temp_arena = temp_arena_create();
	va_list args;
	va_start(args, fmt);
	String text = string_formatv(temp_arena, fmt, args);
	va_end(args);
	float ret = measure_text_width(scene, font_i, size, text.d);
	temp_arena_delete(temp_arena);
	return ret;
}

void add_textf_in_box(Scene *scene, i32 font_i, i32 size, float x, float y, Vector4 color,
	float x1, float y1, float x2, float y2, const char *fmt, ...)
{
	Arena *temp_arena = temp_arena_create();
	va_list args;
	va_start(args, fmt);
	String text = string_formatv(temp_arena, fmt, args);
	va_end(args);
	add_text_in_box(scene, font_i, size, text.d, x, y, color, x1, y1, x2, y2);
	temp_arena_delete(temp_arena);
}

void add_line_in_box(Scene *scene, float start_x, float start_y, float end_x, float end_y,
    Vector4 color, float x1, float y1, float x2, float y2)
{
	Scene child = scene_make_child(scene, x1, y1, x2 - x1, y2 - y1);
	add_line(&child, start_x - x1, start_y - y1, end_x - x1, end_y - y1, color);
}

void add_rectangle_in_box(Scene *scene, float x, float y, float w, float h, Vector4 color, float x1,
    float y1, float x2, float y2)
{
	Scene child = scene_make_child(scene, x1, y1, x2 - x1, y2 - y1);
	add_rectangle(&child, x - x1, y - y1, w, h, color);
}

void add_rectangle_outline_in_box(Scene *scene, float x, float y, float w, float h, Vector4 color,
    float x1, float y1, float x2, float y2)
{
	Scene child = scene_make_child(scene, x1, y1, x2 - x1, y2 - y1);
	add_rectangle_outline(&child, x - x1, y - y1, w, h, color);
}

void add_circle_in_box(Scene *scene, float x, float y, float r, Vector4 color, float x1,
    float y1, float x2, float y2)
{
	Scene child = scene_make_child(scene, x1, y1, x2 - x1, y2 - y1);
	add_circle(&child, x - x1, y - y1, r, color);
}

void add_circle_outline_in_box(Scene *scene, float x, float y, float r, Vector4 color, float x1,
    float y1, float x2, float y2)
{
	Scene child = scene_make_child(scene, x1, y1, x2 - x1, y2 - y1);
	add_circle_outline(&child, x - x1, y - y1, r, color);
}

void add_character_in_box(Scene *scene, i32 font_i, i32 size, u32 c, float x, float y, Vector4 color,
    float *advance_x, float x1, float y1, float x2, float y2)
{
	Scene child = scene_make_child(scene, x1, y1, x2 - x1, y2 - y1);
	add_character(&child, font_i, size, c, x - x1, y - y1, color, advance_x);
}

void add_text_in_box(Scene *scene, i32 font_i, i32 size, const char *text, float x, float y,
    Vector4 color, float x1, float y1, float x2, float y2)
{
	Scene child = scene_make_child(scene, x1, y1, x2 - x1, y2 - y1);
	add_text(&child, font_i, size, text, x - x1, y - y1, color);
}

void add_text_utf32_in_box(Scene *scene, i32 font_i, i32 size, const u32 *text, float x, float y,
    Vector4 color, float x1, float y1, float x2, float y2)
{
	Scene child = scene_make_child(scene, x1, y1, x2 - x1, y2 - y1);
	add_text_utf32(&child, font_i, size, text, x - x1, y - y1, color);
}

void add_image_in_box(Scene *scene, i32 image_i, float x, float y, float x1, float y1, float x2,
    float y2)
{
	Scene child = scene_make_child(scene, x1, y1, x2 - x1, y2 - y1);
	add_image(&child, image_i, x - x1, y - y1);
}

void add_image_with_alpha_in_box(Scene *scene, i32 image_i, float x, float y, float alpha,
    float x1, float y1, float x2, float y2)
{
	Scene child = scene_make_child(scene, x1, y1, x2 - x1, y2 - y1);
	add_image_with_alpha(&child, image_i, x - x1, y - y1, alpha);
}

