/*
quickpick: a color picker

By: Paul Clarke
License: MIT(see LICENSE)
*/

#ifdef _WIN32
#define _CRT_SECURE_NO_WARNINGS
#endif
#include <stdio.h>
#include <stdlib.h> // malloc, exit
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <assert.h>
#include <errno.h>
#include <math.h> // round
#include <stdbool.h>

#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <glad/glad.h>

#include "au_core.h"
#include "au_math.h"
#include "au_containers.h"
#include "au_window.h"
#include "au_window_sdl.h"
#include "au_platform.h"
#include "au_draw.h"
#include "au_string.h"

#include "font/noto_sans_mono.h"
#include "quickpick_icon.h"
// #include "menu_24dp_material.h"
// #include "settings_24dp_material.h"

#define WRITE_INTERVAL 0.5f
#define FONT_SMALL_PX 17*dpi
#define FONT_MEDIUM_PX 23*dpi
#define FONT_LARGE_PX 35*dpi
// For vertically centering text, fraction of the em box to place at the center of the region
#define CENTER_EM 0.35f

/**************/

typedef struct { float x, y, width, height; } Rectangle;

#define WHITE ((Vector4) { 1.0f, 1.0f, 1.0f, 1.0f })
#define BLACK ((Vector4){0.0f, 0.0f, 0.0f, 1.0f})

/*****************/

enum cursor_state { CURSOR_UP, CURSOR_START, CURSOR_DOWN, CURSOR_STOP };

typedef struct state {
	Arena *arena;
	Window *window;
	i32 hsv_grad_context;
	Scene *main_scene;
	Scene *hsv_grad_scene;
	int mode; // rgb(0) or hsv(1)
	int which_s; // red(0), green(1) or blue(2); or hue(0), saturation(1), or value(2)
	enum cursor_state cursor_state;
	bool animating;
	bool square_dragging;
	bool val_slider_dragging;
	// The coordinate that that the slider controls, 0-1
	float s_value;
	// the other two dims, usually x and y but can be theta/r when fixed=value.
	float x_value;
	float y_value;
	// In hsv mode, you can manipulate rgb values and vice versa. In those cases we must treat the
	// alternate system as exact, or a double conversion occurs that can cause the manipulation of
	// one value, say r while in hsv mode, to affect others in the same system, say g and b.
	// alternate_value is the current color represented in the alternate color system, and
	// from_alternate_value signals that alternate_value is taken as the official current color.
	bool from_alternate_value;
	Vector3 alternate_value;
	Vector4 text_color;
	// Font text_font_small;
	// xx variable width fonts...
	// Font text_font_medium;
	// Font text_font_large;
	i32 font;
	i32 text_font_small;
	i32 text_font_medium;
	i32 text_font_large;
// 	i32 menu_icon;
// 	i32 settings_icon;
	int small_char_width;
	int medium_char_width;
	int large_char_width;
	int medium_label_width;
	// Shader hsv_grad_shader;
	struct {
		char *path;
		u32 *shortened_path_utf32;
		i32 shortened_path_len;
		unsigned long long offset;
		int format;
		Vector4 last_write_color;
		double last_write_time;
	} outfile;
	bool debug;
	// XX Input state
	int mouse_x, mouse_y;
	bool mouse_down;
	bool mouse_was_down;
	int key_pressed;
	Uint32 start_ticks;
} State;

typedef struct tab_select
{
	Vector4 active_colors[3];
	Vector4 inactive_colors[3];
	char labels[3];
	float hover_brightness;
	Vector4 active_text_color;
	Vector4 inactive_text_color;
	Vector4 border_color;
	State *st;
	float anim_vdt;
	int x;
	int y;
	int w;
	int h;
	bool top;
	// internal state:
	int sel_i;
	float hover_v[3];
	float active_v[3];
} Tab_Select;

typedef struct number_select {
	char *fmt;
	int min;
	int max;
	bool wrap_around;
	State *st;
	float anim_vdt;
	int x;
	int y;
	float drag_pixels_per_value;
	// "internal" state:
	int w;
	int h;
	int value;
	bool selected;
	bool dragging;
	bool clicking;
	int drag_start_value;
	int drag_start_y;
	float shade_v;
	bool input_active;
	int input_n;
} Number_Select;

void myassert(bool p, char *fmt, ...) {
	if (!p) {
		va_list args;
		va_start(args, fmt);
		vfprintf(stderr, fmt, args);
		va_end(args);
		exit(1);
	}
}

char *color_strings[2][3] = { {"R", "G", "B"}, {"H", "S", "V"} };

Vector4 hex2color(unsigned int hex) {
	return (Vector4){
		((hex >> 24) & 0xFF) / 255.0f,
		((hex >> 16) & 0xFF) / 255.0f,
		((hex >> 8) & 0xFF) / 255.0f,
		(hex & 0xFF) / 255.0f
	};
}

Vector4 color_brightness(Vector4 c, float factor) {
	float r = (float) c.x + ((1.0f - c.x) * factor);
	float g = (float) c.y + ((1.0f - c.y) * factor);
	float b = (float) c.z + ((1.0f - c.z) * factor);
	return (Vector4) {
		CLAMP(r, 0.0f, 1.0f),
		CLAMP(g, 0.0f, 1.0f),
		CLAMP(b, 0.0f, 1.0f),
		c.w
	};
}

float max3f(float x, float y, float z)
{
	return (x > y) ? MAX(x, z) : MAX(y, z);
}

float min3f(float x, float y, float z)
{
	return (x < y) ? MIN(x, z) : MIN(y, z);
}

Vector4 rgb_to_hsv(Vector4 c)
{
	float r = c.x;
	float g = c.y;
	float b = c.z;

	float max = max3f(r, g, b);
	float min = min3f(r, g, b);
	float delta = max - min;

	Vector4 hsv;
	hsv.w = c.w;
	hsv.z = max; // value

	if (delta < 0.00001f) {
		hsv.x = 0;
		hsv.y = 0;
		return hsv;
	}

	hsv.y = (max > 0) ? (delta / max) : 0; // saturation

	// hue
	if (r >= max) {
		hsv.x = (g - b) / delta;
	} else if (g >= max) {
		hsv.x = 2.0f + (b - r) / delta;
	} else {
		hsv.x = 4.0f + (r - g) / delta;
	}

	hsv.x /= 6.0f;
	if (hsv.x < 0) hsv.x += 1.0f;

	return hsv;
}

Vector4 hsv_to_rgb(Vector4 hsv)
{
	float h = hsv.x;
	float s = hsv.y;
	float v = hsv.z;
	float c = v * s;
	float x = c * (1 - fabsf(fmodf(h * 6.0f, 2) - 1));
	float m = v - c;

	float r, g, b;
	if (h < 1.0f/6.0f) { r = c; g = x; b = 0; }
	else if (h < 2.0f/6.0f) { r = x; g = c; b = 0; }
	else if (h < 3.0f/6.0f) { r = 0; g = c; b = x; }
	else if (h < 4.0f/6.0f) { r = 0; g = x; b = c; }
	else if (h < 5.0f/6.0f) { r = x; g = 0; b = c; }
	else { r = c; g = 0; b = x; }

	return (Vector4) { r + m, g + m, b + m, hsv.w };
}

Vector4 rgb_to_hsl(Vector4 c)
{
	float r = c.x;
	float g = c.y;
	float b = c.z;

	float min = min3f(r, g, b);
	float max = max3f(r, g, b);
	float delta = max - min;

	Vector4 hsl;
	hsl.w = c.w;
	hsl.z = (max + min) * 0.5f; // lightness

	if (delta < 0.00001f) {
		hsl.x = 0.0f;
		hsl.y = 0.0f;
		return hsl;
	}

	hsl.y = (hsl.z > 0.0f && hsl.z < 1.0f) ? delta / (1 - fabsf(2*hsl.z - 1)) : 0.0f; // saturation

	// hue
	if (r >= max) {
		hsl.x = (g - b) / delta;
	} else if (g >= max) {
		hsl.x = 2.0f + (b - r) / delta;
	} else {
		hsl.x = 4.0f + (r - g) / delta;
	}

	hsl.x /= 6.0f;
	if (hsl.x < 0) hsl.x += 1.0f;

	return hsl;
}

Vector4 hsl_to_rgb(Vector4 hsl)
{
	float h = hsl.x;
	float s = hsl.y;
	float l = hsl.z;

	float c = (1 - fabsf(2*l - 1))*s;
	float x = c * (1 - fabsf(fmodf(h * 6.0f, 2) - 1));
	float m = l - c / 2;

	float r, g, b;
	if (h < 1.0f / 6.0f) { r = c; g = x; b = 0; }
	else if (h < 2.0f / 6.0f) { r = x; g = c; b = 0; }
	else if (h < 3.0f / 6.0f) { r = 0; g = c; b = x; }
	else if (h < 4.0f / 6.0f) { r = 0; g = x; b = c; }
	else if (h < 5.0f / 6.0f) { r = x; g = 0; b = c; }
	else { r = c; g = 0; b = x; }

	return (Vector4) { r + m, g + m, b + m, hsl.w };
}

