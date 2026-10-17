// Midway DCS (rev 1, "2K" external RAM) sound board: ADSP-2105 + 8 sound EPROMs.
#include "adsp.h"
#include "sound.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ROM_WORDS   0x800000u          // 16 MB 16-bit region, EPROMs on the low byte
#define BANK_WORDS  0x1000u
#define RING        65536

static uint16_t *rom;

static struct {                        // board state (saved in save states)
    uint32_t ext_pm[0x800];            // 2K x 24-bit, shared program/data RAM
    uint16_t bank;
    uint16_t input_data;
    bool     input_full;
    bool     held;                     // reset line asserted by the host
    double   rate;
} d;

static struct {                        // output to the host's audio, not machine state
    int16_t  ring[RING];
    unsigned wpos, rpos;
    bool     log;
} output;

static void boot(void)
{
    // ADSP-21xx boot format: 4 bytes per instruction (MSB first), page length in byte 3
    const uint16_t *src = &rom[(d.bank * BANK_WORDS) % ROM_WORDS];
    uint32_t *ipm = adsp_internal_pm();
    int words = ((src[3] & 0xff) + 1) * 8;
    for (int i = 0; i < words && i < 0x400; i++)
        ipm[i] = (uint32_t)(src[i * 4] & 0xff) << 16 | (uint32_t)(src[i * 4 + 1] & 0xff) << 8 | (src[i * 4 + 2] & 0xff);
}

static void board_reset(void)
{
    d.bank = 0;
    d.input_full = false;
    adsp_reset();
    adsp_set_irq(ADSP_IRQ0, false);
    adsp_set_irq(ADSP_IRQ1, false);
    adsp_set_irq(ADSP_IRQ2, false);
    boot();
}

static uint32_t pm_read(uint16_t addr)
{
    if (addr >= 0x0800 && addr < 0x2000) return d.ext_pm[addr & 0x7ff];
    return 0;
}

static void pm_write(uint16_t addr, uint32_t v)
{
    if (addr >= 0x0800 && addr < 0x2000) d.ext_pm[addr & 0x7ff] = v;
}

static uint16_t dm_read(uint16_t addr)
{
    if (addr < 0x2000) return (uint16_t)(d.ext_pm[addr & 0x7ff] >> 8);
    if (addr < 0x3000) return rom[((uint32_t)d.bank * BANK_WORDS + (addr - 0x2000)) % ROM_WORDS];
    if (addr >= 0x3400 && addr < 0x3800) {
        // reading the input latch acknowledges it
        d.input_full = false;
        adsp_set_irq(ADSP_IRQ2, false);
        return d.input_data;
    }
    return 0xffff;
}

static void dm_write(uint16_t addr, uint16_t v)
{
    if (addr < 0x2000) {
        uint32_t *p = &d.ext_pm[addr & 0x7ff];
        *p = ((uint32_t)v << 8) | (*p & 0xff);
        return;
    }
    if (addr >= 0x3000 && addr < 0x3400) { d.bank = v & 0x7ff; return; }
    // 0x3400-0x37ff output latch: KI2 has no read-back path to the host
}

static void sample_out(int16_t s)
{
    output.ring[output.wpos++ % RING] = s;
    if (output.wpos - output.rpos > RING) output.rpos = output.wpos - RING;
}

static void sample_rate(double hz)
{
    if (output.log && hz != d.rate) fprintf(stderr, "dcs: sample rate %.1f Hz\n", hz);
    d.rate = hz;
}

static void reboot(void)
{
    adsp_reset();
    boot();
}

static bool load_rom(const char *dir, const char *name, uint32_t word_offset)
{
    char path[4096];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); return false; }
    uint8_t *buf = malloc(0x80000);
    size_t n = fread(buf, 1, 0x80000, f);
    fclose(f);
    for (size_t i = 0; i < n; i++) rom[word_offset + i] = 0xff00 | buf[i];
    free(buf);
    return n == 0x80000;
}

bool sound_init(const char *rom_dir)
{
    static const char *names[8] = {
        "ki2_l1.u10", "ki2_l1.u11", "ki2_l1.u12", "ki2_l1.u13",
        "ki2_l1.u33", "ki2_l1.u34", "ki2_l1.u35", "ki2_l1.u36",
    };
    output.log = getenv("KI2_LOG_SOUND") != NULL;
    rom = malloc(ROM_WORDS * sizeof(uint16_t));
    for (uint32_t i = 0; i < ROM_WORDS; i++) rom[i] = 0xffff;
    bool ok = true;
    for (int i = 0; i < 8; i++)
        ok &= load_rom(rom_dir, names[i], (uint32_t)i * 0x100000u);
    if (!ok) fprintf(stderr, "dcs: sound ROMs missing, running silent\n");

    adsp_hooks_t h = {
        .pm_read = pm_read, .pm_write = pm_write,
        .dm_read = dm_read, .dm_write = dm_write,
        .sample_out = sample_out, .sample_rate = sample_rate,
        .sysctl_reboot = reboot,
    };
    adsp_init(&h);
    d.rate = 31250;
    board_reset();
    return ok;
}

void sound_reset_w(bool state)
{
    if (!state) {
        if (!d.held && output.log) fprintf(stderr, "dcs: reset asserted\n");
        d.held = true;
        board_reset();
    } else d.held = false;
}

void sound_data_w(uint16_t data)
{
    if (output.log) fprintf(stderr, "dcs: cmd %04x\n", data);
    d.input_data = data;
    d.input_full = true;
    adsp_set_irq(ADSP_IRQ2, true);
}

bool sound_input_empty(void) { return !d.input_full; }

size_t sound_state(uint8_t *buf, bool load)
{
    size_t n = sizeof d;
    if (buf && load) memcpy(&d, buf, sizeof d);
    else if (buf) memcpy(buf, &d, sizeof d);
    return n + adsp_state(buf ? buf + n : NULL, load);
}

void sound_run(int cycles)
{
    if (!d.held) adsp_run(cycles);
}

double sound_rate(void) { return d.rate; }

int sound_fetch(int16_t *out, int max)
{
    int n = 0;
    while (n < max && output.rpos != output.wpos) out[n++] = output.ring[output.rpos++ % RING];
    return n;
}
