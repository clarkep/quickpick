#include <string.h>
#include <math.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "stb_image_resize2.h"

#define NANOSVG_IMPLEMENTATION
#include "nanosvg.h"
#define AU_NANOSVGRAST_IMPLEMENTATION
#include "au_nanosvgrast.h"

#include <ft2build.h>
#include FT_FREETYPE_H
#include <glad/glad.h>

#include "au_core.h"
#include "au_containers.h"
#include "au_math.h"
#include "au_draw.h"
#include "au_string.h"

#define SCENE_MAX_TEXTURES 8

typedef struct bitmap {
	i32 texture_i;
	float tex_x;
	float tex_y;
	float w;
	float h;
	i32 cap_w;
	i32 cap_h;
	bool active;
	bool is_glyph;
	i32 bitmap_left;
	i32 bitmap_top;
	i32 advance_x;
	i32 advance_y;
} Bitmap;

typedef struct bitmap_dynarray {
	Bitmap *d;
	u64 length;
	u64 capacity;
	u64 item_size;
	Arena *arena;
} Bitmap_Dynarray;

static FT_Library g_ft_library;

static float signf(float x) {
	return (x >= 0.0f) ? 1.0f : -1.0f;
}

typedef struct atlas_glyph_info {
	float tex_x;
	float tex_y;
	float tex_w;
	float tex_h;
	i32 bitmap_left;
	i32 bitmap_top;
	i32 advance_x;
	i32 advance_y; // always 0?
} Atlas_Glyph_Info;

typedef struct glyph_key {
	u32 c;
	i32 size;
} Glyph_Key;

typedef struct font {
	FT_Face ft_face;
	// Glyph_Key -> Bitmap *
	Hash_Table glyphs;
} Font;

typedef struct texture_info {
	u32 id;
	void *data;
	i8 channels;
	i32 w;
	i32 h;
} Texture_Info;

// Data structure used for packing bitmaps. Represents an available spot on top of other bitmaps--
// on the "skyline" of the current bitmaps.
typedef struct packing_node {
	i32 x;
	i32 y;
	struct packing_node *next;
} Packing_Node;

typedef struct texture_pen {
	i32 texture_i;
	Packing_Node *start;
} Texture_Pen;

// typedef struct render_context_info {
// 	Window *window;
// 	Arena *arena;
// 	Float_Dynarray *vertices;
// 	u32 vertex_size;
// } Render_Context_Info;

typedef struct render_context {
	Window *window;
	Arena *arena;
	Float_Dynarray *vertices;
	u32 vertex_size;
	u64 gpu_vertex_buffer_size_bytes;
	struct texture_info textures[SCENE_MAX_TEXTURES];
	i32 textures_n;
	Texture_Pen cur_color_texture;
	Texture_Pen cur_grayscale_texture;
	// FT_Face is a FT_FaceRec *
	Font *fonts[SCENE_MAX_TEXTURES];
	i32 fonts_n;
	Bitmap_Dynarray *bitmaps;
	i32 viewport_w;
	i32 viewport_h;
	bool use_screen_coords;
	float y_scale;
	u32 vao;
	u32 vbo;
	u32 shader_program;
	i32 uYScale_location;
	i32 uTextures_location;
	i32 uTextureChannels_location;
	i32 uTextureSizes_location;
} Render_Context;

typedef struct render_context_dynarray {
	Render_Context **d;
	u64 length;
	u64 capacity;
	u64 item_size;
	Arena *arena;
} Render_Context_Dynarray;

typedef struct renderer_data {
	Render_Context_Dynarray *contexts;
	i32 active_context;
	i32 last_measured_font;
	i32 last_measured_size;
	u32 *last_measured_text;
} Renderer_Data;

const char* default_vertex_shader =
"#version 330 core\n"
"#define MAX_TEXTURES 8\n"
"layout (location = 0) in vec3 aPos;\n"
"layout (location = 1) in vec4 aColor;\n"
"layout (location = 2) in vec2 aTexCoord;\n"
"layout (location = 3) in float aTextureIndex;\n"
"out vec4 fColor;\n"
"out vec2 TexCoord;\n"
"flat out int iTextureIndex;\n"
"flat out int iTextureChannels;\n"
"uniform float uYScale;\n"
"uniform vec2 uTextureSizes[MAX_TEXTURES];\n"
"uniform int uTextureChannels[MAX_TEXTURES];\n"
"void main()\n"
"{\n"
"   gl_Position = vec4(aPos.x, aPos.y / uYScale, aPos.z, 1.0);\n"
"   fColor = aColor;\n"
"   int idx = int(round(aTextureIndex));\n"
"   if (idx < 0) {\n"
"        TexCoord = vec2(0.0, 0.0);\n"
"   } else {\n"
"       TexCoord = vec2(aTexCoord.x / uTextureSizes[idx].x, aTexCoord.y / uTextureSizes[idx].y);\n"
"   }\n"
"   iTextureIndex = idx;\n"
"   iTextureChannels = uTextureChannels[idx];\n"
"}";

const char* default_fragment_shader =
"#version 330 core\n"
"#define MAX_TEXTURES 8\n"
"out vec4 FragColor;\n"
"in vec4 fColor;\n"
"in vec2 TexCoord;\n"
"flat in int iTextureIndex;\n"
"flat in int iTextureChannels;\n"
"uniform sampler2D uTextures[MAX_TEXTURES];\n"
"uniform int uTextureChannels[MAX_TEXTURES];\n"
"void main()\n"
"{\n"
"    vec4 base = fColor;\n"
"    if (iTextureIndex >= 0) {\n"
"        int idx = iTextureIndex;\n"
"        float alpha = base.a;\n"
"        vec4 tex;\n"
"        if (idx == 0) tex = texture(uTextures[0], TexCoord);\n"
"        else if (idx == 1) tex = texture(uTextures[1], TexCoord);\n"
"        else if (idx == 2) tex = texture(uTextures[2], TexCoord);\n"
"        else if (idx == 3) tex = texture(uTextures[3], TexCoord);\n"
"        else if (idx == 4) tex = texture(uTextures[4], TexCoord);\n"
"        else if (idx == 5) tex = texture(uTextures[5], TexCoord);\n"
"        else if (idx == 6) tex = texture(uTextures[6], TexCoord);\n"
"        else if (idx == 7) tex = texture(uTextures[7], TexCoord);\n"
"        if (iTextureChannels == 1) base.a *= tex.r;\n"
"        else if (iTextureChannels == 4) { base *= tex; }\n"
"    }\n"
"    FragColor = base;\n"
"}";

static FT_Library g_ft_library;

u32 vec2hex(Vector4 vector)
{
	return ((u8) (vector.w * 255.0f) << 24)
		 + ((u8) (vector.x * 255.0f) << 16)
		 + ((u8) (vector.y * 255.0f) << 8)
		 + ((u8) (vector.z * 255.0f));
}

Vector4 hex2vec(u32 hex)
{
	return (Vector4) {
		((hex >> 16) & 0xFF) / 255.0f,
		((hex >> 8) & 0xFF) / 255.0f,
		(hex & 0xFF) / 255.0f,
		((hex >> 24) & 0xFF) / 255.0f,
	};
}

/********************************** Context and Scene setup ***************************************/

// XX error handling
static i32 compile_shader_program(const char *vertex_source, const char *fragment_source)
{
	unsigned int vertex_shader = glCreateShader(GL_VERTEX_SHADER);
	glShaderSource(vertex_shader, 1, &vertex_source, NULL);
	glCompileShader(vertex_shader);
	int  success;
	char info[512];
	glGetShaderiv(vertex_shader, GL_COMPILE_STATUS, &success);
	if (!success)
	{
		glGetShaderInfoLog(vertex_shader, 512, NULL, info);
		printf("Failed to compile vertex shader: %s", info);
		return -1;
	}

	unsigned int fragment_shader = glCreateShader(GL_FRAGMENT_SHADER);
	glShaderSource(fragment_shader, 1, &fragment_source, NULL);
	glCompileShader(fragment_shader);
	glGetShaderiv(fragment_shader, GL_COMPILE_STATUS, &success);
	if (!success)
	{
		glGetShaderInfoLog(fragment_shader, 512, NULL, info);
		printf("Failed to compile fragment shader: %s", info);
		return -1;
	}

	unsigned int shader_program = glCreateProgram();
	glAttachShader(shader_program, vertex_shader);
	glAttachShader(shader_program, fragment_shader);
	glLinkProgram(shader_program);
	glGetProgramiv(shader_program, GL_LINK_STATUS, &success);
	if (!success) {
		glGetProgramInfoLog(shader_program, 512, NULL, info);
		printf("Failed to link shader program: %s", info);
		return -1;
	}
	return shader_program;
}

