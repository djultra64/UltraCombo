// What differs between the games UltraCombo plays. Both run on the same
// board (MAME's kinst driver); each program links one profile: game_ki1.c
// (UltraCombo) or game_ki2.c (UltraCombo 2).
#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    const char *label;          // as shown to the player, e.g. "1.5d"
    const char *rom;            // boot ROM file name
} game_version_t;

typedef struct {
    const char *id;             // "ki1" / "ki2"
    const char *title;          // the game it plays, e.g. "Killer Instinct 2"
    const char *product;        // window and dialog titles, e.g. "UltraCombo 2"
    const char *file_name;      // program, data folder and packages, e.g. "UltraCombo2"
    const char *icon_text;      // up to 3 characters; a final digit is drawn glowing

    game_version_t versions[4]; // [0] is the default; label NULL ends the list
    const char *sound_roms[8];  // U10-U13, U33-U36
    const char *disk;           // raw disk image file name
    const char *chd;            // the MAME CHD it is extracted from

    // drive geometry (from the CHD metadata) and the identify word where the
    // game checks the drive model
    uint32_t cyls, heads, secs;
    int serial_offset;

    // I/O ports (physical addresses). Each write port is paired with a read
    // port the same way on both boards: P1/video control, P2/sound reset,
    // volume/sound control, "unused"/sound data.
    struct { uint32_t p1, p2, volume, unused, dsw, coin, nop; } io;   // nop: written, ignored

    int snapshot_frames;        // the game has loaded all of its code by then

    // Game RAM locations, as offsets into ram2; 0 = not known for this game,
    // and the features using them are off.
    uint32_t credits;           // credit count (32-bit)
    int credits_needed;         // credits to start with the default coinage
    uint32_t health_p1;         // current health bar, one byte per player...
    uint32_t health_step;       // ...this far apart
    int health_max;
    uint32_t timer_tick, timer_ones, timer_tens;   // round clock
    uint32_t vars, vars_len;    // range of game variables for netplay's quick check

    // Operator adjustments block on the disk; adj_len 0 = not known.
    uint32_t adj_block, adj_len, adj_difficulty;
} game_t;

extern const game_t game;