// Brighten a color by an amount from 0 to 1.
Vector4 brighten_color(Vector4 rgb, float amount)
{
	Vector4 hsl = rgb_to_hsl(rgb);
	hsl.z = MIN(hsl.z + amount, 1.0f);
	return hsl_to_rgb(hsl);
}

// Darken a color by an amount from 0 to 1.
Vector4 darken_color(Vector4 rgb, float amount)
{
	Vector4 hsl = rgb_to_hsl(rgb);
	hsl.z = MAX(hsl.z - amount, 0.0f);
	return hsl_to_rgb(hsl);
}

float srgb_to_linear(float c) {
    return c <= 0.04045f ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f);
}

float luminance(float r, float g, float b) {
    float rs = srgb_to_linear(r);
    float gs = srgb_to_linear(g);
    float bs = srgb_to_linear(b);
    return 0.2126f * rs + 0.7152f * gs + 0.0722f * bs;
}

bool point_in_rect(Vector2 point, Rectangle rec) {
	return point.x >= rec.x && point.x <= rec.x + rec.width &&
	       point.y >= rec.y && point.y <= rec.y + rec.height;
}

bool point_in_circle(Vector2 point, Vector2 center, float radius) {
	float dx = point.x - center.x;
	float dy = point.y - center.y;
	return (dx*dx + dy*dy) <= (radius * radius);
}

double GetTime(State *st) {
	return (SDL_GetTicks() - st->start_ticks) / 1000.0;
}

Vector2 GetMousePosition(State *st) {
	return (Vector2){st->mouse_x, st->mouse_y};
}

bool read_color_from_outfile_and_maybe_update_offset(struct state *st, Vector4 *c)
{
	char *format = "%02x%02x%02x";
	FILE *f = fopen(st->outfile.path, "rb");
	fseek(f, st->outfile.offset, SEEK_SET);
	char str[8];
	fread(str, 1, 7, f);
	char *strptr = str;
	if (*strptr == '#') {
		st->outfile.offset++;
		strptr++;
	}
	str[7] = '\0';
	int r, b, g;
	int n_parsed = sscanf(strptr, format, &r, &g, &b);
	fclose(f);
	if (n_parsed == 3 && r >= 0 && r <= 255 && g >= 0 && g <= 255 && b >= 0 && b <= 255) {
		*c = (Vector4) { r / 255.0f, g / 255.0f, b / 255.0f, 1.0f };
		return true;
	} else {
		return false;
	}
}

// Returns true if *val moved
bool value_creep_towards(float *val, float target, float amount)
{
	if (*val < target) {
		*val = MIN(*val + amount, target);
	} else if (*val > target) {
		*val = MAX(*val - amount, target);
	} else {
		return false;
	}
	return true;
}

bool vector4_equal(Vector4 c1, Vector4 c2) {
	return c1.x == c2.x && c1.y == c2.y && c1.z == c2.z && c1.w == c2.w;
}

struct color_info {
	Vector4 rgb;
	Vector4 hsv;
};

// Update s_value, x_value and y_value based on current mode, which_s, and a color value.
void update_axis_values(struct state *st, int mode, int which_s, struct color_info ci)
{
	if (st->mode == 1) {
		Vector4 cur_hsv = ci.hsv;
		if (which_s == 0) {
			st->s_value = cur_hsv.x;
			st->x_value = cur_hsv.y;
			st->y_value = cur_hsv.z;
		} else if (which_s == 1) {
			st->s_value = cur_hsv.y;
			st->x_value = cur_hsv.x;
			st->y_value = cur_hsv.z;
		} else if (which_s == 2) {
			st->s_value = cur_hsv.z;
			st->x_value = cur_hsv.x;
			st->y_value = cur_hsv.y;
		}
	} else {
		Vector4 cur_rgb = ci.rgb;
		if (which_s == 0) {
			st->s_value = cur_rgb.x;
			st->x_value = cur_rgb.y;
			st->y_value = cur_rgb.z;
		} else if (which_s == 1) {
			st->s_value = cur_rgb.y;
			st->x_value = cur_rgb.z;
			st->y_value = cur_rgb.x;
		} else if (which_s == 2) {
			st->s_value = cur_rgb.z;
			st->x_value = cur_rgb.x;
			st->y_value = cur_rgb.y;
		}
	}
}

struct color_info current_color(struct state *st)
{
	struct color_info res;
	// rgb
	if ((st->mode == 0 && !st->from_alternate_value) || (st->mode == 1 && st->from_alternate_value)) {
		if (st->from_alternate_value) {
			res.rgb = (Vector4) { st->alternate_value.x, st->alternate_value.y,
				st->alternate_value.z, 1.0f };
		} else {
			float v1 = st->s_value;
			float v2 = st->x_value;
			float v3 = st->y_value;
			switch (st->which_s) {
			case 0:
				res.rgb = (Vector4) { v1, v2, v3, 1.0f };
				break;
			case 1:
				res.rgb = (Vector4) { v3, v1, v2, 1.0f };
				break;
			case 2:
				res.rgb = (Vector4) { v2, v3, v1, 1.0f };
				break;
			}
		}
		res.hsv = rgb_to_hsv(res.rgb);
	} else { // hsv
		if (st->from_alternate_value) {
			res.hsv = (Vector4) { st->alternate_value.x, st->alternate_value.y,
				st->alternate_value.z, 1.0f };
		} else {
			float v1 = st->s_value;
			float v2 = st->x_value;
			float v3 = st->y_value;
			switch (st->which_s) {
			case 0:
				res.hsv = (Vector4) { v1, v2, v3, 1.0f };
			break;
			case 1:
				// Note the difference from above. When slider is sat., x is hue, and y is value.
				res.hsv = (Vector4) { v2, v1, v3, 1.0f };
			break;
			case 2:
				// x=theta, y=r
				res.hsv = (Vector4) { v2, v3, v1, 1.0f };
			break;
			}
		}
		res.rgb = hsv_to_rgb(res.hsv);
	}
	if (st->from_alternate_value) {
		update_axis_values(st, st->mode, st->which_s, res);
	}
	return res;
}

void write_color_to_file(struct state *st, Vector4 color)
{
	char color_text[10];
	snprintf(color_text, 10, "%02x%02x%02x", (int)(color.x*255.0f), (int)(color.y*255.0f),
		(int) (color.z*255.0f));
	FILE *f = fopen(st->outfile.path, "r+b");
	if (!f)
		fprintf(stderr, "Failed to open file: %s.\n", st->outfile.path);
	int res = fseek(f, st->outfile.offset, SEEK_SET);
	if (res)
		fprintf(stderr, "Failed to write byte %llu in file %s.\n", st->outfile.offset, st->outfile.path);
	fwrite(color_text, 1, 6, f);
	fprintf(stderr, "Wrote %s to %s byte %llu.\n", color_text, st->outfile.path, st->outfile.offset);
	fclose(f);
}

// x, y = bottom left of gradient(for convenience with our visualization)
void add_gradient_rectangle(Scene *scene, float x, float y, float w, float h, Vector4 *corner_colors)
{
	Render_Context *context = scene_get_and_activate_context(scene);
	Float_Dynarray *vertices = context_get_vertices(context);
	scene_transform_xy(scene, context, &x, &y);
	context_transform_wh(context, &w, &h);
	i32 vertex_size = context_get_vertex_size(context);
	u64 max_verts = 6;
	dynarray_expand_by(vertices, max_verts * vertex_size);
	float *data = vertices->d + vertices->length;
	Vector2 positions[4] = {
		{ x, y },
		{ x + w, y },
		{ x, y + h },
		{ x + w, y + h }
	};
	i32 corners[6] = { 0, 2, 1, 1, 3, 2};
	for (i32 i=0; i<6; i++) {
		i32 corner = corners[i];
		Vector2 pos = positions[corner];
		Vector4 color = corner_colors[corner];
		data[i*vertex_size] = pos.x;
		data[i*vertex_size+1] = pos.y;
		data[i*vertex_size+2] = 0.0f;
		data[i*vertex_size+3] = color.x;
		data[i*vertex_size+4] = color.y;
		data[i*vertex_size+5] = color.z;
		data[i*vertex_size+6] = color.w;
		data[i*vertex_size+7] = 0.0f;
		data[i*vertex_size+8] = 0.0f;
		data[i*vertex_size+9] = -1.0f;
	}
	vertices->length += max_verts*vertex_size;
}

/* The shape of the slider: like add_gradient_rectangle, but with rounded left and right ends. Each
end is a solid color (corner_colors[0] and corner_colors[1]. Again x, y = bottom left. */
void add_gradient_rectangle_rounded_ends(Scene *scene, float x, float y, float w, float h,
	float radius, i32 segments_per_corner, Vector4 *corner_colors)
{
	add_gradient_rectangle(scene, x+radius, y, w-2.0f*radius, h, corner_colors);
	Vector2 left_corners[4] = { { x, y }, { x, y+h }, { x+radius, y+h }, { x+radius, y } };
	bool left_rounded[4] = { true, true, false, false };
	add_rounded_quad(scene, left_corners, left_rounded, radius, corner_colors[0]);
	Vector2 right_corners[4] = { { x+w-radius, y }, { x+w-radius, y+h }, { x+w, y+h },
	                             { x+w, y } };
	bool right_rounded[4] = { false, false, true, true };
	add_rounded_quad(scene, right_corners, right_rounded, radius, corner_colors[1]);
}

