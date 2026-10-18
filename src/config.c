// ki2.ini: a flat "[section] key = value" file. Unknown keys are ignored and
// missing ones keep their defaults, so old files keep working.
#include "config.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

config_t cfg;

typedef enum { T_STR, T_INT, T_BOOL, T_ENUM, T_KEY, T_PAD } type_t;

typedef struct {
    const char *section, *key;
    type_t type;
    void *ptr;
    const char *def;
    const char *help;
    const char *const *names;   // T_ENUM
} opt_t;

static const char *const scale_names[] = { "fit", "integer", "stretch", 0 };
static const char *const pacing_names[] = { "auto", "audio", "vsync", 0 };
static const char *const blood_names[] = { "none", "low", "medium", "high", 0 };
static const char *const filter_names[] = { "sharp", "smooth", "scanlines", "crt", 0 };

static const char *const button_names[NBUTTONS] = {
    "high_quick", "high_medium", "high_fierce", "low_quick", "low_medium", "low_fierce",
    "up", "down", "left", "right", "start", "coin",
};
static const char *const p1_keys[NBUTTONS] = { "A", "S", "D", "Z", "X", "C", "Up", "Down", "Left", "Right", "1", "5" };
static const char *const p2_keys[NBUTTONS] = { "T", "Y", "U", "G", "H", "B", "I", "K", "J", "L", "2", "6" };
static const char *const pad_defaults[NBUTTONS] = {
    "x", "y", "rightshoulder", "a", "b", "righttrigger", "dpup", "dpdown", "dpleft", "dpright", "start", "back",
};
static const char *const hotkey_names[NHOTKEYS] = {
    "menu", "pause", "frame_advance", "fast_forward", "rewind", "save_state", "load_state", "next_slot",
    "fullscreen", "screenshot", "volume_up", "volume_down", "mute", "test", "service",
    "game_volume_up", "game_volume_down", "input_display", "training", "dummy_record", "dummy_play",
    "performance",
};
static const char *const hotkey_defaults[NHOTKEYS] = {
    "F1", "P", ".", "Tab", "Backspace", "F5", "F7", "F6",
    "F11", "F12", "=", "-", "M", "F2", "9", "PageUp", "PageDown", "F3", "F4", "F8", "F9", "F10",
};

#define MAX_OPTS 256
static opt_t opts[MAX_OPTS];
static int nopts;
static char key_names[2][NBUTTONS][24], pad_keys[NBUTTONS][24], hk_keys[NHOTKEYS][24];

static void add(const char *sec, const char *key, type_t t, void *p, const char *def, const char *help, const char *const *names)
{
    if (nopts == MAX_OPTS) { fprintf(stderr, "config: too many settings, %s ignored\n", key); return; }
    opts[nopts++] = (opt_t){ sec, key, t, p, def, help, names };
}

