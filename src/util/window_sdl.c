#include <SDL3/SDL.h>
#include <string.h>

#include "SDL3/SDL_audio.h"
#include "au_core.h"
#include "au_containers.h"
#include "au_window.h"
#include "au_platform.h"
#include "au_window_sdl.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define VC_EXTRALEAN
#include <windows.h>
#ifdef _MSC_VER
// These libraries are required by SDL when statically linking; listing them here prevents the user from
// having to list them on the cl command line.
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "imm32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "version.lib")
#pragma comment(lib, "uuid.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "dinput8.lib")
// #pragma comment(lib, "dxguid.lib")
// #pragma comment(lib, "ws2_32.lib")
#endif
#endif

#include <glad/glad.h>

enum au_key g_keycodes[512];
bool g_keycodes_initialized = false;
SDL_Cursor *g_cursor_handles[AU_MAX_CURSORS];

static void init_keycodes() {
	g_keycodes_initialized = true;
    g_keycodes[SDL_SCANCODE_0] = AU_KEY_0; g_keycodes[SDL_SCANCODE_1] = AU_KEY_1; g_keycodes[SDL_SCANCODE_2] = AU_KEY_2;
    g_keycodes[SDL_SCANCODE_1] = AU_KEY_3; g_keycodes[SDL_SCANCODE_4] = AU_KEY_4; g_keycodes[SDL_SCANCODE_5] = AU_KEY_5;
    g_keycodes[SDL_SCANCODE_6] = AU_KEY_6; g_keycodes[SDL_SCANCODE_7] = AU_KEY_7; g_keycodes[SDL_SCANCODE_8] = AU_KEY_8;
    g_keycodes[SDL_SCANCODE_9] = AU_KEY_9; g_keycodes[SDL_SCANCODE_A] = AU_KEY_A; g_keycodes[SDL_SCANCODE_B] = AU_KEY_B;
    g_keycodes[SDL_SCANCODE_C] = AU_KEY_C; g_keycodes[SDL_SCANCODE_D] = AU_KEY_D; g_keycodes[SDL_SCANCODE_E] = AU_KEY_E;
    g_keycodes[SDL_SCANCODE_F] = AU_KEY_F; g_keycodes[SDL_SCANCODE_G] = AU_KEY_G; g_keycodes[SDL_SCANCODE_H] = AU_KEY_H;
    g_keycodes[SDL_SCANCODE_I] = AU_KEY_I; g_keycodes[SDL_SCANCODE_J] = AU_KEY_J; g_keycodes[SDL_SCANCODE_K] = AU_KEY_K;
    g_keycodes[SDL_SCANCODE_L] = AU_KEY_L; g_keycodes[SDL_SCANCODE_M] = AU_KEY_M; g_keycodes[SDL_SCANCODE_N] = AU_KEY_N;
    g_keycodes[SDL_SCANCODE_O] = AU_KEY_O; g_keycodes[SDL_SCANCODE_P] = AU_KEY_P; g_keycodes[SDL_SCANCODE_Q] = AU_KEY_Q;
    g_keycodes[SDL_SCANCODE_R] = AU_KEY_R; g_keycodes[SDL_SCANCODE_S] = AU_KEY_S; g_keycodes[SDL_SCANCODE_T] = AU_KEY_T;
    g_keycodes[SDL_SCANCODE_U] = AU_KEY_U; g_keycodes[SDL_SCANCODE_V] = AU_KEY_V; g_keycodes[SDL_SCANCODE_W] = AU_KEY_W;
    g_keycodes[SDL_SCANCODE_X] = AU_KEY_X; g_keycodes[SDL_SCANCODE_Y] = AU_KEY_Y; g_keycodes[SDL_SCANCODE_Z] = AU_KEY_Z;

    g_keycodes[SDL_SCANCODE_APOSTROPHE] = AU_KEY_APOSTROPHE;    g_keycodes[SDL_SCANCODE_BACKSLASH] = AU_KEY_BACKSLASH;
    g_keycodes[SDL_SCANCODE_COMMA] = AU_KEY_COMMA;         g_keycodes[SDL_SCANCODE_EQUALS] = AU_KEY_EQUAL;
    g_keycodes[SDL_SCANCODE_GRAVE] = AU_KEY_GRAVE_ACCENT;  g_keycodes[SDL_SCANCODE_LEFTBRACKET] = AU_KEY_LEFT_BRACKET;
    g_keycodes[SDL_SCANCODE_MINUS] = AU_KEY_MINUS;         g_keycodes[SDL_SCANCODE_PERIOD] = AU_KEY_PERIOD;
    g_keycodes[SDL_SCANCODE_BACKSLASH] = AU_KEY_RIGHT_BRACKET; g_keycodes[SDL_SCANCODE_SEMICOLON] = AU_KEY_SEMICOLON;
    g_keycodes[SDL_SCANCODE_SLASH] = AU_KEY_SLASH;//         g_keycodes[0x056] = AU_KEY_WORLD_2;

    g_keycodes[SDL_SCANCODE_BACKSPACE] = AU_KEY_BACKSPACE;     g_keycodes[SDL_SCANCODE_DELETE] = AU_KEY_DELETE;
    g_keycodes[SDL_SCANCODE_END] = AU_KEY_END;           g_keycodes[SDL_SCANCODE_RETURN] = AU_KEY_ENTER;
    g_keycodes[SDL_SCANCODE_ESCAPE] = AU_KEY_ESCAPE;        g_keycodes[SDL_SCANCODE_HOME] = AU_KEY_HOME;
    g_keycodes[SDL_SCANCODE_INSERT] = AU_KEY_INSERT;        g_keycodes[SDL_SCANCODE_MENU] = AU_KEY_MENU;
    g_keycodes[SDL_SCANCODE_PAGEDOWN] = AU_KEY_PAGE_DOWN;     g_keycodes[SDL_SCANCODE_PAGEUP] = AU_KEY_PAGE_UP;
    g_keycodes[SDL_SCANCODE_PAUSE] = AU_KEY_PAUSE;
    g_keycodes[SDL_SCANCODE_SPACE] = AU_KEY_SPACE;         g_keycodes[SDL_SCANCODE_TAB] = AU_KEY_TAB;
    g_keycodes[SDL_SCANCODE_CAPSLOCK] = AU_KEY_CAPS_LOCK;     g_keycodes[SDL_SCANCODE_NUMLOCKCLEAR] = AU_KEY_NUM_LOCK;
    g_keycodes[SDL_SCANCODE_SCROLLLOCK] = AU_KEY_SCROLL_LOCK;   g_keycodes[SDL_SCANCODE_F1] = AU_KEY_F1;
    g_keycodes[SDL_SCANCODE_F2] = AU_KEY_F2;            g_keycodes[SDL_SCANCODE_F3] = AU_KEY_F3;
    g_keycodes[SDL_SCANCODE_F4] = AU_KEY_F4;            g_keycodes[SDL_SCANCODE_F5] = AU_KEY_F5;
    g_keycodes[SDL_SCANCODE_F6] = AU_KEY_F6;            g_keycodes[SDL_SCANCODE_F7] = AU_KEY_F7;
    g_keycodes[SDL_SCANCODE_F8] = AU_KEY_F8;            g_keycodes[SDL_SCANCODE_F9] = AU_KEY_F9;
    g_keycodes[SDL_SCANCODE_F10] = AU_KEY_F10;           g_keycodes[SDL_SCANCODE_F11] = AU_KEY_F11;
    g_keycodes[SDL_SCANCODE_F12] = AU_KEY_F12;           g_keycodes[SDL_SCANCODE_F13] = AU_KEY_F13;
    g_keycodes[SDL_SCANCODE_F14] = AU_KEY_F14;           g_keycodes[SDL_SCANCODE_F15] = AU_KEY_F15;
    g_keycodes[SDL_SCANCODE_F16] = AU_KEY_F16;           g_keycodes[SDL_SCANCODE_F17] = AU_KEY_F17;
    g_keycodes[SDL_SCANCODE_F18] = AU_KEY_F18;           g_keycodes[SDL_SCANCODE_F19] = AU_KEY_F19;
    g_keycodes[SDL_SCANCODE_F20] = AU_KEY_F20;           g_keycodes[SDL_SCANCODE_F21] = AU_KEY_F21;
    g_keycodes[SDL_SCANCODE_F22] = AU_KEY_F22;           g_keycodes[SDL_SCANCODE_F23] = AU_KEY_F23;
    g_keycodes[SDL_SCANCODE_F24] = AU_KEY_F24;           g_keycodes[SDL_SCANCODE_LALT] = AU_KEY_LEFT_ALT;
    g_keycodes[SDL_SCANCODE_LCTRL] = AU_KEY_LEFT_CONTROL;  g_keycodes[SDL_SCANCODE_LSHIFT] = AU_KEY_LEFT_SHIFT;
    g_keycodes[SDL_SCANCODE_LGUI] = AU_KEY_LEFT_SUPER;    g_keycodes[SDL_SCANCODE_PRINTSCREEN] = AU_KEY_PRINT_SCREEN;
    g_keycodes[SDL_SCANCODE_RALT] = AU_KEY_RIGHT_ALT;     g_keycodes[SDL_SCANCODE_RCTRL] = AU_KEY_RIGHT_CONTROL;
    g_keycodes[SDL_SCANCODE_RSHIFT] = AU_KEY_RIGHT_SHIFT;   g_keycodes[SDL_SCANCODE_RGUI] = AU_KEY_RIGHT_SUPER;
    g_keycodes[SDL_SCANCODE_DOWN] = AU_KEY_DOWN;          g_keycodes[SDL_SCANCODE_LEFT] = AU_KEY_LEFT;
    g_keycodes[SDL_SCANCODE_RIGHT] = AU_KEY_RIGHT;         g_keycodes[SDL_SCANCODE_UP] = AU_KEY_UP;

    g_keycodes[SDL_SCANCODE_KP_0] = AU_KEY_KP_0;          g_keycodes[SDL_SCANCODE_KP_1] = AU_KEY_KP_1;
    g_keycodes[SDL_SCANCODE_KP_2] = AU_KEY_KP_2;          g_keycodes[SDL_SCANCODE_KP_3] = AU_KEY_KP_3;
    g_keycodes[SDL_SCANCODE_KP_4] = AU_KEY_KP_4;          g_keycodes[SDL_SCANCODE_KP_5] = AU_KEY_KP_5;
    g_keycodes[SDL_SCANCODE_KP_6] = AU_KEY_KP_6;          g_keycodes[SDL_SCANCODE_KP_7] = AU_KEY_KP_7;
    g_keycodes[SDL_SCANCODE_KP_8] = AU_KEY_KP_8;          g_keycodes[SDL_SCANCODE_KP_9] = AU_KEY_KP_9;
    g_keycodes[SDL_SCANCODE_KP_PLUS] = AU_KEY_KP_ADD;        g_keycodes[SDL_SCANCODE_KP_DECIMAL] = AU_KEY_KP_DECIMAL;
    g_keycodes[SDL_SCANCODE_KP_DIVIDE] = AU_KEY_KP_DIVIDE;     g_keycodes[SDL_SCANCODE_KP_ENTER] = AU_KEY_KP_ENTER;
    g_keycodes[SDL_SCANCODE_KP_MULTIPLY] = AU_KEY_KP_MULTIPLY;   g_keycodes[SDL_SCANCODE_MINUS] = AU_KEY_KP_SUBTRACT;
}

