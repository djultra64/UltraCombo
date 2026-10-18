// UltraCombo 2: Killer Instinct 2 (MAME kinst2). RAM locations are for the
// L1.4 code and were found by diffing deterministic runs.
#include "game.h"

const game_t game = {
    .id = "ki2",
    .title = "Killer Instinct 2",
    .product = "UltraCombo 2",
    .file_name = "UltraCombo2",
    .icon_text = "UC2",

    .versions = { { "1.4", "ki2-l14.u98" } },
    .sound_roms = { "ki2_l1.u10", "ki2_l1.u11", "ki2_l1.u12", "ki2_l1.u13",
                    "ki2_l1.u33", "ki2_l1.u34", "ki2_l1.u35", "ki2_l1.u36" },
    .disk = "kinst2.raw",
    .chd = "kinst2.chd",
    .cyls = 1463, .heads = 13, .secs = 47,
    .serial_offset = 11,
    .io = { .p1 = 0x10000098, .p2 = 0x10000090, .volume = 0x10000080,
            .unused = 0x100000a0, .dsw = 0x10000088, .coin = 0x100000b8, .nop = 0x100000b0 },

    .snapshot_frames = 900,

    .credits = 0x7ff124, .credits_needed = 2,
    .health_p1 = 0x7fc105, .health_step = 0x100, .health_max = 120,
    .timer_tick = 0x7f7a10, .timer_ones = 0x7f7a11, .timer_tens = 0x7f7a12,
    .vars = 0x7f0000, .vars_len = 0x10000,

    // checksum of the 88 bytes after it, computed by the game at 0x8802d400;
    // +4 is set once the adjustments were edited
    .adj_block = 0xa00, .adj_len = 88, .adj_difficulty = 0xa1c,
};