static Render_Context *render_context_create(Arena *arena, Window *window, const char *vertex_shader,
	const char *fragment_shader, i32 vertex_size, bool use_screen_coords)
{
	Render_Context *context = aalloc(arena, sizeof(Render_Context));
	context->arena = arena;
	context->window = window;
	// TODO
	context->vertices = (Float_Dynarray *) dynarray_create(arena, sizeof(float));
	dynarray_expand_to(context->vertices, 6*vertex_size);
	context->vertex_size = vertex_size;
//    context->vertices = aalloc(arena, (u64) max_vertices*vertex_size*sizeof(float));
//    memset(context->vertices, 0, (u64) max_vertices*vertex_size*sizeof(float));
	if (vertex_size < 10) {
		fprintf(stderr, "vertex_size must be at least 10 (pos3+color4+tex2+font1)\n");
		goto fail;
	}
	context->textures_n = 0;
	context->fonts_n = 0;
	for (int i=0; i<SCENE_MAX_TEXTURES; i++) {
		context->textures[i].id = 0;
		context->textures[i].data = NULL;
		context->fonts[i] = NULL;
	}
	context->cur_color_texture.texture_i = -1;
	context->cur_grayscale_texture.texture_i = -1;
	context->bitmaps = (Bitmap_Dynarray *) dynarray_create(arena, sizeof(Bitmap));

	if (!vertex_shader) vertex_shader = default_vertex_shader;
	if (!fragment_shader) fragment_shader = default_fragment_shader;
	context->shader_program = compile_shader_program(vertex_shader, fragment_shader);
	if (context->shader_program < 0) {
		goto fail;
	}
	context->uYScale_location = glGetUniformLocation(context->shader_program, "uYScale");
	context->uTextures_location = glGetUniformLocation(context->shader_program, "uTextures");
	context->uTextureChannels_location = glGetUniformLocation(context->shader_program, "uTextureChannels");
	context->uTextureSizes_location = glGetUniformLocation(context->shader_program, "uTextureSizes");

	glGenVertexArrays(1, &context->vao);
	glBindVertexArray(context->vao);
	glGenBuffers(1, &context->vbo);
	glBindBuffer(GL_ARRAY_BUFFER, context->vbo);
	context->gpu_vertex_buffer_size_bytes = 10000*vertex_size*sizeof(float);
	glBufferData(GL_ARRAY_BUFFER, context->gpu_vertex_buffer_size_bytes, NULL, GL_DYNAMIC_DRAW);
	// position
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, vertex_size*sizeof(float), (void *) 0);
	glEnableVertexAttribArray(0);
	// color
	glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, vertex_size*sizeof(float), (void *) (3*sizeof(float)));
	glEnableVertexAttribArray(1);
	// texture coordinates
	glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, vertex_size*sizeof(float), (void *) (7*sizeof(float)));
	glEnableVertexAttribArray(2);
	// font index
	glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, vertex_size*sizeof(float), (void *) (9*sizeof(float)));
	glEnableVertexAttribArray(3);
	// If the caller wants any more vertex attributes, they have to set them up themselves as above.

	i32 viewport[4];
	glGetIntegerv(GL_VIEWPORT, viewport);
	context->viewport_w = viewport[2];
	context->viewport_h = viewport[3];
	context->y_scale = (float) context->viewport_h / context->viewport_w;

	context->use_screen_coords = use_screen_coords;

	return context;
	fail:
	if (context) {
		free(context->vertices);
		free(context);
	}
	return NULL;
}

// Todo render_context_delete
static void ensure_renderer_data(Window* win);

i32 window_create_render_context(Window *win, const char *vertex_shader, const char *fragment_shader,
	i32 vertex_size, bool use_screen_coords)
{
	ensure_renderer_data(win);
	Renderer_Data *r = (Renderer_Data *) win->renderer_data;
	// Prints error messages to stderr.
	Render_Context *context = render_context_create(win->arena, win, vertex_shader, fragment_shader,
		vertex_size, use_screen_coords);
	if (context) {
		dynarray_add(r->contexts, &context);
		return (i32) r->contexts->length - 1;
	} else {
		return -1;
	}
}

// Todo: unload

static void reset_render_context(Render_Context *context)
{
	i32 viewport[4];
	glGetIntegerv(GL_VIEWPORT, viewport);
	context->viewport_w = viewport[2];
	context->viewport_h = viewport[3];
	context->y_scale = (float) context->viewport_h / context->viewport_w;

	context->vertices->length = 0;
}

static void flush_render_context(Render_Context *context)
{
	if (context->vertices->length == 0) return;
	glUseProgram(context->shader_program);
	glUniform1f(context->uYScale_location, context->y_scale);
	if (context->uTextures_location >= 0) {
		GLint units[SCENE_MAX_TEXTURES];
		GLint channels[SCENE_MAX_TEXTURES];
		GLfloat sizes[SCENE_MAX_TEXTURES*2];
		for (int i=0; i<context->textures_n; i++) {
			units[i] = i;
			channels[i] = context->textures[i].channels;
			sizes[i*2] = context->textures[i].w;
			sizes[i*2 + 1] = context->textures[i].h;
		}
		glUniform1iv(context->uTextures_location, context->textures_n, units);
		glUniform1iv(context->uTextureChannels_location, context->textures_n, channels);
		glUniform2fv(context->uTextureSizes_location, context->textures_n, sizes);
	}
	for (int i=0; i<context->textures_n; i++) {
		glActiveTexture(GL_TEXTURE0 + i);
		glBindTexture(GL_TEXTURE_2D, context->textures[i].id);
	}
	glBindVertexArray(context->vao);
	glBindBuffer(GL_ARRAY_BUFFER, context->vbo);
	while (context->gpu_vertex_buffer_size_bytes < context->vertices->length * sizeof(float)) {
		context->gpu_vertex_buffer_size_bytes *= 2;
		if (context->gpu_vertex_buffer_size_bytes >= context->vertices->length * sizeof(float))
			glBufferData(GL_ARRAY_BUFFER, context->gpu_vertex_buffer_size_bytes, NULL, GL_DYNAMIC_DRAW);
	}
	// orphan
	glBufferData(GL_ARRAY_BUFFER, context->gpu_vertex_buffer_size_bytes, NULL, GL_STREAM_DRAW);
	glBufferSubData(GL_ARRAY_BUFFER, 0, (u64) context->vertices->length * sizeof(float),
		context->vertices->d);
	glDrawArrays(GL_TRIANGLES, 0, context->vertices->length / context->vertex_size);
	context->vertices->length = 0;
}

static void flush_active_context(Renderer_Data *rdata)
{
	if (rdata->active_context >= 0 && rdata->contexts->d[rdata->active_context]) {
		flush_render_context(rdata->contexts->d[rdata->active_context]);
	}
}

static void renderer_callback(Window *window)
{
	ensure_renderer_data(window);
	Renderer_Data *rdata = (Renderer_Data *) window->renderer_data;
	if (rdata->active_context >= 0)
		flush_active_context(rdata);
	rdata->active_context = -1;

	i32 viewport[4];
	glGetIntegerv(GL_VIEWPORT, viewport);
	// Todo: viewport_w and some other data should be per-window.
	for (i64 i=0; i<rdata->contexts->length; i++) {
		Render_Context *context = rdata->contexts->d[i];
		if (context) {
			context->viewport_w = viewport[2];
			context->viewport_h = viewport[3];
			context->y_scale = (float) context->viewport_h / context->viewport_w;
		}
	}

}

static void ensure_renderer_data(Window *win) {
	if (!win->renderer_data) {
		Renderer_Data *r = aalloc(win->arena, sizeof(Renderer_Data));
		r->contexts = (Render_Context_Dynarray *) dynarray_create(win->arena, sizeof(Render_Context *));
		r->active_context = -1;
		Render_Context *null = NULL;
		dynarray_add(r->contexts, &null);
		win->renderer_data = r;
	}
	if (!win->renderer_callback) {
		win->renderer_callback = renderer_callback;
	}
}

// Get the render context associated with this scene, and make it the active context. If there is a
// current active context, flush it. If the context is 0, create it if necessary.
Render_Context *scene_get_and_activate_context(Scene *scene)
{
	Window *win = scene->window;
	ensure_renderer_data(win);
	Renderer_Data *rdata = (Renderer_Data *) win->renderer_data;
	Render_Context *context;
	if (scene->context == 0 && !rdata->contexts->d[0]) {
		context = render_context_create(win->arena, win, default_vertex_shader,
			default_fragment_shader, 10, true);
		rdata->contexts->d[0] = context;
	} else {
		// scene_create_child_with_context checks that the context exists, but a caller could create
		// their own invalid scene or a context could be deleted.
		assertf(scene->context >= 0 && scene->context < rdata->contexts->length
			&& rdata->contexts->d[scene->context], NULL);
		context = rdata->contexts->d[scene->context];
	}
	if (rdata->active_context >= 0 && rdata->active_context != scene->context) {
		flush_active_context(rdata);
	}
	rdata->active_context = scene->context;
	return context;
}

static Render_Context *scene_get_context(Scene *scene)
{
	Window *win = scene->window;
	ensure_renderer_data(win);
	Renderer_Data *rdata = (Renderer_Data *) win->renderer_data;
	Render_Context *context;
	if (scene->context == 0 && !rdata->contexts->d[0]) {
		context = render_context_create(win->arena, win, default_vertex_shader,
			default_fragment_shader, 10, true);
		rdata->contexts->d[0] = context;
	} else {
		context = rdata->contexts->d[scene->context];
	}
	return context;
}

Scene *scene_create_child_with_context(Scene *parent, Arena *arena, float x, float y, float w, float h,
	i32 context)
{
	Window *win = parent->window;
	// Todo: Instead of constantly calling this, we should set up renderer_data in window_create.
	ensure_renderer_data(win);

	Renderer_Data *rdata = parent->window->renderer_data;
	Render_Context_Dynarray *contexts = rdata->contexts;
	bool valid_context = context == 0
	                     || (context > 0 && context < contexts->length && contexts->d[context]);
	if (!valid_context) {
		return NULL;
	}
	Scene *scene = aalloc(arena, sizeof(Scene));
	*scene = (Scene) { AU_SCENE_TYPE_SCENE, w, h, x, y, context, parent, parent->window };
	return scene;
}

bool scene_init_child_with_context(Scene *scene, Scene *parent, float x, float y, float w, float h,
	i32 context)
{
	Window *win = parent->window;
	// Todo: Instead of constantly calling this, we should set up renderer_data in window_create.
	ensure_renderer_data(win);

	Renderer_Data *rdata = parent->window->renderer_data;
	Render_Context_Dynarray *contexts = rdata->contexts;
	bool valid_context = context == 0
	                     || (context > 0 && context < contexts->length && contexts->d[context]);
	if (!valid_context) {
		return false;
	}
	*scene = (Scene) { AU_SCENE_TYPE_SCENE, w, h, x, y, context, parent, parent->window };
	return true;
}

Float_Dynarray *context_get_vertices(Render_Context *context)
{
	return context->vertices;
}

u32 context_get_vertex_size(Render_Context *context)
{
	return context->vertex_size;
}

/********************************** Font and image loading ****************************************/

static int ensure_freetype_initialized(void)
{
	if (g_ft_library) return 0;
	FT_Error error = FT_Init_FreeType(&g_ft_library);
	if (error) {
		fprintf(stderr, "Failed to init freetype.\n");
		return -1;
	}
	return 0;
}

