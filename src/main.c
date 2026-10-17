// Killer Instinct 2 native frontend.
#include "cpu.h"
#include "ide.h"
#include "mem.h"
#include "sound.h"
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CPU_HZ        100000000
#define LINES         261
#define VISIBLE_LINES 240
#define FRAME_HZ      58.9634
#define CYCLES_PER_LINE ((int)(CPU_HZ / FRAME_HZ / LINES))

static bool load_file(const char *path, void *dst, size_t size)
{
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); return false; }
    size_t n = fread(dst, 1, size, f);
    fclose(f);
    if (n != size) { fprintf(stderr, "%s: short read\n", path); return false; }
    return true;
}

static void run_frame(void)
{
    for (int line = 0; line < LINES; line++) {
        if (line == VISIBLE_LINES) cpu_set_irq(0, true);
        if (line == 0) cpu_set_irq(0, false);
        int remaining = CYCLES_PER_LINE;
        while (remaining > 0) {
            int slice = remaining < 256 ? remaining : 256;
            cpu_run((uint64_t)slice);
            ide_tick(slice);
            remaining -= slice;
        }
    }
}

static void to_rgb(uint32_t *dst)
{
    const uint16_t *src = video_base();
    for (int y = 0; y < 240; y++)
        for (int x = 0; x < 320; x++) {
            uint16_t p = src[y * 320 + x];
            uint32_t r = (p & 31), g = (p >> 5) & 31, b = (p >> 10) & 31;
            dst[y * 320 + x] = (r << 19 | (r >> 2) << 16) | (g << 11 | (g >> 2) << 8) | (b << 3 | b >> 2);
        }
}

static void save_ppm(const char *path)
{
    static uint32_t px[320 * 240];
    to_rgb(px);
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6\n320 240\n255\n");
    for (int i = 0; i < 320 * 240; i++) {
        uint8_t c[3] = { px[i] >> 16, px[i] >> 8, px[i] };
        fwrite(c, 1, 3, f);
    }
    fclose(f);
}

typedef struct { SDL_Scancode key; uint32_t *port; uint32_t bit; } keymap_t;

static keymap_t keys[] = {
    // Player 1: arrows, A S D (high: quick/medium/fierce), Z X C (low)
    { SDL_SCANCODE_A, &hw.p1, 0x0001 }, { SDL_SCANCODE_S, &hw.p1, 0x0002 }, { SDL_SCANCODE_D, &hw.p1, 0x0004 },
    { SDL_SCANCODE_Z, &hw.p1, 0x0008 }, { SDL_SCANCODE_X, &hw.p1, 0x0010 }, { SDL_SCANCODE_C, &hw.p1, 0x0020 },
    { SDL_SCANCODE_UP, &hw.p1, 0x0040 }, { SDL_SCANCODE_DOWN, &hw.p1, 0x0080 },
    { SDL_SCANCODE_LEFT, &hw.p1, 0x0100 }, { SDL_SCANCODE_RIGHT, &hw.p1, 0x0200 },
    { SDL_SCANCODE_1, &hw.p1, 0x0400 }, { SDL_SCANCODE_5, &hw.p1, 0x0800 },
    { SDL_SCANCODE_F2, &hw.p1, 0x1000 },
    // Player 2: I J K L, T Y U (high), G H B (low)
    { SDL_SCANCODE_T, &hw.p2, 0x0001 }, { SDL_SCANCODE_Y, &hw.p2, 0x0002 }, { SDL_SCANCODE_U, &hw.p2, 0x0004 },
    { SDL_SCANCODE_G, &hw.p2, 0x0008 }, { SDL_SCANCODE_H, &hw.p2, 0x0010 }, { SDL_SCANCODE_B, &hw.p2, 0x0020 },
    { SDL_SCANCODE_I, &hw.p2, 0x0040 }, { SDL_SCANCODE_K, &hw.p2, 0x0080 },
    { SDL_SCANCODE_J, &hw.p2, 0x0100 }, { SDL_SCANCODE_L, &hw.p2, 0x0200 },
    { SDL_SCANCODE_2, &hw.p2, 0x0400 }, { SDL_SCANCODE_6, &hw.p2, 0x0800 },
    { SDL_SCANCODE_9, &hw.p2, 0x2000 },
};

