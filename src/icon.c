// Program icon: gold "KI2" on a dark red rounded tile, built from the OSD font.
#include "icon.h"
#include "osd.h"
#include "png.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

void icon_pixels(uint32_t *px, int size)
{
    // tile: vertical gradient, rounded corners
    float r = size * 0.18f;
    for (int y = 0; y < size; y++)
        for (int x = 0; x < size; x++) {
            float cx = x + 0.5f < r ? r : x + 0.5f > size - r ? size - r : x + 0.5f;
            float cy = y + 0.5f < r ? r : y + 0.5f > size - r ? size - r : y + 0.5f;
            float d = hypotf(x + 0.5f - cx, y + 0.5f - cy);
            float a = d > r ? 0 : d > r - 1 ? r - d : 1;
            float t = (float)y / (float)size;
            uint32_t red = (uint32_t)(0x70 - 0x50 * t), green = (uint32_t)(0x08 + 0x04 * t);
            px[y * size + x] = (uint32_t)(a * 255) << 24 | red << 16 | green << 8 | 0x06;
        }
    // "KI2": 3 glyphs of 5x7, scaled to fit with a dark outline
    const char *text = "KI2";
    int scale = size / 20 > 0 ? size / 20 : 1;
    if (size <= 16) scale = 1;
    int gw = 5 * scale, gap = scale, tw = 3 * gw + 2 * gap, th = 7 * scale;
    int x0 = (size - tw) / 2, y0 = (size - th) / 2;
    for (int pass = 0; pass < 2; pass++)
        for (int c = 0; c < 3; c++) {
            const uint8_t *g = osd_glyph(text[c]);
            for (int col = 0; col < 5; col++)
                for (int row = 0; row < 7; row++) {
                    if (!(g[col] >> row & 1)) continue;
                    for (int dy = -1; dy <= scale; dy++)
                        for (int dx = -1; dx <= scale; dx++) {
                            bool inside = dx >= 0 && dy >= 0 && dx < scale && dy < scale;
                            if (pass == 0 && inside) continue;        // outline first
                            if (pass == 1 && !inside) continue;
                            int x = x0 + c * (gw + gap) + col * scale + dx, y = y0 + row * scale + dy;
                            if (x < 0 || y < 0 || x >= size || y >= size) continue;
                            float t = (float)(row * scale + dy) / (float)th;
                            uint32_t gold = 0xff000000u | (uint32_t)(0xff) << 16 | (uint32_t)(0xe0 - 0x60 * t) << 8 | (uint32_t)(0x60 - 0x50 * t);
                            px[y * size + x] = pass ? gold : 0xff200000u;
                        }
                }
        }
}

void set_window_icon(SDL_Window *win)
{
    static uint32_t px[64 * 64];
    icon_pixels(px, 64);
    SDL_Surface *s = SDL_CreateRGBSurfaceFrom(px, 64, 64, 32, 64 * 4, 0x00ff0000, 0x0000ff00, 0x000000ff, 0xff000000);
    if (s) { SDL_SetWindowIcon(win, s); SDL_FreeSurface(s); }
}

static void le16(FILE *f, unsigned v) { fputc(v & 0xff, f); fputc(v >> 8 & 0xff, f); }
static void le32(FILE *f, unsigned v) { le16(f, v & 0xffff); le16(f, v >> 16); }

bool icon_write_ico(const char *path)
{
    static const int sizes[] = { 16, 32, 48, 64 };
    const int n = 4;
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    le16(f, 0); le16(f, 1); le16(f, n);
    unsigned offset = 6 + 16 * n;
    for (int i = 0; i < n; i++) {
        int s = sizes[i];
        unsigned mask = (unsigned)((s + 31) / 32 * 4 * s), bytes = 40 + (unsigned)(s * s * 4) + mask;
        fputc(s, f); fputc(s, f); fputc(0, f); fputc(0, f);
        le16(f, 1); le16(f, 32); le32(f, bytes); le32(f, offset);
        offset += bytes;
    }
    static uint32_t px[64 * 64];
    for (int i = 0; i < n; i++) {
        int s = sizes[i];
        icon_pixels(px, s);
        le32(f, 40); le32(f, (unsigned)s); le32(f, (unsigned)(2 * s)); le16(f, 1); le16(f, 32);
        le32(f, 0); le32(f, 0); le32(f, 0); le32(f, 0); le32(f, 0); le32(f, 0);
        for (int y = s - 1; y >= 0; y--)                 // bottom-up BGRA
            for (int x = 0; x < s; x++) le32(f, px[y * s + x]);
        for (int k = 0; k < (s + 31) / 32 * 4 * s; k++) fputc(0, f);   // AND mask: alpha does the work
    }
    return fclose(f) == 0;
}

bool icon_write_png(const char *path, int size)
{
    static uint32_t px[256 * 256];
    if (size > 256) return false;
    icon_pixels(px, size);
    return png_write_argb(path, px, size, size);
}
