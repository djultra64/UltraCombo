// Killer Instinct 2 native frontend.
#include "cpu.h"
#include "ide.h"
#include "mem.h"
#include "sound.h"
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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

// Use `name` if it exists, otherwise look in run/ beside the executable.
static const char *find_data(const char *name, const char *argv0, char *buf, size_t n)
{
    if (access(name, R_OK) == 0 || name[0] == '/') return name;
    const char *slash = strrchr(argv0, '/');
    int dirlen = slash ? (int)(slash - argv0) : 1;
    snprintf(buf, n, "%.*s/run/%s", dirlen, slash ? argv0 : ".", name);
    return access(buf, R_OK) == 0 ? buf : name;
}

static void run_frame(void)
{
    for (int line = 0; line < LINES; line++) {
        if (line == VISIBLE_LINES) cpu_set_irq(0, true);
        if (line == 0) cpu_set_irq(0, false);
        int remaining = CYCLES_PER_LINE;
        while (remaining > 0) {
            int slice = remaining < 256 ? remaining : 256;
            int ran = (int)cpu_run((uint64_t)slice);
            if (cpu_idle) {
                // spinning on an interrupt line: nothing changes until the next scanline
                cpu_skip((uint64_t)(remaining - ran));
                slice = remaining;
            }
            ide_tick(slice);
            // DCS runs at 10 MHz against the 100 MHz R4600
            static int sound_acc;
            sound_acc += slice;
            sound_run(sound_acc / 10);
            sound_acc %= 10;
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

#define AUDIO_HZ 48000

// Resample DCS output (usually 31.25 kHz) to the device rate and queue it.
static void pump_audio(SDL_AudioDeviceID dev)
{
    static int16_t in[8192], out[16384];
    static double pos;      // fractional read position into the previous+current block
    static int16_t prev;
    int n = sound_fetch(in, 8192);
    if (!dev) return;
    double step = sound_rate() / AUDIO_HZ;
    int m = 0;
    while (pos < n && m < 16384) {
        int i = (int)pos;
        double f = pos - i;
        int16_t a = i == 0 ? prev : in[i - 1];
        int16_t b = in[i];
        out[m++] = (int16_t)(a + (b - a) * f);
        pos += step;
    }
    pos -= n;
    if (n) prev = in[n - 1];
    // keep latency bounded: drop audio if more than ~120 ms is queued
    if (SDL_GetQueuedAudioSize(dev) < AUDIO_HZ * 2 * 120 / 1000)
        SDL_QueueAudio(dev, out, (Uint32)m * 2);
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
    int headless = 0, shot_every = 0, dis_count = 0, trace_from = 0;
    uint32_t dis_addr = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--rom") && i + 1 < argc) rom_path = argv[++i];
        else if (!strcmp(argv[i], "--disk") && i + 1 < argc) disk_path = argv[++i];
        else if (!strcmp(argv[i], "--sound") && i + 1 < argc) sound_dir = argv[++i];
        else if (!strcmp(argv[i], "--headless") && i + 1 < argc) headless = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--shot-every") && i + 1 < argc) shot_every = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--trace")) cpu_trace = true;
        else if (!strcmp(argv[i], "--trace-from") && i + 1 < argc) trace_from = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--dis") && i + 2 < argc) { dis_addr = (uint32_t)strtoul(argv[++i], 0, 16); dis_count = atoi(argv[++i]); }
        else { usage(); return 1; }
    }

    static char rom_buf[4096], disk_buf[4096];
    rom_path = find_data(rom_path, argv[0], rom_buf, sizeof rom_buf);
    disk_path = find_data(disk_path, argv[0], disk_buf, sizeof disk_buf);
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
    static char snd_buf[4096];
    if (!strcmp(sound_dir, ".")) {
        // locate the sound ROMs the same way as the boot ROM
        const char *u10 = find_data("ki2_l1.u10", argv[0], snd_buf, sizeof snd_buf);
        const char *slash = strrchr(u10, '/');
        if (slash) { snd_buf[slash - u10] = 0; if (u10 != snd_buf) { memmove(snd_buf, u10, slash - u10); snd_buf[slash - u10] = 0; } sound_dir = snd_buf; }
    }
    sound_init(sound_dir);
    cpu_reset();

    if (headless) {
        FILE *wav = getenv("KI2_WAV") ? fopen(getenv("KI2_WAV"), "wb") : NULL;
        uint32_t wav_samples = 0;
        if (wav) fseek(wav, 44, SEEK_SET);
        for (int f = 1; f <= headless; f++) {
            if (trace_from && f == trace_from) cpu_trace = true;
            run_frame();
            if (getenv("KI2_DUMP_AT") && f == atoi(getenv("KI2_DUMP_AT"))) {
                FILE *o = fopen("ram0.bin", "wb"); fwrite(ram0, 1, RAM0_SIZE, o); fclose(o);
                o = fopen("ram2.bin", "wb"); fwrite(ram2, 1, RAM2_SIZE, o); fclose(o);
            }
            if (wav) {
                int16_t buf[4096]; int n;
                while ((n = sound_fetch(buf, 4096)) > 0) { fwrite(buf, 2, (size_t)n, wav); wav_samples += (uint32_t)n; }
            }
            if (shot_every && f % shot_every == 0) {
                char name[64];
                snprintf(name, sizeof name, "frame_%05d.ppm", f);
                save_ppm(name);
                fprintf(stderr, "frame %d pc=%08x\n", f, cpu.pc);
            }
        }
        save_ppm("frame_last.ppm");
        if (wav) {
            uint32_t rate = (uint32_t)sound_rate(), bytes = wav_samples * 2;
            uint8_t h[44] = "RIFF____WAVEfmt ";
            uint32_t v;
            v = 36 + bytes; memcpy(h + 4, &v, 4);
            v = 16; memcpy(h + 16, &v, 4);
            uint16_t w = 1; memcpy(h + 20, &w, 2); memcpy(h + 22, &w, 2);
            memcpy(h + 24, &rate, 4); v = rate * 2; memcpy(h + 28, &v, 4);
            w = 2; memcpy(h + 32, &w, 2); w = 16; memcpy(h + 34, &w, 2);
            memcpy(h + 36, "data", 4); memcpy(h + 40, &bytes, 4);
            fseek(wav, 0, SEEK_SET); fwrite(h, 1, 44, wav); fclose(wav);
            fprintf(stderr, "wrote %u samples at %u Hz\n", wav_samples, rate);
        }
        return 0;
    }

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) { fprintf(stderr, "SDL: %s\n", SDL_GetError()); return 1; }
    SDL_Window *win = SDL_CreateWindow("Killer Instinct 2", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                       960, 720, SDL_WINDOW_RESIZABLE);
    SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    SDL_RenderSetLogicalSize(ren, 320, 240);
    SDL_Texture *tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, 320, 240);
    SDL_AudioSpec want = { .freq = AUDIO_HZ, .format = AUDIO_S16SYS, .channels = 1, .samples = 1024 }, have;
    SDL_AudioDeviceID adev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (!adev) fprintf(stderr, "audio: %s\n", SDL_GetError());
    else SDL_PauseAudioDevice(adev, 0);

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
        pump_audio(adev);
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
