// Windowed frontend: video, audio, input, and the quality-of-life features
// (pause, fast-forward, fullscreen, screenshots, auto-coin, volume).
//
// Everything that reaches the emulated machine goes through the input ports
// once per frame (hw.p1, hw.p2, hw.volume), so the emulation stays a pure
// function of those inputs (needed for netplay later).
#include "config.h"
#include "machine.h"
#include "mem.h"
#include "osd.h"
#include "png.h"
#include "sound.h"
#include <SDL2/SDL.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#include <direct.h>
#define make_dir(p) _mkdir(p)
#else
#include <sys/stat.h>
#define make_dir(p) mkdir(p, 0755)
#endif

#define AUDIO_HZ 48000

static SDL_Window *win;
static SDL_Renderer *ren;
static SDL_Texture *tex;
static SDL_AudioDeviceID adev;
static uint32_t px[320 * 240];

static bool paused, step_once, fullscreen, muted, vsync;
static int  volume;
static double display_hz;
static uint64_t frame_count;

/* ---------------------------------------------------------------- */
/* Audio                                                            */
/* ---------------------------------------------------------------- */

// Resample DCS output (usually 31.25 kHz) to the device rate and queue it.
// `speed` > 1 when the game runs faster than 58.96 Hz: the same audio is
// played in less time (and slightly higher pitch).
static void pump_audio(bool discard, double speed)
{
    static int16_t in[8192], out[24576];
    static double pos;      // fractional read position into the previous+current block
    static int16_t prev;
    int n = sound_fetch(in, 8192);
    if (!adev || discard) return;
    double step = sound_rate() / AUDIO_HZ * speed;
    int gain = muted ? 0 : volume * 256 / 100;
    int m = 0;
    while (pos < n && m < 24576) {
        int i = (int)pos;
        double f = pos - i;
        int a = i == 0 ? prev : in[i - 1];
        int b = in[i];
        out[m++] = (int16_t)((int)(a + (b - a) * f) * gain / 256);
        pos += step;
    }
    pos -= n;
    if (n) prev = in[n - 1];
    // after a long hitch, throw away the backlog instead of lagging behind
    if (SDL_GetQueuedAudioSize(adev) > AUDIO_HZ * 2 * 250 / 1000) SDL_ClearQueuedAudio(adev);
    SDL_QueueAudio(adev, out, (Uint32)m * 2);
}

static double queued_ms(void)
{
    return adev ? SDL_GetQueuedAudioSize(adev) * 1000.0 / (AUDIO_HZ * 2) : 0;
}

/* ---------------------------------------------------------------- */
/* Video                                                            */
/* ---------------------------------------------------------------- */

static void apply_scaling(void)
{
    if (cfg.scale_mode == SCALE_STRETCH) SDL_RenderSetLogicalSize(ren, 0, 0);
    else SDL_RenderSetLogicalSize(ren, 320, 240);
    SDL_RenderSetIntegerScale(ren, cfg.scale_mode == SCALE_INTEGER);
    SDL_SetTextureScaleMode(tex, cfg.smooth ? SDL_ScaleModeLinear : SDL_ScaleModeNearest);
}

// Display-locked pacing is used when the display runs close to 60 Hz.
static void choose_pacing(void)
{
    SDL_DisplayMode m;
    int idx = SDL_GetWindowDisplayIndex(win);
    display_hz = (idx >= 0 && SDL_GetCurrentDisplayMode(idx, &m) == 0 && m.refresh_rate) ? m.refresh_rate : 60;
    bool want = cfg.pacing == PACING_VSYNC ||
                (cfg.pacing == PACING_AUTO && display_hz >= 59 && display_hz <= 61);
    if (want != vsync || !frame_count) {
        vsync = want;
        SDL_RenderSetVSync(ren, vsync);
        fprintf(stderr, "video: %.0f Hz display, %s pacing\n", display_hz, vsync ? "vsync" : "audio");
    }
}

