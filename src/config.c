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
    "pause", "frame_advance", "fast_forward", "fullscreen", "screenshot",
    "volume_up", "volume_down", "mute", "test", "service", "game_volume_up", "game_volume_down",
};
static const char *const hotkey_defaults[NHOTKEYS] = {
    "P", ".", "Tab", "F11", "F12", "=", "-", "M", "F2", "9", "PageUp", "PageDown",
};

#define MAX_OPTS 96
static opt_t opts[MAX_OPTS];
static int nopts;
static char key_names[2][NBUTTONS][24], pad_keys[NBUTTONS][24], hk_keys[NHOTKEYS][24];

static void add(const char *sec, const char *key, type_t t, void *p, const char *def, const char *help, const char *const *names)
{
    opts[nopts++] = (opt_t){ sec, key, t, p, def, help, names };
}

static void build_table(void)
{
    if (nopts) return;
    add("paths", "rom", T_STR, cfg.rom, "", "boot ROM; empty = ki2-l14.u98 here or in run/", 0);
    add("paths", "disk", T_STR, cfg.disk, "", "raw disk image; empty = kinst2.raw", 0);
    add("paths", "sound", T_STR, cfg.sound, "", "directory with ki2_l1.u10..u36; empty = beside the ROM", 0);

    add("video", "fullscreen", T_BOOL, &cfg.fullscreen, "no", "start in fullscreen (toggle with the fullscreen hotkey or Alt+Enter)", 0);
    add("video", "window_scale", T_INT, &cfg.window_scale, "3", "initial window size as a multiple of 320x240", 0);
    add("video", "scale", T_ENUM, &cfg.scale_mode, "fit", "fit (largest 4:3), integer (sharpest, may border), stretch", scale_names);
    add("video", "smooth", T_BOOL, &cfg.smooth, "no", "bilinear filtering instead of sharp pixels", 0);
    add("video", "pacing", T_ENUM, &cfg.pacing, "auto",
        "audio: original 58.96 Hz timed by the sound card; vsync: one frame per display refresh"
        " (smooth on 60 Hz, game runs 1.7% fast); auto: vsync on ~60 Hz displays, else audio", pacing_names);

    add("audio", "volume", T_INT, &cfg.volume, "100", "0-100", 0);
    add("audio", "latency_ms", T_INT, &cfg.audio_latency_ms, "70", "audio buffer; lower = less delay, more risk of crackle", 0);

    add("system", "boot_skip_frames", T_INT, &cfg.boot_skip_frames, "90",
        "fast-forward this many frames at startup (90 = blank boot screens, 1000 = to the K2 logo)", 0);
    add("system", "auto_coin", T_BOOL, &cfg.auto_coin, "yes", "Start inserts coins when there are not enough credits", 0);
    add("system", "fast_forward_speed", T_INT, &cfg.fast_speed, "0", "fast-forward limit in x realtime, 0 = as fast as possible", 0);

    for (int p = 0; p < 2; p++)
        for (int b = 0; b < NBUTTONS; b++) {
            static char names[2][NBUTTONS][32];
            snprintf(names[p][b], 32, "p%d_%s", p + 1, button_names[b]);
            snprintf(key_names[p][b], 24, "%s", (p ? p2_keys : p1_keys)[b]);
            add("keyboard", names[p][b], T_KEY, &cfg.keys[p][b], key_names[p][b],
                p == 0 && b == 0 ? "SDL key names, e.g. A, Left Shift, Keypad 4, Space; empty = unbound" : 0, 0);
        }
    for (int h = 0; h < NHOTKEYS; h++) {
        snprintf(hk_keys[h], 24, "%s", hotkey_defaults[h]);
        add("hotkeys", hotkey_names[h], T_KEY, &cfg.hotkeys[h], hk_keys[h],
            h == HK_FAST ? "hold to fast-forward" : h == HK_STEP ? "while paused, advance one frame" : 0, 0);
    }
    for (int b = 0; b < NBUTTONS; b++) {
        snprintf(pad_keys[b], 24, "%s", pad_defaults[b]);
        add("gamepad", button_names[b], T_PAD, &cfg.pad[b], pad_keys[b],
            b == 0 ? "a b x y back start leftshoulder rightshoulder lefttrigger righttrigger dpup dpdown"
                     " dpleft dpright leftstick rightstick; first pad = player 1, second = player 2" : 0, 0);
    }
    add("gamepad", "deadzone", T_INT, &cfg.pad_deadzone, "16000", "left stick deadzone, 0-32767", 0);
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

static void write_default(const char *path)
{
    FILE *f = fopen(path, "w");
    if (!f) return;
    fprintf(f, "; Killer Instinct 2 settings. Delete this file to get the defaults back.\n");
    const char *sec = "";
    for (int i = 0; i < nopts; i++) {
        opt_t *o = &opts[i];
        if (strcmp(sec, o->section)) { fprintf(f, "\n[%s]\n", o->section); sec = o->section; }
        if (o->help) fprintf(f, "; %s\n", o->help);
        fprintf(f, "%s = %s\n", o->key, o->def);
    }
    fclose(f);
    fprintf(stderr, "config: wrote defaults to %s\n", path);
}

void config_load(const char *path)
{
    build_table();
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
        if (!found) fprintf(stderr, "%s:%d: unknown setting [%s] %s\n", path, n, sec, k);
    }
    fclose(f);
}