static void build_table(void)
{
    if (nopts) return;
    add("paths", "rom", T_STR, cfg.rom, "", "boot ROM; empty = ki2-l14.u98 here or in run/", 0);
    add("paths", "disk", T_STR, cfg.disk, "", "raw disk image; empty = kinst2.raw", 0);
    add("paths", "sound", T_STR, cfg.sound, "", "directory with ki2_l1.u10..u36; empty = beside the ROM", 0);

    add("video", "fullscreen", T_BOOL, &cfg.fullscreen, "no", "start in fullscreen (toggle with the fullscreen hotkey or Alt+Enter)", 0);
    add("video", "fullscreen_exclusive", T_BOOL, &cfg.fullscreen_exclusive, "no", "exclusive fullscreen (sets the display mode) instead of borderless", 0);
    add("video", "fullscreen_mode", T_STR, cfg.fullscreen_mode, "", "display mode for exclusive fullscreen, e.g. 1920x1080@60; empty = current", 0);
    add("video", "window_scale", T_INT, &cfg.window_scale, "3", "initial window size as a multiple of 320x240", 0);
    add("video", "performance_overlay", T_BOOL, &cfg.perf_overlay, "no", "show frame rate, emulation speed and audio buffer", 0);
    add("video", "window_x", T_INT, &cfg.window_x, "0", "last window position and size (remembered on exit)", 0);
    add("video", "window_y", T_INT, &cfg.window_y, "0", 0, 0);
    add("video", "window_w", T_INT, &cfg.window_w, "0", 0, 0);
    add("video", "window_h", T_INT, &cfg.window_h, "0", 0, 0);
    add("video", "scale", T_ENUM, &cfg.scale_mode, "fit", "fit (largest 4:3), integer (sharpest, may border), stretch", scale_names);
    add("video", "shader", T_STR, cfg.shader, "", "a libretro .glslp preset under shaders/ (overrides filter), e.g. shaders/crt/crt-lottes.glslp", 0);
    add("video", "shader_params", T_STR, cfg.shader_params, "", "adjusted shader parameters (set from the menu)", 0);
    add("video", "filter", T_ENUM, &cfg.filter, "sharp",
        "sharp (crisp pixels), smooth (sharpened bicubic), scanlines, crt (scanlines, mask, curvature)", filter_names);
    add("video", "pacing", T_ENUM, &cfg.pacing, "auto",
        "audio: original 58.96 Hz timed by the sound card; vsync: one frame per display refresh"
        " (smooth on 60 Hz, game runs 1.7% fast); auto: vsync on ~60 Hz displays, else audio", pacing_names);

    add("audio", "volume", T_INT, &cfg.volume, "100", "0-100", 0);
    add("audio", "latency_ms", T_INT, &cfg.audio_latency_ms, "70", "audio buffer; lower = less delay, more risk of crackle", 0);

    add("system", "pause_unfocused", T_BOOL, &cfg.pause_unfocused, "no", "pause while the window is in the background", 0);
    add("system", "mute_unfocused", T_BOOL, &cfg.mute_unfocused, "no", "mute while the window is in the background", 0);
    add("system", "boot_skip_frames", T_INT, &cfg.boot_skip_frames, "90",
        "fast-forward this many frames at startup (90 = blank boot screens, 1000 = to the K2 logo)", 0);
    add("system", "auto_coin", T_BOOL, &cfg.auto_coin, "yes", "Start inserts coins when there are not enough credits", 0);
    add("system", "fast_forward_speed", T_INT, &cfg.fast_speed, "0", "fast-forward limit in x realtime, 0 = as fast as possible", 0);
    add("system", "run_ahead", T_INT, &cfg.run_ahead, "0",
        "0-2: hide the game's built-in input lag (1 frame) by emulating ahead; 1 is enough, 2 needs a fast CPU", 0);
    add("system", "rewind", T_BOOL, &cfg.rewind, "yes", "keep a rewind history (hold the rewind hotkey)", 0);
    add("system", "rewind_mb", T_INT, &cfg.rewind_mb, "256", "memory for it; about 85 KB per frame (256 MB = ~50 s)", 0);
    add("system", "input_display", T_BOOL, &cfg.input_display, "no", "show both players' recent inputs (numpad notation)", 0);
    add("system", "training", T_BOOL, &cfg.training, "no",
        "training mode: health refills after each combo, the clock stops (start a 2-player game for a dummy)", 0);
    add("game", "free_play", T_BOOL, &cfg.free_play, "yes", "no coins needed: just press Start (the board's coinage DIP switch)", 0);
    add("game", "blood", T_ENUM, &cfg.blood, "high", "none, low, medium, high", blood_names);
    add("game", "finishing_moves", T_BOOL, &cfg.finishing_moves, "yes", "Ultra combos and No Mercy finishers", 0);
    add("game", "demo_sounds", T_BOOL, &cfg.demo_sounds, "yes", "sound during the attract mode", 0);
    add("netplay", "address", T_STR, cfg.net_address, "", "host to join (name or IP, optionally :port)", 0);
    add("netplay", "port", T_INT, &cfg.net_port, "7845", "UDP port to host on (forward it on your router, or use a VPN such as Tailscale)", 0);
    add("netplay", "input_delay", T_INT, &cfg.net_delay, "2",
        "frames before your input takes effect; more = fewer rollbacks. 1-2 for nearby players, 3-4 far away", 0);
    add("system", "state_slot", T_INT, &cfg.state_slot, "1", "save state slot used by the hotkeys, 1-9", 0);

    for (int p = 0; p < 2; p++)
        for (int b = 0; b < NBUTTONS; b++) {
            static char names[2][NBUTTONS][32];
            snprintf(names[p][b], 32, "p%d_%s", p + 1, button_names[b]);
            snprintf(key_names[p][b], 24, "%s", (p ? p2_keys : p1_keys)[b]);
            add("keyboard", names[p][b], T_KEY, &cfg.keys[p][b], key_names[p][b],
                p == 0 && b == 0 ? "SDL key names, e.g. A, Left Shift, Keypad 4, Space; empty = unbound" : 0, 0);
        }
    add("keyboard", "socd_cleaning", T_BOOL, &cfg.socd, "yes", "left+right together = neutral, up+down = up (as on fight sticks and hitboxes)", 0);
    for (int h = 0; h < NHOTKEYS; h++) {
        snprintf(hk_keys[h], 24, "%s", hotkey_defaults[h]);
        add("hotkeys", hotkey_names[h], T_KEY, &cfg.hotkeys[h], hk_keys[h],
            h == HK_MENU ? "settings menu (Esc also opens it)" : h == HK_FAST ? "hold to fast-forward" :
            h == HK_REWIND ? "hold to rewind" : h == HK_STEP ? "while paused, advance one frame" :
            h == HK_DUMMY_RECORD ? "training: record player 2's actions with your controls" : 0, 0);
    }
    for (int b = 0; b < NBUTTONS; b++) {
        snprintf(pad_keys[b], 24, "%s", pad_defaults[b]);
        add("gamepad", button_names[b], T_PAD, &cfg.pad[b], pad_keys[b],
            b == 0 ? "a b x y back start leftshoulder rightshoulder lefttrigger righttrigger dpup dpdown"
                     " dpleft dpright leftstick rightstick; first pad = player 1, second = player 2" : 0, 0);
    }
    add("gamepad", "deadzone", T_INT, &cfg.pad_deadzone, "16000", "left stick deadzone, 0-32767", 0);
    add("gamepad", "rumble", T_BOOL, &cfg.rumble, "yes", "vibrate when your character is hit", 0);
    add("gamepad", "menu", T_PAD, &cfg.pad_menu, "guide", "hotkeys on the pad (Back+Start also opens the menu)", 0);
    add("gamepad", "rewind", T_PAD, &cfg.pad_rewind, "", "hold to rewind, e.g. leftshoulder", 0);
    add("gamepad", "fast_forward", T_PAD, &cfg.pad_fast, "", "hold to fast-forward, e.g. lefttrigger", 0);
}

