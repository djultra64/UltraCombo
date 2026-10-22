// Host-only backdrop composition; never touches game or overlay buffers.
#pragma once
#include <SDL2/SDL.h>
SDL_Rect artwork_viewport(int w, int h, int scale_mode);
// Cover-crop artwork without distortion, then cast a shadow outside the game.
// Caller owns the returned RGBA32 surface; NULL on invalid size/allocation failure.
SDL_Surface *artwork_compose(SDL_Surface *source, int w, int h, SDL_Rect game);