void draw_gradient_square_rgb(State *st, int x, int y, int size, int which_s, float s_value)
{
	Vector4 corner_cols[4];
	for (int i=0; i<2; i++) {
		for (int j=0; j<2; j++) {
			float c1 = j ? 1.0f : 0.0f;
			float c2 = i ? 1.0f : 0.0f;
			float s = s_value;
			if (which_s == 0) {
				corner_cols[(1-i)*2+j] = (Vector4) { s, c1, c2, 1.0f };
			} else if (which_s == 1) { // green
				corner_cols[(1-i)*2+j] = (Vector4) { c2, s, c1, 1.0f };
			} else if (which_s == 2) { // blue
				corner_cols[(1-i)*2+j] = (Vector4) { c1, c2, s, 1.0f };
			}
		}
	}
	add_gradient_rectangle(st->main_scene, x, y, size, size, corner_cols);
}

void draw_gradient_square_hsv(struct state *st, int x, int y, int size, int which_s,
	float s_value)
{
	Vector4 corner_cols[4];
	for (int i=0; i<2; i++) {
		for (int j=0; j<2; j++) {
			float c1 = j ? 1.0f : 0.0f;
			float c2 = i ? 1.0f : 0.0f;
			if (which_s == 0) { // hue
				corner_cols[(1-i)*2+j] =  (Vector4) { s_value, c1, c2, 1.0f };
			} else if (which_s == 1) { // saturation
				corner_cols[(1-i)*2+j] = (Vector4) { c1, s_value, c2, 1.0f };
			}
		}
	}
	add_gradient_rectangle(st->hsv_grad_scene, x, y, size, size, corner_cols);
}

void draw_gradient_circle_and_axes(int x, int y, int r, float s_value, struct state *st)
{
    {
    	Scene *scene = st->hsv_grad_scene;
		Render_Context *context = scene_get_and_activate_context(scene);
		Float_Dynarray *vertices = context_get_vertices(context);
		i32 vertex_size = context_get_vertex_size(context);
		float xf = x, yf = y, rf = r;
		scene_transform_xy(scene, context, &xf, &yf);
		context_transform_1(context, &rf);
		u64 max_verts = 360 * 3;
		dynarray_expand_by(vertices, max_verts * vertex_size);
		// context+
	    float *data = vertices->d + vertices->length;
	    for (int i=0; i<360; i++) {
	    	float angle1 = 2*F_PI*i/360.0f;
	    	float angle2 = 2*F_PI*(i+1)/360.0f;
	    	Vector2 positions[3] = {
	    		{ xf + rf*cos(angle1), yf + rf*sin(angle1) },
	    		{ xf + rf*cos(angle2), yf + rf*sin(angle2) },
	    		{ xf, yf }
	    	};
	    	float hues[3] = { i / 360.0f, (i+1) / 360.0f, i / 360.0f };
	    	float saturations[3] = { 1.0f, 1.0f, 0.0f };
	    	i32 offset = 3*i*vertex_size;
	    	for (i32 j=0; j<3; j++) {
		    	data[offset+j*vertex_size] = positions[j].x;
		    	data[offset+j*vertex_size+1] = positions[j].y;
		    	data[offset+j*vertex_size+2] = 0.0f;
		    	data[offset+j*vertex_size+3] = hues[j];
		    	data[offset+j*vertex_size+4] = saturations[j];
		    	data[offset+j*vertex_size+5] = s_value;
		    	data[offset+j*vertex_size+6] = 1.0f;
		    	data[offset+j*vertex_size+7] = 0.0f;
		    	data[offset+j*vertex_size+8] = 0.0f;
		    	data[offset+j*vertex_size+9] = -1.0f;
	    	}
	    }
		vertices->length += max_verts*vertex_size;
	}
	// tick marks
	float dpi = st->window->scale;
	for (float ang=0.0f; ang<360.0f; ang+=30.0) {
		float dx = cosf(ang*2*F_PI/360.0f);
		float dy = sinf(ang*2*F_PI/360.0f);
		int length = 5*dpi;
		Vector2 start = { x + r * dx, y + r * dy };
		Vector2 end = { start.x + length * dx, start.y + length * dy };
		add_line_ex(st->main_scene, start.x, start.y, end.x, end.y, 2.0*dpi, st->text_color);
	}
	// s arrow
	int arrow_len = 60*dpi;
	float arrow_w = 2.0*dpi;
	// int  = 4*dpi;
	// arrowhead
	float ah_len = 13*dpi;
	float ah_ang = (180-28)*2*F_PI/360.0f;
	float ah_w = 2.0*dpi;
	Vector2 arrow_end = (Vector2) { x+r+arrow_len, y};
	add_line_ex(st->main_scene, x+r+12*dpi, y, arrow_end.x, arrow_end.y, arrow_w, st->text_color);
	Vector2 ah_left = { arrow_end.x + ah_len*cosf(ah_ang), arrow_end.y + ah_len*sinf(ah_ang)};
	Vector2 ah_right = { arrow_end.x + ah_len*cosf(-ah_ang), arrow_end.y + ah_len*sinf(-ah_ang)};
	add_line_ex(st->main_scene, arrow_end.x, arrow_end.y, ah_left.x, ah_left.y, ah_w, st->text_color);
	add_line_ex(st->main_scene, arrow_end.x, arrow_end.y, ah_right.x, ah_right.y, ah_w, st->text_color);
	add_text(st->main_scene, st->font, FONT_MEDIUM_PX, "S", arrow_end.x-16.0*dpi, arrow_end.y-20.0*dpi,
		st->text_color);
	// h arrow
	float harr_d = 30*dpi;
	float harr_w = 2*dpi;
	float harr_ang1 = 12;
	float harr_ang2 = 28;
	Vector2 harr_end = { x + (r+harr_d+harr_w/2)*cosf(2*F_PI*harr_ang2/360.0f),
		y - (r+harr_d+harr_w/2)*sinf(2*F_PI*harr_ang2/360.0f) };
	float harr_dir_ang = (harr_ang2*2*F_PI / 360.0f) + F_PI / 2;
	// arrowhead
	float adj = 2*F_PI/120;
	// Angles are set up so that left arrowhead goes straight down
		Vector2 h_ah_left = { harr_end.x /*+ah_len*cosf(harr_dir_ang+ah_ang+adj)*/,
		harr_end.y-ah_len*sinf(harr_dir_ang+ah_ang+adj)};
	Vector2 h_ah_right = { harr_end.x+ah_len*cosf(harr_dir_ang-ah_ang+adj),
		harr_end.y-ah_len*sinf(harr_dir_ang-ah_ang+adj)};
	Vector4 c3 = st->text_color;
	add_line_ex(st->main_scene, harr_end.x, harr_end.y, h_ah_left.x, h_ah_left.y, ah_w, c3);
	add_line_ex(st->main_scene, harr_end.x, harr_end.y, h_ah_right.x, h_ah_right.y, ah_w,
		c3);
	add_text(st->main_scene, st->text_font_medium, FONT_MEDIUM_PX, "H", harr_end.x+18*dpi, harr_end.y-2*dpi,
		st->text_color);
	add_circle_arc_ex(st->main_scene, x, y, r+harr_d+harr_w/2, 2*F_PI*harr_ang1/360.0f,
		2*F_PI*harr_ang2/360.0f, 30, 2.0f*dpi, st->text_color);
}

// TODO: parameter for 512 vs etc ?
void draw_axes(int x, int y, int w, int h, float scale, struct state *st)
{
	int tick_sep = 64*scale;
	int tick_width = 2*scale;
	int y_tick_len = w/4;
	int x_tick_len = h/4;
	Vector4 tick_color = st->text_color;
	int label_size = 30*scale;
	Vector4 label_color = st->text_color;

	char *x_label;
	char *y_label;
	if (!(st->mode == 1 && st->which_s == 1)) {
		x_label = color_strings[st->mode][(st->which_s+1)%3];
		y_label = color_strings[st->mode][(st->which_s+2)%3];
	} else {
		x_label = color_strings[1][0];
		y_label = color_strings[1][2];
	}
	float dpi = st->window->scale;
	// x axis label
	add_text(st->main_scene, st->font, FONT_MEDIUM_PX, x_label,
			   x + 512*scale/2 - label_size, y + 512*scale + h, label_color);
	// y axis label
	add_text(st->main_scene, st->text_font_medium, FONT_MEDIUM_PX, y_label,
			   x - h, y + 512*scale/2, label_color);
	// x axis
	for (int ix = x; ix < (x+512*scale); ix += tick_sep) {
		add_rectangle(st->main_scene, ix, y+512*scale, tick_width, x_tick_len, tick_color);
	}
	// y axis
	for (int yi = 0; yi < (512*scale); yi += tick_sep) {
		add_rectangle(st->main_scene, x-y_tick_len, y + 512*scale - yi - tick_width, y_tick_len,
			tick_width, tick_color);
	}
}