static int parse_pad(const char *v)
{
    if (!*v) return PAD_NONE;
    if (!SDL_strcasecmp(v, "lefttrigger")) return PAD_LT;
    if (!SDL_strcasecmp(v, "righttrigger")) return PAD_RT;
    SDL_GameControllerButton b = SDL_GameControllerGetButtonFromString(v);
    return b == SDL_CONTROLLER_BUTTON_INVALID ? PAD_NONE : (int)b;
}

static bool set(opt_t *o, const char *v)
{
    switch (o->type) {
    case T_STR: snprintf((char *)o->ptr, 1024, "%s", v); return true;
    case T_INT: *(int *)o->ptr = atoi(v); return true;
    case T_BOOL:
        *(bool *)o->ptr = !SDL_strcasecmp(v, "yes") || !SDL_strcasecmp(v, "true") || !SDL_strcasecmp(v, "on") || !strcmp(v, "1");
        return true;
    case T_ENUM:
        for (int i = 0; o->names[i]; i++)
            if (!SDL_strcasecmp(v, o->names[i])) { *(int *)o->ptr = i; return true; }
        return false;
    case T_KEY: {
        SDL_Scancode sc = *v ? SDL_GetScancodeFromName(v) : SDL_SCANCODE_UNKNOWN;
        *(SDL_Scancode *)o->ptr = sc;
        return !*v || sc != SDL_SCANCODE_UNKNOWN;
    }
    case T_PAD:
        *(int *)o->ptr = parse_pad(v);
        return !*v || *(int *)o->ptr != PAD_NONE;
    }
    return false;
}

static char *trim(char *s)
{
    while (isspace((unsigned char)*s)) s++;
    char *e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) *--e = 0;
    return s;
}

static char cfg_path[4096];