static i32 create_and_add_opengl_texture(Render_Context *context, i32 w, i32 h, i32 channels, void *data)
{
	i32 i = context->textures_n++;
	assertf(i < SCENE_MAX_TEXTURES, NULL);
	u32 tex;
	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D, tex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	GLenum format;
	GLenum type;
	switch (channels) {
	case 1:
		format = GL_RED;
		type = GL_UNSIGNED_BYTE;
	break;
	case 4:
		format = GL_RGBA;
		type = GL_UNSIGNED_BYTE;
	break;
	default:
		errexit("Programmer error, have to stop: invalid # of texture channels.\n");
	break;
	}
	glTexImage2D(GL_TEXTURE_2D, 0, format, w, h, 0, format, type, data);
	context->textures[i].id = tex;
	context->textures[i].channels = channels;
	context->textures[i].data = data;
	context->textures[i].w = w;
	context->textures[i].h = h;
	return i;
}

static void create_new_opengl_texture_and_set_pen(Render_Context *context, i32 w, i32 h, i32 channels) {
	u64 size = (u64) w * (u64) h * channels;
	void *new_texture_data = adalloc(context->arena, size);
	memset(new_texture_data, 0, size);
	Texture_Pen *pen;
	if (channels == 4) {
		context->cur_color_texture.texture_i = create_and_add_opengl_texture(context, w, h,
			channels, new_texture_data);
		pen = &context->cur_color_texture;
	} else {
		assertf(channels == 1, NULL);
		context->cur_grayscale_texture.texture_i = create_and_add_opengl_texture(context, w, h,
			channels, new_texture_data);
		pen = &context->cur_grayscale_texture;
	}
	Packing_Node *cur = pen->start;
	while (cur) {
		Packing_Node *next = cur->next;
		afree(context->arena, cur);
		cur = next;
	}
	pen->start = adalloc(context->arena, sizeof(Packing_Node));
	pen->start->x = 0;
	pen->start->y = 0;
	pen->start->next = NULL;
}

static bool resize_opengl_texture(Render_Context *context, i32 i, i32 new_w, i32 new_h, i32 channels)
{
	i32 old_w = context->textures[i].w;
	i32 old_h = context->textures[i].h;
	u8 *old_data = (u8 *) context->textures[i].data;
	u64 new_size = (u64) new_w * new_h * channels;
	u8 *new_data = (u8 *) adalloc(context->arena, new_size);
	memset(new_data, 0, new_size);
	if (!new_data) return false;

	// Copy existing rows with proper stride
	i32 copy_w = old_w < new_w ? old_w : new_w;
	i32 copy_h = old_h < new_h ? old_h : new_h;
	for (i32 row = 0; row < copy_h; row++) {
		memcpy(new_data + (u64) row * new_w * channels,
			   old_data + (u64) row * old_w * channels,
			   (u64) copy_w * channels);
	}

	afree(context->arena, old_data);
	context->textures[i].data = new_data;
	context->textures[i].w = new_w;
	context->textures[i].h = new_h;
	u32 tex = context->textures[i].id;
	glBindTexture(GL_TEXTURE_2D, tex);
	GLenum format;
	GLenum type;
	switch (channels) {
	case 1:
		format = GL_RED;
		type = GL_UNSIGNED_BYTE;
	break;
	case 4:
		format = GL_RGBA;
		type = GL_UNSIGNED_BYTE;
	break;
	default:
		errexit("Programmer error, have to stop: invalid # of texture channels.\n");
	break;
	}
	glTexImage2D(GL_TEXTURE_2D, 0, format, new_w, new_h, 0, format, type, new_data);
	return true;
}

// img_w, img_h: size of image with padding included, i.e. resulting size in texture
void copy_image_data_to_texture(Texture_Info *texture, i32 tex_x, i32 tex_y, const void *data,
	i32 channels, i32 img_w, i32 img_h, i32 pix_per_row, i32 padding)
{
	i32 tex_w = texture->w;
	i32 tex_h = texture->h;
	assertf(tex_x + img_w <= tex_w && tex_y + img_h <= tex_h, NULL);
	if (channels == 4) {
		u32 *img_data = (u32 *) data;
		u32 *tex_data = (u32 *) texture->data;
		for (i32 r=0; r<img_h-padding; r++) {
			i32 tex_row = tex_y + (img_h-padding) - r - 1;
			for (i32 c=0; c<img_w-padding; c++) {
				i32 tex_col = tex_x + c;
				tex_data[tex_row*tex_w + tex_col] = img_data[r*pix_per_row + c];
			}
		}
		glBindTexture(GL_TEXTURE_2D, texture->id);
		glPixelStorei(GL_UNPACK_ROW_LENGTH, tex_w);
		glTexSubImage2D(GL_TEXTURE_2D, 0, tex_x, tex_y, img_w, img_h, GL_RGBA, GL_UNSIGNED_BYTE,
			&tex_data[(tex_y * tex_w) + tex_x]);
		glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
	} else {
		u8 *img_data = (u8 *) data;
		u8 *tex_data = (u8 *) texture->data;
		for (i32 r=0; r<img_h-padding; r++) {
			i32 tex_row = tex_y + (img_h-padding) - r - 1;
			for (i32 c=0; c<img_w-1; c++) {
				i32 tex_col = tex_x + c;
				tex_data[tex_row*tex_w + tex_col] = img_data[r*pix_per_row + c];
			}
		}
		glBindTexture(GL_TEXTURE_2D, texture->id);
		glPixelStorei(GL_UNPACK_ROW_LENGTH, tex_w);
		glTexSubImage2D(GL_TEXTURE_2D, 0, tex_x, tex_y, img_w, img_h, GL_RED, GL_UNSIGNED_BYTE,
			&tex_data[(tex_y * tex_w) + tex_x]);
		glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
	}
}

static Packing_Node *place_bitmap_at_node(Arena *arena, Packing_Node *node, Packing_Node *previous,
	i32 max_texture_size, i32 w, i32 h)
{
	// The new node we get from placing the bitmap; it is located on top of 'node'.
	Packing_Node *new_node = adalloc(arena, sizeof(Packing_Node));
	new_node->x = node->x;
	new_node->y = node->y + h;
	// It seems wrong to have new_node->next point to the node it is placed on top of, but see below: it
	// might be split and survive as the uncovered portion.
	new_node->next = node;
	if (previous)
		previous->next = new_node;

	i32 bitmap_end_x = node->x + w;
	Packing_Node *cur = node;
	while (cur) {
		i32 cur_end_x = cur->next ? cur->next->x : max_texture_size;
		if (bitmap_end_x < cur_end_x) {
			// We have found the end of the bitmap, over 'cur'. Keep only the uncovered portion.
			cur->x = bitmap_end_x;
			return new_node;
		} else {
			// Delete cur; it is entirely covered by new_node.
			new_node->next = cur->next;
			afree(arena, cur);
			cur = new_node->next;
		}
	}
	return new_node;
}

static bool bitmap_fits_at_node(Packing_Node *node, i32 max_texture_size, i32 w, i32 h)
{
	if (max_texture_size - node->y < h)
		return false;

	// Either the next node, or the wall if null.
	Packing_Node *next = node->next;
	while (true) {
		i32 next_x = next ? next->x : max_texture_size;
		if (next_x - node->x >= w) {
			return true;
		} else if (next && next->y <= node->y) {
			// If an adjacent node is *lower*, it can be included in this slot...
			next = next->next;
		} else {
			// but anything higher or the wall ends the search.
			return false;
		}
	}
}

// Returns a packing node 'result'. If result.next is NULL, a location could not be found. Otherwise,
// result.next points to the new start node.
static Packing_Node find_node_for_bitmap(Arena *arena, Packing_Node *start, i32 max_texture_size,
	i32 w, i32 h)
{
	i32 min_y = max_texture_size;
	Packing_Node *min_y_node = NULL;
	Packing_Node *min_y_prev = NULL;
	Packing_Node *cur = start;
	Packing_Node *prev = NULL;
	while (cur) {
		if (cur->y < min_y && bitmap_fits_at_node(cur, max_texture_size, w, h)) {
			min_y = cur->y;
			min_y_node = cur;
			min_y_prev = prev;
		}
		prev = cur;
		cur = cur->next;
	}
	if (min_y_node) {
		Packing_Node result = { min_y_node->x, min_y_node->y, start };
		Packing_Node *new_node = place_bitmap_at_node(arena, min_y_node, min_y_prev, max_texture_size,
			w, h);
		if (min_y_node == start) {
			result.next = new_node;
		}
		return result;
	} else {
		return (Packing_Node) { 0, 0, NULL };
	}
}