bool tab_select(Tab_Select *self, Input_State *input)
{
	State *st = self->st;
	float dpi = st->window->scale;
	Vector2 pos = { input->pointer_x, input->pointer_y };
	int i = self->sel_i;
	Vector4 *active = self->active_colors;
	Vector4 *inactive = self->inactive_colors;
	Vector4 active_text = self->active_text_color;
	Vector4 inactive_text = self->inactive_text_color;
	float *hover_v = self->hover_v;
	Vector4 color1 = i == 0 ? active[0] : color_brightness(inactive[0], self->hover_brightness*hover_v[0]);
	Vector4 color2 = i == 1 ? active[1] : color_brightness(inactive[1], self->hover_brightness*hover_v[1]);
	Vector4 color3 = i == 2 ? active[2] : color_brightness(inactive[2], self->hover_brightness*hover_v[2]);
	Vector4 text_color1 = i == 0 ? active_text : color_brightness(inactive_text, self->hover_brightness*hover_v[0]);
	Vector4 text_color2 = i == 1 ? active_text : color_brightness(inactive_text, self->hover_brightness*hover_v[1]);
	Vector4 text_color3 = i == 2 ? active_text : color_brightness(inactive_text, self->hover_brightness*hover_v[2]);
	char text[2] = "X";
	float x = self->x;
	float tw = self->w / 3.0;
	float rnd = 7.0f; // rounded rectangle roundness
	float segs = 20; // rounded rectangle segments
	float off = 0 * dpi;
	float y = self->top ? self->y : self->y - off;
	// xx
	float h = self->h + off;
	float text_y = y + h/2.0f + FONT_SMALL_PX*CENTER_EM;
	Vector2 left_corners[4] = { { x, y }, { x, y+h }, { x+tw, y+h }, { x+tw, y }};
	bool left_rounded[4] = { self->top, !self->top, false, false };
	add_rounded_quad_ex(st->main_scene, left_corners, left_rounded, rnd, segs, color1);
	add_rounded_quad_outline_ex(st->main_scene, left_corners, left_rounded, rnd, segs, 1.0f,
		self->border_color);
	text[0] = self->labels[0];
	add_text(st->main_scene, st->font, FONT_SMALL_PX, text, x + (tw - st->small_char_width)/2.0,
		text_y, text_color1);
	float x_mid = x + tw;
	int last_x = x_mid + tw;
	int end_x = self->x + self->w;
	int last_w = end_x - last_x;
	Vector2 right_corners[4] = { { last_x, y }, { last_x, y+h }, { last_x+last_w, y+h },
		{ last_x+last_w, y } };
	bool right_rounded[4] = { false, false, !self->top, self->top };
	add_rounded_quad_ex(st->main_scene, right_corners, right_rounded, rnd, segs, color3);
	add_rounded_quad_outline_ex(st->main_scene, right_corners, right_rounded, rnd, segs, 1.0f,
		self->border_color);
	text[0] = self->labels[2];
	add_text(st->main_scene, st->font, FONT_SMALL_PX, text,
		last_x + (last_w - st->small_char_width)/2.0, text_y, text_color3);

	add_rectangle(st->main_scene, x_mid, self->y, tw, self->h, color2);
	add_rectangle_outline_ex(st->main_scene, x_mid, self->y, tw, self->h, 1*dpi,
		self->border_color);
	text[0] = self->labels[1];
	add_text(st->main_scene, st->font, FONT_SMALL_PX, text, x_mid + (tw - st->small_char_width)/2.0,
		text_y, text_color2);

	bool updated = false;
	float hover_targets[3] = { 0.0f, 0.0f, 0.0f };
	if (point_in_rect(pos, (Rectangle) { self->x, self->y, self->w, self->h})) {
		int tab_i = CLAMP((pos.x - self->x) / (self->w / 3.0f), 0, 2);
		if (input->mouse_pressed[AU_MOUSE_BUTTON_LEFT] && tab_i != self->sel_i) {
			self->sel_i = tab_i;
			updated = true;
		}
		hover_targets[tab_i] = 1.0f;
	}
	float active_targets[3] = { 0.0f, 0.0f, 0.0f };
	if (self->sel_i > 0) {
		active_targets[self->sel_i] = 1.0f;
	}
	for (int tab_i=0; tab_i<3; tab_i++) {
		bool hover_moved = value_creep_towards(&self->hover_v[tab_i], hover_targets[tab_i],
			self->anim_vdt);
		bool active_moved = value_creep_towards(&self->active_v[tab_i], active_targets[tab_i],
			self->anim_vdt);
		if (hover_moved || active_moved) {
			st->animating = true;
		}
	}

	return updated;
}

bool number_select(Number_Select *self, Input_State *input, struct color_info *ci)
{
	State *st = self->st;
	Vector4 rgb = ci->rgb;
	float dpi = st->window->scale;
	int new_value = self->value;
	// Draw the background highlight
	Vector4 hl_color;
	float lum = luminance(rgb.x, rgb.y, rgb.z);
	float cutoff = 0.179f;
	if (lum > cutoff) {
		// Todo: this scheme was reached with some manual tweaking: border colors were overdarkened,
		// saturated pinks and cyans were underdarkened. Still not perfect.
		float d = (lum - cutoff) / (1 - cutoff);
		hl_color = darken_color(rgb, self->shade_v*(0.075f + d*0.12f));
	} else {
		// Todo: overlightning on dark greens and reds, but not black.
		float d = (cutoff - lum) / (cutoff);
		hl_color = brighten_color(rgb, self->shade_v*(0.09f + d*0.05f));
	}

	i32 text_y = self->y + self->h/2.0f + FONT_MEDIUM_PX*CENTER_EM;

	bool hovered = false;
	float rnd = 7.0f;
	i32 segs = 15;
	add_rounded_rectangle_ex(st->main_scene, self->x - 10*dpi, self->y, self->w, self->h,
		rnd, segs, hl_color);
	char text[21];
	memset(text, 0, 21);
	if (self->input_active) {
		// Manually format self->fmt into 'text', and draw it. self->fmt must contain one and only one
		// %d, and can also contain %%. The formatted number is drawn in a different shade than the
		// rest of 'text'.
		int text_i = 0;
		int fmt_i = 0;
		int d_i;
		int d_chars;
		int len = (int) strlen(self->fmt);
		while (fmt_i <= len-2) {
			if (self->fmt[fmt_i] == '%' && self->fmt[fmt_i+1] == 'd') {
				d_chars = snprintf(text+text_i, 20-fmt_i, "%d", self->input_n);
				d_i = text_i;
				fmt_i += 2;
				text_i += d_chars;
				continue;
			} else if (self->fmt[fmt_i] == '%' && self->fmt[fmt_i+1] == '%') {
				// manually convert %% to % by skipping the first %
				fmt_i++;
				continue;
			} else {
				text[text_i++] = self->fmt[fmt_i++];
			}
			assert(text_i < 20);
		}
		text[text_i++] = self->fmt[fmt_i++];
		int x = self->x;
		char c = text[d_i];
		text[d_i] = '\0';
		add_text(st->main_scene, st->font, FONT_MEDIUM_PX, text, x, text_y, st->text_color);
		text[d_i] = c;
		x += d_i*(st->medium_char_width + 1.5*dpi);
		c = text[d_i + d_chars];
		text[d_i + d_chars] = '\0';
		add_text(st->main_scene, st->font, FONT_MEDIUM_PX, &text[d_i], x, text_y,
			st->text_color.x < 0.5f ? hex2color(0x303030ff) : hex2color(0xd8d8d8ff));
		text[d_i + d_chars] = c;
		x += d_chars * (st->medium_char_width + 1.5*dpi);
		add_text(st->main_scene, st->font, FONT_MEDIUM_PX, &text[d_i+d_chars], x, text_y,
			st->text_color);
	} else {
		snprintf(text, 21, self->fmt,
			self->input_active ? self->input_n : self->value);
		add_text(st->main_scene, st->font, FONT_MEDIUM_PX, text, self->x, text_y,
			st->text_color);
	}
	Vector2 pos = { input->pointer_x, input->pointer_y };
	bool hit = point_in_rect(pos, (Rectangle) { self->x, self->y, self->w, self->h });
	// Use hoverered highlighting for actual hovers and while dragging
	if ((hit && !input->mouse_down[AU_MOUSE_BUTTON_LEFT]) || self->dragging) {
		hovered = true;
	}
	// A click ending inside our borders means we are selected.
	if (hit && input->mouse_released[AU_MOUSE_BUTTON_LEFT] && self->clicking) {
		self->selected = true;
	}
	// A mouse press outside our borders means we are unselected.
	if (!hit && input->mouse_pressed[AU_MOUSE_BUTTON_LEFT]) {
		self->selected = false;
		self->input_active = false;
	}
	// In-progress clicks cannot leave our borders.
	if (!hit) {
		self->clicking = false;
	}
	// Handle keyboard input while selected.
	if (self->selected) {
		// Set animating conservatively on all the keypresses we handle.
		if (input->text_entered->length > 0 || input->key_pressed[AU_KEY_BACKSPACE]
			|| input->key_pressed[AU_KEY_ENTER] || input->key_pressed[AU_KEY_ESCAPE]) {
			st->animating = true;
		}
		for (i64 i = 0; i<input->text_entered->length; i++) {
			i32 key = input->text_entered->d[i];
			int key_num = -1;
			if (key >= '0' && key <= '9') {
				key_num = key-'0';
			}
			if (!self->input_active && key_num >= 0) {
				if (key_num >= self->min && key_num <= self->max) {
					self->input_active = true;
					self->input_n = key_num;
				}
			} else if (self->input_active && key_num >= 0) {
				int new_input_n = 10 * self->input_n + key_num;
				if (new_input_n >= self->min && new_input_n <= self->max) {
					self->input_n = new_input_n;
				}
			}
		}
		if (self->input_active && input->key_pressed[AU_KEY_BACKSPACE]) {
			if (self->input_n >= 10) {
				self->input_n /= 10;
			} else {
				self->input_active = false;
			}
		}
		if (self->input_active && input->key_pressed[AU_KEY_ESCAPE]) {
			self->input_active = false;
		}
		if (self->input_active && input->key_pressed[AU_KEY_ENTER]) {
			new_value = self->input_n;
			self->input_active = false;
		}
	}
	// The start conditions for both clicks and drags are the same: a mouse press within our borders.
	if (hit && input->mouse_pressed[AU_MOUSE_BUTTON_LEFT]) {
		self->dragging = true;
		self->clicking = true;
		self->drag_start_y = pos.y;
		self->drag_start_value = self->value;
	}
	if (input->mouse_released[AU_MOUSE_BUTTON_LEFT]) {
		self->dragging = false;
	}
	if (self->dragging) {
		new_value = self->drag_start_value
			+ (-(pos.y - self->drag_start_y) / (self->drag_pixels_per_value));
		if (self->wrap_around) {
			if (new_value < self->min) {
				new_value = self->max + 1 - (self->min - new_value) % (self->max + 1 - self->min);
			} else if (new_value > self->max) {
				new_value = self->min + (new_value - self->min) % (self->max + 1 - self->min);
			}
		} else {
			new_value = CLAMP(new_value, self->min, self->max);
			if (new_value == self->max || new_value == self->min) {
				// so that you can go past the end, and get immediate changes coming back
				self->drag_start_value = new_value;
				self->drag_start_y = pos.y;
			}
		}
	}
	float shade_v_target = 0.0;
	if (self->selected) {
		shade_v_target = 1.0f;
	} else if (hovered) {
		shade_v_target = 0.5f;
	}
	if (value_creep_towards(&self->shade_v, shade_v_target, self->anim_vdt * .6)) {
		st->animating = true;
	}
	if (new_value != self->value) {
		st->animating = true;
		self->value = new_value;
		return true;
	} else {
		return false;
	}
}