static void usage(void)
{
    fprintf(stderr,
        "usage: ki2 [options]\n"
        "  --rom PATH      boot ROM (ki2-l14.u98)\n"
        "  --disk PATH     raw disk image extracted from kinst2.chd\n"
        "  --sound DIR     directory holding ki2_l1.u10..u36\n"
        "  --headless N    run N frames without a window\n"
        "  --shot-every N  in headless mode, save a PPM every N frames\n"
        "  --trace         log every executed instruction\n");
}

int main(int argc, char **argv)
{
    const char *rom_path = "ki2-l14.u98", *disk_path = "kinst2.raw", *sound_dir = ".";
    int headless = 0, shot_every = 0, dis_count = 0;
    uint32_t dis_addr = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--rom") && i + 1 < argc) rom_path = argv[++i];
        else if (!strcmp(argv[i], "--disk") && i + 1 < argc) disk_path = argv[++i];
        else if (!strcmp(argv[i], "--sound") && i + 1 < argc) sound_dir = argv[++i];
        else if (!strcmp(argv[i], "--headless") && i + 1 < argc) headless = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--shot-every") && i + 1 < argc) shot_every = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--trace")) cpu_trace = true;
        else if (!strcmp(argv[i], "--dis") && i + 2 < argc) { dis_addr = (uint32_t)strtoul(argv[++i], 0, 16); dis_count = atoi(argv[++i]); }
        else { usage(); return 1; }
    }

    if (!load_file(rom_path, rom, ROM_SIZE)) return 1;
    if (dis_count) {
        for (int i = 0; i < dis_count; i++) {
            uint32_t a = dis_addr + 4 * i, op;
            uint8_t *p = mem_ptr(a & 0x1fffffffu);
            if (getenv("KI2_DIS_RAM")) { static bool loaded; if (!loaded) { loaded = true;
                FILE *f = fopen("ram0.bin", "rb"); if (f) { fread(ram0, 1, RAM0_SIZE, f); fclose(f); }
                f = fopen("ram2.bin", "rb"); if (f) { fread(ram2, 1, RAM2_SIZE, f); fclose(f); } } }
            if (!p) break;
            memcpy(&op, p, 4);
            char buf[96];
            printf("%08x: %08x  %s\n", a, op, cpu_disasm(a, op, buf));
        }
        return 0;
    }
    // KI2 drive geometry from the CHD metadata; serial lives at identify word 11
    if (!ide_open(disk_path, 1463, 13, 47, 11)) return 1;
    sound_init(sound_dir);
    cpu_reset();

    if (headless) {
        for (int f = 1; f <= headless; f++) {
            run_frame();
            if (shot_every && f % shot_every == 0) {
                char name[64];
                snprintf(name, sizeof name, "frame_%05d.ppm", f);
                save_ppm(name);
                fprintf(stderr, "frame %d pc=%08x\n", f, cpu.pc);
            }
        }
        save_ppm("frame_last.ppm");
        return 0;
    }

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) { fprintf(stderr, "SDL: %s\n", SDL_GetError()); return 1; }
    SDL_Window *win = SDL_CreateWindow("Killer Instinct 2", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                       960, 720, SDL_WINDOW_RESIZABLE);
    SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    SDL_RenderSetLogicalSize(ren, 320, 240);
    SDL_Texture *tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, 320, 240);

    static uint32_t px[320 * 240];
    uint64_t freq = SDL_GetPerformanceFrequency();
    uint64_t next = SDL_GetPerformanceCounter();
    bool running = true;
    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) running = false;
            if (ev.type == SDL_KEYDOWN && ev.key.keysym.scancode == SDL_SCANCODE_ESCAPE) running = false;
        }
        const uint8_t *ks = SDL_GetKeyboardState(NULL);
        hw.p1 = hw.p2 = 0xffffffffu;
        for (size_t i = 0; i < sizeof keys / sizeof keys[0]; i++)
            if (ks[keys[i].key]) *keys[i].port &= ~keys[i].bit;

        run_frame();
        to_rgb(px);
        SDL_UpdateTexture(tex, NULL, px, 320 * 4);
        SDL_RenderClear(ren);
        SDL_RenderCopy(ren, tex, NULL, NULL);
        SDL_RenderPresent(ren);

        next += (uint64_t)(freq / FRAME_HZ);
        uint64_t now = SDL_GetPerformanceCounter();
        if (now < next) SDL_Delay((uint32_t)((next - now) * 1000 / freq));
        else next = now;
    }
    SDL_Quit();
    return 0;
}