// SDL Window TODOs:
// mutli sample antialiasing not working
Window *window_create(Arena *arena, u32 w, u32 h, char *title, bool double_buffer)
{
	assertf(double_buffer, "window_create: With SDL backend double_buffer must be true.\n");
	AU_SDL_Window *win = aalloc(arena, sizeof(AU_SDL_Window));
	win->w.sc.type = 1;
	win->w.arena = arena;
	win->w.sc.x = 0;
	win->w.sc.y = 0;
	win->w.sc.parent = NULL;
	win->w.sc.window = (Window *) win;
	win->w.input = aalloc(arena, sizeof(Input_State));
	win->w.input->text_entered = (Char_Dynarray *) dynarray_create(arena, sizeof(char));
	win->w.stride = w;
	win->w.rows = h;
	win->w.renderer_data = NULL;
	win->w.renderer_callback = NULL;
	win->w.pixels = NULL;
	win->w.front_buffer = NULL;
	win->w.double_buffer = double_buffer;

	if (!g_keycodes_initialized)
		init_keycodes();

#ifdef __linux__
	// On wayland, default to XWayland over native wayland. When native wayland is selected and libdecor
	// is active, my tests show cursor capture is broken (A pointer up event is sent when leaving
	// the window).
	SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "x11,wayland");
	if (!SDL_Init(SDL_INIT_VIDEO)) {
		// Neither worked; let SDL try its full driver list.
		SDL_ResetHint(SDL_HINT_VIDEO_DRIVER);
		SDL_Init(SDL_INIT_VIDEO);
	}
