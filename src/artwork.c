#include "artwork.h"
#include "config.h"
#include <math.h>

SDL_Rect artwork_viewport(int w, int h, int scale_mode)
{
    SDL_Rect r = {0,0,w,h};
    if (w <= 0 || h <= 0 || scale_mode == SCALE_STRETCH) return r;
    int vw = w, vh = (int)((Sint64)w * 3 / 4);
    if (vh > h) { vh = h; vw = (int)((Sint64)h * 4 / 3); }
    if (scale_mode == SCALE_INTEGER && vw >= 320 && vh >= 240) {
        int k = SDL_min(vw / 320, vh / 240);
        vw = 320*k; vh = 240*k;
    }
    r.x=(w-vw)/2; r.y=(h-vh)/2; r.w=vw; r.h=vh;
    return r;
}

SDL_Surface *artwork_compose(SDL_Surface *source, int w, int h, SDL_Rect game)
{
    if (!source || w <= 0 || h <= 0 || w > 16384 || h > 16384) return NULL;
    SDL_Surface *out=SDL_CreateRGBSurfaceWithFormat(0,w,h,32,SDL_PIXELFORMAT_RGBA32);
    if (!out) return NULL;
    SDL_Rect crop={0,0,source->w,source->h};
    if ((Sint64)source->w*h > (Sint64)source->h*w) {
        crop.w=SDL_max(1,(int)((Sint64)source->h*w/h)); crop.x=(source->w-crop.w)/2;
    } else {
        crop.h=SDL_max(1,(int)((Sint64)source->w*h/w)); crop.y=(source->h-crop.h)/2;
    }
    if (SDL_BlitScaled(source,&crop,out,NULL)<0) { SDL_FreeSurface(out); return NULL; }
    // A tight contact edge and a wider soft shadow, measured in drawable pixels.
    // Offset slightly downwards to suggest light above the raised game panel.
    float unit=SDL_max(1,game.h)/1080.0f, sigma=8.0f*unit;
    float radius=32.0f*unit, contact=1.6f*unit, offset=3.0f*unit;
    for (int y=0;y<h;y++) for (int x=0;x<w;x++) {
        if (x>=game.x && x<game.x+game.w && y>=game.y && y<game.y+game.h) continue;
        float dx=SDL_max(SDL_max(game.x-x-0.5f,x+0.5f-game.x-game.w),0.0f);
        float dy=SDL_max(SDL_max(game.y+offset-y-0.5f,y+0.5f-game.y-game.h-offset),0.0f);
        if (dx>radius || dy>radius) continue;
        float d=sqrtf(dx*dx+dy*dy);
        if (d>radius) continue;
        float alpha=SDL_min(0.86f,0.64f*expf(-d/contact)+0.44f*expf(-d*d/(2*sigma*sigma)));
        Uint8 *p=(Uint8 *)out->pixels+y*out->pitch+x*4;
        for (int c=0;c<3;c++) p[c]=(Uint8)(p[c]*(1.0f-alpha)+0.5f);
    }
    return out;
}
