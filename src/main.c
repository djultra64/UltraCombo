// Killer Instinct 2 native frontend.
#include "config.h"
#include "cpu.h"
#include "ide.h"
#include "machine.h"
#include "mem.h"
#include "rec.h"
#include "sound.h"
#include "state.h"
#include "net.h"
#include "netplay.h"
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CPU_HZ        100000000
#define LINES         261
#define VISIBLE_LINES 240
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

static bool readable(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (f) fclose(f);
    return f != NULL;
}

const char *base_dir(void)
{
    static char *dir;
    if (!dir) dir = SDL_GetBasePath();
    if (!dir) dir = SDL_strdup("./");
    return dir;
}

// Use `name` if it exists, otherwise look beside the executable and in run/ there.
const char *find_data(const char *name, char *buf, size_t n)
{
    if (readable(name)) return name;
    snprintf(buf, n, "%s%s", base_dir(), name);
    if (readable(buf)) return buf;
    snprintf(buf, n, "%srun/%s", base_dir(), name);
    return readable(buf) ? buf : name;
}

static struct {
    int sound_acc;        // CPU cycles not yet passed on to the DCS
    uint64_t frame;       // frames run since power-on
} m;
#define sound_acc m.sound_acc

uint64_t machine_frame(void) { return m.frame; }

size_t machine_state(uint8_t *buf, bool load)
{
    if (buf && load) { memcpy(&m, buf, sizeof m); memcpy(&hw, buf + sizeof m, sizeof hw); }
    else if (buf) { memcpy(buf, &m, sizeof m); memcpy(buf + sizeof m, &hw, sizeof hw); }
    return sizeof m + sizeof hw;
}

// Power-cycle: as at startup, without reloading anything from disk.
void machine_reset(void)
{
    memset(ram, 0, RAM_SIZE);
    memset(&m, 0, sizeof m);
    uint32_t dsw = hw.dsw;     // switches stay as set
    hw = (hw_t){ .p1 = 0xffffffffu, .p2 = 0xffffffffu, .volume = 0xffffffffu, .dsw = dsw };
    sound_power_on();
    ide_power_on();
    cpu_reset();
    rec_reset();
    state_init();
}

void run_frame(void)
{
    m.frame++;
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
            sound_acc += slice;
            sound_run(sound_acc / 10);
            sound_acc %= 10;
            remaining -= slice;
        }
    }
}

void to_rgb(uint32_t *dst)
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

// Scripted input for headless runs: "frame:button:duration,..."
typedef struct { const char *name; uint32_t *port; uint32_t bit; } button_t;
static const button_t buttons[] = {
    { "p1hq", &hw.p1, 0x0001 }, { "p1hm", &hw.p1, 0x0002 }, { "p1hf", &hw.p1, 0x0004 },
    { "p1lq", &hw.p1, 0x0008 }, { "p1lm", &hw.p1, 0x0010 }, { "p1lf", &hw.p1, 0x0020 },
    { "p1up", &hw.p1, 0x0040 }, { "p1down", &hw.p1, 0x0080 }, { "p1left", &hw.p1, 0x0100 },
    { "p1right", &hw.p1, 0x0200 }, { "start1", &hw.p1, 0x0400 }, { "coin1", &hw.p1, 0x0800 },
    { "test", &hw.p1, 0x1000 },
    { "p2hq", &hw.p2, 0x0001 }, { "p2lq", &hw.p2, 0x0008 }, { "p2left", &hw.p2, 0x0100 },
    { "p2right", &hw.p2, 0x0200 }, { "start2", &hw.p2, 0x0400 }, { "coin2", &hw.p2, 0x0800 },
};
typedef struct { int frame, len; const button_t *b; } script_ev_t;
static script_ev_t script[256];
static int nscript;

static void parse_script(const char *spec)
{
    char buf[4096];
    snprintf(buf, sizeof buf, "%s", spec);
    for (char *tok = strtok(buf, ","); tok && nscript < 256; tok = strtok(NULL, ",")) {
        int frame, len = 6;
        char name[32];
        if (sscanf(tok, "%d:%31[a-z0-9]:%d", &frame, name, &len) < 2) continue;
        for (size_t i = 0; i < sizeof buttons / sizeof buttons[0]; i++)
            if (!strcmp(buttons[i].name, name)) { script[nscript++] = (script_ev_t){ frame, len, &buttons[i] }; break; }
    }
}

static void apply_script(int frame)
{
    hw.p1 = hw.p2 = 0xffffffffu;
    for (int i = 0; i < nscript; i++)
        if (frame >= script[i].frame && frame < script[i].frame + script[i].len)
            *script[i].b->port &= ~script[i].b->bit;
}