static i32 load_bitmap_internal(Render_Context *context, const void *data, i32 width, i32 height,
	i32 bytes_per_row, i32 channels, Bitmap *b)
{
	// 1px of padding to avoid blending adjacent bitmaps
	i32 img_w = width + 1;
	i32 img_h = height + 1;
	i32 pix_per_row = bytes_per_row / channels;
	// Ensure image is 4 byte aligned.
	if (channels == 4 && ((u64) data & 3))
		return -1;

	// First, look for an inactive bitmap slot that can fit our image.
	for (u64 i=0; i<context->bitmaps->length; i++) {
		Bitmap *bit = &context->bitmaps->d[i];
		Texture_Info *tex = &context->textures[bit->texture_i];
		if (!bit->active && tex->channels == channels && bit->cap_w >= img_w - 1 && bit->cap_h >= img_h - 1) {
			copy_image_data_to_texture(tex, bit->tex_x, bit->tex_y, data, channels, img_w, img_h,
				pix_per_row, 1);
			bit->w = img_w-1;
			bit->h = img_h-1;
			bit->active = true;
			// Copy in optional glyph info set by caller.
			bit->is_glyph = b->is_glyph;
			bit->bitmap_left = b->bitmap_left, bit->bitmap_top = b->bitmap_top;
			bit->advance_x = b->advance_x, bit->advance_y = b->advance_y;
			return i;
		}
	}

	// If not found, expand the current texture of the correct channel count.
	i32 max_texture_size; // 1d 2d textures
	glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_texture_size);
	if (img_w > max_texture_size || img_h > max_texture_size)
		return -1;
	Texture_Pen *tex_pen;
	if (channels == 1) {
		tex_pen = &context->cur_grayscale_texture;
	} else if (channels == 4) {
		tex_pen = &context->cur_color_texture;
	} else {
		return -1;
	}
	if (tex_pen->texture_i < 0) {
		create_new_opengl_texture_and_set_pen(context, img_w, img_h, channels);
	}
	Packing_Node result = find_node_for_bitmap(context->arena, tex_pen->start, max_texture_size,
		img_w, img_h);
	if (!result.next) {
		create_new_opengl_texture_and_set_pen(context, img_w, img_h, channels);
		result = find_node_for_bitmap(context->arena, tex_pen->start, max_texture_size, img_w, img_h);
		if (!result.next) {
			return -1;
		}
	}
	tex_pen->start = result.next;
	Texture_Info *texture = &context->textures[tex_pen->texture_i];
	i32 i = tex_pen->texture_i;
	if (result.x + img_w > texture->w || result.y + img_h > texture->h) {
		// Resize by powers of two.
		u64 new_w = au_round_up_to_pow2(MAX(result.x + img_w, texture->w));
		u64 new_h = au_round_up_to_pow2(MAX(result.y + img_h, texture->h));
		new_w = MIN(new_w, max_texture_size);
		new_h = MIN(new_h, max_texture_size);
		if (!resize_opengl_texture(context, i, new_w, new_h, channels)) {
			// xx leaves the node reserved.
			return -1;
		}
	}
	copy_image_data_to_texture(texture, result.x, result.y, data, channels, img_w, img_h, pix_per_row, 1);

	Bitmap_Dynarray *bitmaps = (Bitmap_Dynarray *) context->bitmaps;
	i32 res = bitmaps->length;
	b->texture_i = i;
	b->tex_x = result.x;
	b->tex_y = result.y;
	b->w = img_w - 1;
	b->h = img_h - 1;
	b->cap_w = b->w;
	b->cap_h = b->h;
	b->active = true;
	dynarray_add(bitmaps, b);
	return res;
}

i32 load_font(Scene *scene, const char *font_file)
{
	Render_Context *context = scene_get_context(scene);
	if (context->textures_n >= SCENE_MAX_TEXTURES) {
		fprintf(stderr, "Max textures (%d) reached\n", SCENE_MAX_TEXTURES);
		return -1;
	}
	if (ensure_freetype_initialized() != 0) {
		return -1;
	}
	FT_Face face;
	FT_Error error = FT_New_Face(g_ft_library, font_file, 0, &face);
	if (error) {
		fprintf(stderr, "Failed to create font face for %s.\n", font_file);
		return -1;
	}

	Font *font = aalloc(context->arena, sizeof(Font));
	font->ft_face = face;
	hash_table_init(&font->glyphs, context->arena, 1000, true, false);

	int ret = context->fonts_n;
	context->fonts[context->fonts_n++] = font;
	return ret;
}

i32 load_font_from_memory(Scene *scene, const void *font_data, u64 data_size)
{
	Render_Context *context = scene_get_context(scene);
	if (context->textures_n >= SCENE_MAX_TEXTURES) {
		fprintf(stderr, "Max textures (%d) reached\n", SCENE_MAX_TEXTURES);
		return -1;
	}
	if (ensure_freetype_initialized() != 0) {
		return -1;
	}
	FT_Open_Args open_args = { FT_OPEN_MEMORY, font_data, data_size, NULL, NULL, NULL, 0, NULL };
	FT_Face face;
	FT_Error error = FT_Open_Face(g_ft_library, &open_args, 0, &face);
	if (error) {
		fprintf(stderr, "Failed to create font face(from memory).\n");
		return -1;
	}

	Font *font = aalloc(context->arena, sizeof(Font));
	font->ft_face = face;
	hash_table_init(&font->glyphs, context->arena, 1000, true, false);

	int ret = context->fonts_n;
	context->fonts[context->fonts_n++] = font;
	return ret;
}

Font_Metrics au_font_get_metrics(Scene *scene, i32 font_i)
{
	Render_Context *context = scene_get_context(scene);
	FT_Face font = context->fonts[font_i]->ft_face;
	return (Font_Metrics) { (float) font->ascender / (float) font->units_per_EM,
	                        (float) font->descender / (float) font->units_per_EM };
}

static u8 *read_whole_file(Arena *arena, const char *path, u64 *data_size)
{
	FILE *in = fopen(path, "rb");
	assertf(in, "could not open %s.\n", path);
	fseek(in, 0, SEEK_END);
	*data_size = ftell(in);
	fseek(in, 0, SEEK_SET);
	u8 *in_data = (u8 *) aalloc(arena, *data_size);

	fread(in_data, 1, *data_size, in);
	return in_data;
}

i32 load_image(Scene *scene, const char *path, const char *type)
{
	u64 in_size;
	u8 *in_data = read_whole_file(NULL, path, &in_size);

	i32 res = load_image_from_memory(scene, in_data, in_size, type);
	free(in_data);
	return res;
}

i32 load_image_at_size(Scene *scene, const char *path, const char *type, i32 w, i32 h)
{
	u64 in_size;
	u8 *in_data = read_whole_file(NULL, path, &in_size);

	i32 res = load_image_at_size_from_memory(scene, in_data, in_size, type, w, h);
	free(in_data);
	return res;
}

i32 load_image_from_memory(Scene *scene, const void *data, u64 data_size, const char *type)
{
	Render_Context *context = scene_get_context(scene);
	// TODO: make this check whether the last texture is *full*, not whether it exists.
	if (context->textures_n >= SCENE_MAX_TEXTURES) {
		fprintf(stderr, "Max textures (%d) reached\n", SCENE_MAX_TEXTURES);
		return -1;
	}

	Arena *arena = scene->window->arena;
	i32 img_w;
	i32 img_h;
	u32 *img_data;
	i32 res;
	if (strcmp(type, "png") == 0 || strcmp(type, "jpg") == 0) {
		i32 channels_in_file = 0;
		// TODO: make stbi use adalloc
		img_data = (u32 *) stbi_load_from_memory(data, data_size, &img_w, &img_h,
			&channels_in_file, 4);
		res = load_bitmap(scene, img_data, img_w, img_h, img_w*4, 4);
		free(img_data);
	} else if (strcmp(type, "svg")==0) {
		u8* svg_copy = adalloc(arena, data_size + 1);
		memcpy(svg_copy, data, data_size);
		svg_copy[data_size] = 0;
		NSVGimage* image = nsvgParse((char *) svg_copy, "px", 96);
		img_w = image->width;
		img_h = image->height;

		struct ANSVGrasterizer* rast = ansvgCreateRasterizer();
		img_data = adalloc(arena, img_w*img_h*4);
		ansvgRasterize(rast, image, 0,0,1, (u8 *) img_data, img_w, img_h, img_w*4, ANSVG_RENDER_BGRA);
		nsvgDelete(image);
		ansvgDeleteRasterizer(rast);
		afree(arena, svg_copy);
		res = load_bitmap(scene, img_data, img_w, img_h, img_w*4, 4);
		afree(arena, img_data);
	} else if (strcmp(type, "svg_alpha")==0) {
		u8* svg_copy = adalloc(arena, data_size + 1);
		memcpy(svg_copy, data, data_size);
		svg_copy[data_size] = 0;
		NSVGimage* image = nsvgParse((char *) svg_copy, "px", 96);
		img_w = image->width;
		img_h = image->height;

		struct ANSVGrasterizer* rast = ansvgCreateRasterizer();
		img_data = adalloc(arena, img_w*img_h*4);
		ansvgRasterize(rast, image, 0,0,1, (u8 *) img_data, img_w, img_h, img_w, ANSVG_RENDER_ALPHA);
		nsvgDelete(image);
		ansvgDeleteRasterizer(rast);
		afree(arena, svg_copy);
		res = load_bitmap(scene, img_data, img_w, img_h, img_w, 1);
		afree(arena, img_data);
	} else {
		return -1;
	}

	return res;
}

i32 load_image_at_size_from_memory(Scene *scene, const void *data, u64 data_size,
	const char *type, i32 w, i32 h)
{
	Render_Context *context = scene_get_context(scene);
	// TODO: make this check whether the last texture is *full*, not whether it exists.
	if (context->textures_n >= SCENE_MAX_TEXTURES) {
		fprintf(stderr, "Max textures (%d) reached\n", SCENE_MAX_TEXTURES);
		return -1;
	}

	Arena *arena = scene->window->arena;
	i32 img_w;
	i32 img_h;
	u32 *img_data;
	i32 res;
	if (strcmp(type, "png") == 0 || strcmp(type, "jpg") == 0) {
		i32 channels_in_file = 0;
		u32 *orig_img_data = (u32 *) stbi_load_from_memory(data, data_size, &img_w, &img_h,
			&channels_in_file, 4);
		img_data = adalloc(arena, w*h*4);

		STBIR_RESIZE resize_struct;
		stbir_resize_init(&resize_struct, orig_img_data, img_w, img_h, img_w*4, img_data, w, h, w*4,
			STBIR_ARGB, STBIR_TYPE_UINT8);
		stbir_set_pixel_layouts(&resize_struct, STBIR_RGBA_PM, STBIR_BGRA_PM);
		stbir_resize_extended(&resize_struct);
		free(orig_img_data);
		res = load_bitmap(scene, img_data, w, h, w*4, 4);
	} else if (strcmp(type, "svg")==0) {
		u8* svg_copy = adalloc(arena, data_size + 1);
		memcpy(svg_copy, data, data_size);
		svg_copy[data_size] = 0;
		NSVGimage* image = nsvgParse((char *) svg_copy, "px", 96);
		float scale = w / (float) image->width;
		img_w = w;
		img_h = image->height * scale;

		struct ANSVGrasterizer* rast = ansvgCreateRasterizer();
		img_data = adalloc(arena, img_w*img_h*4);
		ansvgRasterize(rast, image, 0, 0, scale, (u8 *) img_data, img_w, img_h, img_w*4,
			ANSVG_RENDER_BGRA);
		nsvgDelete(image);
		ansvgDeleteRasterizer(rast);
		afree(arena, svg_copy);
		res = load_bitmap(scene, img_data, w, h, w*4, 4);
	} else if (strcmp(type, "svg_alpha")==0) {
		u8* svg_copy = adalloc(arena, data_size + 1);
		memcpy(svg_copy, data, data_size);
		svg_copy[data_size] = 0;
		NSVGimage* image = nsvgParse((char *) svg_copy, "px", 96);
		float scale = w / (float) image->width;
		img_w = w;
		img_h = image->height * scale;

		struct ANSVGrasterizer* rast = ansvgCreateRasterizer();
		img_data = adalloc(arena, img_w*img_h);
		ansvgRasterize(rast, image, 0, 0, scale, (u8 *) img_data, img_w, img_h, img_w,
			ANSVG_RENDER_ALPHA);
		nsvgDelete(image);
		ansvgDeleteRasterizer(rast);
		afree(arena, svg_copy);
		res = load_bitmap(scene, img_data, w, h, w, 1);
	} else {
		return -1;
	}

	afree(arena, img_data);
	return res;
}

