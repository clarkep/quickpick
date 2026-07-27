#ifndef AU_WINDOW_SDL_H
#define AU_WINDOW_SDL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <SDL3/SDL.h>

#include "au_core.h"
#include "au_window.h"

typedef struct au_sdl_audio_buffer
{
	SDL_AudioSpec spec;
	u8 *buf;
	u32 len;
} AU_SDL_Audio_Buffer;

typedef struct au_sdl_audio_buffer_dynarray
{
	AU_SDL_Audio_Buffer *d;
	u64 length;
	u64 capacity;
	u64 item_size;
	Arena *arena;
} AU_SDL_Audio_Buffer_Dynarray;

typedef struct au_sdl_window {
	Window w;
	/* SDL specific */
	SDL_Window *sdl_window;
	float sdl_window_coords_to_pixels;
	AU_SDL_Audio_Buffer_Dynarray *audio_buffers;
	SDL_AudioStream *audio_stream;
} AU_SDL_Window;

#ifdef __cplusplus
}
#endif

#endif