/* Netplay callbacks shared with the frontend: emulate with explicit inputs. */
void machine_run_inputs(const uint16_t in[2], bool resim)
{
    hw.p1 = 0xffffffffu & ~(uint32_t)(in[0] & 0x0fff);
    hw.p2 = 0xffffffffu & ~(uint32_t)(in[1] & 0x0fff);
    run_frame();
    static int desync_at = -1;                      // testing: KI2_DESYNC_AT=frame breaks this side
    if (desync_at < 0) desync_at = getenv("KI2_DESYNC_AT") ? atoi(getenv("KI2_DESYNC_AT")) : 0;
    if (desync_at && machine_frame() == (uint64_t)desync_at && !resim) { ram2[0x7f0010] ^= 0x55; ram_touch(&ram2[0x7f0010]); }
    state_checkpoint();
    if (resim) { int16_t junk[4096]; while (sound_fetch(junk, 4096) > 0) {} }
}

void machine_netplay_reset(void)
{
    machine_reset();
    for (int i = 0; i < NP_BOOT_FRAMES; i++) {
        hw.p1 = hw.p2 = 0xffffffffu;
        run_frame();
    }
    state_init();
}

int net_warmup;     // testing: play locally (with rewind history) until the session starts

static int headless_netplay(int end, const char *host_port, const char *join, int delay, int fps,
                            int lat, int jit, int loss)
{
    netplay_set_callbacks(machine_run_inputs, machine_netplay_reset);
    net_simulate(lat, jit, loss);
    bool ok;
    if (host_port) ok = netplay_host(atoi(host_port), delay);
    else {
        char h[256]; int port = NP_DEFAULT_PORT;
        snprintf(h, sizeof h, "%s", join);
        char *c = strrchr(h, ':');
        if (c) { *c = 0; port = atoi(c + 1); }
        ok = netplay_join(h, port, delay);
    }
    if (!ok) { fprintf(stderr, "%s\n", netplay_take_error()); return 1; }
    netplay_set_end((uint32_t)end);
    int me = host_port ? 0 : 1;
    double next = net_now_ms();
    uint32_t last_report = 0;
    if (net_warmup) state_set_history((size_t)256 << 20);
    for (;;) {
        if (net_warmup && !netplay_running()) { hw.p1 = hw.p2 = 0xffffffffu; run_frame(); state_checkpoint(); }
        // this side's buttons from the script, for the frame the input will be used in
        uint32_t f = netplay_input_frame();
        apply_script((int)f + 1);
        uint16_t local = (uint16_t)(~(me ? hw.p2 : hw.p1) & 0x0fff);
        netplay_update(local);
        const char *err = netplay_take_error();
        if (err) { fprintf(stderr, "%s\n", err); return 2; }
        if (netplay_running() && netplay_confirmed() >= (uint32_t)end) {
            fprintf(stderr, "state hash %016llx at frame %llu\n", (unsigned long long)state_hash(), (unsigned long long)machine_frame());
            for (int i = 0; i < 30; i++) { netplay_update(0); SDL_Delay(10); }   // let the other side confirm too
            netplay_stop();
            return 0;
        }
        if (netplay_running() && machine_frame() / 600 != last_report) {
            last_report = (uint32_t)(machine_frame() / 600);
            fprintf(stderr, "%s\n", netplay_status());
        }
        next += 1000.0 / fps;
        double now = net_now_ms();
        if (next > now) SDL_Delay((uint32_t)(next - now));
        else if (now - next > 100) next = now;
    }
}

static void usage(void)
{
    fprintf(stderr,
        "usage: ki2 [options]\n"
        "  --rom PATH      boot ROM (ki2-l14.u98)\n"
        "  --disk PATH     raw disk image extracted from kinst2.chd\n"
        "  --sound DIR     directory holding ki2_l1.u10..u36\n"
        "  --headless N    run N frames without a window\n"
        "  --shot-every N  in headless mode, save a PPM every N frames\n"
        "  --script S      headless input, e.g. \"3600:coin1:6,3700:start1:6\"\n"
        "  --trace         log every executed instruction\n"
        "  --save-at F P   in headless mode, save a state file P after frame F\n"
        "  --load P        start from state file P\n"
        "  --rewind-test N headless: every N frames rewind N, replay, and check the state matches\n"
        "  --runahead N    headless: run N frames ahead and roll back after each frame\n"
        "  --host [PORT]   host a netplay game (default port from ki2.ini, 7845)\n"
        "  --join HOST[:PORT]  join a netplay game\n"
        "  --net-host PORT / --net-join HOST:PORT\n"
        "                  headless netplay test: play this side of --script until frame N\n"
        "  --net-delay N, --net-fps F, --net-sim LATENCY:JITTER:LOSS%%  (testing)\n"
        "Settings, key bindings and hotkeys are in ki2.ini beside the executable\n"
        "(written with defaults on first run). See README.md.\n");
}