static const char *pad_name(int b)
{
    if (b == PAD_LT) return "lefttrigger";
    if (b == PAD_RT) return "righttrigger";
    if (b == PAD_NONE) return "";
    const char *s = SDL_GameControllerGetStringForButton((SDL_GameControllerButton)b);
    return s ? s : "";
}

static void format(const opt_t *o, char *buf, size_t n, bool defaults)
{
    if (defaults) { snprintf(buf, n, "%s", o->def); return; }
    switch (o->type) {
    case T_STR: snprintf(buf, n, "%s", (const char *)o->ptr); break;
    case T_INT: snprintf(buf, n, "%d", *(int *)o->ptr); break;
    case T_BOOL: snprintf(buf, n, "%s", *(bool *)o->ptr ? "yes" : "no"); break;
    case T_ENUM: snprintf(buf, n, "%s", o->names[*(int *)o->ptr]); break;
    case T_KEY: {
        SDL_Scancode sc = *(SDL_Scancode *)o->ptr;
        snprintf(buf, n, "%s", sc ? SDL_GetScancodeName(sc) : "");
        break;
    }
    case T_PAD: snprintf(buf, n, "%s", pad_name(*(int *)o->ptr)); break;
    }
}

static bool write_file(const char *path, bool defaults)
{
    FILE *f = fopen(path, "w");
    if (!f) return false;
    fprintf(f, "; UltraCombo settings. Delete this file to get the defaults back.\n");
    const char *sec = "";
    for (int i = 0; i < nopts; i++) {
        opt_t *o = &opts[i];
        char val[1024];
        if (strcmp(sec, o->section)) { fprintf(f, "\n[%s]\n", o->section); sec = o->section; }
        if (o->help) fprintf(f, "; %s\n", o->help);
        format(o, val, sizeof val, defaults);
        fprintf(f, "%s = %s\n", o->key, val);
    }
    return fclose(f) == 0;
}

bool config_save(void) { return *cfg_path && write_file(cfg_path, false); }

static void write_default(const char *path)
{
    if (write_file(path, true)) fprintf(stderr, "config: wrote defaults to %s\n", path);
}

void config_load(const char *path)
{
    build_table();
    snprintf(cfg_path, sizeof cfg_path, "%s", path);
    for (int i = 0; i < nopts; i++) set(&opts[i], opts[i].def);

    FILE *f = fopen(path, "r");
    if (!f) { write_default(path); return; }
    char line[2048], sec[64] = "";
    int n = 0;
    while (fgets(line, sizeof line, f)) {
        n++;
        char *s = trim(line);
        if (!*s || *s == ';' || *s == '#') continue;
        if (*s == '[') {
            char *e = strchr(s, ']');
            if (e) { *e = 0; snprintf(sec, sizeof sec, "%s", trim(s + 1)); }
            continue;
        }
        char *eq = strchr(s, '=');
        if (!eq) continue;
        *eq = 0;
        char *k = trim(s), *v = trim(eq + 1);
        bool found = false;
        for (int i = 0; i < nopts; i++)
            if (!SDL_strcasecmp(opts[i].section, sec) && !SDL_strcasecmp(opts[i].key, k)) {
                found = true;
                if (!set(&opts[i], v)) {
                    fprintf(stderr, "%s:%d: bad value \"%s\" for %s, using \"%s\"\n", path, n, v, k, opts[i].def);
                    set(&opts[i], opts[i].def);
                }
            }
        if (!found && !(!SDL_strcasecmp(sec, "video") && !SDL_strcasecmp(k, "smooth")))   // replaced by filter
            fprintf(stderr, "%s:%d: unknown setting [%s] %s\n", path, n, sec, k);
    }
    fclose(f);
}

// KI2 DIP switches (MAME's kinst2 layout): bits 0-1 blood level, 2 demo sounds,
// 3 finishing moves, 4 warning screen, 8 coinage from switches, 9-13 coinage.
uint32_t config_dsw(void)
{
    uint32_t d = 0xffffffffu;
    d = (d & ~3u) | (uint32_t)(cfg.blood & 3);
    if (!cfg.demo_sounds) d &= ~0x04u;
    if (!cfg.finishing_moves) d &= ~0x08u;
    if (cfg.free_play) d = (d & ~0x3e00u) | 0x3000u;          // USA - free play
    return d;
}
