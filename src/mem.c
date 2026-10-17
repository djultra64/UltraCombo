// Killer Instinct 2 I/O space (control register layout specific to KI2).
#include "mem.h"
#include "ide.h"
#include "sound.h"
#include <stdio.h>

uint8_t ram0[RAM0_SIZE];
uint8_t ram2[RAM2_SIZE];
uint8_t rom[ROM_SIZE];
hw_t hw = { .p1 = 0xffffffffu, .p2 = 0xffffffffu, .volume = 0xffffffffu, .dsw = 0xffffffffu };

const uint16_t *video_base(void)
{
    return (const uint16_t *)&ram0[(hw.vram_control & 4) ? 0x58000 : 0x30000];
}

static uint32_t volume_r(void)
{
    uint32_t v = hw.volume & ~2u;
    if (sound_input_empty()) v |= 2;
    return v;
}

static uint32_t io_read32(uint32_t pa)
{
    switch (pa) {
    case 0x10000080: return volume_r();
    case 0x10000088: return hw.dsw;
    case 0x10000090: return hw.p2;
    case 0x10000098: return hw.p1;
    case 0x100000a0: return 0xffffffffu;
    case 0x10000170: return ide_alt_status();
    }
    if (pa >= 0x10000100 && pa < 0x10000140) return ide_read((pa - 0x10000100) >> 3);
    static uint32_t last;
    if (pa != last) { fprintf(stderr, "io: unmapped read %08x\n", pa); last = pa; }
    return 0xffffffffu;
}

uint32_t io_read(uint32_t pa, int size)
{
    uint32_t v = io_read32(pa & ~3u);
    int sh = (pa & 3) * 8;
    if (size == 1) return (v >> sh) & 0xff;
    if (size == 2) return (v >> sh) & 0xffff;
    return v;
}

void io_write(uint32_t pa, uint32_t val, int size)
{
    uint32_t a = pa & ~3u;
    if (size != 4) {
        int sh = (pa & 3) * 8;
        val <<= sh;
    }
    switch (a) {
    case 0x10000080: {
        uint32_t prev = hw.sound_control;
        hw.sound_control = val;
        if (!(prev & 2) && (val & 2)) sound_data_w((uint16_t)hw.sound_data);
        return;
    }
    case 0x10000088: return;
    case 0x10000090: hw.sound_reset = val; sound_reset_w(val & 1); return;
    case 0x10000098: hw.vram_control = val; return;
    case 0x100000a0: hw.sound_data = val; return;
    case 0x100000b0: return;
    case 0x100000b8: hw.coin_control = val; return;
    case 0x10000170: ide_devctl(val); return;
    }
    if (a >= 0x10000100 && a < 0x10000140) { ide_write((a - 0x10000100) >> 3, val); return; }
    static uint32_t last;
    if (a != last) { fprintf(stderr, "io: unmapped write %08x = %08x\n", pa, val); last = a; }
}
