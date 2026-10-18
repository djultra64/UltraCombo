// UltraCombo: Killer Instinct (MAME kinst). RAM locations not found yet are
// 0, which turns the features using them off.
#include "game.h"

const game_t game = {
    .id = "ki1",
    .title = "Killer Instinct",
    .product = "UltraCombo",
    .file_name = "UltraCombo",
    .icon_text = "UC",

    .versions = { { "1.5d", "ki-l15d.u98" }, { "1.4", "ki-l14.u98" } },
    .sound_roms = { "u10-l1", "u11-l1", "u12-l1", "u13-l1",
                    "u33-l1", "u34-l1", "u35-l1", "u36-l1" },
    .disk = "kinst.raw",
    .chd = "kinst.chd",
    .cyls = 419, .heads = 13, .secs = 47,
    .serial_offset = 27,
    .io = { .p1 = 0x10000080, .p2 = 0x10000088, .volume = 0x10000090,
            .unused = 0x10000098, .dsw = 0x100000a0, .coin = 0x100000b0 },

    .snapshot_frames = 900,
};
