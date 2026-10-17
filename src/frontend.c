// Windowed frontend: video, audio, input, and the quality-of-life features
// (pause, fast-forward, fullscreen, screenshots, auto-coin, volume).
//
// Everything that reaches the emulated machine goes through the input ports
// once per frame (hw.p1, hw.p2, hw.volume), so the emulation stays a pure
// function of those inputs (needed for netplay later).
#include "config.h"
#include "ide.h"
#include "machine.h"
#include "mem.h"
#include "osd.h"
#include "png.h"
#include "sound.h"
#include "state.h"
#include "training.h"
#include "video.h"
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
static SDL_AudioDeviceID adev;
static uint32_t px[320 * 240];     // game image
static uint32_t ov[320 * 240];     // overlay: messages, menu, input display

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
        video_set_vsync(vsync);
        fprintf(stderr, "video: %.0f Hz display, %s pacing\n", display_hz, vsync ? "vsync" : "audio");
    }
}

static void set_fullscreen(bool on)
{
    fullscreen = on;
    SDL_SetWindowFullscreen(win, on ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
    SDL_ShowCursor(on ? SDL_DISABLE : SDL_ENABLE);
}

static void draw_menu(uint32_t *ov);
static void draw_inputs(uint32_t *ov);

static void present(const char *status)
{
    if (!status) status = training_status();
    to_rgb(px);
    osd_clear(ov);
    draw_inputs(ov);
    osd_draw(ov, status);
    draw_menu(ov);
    video_present(px, ov, cfg.filter, cfg.scale_mode);
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

/* ---------------------------------------------------------------- */
/* Input display                                                    */
/* ---------------------------------------------------------------- */

#define HIST 12
static struct { uint32_t held; int frames; } inhist[2][HIST];   // [0] is the newest

static void record_inputs(const uint32_t held[2])
{
    const uint32_t mask = (1u << BTN_START) - 1;               // directions and attacks
    for (int p = 0; p < 2; p++) {
        uint32_t h = held[p] & mask;
        if (inhist[p][0].frames && inhist[p][0].held == h) {
            if (inhist[p][0].frames < 99) inhist[p][0].frames++;
            continue;
        }
        memmove(&inhist[p][1], &inhist[p][0], (HIST - 1) * sizeof inhist[p][0]);
        inhist[p][0].held = h;
        inhist[p][0].frames = 1;
    }
}

static void describe(uint32_t h, char *buf, size_t n)
{
    int up = h >> BTN_UP & 1, down = h >> BTN_DOWN & 1, left = h >> BTN_LEFT & 1, right = h >> BTN_RIGHT & 1;
    int row = up ? 2 : down ? 0 : 1, col = left ? 0 : right ? 2 : 1;
    static const char *const names[6] = { "QP", "MP", "FP", "QK", "MK", "FK" };
    int len = snprintf(buf, n, "%d", row * 3 + col + 1);
    for (int b = 0; b < 6; b++)
        if (h >> b & 1) len += snprintf(buf + len, n - (size_t)len, " %s", names[b]);
}

static void draw_inputs(uint32_t *out)
{
    if (!cfg.input_display) return;
    for (int p = 0; p < 2; p++) {
        int y = 44;
        osd_box(out, p ? 320 - 62 : 0, y - 2, 62, HIST * 9 + 3, 0x90000000u);
        for (int i = 0; i < HIST && inhist[p][i].frames; i++, y += 9) {
            char a[32], line[48];
            describe(inhist[p][i].held, a, sizeof a);
            snprintf(line, sizeof line, "%2d %s", inhist[p][i].frames, a);
            uint32_t c = i == 0 ? 0xffffff : 0xa0a0a0;
            int x = p ? 320 - 4 - (int)strlen(line) * 6 : 3;
            osd_text(out, x, y, line, c);
        }
    }
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

static bool key_held(int hk)
{
    const uint8_t *ks = SDL_GetKeyboardState(NULL);
    return cfg.hotkeys[hk] && ks[cfg.hotkeys[hk]];
}

static bool pad_held(int b)
{
    for (int i = 0; i < MAX_PADS; i++)
        if (pads[i] && pad_pressed(pads[i], b)) return true;
    return false;
}

static void set_volume(int v)
{
    volume = v < 0 ? 0 : v > 100 ? 100 : v;
    muted = false;
    osd_message("Volume %d%%", volume);
}

static void stop_audio(void) { if (adev) SDL_ClearQueuedAudio(adev); }

/* ---------------------------------------------------------------- */
/* Save states                                                      */
/* ---------------------------------------------------------------- */

static int slot(void) { return cfg.state_slot >= 1 && cfg.state_slot <= 9 ? cfg.state_slot : 1; }

static void slot_path(char *buf, size_t n, int s)
{
    char dir[4096];
    snprintf(dir, sizeof dir, "%sstates", base_dir());
    make_dir(dir);
    snprintf(buf, n, "%s/slot%d.ki2s", dir, s);
}

static void save_state(void)
{
    char path[4200];
    slot_path(path, sizeof path, slot());
    if (state_save_file(path)) osd_message("Saved state %d", slot());
    else osd_message("Could not save state %d", slot());
}

static void load_state(void)
{
    char path[4200];
    slot_path(path, sizeof path, slot());
    FILE *f = fopen(path, "rb");
    if (!f) { osd_message("State %d is empty", slot()); return; }
    fclose(f);
    if (!state_load_file(path)) { osd_message("State %d is from another version", slot()); return; }
    ac.active = false;
    if (frame_count < (uint64_t)cfg.boot_skip_frames) frame_count = (uint64_t)cfg.boot_skip_frames;
    stop_audio();
    osd_message("Loaded state %d", slot());
}

static void apply_rewind_setting(void)
{
    state_set_history(cfg.rewind ? (size_t)(cfg.rewind_mb > 8 ? cfg.rewind_mb : 8) << 20 : 0);
}

/* ---------------------------------------------------------------- */
/* Menu                                                             */
/* ---------------------------------------------------------------- */

enum { M_RESUME, M_SAVE, M_LOAD, M_SLOT, M_RUNAHEAD, M_REWIND, M_TRAINING, M_INPUTS, M_FULLSCREEN, M_SCALE, M_FILTER,
       M_PACING, M_VOLUME, M_AUTOCOIN, M_RESET, M_QUIT, M_COUNT };

static bool menu_open, menu_changed, quit_requested;
static int menu_sel;

static void draw_menu(uint32_t *out)
{
    if (!menu_open) return;
    static const char *const scale_names[] = { "Fit", "Integer", "Stretch" };
    static const char *const pacing_names[] = { "Auto", "Audio", "Vsync" };
    static const char *const ra_names[] = { "Off", "1 frame", "2 frames" };
    char b[M_COUNT][64];
    const char *items[M_COUNT];
    int ra = cfg.run_ahead < 0 ? 0 : cfg.run_ahead > 2 ? 2 : cfg.run_ahead;
    snprintf(b[M_RESUME], 64, "Resume");
    snprintf(b[M_SAVE], 64, "Save state\tslot %d", slot());
    snprintf(b[M_LOAD], 64, "Load state\tslot %d", slot());
    snprintf(b[M_SLOT], 64, "State slot\t< %d >", slot());
    snprintf(b[M_RUNAHEAD], 64, "Run-ahead (less lag)\t%s", ra_names[ra]);
    snprintf(b[M_REWIND], 64, "Rewind\t%s", cfg.rewind ? "On" : "Off");
    snprintf(b[M_TRAINING], 64, "Training mode\t%s", cfg.training ? "On" : "Off");
    snprintf(b[M_INPUTS], 64, "Input display\t%s", cfg.input_display ? "On" : "Off");
    snprintf(b[M_FULLSCREEN], 64, "Fullscreen\t%s", fullscreen ? "On" : "Off");
    snprintf(b[M_SCALE], 64, "Scaling\t%s", scale_names[cfg.scale_mode]);
    static const char *const filter_names[] = { "Sharp", "Smooth", "Scanlines", "CRT" };
    snprintf(b[M_FILTER], 64, "Filter\t%s", filter_names[cfg.filter]);
    snprintf(b[M_PACING], 64, "Frame pacing\t%s%s", pacing_names[cfg.pacing],
             cfg.pacing == PACING_AUTO ? (vsync ? " (vsync)" : " (audio)") : "");
    snprintf(b[M_VOLUME], 64, "Volume\t%s", muted ? "Muted" : "");
    if (!muted) snprintf(b[M_VOLUME], 64, "Volume\t%d%%", volume);
    snprintf(b[M_AUTOCOIN], 64, "Auto-coin\t%s", cfg.auto_coin ? "On" : "Off");
    snprintf(b[M_RESET], 64, "Reset game");
    snprintf(b[M_QUIT], 64, "Quit");
    for (int i = 0; i < M_COUNT; i++) items[i] = b[i];
    osd_menu(out, "KILLER INSTINCT 2", items, M_COUNT, menu_sel, "ENTER/A SELECT   ESC/B CLOSE");
}

static void menu_close(void)
{
    menu_open = false;
    if (menu_changed) {
        cfg.volume = volume;
        cfg.fullscreen = fullscreen;
        if (config_save()) osd_message("Settings saved");
        menu_changed = false;
    }
}

static void cycle(int *v, int n, int dir) { *v = ((*v + dir) % n + n) % n; }

// Left/right on the selected item (dir = -1/+1), or Enter (dir = 0).
static void menu_change(int dir)
{
    int d = dir ? dir : 1;
    switch (menu_sel) {
    case M_RESUME: if (!dir) menu_close(); return;
    case M_SAVE: if (!dir) { save_state(); menu_close(); } return;
    case M_LOAD: if (!dir) { load_state(); menu_close(); } return;
    case M_SLOT: { int s = slot() - 1; cycle(&s, 9, d); cfg.state_slot = s + 1; break; }
    case M_RUNAHEAD: cycle(&cfg.run_ahead, 3, d); break;
    case M_REWIND: cfg.rewind = !cfg.rewind; apply_rewind_setting(); break;
    case M_TRAINING: cfg.training = !cfg.training; break;
    case M_INPUTS: cfg.input_display = !cfg.input_display; break;
    case M_FULLSCREEN: set_fullscreen(!fullscreen); break;
    case M_SCALE: cycle(&cfg.scale_mode, 3, d); break;
    case M_FILTER: cycle(&cfg.filter, NFILTERS, d); break;
    case M_PACING: cycle(&cfg.pacing, 3, d); choose_pacing(); break;
    case M_VOLUME:
        if (!dir) muted = !muted;
        else { volume = volume + 10 * dir; volume = volume < 0 ? 0 : volume > 100 ? 100 : volume; muted = false; }
        break;
    case M_AUTOCOIN: cfg.auto_coin = !cfg.auto_coin; break;
    case M_RESET:
        if (dir) return;
        machine_reset();
        apply_rewind_setting();
        ac.active = false;
        frame_count = 0;
        stop_audio();
        menu_close();
        osd_message("Reset");
        return;
    case M_QUIT: if (!dir) quit_requested = true; return;
    }
    menu_changed = true;
}

static void menu_toggle(void)
{
    if (menu_open) menu_close();
    else { menu_open = true; stop_audio(); }
}

static void menu_nav(int up_down, int left_right, bool enter, bool back)
{
    if (up_down) cycle(&menu_sel, M_COUNT, up_down);
    if (left_right) menu_change(left_right);
    if (enter) menu_change(0);
    if (back) menu_close();
}

/* ---------------------------------------------------------------- */
/* Events                                                           */
/* ---------------------------------------------------------------- */

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
        case SDL_CONTROLLERBUTTONDOWN: {
            int b = ev.cbutton.button;
            SDL_GameController *c = SDL_GameControllerFromInstanceID(ev.cbutton.which);
            bool combo = c && ((b == SDL_CONTROLLER_BUTTON_START && SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_BACK)) ||
                               (b == SDL_CONTROLLER_BUTTON_BACK && SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_START)));
            if ((cfg.pad_menu != PAD_NONE && b == cfg.pad_menu) || combo) { menu_toggle(); break; }
            if (!menu_open) break;
            menu_nav(b == SDL_CONTROLLER_BUTTON_DPAD_UP ? -1 : b == SDL_CONTROLLER_BUTTON_DPAD_DOWN ? 1 : 0,
                     b == SDL_CONTROLLER_BUTTON_DPAD_LEFT ? -1 : b == SDL_CONTROLLER_BUTTON_DPAD_RIGHT ? 1 : 0,
                     b == SDL_CONTROLLER_BUTTON_A, b == SDL_CONTROLLER_BUTTON_B || b == SDL_CONTROLLER_BUTTON_START);
            break;
        }
        case SDL_KEYDOWN: {
            SDL_Scancode sc = ev.key.keysym.scancode;
            bool alt_enter = sc == SDL_SCANCODE_RETURN && (ev.key.keysym.mod & KMOD_ALT);
            if (sc == SDL_SCANCODE_ESCAPE || is_hotkey(sc, HK_MENU)) { if (!ev.key.repeat) menu_toggle(); break; }
            if (menu_open && !alt_enter) {
                menu_nav(sc == SDL_SCANCODE_UP ? -1 : sc == SDL_SCANCODE_DOWN ? 1 : 0,
                         sc == SDL_SCANCODE_LEFT ? -1 : sc == SDL_SCANCODE_RIGHT ? 1 : 0,
                         !ev.key.repeat && (sc == SDL_SCANCODE_RETURN || sc == SDL_SCANCODE_SPACE || sc == SDL_SCANCODE_KP_ENTER),
                         sc == SDL_SCANCODE_BACKSPACE);
                break;
            }
            if (ev.key.repeat && !is_hotkey(sc, HK_VOL_UP) && !is_hotkey(sc, HK_VOL_DOWN) && !is_hotkey(sc, HK_STEP)) break;
            if (alt_enter || is_hotkey(sc, HK_FULLSCREEN)) { set_fullscreen(!fullscreen); if (menu_open) menu_changed = true; }
            else if (is_hotkey(sc, HK_PAUSE)) { paused = !paused; if (paused) stop_audio(); }
            else if (is_hotkey(sc, HK_STEP)) { if (paused) step_once = true; else { paused = true; stop_audio(); } }
            else if (is_hotkey(sc, HK_SAVE_STATE)) save_state();
            else if (is_hotkey(sc, HK_LOAD_STATE)) load_state();
            else if (is_hotkey(sc, HK_NEXT_SLOT)) { cfg.state_slot = slot() % 9 + 1; osd_message("State slot %d", slot()); }
            else if (is_hotkey(sc, HK_SCREENSHOT)) screenshot();
            else if (is_hotkey(sc, HK_TRAINING)) { cfg.training = !cfg.training; osd_message("Training mode %s", cfg.training ? "on" : "off"); }
            else if (is_hotkey(sc, HK_DUMMY_RECORD)) training_record_toggle();
            else if (is_hotkey(sc, HK_DUMMY_PLAY)) training_play_toggle();
            else if (is_hotkey(sc, HK_INPUT_DISPLAY)) { cfg.input_display = !cfg.input_display; osd_message("Input display %s", cfg.input_display ? "on" : "off"); }
            else if (is_hotkey(sc, HK_VOL_UP)) set_volume(volume + 10);
            else if (is_hotkey(sc, HK_VOL_DOWN)) set_volume(volume - 10);
            else if (is_hotkey(sc, HK_MUTE)) { muted = !muted; osd_message(muted ? "Muted" : "Volume %d%%", volume); }
            break;
        }
        }
    }
    return !quit_requested;
}