#else
	SDL_Init(SDL_INIT_VIDEO);
#endif

	// Some drivers apparently need window sizes passed to SDL_CreateWindow to be in pixels, not logical
	// units, so pre-scale on those platforms.
	const char *video_driver = SDL_GetCurrentVideoDriver();
	if (video_driver && (strcmp(video_driver, "x11") == 0 || strcmp(video_driver, "windows") == 0)) {
		float start_scale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
		if (start_scale > 0.0f) {
			w = SDL_roundf(w * start_scale);
			h = SDL_roundf(h * start_scale);
		}
	}

	win->w.sc.w = w;
	win->w.sc.h = h;

	win->w.animation_rate = 1.0;
	win->w.input->start_time = au_os_time();
	win->w.input->wall_time = 0;
	win->w.input->anim_time = 0;
	win->w.input->wall_dt = 0;
	win->w.input->anim_dt = 0;
	win->w.input->wall_dt_s = 0.0;
	win->w.input->anim_dt_s = 0.0;

	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    // xx ?
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
	SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
	SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 4);

	win->sdl_window = SDL_CreateWindow(title, w, h,
		SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
	SDL_GLContext context = SDL_GL_CreateContext(win->sdl_window);
	// SDL_GL_MakeCurrent(win->sdl_window, context);
	gladLoadGLLoader((GLADloadproc) SDL_GL_GetProcAddress);

	SDL_GL_SetSwapInterval(1);
	// /* Don’t trust drawable size immediately on Wayland SDL2 */
	// SDL_GL_SwapWindow(win->sdl_window);

	// SDL_Event ev;
	// while (SDL_PollEvent(&ev)) {
	//     /* consume initial configure/resize events */
	// }

	SDL_StartTextInput(win->sdl_window);

	glEnable(GL_MULTISAMPLE);
	glDisable(GL_DEPTH_TEST);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	i32 drawable_w, drawable_h, window_w, window_h;
	SDL_GetWindowSizeInPixels(win->sdl_window, &drawable_w, &drawable_h);
	SDL_GetWindowSize(win->sdl_window, &window_w, &window_h);

	win->w.sc.w = drawable_w;
	win->w.sc.h = drawable_h;
	win->sdl_window_coords_to_pixels = (float) drawable_w / window_w;
	win->w.scale = SDL_GetWindowDisplayScale(win->sdl_window);
	glViewport(0, 0, drawable_w, drawable_h);

	return (Window *) win;
}