i32 load_bitmap(Scene *scene, const void *data, i32 width, i32 height, i32 bytes_per_row,
	i8 channels)
{
	Render_Context *context = scene_get_context(scene);
	Bitmap b = { 0 };
	b.is_glyph = false;
	return load_bitmap_internal(context, data, width, height, bytes_per_row, channels, &b);
}

void unload_image(Scene *scene, i32 image_i)
{
	Render_Context *context = scene_get_context(scene);
	assertf(image_i >= 0 && image_i < context->bitmaps->length, NULL);
	context->bitmaps->d[image_i].active = false;
}

/********************************** Internal geometry generation **********************************/

static i32 generate_rectangle(float *data, i32 stride, float x, float y, float w, float h, Vector4 color)
{
	data[0*stride + 0] = x;
	data[0*stride + 1] = y;
	data[1*stride + 0] = x;
	data[1*stride + 1] = y + h;
	data[2*stride + 0] = x + w;
	data[2*stride + 1] = y + h;
	data[3*stride + 0] = x + w;
	data[3*stride + 1] = y;
	return 4;
}

static i32 generate_quad(float *data, i32 stride, Vector2 *corners)
{
	for (i32 i=0; i<4; i++) {
		data[i*stride + 0] = corners[i].x;
		data[i*stride + 1] = corners[i].y;
	}
	return 4;
}

static i32 generate_circle(float *data, i32 stride, float x, float y, float r, i32 segments)
{
	// generate_circle is separate from generate_circle_arc because a circle is a connected shape,
	// so we don't generate the last vertex at stop_angle==2PI.
	for (i32 i=0; i<segments; i++) {
		float angle = 2*M_PI*i/segments;
		data[i*stride] = x + r*cosf(angle);
		data[i*stride+1] = y + r*sinf(angle);
	}
	return segments;
}

static i32 generate_circle_arc(float *data, i32 stride, float x, float y, float r, float start_angle,
	float stop_angle, i32 segments)
{
	float d_angle = stop_angle - start_angle;
	for (i32 i=0; i<=segments; i++) {
		float angle = start_angle + d_angle*i/(segments);
		data[i*stride] = x + r*cosf(angle);
		data[i*stride+1] = y + r*sinf(angle);
	}
	return segments+1;
}

// (x/a)^n + (y/b)^n = 1.0
static i32 generate_superellipse(float *data, i32 stride, float x, float y, float a, float b, float n,
	i32 segments)
{
	for (i32 i = 0; i < segments; i++) {
		float angle = 2.0f * M_PI * i / segments;
		float cx = cosf(angle);
		float cy = sinf(angle);
		data[i*stride] = x + a * signf(cx) * powf(fabsf(cx), 2.0f / n);
		data[i*stride + 1] = y + b * signf(cy) * powf(fabsf(cy), 2.0f / n);
	}
	return segments;
}

static double signed_area(Vector2 *p, i32 n) {
	double sum = 0.0;
	for (int i = 0; i < n; ++i) {
		int j = (i + 1) % n;
		sum += p[i].x * p[j].y - p[j].x * p[i].y;
	}
	return 0.5 * sum;
}

static i32 generate_rounded_quad(float *data, i32 stride, Vector2 *corners, bool *rounded, float radius,
	i32 segments_per_corner)
{
	i32 n = 0;
	i32 start_i = 0;
	i32 stop_i = 4;
	i32 dir = 1;
	// "shoelace formula" to compute signed area and therefore winding direction; if negative, corners
	// wind clockwise, and so we traverse them in reverse order.
	if (signed_area(corners, 4) < 0) {
		start_i = 3;
		stop_i = -1;
		dir = -1;
	}

	// Process each corner: the goal is to draw a circle of radius 'radius', with the sides of the
	// quadrilateral tangent so that the entire shape is smooth.
	for (int corner = start_i; corner != stop_i; corner+=dir) {
		Vector2 cur = corners[corner];

		if (rounded && !rounded[corner]) {
			data[n*stride] = cur.x;
			data[n*stride+1] = cur.y;
			n++;
			continue;
		}

		Vector2 prev = corners[au_positive_modulo(corner - dir, 4)];
		Vector2 next = corners[au_positive_modulo(corner + dir, 4)];

		Vector2 vprev = { prev.x - cur.x, prev.y - cur.y };
		Vector2 vnext = { next.x - cur.x, next.y - cur.y };
		Vector2 vprev_n = au_normalize_v2(vprev);
		Vector2 vnext_n = au_normalize_v2(vnext);

		Vector2 bisector = { (vprev_n.x + vnext_n.x)*0.5, (vprev_n.y + vnext_n.y)*0.5 };
		bisector = au_normalize_v2(bisector);
		float theta = acosf(vprev_n.x * vnext_n.x + vprev_n.y * vnext_n.y);
		float theta_mid = theta / 2;

		// xx diagram...
		// The radii of the circle form a right angle with the sides of the quad, so
		//     radius = d_to_circle_center * sin(theta_mid),
		// where d_to_circle_center is the distance to the circle center along the bisector of the
		// corner angle.
		float d_to_circle_center = radius / sinf(theta_mid);
		Vector2 circle_center = { cur.x + d_to_circle_center*bisector.x,
								  cur.y + d_to_circle_center*bisector.y};

		// The distance from the corner to the first and last points of the circle(the tangent points)
		// is the projection of d_to_circle_center onto the quad sides.
		float d_to_first_and_last_points = d_to_circle_center * cosf(theta_mid);
		Vector2 first_point = au_add_v2(cur, au_mult_cv2(d_to_first_and_last_points, vprev_n));
		Vector2 last_point = au_add_v2(cur, au_mult_cv2(d_to_first_and_last_points, vnext_n));

		// Use phi to denote angles around the circle_center, which also define the normals.
		float phi_first = atan2f(first_point.y-circle_center.y, first_point.x-circle_center.x);
		float phi_last = atan2f(last_point.y-circle_center.y, last_point.x-circle_center.x);
		if (phi_last < phi_first) {
			phi_last += 2*F_PI;
		}

		Vector2 prev_point = first_point;
		float phi = phi_first;
		for (int seg_i = 0; seg_i < segments_per_corner; seg_i++) {
			data[n*stride] = prev_point.x;
			data[n*stride+1] = prev_point.y;
			phi = phi_first + (phi_last-phi_first)*((seg_i+1)/(float)segments_per_corner);
			Vector2 next_point = { circle_center.x + radius*cosf(phi),
								   circle_center.y + radius*sinf(phi)};
			prev_point = next_point;
			n++;
		}
		data[n*stride] = last_point.x; // xx should have prev_point == last_point
		data[n*stride+1] = last_point.y;
		n++;
	}
	return n;
}

void triangleize(float *data, i32 stride, i32 n, Vector2 center, Vector4 color, bool connected)
{
	i32 vstride = stride;
	i32 tstride = 3 * vstride;
	i32 limit = n - (i32) !connected;
	for (i32 i=0; i<limit; i++) {
		data[i*tstride + 2] = 0;
		data[i*tstride + 3] = color.x;
		data[i*tstride + 4] = color.y;
		data[i*tstride + 5] = color.z;
		data[i*tstride + 6] = color.w;
		i32 next_offset = ((i + 1) % n)*tstride;
		data[i*tstride + vstride] = data[next_offset];
		data[i*tstride + vstride + 1] = data[next_offset+1];
		data[i*tstride + vstride + 2] = 0;
		data[i*tstride + vstride + 3] = color.x;
		data[i*tstride + vstride + 4] = color.y;
		data[i*tstride + vstride + 5] = color.z;
		data[i*tstride + vstride + 6] = color.w;
		data[i*tstride + 2*vstride] = center.x;
		data[i*tstride + 2*vstride + 1] = center.y;
		data[i*tstride + 2*vstride + 2] = 0;
		data[i*tstride + 2*vstride + 3] = color.x;
		data[i*tstride + 2*vstride + 4] = color.y;
		data[i*tstride + 2*vstride + 5] = color.z;
		data[i*tstride + 2*vstride + 6] = color.w;
		for (int j=0; j<3; j++) {
			i32 base = i*tstride + j*vstride;
			data[base + 7] = 0.0f;
			data[base + 8] = 0.0f;
			data[base + 9] = -1.0f;
		}
	}
}

