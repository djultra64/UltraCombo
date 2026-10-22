// Read the composed frame before presentation, using synthetic pixels only.
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL2/SDL.h>
static SDL_Surface *capture;
static void capture_gl(SDL_Window *window);
static void capture_sdl(SDL_Renderer *renderer);
#define SDL_GL_SwapWindow capture_gl
#define SDL_RenderPresent capture_sdl
#include "../src/video.c"
#undef SDL_GL_SwapWindow
#undef SDL_RenderPresent

static void capture_gl(SDL_Window *window)
{
    typedef void (APIENTRY *read_pixels_t)(GLint,GLint,GLsizei,GLsizei,GLenum,GLenum,void *);
    read_pixels_t read_pixels=(read_pixels_t)SDL_GL_GetProcAddress("glReadPixels");
    assert(read_pixels);
    int w,h; SDL_GL_GetDrawableSize(window,&w,&h);
    SDL_FreeSurface(capture);
    capture=SDL_CreateRGBSurfaceWithFormat(0,w,h,32,SDL_PIXELFORMAT_RGBA32);
    assert(capture);
    read_pixels(0,0,w,h,GL_RGBA,GL_UNSIGNED_BYTE,capture->pixels);
    Uint8 *row=malloc((size_t)capture->pitch); assert(row);
    for (int y=0;y<h/2;y++) {
        Uint8 *top=(Uint8 *)capture->pixels+y*capture->pitch;
        Uint8 *bottom=(Uint8 *)capture->pixels+(h-1-y)*capture->pitch;
        memcpy(row,top,capture->pitch); memcpy(top,bottom,capture->pitch); memcpy(bottom,row,capture->pitch);
    }
    free(row);
    SDL_GL_SwapWindow(window);
}
static void capture_sdl(SDL_Renderer *renderer)
{
    int w,h; SDL_GetRendererOutputSize(renderer,&w,&h);
    SDL_FreeSurface(capture);
    capture=SDL_CreateRGBSurfaceWithFormat(0,w,h,32,SDL_PIXELFORMAT_RGBA32);
    assert(capture && SDL_RenderReadPixels(renderer,NULL,SDL_PIXELFORMAT_RGBA32,capture->pixels,capture->pitch)==0);
    SDL_RenderPresent(renderer);
}
static Uint8 channel(int x,int y,int c)
{
    return ((Uint8 *)capture->pixels)[y*capture->pitch+4*x+c];
}
static void check_mode(int w,int h,int mode,const uint32_t *game,const uint32_t *over,int filter)
{
    SDL_SetWindowSize(win,w,h); SDL_PumpEvents();
    video_present(game,over,filter,mode,false);
    SDL_Surface *plain=SDL_ConvertSurface(capture,capture->format,0); assert(plain);
    video_present(game,over,filter,mode,true);
    SDL_Rect v=artwork_viewport(capture->w,capture->h,mode);
    // Every game/overlay pixel must match the artwork-off rendering exactly.
    for (int y=v.y;y<v.y+v.h;y++)
        assert(!memcmp((Uint8 *)plain->pixels+y*plain->pitch+v.x*4,
                       (Uint8 *)capture->pixels+y*capture->pitch+v.x*4,(size_t)v.w*4));
    if (v.x>20) {
        assert(channel(0,h/2,0)>240);
        assert(channel(v.x-1,h/2,0)<100);
        assert(channel(v.x-15,h/2,0)>channel(v.x-1,h/2,0));
    }
    if (v.y>20) assert(channel(w/2,0,0)>240);
    assert(game[0]==0x001060a0 && over[0]==0xffff0000);
    SDL_FreeSurface(plain);
}
int main(int argc,char **argv)
{
    assert(SDL_Init(SDL_INIT_VIDEO)==0);
    assert(video_open("Synthetic artwork test",960,540));
    if (argc>1 && !strcmp(argv[1],"sdl")) assert(!use_gl); else assert(use_gl);
    SDL_FreeSurface(art_source);
    art_source=SDL_CreateRGBSurfaceWithFormat(0,160,90,32,SDL_PIXELFORMAT_RGBA32); assert(art_source);
    SDL_FillRect(art_source,NULL,SDL_MapRGBA(art_source->format,255,255,255,255));
    uint32_t game[320*240],over[320*240]={0};
    for (int y=0;y<240;y++) for (int x=0;x<320;x++) game[y*320+x]=0x001060a0+(x%64)*0x10000+(y%64)*0x100;
    over[0]=0xffff0000;
    for (int filter=0;filter<NFILTERS;filter++) check_mode(960,540,SCALE_FIT,game,over,filter);
    check_mode(800,600,SCALE_INTEGER,game,over,FILTER_SHARP);
    check_mode(600,900,SCALE_FIT,game,over,FILTER_SHARP);
    check_mode(960,540,SCALE_STRETCH,game,over,FILTER_SHARP);
    check_mode(1280,720,SCALE_FIT,game,over,FILTER_SHARP);
    check_mode(3840,2160,SCALE_FIT,game,over,FILTER_SHARP);
    if (use_gl) {
        char err[512];
        assert(video_set_shader("shaders/crt/crt-lottes-multipass.glslp",err,sizeof err));
        check_mode(960,540,SCALE_FIT,game,over,FILTER_SHARP);
        video_set_shader(NULL,err,sizeof err);
    }
    // Real PNG loader, vertical orientation and cover cropping on both backends.
    SDL_FreeSurface(art_source); art_source=NULL;
    load_artwork(); assert(art_source && art_source->w==1672 && art_source->h==941);
    art_w=art_h=0;
    video_present(game,over,FILTER_SHARP,SCALE_FIT,true);
    SDL_Rect actual=artwork_viewport(capture->w,capture->h,SCALE_FIT);
    SDL_Surface *expected=artwork_compose(art_source,capture->w,capture->h,actual); assert(expected);
    for (int y=20;y<capture->h-20;y+=37) for (int x=10;x<actual.x-20;x+=11) {
        Uint8 *p=(Uint8 *)expected->pixels+y*expected->pitch+x*4;
        for (int c=0;c<3;c++) assert(abs(channel(x,y,c)-p[c])<=1);
    }
    SDL_FreeSurface(expected);
    // Missing/removed optional asset must leave functional black borders.
    SDL_FreeSurface(art_source); art_source=NULL;
    video_present(game,over,FILTER_SHARP,SCALE_FIT,true);
    assert(channel(0,capture->h/2,0)==0);
    puts(use_gl ? "OpenGL artwork composition: PASS" : "SDL artwork composition: PASS");
    video_close(); SDL_DestroyWindow(win); SDL_FreeSurface(capture); SDL_Quit();
}