static void input_begin_frame(Input_State *input) {
	input->quit = false;

	input->window_resized = false;
	input->window_rescaled = false;
	input->focus_gained = false;
	input->focus_lost = false;

	memset(input->key_pressed,  0, sizeof(input->key_pressed));
	memset(input->key_released, 0, sizeof(input->key_released));
	memset(input->key_repeated, 0, sizeof(input->key_repeated));

	input->pointer_dx = 0;
	input->pointer_dy = 0;

	memset(input->mouse_pressed,  0, sizeof(input->mouse_pressed));
	memset(input->mouse_released, 0, sizeof(input->mouse_released));

	input->wheel_dx = 0.0f;
	input->wheel_dy = 0.0f;
//  in->wheel_is_precise = false;

	input->text_entered->length = 0;
}

static int mouse_button_index(uint8_t sdl_button) {
	switch (sdl_button) {
	case SDL_BUTTON_LEFT:   return AU_MOUSE_BUTTON_LEFT;
	case SDL_BUTTON_RIGHT:  return AU_MOUSE_BUTTON_RIGHT;
	case SDL_BUTTON_MIDDLE: return AU_MOUSE_BUTTON_MIDDLE;
	case SDL_BUTTON_X1:     return AU_MOUSE_BUTTON_X1;
	case SDL_BUTTON_X2:     return AU_MOUSE_BUTTON_X2;
	default:                return -1;
	}
}

