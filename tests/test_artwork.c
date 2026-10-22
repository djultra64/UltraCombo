// Synthetic presentation checks; no game files or display server required.
#include "artwork.h"
#include "config.h"
#include <assert.h>
#include <stdio.h>

static Uint8 red(SDL_Surface *s, int x, int y)
{
    Uint8 r, g, b, a;
    SDL_GetRGBA(((Uint32 *)((Uint8 *)s->pixels + y*s->pitch))[x], s->format, &r,&g,&b,&a);
    return r;
}
int main(void)
{
    SDL_Rect v = artwork_viewport(1920,1080,SCALE_FIT);
    assert(v.x==240 && v.y==0 && v.w==1440 && v.h==1080);
    v = artwork_viewport(1920,1080,SCALE_INTEGER);
    assert(v.x==320 && v.y==60 && v.w==1280 && v.h==960);
    v = artwork_viewport(600,900,SCALE_FIT);
    assert(v.x==0 && v.y==225 && v.w==600 && v.h==450);
    v = artwork_viewport(3840,2160,SCALE_FIT);
    assert(v.x==480 && v.w==2880 && v.h==2160);
    v = artwork_viewport(1920,1080,SCALE_STRETCH);
    assert(v.x==0 && v.w==1920 && v.h==1080);
    SDL_Surface *src=SDL_CreateRGBSurfaceWithFormat(0,160,90,32,SDL_PIXELFORMAT_RGBA32);
    assert(src);
    SDL_FillRect(src,NULL,SDL_MapRGBA(src->format,255,255,255,255));
    v = artwork_viewport(1920,1080,SCALE_FIT);
    SDL_Surface *out=artwork_compose(src,1920,1080,v);
    assert(out);
    assert(red(out,239,540)<100); // narrow contact shadow next to the game
    assert(red(out,225,540)>red(out,239,540));
    assert(red(out,200,540)==255); // fades out instead of a thick black border
    assert(red(out,240,540)==255); // never darken even the first game pixel
    assert(red(out,1080,540)==255);
    assert(red(out,1680,540)<100);
    assert(red(src,80,45)==255); // source artwork remains intact
    SDL_FreeSurface(out);
    v=artwork_viewport(800,600,SCALE_INTEGER);
    out=artwork_compose(src,800,600,v);
    assert(out && red(out,400,59)<150 && red(out,400,540)<150);
    SDL_FreeSurface(out);
    assert(!artwork_compose(src,0,0,v));
    SDL_FreeSurface(src);
    puts("artwork geometry and shadow checks passed");
}