bool number_select_immargs(Number_Select *ns, char *fmt, int min, int max, bool wrap_around,
	State *st, float anim_vdt, int x, int y, int w, int h, float drag_pixels_per_value,
	Input_State *input, struct color_info *ci)
{
	ns->fmt = fmt;
	ns->min = min;
	ns->max = max;
	ns->wrap_around = wrap_around;
	ns->st = st;
	ns->anim_vdt = anim_vdt;
	ns->x = x;
	ns->y = y;
	ns->w = w;
	ns->h = h;
	ns->drag_pixels_per_value = drag_pixels_per_value;
	return number_select(ns, input, ci);
}

void draw_ui_and_respond_input(struct state *st, Input_State *input)
{
	float anim_vdt = 0.2f;
	st->animating = false;

	Vector2 pos = { input->pointer_x, input->pointer_y };
	if (input->mouse_released[AU_MOUSE_BUTTON_LEFT]) {
		st->square_dragging = false;
		st->val_slider_dragging = false;
	}

	if (input->mouse_pressed[AU_MOUSE_BUTTON_LEFT]) {
		/*
		** Most left mouse presses will result in some visible change to the screen, and we can't always
		** reflect that change this frame. So, guarantee another frame is drawn after this one by setting
		** animating = true. This is the lazy way: compared to setting animating = true in each branch
		** where we definitely need it, we sometimes draw one extra frame we didn't need to. Compared to
		** handling all input before all output, we sometimes cause one extra frame of latency.
		*/
		st->animating = true;
	}

	struct color_info ci = current_color(st);
	Vector4 cur_color = ci.rgb;
	Vector4 cur_hsv = ci.hsv;
	glClearColor(cur_color.x, cur_color.y, cur_color.z, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);
	float dpi = st->window->scale;
	if (luminance(cur_color.x, cur_color.y, cur_color.z) >= 0.179) {
		st->text_color = BLACK;
	} else {
		st->text_color = WHITE;
	}

	// misc ui colors
	Vector4 bright_grey_bg = hex2color(0xa0a0a0ff);
	Vector4 dim_grey_bg = hex2color(0x505050ff);
	Vector4 fixed_indication_color;
	if (st->mode == 0) {
		int wf = st->which_s;
		int rgb_fixed_ind[] = { 0xc00000ff, 0x00c000ff, 0x0080ffff };
		fixed_indication_color = hex2color(rgb_fixed_ind[wf]);
	} else {
		fixed_indication_color = bright_grey_bg;
	}

	// menu_icon
	// add_image(st->main_scene, st->menu_icon, 10*dpi, 30*dpi);
	// int screen_min_dim = MIN(st->screenWidth, st->screenHeight);

	// The scale computed here includes dpi scaling and potential scaling down to fit within the window.
	// ui_h and ui_w depend on a bunch constants below, e.g. grad_x_axis_h; changing anything that
	// changes the overal width or height requires an update here.
	float ui_h = st->outfile.path ? 805*dpi : 775*dpi;
	float ui_w = 532*dpi;
	float scale = dpi;
	if (st->main_scene->h < ui_h || st->main_scene->w < ui_w) {
		float scaledown_h = (st->main_scene->h / ui_h);
		float scaledown_w = (st->main_scene->w / ui_w);
		scale = dpi * MIN(scaledown_w, scaledown_h);
	}

	// output file indicator
	int out_ind_top_w = 512*scale;
	int out_ind_bottom_w = 462*scale;
	int out_ind_h = 30*scale;
	int out_ind_top_x = (st->main_scene->w - out_ind_top_w) / 2.0f;
	int out_ind_bottom_x = (st->main_scene->w - out_ind_bottom_w) / 2.0f;
	int out_ind_top_y = 0;
	int out_ind_bottom_y = 0;
	if (st->outfile.path)
	{
		out_ind_bottom_y = out_ind_top_y + out_ind_h;
		Vector2 out_ind_verts[4] = {
			{ out_ind_bottom_x, out_ind_bottom_y },
			{ out_ind_bottom_x + out_ind_bottom_w, out_ind_bottom_y},
			{ out_ind_top_x + out_ind_top_w, out_ind_top_y },
			{ out_ind_top_x, out_ind_top_y }
		};
		bool out_ind_rounded[4] = { true, true, false, false };
		Vector4 out_ind_bgcolor = hex2color(0x303030c0);
		i32 text_x = out_ind_bottom_x +
			(out_ind_bottom_w - st->outfile.shortened_path_len*(st->small_char_width+1.0*scale))/2.0f;
		i32 text_y = out_ind_top_y + out_ind_h/2.0f + FONT_SMALL_PX*CENTER_EM;
		add_rounded_quad_ex(st->main_scene, out_ind_verts, out_ind_rounded, 12*scale, 12,
			out_ind_bgcolor);
		add_text_utf32(st->main_scene, st->font, FONT_SMALL_PX, st->outfile.shortened_path_utf32,
			text_x, text_y, WHITE);
	}

	// gradient square or circle
	int grad_y_axis_w = 30*scale;
	int grad_x_axis_h = 30*scale;
	int grad_square_x = MAX(grad_y_axis_w, (st->main_scene->w - 512*scale)/2);
	// The entire color picker UI is around 775 pixels tall; center it with slightly more margin below.
	int grad_square_y = out_ind_bottom_y + (st->main_scene->h - out_ind_bottom_y - 775*scale) * 0.4f;
	int grad_square_y_end = grad_square_y + 512*scale;
	int grad_square_x_end = grad_square_x + 512*scale;
	bool grad_square = true;
	int grad_circle_x = grad_square_x + 512*scale/2;
	int grad_circle_y = grad_square_y + 512*scale/2;
	int grad_circle_r = 512*scale/2;
	if (st->mode == 0)
	{
		draw_gradient_square_rgb(st, grad_square_x, grad_square_y, 512*scale, st->which_s,
			st->s_value);
		draw_axes(grad_square_x, grad_square_y, grad_x_axis_h, grad_y_axis_w, scale, st);
	}
	else
	{
		if (st->which_s == 2) {
			grad_square = false;
			draw_gradient_circle_and_axes(grad_circle_x, grad_circle_y, grad_circle_r,
				st->s_value, st);
		} else {
			draw_gradient_square_hsv(st, grad_square_x, grad_square_y, 512*scale, st->which_s,
				st->s_value);
			draw_axes(grad_square_x, grad_square_y, grad_x_axis_h, grad_y_axis_w, scale, st);
		}
	}
	// indicator circle
	{
		int ind_x, ind_y;
		if (grad_square) {
			ind_x = grad_square_x + st->x_value * (512*scale);
			ind_y = grad_square_y + 512*scale - st->y_value * (512*scale);
		} else {
			ind_x = grad_circle_x + grad_circle_r*st->y_value*cosf(st->x_value * 2*F_PI);
			ind_y = grad_circle_y - grad_circle_r*st->y_value*sinf(st->x_value * 2*F_PI);
		}
		add_circle_outline_ex(st->main_scene, ind_x, ind_y, 6*scale, 20, 1*scale, st->text_color);
		int r2 = 4*scale;
		int r3 = 8*scale;
		add_line_ex(st->main_scene, ind_x - r3, ind_y, ind_x - r2, ind_y, 1*scale, st->text_color);
		add_line_ex(st->main_scene, ind_x + r2, ind_y, ind_x + r3, ind_y, 1*scale, st->text_color);
		add_line_ex(st->main_scene, ind_x, ind_y - r3, ind_x, ind_y - r2, 1*scale, st->text_color);
		add_line_ex(st->main_scene, ind_x, ind_y + r2, ind_x, ind_y + r3, 1*scale, st->text_color);
		if (input->mouse_pressed[AU_MOUSE_BUTTON_LEFT]) {
			Rectangle rec = {grad_square_x, grad_square_y, 512*scale, 512*scale};
			Vector2 c = { grad_square_x + 512*scale/2, grad_square_y + 512*scale/2 };
			if ((grad_square && point_in_rect(pos, rec))
				|| (!grad_square && point_in_circle(pos, c, 512*scale/2))) {
				st->square_dragging = true;
			}
		}
		if (st->square_dragging) {
			if (input->pointer_dy || input->pointer_dx)
				st->animating = true;
			int y_adj = 3*scale;
			int x_adj = 2*scale;
			if (grad_square) {
				st->x_value = MIN(MAX((pos.x - x_adj - grad_square_x) / (512*scale), 0.0f), 1.0f);
				// xx off by one?
				st->y_value = MIN(MAX((grad_square_y + 512*scale - pos.y + y_adj) / (512*scale), 0.0f),
					1.0f);
			} else {
				int x_res = pos.x -x_adj - (grad_square_x + 512*scale/2);
				int y_res = pos.y - y_adj - (grad_square_y + 512*scale/2);
				y_res = -y_res;
				// theta
				st->x_value = atan2(y_res, x_res) / (2*F_PI);
				st->x_value = st->x_value < 0 ? 1.0 + st->x_value : st->x_value;
				// r
				st->y_value = MIN(MAX(sqrtf(x_res*x_res+y_res*y_res)/(512*scale/2), 0.0), 1.0);
			}
			st->from_alternate_value = false;
		}
	}

	// fixed color buttons
	int top_tabs_x = grad_square_x;
	int top_tabs_y = grad_square_y_end + grad_x_axis_h + 10*scale;
	int top_tabs_h = 30*scale;
	int top_tabs_w = 95*scale;
	int main_button_x = top_tabs_x;
	int main_button_y = top_tabs_y + top_tabs_h;
	int main_button_w = top_tabs_w;
	int main_button_h = 75*scale;
	int ind_tabs_y = main_button_y + main_button_h;
	Vector4 buttons_border_color = hex2color(0xb0b0b0ff);
	// tabs
	{
		static Tab_Select rgb_tabs;
		static Tab_Select hsv_tabs;
		static bool first_frame_setup_done = false;
		if (!first_frame_setup_done) {
			rgb_tabs.active_colors[0] = hex2color(0xc00000ff);
			rgb_tabs.active_colors[1] = hex2color(0x00c000ff);
			rgb_tabs.active_colors[2] = hex2color(0x0080ffff);
			rgb_tabs.inactive_colors[0] = hex2color(0x700000ff);
			rgb_tabs.inactive_colors[1] = hex2color(0x007000ff);
			rgb_tabs.inactive_colors[2] = hex2color(0x0000c0ff);
			rgb_tabs.active_text_color = hex2color(0xffffffff);
			rgb_tabs.inactive_text_color = hex2color(0xa0a0a0ff);
			rgb_tabs.labels[0] = 'R';
			rgb_tabs.labels[1] = 'G';
			rgb_tabs.labels[2] = 'B';
			rgb_tabs.top = true;
			for (int i=0; i<3; i++) {
				hsv_tabs.active_colors[i] = bright_grey_bg;
				hsv_tabs.inactive_colors[i] = dim_grey_bg;
			}
			float sel_hov_brightness = 0.4f;
			hsv_tabs.active_text_color = rgb_tabs.active_text_color;
			hsv_tabs.inactive_text_color = rgb_tabs.inactive_text_color;
			rgb_tabs.hover_brightness = sel_hov_brightness;
			rgb_tabs.anim_vdt = anim_vdt;
			rgb_tabs.st = st;
			hsv_tabs.labels[0] = 'H';
			hsv_tabs.labels[1] = 'S';
			hsv_tabs.labels[2] = 'V';
			hsv_tabs.hover_brightness = sel_hov_brightness;
			hsv_tabs.anim_vdt = anim_vdt;
			hsv_tabs.st = st;
			hsv_tabs.top = false;
			first_frame_setup_done = true;
		}
		rgb_tabs.border_color = buttons_border_color;
		hsv_tabs.border_color = rgb_tabs.border_color;
		if (!st->mode) {
			rgb_tabs.sel_i = st->which_s;
			hsv_tabs.sel_i = -1;
		} else {
			rgb_tabs.sel_i = -1;
			hsv_tabs.sel_i = st->which_s;
		}
		rgb_tabs.x = top_tabs_x;
		rgb_tabs.y = top_tabs_y;
		rgb_tabs.w = top_tabs_w;
		rgb_tabs.h = top_tabs_h;
		if (tab_select(&rgb_tabs, input)) {
			st->mode = 0;
			st->which_s = rgb_tabs.sel_i;
			update_axis_values(st, st->mode, st->which_s, ci);
		}
		hsv_tabs.x = main_button_x;
		hsv_tabs.y = main_button_y + main_button_h;
		hsv_tabs.w = main_button_w;
		hsv_tabs.h = top_tabs_h;
		if (tab_select(&hsv_tabs, input)) {
			st->mode = 1;
			st->which_s = hsv_tabs.sel_i;
			update_axis_values(st, st->mode, st->which_s, ci);
		}
	}
	// main button
	{
		static float main_button_hover_v = 0;
		float hov_bright = 0.4f;
		Vector4 fixed_button_color = color_brightness(fixed_indication_color,
			main_button_hover_v * hov_bright);
		add_rectangle(st->main_scene, main_button_x, main_button_y, main_button_w, main_button_h,
			fixed_button_color);
		add_rectangle_outline_ex(st->main_scene, main_button_x, main_button_y, main_button_w,
			main_button_h, 1*scale, buttons_border_color);
		i32 main_button_text_x = main_button_x + main_button_w/2.0f-st->large_char_width/2.0f;
		i32 main_button_text_y = main_button_y + main_button_h/2.0f+FONT_LARGE_PX*CENTER_EM;
		add_text(st->main_scene, st->font, FONT_LARGE_PX, color_strings[st->mode][st->which_s],
			main_button_text_x, main_button_text_y, WHITE);
		if (point_in_rect(pos, (Rectangle) { main_button_x, main_button_y, main_button_w,
			ind_tabs_y-main_button_y})) {
			if (input->mouse_pressed[AU_MOUSE_BUTTON_LEFT]) {
				st->which_s = (st->which_s + 1) % 3;
				update_axis_values(st, st->mode, st->which_s, ci);
				main_button_hover_v = 0;
			}
			if (!input->mouse_down[AU_MOUSE_BUTTON_LEFT]) {
				st->animating = st->animating || value_creep_towards(&main_button_hover_v, 1.0f, anim_vdt);
			}
		} else {
			st->animating = st->animating || value_creep_towards(&main_button_hover_v, 0.0f, anim_vdt);
		}
	}

	// fixed value slider
	int val_slider_x = main_button_x + main_button_w + 30*scale;
	// center vertically relative to two adjacent buttons
	int val_slider_y = main_button_y + main_button_h / 2.0f;
	int val_slider_w = grad_square_x_end - val_slider_x;
	int val_slider_h = 60*scale;
	int val_slider_offset = roundf(val_slider_w * ( (float) st->s_value ));
	{
		int bar_h = 26*scale;
		int circle_r = 18*scale;
		// Compute colors that would result from slider being all the way down and all the way up
		Vector4 slider_down_color;
		Vector4 slider_up_color;
		float xv = st->x_value;
		float yv = st->y_value;
		switch (st->which_s) {
		case 0: {
			slider_down_color = (Vector4) { 0.0f, xv, yv, 1.0f };
			slider_up_color = (Vector4) { 1.0f, xv, yv, 1.0f };
		} break;
		case 1: {
			slider_down_color = (Vector4) { xv, 0.0f, yv, 1.0f };
			slider_up_color = (Vector4) { xv, 1.0, yv, 1.0f };
		} break;
		case 2: {
			slider_down_color = (Vector4) { xv, yv, 0.0f, 1.0f };
			slider_up_color = (Vector4) { xv, yv, 1.0f, 1.0f };
		} break;
		}
		// if (st->mode == 1) {
		// 	slider_down_color = hsv_to_rgb(slider_down_color);
		// 	slider_up_color = hsv_to_rgb(slider_up_color);
		// }
		Vector4 corner_cols[4] = { slider_down_color, slider_up_color,
		                           slider_down_color, slider_up_color };
		float out_w = 8.0f*scale;
		// Todo: because outline color is the same as fixed_indication_color for hsv modes, the slider
		// can completely disappear in rare cases.
		Vector4 outline_color = bright_grey_bg;
		add_rounded_rectangle_ex(st->main_scene, val_slider_x-out_w/2.0f, val_slider_y-(bar_h+out_w)/2.0f,
			val_slider_w+out_w, bar_h+out_w, 12.0f*scale, 10, outline_color);
		Scene *scene = st->mode == 1 ? st->hsv_grad_scene : st->main_scene;
		add_gradient_rectangle_rounded_ends(scene, val_slider_x, val_slider_y-bar_h/2.0f,
			val_slider_w, bar_h, 8.0f*scale, 10, corner_cols);
		add_circle_ex(st->main_scene, val_slider_x + val_slider_offset, val_slider_y, circle_r,
			30, fixed_indication_color);
		if (input->mouse_pressed[AU_MOUSE_BUTTON_LEFT] || st->val_slider_dragging) {
			if (!st->val_slider_dragging && point_in_rect(pos,
				(Rectangle) { val_slider_x - circle_r, val_slider_y-val_slider_h/2.0f,
					val_slider_w+2*circle_r, val_slider_h } )) {
				st->val_slider_dragging = true;
			}
			if (st->val_slider_dragging) {
				val_slider_offset = MIN(val_slider_w, MAX(0, pos.x - val_slider_x));
				st->s_value = MIN(MAX((float) val_slider_offset / val_slider_w, 0), 1.0);
				// xx check if we actually changed the color?
				st->from_alternate_value = false;
				if (input->pointer_dx) {
					st->animating = true;
				}
			}
		}
	}

	int rgb_select_w = 6*(st->medium_char_width + 1.5*scale);
	int r_select_x = (st->main_scene->w - st->medium_label_width)/2.0f;
	int r_select_y = val_slider_y + 90*scale;
	{
		// rgb number selectors
		bool rgb_num_select_changed = false;
		static Number_Select r_num_select;
		r_num_select.value = cur_color.x * 255.0f;
		if (number_select_immargs(&r_num_select, "r:%d ", 0, 255, false, st, anim_vdt, r_select_x,
			r_select_y, rgb_select_w, 30*scale, 800.0f / 256.0f, input, &ci)) {
			rgb_num_select_changed = true;
		}
		static Number_Select g_num_select;
		g_num_select.value = cur_color.y * 255.0f;
		if (number_select_immargs(&g_num_select, "g:%d ", 0, 255, false, st, anim_vdt,
			r_num_select.x+r_num_select.w, r_num_select.y, rgb_select_w, 30*scale, 800.0f / 256.0f,
			input, &ci)) {
			rgb_num_select_changed = true;
		}
		static Number_Select b_num_select;
		b_num_select.value = cur_color.z * 255.0f;
		if (number_select_immargs(&b_num_select, "b:%d ", 0, 255, false, st, anim_vdt,
			g_num_select.x+g_num_select.w, g_num_select.y, rgb_select_w, 30*scale, 800.0f / 256.0f,
			input, &ci)) {
			rgb_num_select_changed = true;
		}
		if (rgb_num_select_changed) {
			Vector4 new_rgb = { r_num_select.value / 255.0f,  g_num_select.value / 255.0f,
				b_num_select.value / 255.0f, 1.0f };
			if (st->mode == 1) {
				st->from_alternate_value = true;
				st->alternate_value = (Vector3) { new_rgb.x, new_rgb.y, new_rgb.z };
			} else if (st->from_alternate_value) {
				st->from_alternate_value = false;
			}
			Vector4 new_hsv = rgb_to_hsv(new_rgb);
			struct color_info new_ci = { new_rgb, new_hsv };
			update_axis_values(st, st->mode, st->which_s, new_ci);
		}
		// hex label
		char value[40];
		sprintf(value, "hex:#%02x%02x%02x", (int)(cur_color.x*255.0f), (int)(cur_color.y*255.0f),
			(int)(cur_color.z*255.0f));
		int hex_label_x = b_num_select.x + b_num_select.w;
		int hex_label_y = r_num_select.y + r_num_select.h/2.0f + FONT_MEDIUM_PX*CENTER_EM;
		add_text(st->main_scene, st->font, FONT_MEDIUM_PX, value, hex_label_x, hex_label_y,
			st->text_color);
		// hsv number selectors
		bool hsv_num_select_changed = false;
		int hsv_select_w = 7*(st->medium_char_width + 1.5*scale);
		static Number_Select h_num_select;
		h_num_select.value = cur_hsv.x * 360.0f;
		if (number_select_immargs(&h_num_select, "h:%d\xc2\xb0", 0, 359, true, st, anim_vdt,
			r_num_select.x, r_num_select.y + 35*scale, hsv_select_w, 30*scale, 800.0f/360.0f,
			input, &ci)) {
			hsv_num_select_changed = true;
		}
		static Number_Select s_num_select;
		s_num_select.value = cur_hsv.y * 100.0f;
		if (number_select_immargs(&s_num_select, "s:%d%% ", 0, 100, false, st, anim_vdt,
			h_num_select.x+h_num_select.w, h_num_select.y, hsv_select_w, 30*scale, 800.0f/100.0f,
			input, &ci)) {
			hsv_num_select_changed = true;
		}
		static Number_Select v_num_select;
		v_num_select.value = cur_hsv.z * 100.0f;
		if (number_select_immargs(&v_num_select, "v:%d%% ", 0, 100, false, st, anim_vdt,
			s_num_select.x+s_num_select.w, h_num_select.y, hsv_select_w, 30*scale, 800.0f/100.0f,
			input, &ci)) {
			hsv_num_select_changed = true;
		}
		if (hsv_num_select_changed) {
			Vector4 new_hsv = { h_num_select.value / 360.0f, s_num_select.value / 100.0f,
				v_num_select.value / 100.0f, 1.0f };
			if (st->mode == 0) {
				st->from_alternate_value = true;
				st->alternate_value = (Vector3) { new_hsv.x, new_hsv.y, new_hsv.z };
			} else if (st->from_alternate_value) {
				st->from_alternate_value = false;
			}
			Vector4 new_rgb = hsv_to_rgb(new_hsv);
			struct color_info new_ci = { new_rgb, new_hsv };
			update_axis_values(st, st->mode, st->which_s, new_ci);
		}
		if (rgb_num_select_changed && hsv_num_select_changed) {
			printf("rgb and hsv selects changed\n");
		}
	}

	// write to file
	double now = GetTime(st);
	if (st->outfile.path && now - st->outfile.last_write_time > WRITE_INTERVAL &&
		!vector4_equal(cur_color, st->outfile.last_write_color)) {
		write_color_to_file(st, cur_color);
		st->outfile.last_write_color = cur_color;
		st->outfile.last_write_time = now;
	}
	// frame_n++;
}