void outlineize(float *data, i32 stride, i32 n, float thickness, Vector4 color, bool connected)
{
	i32 vstride = stride;
	i32 sstride = 6*vstride; // segment stride
	i32 limit = n - (i32) !connected;
	float first_x, first_y;
	for (i32 i=0; i<limit; i++) {
		i32 offset = i*sstride;
		float x1 = data[offset];
		float y1 = data[offset+1];
		if (i==0) {
			first_x = x1;
			first_y = y1;
		}
		float x2 = i==n-1 ? first_x : data[offset+sstride];
		float y2 = i==n-1 ? first_y : data[offset+sstride+1];
		float dx = (x2 - x1);
		float dy = (y2 - y1);
		float d = sqrt(dx*dx + dy*dy);
		// (xadj, yadj) is a vector pointing to the right of the direction of travel.
		float adj = thickness / 2.0f;
		float xadj = adj*(dy / d);
		float yadj = adj*(-dx / d);
		data[offset] = x1+xadj;
		data[offset+1] = y1+yadj;
		data[offset+vstride] = x2+xadj;
		data[offset+vstride+1] = y2+yadj;
		data[offset+2*vstride] = x1-xadj;
		data[offset+2*vstride+1] = y1-yadj;
		data[offset+3*vstride] = x1-xadj;
		data[offset+3*vstride+1] = y1-yadj;
		data[offset+4*vstride] = x2+xadj;
		data[offset+4*vstride+1] = y2+yadj;
		data[offset+5*vstride] = x2-xadj;
		data[offset+5*vstride+1] = y2-yadj;
		for (int j=0; j<6; j++) {
			data[offset+j*vstride+2] = 0.0f;
			data[offset+j*vstride+3] = color.x;
			data[offset+j*vstride+4] = color.y;
			data[offset+j*vstride+5] = color.z;
			data[offset+j*vstride+6] = color.w;
			data[offset+j*vstride+7] = 0.0f;
			data[offset+j*vstride+8] = 0.0f;
			data[offset+j*vstride+9] = -1.0f;
		}
	}
}

/************************************** add_* functions *******************************************/

/*
void assert_not_overflowing(Render_Context *context)
{
	assertf(context->n < context->capacity, "Overflow of scene buffer capacity(too much drawing).\n");
}
*/

void clear_background(Scene *scene, Vector4 color)
{
	glClearColor(color.x, color.y, color.z, color.w);
	glClear(GL_COLOR_BUFFER_BIT);
}

// pass in the context to avoid redundant call to scene_get_and_activate_context
void scene_transform_xy(Scene *scene, Render_Context *context, float *x, float *y)
{
	Vector4 bounds = scene_get_window_bounds(scene);
	*x += bounds.x;
	*y += bounds.y;
	if (context->use_screen_coords) {
		/* We translate the geometry into a coordinate system with x from -1 to 1, and y
		from -y_scale(bottom) to y_scale(top), where y_scale is the window's h/w ratio. This is
		a uniform scaling, so that shapes are preserved(we can do things like generate circles in
		this system that will be circles on screen) but is also right handed, so that we can write
		cpu code here that assumes right handedness. It is also trivial to translate to OpenGL's
		NDC system in the vertex shader(by dividing y by y_scale). */
		*x = *x * (2.0f / context->viewport_w) - 1.0f;
		*y = *y * (-2.0f  / context->viewport_w) + context->y_scale;
	}
}

void context_transform_wh(Render_Context *context, float *w, float *h)
{
	// don't need scene because no translation
	if (context->use_screen_coords) {
		*w = *w * (2.0f / context->viewport_w);
		*h = *h * (-2.0f  / context->viewport_w);
	}
}

void context_transform_1(Render_Context *context, float *q)
{
	if (context->use_screen_coords) {
		*q = *q * (2.0f / context->viewport_w);
	}
}

void add_line_ex(Scene *scene, float x1, float y1, float x2, float y2, float thickness,
	Vector4 color)
{
	Render_Context *context = scene_get_and_activate_context(scene);
	scene_transform_xy(scene, context, &x1, &y1);
	scene_transform_xy(scene, context, &x2, &y2);
	context_transform_1(context, &thickness);
	u64 max_verts = 6;
	dynarray_expand_by(context->vertices, max_verts*context->vertex_size);
	float *data = context->vertices->d + context->vertices->length;
	i32 stride = context->vertex_size;
	// copied from outlinize
	float dx = (x2 - x1);
	float dy = (y2 - y1);
	float d = sqrt(dx*dx + dy*dy);
	// (xadj, yadj) is a vector pointing to the right of the direction of travel.
	float adj = thickness / 2.0f;
	float xadj = adj*(dy / d);
	float yadj = adj*(-dx / d);
	data[0] = x1+xadj;
	data[1] = y1+yadj;
	data[stride] = x2+xadj;
	data[stride+1] = y2+yadj;
	data[2*stride] = x1-xadj;
	data[2*stride+1] = y1-yadj;
	data[3*stride] = x1-xadj;
	data[3*stride+1] = y1-yadj;
	data[4*stride] = x2+xadj;
	data[4*stride+1] = y2+yadj;
	data[5*stride] = x2-xadj;
	data[5*stride+1] = y2-yadj;
	for (int j=0; j<6; j++) {
		data[j*stride+2] = 0.0f;
		data[j*stride+3] = color.x;
		data[j*stride+4] = color.y;
		data[j*stride+5] = color.z;
		data[j*stride+6] = color.w;
		data[j*stride+7] = 0.0f;
		data[j*stride+8] = 0.0f;
		data[j*stride+9] = -1.0f;
	}
	context->vertices->length += 6*context->vertex_size;;
}

void add_line(Scene *scene, float x1, float y1, float x2, float y2, Vector4 color)
{
	add_line_ex(scene, x1, y1, x2, y2, 1, color);
}

void add_rectangle(Scene *scene, float x, float y, float w, float h, Vector4 color)
{
	Render_Context *context = scene_get_and_activate_context(scene);
	scene_transform_xy(scene, context, &x, &y);
	context_transform_wh(context, &w, &h);
	u64 max_verts = 4 * 3;
	dynarray_expand_by(context->vertices, max_verts*context->vertex_size);
	float *data = context->vertices->d + context->vertices->length;
	i32 n = generate_rectangle(data, 3*context->vertex_size, x, y, w, h, color);
	assertf(n*3 <= max_verts, NULL);
	Vector2 center = { x + 0.5*w, y + 0.5*h };
	triangleize(data, context->vertex_size, n, center, color, true);
	context->vertices->length += n*3*context->vertex_size;
}

void add_rectangle_outline_ex(Scene *scene, float x, float y, float w, float h, float thickness,
	Vector4 color)
{
	Render_Context *context = scene_get_and_activate_context(scene);
	scene_transform_xy(scene, context, &x, &y);
	context_transform_wh(context, &w, &h);
	context_transform_1(context, &thickness);
	u64 max_verts = 4 * 6;
	dynarray_expand_by(context->vertices, max_verts*context->vertex_size);
	float *data = context->vertices->d + context->vertices->length;
	i32 n = generate_rectangle(data, 6*context->vertex_size, x, y, w, h, color);
	assertf(n*6 <= max_verts, NULL);
	outlineize(data, context->vertex_size, n, thickness, color, true);
	context->vertices->length += n*6*context->vertex_size;
}

void add_rectangle_outline(Scene *scene, float x, float y, float w, float h, Vector4 color)
{
	add_rectangle_outline_ex(scene, x, y, w, h, 1, color);
}

void add_triangle(Scene *scene, float x1, float y1, float x2, float y2, float x3, float y3, Vector4 color)
{
	Render_Context *context = scene_get_and_activate_context(scene);
	scene_transform_xy(scene, context, &x1, &y1);
	scene_transform_xy(scene, context, &x2, &y2);
	scene_transform_xy(scene, context, &x3, &y3);
	dynarray_expand_by(context->vertices, 3*context->vertex_size);
	float *data = context->vertices->d + context->vertices->length;

	float positions[3][2] = {
		{ x1, y1 },
		{ x2, y2 },
		{ x3, y3 },
	};
	i32 stride = context->vertex_size;
	for (i32 i=0; i<3; i++) {
		i32 base = i*stride;
		data[base + 0] = positions[i][0];
		data[base + 1] = positions[i][1];
		data[base + 2] = 0.0f;
		data[base + 3] = color.x;
		data[base + 4] = color.y;
		data[base + 5] = color.z;
		data[base + 6] = color.w;
		data[base + 7] = 0.0f;
		data[base + 8] = 0.0f;
		data[base + 9] = -1.0f;
	}
	context->vertices->length += 3*context->vertex_size;
}

void add_triangle_outline_ex(Scene *scene, float x1, float y1, float x2, float y2, float x3, float y3, Vector4 color, float thickness)
{
	Render_Context *context = scene_get_and_activate_context(scene);
	scene_transform_xy(scene, context, &x1, &y1);
	scene_transform_xy(scene, context, &x2, &y2);
	scene_transform_xy(scene, context, &x3, &y3);
	context_transform_1(context, &thickness);
	dynarray_expand_by(context->vertices, 3*6*context->vertex_size);
	float *data = context->vertices->d + context->vertices->length;

	float positions[3][2] = {
		{ x1, y1 },
		{ x2, y2 },
		{ x3, y3 },
	};
	i32 stride = 6*context->vertex_size;
	for (i32 i=0; i<3; i++) {
		i32 base = i*stride;
		data[base + 0] = positions[i][0];
		data[base + 1] = positions[i][1];
		data[base + 2] = 0.0f;
		data[base + 3] = color.x;
		data[base + 4] = color.y;
		data[base + 5] = color.z;
		data[base + 6] = color.w;
		data[base + 7] = 0.0f;
		data[base + 8] = 0.0f;
		data[base + 9] = -1.0f;
	}
	outlineize(data, context->vertex_size, 3, thickness, color, true);
	context->vertices->length += 3*6*context->vertex_size;
}

void add_triangle_outline(Scene *scene, float x1, float y1, float x2, float y2, float x3, float y3, Vector4 color)
{
	add_triangle_outline_ex(scene, x1, y1, x2, y2, x3, y3, color, 1);
}

void add_circle_ex(Scene *scene, float x, float y, float r, float segments, Vector4 color)
{
	Render_Context *context = scene_get_and_activate_context(scene);
	scene_transform_xy(scene, context, &x, &y);
	context_transform_1(context, &r);
	u64 max_verts = segments * 3;
	dynarray_expand_by(context->vertices, max_verts*context->vertex_size);
	float *data = context->vertices->d + context->vertices->length;
	i32 n = generate_circle(data, 3*context->vertex_size, x, y, r, segments);
	assertf(n*3 <= max_verts, NULL);
	triangleize(data, context->vertex_size, n, (Vector2) { x, y }, color, true);
	context->vertices->length += n*3*context->vertex_size;
}

void add_circle(Scene *scene, float x, float y, float r, Vector4 color)
{
	// XX num segments?
	add_circle_ex(scene, x, y, r, MAX(20, r/2), color);
}

