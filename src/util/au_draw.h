#ifndef AU_DRAW_H
#define AU_DRAW_H

#include "au_core.h"
#include "au_math.h"
#include "au_window.h"

#ifdef __cplusplus
extern "C" {
#endif

/*************************************** Graphics *************************************************/

/********* Generic API *************/

// 0xRRGGBBAA -> { r, g, b, a}
Vector4 hex2vec(u32 hex);
u32 vec2hex(Vector4 vector);

// Color blending. c1: destination, c2: source. Colors are 0xAARRGGBB, unpremultiplied alpha.
u32 au_alpha_blend(u32 c1, u32 c2);
// fade_in multiplies source alpha.
u32 au_alpha_blend_fade_in(u32 c1, u32 c2, float fade_in);
u32 au_alpha_blend_u8_fade_in(u32 c1, u32 c2, u8 fade_in);

// Linear interpolation from c1 to c2.
u32 au_blend_colors(u32 c1, u32 c2, float p);

// Create a child scene of parent. To this is and all Scene functions, you can pass a
// Window * instead, cast to Scene *.
Scene *scene_create_child(Scene *parent, Arena *arena, float x, float y, float w, float h);

// Same as create_child_scene, but return the scene directly.
Scene scene_make_child(Scene *parent, float x, float y, float w, float h);

// Translate scene coords to window coords
Vector2 scene_coords_to_window(Scene *scene, float x, float y);

// The boundaries of the scene in window coordinates, returns { x=x1, y=y1, z=x2, w=y2 }
Vector4 scene_get_window_bounds(Scene *scene);

Vector2 window_coords_to_scene(Scene *scene, float x, float y);

i32 load_font(Scene *scene, const char *font_file);

i32 load_font_from_memory(Scene *scene, const void *font_data, u64 data_size);

// 'type' can be one of:
//     "png"
//     "jpg"
//     "svg", which rasterizes the svg
//     "svg_alpha", which rasterzies the svg, only keeping the alpha channel
//         -can be used with add_image_with_color
i32 load_image(Scene *scene, const char *path, const char *type);

i32 load_image_from_memory(Scene *scene, const void *data, u64 data_size, const char *type);

i32 load_image_at_size(Scene *scene, const char *path, const char *type, i32 w, i32 h);

i32 load_image_at_size_from_memory(Scene *scene, const void *data, u64 data_size, const char *type,
	i32 w, i32 h);

i32 load_bitmap(Scene *scene, const void *data, i32 width, i32 height, i32 bytes_per_row,
	i8 channels);

void unload_image(Scene *scene, i32 image_i);

void clear_background(Scene *scene, Vector4 color);

void add_line(Scene *scene, float x1, float y1, float x2, float y2, Vector4 color);

void add_rectangle(Scene *scene, float x, float y, float w, float h, Vector4 color);

void add_rectangle_outline(Scene *scene, float x, float y, float w, float h, Vector4 color);

void add_triangle(Scene *scene, float x1, float y1, float x2, float y2, float x3, float y3, Vector4 color);

void add_triangle_outline(Scene *scene, float x1, float y1, float x2, float y2, float x3, float y3,
	Vector4 color);

void add_circle(Scene *scene, float x, float y, float r, Vector4 color);

void add_circle_outline(Scene *scene, float x, float y, float r, Vector4 color);

void add_character(Scene *scene, i32 font_i, i32 size, u32 c, float x, float y, Vector4 color,
	float *advance_x);

// Only angle_turns of 0 and 0.25 supported on CPU. advance x still points along the baseline.
void add_character_rotated(Scene *scene, i32 font_i, i32 size, u32 c, float x, float y,
	Vector4 color, float angle_turns, float *advance_x);

void add_text(Scene *scene, i32 font_i, i32 size, const char *text, float x, float y,
	Vector4 color);

// Only angle_turns of 0 and 0.25 supported on CPU.
void add_text_rotated(Scene *scene, i32 font_i, i32 size, const char *text, float x, float y,
	float angle_turns, Vector4 color);

void add_textf(Scene *scene, i32 font_i, i32 size, float x, float y, Vector4 color,
	const char *fmt, ...);

void add_text_utf32(Scene *scene, i32 font_i, i32 size, const u32 *text, float x, float y,
	Vector4 color);

// Only angle_turns of 0 and 0.25 supported on CPU.
void add_text_rotated_utf32(Scene *scene, i32 font_i, i32 size, const u32 *text, float x, float y,
	float angle_turns, Vector4 color);

// Add the last text measured by measure_text_width*. Text is stored by window, not by scene.
void add_text_last_measured(Scene *scene, float x, float y, Vector4 color);

float measure_text_width(Scene *scene, i32 font_i, i32 size, const char *text);

float measure_text_widthf(Scene *scene, i32 font_i, i32 size, const char *fmt, ...);

float measure_text_width_utf32(Scene *scene, i32 font_i, i32 size, const u32 *text);

typedef struct font_metrics
{
	float ascent;
	float descent;
} Font_Metrics;

// Returns unscaled font metrics. Multiply by the font size you are using.
Font_Metrics au_font_get_metrics(Scene *scene, i32 font_i);

void add_image(Scene *scene, i32 image_i, float x, float y);

void add_image_with_alpha(Scene *scene, i32 image_i, float x, float y, float alpha);

// image must be single channel, loaded with load_bitmap or load_image and type "svg_alpha"
void add_image_with_color(Scene *scene, i32 image_i, float x, float y, Vector4 color);

/********* "in_box" ***********/

// These convenience functions create a temporary scene with corners (x1, y1) and (x2, y2) and call
// the functions above. All coordinates are relative to the passed in scene.

void add_line_in_box(Scene *scene, float start_x, float start_y, float end_x, float end_y,
	Vector4 color, float x1, float y1, float x2, float y2);

void add_rectangle_in_box(Scene *scene, float x, float y, float w, float h, Vector4 color, float x1,
	float y1, float x2, float y2);

void add_rectangle_outline_in_box(Scene *scene, float x, float y, float w, float h, Vector4 color,
	float x1, float y1, float x2, float y2);

void add_triangle_in_box(Scene *scene, float x1, float y1, float x2, float y2, float x3, float y3,
	Vector4 color, float box_x1, float box_y1, float box_x2, float box_y2);

void add_triangle_outline_in_box(Scene *scene, float x1, float y1, float x2, float y2, float x3,
	float y3, Vector4 color);

void add_circle_in_box(Scene *scene, float x, float y, float r, Vector4 color, float x1,
	float y1, float x2, float y2);

void add_circle_outline_in_box(Scene *scene, float x, float y, float r, Vector4 color, float x1,
	float y1, float x2, float y2);

void add_character_in_box(Scene *scene, i32 font_i, i32 size, u32 c, float x, float y, Vector4 color,
	float *advance_x, float x1, float y1, float x2, float y2);

void add_text_in_box(Scene *scene, i32 font_i, i32 size, const char *text, float x, float y,
	Vector4 color, float x1, float y1, float x2, float y2);

void add_textf_in_box(Scene *scene, i32 font_i, i32 size, float x, float y, Vector4 color,
	float x1, float y1, float x2, float y2, const char *fmt, ...);

void add_text_utf32_in_box(Scene *scene, i32 font_i, i32 size, const u32 *text, float x, float y,
	Vector4 color, float x1, float y1, float x2, float y2);

void add_image_in_box(Scene *scene, i32 image_i, float x, float y, float x1, float y1, float x2,
	float y2);

void add_image_with_alpha_in_box(Scene *scene, i32 image_i, float x, float y, float alpha,
	float x1, float y1, float x2, float y2);

/********* OpenGL only **************/

typedef struct render_context Render_Context;

typedef struct float_dynarray {
	float *d;
	u64 length;
	u64 capacity;
	u64 item_size;
	Arena *arena;
} Float_Dynarray;

// A render context is a vertex buffer (i.e. a list of shapes) with an associated set of shaders, fonts
// and bitmaps (it is not the same as OpenGL's concept of a context). Each scene is associated with a
// context; when you add to one, it becomes active and the old active context is rendered. This
// allows interleaving scenes that use different shaders.
//
// load_render_context creates a new render context associated with the given vertex_shader and
// fragment_shader, and returns its id. If either vertex_shader or fragment_shader is NULL, the
// corresponding default shader is used.
//
// If the render_context cannot be created, a negative value is returned and error messages are printed
// to stderr. Todo: return error messages instead of printing.
i32 window_create_render_context(Window *window, const char *vertex_shader, const char *fragment_shader,
	i32 vertex_size, bool use_screen_coords);

// Returns NULL if context is invalid
Scene *scene_create_child_with_context(Scene *parent, Arena *arena, float x, float y, float w,
 float h, i32 context);

// Returns false if context is invalid
bool scene_init_child_with_context(Scene *child_out, Scene *parent, float x, float y, float w,
	float h, i32 context);

void add_circle_arc(Scene *scene, float x, float y, float r, float angle1, float angle2,
    Vector4 color);

void add_rounded_rectangle(Scene *scene, float x, float y, float w, float h, float radius,
	Vector4 color);

void add_rounded_rectangle_outline(Scene *scene, float x, float y, float w, float h, float radius,
	Vector4 color);

void add_rounded_quad(Scene *scene, Vector2 *corners, bool *rounded, float radius, Vector4 color);

void add_rounded_quad_outline(Scene *scene, Vector2 *corners, bool *rounded, float radius,
	Vector4 color);

void add_line_ex(Scene *scene, float x1, float y1, float x2, float y2, float thickness,
	Vector4 color);

void add_rectangle_outline_ex(Scene *scene, float x, float y, float w, float h, float thickness,
	Vector4 color);

void add_circle_ex(Scene *scene, float x, float y, float r, float segments, Vector4 color);

void add_circle_outline_ex(Scene *scene, float x, float y, float r, float segments, float thickness,
	Vector4 color);

void add_circle_arc_ex(Scene *scene, float x, float y, float r, float angle1, float angle2,
    i32 segments, float thickness, Vector4 color);

void add_rounded_rectangle_ex(Scene *scene, float x, float y, float w, float h, float radius,
	i32 segments_per_corner, Vector4 color);

void add_rounded_quad_ex(Scene *scene, Vector2 *corners, bool *rounded, float radius,
	i32 segments_per_corner, Vector4 color);

void add_rounded_quad_outline_ex(Scene *scene, Vector2 *corners, bool *rounded, float radius,
	i32 segments_per_corner, float thickness, Vector4 color);

// The functions below can be used to write your own shape generators. See test_multi_context.c

Render_Context *scene_get_and_activate_context(Scene *scene);

void scene_transform_xy(Scene *scene, Render_Context *context, float *x, float *y);

void context_transform_wh(Render_Context *context, float *w, float *h);

void context_transform_1(Render_Context *context, float *q);

Float_Dynarray *context_get_vertices(Render_Context *context);

u32 context_get_vertex_size(Render_Context *context);

#ifdef __cplusplus
}
#endif

#endif