const char* hsv_grad_vertex_shader =
"#version 330 core\n"
"layout (location = 0) in vec3 aPos;\n"
"layout (location = 1) in vec4 aColor;\n"
"layout (location = 2) in vec2 aTexCoord;\n"
"layout (location = 3) in float aFontIndex;\n"
"out vec4 fColor;\n"
"out vec2 TexCoord;\n"
"flat out float fFontIndex;\n"
"uniform float uYScale;\n"
"void main()\n"
"{\n"
"   gl_Position = vec4(aPos.x, aPos.y / uYScale, aPos.z, 1.0);\n"
"   fColor = aColor;\n"
"   TexCoord = aTexCoord;\n"
"   fFontIndex = aFontIndex;\n"
"}";

const char* hsv_grad_fragment_shader =
"#version 330 core\n"
"#define MAX_FONTS 8\n"
"out vec4 FragColor;\n"
"in vec4 fColor;\n"
"in vec2 TexCoord;\n"
"flat in float fFontIndex;\n"
"uniform sampler2D uFonts[MAX_FONTS];\n"
"vec3 hsv2rgb(vec3 c) {\n"
"    vec4 K = vec4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);\n"
"    vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);\n"
"    return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);\n"
"}\n"
"void main()\n"
"{\n"
"    vec4 base = fColor;\n"
"    FragColor = vec4(hsv2rgb(base.xyz), base.w);\n"
"}";