int main(int argc, char **argv)
{
#ifdef _WIN32
    {   // a GUI program has no console: keep the log beside the executable
        static char log[4096];
        snprintf(log, sizeof log, "%ski2.log", base_dir());
        if (!getenv("KI2_CONSOLE")) { freopen(log, "w", stderr); setvbuf(stderr, NULL, _IONBF, 0); }
    }
#endif
    static char ini[4096];
    snprintf(ini, sizeof ini, "%ski2.ini", base_dir());
    config_load(ini);
    const char *rom_path = *cfg.rom ? cfg.rom : "ki2-l14.u98";
    const char *disk_path = *cfg.disk ? cfg.disk : "kinst2.raw";
    const char *sound_dir = *cfg.sound ? cfg.sound : ".";
    int headless = 0, shot_every = 0, dis_count = 0, trace_from = 0;
    int save_at = 0, rewind_test = 0, runahead = 0;
    const char *net_host = NULL, *net_join = NULL;
    int net_delay = 2, net_fps = 60, net_lat = 0, net_jit = 0, net_loss = 0;
    extern int net_warmup;
    const char *save_path = NULL, *load_path = NULL;
    uint32_t dis_addr = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--rom") && i + 1 < argc) rom_path = argv[++i];
        else if (!strcmp(argv[i], "--disk") && i + 1 < argc) disk_path = argv[++i];
        else if (!strcmp(argv[i], "--sound") && i + 1 < argc) sound_dir = argv[++i];
        else if (!strcmp(argv[i], "--headless") && i + 1 < argc) headless = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--shot-every") && i + 1 < argc) shot_every = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--trace")) cpu_trace = true;
        else if (!strcmp(argv[i], "--script") && i + 1 < argc) parse_script(argv[++i]);
        else if (!strcmp(argv[i], "--trace-from") && i + 1 < argc) trace_from = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--save-at") && i + 2 < argc) { save_at = atoi(argv[++i]); save_path = argv[++i]; }
        else if (!strcmp(argv[i], "--load") && i + 1 < argc) load_path = argv[++i];
        else if (!strcmp(argv[i], "--rewind-test") && i + 1 < argc) rewind_test = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--runahead") && i + 1 < argc) runahead = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--host")) { extern const char *opt_net_host; opt_net_host = (i + 1 < argc && argv[i + 1][0] != '-') ? argv[++i] : ""; }
        else if (!strcmp(argv[i], "--join") && i + 1 < argc) { extern const char *opt_net_join; opt_net_join = argv[++i]; }
        else if (!strcmp(argv[i], "--net-host") && i + 1 < argc) net_host = argv[++i];
        else if (!strcmp(argv[i], "--net-join") && i + 1 < argc) net_join = argv[++i];
        else if (!strcmp(argv[i], "--net-delay") && i + 1 < argc) net_delay = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--net-fps") && i + 1 < argc) net_fps = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--net-warmup")) net_warmup = 1;
        else if (!strcmp(argv[i], "--net-sim") && i + 1 < argc) sscanf(argv[++i], "%d:%d:%d", &net_lat, &net_jit, &net_loss);
        else if (!strcmp(argv[i], "--dis") && i + 2 < argc) { dis_addr = (uint32_t)strtoul(argv[++i], 0, 16); dis_count = atoi(argv[++i]); }
        else { usage(); return 1; }
    }

    static char rom_buf[4096], disk_buf[4096];
    rom_path = find_data(rom_path, rom_buf, sizeof rom_buf);
    disk_path = find_data(disk_path, disk_buf, sizeof disk_buf);
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
        const char *u10 = find_data("ki2_l1.u10", snd_buf, sizeof snd_buf);
        const char *slash = strrchr(u10, '/');
        if (!slash) slash = strrchr(u10, '\\');
        if (slash) { memmove(snd_buf, u10, (size_t)(slash - u10)); snd_buf[slash - u10] = 0; sound_dir = snd_buf; }
    }
    sound_init(sound_dir);
    hw.dsw = getenv("KI2_DSW") ? (uint32_t)strtoul(getenv("KI2_DSW"), 0, 16) : config_dsw();
    cpu_reset();
    rec_init();
    // KI2_INTERP_HIST=file: where the interpreter still runs (missing entry points)
    if (headless && getenv("KI2_INTERP_HIST")) cpu_interp_hist = calloc(1 << 16, sizeof *cpu_interp_hist);

    state_init();
    if (load_path) {
        if (!state_load_file(load_path)) { fprintf(stderr, "%s: cannot load state\n", load_path); return 1; }
        fprintf(stderr, "loaded %s at frame %llu\n", load_path, (unsigned long long)machine_frame());
    }

    if (getenv("KI2_RESET_TEST")) {         // testing: does anything survive a power-on reset?
        int n = atoi(getenv("KI2_RESET_TEST"));
        for (int i = 0; i < n; i++) run_frame();
        machine_netplay_reset();
        fprintf(stderr, "after reset: hash %016llx quick %016llx\n", (unsigned long long)state_hash(), (unsigned long long)state_quick_hash());
        FILE *o = fopen("reset_ram.bin", "wb"); fwrite(ram, 1, RAM_SIZE, o); fclose(o);
        static uint8_t core[1 << 20];
        size_t n0 = cpu_state(core, false), n1 = rec_state(core + n0, false), n2 = ide_state(core + n0 + n1, false);
        size_t n3 = sound_state(core + n0 + n1 + n2, false), n4 = machine_state(core + n0 + n1 + n2 + n3, false);
        o = fopen("reset_core.bin", "wb"); fwrite(core, 1, n0 + n1 + n2 + n3 + n4, o); fclose(o);
        fprintf(stderr, "core parts: cpu %zu rec %zu ide %zu sound %zu machine %zu\n", n0, n1, n2, n3, n4);
        return 0;
    }
    if (headless && (net_host || net_join)) return headless_netplay(headless, net_host, net_join, net_delay, net_fps, net_lat, net_jit, net_loss);

    if (headless) {
        if (rewind_test || getenv("KI2_HISTORY")) state_set_history((size_t)1 << 30);
        int rewind_fail = 0;
        FILE *wav = getenv("KI2_WAV") ? fopen(getenv("KI2_WAV"), "wb") : NULL;
        uint32_t wav_samples = 0;
        if (wav) fseek(wav, 44, SEEK_SET);
        for (int f = (int)machine_frame() + 1; f <= headless; f++) {
            if (trace_from && f == trace_from) cpu_trace = true;
            apply_script(f);
            uint64_t t0 = SDL_GetPerformanceCounter();
            run_frame();
            state_checkpoint();
            if (runahead) {
                for (int k = 0; k < runahead; k++) run_frame();
                state_rollback();
            }
            if (save_at && f == save_at) {
                if (state_save_file(save_path)) fprintf(stderr, "saved %s at frame %d\n", save_path, f);
                else fprintf(stderr, "%s: save failed\n", save_path);
            }
            if (rewind_test && f % rewind_test == 0 && f > rewind_test) {
                // go back N frames, replay them with the same input, compare
                uint64_t want = state_hash();
                int n = 0;
                while (n < rewind_test && state_rewind()) n++;
                for (int k = f - n + 1; k <= f; k++) { apply_script(k); run_frame(); state_checkpoint(); }
                uint64_t got = state_hash();
                if (got != want) rewind_fail++;
                fprintf(stderr, "rewind test frame %d: back %d, %s (%.1f MB history)\n", f, n,
                        got == want ? "match" : "MISMATCH", state_history_bytes() / 1048576.0);
            }
            static double acc_ms, max_ms;
            double ms = (SDL_GetPerformanceCounter() - t0) * 1000.0 / SDL_GetPerformanceFrequency();
            acc_ms += ms;
            if (ms > max_ms) max_ms = ms;
            if (f % 60 == 0) {
                if (getenv("KI2_SPEED")) {
                    static uint64_t last_cyc, last_int;
                    fprintf(stderr, "frames %d-%d: %.1f ms/frame (%.0f%% speed) worst %.1f ms, %.0f%% interpreted, code gen %u\n", f - 59, f, acc_ms / 60, 100.0 * (1000.0 / FRAME_HZ) / (acc_ms / 60), max_ms,
                            100.0 * (cpu_interp_count - last_int) / (double)(cpu.cycles - last_cyc + 1), rec_code_gen);
                    last_cyc = cpu.cycles; last_int = cpu_interp_count;
                }
                acc_ms = max_ms = 0;
            }
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
        fprintf(stderr, "state hash %016llx at frame %llu\n", (unsigned long long)state_hash(), (unsigned long long)machine_frame());
        if (rewind_fail) { fprintf(stderr, "%d rewind mismatches\n", rewind_fail); return 3; }
        if (cpu_interp_hist) {
            FILE *h = fopen(getenv("KI2_INTERP_HIST"), "w");
            for (int i = 0; i < 1 << 16; i++) if (cpu_interp_hist[i] > 1000) fprintf(h, "%08x %u\n", 0x88000000u + i * 4, cpu_interp_hist[i]);
            fclose(h);
        }
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

    return frontend_main();
}
