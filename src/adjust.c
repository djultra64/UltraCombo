// Operator adjustments on disk (see adjust.h). The block starts at disk
// offset 0xa00 with a checksum of the following 88 bytes, computed by the
// game's routine at 0x8802d400; 0xa04 is set once adjustments were edited,
// 0xa1c holds the difficulty as 0-4.
#include "adjust.h"
#include "ide.h"
#include <stdint.h>
#include <string.h>

#define ADJ_BLOCK 0xa00
#define ADJ_LEN 88
#define ADJ_EDITED (ADJ_BLOCK + 4)
#define ADJ_DIFFICULTY 0xa1c

// The game's checksum: a 33-bit rotate/xor shift register fed with each byte.
static uint32_t ki2_checksum(const uint8_t *p, int n)
{
    uint64_t t = 1;
    for (int i = 0; i < n; i++) {
        uint64_t x = ((t & 1) << 32) | ((t >> 1) & 0xffffffffu);
        x ^= (t << 12) & 0xffffffffu;
        t = x ^ ((x >> 20) & 0xfff) ^ p[i];
    }
    return (uint32_t)t;
}

static bool block_valid(const uint8_t *d)
{
    uint32_t stored;
    memcpy(&stored, d + ADJ_BLOCK, 4);
    return stored == ki2_checksum(d + ADJ_BLOCK + 4, ADJ_LEN);
}

int adjust_difficulty(void)
{
    const uint8_t *d = ide_tracked();
    if (!d || !block_valid(d) || d[ADJ_DIFFICULTY] > 4) return -1;
    return d[ADJ_DIFFICULTY] + 1;
}

bool adjust_set_difficulty(int level)
{
    uint8_t *d = ide_tracked();
    if (!d || level < 1 || level > 5 || !block_valid(d)) return false;
    if (d[ADJ_DIFFICULTY] == level - 1) return true;
    d[ADJ_DIFFICULTY] = (uint8_t)(level - 1);
    d[ADJ_EDITED] = 1;
    uint32_t sum = ki2_checksum(d + ADJ_BLOCK + 4, ADJ_LEN);
    memcpy(d + ADJ_BLOCK, &sum, 4);
    ide_dirty[ADJ_BLOCK >> 12] = 1;      // save states and rewind see the change
    ide_tracked_changed();               // and it gets written back to the image
    return true;
}