void add_circle_outline_ex(Scene *scene, float x, float y, float r, float segments, float thickness,
	Vector4 color)
{
	Render_Context *context = scene_get_and_activate_context(scene);
	scene_transform_xy(scene, context, &x, &y);
	context_transform_1(context, &r);
	context_transform_1(context, &thickness);
	u64 max_verts = segments * 6;
	dynarray_expand_by(context->vertices, max_verts*context->vertex_size);
	float *data = context->vertices->d + context->vertices->length;
	i32 n = generate_circle(data, 6*context->vertex_size, x, y, r, segments);
	outlineize(data, context->vertex_size, n, thickness, color, true);
	assertf(n*6 <= max_verts, NULL);
	context->vertices->length += n*6*context->vertex_size;
}

void add_circle_outline(Scene *scene, float x, float y, float r, Vector4 color)
{
	// XX num segments?
	add_circle_outline_ex(scene, x, y, r, MAX(20, r/2), 1, color);
}

void add_circle_arc_ex(Scene *scene, float x, float y, float r, float angle1, float angle2,
    i32 segments, float thickness, Vector4 color)
{
	Render_Context *context = scene_get_and_activate_context(scene);
	scene_transform_xy(scene, context, &x, &y);
	context_transform_1(context, &r);
	context_transform_1(context, &thickness);
	u64 max_verts = (segments+1) * 6;
	dynarray_expand_by(context->vertices, max_verts*context->vertex_size);
	float *data = context->vertices->d + context->vertices->length;
	i32 n = generate_circle_arc(data, 6*context->vertex_size, x, y, r, angle1, angle2, segments);
	outlineize(data, context->vertex_size, n, thickness, color, false);
	assertf(n*6 <= max_verts, NULL);
	context->vertices->length += (n-1)*6*context->vertex_size;
}

void add_circle_arc(Scene *scene, float x, float y, float r, float angle1, float angle2,
    Vector4 color)
{
	add_circle_arc_ex(scene, x, y, r, angle1, angle2, MAX(20, r / 2), 1.0f, color);
}

void add_rounded_quad_ex(Scene *scene, Vector2 *corners, bool *rounded, float radius,
	i32 segments_per_corner, Vector4 color)
{
	Render_Context *context = scene_get_and_activate_context(scene);
	Vector2 corners_copy[4];
	memcpy(corners_copy, corners, sizeof(corners_copy));
	Vector2 center = { 0.0f, 0.0f };
	for (i32 i=0; i < 4; i++) {
		scene_transform_xy(scene, context, &corners_copy[i].x, &corners_copy[i].y);
		center.x += corners_copy[i].x;
		center.y += corners_copy[i].y;
	}
	center.x /= 4.0f;
	center.y /= 4.0f;
	context_transform_1(context, &radius);
	u64 max_verts = (4*segments_per_corner + 4) * 3;
	dynarray_expand_by(context->vertices, max_verts*context->vertex_size);
	float *data = context->vertices->d + context->vertices->length;
	i32 n = generate_rounded_quad(data, 3*context->vertex_size, corners_copy, rounded, radius,
		segments_per_corner);
	triangleize(data, context->vertex_size, n, center, color, true);
	assertf(n*3 <= max_verts, NULL);
	context->vertices->length += n*3*context->vertex_size;
}

void add_rounded_quad_outline_ex(Scene *scene, Vector2 *corners, bool *rounded, float radius,
	i32 segments_per_corner, float thickness, Vector4 color)
{
	Render_Context *context = scene_get_and_activate_context(scene);
	Vector2 corners_copy[4];
	memcpy(corners_copy, corners, sizeof(corners_copy));
	for (i32 i=0; i < 4; i++) {
		scene_transform_xy(scene, context, &corners_copy[i].x, &corners_copy[i].y);
	}
	context_transform_1(context, &radius);
	context_transform_1(context, &thickness);
	u64 max_verts = (4*segments_per_corner + 4)*6;
	dynarray_expand_by(context->vertices, max_verts*context->vertex_size);
	float *data = context->vertices->d + context->vertices->length;
	i32 n = generate_rounded_quad(data, 6*context->vertex_size, corners_copy, rounded, radius,
		segments_per_corner);
	outlineize(data, context->vertex_size, n, thickness, color, true);
	assertf(n*6 <= max_verts, NULL);
	context->vertices->length += n*6*context->vertex_size;
}

void add_rounded_quad(Scene *scene, Vector2 *corners, bool *rounded, float radius, Vector4 color)
{
	add_rounded_quad_ex(scene, corners, rounded, radius, 20, color);
}

void add_rounded_quad_outline(Scene *scene, Vector2 *corners, bool *rounded, float radius,
	Vector4 color)
{
	add_rounded_quad_outline_ex(scene, corners, rounded, radius, 20, 1, color);
}

void add_rounded_rectangle_ex(Scene *scene, float x, float y, float w, float h, float radius,
	i32 segments_per_corner, Vector4 color)
{
	// Vector2 rect_corners[4] = { { x, y }, { x, y+h }, { x+w, y+h }, { x+w, y } };
	Vector2 rect_corners[4] = { { x, y }, { x + w, y }, { x+w, y+h }, { x, y + h } };
	bool rounded[4] = { true, true, true, true };
	add_rounded_quad_ex(scene, rect_corners, rounded, radius, 20, color);
}

void add_rounded_rectangle(Scene *scene, float x, float y, float w, float h, float radius,
	Vector4 color)
{
	// Vector2 rect_corners[4] = { { x, y }, { x, y+h }, { x+w, y+h }, { x+w, y } };
	Vector2 rect_corners[4] = { { x, y }, { x + w, y }, { x+w, y+h }, { x, y + h } };
	bool rounded[4] = { true, true, true, true };
	add_rounded_quad_ex(scene, rect_corners, rounded, radius, 20, color);
}

void add_rounded_rectangle_outline(Scene *scene, float x, float y, float w, float h, float radius,
	Vector4 color)
{
	// Vector2 rect_corners[4] = { { x, y }, { x, y+h }, { x+w, y+h }, { x+w, y } };
	Vector2 rect_corners[4] = { { x, y }, { x + w, y }, { x+w, y+h }, { x, y + h } };
	bool rounded[4] = { true, true, true, true };
	add_rounded_quad_outline_ex(scene, rect_corners, rounded, radius, 20, 1, color);
}

i32 load_glyph(Scene *scene, Font *font, i32 size, u32 c)
{
	Render_Context *context = scene_get_context(scene);
	FT_Face ft_face = font->ft_face;
	FT_Error error = FT_Set_Pixel_Sizes(ft_face, size, size);
	FT_UInt glyph_index = FT_Get_Char_Index(ft_face, c);
	if (!glyph_index) {
		fprintf(stderr, "Failed to find character in font face: %u.\n", c);
	}
	error = FT_Load_Glyph(ft_face, glyph_index, FT_LOAD_DEFAULT);

	error = FT_Render_Glyph(ft_face->glyph, FT_RENDER_MODE_NORMAL);
	FT_GlyphSlotRec *slot = ft_face->glyph;
	FT_Bitmap ft_bitmap = ft_face->glyph->bitmap;

	Bitmap b = { 0 };
	b.is_glyph = true;
	b.bitmap_left = slot->bitmap_left;
	b.bitmap_top = slot->bitmap_top;
	b.advance_x = slot->advance.x >> 6;
	b.advance_y = slot->advance.y >> 6;
	i32 bitmap_i = load_bitmap_internal(context, ft_bitmap.buffer, ft_bitmap.width, ft_bitmap.rows,
		ft_bitmap.pitch, 1, &b);
	if (bitmap_i >= 0) {
		// Bitmap *b_new_loc = &scene->bitmaps->d[bitmap_i];
		Glyph_Key k = { c, size };
		hash_table_set(&font->glyphs, &k, sizeof(Glyph_Key), (void *) (u64) bitmap_i, 0);
		return bitmap_i;
	} else {
		return -1;
	}
}

// xx stride
void add_character(Scene *scene, int font_i, i32 size, u32 c, float x, float y, Vector4 color,
	float *advance_x)
{
	Render_Context *context = scene_get_and_activate_context(scene);
	if (font_i < 0 || font_i >= context->fonts_n) {
		fprintf(stderr, "Invalid font index %d\n", font_i);
		*advance_x = -1;
		return;
	}
	Font *font = context->fonts[font_i];
	Glyph_Key k = { c,  size };
	void *bitmap_i_v;
	bool found = hash_table_get2(&font->glyphs, &k, sizeof(Glyph_Key), &bitmap_i_v, 0);
	i32 bitmap_i;
	if (found) {
		bitmap_i = (i32) (u64) bitmap_i_v;
	} else {
		bitmap_i = load_glyph(scene, font, size, c);
		if (bitmap_i < 0) {
			*advance_x = -1;
			return;
		}
	}
	Bitmap *bitmap = &context->bitmaps->d[bitmap_i];
	Texture_Info *texture = &context->textures[bitmap->texture_i];
	float tex_x = (float) bitmap->tex_x;
	float tex_y = (float) bitmap->tex_y;
	float tex_w = (float) bitmap->w;
	float tex_h = (float) bitmap->h;
	float dc_w = bitmap->w * (2.0f / context->viewport_w);
	float dc_h = bitmap->h * (2.0f / context->viewport_w);
	float bitmap_left = bitmap->bitmap_left * (2.0f / context->viewport_w);
	float bitmap_top = bitmap->bitmap_top * (2.0f / context->viewport_w);
	scene_transform_xy(scene, context, &x, &y);
	x += bitmap_left;
	// The -dc_h is because (x, y) is at the bottom of the bitmap. Freetype would normally expect you to
	// start drawing at the top of the character(row 0) and work down, which is why you see
	// y = baseline_y + bitmap_top in example code, but we flipped the bitmap during atlas generation.
	y += (bitmap_top  - dc_h);
	dynarray_expand_by(context->vertices, 6*context->vertex_size);
	float *data = context->vertices->d + context->vertices->length;
	context->vertices->length += 6*context->vertex_size;
	i32 stride = context->vertex_size;
	float positions[6][2] = {
		{ x, y },
		{ x + dc_w, y },
		{ x + dc_w, y + dc_h },
		{ x + dc_w, y + dc_h },
		{ x, y + dc_h },
		{ x, y }
	};
	float texcoords[6][2] = {
		{ tex_x, tex_y },
		{ tex_x + tex_w, tex_y },
		{ tex_x + tex_w, tex_y + tex_h },
		{ tex_x + tex_w, tex_y + tex_h },
		{ tex_x, tex_y + tex_h },
		{ tex_x, tex_y },
	};
	for (i32 i=0; i<6; i++) {
		i32 base = i*stride;
		data[base + 0] = positions[i][0];
		data[base + 1] = positions[i][1];
		data[base + 2] = 0;
		data[base + 3] = color.x;
		data[base + 4] = color.y;
		data[base + 5] = color.z;
		data[base + 6] = color.w;
		data[base + 7] = texcoords[i][0];
		data[base + 8] = texcoords[i][1];
		data[base + 9] = (float) bitmap->texture_i;
	}
	float adv_x = bitmap->advance_x;
	// bitmap->advance_x is in pixels, so translate to NDC if *not* using pixels.
	if (!context->use_screen_coords) {
		adv_x = adv_x * (2.0f / context->viewport_w);
	}
	*advance_x = adv_x;
}