// One real frame of the game: inputs, emulation, and a checkpoint for
// rollback/rewind. Every emulated frame must end in a checkpoint.
static void emulate_frame(void)
{
    uint32_t held[2];
    read_players(held);
    training_inputs(held);
    auto_coin(held);
    record_inputs(held);
    set_ports(held);
    training_apply(true);
    run_frame();
    state_checkpoint();
    frame_count++;
}

// Show the frame `run_ahead` frames in the future (same inputs), then
// return to the real one: the game's own input lag disappears from view.
static void present_ahead(int n)
{
    if (n <= 0) { present(NULL); return; }
    for (int k = 0; k < n; k++) { training_apply(false); run_frame(); pump_audio(true, 1); }
    present(NULL);
    state_rollback();
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
    win = video_open("Killer Instinct 2", 320 * scale, 240 * scale);
    if (!win) { fprintf(stderr, "SDL: %s\n", SDL_GetError()); return 1; }
    fprintf(stderr, "video: %s\n", video_backend());
    if (cfg.fullscreen) set_fullscreen(true);
    choose_pacing();
    apply_rewind_setting();

    SDL_AudioSpec want = { .freq = AUDIO_HZ, .format = AUDIO_S16SYS, .channels = 1, .samples = 512 }, have;
    adev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (!adev) fprintf(stderr, "audio: %s\n", SDL_GetError());
    else SDL_PauseAudioDevice(adev, 0);
    volume = cfg.volume < 0 ? 0 : cfg.volume > 100 ? 100 : cfg.volume;
    int latency = cfg.audio_latency_ms > 10 ? cfg.audio_latency_ms : 10;

    uint64_t freq = SDL_GetPerformanceFrequency();
    uint64_t next = SDL_GetPerformanceCounter();
    while (handle_events()) {
        static uint32_t last_flush;
        if (SDL_GetTicks() - last_flush > 5000) { ide_flush(); last_flush = SDL_GetTicks(); }
        bool booting = frame_count < (uint64_t)cfg.boot_skip_frames;
        bool fast = key_held(HK_FAST) || pad_held(cfg.pad_fast) || booting;
        bool rewinding = cfg.rewind && (key_held(HK_REWIND) || pad_held(cfg.pad_rewind));

        if (menu_open || (paused && !step_once)) {
            present(menu_open ? NULL : "PAUSED");
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
        if (rewinding && !booting) {
            // one checkpoint back per display frame: rewinds at normal speed
            bool more = state_rewind();
            stop_audio();
            present(more ? "<< REWIND" : "REWIND: NO MORE HISTORY");
            if (!vsync) SDL_Delay((uint32_t)(1000 / FRAME_HZ));
            next = SDL_GetPerformanceCounter();
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
            stop_audio();
            present(booting ? "" : ">> FAST");
            if (!vsync) {
                uint64_t now = SDL_GetPerformanceCounter();
                if (now - start < budget) SDL_Delay((uint32_t)((budget - (now - start)) * 1000 / freq));
            }
            next = SDL_GetPerformanceCounter();
            continue;
        }

        emulate_frame();
        int ahead = cfg.run_ahead < 0 ? 0 : cfg.run_ahead > 2 ? 2 : cfg.run_ahead;
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
            present_ahead(ahead);
        } else {
            pump_audio(false, 1);
            present_ahead(ahead);
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
    if (menu_changed) menu_close();
    video_close();
    SDL_Quit();
    return 0;
}