static void set_fullscreen(bool on)
{
    fullscreen = on;
    SDL_SetWindowFullscreen(win, on ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
    SDL_ShowCursor(on ? SDL_DISABLE : SDL_ENABLE);
}

static void present(const char *status)
{
    to_rgb(px);
    osd_draw(px, status);
    SDL_UpdateTexture(tex, NULL, px, 320 * 4);
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
    SDL_RenderClear(ren);
    SDL_RenderCopy(ren, tex, NULL, NULL);
    SDL_RenderPresent(ren);
}

static void screenshot(void)
{
    char dir[4096], path[4200];
    snprintf(dir, sizeof dir, "%sscreenshots", base_dir());
    make_dir(dir);
    time_t t = time(NULL);
    char stamp[32];
    strftime(stamp, sizeof stamp, "%Y%m%d_%H%M%S", localtime(&t));
    snprintf(path, sizeof path, "%s/ki2_%s.png", dir, stamp);
    for (int i = 2; i < 100; i++) {         // several in one second
        FILE *f = fopen(path, "rb");
        if (!f) break;
        fclose(f);
        snprintf(path, sizeof path, "%s/ki2_%s_%d.png", dir, stamp, i);
    }
    to_rgb(px);
    if (png_write(path, px, 320, 240)) { osd_message("Screenshot saved"); fprintf(stderr, "screenshot: %s\n", path); }
    else osd_message("Screenshot failed");
}

/* ---------------------------------------------------------------- */
/* Input                                                            */
/* ---------------------------------------------------------------- */

#define MAX_PADS 2
static SDL_GameController *pads[MAX_PADS];

static void pad_added(int index)
{
    for (int i = 0; i < MAX_PADS; i++) {
        if (pads[i]) continue;
        pads[i] = SDL_GameControllerOpen(index);
        if (pads[i]) {
            fprintf(stderr, "pad: player %d = %s\n", i + 1, SDL_GameControllerName(pads[i]));
            osd_message("Player %d: %s", i + 1, SDL_GameControllerName(pads[i]));
        }
        return;
    }
}

static void pad_removed(SDL_JoystickID id)
{
    for (int i = 0; i < MAX_PADS; i++)
        if (pads[i] && SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pads[i])) == id) {
            osd_message("Player %d controller disconnected", i + 1);
            SDL_GameControllerClose(pads[i]);
            pads[i] = NULL;
        }
}

static bool pad_pressed(SDL_GameController *c, int b)
{
    if (b == PAD_NONE) return false;
    if (b == PAD_LT) return SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_TRIGGERLEFT) > 12000;
    if (b == PAD_RT) return SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 12000;
    return SDL_GameControllerGetButton(c, (SDL_GameControllerButton)b);
}

// Buttons held by each player this frame, one bit per BTN_*.
static void read_players(uint32_t held[2])
{
    const uint8_t *ks = SDL_GetKeyboardState(NULL);
    for (int p = 0; p < 2; p++) {
        held[p] = 0;
        for (int b = 0; b < NBUTTONS; b++)
            if (cfg.keys[p][b] && ks[cfg.keys[p][b]]) held[p] |= 1u << b;
        SDL_GameController *c = pads[p];
        if (!c) continue;
        for (int b = 0; b < NBUTTONS; b++)
            if (pad_pressed(c, cfg.pad[b])) held[p] |= 1u << b;
        int x = SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTX);
        int y = SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTY);
        if (y < -cfg.pad_deadzone) held[p] |= 1u << BTN_UP;
        if (y > cfg.pad_deadzone) held[p] |= 1u << BTN_DOWN;
        if (x < -cfg.pad_deadzone) held[p] |= 1u << BTN_LEFT;
        if (x > cfg.pad_deadzone) held[p] |= 1u << BTN_RIGHT;
    }
}

// Auto-coin: pressing Start without enough credits drops in the missing
// coins first, then presses Start. Driven entirely through the coin and
// start inputs, so the game does its own credit bookkeeping.
#define CREDITS_ADDR 0x7ff124u      // ram2 offset of the credit count (l14 code)
#define CREDITS_NEEDED 2            // default coinage: 2 credits to start or join

static struct { int player, coins, t, wait; bool active; } ac;

static int credits(void)
{
    uint32_t v;
    memcpy(&v, &ram2[CREDITS_ADDR], 4);
    return v < 100 ? (int)v : -1;   // garbage: different code version
}

