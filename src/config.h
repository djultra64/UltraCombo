// Frontend settings, read from ki2.ini beside the executable.
#pragma once
#include <SDL2/SDL.h>
#include <stdbool.h>

// Cabinet buttons per player, in the bit order of the input ports.
enum {
    BTN_HQ, BTN_HM, BTN_HF, BTN_LQ, BTN_LM, BTN_LF,
    BTN_UP, BTN_DOWN, BTN_LEFT, BTN_RIGHT, BTN_START, BTN_COIN,
    NBUTTONS
};

enum { HK_MENU, HK_PAUSE, HK_STEP, HK_FAST, HK_REWIND, HK_SAVE_STATE, HK_LOAD_STATE, HK_NEXT_SLOT,
       HK_FULLSCREEN, HK_SCREENSHOT, HK_VOL_UP, HK_VOL_DOWN, HK_MUTE, HK_TEST, HK_SERVICE,
       HK_GAME_VOL_UP, HK_GAME_VOL_DOWN, NHOTKEYS };

enum { SCALE_FIT, SCALE_INTEGER, SCALE_STRETCH };
enum { PACING_AUTO, PACING_AUDIO, PACING_VSYNC };

// Pad inputs: SDL_GameControllerButton values, plus the two triggers.
#define PAD_LT (SDL_CONTROLLER_BUTTON_MAX + 0)
#define PAD_RT (SDL_CONTROLLER_BUTTON_MAX + 1)
#define PAD_NONE (-1)

typedef struct {
    char rom[1024], disk[1024], sound[1024];

    bool fullscreen;
    int  window_scale;
    int  scale_mode;
    bool smooth;
    int  pacing;

    int  volume;               // 0-100
    int  audio_latency_ms;

    int  boot_skip_frames;     // run this many frames unthrottled and silent at startup
    bool auto_coin;
    int  fast_speed;           // fast-forward speed limit, x realtime (0 = unlimited)
    int  run_ahead;            // frames (0-2) emulated ahead to hide the game's input lag
    bool rewind;
    int  rewind_mb;            // memory for the rewind history
    int  state_slot;           // 1-9

    SDL_Scancode keys[2][NBUTTONS];
    SDL_Scancode hotkeys[NHOTKEYS];
    int  pad[NBUTTONS];        // same layout for both pads; PAD_* or SDL button
    int  pad_deadzone;         // 0-32767
    int  pad_menu, pad_rewind, pad_fast;   // gamepad hotkeys (PAD_NONE = unbound)
} config_t;

extern config_t cfg;

// Load from `path`, writing a commented default file there if it does not exist.
void config_load(const char *path);
// Write the current settings back (to the path given to config_load).
bool config_save(void);