char *usage_str =
"quickpick [file@offset]\n"
"Options:\n"
"  --file FILE     choose a file to output to; alternative to file@offset\n"
"  --offset N      choose an offset in FILE; alternative to file@offset\n";

// nanosvg wants byte order RGBA, so (little endian) hex is 0xaabbggrr.
u32 color2abgrhex(Vector4 color) {
	return ((u32) color.w << 24) + ((u32) color.z << 16) + ((u32) color.y << 8) + ((u32) color.x);
}

// Prevent UI from stoppping during an interactive resize on Windows.
void on_resize_watcher(struct window *window, i32 old_width, i32 old_height) {
	State *st = (State *) window->udata;
	draw_ui_and_respond_input(st, window->input);
	commit_changes(window);
}

void init_for_dpi(struct state *st, float dpi)
{
	st->small_char_width = measure_text_widthf(st->main_scene, st->font, FONT_SMALL_PX, "R");
	st->medium_char_width = measure_text_widthf(st->main_scene, st->font, FONT_MEDIUM_PX, "R");
	st->large_char_width = measure_text_widthf(st->main_scene, st->font, FONT_MEDIUM_PX, "R");

	st->medium_label_width = measure_text_width(st->main_scene, st->font, FONT_MEDIUM_PX,
		"r:255 g:255 b:255 hex:#ffffff");
}