// TODO: decode utf-8
void add_text(Scene *scene, int font_i, i32 size, const char *text, float x, float y, Vector4 color)
{
	Window *win = (Window *) scene->window;
	u64 utf32_length;
	// xx decode as we go...
	u32 *text_utf32 = decode_utf8(win->arena, text, &utf32_length);
	if (text_utf32) {
		add_text_utf32(scene, font_i, size, text_utf32, x, y, color);
		afree(win->arena, text_utf32);
	}
}

void add_text_utf32(Scene *scene, i32 font_i, i32 size, const u32 *text, float x, float y, Vector4 color)
{
	Render_Context *context = scene_get_and_activate_context(scene);
	if (font_i < 0 || font_i >= context->fonts_n) {
		fprintf(stderr, "Invalid font index %d\n", font_i);
		return;
	}
	Font *font = context->fonts[font_i];
	float pen_x = x;
	float pen_y = y;
	float line_advance_ratio = 1.2f;
	float line_advance = size * line_advance_ratio;
	for (const u32 *p = text; *p; p++) {
		if (*p == '\n') {
			pen_x = x;
			pen_y += line_advance;
			continue;
		}
		u32 code = *p;
		float adv = 0;
		add_character(scene, font_i, size, code, pen_x, pen_y, color, &adv);
		if (adv <= 0) {
			// maybe a character wasn't found; skip it using a best guess size
			float default_advance = size;
			if (!context->use_screen_coords) {
				default_advance = default_advance * (2.0f / context->viewport_w);
			}
			pen_x += default_advance;
		} else {
			pen_x += adv;
		}
	}
}

void add_text_last_measured(Scene *scene, float x, float y, Vector4 color)
{
	Renderer_Data *rdata = (Renderer_Data *) scene->window->renderer_data;
	add_text_utf32(scene, rdata->last_measured_font, rdata->last_measured_size,
		rdata->last_measured_text, x, y, color);
}

float measure_text_width(Scene *scene, int font_i, i32 size, const char *text)
{
	Window *win = (Window *) scene->window;
	u64 utf32_length;
	// xx decode as we go...
	u32 *text_utf32 = decode_utf8(win->arena, text, &utf32_length);
	if (text_utf32) {
		float res = measure_text_width_utf32(scene, font_i, size, text_utf32);
		afree(win->arena, text_utf32);
		return res;
	} else {
		return -1;
	}
}

float measure_text_width_utf32(Scene *scene, int font_i, i32 size, const u32 *text)
{
	Render_Context *context = scene_get_context(scene);
	if (font_i < 0 || font_i >= context->fonts_n) {
		fprintf(stderr, "Invalid font index %d\n", font_i);
		return 0;
	}
	Renderer_Data *rdata = (Renderer_Data *) scene->window->renderer_data;
	Font *font = context->fonts[font_i];
	u64 text_len = 0;
	float pen_x = 0;
	float line_advance_ratio = 1.2f;
	float line_advance = size * line_advance_ratio;
	for (const u32 *p = text; *p; p++) {
		text_len++;
		if (*p == '\n') {
			pen_x = 0;
			continue;
		}
		u32 code = *p;
		Glyph_Key k = { code,  size };
		void *bitmap_i_v;
		bool found = hash_table_get2(&font->glyphs, &k, sizeof(Glyph_Key), &bitmap_i_v, NULL);
		i32 bitmap_i;
		if (found) {
			bitmap_i = (i32) (u64) bitmap_i_v;
		} else {
			bitmap_i = load_glyph(scene, font, size, code);
		}
		float default_advance = size;
		float adv = default_advance;
		if (bitmap_i >= 0) {
			Bitmap *bitmap = &context->bitmaps->d[bitmap_i];
			adv = bitmap->advance_x;
		}
		if (!context->use_screen_coords) {
			adv = adv * (2.0f / context->viewport_w);
		}
		pen_x += adv;
	}
	// Cache text for add_text_last_measured
	Arena *arena = scene->window->arena;
	if (rdata->last_measured_text) {
		rdata->last_measured_text = arealloc(arena, rdata->last_measured_text, (text_len+1)*sizeof(u32));
	} else {
		rdata->last_measured_text = adalloc(arena, (text_len+1)*sizeof(u32));
	}
	memmove(rdata->last_measured_text, text, text_len*sizeof(u32));
	rdata->last_measured_text[text_len] = 0;
	rdata->last_measured_font = font_i;
	rdata->last_measured_size = size;
	return pen_x;
}

void add_image(Scene *scene, i32 image_i, float x, float y)
{
	add_image_with_alpha(scene, image_i, x, y, 1.0f);
}

void add_image_with_alpha(Scene *scene, i32 image_i, float x, float y, float alpha)
{
	Render_Context *context = scene_get_and_activate_context(scene);
	Bitmap_Dynarray *bitmaps = (Bitmap_Dynarray *) context->bitmaps;
	assertf(image_i >= 0 && image_i < bitmaps->length, NULL);
	Bitmap *bitmap = &bitmaps->d[image_i];
	float dc_w = (bitmap->w) * (2.0f / context->viewport_w);
	float dc_h = (bitmap->h) * (2.0f / context->viewport_w);
	scene_transform_xy(scene, context, &x, &y);
	y -= dc_h;
	dynarray_expand_by(context->vertices, 6*context->vertex_size);
	float *data = context->vertices->d + context->vertices->length;
	context->vertices->length += 6*context->vertex_size;
	i32 stride = context->vertex_size;
	float positions[6][2] = {
		{ x, y },
		{ x + dc_w, y },
		{ x + dc_w, y + dc_h },
		{ x + dc_w, y + dc_h },
		{ x, y + dc_h },
		{ x, y }
	};
	i32 tex_id = bitmap->texture_i;
	float tex_x = bitmap->tex_x;
	float tex_y = bitmap->tex_y;
	float tex_w = bitmap->w;
	float tex_h = bitmap->h;
	float texcoords[6][2] = {
		{ tex_x, tex_y },
		{ tex_x + tex_w, tex_y },
		{ tex_x + tex_w, tex_y + tex_h },
		{ tex_x + tex_w, tex_y + tex_h },
		{ tex_x, tex_y + tex_h },
		{ tex_x, tex_y },
	};
	for (i32 i=0; i<6; i++) {
		i32 base = i*stride;
		data[base + 0] = positions[i][0];
		data[base + 1] = positions[i][1];
		data[base + 2] = 0.0f;
		data[base + 3] = 1.0f;
		data[base + 4] = 1.0f;
		data[base + 5] = 1.0f;
		data[base + 6] = alpha;
		data[base + 7] = texcoords[i][0];
		data[base + 8] = texcoords[i][1];
		data[base + 9] = (float) tex_id;
	}
}

void add_image_with_color(Scene *scene, i32 image_i, float x, float y, Vector4 color)
{
	Render_Context *context = scene_get_and_activate_context(scene);
	Bitmap_Dynarray *bitmaps = (Bitmap_Dynarray *) context->bitmaps;
	assertf(image_i >= 0 && image_i < bitmaps->length, NULL);
	Bitmap *bitmap = &bitmaps->d[image_i];
	assertf(context->textures[bitmap->texture_i].channels == 1, "add_imge_with_color: Image must be single-channel.\n");
	float dc_w = (bitmap->w) * (2.0f / context->viewport_w);
	float dc_h = (bitmap->h) * (2.0f / context->viewport_w);
	scene_transform_xy(scene, context, &x, &y);
	y -= dc_h;
	dynarray_expand_by(context->vertices, 6*context->vertex_size);
	float *data = context->vertices->d + context->vertices->length;
	context->vertices->length += 6*context->vertex_size;
	i32 stride = context->vertex_size;
	float positions[6][2] = {
		{ x, y },
		{ x + dc_w, y },
		{ x + dc_w, y + dc_h },
		{ x + dc_w, y + dc_h },
		{ x, y + dc_h },
		{ x, y }
	};
	i32 tex_id = bitmap->texture_i;
	float tex_x = bitmap->tex_x;
	float tex_y = bitmap->tex_y;
	float tex_w = bitmap->w;
	float tex_h = bitmap->h;
	float texcoords[6][2] = {
		{ tex_x, tex_y },
		{ tex_x + tex_w, tex_y },
		{ tex_x + tex_w, tex_y + tex_h },
		{ tex_x + tex_w, tex_y + tex_h },
		{ tex_x, tex_y + tex_h },
		{ tex_x, tex_y },
	};
	for (i32 i=0; i<6; i++) {
		i32 base = i*stride;
		data[base + 0] = positions[i][0];
		data[base + 1] = positions[i][1];
		data[base + 2] = 0.0f;
		data[base + 3] = color.x;
		data[base + 4] = color.y;
		data[base + 5] = color.z;
		data[base + 6] = color.w;
		data[base + 7] = texcoords[i][0];
		data[base + 8] = texcoords[i][1];
		data[base + 9] = (float) tex_id;
	}
}