static void append_text(Input_State *input, const char *utf8) {
	if (!utf8) return;
	i32 n = (i32)strlen(utf8);
	if (n <= 0) return;

	Char_Dynarray *text_entered = input->text_entered;
	// Add one by one because most of the time this will be a single character.
	for (i32 i=0; i<n; i++) {
		char c = utf8[i];
		dynarray_add(text_entered, &c);
	}
}

static void input_handle_event(AU_SDL_Window *win, Input_State *input, const SDL_Event *e) {
 	switch (e->type) {
	case SDL_EVENT_QUIT:
		input->quit = true;
		break;

 	// TODO: Restride?? Rescale???
	case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
	case SDL_EVENT_WINDOW_RESIZED: {
		i32 drawable_w, drawable_h, window_w, window_h;
		SDL_GetWindowSizeInPixels(win->sdl_window, &drawable_w, &drawable_h);
		SDL_GetWindowSize(win->sdl_window, &window_w, &window_h);

		input->window_resized = true;
		i32 old_width = win->w.sc.w;
		i32 old_height = win->w.sc.h;
		win->w.sc.w = drawable_w;
		win->w.sc.h = drawable_h;
		win->sdl_window_coords_to_pixels = (float) drawable_w / window_w;
		win->w.scale = SDL_GetWindowDisplayScale(win->sdl_window);
		glViewport(0, 0, drawable_w, drawable_h);

		if (win->w.resize_callback)
			win->w.resize_callback((Window *) win, old_width, old_height);
	} break;
	case SDL_EVENT_WINDOW_FOCUS_GAINED:
		input->focus_gained = true;
	break;

	case SDL_EVENT_WINDOW_FOCUS_LOST:
		input->focus_lost = true;
	break;
	case SDL_EVENT_KEY_DOWN: {
		SDL_Scancode sc = e->key.scancode;
		if (sc >= 0 && sc < SDL_SCANCODE_COUNT) {
			enum au_key k = g_keycodes[sc];
			input->key_pressed[k] = true;
			if (e->key.repeat)
				input->key_repeated[k] = true;
			input->key_down[k] = true;

			if (win->w.key_press_callback)
				win->w.key_press_callback((Window *) win, k, true);
		}
//		input->mods = SDL_GetModState();
	} break;

	case SDL_EVENT_KEY_UP: {
		SDL_Scancode sc = e->key.scancode;
		if (sc >= 0 && sc < SDL_SCANCODE_COUNT) {
			enum au_key k = g_keycodes[sc];
			if (input->key_down[k]) {
				input->key_released[k] = true;
			}
			input->key_down[k] = false;

			if (win->w.key_press_callback)
				win->w.key_press_callback((Window *) win, k, false);
		}
//		input->mods = SDL_GetModState();
	} break;

	case SDL_EVENT_TEXT_INPUT:
		// Committed text input, possibly multiple UTF-8 codepoints.
		append_text(input, e->text.text);
	break;

	case SDL_EVENT_MOUSE_MOTION:
		i32 old_x = input->pointer_x;
		i32 old_y = input->pointer_y;
		input->pointer_x = e->motion.x * win->sdl_window_coords_to_pixels;
	 	input->pointer_y = e->motion.y * win->sdl_window_coords_to_pixels;
		input->pointer_dx += input->pointer_x - old_x;
		input->pointer_dy += input->pointer_y - old_y;

		if (win->w.mouse_move_callback)
			win->w.mouse_move_callback((Window *) win, e->motion.x, e->motion.y);
	break;

	case SDL_EVENT_MOUSE_BUTTON_DOWN: {
		int idx = mouse_button_index(e->button.button);
		if (idx >= 0 && idx < AU_MOUSE_BUTTON_COUNT) {
			if (!input->mouse_down[idx])
				input->mouse_pressed[idx] = true;
			input->mouse_down[idx] = true;
		}
		input->pointer_x = e->button.x * win->sdl_window_coords_to_pixels;
		input->pointer_y = e->button.y * win->sdl_window_coords_to_pixels;

		if (win->w.mouse_press_callback)
			win->w.mouse_press_callback((Window *) win, idx, true);
	} break;

	case SDL_EVENT_MOUSE_BUTTON_UP: {
		int idx = mouse_button_index(e->button.button);
		if (idx >= 0 && idx < AU_MOUSE_BUTTON_COUNT) {
			if (input->mouse_down[idx]) input->mouse_released[idx] = true;
			input->mouse_down[idx] = false;
		}
		input->pointer_x = e->button.x * win->sdl_window_coords_to_pixels;
		input->pointer_y = e->button.y * win->sdl_window_coords_to_pixels;

		if (win->w.mouse_press_callback)
			win->w.mouse_press_callback((Window *) win, idx, false);
	} break;

	case SDL_EVENT_MOUSE_WHEEL: {
		float wx = (float)e->wheel.x;
		float wy = (float)e->wheel.y;
		if (e->wheel.direction == SDL_MOUSEWHEEL_FLIPPED) {
			wx = -wx;
			wy = -wy;
		}
		input->wheel_dx += wx;
		input->wheel_dy += wy;

		if (win->w.mouse_scroll_callback)
			win->w.mouse_scroll_callback((Window *) win, wy);
	} break;

	default:
	break;
 	}
}

