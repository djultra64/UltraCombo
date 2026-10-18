// The game picker (picker.h): a small window with a tile per game.
#include "picker.h"
#include "icon.h"
#include "osd.h"
#include <SDL2/SDL.h>
#include <stdio.h>
#include <string.h>

#define W 640
#define H 480
#define ICON 128

static uint32_t canvas[W * H], ov[320 * 240], icons[2][ICON * ICON];

static int tile_x(int i) { return i == 0 ? 160 - ICON / 2 : 480 - ICON / 2; }
#define TILE_Y 110

static void blend(uint32_t *d, uint32_t s)
{
    uint32_t a = s >> 24;
    if (!a) return;
    uint32_t r = ((s >> 16 & 255) * a + (*d >> 16 & 255) * (255 - a)) / 255;
    uint32_t g = ((s >> 8 & 255) * a + (*d >> 8 & 255) * (255 - a)) / 255;
    uint32_t b = ((s & 255) * a + (*d & 255) * (255 - a)) / 255;
    *d = r << 16 | g << 8 | b;
}

static void text(int x, int y, const char *s, uint32_t rgb)   // centred on x, in canvas pixels
{
    osd_text(ov, x / 2 - (int)strlen(s) * 3, y / 2, s, rgb);
}

static void draw(const game_t *const *games, const bool *installed, int sel)
{
    for (int y = 0; y < H; y++)                          // dark red-to-black backdrop
        for (int x = 0; x < W; x++) {
            uint32_t r = (uint32_t)(0x28 - 0x20 * y / H);
            canvas[y * W + x] = r << 16 | 0x040406;
        }
    osd_clear(ov);
    text(320, 40, "ULTRACOMBO", 0xffc040);
    text(320, 62, "CHOOSE A GAME", 0x909090);
    for (int i = 0; i < 2; i++) {
        int x0 = tile_x(i), y0 = TILE_Y;
        if (i == sel) {                                  // gold frame around the chosen one
            for (int y = y0 - 8; y < y0 + ICON + 8; y++)
                for (int x = x0 - 8; x < x0 + ICON + 8; x++)
                    if (y < y0 - 4 || y >= y0 + ICON + 4 || x < x0 - 4 || x >= x0 + ICON + 4)
                        canvas[y * W + x] = 0xd0a030;
        }
        for (int y = 0; y < ICON; y++)
            for (int x = 0; x < ICON; x++) {
                uint32_t p = icons[i][y * ICON + x];
                if (!installed[i]) p = (p & 0xff000000u) | ((p >> 2) & 0x3f3f3fu);   // dimmed
                blend(&canvas[(y0 + y) * W + x0 + x], p);
            }
        int cx = x0 + ICON / 2;
        text(cx, y0 + ICON + 24, games[i]->title, i == sel ? 0xffffff : 0xa0a0a0);
        text(cx, y0 + ICON + 44, games[i]->product, i == sel ? 0xffc040 : 0x806020);
        char vers[64] = "";
        for (int v = 0; v < 4 && games[i]->versions[v].label; v++)
            if (picker_version_there(games[i], v)) {
                if (*vers) strncat(vers, ", ", sizeof vers - strlen(vers) - 1);
                strncat(vers, games[i]->versions[v].label, sizeof vers - strlen(vers) - 1);
            }
        char line[96];
        if (installed[i]) snprintf(line, sizeof line, "READY: VERSION%s %s", strchr(vers, ',') ? "S" : "", vers);
        else snprintf(line, sizeof line, "FILES NOT FOUND");
        for (char *c = line; *c; c++) if (*c >= 'a' && *c <= 'z') *c -= 32;
        text(cx, y0 + ICON + 64, line, installed[i] ? 0x60c060 : 0xc05050);
    }
    text(320, 400, "LEFT/RIGHT CHOOSE   ENTER/A START   ESC QUIT", 0x808080);
    if (!installed[0] || !installed[1])
        text(320, 424, "A GAME'S FILES GO IN THE RUN FOLDER (SEE README)", 0x606060);
    for (int y = 0; y < H; y++)                          // the text, doubled
        for (int x = 0; x < W; x++) blend(&canvas[y * W + x], ov[(y / 2) * 320 + x / 2]);
}