int main(int argc, char *argv[])
{
	arena_init(&app_arena);
	struct state *st = (struct state *) aalloc(&app_arena, sizeof(struct state));
	st->arena = &app_arena;
	st->mode = 0;
	st->which_s = 0;
	st->s_value = 0.0f;
	st->x_value = 0;
	st->y_value = 0;
	st->square_dragging = false;
	st->val_slider_dragging = false;
	st->text_color = WHITE;
	st->outfile.path = NULL;
	st->outfile.offset = 0;
	st->outfile.format = 0;
	st->outfile.last_write_color = (Vector4) { 0, 0, 0, 1.0f };
	st->debug = false;

	for (int i=1; i<argc; i++) {
		char *arg = argv[i];
		if (argv[i][0] == '-' && argv[i][1] == '-') {
			char *longarg = &argv[i][2];
			if (strcmp(longarg, "file")==0) {
				errexit_unless(i+1<argc && !st->outfile.path, usage_str);
				st->outfile.path = argv[i+1];
				i++;
			} else if (strcmp(longarg, "offset")==0) {
				errexit_unless(i+1<argc && !st->outfile.offset, usage_str);
				errno = 0;
				st->outfile.offset = strtoull(argv[i+1], NULL, 10);
				errexit_unless(!errno, usage_str);
				i++;
			} else if (strcmp(longarg, "debug")==0) {
				st->debug = true;
			}
		} else if (argv[i][0] == '-') {
			errexit(usage_str);
		} else {
			errexit_unless(!st->outfile.path, usage_str);
			char *sep = strchr(arg, '@');
			errexit_unless(sep, usage_str);
			int path_len = sep - arg;
			st->outfile.path = malloc(path_len+1);
			memcpy(st->outfile.path, arg, path_len);
			st->outfile.path[path_len] = '\0';
			errno = 0;
			st->outfile.offset = strtoul(sep+1, NULL, 10);
			errexit_unless(!errno, usage_str);
			i++;
		}
	}

	if (st->debug && st->outfile.path) {
		printf("Outfile: %s\n", st->outfile.path);
	}

	Window *window = window_create(&app_arena, 680, 860, "QuickPick", true);
	st->window = window;
	st->main_scene = &window->sc;
	window->udata = st;
	window->resize_callback = &on_resize_watcher;

	st->hsv_grad_context = window_create_render_context(window, hsv_grad_vertex_shader,
		hsv_grad_fragment_shader, 10, true);
	st->hsv_grad_scene = scene_create_child_with_context(&window->sc, st->arena, 0, 0,
		window->sc.w, window->sc.h, st->hsv_grad_context);

	st->font = load_font_from_memory(st->main_scene, noto_sans_mono, noto_sans_mono_len);

	if (st->debug) {
		int msaa_buffers = 0, msaa_samples = 0, doublebuffer = 0;
		SDL_GL_GetAttribute(SDL_GL_MULTISAMPLEBUFFERS, &msaa_buffers);
		SDL_GL_GetAttribute(SDL_GL_MULTISAMPLESAMPLES, &msaa_samples);
		SDL_GL_GetAttribute(SDL_GL_DOUBLEBUFFER, &doublebuffer);
		fprintf(stderr, "GL_VENDOR: %s\n", glGetString(GL_VENDOR));
		fprintf(stderr, "GL_RENDERER: %s\n", glGetString(GL_RENDERER));
		fprintf(stderr, "GL_VERSION: %s\n", glGetString(GL_VERSION));
		int interval;
		if (!SDL_GL_GetSwapInterval(&interval)) {
			fprintf(stderr, "%s\n", SDL_GetError());
		}
		fprintf(stderr, "MSAA buffers: %d, samples: %d, doublebuffer: %d, swap interval: %d\n",
			msaa_buffers, msaa_samples, doublebuffer, interval);
		SDL_Window *sdl_window = ((AU_SDL_Window *) window)->sdl_window;
		const SDL_DisplayMode *mode = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(sdl_window));
		if (mode) {
			fprintf(stderr, "Display: %dx%d @ %f Hz\n", mode->w, mode->h, mode->refresh_rate);
		} else {
			fprintf(stderr, "%s\n", SDL_GetError());
		}
	}

    // The charset for our small font includes the default ASCII characters, and anything in the
    // outfile name which will be displayed at the top of the window.
	u32 *small_charset = malloc((128 + st->outfile.shortened_path_len)*sizeof(u32));
	u32 small_charset_n = 0;
    for (i32 i=0x20; i<0x7f; i++) {
        small_charset[small_charset_n++] = i;
    }
    char *spath;
    size_t spath_len;
    // If we have an outfile, shorten its name for the outfile indicator convert it to utf-32, and
    // add its codepoints to the small charset.
	if (st->outfile.path) {
		int maxlen = 5 + (int) strlen(st->outfile.path) + 3 + 20 + 1;
		spath = (char *) malloc(maxlen);
		int n = snprintf(spath, maxlen, "out: %s @ %llu", st->outfile.path, st->outfile.offset);

		int str_n = MIN(maxlen-1, n);
		// Todo: expand based on window size
		int max_chars = 46;
		int remove = MAX(str_n - max_chars, 0);
		if (remove) {
			memmove(spath + 5, "...", 3);
			memmove(spath + 8, spath+8+remove, str_n-(8+remove)+1);
		}
		spath_len = str_n - remove;
		assertf(spath_len == strlen(spath), NULL);

		Vector4 start_color;
		bool success = read_color_from_outfile_and_maybe_update_offset(st, &start_color);
		if (success) {
			struct color_info ci;
			ci.rgb = start_color;
			ci.hsv = rgb_to_hsv(start_color);
			update_axis_values(st, st->mode, st->which_s, ci);
		} else {
			// since we failed to read, we probably shouldn't write
			fprintf(stderr, "[QUICKPICK WARNING] Failed to find a valid rrggbb(or #rrggbb) color at"
				" %s byte offset %llu, so not writing to the file.\n", st->outfile.path,
				st->outfile.offset);
			st->outfile.path = NULL;
		}
		u64 codepoints_len;
		// TODO: Windows uses UTF-16, not UTF-8, and may need special calls to retrieve unicode cli
		// args.
		u32 *codepoints = decode_utf8(&app_arena, spath, &codepoints_len);
		if (codepoints) {
		    for (i32 i=0; i<codepoints_len; i++) {
		    	u32 c = codepoints[i];
		    	if (c >= 0x20 && c <= 0x7f)
		    		continue;
		    	bool found = false;
		    	for (i32 j=0; j<i; j++) {
		    		if (codepoints[j] == c) {
		    			found = true;
		    			break;
		    		}
		    	}
		    	if (found)
		    		continue;
		    	small_charset[small_charset_n++] = c;
		    }
		    st->outfile.shortened_path_utf32 = codepoints;
		    assertf(codepoints_len < INT32_MAX, NULL);
		    st->outfile.shortened_path_len = (i32) codepoints_len;
		} else {
			// It can't be decoded, so we won't be able to display the filename
			// Todo: decode from locale encoding to unicode, or at least add better escapes
			st->outfile.shortened_path_utf32[0] = '?';
			st->outfile.shortened_path_utf32[1] = '?';
			st->outfile.shortened_path_utf32[2] = '?';
			st->outfile.shortened_path_len = 3;
		}
	}

	init_for_dpi(st, window->scale);

	double frames_start = au_os_time_s();
	u64 frames = 0;
	while (!window->input->quit)
	{
		st->key_pressed = 0;
		st->mouse_was_down = st->mouse_down;

		if (window->input->window_rescaled) {
			init_for_dpi(st, window->scale);
		}

		draw_ui_and_respond_input(st, window->input);

		commit_changes(window);

		if (st->debug) {
			frames++;
			double now = au_os_time_s();
			if (now >= frames_start + 2.0f) {
				printf("FPS: %.1f\n", frames / (now - frames_start));
				frames_start = now;
				frames = 0;
			}
		}
		if (st->animating) {
			// Go to the next frame whether there is input or not.
			update_input_state(window);
		} else {
			// Wait for an input event before going to the next frame.
			wait_and_update_input_state(window);
		}
    }

	return 0;
}