// window_common.c
bool input_state_read_and_deserialize(Window *window);
bool input_state_serialize_and_write(Window *window);

void update_input_state(Window *window)
{
	AU_SDL_Window *win = (AU_SDL_Window *) window;
	Input_State *input = win->w.input;
	u64 last_wall_time = input->wall_time;
	input_begin_frame(input);
	if (window->input_mode == AU_INPUT_MODE_PLAYBACK) {
		input_state_read_and_deserialize(window);
	} else {
		SDL_Event e;
		while (SDL_PollEvent(&e)) {
			input_handle_event(win, input, &e);
		}

		input->wall_time = au_os_time() - input->start_time;
		input->wall_dt = input->wall_time - last_wall_time;
		input->anim_dt = input->wall_dt * win->w.animation_rate;
		input->anim_time += input->anim_dt;
		input->wall_dt_s = input->wall_dt / (double) AU_TICK_FREQ;
		input->anim_dt_s = input->anim_dt / (double) AU_TICK_FREQ;
		if (window->input_mode == AU_INPUT_MODE_RECORDING) {
			input_state_serialize_and_write(window);
		}
	}
}

void wait_and_update_input_state(Window *window)
{
	AU_SDL_Window *win = (AU_SDL_Window *) window;
	Input_State *input = win->w.input;
	u64 last_wall_time = input->wall_time;
	input_begin_frame(input);
	if (window->input_mode == AU_INPUT_MODE_PLAYBACK) {
		input_state_read_and_deserialize(window);
	} else {
		bool got_pending_events = false;
		SDL_Event e;
		while (SDL_PollEvent(&e)) {
			got_pending_events = true;
			input_handle_event(win, input, &e);
		}
		if (!got_pending_events) {
			if (SDL_WaitEvent(&e)) {
				input_handle_event(win, input, &e);
				while (SDL_PollEvent(&e))
					input_handle_event(win, input, &e);
			}
		}

		input->wall_time = au_os_time() - input->start_time;
		input->wall_dt = input->wall_time - last_wall_time;
		input->anim_dt = input->wall_dt * win->w.animation_rate;
		input->anim_time += input->anim_dt;
		input->wall_dt_s = input->wall_dt / (double) AU_TICK_FREQ;
		input->anim_dt_s = input->anim_dt / (double) AU_TICK_FREQ;
		if (window->input_mode == AU_INPUT_MODE_RECORDING) {
			input_state_serialize_and_write(window);
		}
	}
}

void run_window(Window *window, float milliseconds)
{
	// TODO
}

void commit_changes(Window *window)
{
	AU_SDL_Window *win = (AU_SDL_Window *) window;
	if (win->w.renderer_callback)
		win->w.renderer_callback(window);
	SDL_GL_SwapWindow(win->sdl_window);
}