const game_t *picker_choose(const game_t *const *games, const bool *installed, int initial)
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) return NULL;
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    SDL_Window *win = SDL_CreateWindow(PRODUCT, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, W, H, SDL_WINDOW_RESIZABLE);
    SDL_Renderer *r = win ? SDL_CreateRenderer(win, -1, SDL_RENDERER_PRESENTVSYNC) : NULL;
    if (!r && win) r = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    SDL_Texture *tex = r ? SDL_CreateTexture(r, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, W, H) : NULL;
    if (!tex) {
        fprintf(stderr, "picker: %s\n", SDL_GetError());
        if (r) SDL_DestroyRenderer(r);
        if (win) SDL_DestroyWindow(win);
        return NULL;
    }
    SDL_RenderSetLogicalSize(r, W, H);                   // keeps the aspect; mouse coordinates follow
    {
        static uint32_t small[64 * 64];
        icon_pixels_text(small, 64, "UC");
        SDL_Surface *s = SDL_CreateRGBSurfaceFrom(small, 64, 64, 32, 64 * 4, 0x00ff0000, 0x0000ff00, 0x000000ff, 0xff000000);
        if (s) { SDL_SetWindowIcon(win, s); SDL_FreeSurface(s); }
    }
    for (int i = 0; i < 2; i++) icon_pixels_text(icons[i], ICON, games[i]->icon_text);

    SDL_GameController *pads[8];
    int npads = 0;
    int sel = initial == 1 ? 1 : 0;
    if (!installed[sel]) sel = !sel;
    const game_t *chosen = NULL;
    bool done = false, axis_held = false;
    while (!done) {
        SDL_Event ev;
        int move = 0;
        bool go = false;
        while (SDL_PollEvent(&ev)) {
            switch (ev.type) {
            case SDL_QUIT: done = true; break;
            case SDL_CONTROLLERDEVICEADDED:
                if (npads < 8 && (pads[npads] = SDL_GameControllerOpen(ev.cdevice.which))) npads++;
                break;
            case SDL_KEYDOWN:
                switch (ev.key.keysym.sym) {
                case SDLK_LEFT: case SDLK_a: move = -1; break;
                case SDLK_RIGHT: case SDLK_d: move = 1; break;
                case SDLK_ESCAPE: done = true; break;
                }
                break;
            case SDL_KEYUP:                      // on release: the game's window mustn't get it as held
                switch (ev.key.keysym.sym) {
                case SDLK_1: sel = 0; go = true; break;
                case SDLK_2: sel = 1; go = true; break;
                case SDLK_RETURN: case SDLK_KP_ENTER: case SDLK_SPACE: go = true; break;
                }
                break;
            case SDL_CONTROLLERBUTTONDOWN:
                switch (ev.cbutton.button) {
                case SDL_CONTROLLER_BUTTON_DPAD_LEFT: move = -1; break;
                case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: move = 1; break;
                case SDL_CONTROLLER_BUTTON_A: case SDL_CONTROLLER_BUTTON_START: go = true; break;
                case SDL_CONTROLLER_BUTTON_B: case SDL_CONTROLLER_BUTTON_BACK: done = true; break;
                }
                break;
            case SDL_CONTROLLERAXISMOTION:
                if (ev.caxis.axis != SDL_CONTROLLER_AXIS_LEFTX) break;
                if (ev.caxis.value < -16000 || ev.caxis.value > 16000) {
                    if (!axis_held) move = ev.caxis.value < 0 ? -1 : 1;
                    axis_held = true;
                } else axis_held = false;
                break;
            case SDL_MOUSEMOTION: case SDL_MOUSEBUTTONDOWN: {
                int mx = ev.type == SDL_MOUSEMOTION ? ev.motion.x : ev.button.x;
                int my = ev.type == SDL_MOUSEMOTION ? ev.motion.y : ev.button.y;
                for (int i = 0; i < 2; i++)
                    if (installed[i] && mx >= tile_x(i) - 8 && mx < tile_x(i) + ICON + 8 && my >= TILE_Y - 8 && my < TILE_Y + ICON + 80) {
                        sel = i;
                        if (ev.type == SDL_MOUSEBUTTONDOWN && ev.button.button == SDL_BUTTON_LEFT) go = true;
                    }
                break;
            }
            }
        }
        if (move && installed[0] && installed[1]) sel = !sel;     // two tiles: either way swaps
        if (go && installed[sel]) { chosen = games[sel]; done = true; }
        draw(games, installed, sel);
        SDL_UpdateTexture(tex, NULL, canvas, W * 4);
        SDL_RenderClear(r);
        SDL_RenderCopy(r, tex, NULL, NULL);
        SDL_RenderPresent(r);
        SDL_Delay(10);
    }
    for (int i = 0; i < npads; i++) SDL_GameControllerClose(pads[i]);   // the game opens its own
    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(r);
    SDL_DestroyWindow(win);
    SDL_ResetKeyboard();                     // nothing counts as held in the game's window
    SDL_FlushEvents(SDL_FIRSTEVENT, SDL_LASTEVENT);
    return chosen;
}
