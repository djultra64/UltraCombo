// Display output: OpenGL with filter shaders, or plain SDL rendering as a fallback.
#pragma once
#include <SDL2/SDL.h>
#include <stdbool.h>
#include <stdint.h>

enum { FILTER_SHARP, FILTER_SMOOTH, FILTER_SCANLINES, FILTER_CRT, NFILTERS };

// Create the window. Uses OpenGL when available.
SDL_Window *video_open(const char *title, int w, int h);
void video_set_vsync(bool on);
// game: 320x240 0x00RRGGBB. overlay: 320x240 0xAARRGGBB drawn on top, unfiltered.
void video_present(const uint32_t *game, const uint32_t *overlay, int filter, int scale_mode);
const char *video_backend(void);
void video_close(void);
