// UltraCombo: Killer Instinct (MAME kinst). RAM locations not found yet are
// 0, which turns the features using them off.
#include "game.h"

const game_t game = {
    .id = "ki1",
    .title = "Killer Instinct",
    .product = "UltraCombo",
    .file_name = "UltraCombo",
    .icon_text = "UC",

    .versions = { { "1.5d", "ki-l15d.u98", 0x7b65ca3d }, { "1.4", "ki-l14.u98", 0xafedb75f } },
    .sound_roms = { "u10-l1", "u11-l1", "u12-l1", "u13-l1",
                    "u33-l1", "u34-l1", "u35-l1", "u36-l1" },
    .sound_crc = { 0xb6cc155f, 0x0b5e05df, 0xd05ce6ad, 0x7d0954ea,
                   0x8bbe4f0c, 0xb2e73603, 0x0aaef4fc, 0x0577bb60 },
    .set = "kinst",
    .disk = "kinst.raw",
    .chd = "kinst.chd",
    .chd_sha1 = "81d833236e994528d1482979261401b198d1ca53",
    .cyls = 419, .heads = 13, .secs = 47,
    .serial_offset = 27,
    .io = { .p1 = 0x10000080, .p2 = 0x10000088, .volume = 0x10000090,
            .unused = 0x10000098, .dsw = 0x100000a0, .coin = 0x100000b0 },

    .snapshot_frames = 900,
};
