#ifndef AU_WINDOW_H
#define AU_WINDOW_H

#include "au_core.h"

#ifdef __cplusplus
extern "C" {
#endif

/************************************** Window and input ******************************************/

/********* Backend agnostic API **********/

// This enum is adapted from minifb. Todo: LICENSE
enum au_key {
    AU_KEY_UNKNOWN       = -1,

    AU_KEY_SPACE         = 32, AU_KEY_APOSTROPHE    = 39, AU_KEY_COMMA         = 44,
    AU_KEY_MINUS         = 45, AU_KEY_PERIOD        = 46, AU_KEY_SLASH         = 47,
    AU_KEY_0             = 48, AU_KEY_1             = 49, AU_KEY_2             = 50,
    AU_KEY_3             = 51, AU_KEY_4             = 52, AU_KEY_5             = 53,
    AU_KEY_6             = 54, AU_KEY_7             = 55, AU_KEY_8             = 56,
    AU_KEY_9             = 57, AU_KEY_SEMICOLON     = 59, AU_KEY_EQUAL         = 61,
    AU_KEY_A             = 65, AU_KEY_B             = 66, AU_KEY_C             = 67,
    AU_KEY_D             = 68, AU_KEY_E             = 69, AU_KEY_F             = 70,
    AU_KEY_G             = 71, AU_KEY_H             = 72, AU_KEY_I             = 73,
    AU_KEY_J             = 74, AU_KEY_K             = 75, AU_KEY_L             = 76,
    AU_KEY_M             = 77, AU_KEY_N             = 78, AU_KEY_O             = 79,
    AU_KEY_P             = 80, AU_KEY_Q             = 81, AU_KEY_R             = 82,
    AU_KEY_S             = 83, AU_KEY_T             = 84, AU_KEY_U             = 85,
    AU_KEY_V             = 86, AU_KEY_W             = 87, AU_KEY_X             = 88,
    AU_KEY_Y             = 89, AU_KEY_Z             = 90, AU_KEY_LEFT_BRACKET  = 91,
    AU_KEY_BACKSLASH     = 92, AU_KEY_RIGHT_BRACKET = 93, AU_KEY_GRAVE_ACCENT  = 96,
    AU_KEY_WORLD_1       = 161, AU_KEY_WORLD_2       = 162,

    AU_KEY_ESCAPE        = 256, AU_KEY_ENTER         = 257, AU_KEY_TAB           = 258,
    AU_KEY_BACKSPACE     = 259, AU_KEY_INSERT        = 260, AU_KEY_DELETE        = 261,
    AU_KEY_RIGHT         = 262, AU_KEY_LEFT          = 263, AU_KEY_DOWN          = 264,
    AU_KEY_UP            = 265, AU_KEY_PAGE_UP       = 266, AU_KEY_PAGE_DOWN     = 267,
    AU_KEY_HOME          = 268, AU_KEY_END           = 269, AU_KEY_CAPS_LOCK     = 280,
    AU_KEY_SCROLL_LOCK   = 281, AU_KEY_NUM_LOCK      = 282, AU_KEY_PRINT_SCREEN  = 283,
    AU_KEY_PAUSE         = 284, AU_KEY_F1            = 290, AU_KEY_F2            = 291,
    AU_KEY_F3            = 292, AU_KEY_F4            = 293, AU_KEY_F5            = 294,
    AU_KEY_F6            = 295, AU_KEY_F7            = 296, AU_KEY_F8            = 297,
    AU_KEY_F9            = 298, AU_KEY_F10           = 299, AU_KEY_F11           = 300,
    AU_KEY_F12           = 301, AU_KEY_F13           = 302, AU_KEY_F14           = 303,
    AU_KEY_F15           = 304, AU_KEY_F16           = 305, AU_KEY_F17           = 306,
    AU_KEY_F18           = 307, AU_KEY_F19           = 308, AU_KEY_F20           = 309,
    AU_KEY_F21           = 310, AU_KEY_F22           = 311, AU_KEY_F23           = 312,
    AU_KEY_F24           = 313, AU_KEY_F25           = 314, AU_KEY_KP_0          = 320,
    AU_KEY_KP_1          = 321, AU_KEY_KP_2          = 322, AU_KEY_KP_3          = 323,
    AU_KEY_KP_4          = 324, AU_KEY_KP_5          = 325, AU_KEY_KP_6          = 326,
    AU_KEY_KP_7          = 327, AU_KEY_KP_8          = 328, AU_KEY_KP_9          = 329,
    AU_KEY_KP_DECIMAL    = 330, AU_KEY_KP_DIVIDE     = 331, AU_KEY_KP_MULTIPLY   = 332,
    AU_KEY_KP_SUBTRACT   = 333, AU_KEY_KP_ADD        = 334, AU_KEY_KP_ENTER      = 335,
    AU_KEY_KP_EQUAL      = 336, AU_KEY_LEFT_SHIFT    = 340, AU_KEY_LEFT_CONTROL  = 341,
    AU_KEY_LEFT_ALT      = 342, AU_KEY_LEFT_SUPER    = 343, AU_KEY_RIGHT_SHIFT   = 344,
    AU_KEY_RIGHT_CONTROL = 345, AU_KEY_RIGHT_ALT     = 346, AU_KEY_RIGHT_SUPER   = 347,
    AU_KEY_MENU          = 348, AU_KEY_COMMAND       = 349, AU_KEY_COUNT          = 350
};

enum au_mouse_button {
    AU_MOUSE_BUTTON_LEFT = 0,
    AU_MOUSE_BUTTON_RIGHT = 1,
    // todo: I dont receive events from middle or side buttons on Windows.
    AU_MOUSE_BUTTON_MIDDLE = 2,
    AU_MOUSE_BUTTON_X1 = 3,
    AU_MOUSE_BUTTON_X2 = 4,
    AU_MOUSE_BUTTON_COUNT = 5
};

enum au_cursor {
	AU_CURSOR_NORMAL,
	AU_CURSOR_HORIZONTAL_RESIZE,
	AU_CURSOR_VERTICAL_RESIZE,
	AU_CURSOR_FORWARD_DIAGONAL_RESIZE,
	AU_CURSOR_BACKWARD_DIAGONAL_RESIZE,
	AU_CURSOR_HAND,
	AU_CURSOR_COUNT,
 };

#define MOUSE_BUTTONS_N 5
#define KEYS_N 350
#define AU_TICK_FREQ 1000000000
#define AU_MAX_CURSORS 20

typedef struct char_dynarray {
	char *d;
	u64 length;
	u64 capacity;
	u64 item_size;
	Arena *arena;
} Char_Dynarray;

typedef struct input_state {
	bool quit;

	bool focus_gained;
	bool focus_lost;
	bool window_resized;
	bool window_rescaled;

	bool mouse_pressed[AU_MOUSE_BUTTON_COUNT];
	bool mouse_released[AU_MOUSE_BUTTON_COUNT];
	bool mouse_down[AU_MOUSE_BUTTON_COUNT];
	// @Breaking was mouse_x, etc
	i32 pointer_x, pointer_y;
	i32 pointer_dx, pointer_dy;
	// TODO: bool pointer_entered, pointer_left;

	float wheel_dx, wheel_dy;

	bool key_pressed[AU_KEY_COUNT];
	bool key_released[AU_KEY_COUNT];
	bool key_down[AU_KEY_COUNT];
	// handles modifiers even if remapped
//  	i32 mods;
	Char_Dynarray *text_entered;
	u64 start_time;
	// time in ticks from start_time, scaled by win->animation_rate
	u64 anim_time;
	// unscaled time in ticks
	u64 wall_time;
	// time since last update_input_state
	u64 anim_dt;
	u64 wall_dt;
	// dt_s = dt / AU_TICK_FREQ
	double anim_dt_s;
	double wall_dt_s;
} Input_State;

enum au_scene_type {
	AU_SCENE_TYPE_SCENE = -1,
	AU_SCENE_TYPE_WINDOW = 1,
};

struct window;

// Todo: type -> flags. flags window type, y_up.
typedef struct scene {
	i32 type;
	i32 w, h, x, y, context;
	struct scene *parent;
	struct window *window;
} Scene;

enum au_input_recording_mode_e {
	AU_INPUT_MODE_NORMAL,
	AU_INPUT_MODE_RECORDING,
	AU_INPUT_MODE_PLAYBACK,
	AU_INPUT_MODE_COUNT
};

typedef struct window {
	Scene sc;
	// beginning of line to beginning of next line, in pixels
	u32 stride;
	u32 rows;
	Arena *arena;
	// user data
	void *udata;
	// set by window_create: ui element sizes specified in pixels should be multiplied this.
	// When it changes, rescale_callback will be called if it exists
	float scale;
   struct window *parent;
   Input_State *input;
   i8 input_mode;
   double animation_rate;
	// optional
	// if a resize requires a new buffer, this is called before resize_callback. old_buffers
	// is either one or two pointers depending on double_buffer
	void (*restride_callback)(struct window *, u32 **old_buffers, i32 old_stride,
		i32 old_rows);
	void (*resize_callback)(struct window *, i32 old_width, i32 old_height);
	void (*rescale_callback)(struct window *, float old_scale);
	void (*mouse_move_callback)(struct window *, i32 x, i32 y);
	void (*mouse_leave_callback)(struct window *);
	void (*mouse_press_callback)(struct window *, int mouse_n, bool down);
	void (*mouse_scroll_callback)(struct window *, float amount);
	void (*key_press_callback)(struct window *, enum au_key key, bool down);
	// Used by draw_cpu.c and draw_opengl.c. window_create initializes to NULL.
	void *renderer_data;
	// This is called on commit_changes, in draw_opengl it's used to call glDrawArrays, etc.
	void (*renderer_callback)(struct window *);
	bool double_buffer;
	bool running;
	// CPU only. If double_buffer is enabled, this is the back(drawing) buffer.
	u32 *pixels;
	// CPU only. If double_buffer is enabled, this is the front buffer. Otherwise, it's null.
	u32 *front_buffer;
} Window;

// If you know the backend will be Native/CPU, you can cast this to a Window *; if you know the
// backend is SDL, you can cast to an SDL_Window *
Window *window_create(Arena *arena, u32 width, u32 height, char *title, bool double_buffer);

// Reset input state, then listen for and dispatch events. If milliseconds < 0, run indefinitely
void window_run(Window *window, float milliseconds);

// Reset input state and flush event queue, updating input state
void update_input_state(Window *window);

// In double buffered mode, swap the front and back buffers, then commit the new front buffer
// In single buffered mode, commit the buffer
void commit_changes(Window *window);

bool au_window_set_cursor(Window *window, i32 cursor);

// Set the window's icon from an encoded image in memory. Format (PNG, JPG, BMP,
// GIF, TGA, ...) is auto-detected. Returns false on decode/creation failure.
// Currently implemented on the Windows backend only.
bool au_window_set_icon(Window *window, const void *data, u64 length);

// XX Sounds should not be tied to windows, they should be global, maybe in au_platform...
i32 au_window_load_sound(Window *window, const char *path, const char *type);

i32 au_window_load_sound_from_memory(Window *window, const void *data, u64 length,
	const char *type);

void au_window_play_sound(Window *window, i32 sound_i);

void window_close(Window *window);

void window_delete(Window *window);

bool au_window_record_input(Window *window, const char *out_path);

bool au_window_stop_recording(Window *window);

bool au_window_playback_input(Window *window, const char *in_path);

bool au_window_stop_playback(Window *window);

/******** Native/CPU window only API ************/

// In both modes, commit the front buffer
void commit_changes_noswap(Window *window);

Window *window_create_child(Arena *arena, Window *parent, u32 width,
    u32 height, char *title, bool double_buffer);

#ifdef __cplusplus
}
#endif

#endif