static void auto_coin(uint32_t held[2])
{
    static uint32_t prev[2];
    if (!ac.active && cfg.auto_coin)
        for (int p = 0; p < 2; p++) {
            int c = credits();
            bool press = (held[p] & ~prev[p]) >> BTN_START & 1;
            if (press && c >= 0 && c < CREDITS_NEEDED) {
                ac.player = p; ac.coins = CREDITS_NEEDED - c; ac.t = ac.wait = 0; ac.active = true;
                break;
            }
        }
    prev[0] = held[0]; prev[1] = held[1];
    if (!ac.active) return;

    // 8 frames coin down, 8 up, per coin; wait (up to 1.5 s) for the game to
    // count them; then 10 frames of Start. Early in boot the game ignores
    // coins, in which case this gives up quietly.
    held[ac.player] &= ~(1u << BTN_START);
    int coin_frames = ac.coins * 16;
    if (ac.t < coin_frames) {
        if (ac.t % 16 < 8) held[ac.player] |= 1u << BTN_COIN;
        ac.t++;
    } else if (ac.t == coin_frames) {
        if (credits() >= CREDITS_NEEDED) ac.t++;
        else if (++ac.wait > 90) ac.active = false;
    } else if (ac.t < coin_frames + 11) {
        held[ac.player] |= 1u << BTN_START;
        ac.t++;
    } else ac.active = false;
}

static void set_ports(const uint32_t held[2])
{
    const uint8_t *ks = SDL_GetKeyboardState(NULL);
    hw.p1 = ~held[0];
    hw.p2 = ~held[1];
    if (cfg.hotkeys[HK_TEST] && ks[cfg.hotkeys[HK_TEST]]) hw.p1 &= ~0x1000u;
    if (cfg.hotkeys[HK_SERVICE] && ks[cfg.hotkeys[HK_SERVICE]]) hw.p2 &= ~0x2000u;
    hw.volume = 0xffffffffu;
    if (cfg.hotkeys[HK_GAME_VOL_UP] && ks[cfg.hotkeys[HK_GAME_VOL_UP]]) hw.volume &= ~0x4u;
    if (cfg.hotkeys[HK_GAME_VOL_DOWN] && ks[cfg.hotkeys[HK_GAME_VOL_DOWN]]) hw.volume &= ~0x8u;
}

static bool is_hotkey(SDL_Scancode sc, int hk) { return cfg.hotkeys[hk] && sc == cfg.hotkeys[hk]; }

static void set_volume(int v)
{
    volume = v < 0 ? 0 : v > 100 ? 100 : v;
    muted = false;
    osd_message("Volume %d%%", volume);
}

/* returns false to quit */
static bool handle_events(void)
{
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        switch (ev.type) {
        case SDL_QUIT: return false;
        case SDL_CONTROLLERDEVICEADDED: pad_added(ev.cdevice.which); break;
        case SDL_CONTROLLERDEVICEREMOVED: pad_removed(ev.cdevice.which); break;
        case SDL_WINDOWEVENT:
            if (ev.window.event == SDL_WINDOWEVENT_DISPLAY_CHANGED || ev.window.event == SDL_WINDOWEVENT_MOVED) choose_pacing();
            break;
        case SDL_KEYDOWN: {
            SDL_Scancode sc = ev.key.keysym.scancode;
            if (sc == SDL_SCANCODE_ESCAPE) {
                if (fullscreen) set_fullscreen(false);
                else return false;
            }
            if (ev.key.repeat && !is_hotkey(sc, HK_VOL_UP) && !is_hotkey(sc, HK_VOL_DOWN) && !is_hotkey(sc, HK_STEP)) break;
            if ((sc == SDL_SCANCODE_RETURN && (ev.key.keysym.mod & KMOD_ALT)) || is_hotkey(sc, HK_FULLSCREEN))
                set_fullscreen(!fullscreen);
            else if (is_hotkey(sc, HK_PAUSE)) {
                paused = !paused;
                if (paused && adev) SDL_ClearQueuedAudio(adev);
            }
            else if (is_hotkey(sc, HK_STEP)) { if (paused) step_once = true; else { paused = true; if (adev) SDL_ClearQueuedAudio(adev); } }
            else if (is_hotkey(sc, HK_SCREENSHOT)) screenshot();
            else if (is_hotkey(sc, HK_VOL_UP)) set_volume(volume + 10);
            else if (is_hotkey(sc, HK_VOL_DOWN)) set_volume(volume - 10);
            else if (is_hotkey(sc, HK_MUTE)) { muted = !muted; osd_message(muted ? "Muted" : "Volume %d%%", volume); }
            break;
        }
        }
    }
    return true;
}

static void emulate_frame(void)
{
    uint32_t held[2];
    read_players(held);
    auto_coin(held);
    set_ports(held);
    run_frame();
    frame_count++;
}

/* ---------------------------------------------------------------- */