bool au_window_set_cursor(Window *window, i32 cursor)
{
    if (cursor >= AU_MAX_CURSORS) {
        return false;
    } else if (cursor < AU_CURSOR_COUNT) {
        if (g_cursor_handles[cursor]) {
            SDL_SetCursor(g_cursor_handles[cursor]);
            return true;
        } else {
            SDL_Cursor *result;
            switch (cursor) {
            case AU_CURSOR_NORMAL:
                result = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_DEFAULT);
            break;
            case AU_CURSOR_HORIZONTAL_RESIZE:
                result = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_EW_RESIZE);
            break;
            case AU_CURSOR_VERTICAL_RESIZE:
                result = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_NS_RESIZE);
            break;
            case AU_CURSOR_FORWARD_DIAGONAL_RESIZE:
                result = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_NESW_RESIZE);
            break;
            case AU_CURSOR_BACKWARD_DIAGONAL_RESIZE:
                result = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_NWSE_RESIZE);
            break;
            case AU_CURSOR_HAND:
                result = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_POINTER);
            break;
            default:
                result = NULL;
            break;
            }
            if (result) {
                g_cursor_handles[cursor] = result;
                SDL_SetCursor(result);
                return true;
            } else {
                return false;
            }
        }
    } else {
        if (g_cursor_handles[cursor]) {
            SDL_SetCursor(g_cursor_handles[cursor]);
            return true;
        } else {
            return false;
        }
    }
}

static bool setup_audio_stream(AU_SDL_Window *win, SDL_AudioSpec *initial_spec)
{
	win->audio_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
			initial_spec, NULL, NULL);
	if (!win->audio_stream) {
		// errexit("Failed to open audio stream\n");
		return false;
	}
	bool result = SDL_ResumeAudioStreamDevice(win->audio_stream);
	if (!result) {
		// errexit("Failed to resume audio stream\n");
		return false;
	}
	return true;
}

i32 au_window_load_sound(Window *window, const char *path, const char *type)
{
	AU_SDL_Window *win = (AU_SDL_Window *) window;
	if (!win->audio_buffers) {
		SDL_Init(SDL_INIT_AUDIO);
		win->audio_buffers = (AU_SDL_Audio_Buffer_Dynarray *) dynarray_create(window->arena,
			sizeof(AU_SDL_Audio_Buffer));
	}
	if (!strcmp(type, "wav")) {
		AU_SDL_Audio_Buffer abuf;
		bool res = SDL_LoadWAV(path, &abuf.spec, &abuf.buf, &abuf.len);
		if (res) {
			if (!win->audio_stream) {
				res = setup_audio_stream(win, &abuf.spec);
				if (!res)
					return -1;
			}
			i32 ret = win->audio_buffers->length;
			dynarray_add(win->audio_buffers, &abuf);
			return ret;
		} else {
			return -1;
		}
	} else {
		return -1;
	}
}

i32 au_window_load_sound_from_memory(Window *window, const void *data, u64 length,
	const char *type)
{
	AU_SDL_Window *win = (AU_SDL_Window *) window;
	if (!win->audio_buffers) {
		SDL_Init(SDL_INIT_AUDIO);
		win->audio_buffers = (AU_SDL_Audio_Buffer_Dynarray *) dynarray_create(window->arena,
			sizeof(AU_SDL_Audio_Buffer));
	}

	if (!strcmp(type, "wav")) {
		AU_SDL_Audio_Buffer abuf;
		SDL_IOStream *io = SDL_IOFromConstMem(data, length);
		bool res = SDL_LoadWAV_IO(io, true, &abuf.spec, &abuf.buf, &abuf.len);
		if (res) {
			if (!win->audio_stream) {
				res = setup_audio_stream(win, &abuf.spec);
				if (!res)
					return -1;
			}
			i32 ret = win->audio_buffers->length;
			dynarray_add(win->audio_buffers, &abuf);
			return ret;
		} else {
			return -1;
		}
	} else {
		return -1;
	}
}

void au_window_play_sound(Window *window, i32 sound_i)
{
	AU_SDL_Window *win = (AU_SDL_Window *) window;
	assertf(sound_i >= 0 && sound_i < win->audio_buffers->length, "Invalid audio buffer: %d\n",
		sound_i);
	AU_SDL_Audio_Buffer *buf = &win->audio_buffers->d[sound_i];
	if (!win->audio_stream) {
		return;
	} else {
		bool result = SDL_SetAudioStreamFormat(win->audio_stream, NULL, &buf->spec);
		if (!result) {
			// errexit("Failed to set audio stream spec\n");
			return;
		}
	}
	SDL_PutAudioStreamData(win->audio_stream, buf->buf, buf->len);
	SDL_FlushAudioStream(win->audio_stream);
}

void window_close(Window *window)
{
	// TODO
}

void window_delete(Window *window)
{
	// TODO
}