int frontend_main(void)
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) {
        fprintf(stderr, "SDL: %s\n", SDL_GetError());
        return 1;
    }
    {   // extra controller mappings (https://github.com/mdqinc/SDL_GameControllerDB), if present
        char buf[4096];
        const char *db = find_data("gamecontrollerdb.txt", buf, sizeof buf);
        FILE *f = fopen(db, "rb");
        if (f) { fclose(f); SDL_GameControllerAddMappingsFromFile(db); }
    }
    int scale = cfg.window_scale > 0 ? cfg.window_scale : 3;
    win = SDL_CreateWindow("Killer Instinct 2", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                           320 * scale, 240 * scale, SDL_WINDOW_RESIZABLE);
    ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
    if (!win || !ren) { fprintf(stderr, "SDL: %s\n", SDL_GetError()); return 1; }
    tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, 320, 240);
    apply_scaling();
    if (cfg.fullscreen) set_fullscreen(true);
    choose_pacing();

    SDL_AudioSpec want = { .freq = AUDIO_HZ, .format = AUDIO_S16SYS, .channels = 1, .samples = 512 }, have;
    adev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (!adev) fprintf(stderr, "audio: %s\n", SDL_GetError());
    else SDL_PauseAudioDevice(adev, 0);
    volume = cfg.volume < 0 ? 0 : cfg.volume > 100 ? 100 : cfg.volume;
    int latency = cfg.audio_latency_ms > 10 ? cfg.audio_latency_ms : 10;

    uint64_t freq = SDL_GetPerformanceFrequency();
    uint64_t next = SDL_GetPerformanceCounter();
    while (handle_events()) {
        const uint8_t *ks = SDL_GetKeyboardState(NULL);
        bool fast = (cfg.hotkeys[HK_FAST] && ks[cfg.hotkeys[HK_FAST]]) || frame_count < (uint64_t)cfg.boot_skip_frames;

        if (paused && !step_once) {
            present("PAUSED");
            if (!vsync) SDL_Delay(16);
            next = SDL_GetPerformanceCounter();
            continue;
        }
        if (step_once) {
            step_once = false;
            emulate_frame();
            pump_audio(true, 1);
            present("PAUSED");
            continue;
        }

        if (fast) {
            // run unthrottled (or up to the speed limit) for one display frame, silently
            uint64_t start = SDL_GetPerformanceCounter(), budget = freq / 60;
            int limit = cfg.fast_speed > 0 ? cfg.fast_speed : 1000;   // ~frames per display frame
            for (int n = 0; n < limit && SDL_GetPerformanceCounter() - start < budget; n++) {
                emulate_frame();
                pump_audio(true, 1);
            }
            if (adev) SDL_ClearQueuedAudio(adev);
            present(frame_count < (uint64_t)cfg.boot_skip_frames ? "" : ">> FAST");
            if (!vsync) {
                uint64_t now = SDL_GetPerformanceCounter();
                if (now - start < budget) SDL_Delay((uint32_t)((budget - (now - start)) * 1000 / freq));
            }
            next = SDL_GetPerformanceCounter();
            continue;
        }

        emulate_frame();
        if (vsync) {
            // one game frame per refresh; nudge the resampling so the audio
            // queue stays near its target instead of slowly over- or underflowing
            double q = queued_ms();
            if (adev && q < 5) {        // (re)starting or after a hitch: pad with silence
                static int16_t silence[AUDIO_HZ / 10];
                int n = AUDIO_HZ * latency / 2000;
                SDL_QueueAudio(adev, silence, (Uint32)(n < AUDIO_HZ / 10 ? n : AUDIO_HZ / 10) * 2);
                q = queued_ms();
            }
            double err = (q - latency) / latency;
            if (err > 1) err = 1;
            if (err < -1) err = -1;
            pump_audio(false, display_hz / FRAME_HZ * (1 + 0.005 * err));
            present(NULL);
        } else {
            pump_audio(false, 1);
            present(NULL);
            if (adev) {
                // the audio device is the clock: keep `latency` ms queued, run ahead otherwise
                while (queued_ms() > latency) SDL_Delay(1);
            } else {
                next += (uint64_t)(freq / FRAME_HZ);
                uint64_t now = SDL_GetPerformanceCounter();
                if (now < next) SDL_Delay((uint32_t)((next - now) * 1000 / freq));
                else next = now;
            }
        }
    }
    SDL_Quit();
    return 0;
}
